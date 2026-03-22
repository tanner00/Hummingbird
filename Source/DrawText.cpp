#include "DrawText.hpp"
#include "DDS.hpp"
#include "JSON.hpp"
#include "RenderContext.hpp"
#include "ResourceUploader.hpp"

static constexpr usize MaxCharactersPerFrame = 2048;

void DrawText::Init()
{
	DDS::Image fontImage = DDS::LoadImage("Assets/Fonts/RobotoMSDF.dds"_view);

	const JSON::Object fontDescription = JSON::Load("Assets/Fonts/RobotoMSDF.json"_view);

	const JSON::Object& fontAtlasDescription = fontDescription["atlas"_view].GetObject();

	const double distanceRange = fontAtlasDescription["distanceRange"_view].GetDecimal();
	const uint32 width = static_cast<uint32>(fontAtlasDescription["width"_view].GetDecimal());
	const uint32 height = static_cast<uint32>(fontAtlasDescription["height"_view].GetDecimal());

	const JSON::Object& fontMetrics = fontDescription["metrics"_view].GetObject();
	Ascender = static_cast<float>(fontMetrics["ascender"_view].GetDecimal());

	RootConstants.UnitRange.X = static_cast<float>(distanceRange / width);
	RootConstants.UnitRange.Y = static_cast<float>(distanceRange / height);

	const JSON::Array& fontGlyphs = fontDescription["glyphs"_view].GetArray();

	for (const JSON::Value& glyphValue : fontGlyphs)
	{
		const JSON::Object& glyphObject = glyphValue.GetObject();

		const float advance = static_cast<float>(glyphObject["advance"_view].GetDecimal());
		Float2 atlasPosition = { .X = 0.0f, .Y = 0.0f };
		Float2 atlasSize = { .X = 0.0f, .Y = 0.0f };
		Float2 planePosition = { .X = 0.0f, .Y = 0.0f };
		Float2 planeSize = { .X = 0.0f, .Y = 0.0f };

		if (glyphObject.HasKey("atlasBounds"_view))
		{
			const JSON::Object& atlasBounds = glyphObject["atlasBounds"_view].GetObject();

			const double left = atlasBounds["left"_view].GetDecimal();
			const double bottom = atlasBounds["bottom"_view].GetDecimal();
			const double right = atlasBounds["right"_view].GetDecimal();
			const double top = atlasBounds["top"_view].GetDecimal();

			atlasPosition.X = static_cast<float>(left) / static_cast<float>(fontImage.Width);
			atlasPosition.Y = static_cast<float>(top) / static_cast<float>(fontImage.Height);

			atlasSize.X = static_cast<float>(right - left) / static_cast<float>(fontImage.Width);
			atlasSize.Y = static_cast<float>(bottom - top) / static_cast<float>(fontImage.Height);
		}

		if (glyphObject.HasKey("planeBounds"_view))
		{
			const JSON::Object& planeBounds = glyphObject["planeBounds"_view].GetObject();

			const double left = planeBounds["left"_view].GetDecimal();
			const double bottom = planeBounds["bottom"_view].GetDecimal();
			const double right = planeBounds["right"_view].GetDecimal();
			const double top = planeBounds["top"_view].GetDecimal();

			planePosition.X = static_cast<float>(left);
			planePosition.Y = static_cast<float>(top);

			planeSize.X = static_cast<float>(right - left);
			planeSize.Y = static_cast<float>(bottom - top);
		}

		const char codepoint = static_cast<char>(glyphObject["unicode"_view].GetDecimal());
		Glyphs.Add(codepoint, Glyph
		{
			.AtlasPosition = atlasPosition,
			.AtlasSize = atlasSize,
			.PlanePosition = planePosition,
			.PlaneSize = planeSize,
			.Advance = advance,
		});
	}

	FontTexture = ResourceUploader::Get().Upload(ResourceLifetime::Persistent, fontImage.Data,
	{
		.Type = RHI::ResourceType::Texture2D,
		.Format = fontImage.Format,
		.Flags = RHI::ResourceFlags::None,
		.InitialLayout = RHI::BarrierLayout::GraphicsQueueCommon,
		.Dimensions = { fontImage.Width, fontImage.Height },
		.MipMapCount = fontImage.MipMapCount,
		.Name = String("Font Texture"_view),
	});
	FontTextureView = GlobalDevice().Create(
	{
		.Type = RHI::ViewType::ShaderResource,
		.Resource = FontTexture,
	});

	DDS::UnloadImage(&fontImage);

	RHI::Shader vertex = GlobalDevice().Create(
	{
		.Stage = RHI::ShaderStage::Vertex,
		.FilePath = String("Shaders/Text.hlsl"_view),
	});
	RHI::Shader pixel = GlobalDevice().Create(
	{
		.Stage = RHI::ShaderStage::Pixel,
		.FilePath = String("Shaders/Text.hlsl"_view),
	});

	Pipeline = GlobalDevice().Create(
	{
		.VertexStage = vertex,
		.PixelStage = pixel,
		.RenderTargetFormats = { RHI::ResourceFormat::RGBA8UNormSRGB },
		.DepthStencilFormat = RHI::ResourceFormat::None,
		.AlphaBlend = true,
		.Name = String("Text Pipeline"_view),
	});
	GlobalDevice().Destroy(&vertex);
	GlobalDevice().Destroy(&pixel);

	CharacterData.GrowToLengthUninitialized(MaxCharactersPerFrame);
	for (usize frameIndex = 0; frameIndex < RHI::FramesInFlight; ++frameIndex)
	{
		CharacterBuffers[frameIndex] = GlobalDevice().Create(
		{
			.Format = RHI::ResourceFormat::None,
			.Flags = RHI::ResourceFlags::Upload,
			.InitialLayout = RHI::BarrierLayout::Undefined,
			.Size = MaxCharactersPerFrame * sizeof(HLSL::Character),
			.Name = String("Character Buffer"_view),
		});
		CharacterBufferViews[frameIndex] = GlobalDevice().Create(RHI::BufferViewDescription
		{
			.Type = RHI::ViewType::ShaderResource,
			.Buffer =
			{
				.Resource = CharacterBuffers[frameIndex],
				.Size = CharacterBuffers[frameIndex].Size,
				.Stride = sizeof(HLSL::Character),
			},
		});
	}
}

