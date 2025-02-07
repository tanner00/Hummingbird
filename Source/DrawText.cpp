#include "DrawText.hpp"
#include "DDS.hpp"
#include "JSON.hpp"

static constexpr usize MaxCharactersPerFrame = 2048;

DrawText::DrawText()
	: Glyphs(64)
	, Ascender(0.0f)
	, RootConstants()
	, CharacterIndex(0)
	, Device(nullptr)
{
}

void DrawText::Init(GpuDevice* device)
{
	Device = device;

	DdsImage fontImage = LoadDdsImage("Assets/Fonts/RobotoMSDF.dds"_view);

	const JsonObject fontDescription = LoadJson("Assets/Fonts/RobotoMSDF.json"_view);

	const JsonObject& fontAtlasDescription = fontDescription["atlas"_view].GetObject();

	const double distanceRange = fontAtlasDescription["distanceRange"_view].GetDecimal();
	const uint32 width = static_cast<uint32>(fontAtlasDescription["width"_view].GetDecimal());
	const uint32 height = static_cast<uint32>(fontAtlasDescription["height"_view].GetDecimal());

	const JsonObject& fontMetrics = fontDescription["metrics"_view].GetObject();
	Ascender = static_cast<float>(fontMetrics["ascender"_view].GetDecimal());

	RootConstants.UnitRange.X = static_cast<float>(distanceRange / width);
	RootConstants.UnitRange.Y = static_cast<float>(distanceRange / height);

	const JsonArray& fontGlyphs = fontDescription["glyphs"_view].GetArray();

	for (const JsonValue& glyphValue : fontGlyphs)
	{
		const JsonObject& glyphObject = glyphValue.GetObject();

		const float advance = static_cast<float>(glyphObject["advance"_view].GetDecimal());
		Float2 atlasPosition = { 0.0f, 0.0f };
		Float2 atlasSize = { 0.0f, 0.0f };
		Float2 planePosition = { 0.0f, 0.0f };
		Float2 planeSize = { 0.0f, 0.0f };

		if (glyphObject.HasKey("atlasBounds"_view))
		{
			const JsonObject& atlasBounds = glyphObject["atlasBounds"_view].GetObject();

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
			const JsonObject& planeBounds = glyphObject["planeBounds"_view].GetObject();

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

	FontTexture = Device->CreateTexture("Font"_view, BarrierLayout::GraphicsQueueCommon,
	{
		.Width = fontImage.Width,
		.Height = fontImage.Height,
		.Type = TextureType::Rectangle,
		.Format = fontImage.Format,
		.MipMapCount = fontImage.MipMapCount,
	});
	Device->Write(FontTexture, fontImage.Data);

	UnloadDdsImage(&fontImage);

	Shader vertex = Device->CreateShader(
	{
		.Stage = ShaderStage::Vertex,
		.FilePath = "Shaders/Text.hlsl"_view,
	});
	Shader pixel = Device->CreateShader(
	{
		.Stage = ShaderStage::Pixel,
		.FilePath = "Shaders/Text.hlsl"_view,
	});

	ShaderStages stages;
	stages.AddStage(vertex);
	stages.AddStage(pixel);
	Pipeline = Device->CreatePipeline("Text Pipeline"_view,
	{
		.Stages = Move(stages),
		.RenderTargetFormat = TextureFormat::Rgba8SrgbUnorm,
		.DepthFormat = TextureFormat::None,
		.AlphaBlend = true,
	});
	Device->DestroyShader(&vertex);
	Device->DestroyShader(&pixel);

	Sampler = Device->CreateSampler(
	{
		.MinificationFilter = SamplerFilter::Linear,
		.MagnificationFilter = SamplerFilter::Linear,
		.HorizontalAddress = SamplerAddress::Wrap,
		.VerticalAddress = SamplerAddress::Wrap,
	});

	CharacterData.GrowToLengthUninitialized(MaxCharactersPerFrame);
	CharacterBuffer = Device->CreateBuffer("Character Buffer"_view,
	{
		.Type = BufferType::StructuredBuffer,
		.Usage = BufferUsage::Stream,
		.Size = MaxCharactersPerFrame * sizeof(Hlsl::Character),
		.Stride = sizeof(Hlsl::Character),
	});
}

void DrawText::Shutdown()
{
	Device->DestroySampler(&Sampler);
	Device->DestroyPipeline(&Pipeline);
	Device->DestroyTexture(&FontTexture);
	Device->DestroyBuffer(&CharacterBuffer);

	this->~DrawText();
}

void DrawText::Draw(StringView text, Float2 position, Float3 rgb, float scale)
{
	Draw(text, position, Float4 { rgb.X, rgb.Y, rgb.Z, 1.0f }, scale);
}

void DrawText::Draw(StringView text, Float2 position, Float4 rgba, float scale)
{
	if (CharacterIndex + text.GetLength() > MaxCharactersPerFrame)
	{
		CharacterIndex = 0;
	}

	Float2 currentPosition = { position.X, position.Y - scale * Ascender };
	for (usize i = 0; i < text.GetLength(); ++i)
	{
		const Glyph& glyph = Glyphs[text[i]];

		CharacterData[CharacterIndex] = Hlsl::Character
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

void DrawText::Submit(GraphicsContext* graphics, uint32 width, uint32 height)
{
	CHECK(graphics);

	RootConstants.ViewProjection = Matrix::Orthographic(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height), 0.0f, 1.0f);

	RootConstants.CharacterBuffer = Device->Get(CharacterBuffer);
	RootConstants.Texture = Device->Get(FontTexture);
	RootConstants.Sampler = Device->Get(Sampler);

	Device->Write(CharacterBuffer, CharacterData.GetData());

	graphics->SetPipeline(&Pipeline);

	graphics->SetRootConstants(&RootConstants);

	static constexpr usize verticesPerQuad = 6;
	graphics->Draw(CharacterIndex * verticesPerQuad);

	CharacterIndex = 0;
}
