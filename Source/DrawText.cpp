#include "DrawText.hpp"
#include "DDS.hpp"
#include "JSON.hpp"

static constexpr usize MaxCharactersPerFrame = 2048;

DrawText::DrawText()
	: Glyphs(64, &GlobalAllocator::Get())
	, Ascender(0.0f)
	, RootConstants()
	, CharacterIndex(0)
{
}

void DrawText::Init(RHI::Device* device)
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

	FontTexture = device->Create(
	{
		.Type = RHI::ResourceType::Texture2D,
		.Format = fontImage.Format,
		.Flags = RHI::ResourceFlags::None,
		.InitialLayout = RHI::BarrierLayout::GraphicsQueueCommon,
		.Dimensions = { fontImage.Width, fontImage.Height } ,
		.MipMapCount = fontImage.MipMapCount,
		.Name = "Font Texture"_view,
	});
	FontTextureView = device->Create(
	{
		.Resource = FontTexture,
		.Type = RHI::ViewType::ShaderResource,
		.Format = FontTexture.Format,
		.MipMapCount = fontImage.MipMapCount,
	});
	device->Write(&FontTexture, fontImage.Data);

	DDS::UnloadImage(&fontImage);

	RHI::Shader vertex = device->Create(
	{
		.FilePath = "Shaders/Text.hlsl"_view,
		.Stage = RHI::ShaderStage::Vertex,
	});
	RHI::Shader pixel = device->Create(
	{
		.FilePath = "Shaders/Text.hlsl"_view,
		.Stage = RHI::ShaderStage::Pixel,
	});

	RHI::ShaderStages stages;
	stages.AddStage(vertex);
	stages.AddStage(pixel);
	Pipeline = device->Create(
	{
		.Stages = Move(stages),
		.RenderTargetFormats = { RHI::ResourceFormat::RGBA8UNormSRGB },
		.DepthStencilFormat = RHI::ResourceFormat::None,
		.AlphaBlend = true,
		.Name = "Text Pipeline"_view,
	});
	device->Destroy(&vertex);
	device->Destroy(&pixel);

	LinearWrapSampler = device->Create(
	{
		.MinificationFilter = RHI::SamplerFilter::Linear,
		.MagnificationFilter = RHI::SamplerFilter::Linear,
		.HorizontalAddress = RHI::SamplerAddress::Wrap,
		.VerticalAddress = RHI::SamplerAddress::Wrap,
	});

	CharacterData.GrowToLengthUninitialized(MaxCharactersPerFrame);
	for (usize i = 0; i < RHI::FramesInFlight; ++i)
	{
		CharacterBuffers[i] = device->Create(
		{
			.Format = RHI::ResourceFormat::None,
			.Flags = RHI::ResourceFlags::Upload,
			.InitialLayout = RHI::BarrierLayout::Undefined,
			.Size = MaxCharactersPerFrame * sizeof(HLSL::Character),
			.Name = "Character Buffer"_view,
		});
		CharacterBufferViews[i] = device->Create(RHI::BufferViewDescription
		{
			.Type = RHI::ViewType::ShaderResource,
			.Buffer =
			{
				.Resource = CharacterBuffers[i],
				.Size = CharacterBuffers[i].Size,
				.Stride = sizeof(HLSL::Character),
			},
		});
	}
}

void DrawText::Shutdown(const RHI::Device& device)
{
	for (usize i = 0; i < RHI::FramesInFlight; ++i)
	{
		device.Destroy(&CharacterBufferViews[i]);
		device.Destroy(&CharacterBuffers[i]);
	}
	device.Destroy(&LinearWrapSampler);
	device.Destroy(&Pipeline);
	device.Destroy(&FontTextureView);
	device.Destroy(&FontTexture);

	this->~DrawText();
}

void DrawText::Draw(StringView text, Float2 position, Float3 rgb, float scale)
{
	Draw(text, position, { .R = rgb.R, .G = rgb.G, .B = rgb.B, .A = 1.0f }, scale);
}

void DrawText::Draw(StringView text, Float2 position, Float4 rgba, float scale)
{
	if (CharacterIndex + text.GetLength() > MaxCharactersPerFrame)
	{
		CharacterIndex = 0;
	}

	Float2 currentPosition = { .X = position.X, .Y = position.Y - scale * Ascender };
	for (usize i = 0; i < text.GetLength(); ++i)
	{
		const Glyph& glyph = Glyphs[text[i]];

		CharacterData[CharacterIndex] = HLSL::Character
		{
			.Color = rgba,
			.ScreenPosition = currentPosition,
			.AtlasPosition = glyph.AtlasPosition,
			.AtlasSize = glyph.AtlasSize,
			.PlanePosition = glyph.PlanePosition,
			.PlaneSize = glyph.PlaneSize,
			.Scale = scale,
		};

		currentPosition.X += glyph.Advance * scale;
		++CharacterIndex;
	}
}

void DrawText::Submit(RHI::GraphicsContext* graphics, RHI::Device* device, uint32 width, uint32 height)
{
	CHECK(graphics);

	RootConstants.ViewProjection = Matrix::Orthographic(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height), 0.0f, 1.0f);

	RootConstants.CharacterBufferIndex = device->Get(CharacterBufferViews[device->GetFrameIndex()]);
	RootConstants.FontTextureIndex = device->Get(FontTextureView);
	RootConstants.LinearWrapSampler = device->Get(LinearWrapSampler);

	device->Write(&CharacterBuffers[device->GetFrameIndex()], CharacterData.GetData());

	graphics->SetPipeline(Pipeline);

	graphics->SetRootConstants(&RootConstants);

	static constexpr usize verticesPerQuad = 6;
	graphics->Draw(CharacterIndex * verticesPerQuad);

	CharacterIndex = 0;
}