void DrawText::Shutdown()
{
	GlobalDevice().Destroy(&FontTexture);
	GlobalDevice().Destroy(&FontTextureView);
	GlobalDevice().Destroy(&Pipeline);
	for (usize frameIndex = 0; frameIndex < RHI::FramesInFlight; ++frameIndex)
	{
		GlobalDevice().Destroy(&CharacterBufferViews[frameIndex]);
		GlobalDevice().Destroy(&CharacterBuffers[frameIndex]);
	}

	this->~DrawText();
}

void DrawText::Draw(StringView text, Float2 position, Float3 rgb, float scale)
{
	Draw(text, position, Float4 { .R = rgb.R, .G = rgb.G, .B = rgb.B, .A = 1.0f }, scale);
}

void DrawText::Draw(StringView text, Float2 position, Float4 rgba, float scale)
{
	CHECK(CharacterIndex + text.GetLength() <= MaxCharactersPerFrame);

	Float2 currentPositionScreen = { .X = position.X, .Y = position.Y - scale * Ascender };
	for (usize textIndex = 0; textIndex < text.GetLength(); ++textIndex)
	{
		const Glyph& glyph = Glyphs[text[textIndex]];

		CharacterData[CharacterIndex] = HLSL::Character
		{
			.Color = rgba,
			.PositionScreen = currentPositionScreen,
			.AtlasPosition = glyph.AtlasPosition,
			.AtlasSize = glyph.AtlasSize,
			.PlanePosition = glyph.PlanePosition,
			.PlaneSize = glyph.PlaneSize,
			.Scale = scale,
		};

		currentPositionScreen.X += glyph.Advance * scale;
		++CharacterIndex;
	}
}

void DrawText::Submit(uint32 width, uint32 height)
{
	RootConstants.ScreenToClip = Matrix::Orthographic(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height), 0.0f, 1.0f);

	RootConstants.CharacterBufferIndex = GlobalDevice().Get(CharacterBufferViews[GlobalDevice().GetFrameIndex()]);
	RootConstants.FontTextureIndex = GlobalDevice().Get(FontTextureView);
	RootConstants.LinearWrapSampler = GlobalDevice().Get(RenderContext.LinearWrapSampler);

	GlobalDevice().Write(&CharacterBuffers[GlobalDevice().GetFrameIndex()], CharacterData.GetData());

	GlobalGraphics().SetPipeline(Pipeline);
	GlobalGraphics().SetRootConstants(&RootConstants);

	static constexpr usize verticesPerQuad = 6;
	GlobalGraphics().Draw(CharacterIndex * verticesPerQuad);

	CharacterIndex = 0;
}
