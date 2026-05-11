#include "UI.hpp"
#include "DDS.hpp"
#include "JSON.hpp"
#include "RenderContext.hpp"
#include "ResourceUploader.hpp"

#include "Luft/Sort.hpp"

namespace HLSL
{
#include "Shaders/Types.hlsli"
}

using namespace RHI;

namespace UI
{

static ::Allocator* Allocator = &GlobalAllocator::Get();

static constexpr usize MaxUIDrawsPerFrame = 8192;

struct ElementStorage
{
	ID ParentID;
	Array<ID> ChildrenIDs;

	float32x2 PositionSS;
	float32x2 SizeSS;

	usize Layer;

	Description Description;

	String Text;
	float32 TextScale;

	TextureView Image;
};

static HashTable<ID, ElementStorage> Elements(32, Allocator);
static Array<ID> RootElementIDs(Allocator);

static HashTable<ID, ElementStorage> LastFrameElements(32, Allocator);
static Array<ID> LastFrameRootElementIDs(Allocator);

static ID OpenElementID = INDEX_NONE;

namespace Font
{

struct Glyph
{
	float32x2 AtlasPosition;
	float32x2 AtlasSize;
	float32x2 PlanePosition;
	float32x2 PlaneSize;
	float32 Advance;
};

static HashTable<char, Glyph> Glyphs(64, Allocator);

static float32 Em = 0.0f;
static float32 LineHeight = 0.0f;
static float32 Ascender = 0.0f;
static float32 Descender = 0.0f;

static Resource Resource = Resource::Invalid();
static TextureView TextureView = TextureView::Invalid();

}

static HLSL::UIRootConstants RootConstants = {};

static usize DrawIndex = 0;
static Array<HLSL::UIDraw> Draws(Allocator);

static Resource DrawBufferResources[FramesInFlight] = {};
static BufferView DrawBufferViews[FramesInFlight] = {};

static GraphicsPipeline Pipeline = GraphicsPipeline::Invalid();

void Init()
{
	DDS::Image fontImage = DDS::LoadImage("Assets/Fonts/RobotoMSDF.dds"_view);

	const JSON::Object fontDescription = JSON::Load("Assets/Fonts/RobotoMSDF.json"_view);

	const JSON::Object& fontAtlasDescription = fontDescription["atlas"_view].GetObject();

	const float64 distanceRange = fontAtlasDescription["distanceRange"_view].GetDecimal();
	const uint32 width = static_cast<uint32>(fontAtlasDescription["width"_view].GetDecimal());
	const uint32 height = static_cast<uint32>(fontAtlasDescription["height"_view].GetDecimal());

	const JSON::Object& fontMetrics = fontDescription["metrics"_view].GetObject();
	Font::Em = static_cast<float32>(fontMetrics["emSize"_view].GetDecimal());
	Font::LineHeight = static_cast<float32>(fontMetrics["lineHeight"_view].GetDecimal());
	Font::Ascender = static_cast<float32>(fontMetrics["ascender"_view].GetDecimal());
	Font::Descender = static_cast<float32>(fontMetrics["descender"_view].GetDecimal());

	RootConstants.UnitRange.X = static_cast<float32>(distanceRange / width);
	RootConstants.UnitRange.Y = static_cast<float32>(distanceRange / height);

	const JSON::Array& fontGlyphs = fontDescription["glyphs"_view].GetArray();

	for (const JSON::Value& glyphValue : fontGlyphs)
	{
		const JSON::Object& glyphObject = glyphValue.GetObject();

		const float32 advance = static_cast<float32>(glyphObject["advance"_view].GetDecimal());
		float32x2 atlasPosition = { .X = 0.0f, .Y = 0.0f };
		float32x2 atlasSize = { .X = 0.0f, .Y = 0.0f };
		float32x2 planePosition = { .X = 0.0f, .Y = 0.0f };
		float32x2 planeSize = { .X = 0.0f, .Y = 0.0f };

		if (glyphObject.HasKey("atlasBounds"_view))
		{
			const JSON::Object& atlasBounds = glyphObject["atlasBounds"_view].GetObject();

			const float64 left = atlasBounds["left"_view].GetDecimal();
			const float64 bottom = atlasBounds["bottom"_view].GetDecimal();
			const float64 right = atlasBounds["right"_view].GetDecimal();
			const float64 top = atlasBounds["top"_view].GetDecimal();

			atlasPosition.X = static_cast<float32>(left) / static_cast<float32>(fontImage.Width);
			atlasPosition.Y = static_cast<float32>(top) / static_cast<float32>(fontImage.Height);

			atlasSize.X = static_cast<float32>(right - left) / static_cast<float32>(fontImage.Width);
			atlasSize.Y = static_cast<float32>(bottom - top) / static_cast<float32>(fontImage.Height);
		}

		if (glyphObject.HasKey("planeBounds"_view))
		{
			const JSON::Object& planeBounds = glyphObject["planeBounds"_view].GetObject();

			const float64 left = planeBounds["left"_view].GetDecimal();
			const float64 bottom = planeBounds["bottom"_view].GetDecimal();
			const float64 right = planeBounds["right"_view].GetDecimal();
			const float64 top = planeBounds["top"_view].GetDecimal();

			planePosition.X = static_cast<float32>(left);
			planePosition.Y = static_cast<float32>(top);

			planeSize.X = static_cast<float32>(right - left);
			planeSize.Y = static_cast<float32>(bottom - top);
		}

		const char codepoint = static_cast<char>(glyphObject["unicode"_view].GetDecimal());
		Font::Glyphs.Add(codepoint, Font::Glyph
		{
			.AtlasPosition = atlasPosition,
			.AtlasSize = atlasSize,
			.PlanePosition = planePosition,
			.PlaneSize = planeSize,
			.Advance = advance,
		});
	}

	Font::Resource = ResourceUploader::Upload(ResourceUploader::Lifetime::Persistent, fontImage.Data,
	{
		.Type = ResourceType::Texture2D,
		.Format = fontImage.Format,
		.Flags = ResourceFlags::None,
		.InitialLayout = BarrierLayout::GraphicsQueueCommon,
		.Dimensions = { fontImage.Width, fontImage.Height },
		.MipMapCount = fontImage.MipMapCount,
		.Name = String("Font Texture"_view, Allocator),
	});
	Font::TextureView = GlobalDevice().Create(
	{
		.Type = ViewType::ShaderResource,
		.Resource = Font::Resource,
	});

	DDS::UnloadImage(&fontImage);

	Draws.AddUninitialized(MaxUIDrawsPerFrame);
	for (usize frameIndex = 0; frameIndex < FramesInFlight; ++frameIndex)
	{
		DrawBufferResources[frameIndex] = GlobalDevice().Create(
		{
			.Format = ResourceFormat::None,
			.Flags = ResourceFlags::Upload,
			.InitialLayout = BarrierLayout::Undefined,
			.Size = MaxUIDrawsPerFrame * sizeof(HLSL::UIDraw),
			.Name = String("UI Draw Buffer"_view, Allocator),
		});
		DrawBufferViews[frameIndex] = GlobalDevice().Create(
		{
			.Type = ViewType::ShaderResource,
			.Buffer = Buffer
			{
				.Resource = DrawBufferResources[frameIndex],
				.Size = DrawBufferResources[frameIndex].Size,
				.Stride = sizeof(HLSL::UIDraw),
			},
		});
	}
}

void Shutdown()
{
	GlobalDevice().Destroy(&Font::Resource);
	GlobalDevice().Destroy(&Font::TextureView);

	for (usize frameIndex = 0; frameIndex < FramesInFlight; ++frameIndex)
	{
		GlobalDevice().Destroy(&DrawBufferResources[frameIndex]);
		GlobalDevice().Destroy(&DrawBufferViews[frameIndex]);
	}
}

void CreatePipeline()
{
	Shader vertex = GlobalDevice().Create(
	{
		.Stage = ShaderStage::Vertex,
		.FilePath = String("Shaders/UI.hlsl"_view, Allocator),
	});
	Shader pixel = GlobalDevice().Create(
	{
		.Stage = ShaderStage::Pixel,
		.FilePath = String("Shaders/UI.hlsl"_view, Allocator),
	});
	Pipeline = GlobalDevice().Create(
	{
		.VertexStage = vertex,
		.PixelStage = pixel,
		.RenderTargetFormats = { ResourceFormat::RGBA8UNormSRGB },
		.DepthStencilFormat = ResourceFormat::None,
		.AlphaBlend = true,
		.Name = String("UI Pipeline"_view, Allocator),
	});
	GlobalDevice().Destroy(&vertex);
	GlobalDevice().Destroy(&pixel);
}

void DestroyPipeline()
{
	GlobalDevice().Destroy(&Pipeline);
}

void DrawRectangle(float32x2 positionSS, float32x2 sizeSS, float32x3 rgb, usize layer)
{
	DrawBorderedRoundedRectangle(positionSS, sizeSS, 0.0f, 0.0f, float32x4 { rgb.X, rgb.Y, rgb.Z, 1.0f }, float32x4 {}, layer);
}

void DrawRectangle(float32x2 positionSS, float32x2 sizeSS, float32x4 rgba, usize layer)
{
	DrawBorderedRoundedRectangle(positionSS, sizeSS, 0.0f, 0.0f, rgba, float32x4 {}, layer);
}

void DrawRoundedRectangle(float32x2 positionSS, float32x2 sizeSS, float32 cornerRadiusSS, float32x3 rgb, usize layer)
{
	DrawBorderedRoundedRectangle(positionSS, sizeSS, 0.0f, cornerRadiusSS, float32x4 { rgb.X, rgb.Y, rgb.Z, 1.0f }, float32x4 {}, layer);
}

void DrawRoundedRectangle(float32x2 positionSS, float32x2 sizeSS, float32 cornerRadiusSS, float32x4 rgba, usize layer)
{
	DrawBorderedRoundedRectangle(positionSS, sizeSS, 0.0f, cornerRadiusSS, rgba, float32x4 {}, layer);
}

void DrawBorderedRectangle(float32x2 positionSS, float32x2 sizeSS, float32 borderSizeSS, float32x3 rgb, float32x3 borderRGB, usize layer)
{
	DrawBorderedRoundedRectangle(positionSS,
								 sizeSS,
								 borderSizeSS,
								 0.0f,
								 float32x4 { rgb.X, rgb.Y, rgb.Z, 1.0f },
								 float32x4 { borderRGB.X, borderRGB.Y, borderRGB.Z, 1.0f },
								 layer);
}

void DrawBorderedRectangle(float32x2 positionSS, float32x2 sizeSS, float32 borderSizeSS, float32x4 rgba, float32x4 borderRGBA, usize layer)
{
	DrawBorderedRoundedRectangle(positionSS, sizeSS, borderSizeSS, 0.0f, rgba, borderRGBA, layer);
}

void DrawBorderedRoundedRectangle(float32x2 positionSS, float32x2 sizeSS, float32 borderSizeSS, float32 cornerRadiusSS, float32x3 rgb, float32x3 borderRGB, usize layer)
{
	DrawBorderedRoundedRectangle(positionSS,
								 sizeSS,
								 borderSizeSS,
								 cornerRadiusSS,
								 float32x4 { rgb.X, rgb.Y, rgb.Z, 1.0f },
								 float32x4 { borderRGB.X, borderRGB.Y, borderRGB.Z, 1.0f },
								 layer);
}

void DrawBorderedRoundedRectangle(float32x2 positionSS, float32x2 sizeSS, float32 borderSizeSS, float32 cornerRadiusSS, float32x4 rgba, float32x4 borderRGBA, usize layer)
{
	CHECK(DrawIndex + 1 <= MaxUIDrawsPerFrame);

	Draws[DrawIndex] = HLSL::UIDraw
	{
		.PositionSS = positionSS,
		.SizeSS = sizeSS,
		.RGBA = rgba,
		.Type = HLSL::UIDrawType::Rectangle,
		.BorderRGBA = borderRGBA,
		.BorderSizeSS = borderSizeSS,
		.CornerRadiusSS = cornerRadiusSS,
		.Layer = static_cast<uint32>(layer),
	};
	++DrawIndex;
}

void DrawText(StringView text, float32x2 positionSS, float32 scale, float32x3 rgb, usize layer)
{
	DrawText(text, positionSS, scale, float32x4 { rgb.X, rgb.Y, rgb.Z, 1.0f }, layer);
}

void DrawText(StringView text, float32x2 positionSS, float32 scale, float32x4 rgba, usize layer)
{
	CHECK(DrawIndex + text.GetLength() <= MaxUIDrawsPerFrame);

	float32x2 currentPositionSS = { .X = positionSS.X, .Y = positionSS.Y - Font::Ascender * scale };
	for (usize textIndex = 0; textIndex < text.GetLength(); ++textIndex)
	{
		const char c = text[textIndex];
		const Font::Glyph& glyph = Font::Glyphs[c];

		Draws[DrawIndex] = HLSL::UIDraw
		{
			.PositionSS = currentPositionSS,
			.RGBA = rgba,
			.Type = HLSL::UIDrawType::Character,
			.AtlasPosition = glyph.AtlasPosition,
			.AtlasSize = glyph.AtlasSize,
			.PlanePosition = glyph.PlanePosition,
			.PlaneSize = glyph.PlaneSize,
			.Scale = scale,
			.Layer = static_cast<uint32>(layer),
		};
		++DrawIndex;

		currentPositionSS.X += GetCharacterWidth(c, scale);
	}
}

void DrawImage(const TextureView& image, float32x2 positionSS, float32x2 sizeSS, usize layer)
{
	CHECK(DrawIndex + 1 <= MaxUIDrawsPerFrame);

	Draws[DrawIndex] = HLSL::UIDraw
	{
		.PositionSS = positionSS,
		.SizeSS = sizeSS,
		.Type = HLSL::UIDrawType::Image,
		.ImageIndex = GlobalDevice().Get(image),
		.Layer = static_cast<uint32>(layer),
	};
	++DrawIndex;
}

float32 GetCharacterWidth(char c, float32 scale)
{
	return Font::Glyphs[c].Advance * scale;
}

float32 GetTextWidth(StringView text, float32 scale)
{
	float32 widthSS = 0.0f;
	for (usize textIndex = 0; textIndex < text.GetLength(); ++textIndex)
	{
		widthSS += GetCharacterWidth(text[textIndex], scale);
	}
	return widthSS;
}

float32 GetTextHeight(float32 scale)
{
	return Font::LineHeight * scale;
}

float32x2 GetTextSize(StringView text, float32 scale)
{
	return float32x2 { .X = GetTextWidth(text, scale), .Y = GetTextHeight(scale) };
}

ID BeginElement(const Description& description)
{
	const bool hasParent = Elements.Contains(OpenElementID);

	ID id;
	if (description.ID == 0)
	{
		usize parentChildCount = 0;
		if (hasParent)
		{
			const ElementStorage& parent = Elements[OpenElementID];
			parentChildCount += parent.ChildrenIDs.GetCount();
		}
		id = (HashCombine(Hash<ID>{}(OpenElementID), Hash<usize>{}(parentChildCount)));
	}
	else
	{
		id = description.ID;
	}

	CHECK(!Elements.Contains(id));

	usize layer = 0;
	if (hasParent)
	{
		ElementStorage& parent = Elements[OpenElementID];
		parent.ChildrenIDs.Add(id);

		layer = parent.Layer + 1;
	}
	else
	{
		RootElementIDs.Add(id);
	}

	Elements.Add(id, ElementStorage
	{
		.ParentID = OpenElementID,
		.ChildrenIDs = Array<ID>(Allocator),
		.Layer = layer,
		.Description = description,
	});

	OpenElementID = id;

	return id;
}

void EndElement()
{
	CHECK(OpenElementID != INDEX_NONE);
	const ElementStorage& element = Elements[OpenElementID];

	OpenElementID = element.ParentID;
}

ID Rectangle(const Description& description)
{
	const ID id = BeginElement(description);
	EndElement();
	return id;
}

ID Text(StringView text, float32 scale, const Description& description)
{
	const ID id = BeginElement(description);
	Elements[OpenElementID].Text = String(text, Allocator);
	Elements[OpenElementID].TextScale = scale;
	EndElement();
	return id;
}

ID Image(const TextureView& image, const Description& description)
{
	const ID id = BeginElement(description);
	Elements[OpenElementID].Image = image;
	EndElement();
	return id;
}

float32 GetWidth(ID id)
{
	if (!LastFrameElements.Contains(id))
	{
		return 0.0f;
	}

	return LastFrameElements[id].SizeSS.X;
}

float32 GetHeight(ID id)
{
	if (!LastFrameElements.Contains(id))
	{
		return 0.0f;
	}

	return LastFrameElements[id].SizeSS.Y;
}

float32x2 GetSize(ID id)
{
	if (!LastFrameElements.Contains(id))
	{
		return float32x2 {};
	}

	const ElementStorage& element = LastFrameElements[id];
	return float32x2 { element.SizeSS.X, element.SizeSS.Y };
}

float32x2 GetPosition(ID id)
{
	if (!LastFrameElements.Contains(id))
	{
		return float32x2 {};
	}

	const ElementStorage& element = LastFrameElements[id];
	return float32x2 { element.PositionSS.X, element.PositionSS.Y };
}

static bool IsPointInRoundedRectangle(float32 x, float32 y, float32 left, float32 right, float32 top, float32 bottom, float32 cornerRadius)
{
	const float32 rectangleX = Clamp(x, left + cornerRadius, right - cornerRadius);
	const float32 rectangleY = Clamp(y, top + cornerRadius, bottom - cornerRadius);

	const float32 differenceX = x - rectangleX;
	const float32 differenceY = y - rectangleY;

	return (differenceX * differenceX + differenceY * differenceY) <= (cornerRadius * cornerRadius);
}

bool IsHovered(ID id)
{
	if (!LastFrameElements.Contains(id))
	{
		return false;
	}

	const float32x2 mousePositionSS = float32x2 { .X = static_cast<float32>(Platform::GetMouseX()), .Y = static_cast<float32>(Platform::GetMouseY()) };

	const ElementStorage& element = LastFrameElements[id];
	if (!element.Text.IsEmpty())
	{
		return false;
	}

	const bool hovered = IsPointInRoundedRectangle(mousePositionSS.X,
												   mousePositionSS.Y,
												   element.PositionSS.X,
												   element.PositionSS.X + element.SizeSS.X,
												   element.PositionSS.Y,
												   element.PositionSS.Y + element.SizeSS.Y,
												   element.Description.Style.CornerRadiusSS);
	if (!hovered)
	{
		return false;
	}

	bool higherHovered = false;
	for (const ID rootElementID : LastFrameRootElementIDs)
	{
		Array<ID> breadthFirst(Allocator);
		breadthFirst.Add(rootElementID);

		while (!breadthFirst.IsEmpty() && !higherHovered)
		{
			const ID currentID = breadthFirst.First();
			breadthFirst.Remove(0);

			const ElementStorage& currentElement = LastFrameElements[currentID];

			for (const ID childID : currentElement.ChildrenIDs)
			{
				breadthFirst.Add(childID);
			}

			const bool ignore = currentID == id || currentElement.Layer <= element.Layer || !element.Text.IsEmpty();
			if (ignore)
			{
				continue;
			}

			higherHovered |= IsPointInRoundedRectangle(mousePositionSS.X,
													   mousePositionSS.Y,
													   currentElement.PositionSS.X,
													   currentElement.PositionSS.X + currentElement.SizeSS.X,
													   currentElement.PositionSS.Y,
													   currentElement.PositionSS.Y + currentElement.SizeSS.Y,
													   currentElement.Description.Style.CornerRadiusSS);
		}

		if (higherHovered)
		{
			break;
		}
	}

	return hovered && !higherHovered;
}

bool IsPressed(ID id)
{
	return IsHovered(id) && Platform::IsMouseButtonPressed(Platform::MouseButton::Left);
}

bool IsPressedOnce(ID id)
{
	return IsHovered(id) && Platform::IsMouseButtonPressedOnce(Platform::MouseButton::Left);
}

static bool IsSameDirection(bool x, Direction direction)
{
	return (direction == Direction::Horizontal && x) || (direction == Direction::Vertical && !x);
}

static float32 GetTextHeightForLineWidth(const String& text, float32 widthSS, float32 scale)
{
	float32x2 positionLS = float32x2 { .X = 0.0f, .Y = 0.0f };
	for (const String& piece : text.Split(' ', Allocator))
	{
		const float32 pieceWidth = GetTextWidth(piece, scale);
		if (positionLS.X + pieceWidth > widthSS)
		{
			positionLS.X = 0.0f;
			positionLS.Y += GetTextHeight(scale);
		}

		positionLS.X += pieceWidth + GetCharacterWidth(' ', scale);
	}

	return positionLS.Y + GetTextHeight(scale);
}

static void LayoutSize(ID rootElementID, bool x)
{
	struct ToVisit
	{
		ID ID;
		bool Visited;
	};

	Array<ToVisit> postOrderDepthFirst(Allocator);
	postOrderDepthFirst.Add(ToVisit { .ID = rootElementID, .Visited = false });

	while (!postOrderDepthFirst.IsEmpty())
	{
		const ToVisit current = postOrderDepthFirst.Last();
		postOrderDepthFirst.Remove(postOrderDepthFirst.GetCount() - 1);

		ElementStorage& element = Elements[current.ID];
		Description::LayoutDescription& elementLayout = element.Description.Layout;

		if (!current.Visited)
		{
			postOrderDepthFirst.Add(ToVisit { .ID = current.ID, .Visited = true });
			for (usize childIndicesIndex = element.ChildrenIDs.GetCount() - 1; childIndicesIndex != INDEX_NONE; --childIndicesIndex)
			{
				postOrderDepthFirst.Add(ToVisit { .ID = element.ChildrenIDs[childIndicesIndex], .Visited = false });
			}
			continue;
		}

		float32& sizeSS = x ? element.SizeSS.X : element.SizeSS.Y;

		for (const ID childID : element.ChildrenIDs)
		{
			const ElementStorage& childElement = Elements[childID];

			const float32 childElementSizeSS = x ? childElement.SizeSS.X : childElement.SizeSS.Y;

			sizeSS = IsSameDirection(x, elementLayout.Direction) ? (sizeSS + childElementSizeSS) : Max(sizeSS, childElementSizeSS);
		}

		sizeSS += 2.0f * (x ? elementLayout.PaddingSS.X : elementLayout.PaddingSS.Y);
		if (IsSameDirection(x, elementLayout.Direction) && element.ChildrenIDs.GetCount() != 0)
		{
			sizeSS += elementLayout.SpacingSS * static_cast<float32>(element.ChildrenIDs.GetCount() - 1);
		}

		MinMax& minMax = x ? elementLayout.SizeX.MinMax : elementLayout.SizeY.MinMax;
		if (minMax.Max == 0.0f)
		{
			minMax.Max = FLOAT32_MAX;
		}

		if (!element.Text.IsEmpty())
		{
			if (x)
			{
				sizeSS += GetTextWidth(element.Text, element.TextScale);

				float32 maxPieceWidthSS = 0.0f;
				for (const String& piece : element.Text.Split(' ', Allocator))
				{
					maxPieceWidthSS = Max(maxPieceWidthSS, GetTextWidth(piece, element.TextScale));
				}
				minMax.Min = Max(minMax.Min, maxPieceWidthSS);
			}
			else
			{
				elementLayout.SizeY.MinMax.Min = Max(elementLayout.SizeY.MinMax.Min,
													 GetTextHeightForLineWidth(element.Text, element.SizeSS.X, element.TextScale));
			}
		}

		if (element.Image.IsValid())
		{
			sizeSS += x ? static_cast<float32>(element.Image.Resource.Dimensions.Width) : static_cast<float32>(element.Image.Resource.Dimensions.Height);
		}

		sizeSS = Min(Max(sizeSS, minMax.Min), minMax.Max);
	}

	Array<ID> breadthFirst(Allocator);
	breadthFirst.Add(rootElementID);

	while (!breadthFirst.IsEmpty())
	{
		const ID currentID = breadthFirst.First();
		breadthFirst.Remove(0);

		ElementStorage& element = Elements[currentID];
		const Description::LayoutDescription& elementLayout = element.Description.Layout;

		const Size& elementSize = x ? elementLayout.SizeX : elementLayout.SizeY;
		if (currentID == rootElementID && elementSize.Mode == Mode::Grow)
		{
			ElementStorage& flexibleElement = Elements[currentID];
			float32& flexibleElementSizeSS = x ? flexibleElement.SizeSS.X : flexibleElement.SizeSS.Y;

			const float32 flexibleElementMaxSizeSS = x ? flexibleElement.Description.Layout.SizeX.MinMax.Max
													   : flexibleElement.Description.Layout.SizeY.MinMax.Max;

			flexibleElementSizeSS = flexibleElementMaxSizeSS;
		}

		for (const ID childID : element.ChildrenIDs)
		{
			breadthFirst.Add(childID);
		}

		float32 remainingSS = (x ? element.SizeSS.X : element.SizeSS.Y) -
							  2.0f * (x ? elementLayout.PaddingSS.X : elementLayout.PaddingSS.Y);
		if (IsSameDirection(x, elementLayout.Direction) && element.ChildrenIDs.GetCount() != 0)
		{
			remainingSS -= elementLayout.SpacingSS * static_cast<float32>(element.ChildrenIDs.GetCount() - 1);
		}

		Array<ID> flexibleIDs(Allocator);

		for (const ID childID : element.ChildrenIDs)
		{
			const ElementStorage& childElement = Elements[childID];

			const Size& childSize = x ? childElement.Description.Layout.SizeX : childElement.Description.Layout.SizeY;
			if (childSize.Mode == Mode::Grow || (x && !childElement.Text.IsEmpty()) || childElement.Image.IsValid())
			{
				flexibleIDs.Add(childID);
			}

			if (IsSameDirection(x, elementLayout.Direction))
			{
				remainingSS -= x ? childElement.SizeSS.X : childElement.SizeSS.Y;
			}
		}

		if (!IsSameDirection(x, elementLayout.Direction))
		{
			for (const ID flexibleID : flexibleIDs)
			{
				ElementStorage& flexibleElement = Elements[flexibleID];
				float32& flexibleElementSizeSS = x ? flexibleElement.SizeSS.X : flexibleElement.SizeSS.Y;

				const float32 flexibleElementMaxSizeSS = x ? flexibleElement.Description.Layout.SizeX.MinMax.Max
														   : flexibleElement.Description.Layout.SizeY.MinMax.Max;

				flexibleElementSizeSS = Min(remainingSS, flexibleElementMaxSizeSS);
			}
		}
		else
		{
			static constexpr float32 epsilon = 1e-4f;

			while (!IsAlmostEqual(remainingSS, 0.0f, epsilon) && !flexibleIDs.IsEmpty())
			{
				const bool grow = remainingSS > 0.0f;

				float32 firstFlexibleSS = grow ? static_cast<float32>(INFINITY) : 0.0f;
				float32 secondFlexibleSS = firstFlexibleSS;
				float32 distributeSS = remainingSS;

				for (usize flexibleIDsIndex = 0; flexibleIDsIndex < flexibleIDs.GetCount(); ++flexibleIDsIndex)
				{
					const ID flexibleID = flexibleIDs[flexibleIDsIndex];
					ElementStorage& flexibleElement = Elements[flexibleID];

					const Description::LayoutDescription& flexibleElementLayout = flexibleElement.Description.Layout;

					const float32 flexibleElementLimitSizeSS = grow ? (x ? flexibleElementLayout.SizeX.MinMax.Max : flexibleElementLayout.SizeY.MinMax.Max)
																	: (x ? flexibleElementLayout.SizeX.MinMax.Min : flexibleElementLayout.SizeY.MinMax.Min);

					const float32 flexibleElementSizeSS = x ? flexibleElement.SizeSS.X : flexibleElement.SizeSS.Y;

					if (grow ? (flexibleElementSizeSS >= flexibleElementLimitSizeSS) : (flexibleElementSizeSS <= flexibleElementLimitSizeSS))
					{
						flexibleIDs.Remove(flexibleIDsIndex);
						--flexibleIDsIndex;
						continue;
					}

					if (grow ? (flexibleElementSizeSS < firstFlexibleSS) : (flexibleElementSizeSS > firstFlexibleSS))
					{
						secondFlexibleSS = firstFlexibleSS;
						firstFlexibleSS = flexibleElementSizeSS;
					}
					if (grow ? (flexibleElementSizeSS > firstFlexibleSS) : (flexibleElementSizeSS < firstFlexibleSS))
					{
						secondFlexibleSS = grow ? Min(secondFlexibleSS, flexibleElementSizeSS) : Max(secondFlexibleSS, flexibleElementSizeSS);
						distributeSS = secondFlexibleSS - firstFlexibleSS;
					}
				}

				const float32 remainingEvenSplit = remainingSS / static_cast<float32>(flexibleIDs.GetCount());
				distributeSS = grow ? Min(distributeSS, remainingEvenSplit) : Max(distributeSS, remainingEvenSplit);

				bool distributed = false;
				for (const ID flexibleID : flexibleIDs)
				{
					ElementStorage& flexibleElement = Elements[flexibleID];
					float32& flexibleElementSizeSS = x ? flexibleElement.SizeSS.X : flexibleElement.SizeSS.Y;

					if (IsAlmostEqual(flexibleElementSizeSS, firstFlexibleSS, epsilon))
					{
						Description::LayoutDescription& flexibleElementLayout = flexibleElement.Description.Layout;

						const float32 flexibleElementLimitSizeSS = grow ? (x ? flexibleElementLayout.SizeX.MinMax.Max : flexibleElementLayout.SizeY.MinMax.Max)
																		: (x ? flexibleElementLayout.SizeX.MinMax.Min : flexibleElementLayout.SizeY.MinMax.Min);

						const float32 newSizeSS = grow ? Min(flexibleElementSizeSS + distributeSS, flexibleElementLimitSizeSS)
													   : Max(flexibleElementSizeSS + distributeSS, flexibleElementLimitSizeSS);

						distributed |= !IsAlmostEqual(flexibleElementSizeSS, newSizeSS, epsilon);

						remainingSS -= newSizeSS - flexibleElementSizeSS;
						flexibleElementSizeSS = newSizeSS;

						if (!grow && !flexibleElement.Text.IsEmpty())
						{
							flexibleElementLayout.SizeY.MinMax.Min = Max(flexibleElementLayout.SizeY.MinMax.Min,
																		 GetTextHeightForLineWidth(element.Text, element.SizeSS.X, element.TextScale));
						}
					}
				}

				if (!distributed)
				{
					break;
				}
			}
		}
	}
}

static void LayoutPosition(ID elementID, bool x, float32 cursorSS)
{
	ElementStorage& element = Elements[elementID];
	const Description::LayoutDescription& elementLayout = element.Description.Layout;

	float32& elementPositionSS = x ? element.PositionSS.X : element.PositionSS.Y;
	elementPositionSS = cursorSS;

	cursorSS += x ? element.Description.Layout.PaddingSS.X : element.Description.Layout.PaddingSS.Y;

	const Alignment alignment = x ? elementLayout.AlignmentX : elementLayout.AlignmentY;

	if (IsSameDirection(x, elementLayout.Direction))
	{
		float32 remainingSS = (x ? element.SizeSS.X : element.SizeSS.Y) -
							  2.0f * (x ? elementLayout.PaddingSS.X : elementLayout.PaddingSS.Y);
		if (element.ChildrenIDs.GetCount() != 0)
		{
			remainingSS -= elementLayout.SpacingSS * static_cast<float32>(element.ChildrenIDs.GetCount() - 1);
		}

		for (const ID childID : element.ChildrenIDs)
		{
			const ElementStorage& childElement = Elements[childID];

			remainingSS -= x ? childElement.SizeSS.X : childElement.SizeSS.Y;
		}

		switch (alignment)
		{
		case Alignment::Left:
			break;
		case Alignment::Center:
			cursorSS += remainingSS / 2.0f;
			break;
		case Alignment::Right:
			cursorSS += remainingSS;
			break;
		}
	}

	for (const ID childID : element.ChildrenIDs)
	{
		const ElementStorage& childElement = Elements[childID];

		float32 alignmentOffsetSS = 0.0f;
		if (!IsSameDirection(x, elementLayout.Direction))
		{
			const float32 remainingSS = (x ? element.SizeSS.X : element.SizeSS.Y) -
										2.0f * (x ? elementLayout.PaddingSS.X : elementLayout.PaddingSS.Y) -
										(x ? childElement.SizeSS.X : childElement.SizeSS.Y);

			switch (alignment)
			{
			case Alignment::Left:
				break;
			case Alignment::Center:
				alignmentOffsetSS = remainingSS / 2.0f;
				break;
			case Alignment::Right:
				alignmentOffsetSS = remainingSS;
				break;
			}
		}
		LayoutPosition(childID, x, cursorSS + alignmentOffsetSS);

		if (IsSameDirection(x, elementLayout.Direction))
		{
			const float32 childElementSizeSS = x ? childElement.SizeSS.X : childElement.SizeSS.Y;

			cursorSS += childElementSizeSS + elementLayout.SpacingSS;
		}
	}
}

static void Draw(ID elementID)
{
	const ElementStorage& element = Elements[elementID];

	if (element.Text.IsEmpty() && !element.Image.IsValid())
	{
		DrawBorderedRoundedRectangle(element.PositionSS,
									 element.SizeSS,
									 element.Description.Style.BorderSizeSS,
									 element.Description.Style.CornerRadiusSS,
									 element.Description.Style.RGBA,
									 element.Description.Style.BorderRGBA,
									 element.Layer);
	}

	if (!element.Text.IsEmpty())
	{
		float32x2 positionLS = float32x2 { .X = 0.0f, .Y = 0.0f };
		for (const String& piece : element.Text.Split(' ', Allocator))
		{
			const float32 pieceWidth = GetTextWidth(piece, element.TextScale);
			if (positionLS.X + pieceWidth > element.SizeSS.X)
			{
				positionLS.X = 0.0f;
				positionLS.Y += GetTextHeight(element.TextScale);
			}

			DrawText(piece,
					 float32x2 { .X = element.PositionSS.X + positionLS.X, .Y = element.PositionSS.Y + positionLS.Y },
					 element.TextScale,
					 element.Description.Style.RGBA,
					 element.Layer);

			positionLS.X += pieceWidth + GetCharacterWidth(' ', element.TextScale);
		}
	}

	if (element.Image.IsValid())
	{
		DrawImage(element.Image, element.PositionSS, element.SizeSS, element.Layer);
	}

	for (const ID childID : element.ChildrenIDs)
	{
		Draw(childID);
	}

	const bool horizontal = element.Description.Layout.Direction == Direction::Horizontal;
	const Description::StyleDescription& parentStyle = element.Description.Style;

	for (usize childIDIndex = 0; childIDIndex + 1 < element.ChildrenIDs.GetCount(); ++childIDIndex)
	{
		const ID childID = element.ChildrenIDs[childIDIndex];
		const ElementStorage& childElement = Elements[childID];

		const ID nextChildID = element.ChildrenIDs[childIDIndex];
		const ElementStorage& nextChildElement = Elements[nextChildID];

		const float32 xCenterSS = childElement.PositionSS.X +
								  (childElement.SizeSS.X + nextChildElement.SizeSS.X) / 2.0f +
								  element.Description.Layout.SpacingSS / 2.0f -
								  parentStyle.BetweenSizeSS / 2.0f;
		const float32 yCenterSS = childElement.PositionSS.Y +
								  (childElement.SizeSS.Y + nextChildElement.SizeSS.Y) / 2.0f +
								  element.Description.Layout.SpacingSS / 2.0f -
								  parentStyle.BetweenSizeSS / 2.0f;

		const float32x2 betweenPositionSS =
		{
			.X = horizontal ? xCenterSS : (element.PositionSS.X + parentStyle.BorderSizeSS),
			.Y = horizontal ? (element.PositionSS.Y + parentStyle.BorderSizeSS) : yCenterSS,
		};
		const float32x2 betweenSizeSS =
		{
			.X = horizontal ? parentStyle.BetweenSizeSS : (element.SizeSS.X - parentStyle.BorderSizeSS * 2.0f),
			.Y = horizontal ? (element.SizeSS.Y - parentStyle.BorderSizeSS * 2.0f) : parentStyle.BetweenSizeSS,
		};
		DrawRectangle(betweenPositionSS, betweenSizeSS, parentStyle.BetweenRGBA);
	}
}

void Submit(uint32 screenWidth, uint32 screenHeight)
{
	CHECK(OpenElementID == INDEX_NONE);

	for (const ID rootElementID : RootElementIDs)
	{
		LayoutSize(rootElementID, true);
	}
	for (const ID rootElementID : RootElementIDs)
	{
		LayoutSize(rootElementID, false);
	}
	for (const ID rootElementID : RootElementIDs)
	{
		LayoutPosition(rootElementID, true, 0.0f);
	}
	for (const ID rootElementID : RootElementIDs)
	{
		LayoutPosition(rootElementID, false, 0.0f);
	}
	for (const ID rootElementID : RootElementIDs)
	{
		Draw(rootElementID);
	}

	RootConstants.ScreenToClip = Matrix::Orthographic(0.0f, static_cast<float32>(screenWidth), 0.0f, static_cast<float32>(screenHeight), 0.0f, 1.0f);

	RootConstants.UIDrawBufferIndex = GlobalDevice().Get(DrawBufferViews[GlobalDevice().GetFrameIndex()]);
	RootConstants.FontTextureIndex = GlobalDevice().Get(Font::TextureView);
	RootConstants.LinearWrapSampler = GlobalDevice().Get(RenderContext.LinearWrapSampler);

	SortStable(Draws.GetData(), DrawIndex, Allocator, [](const HLSL::UIDraw& a, const HLSL::UIDraw& b) -> bool
	{
		return a.Layer < b.Layer;
	});
	GlobalDevice().Write(&DrawBufferResources[GlobalDevice().GetFrameIndex()], Draws.GetData());

	GlobalGraphics().SetPipeline(Pipeline);
	GlobalGraphics().SetRootConstants(&RootConstants);

	static constexpr usize verticesPerQuad = 6;
	GlobalGraphics().Draw(DrawIndex * verticesPerQuad);

	DrawIndex = 0;

	LastFrameElements = Elements;
	LastFrameRootElementIDs = RootElementIDs;

	Elements.Clear();
	RootElementIDs.Clear();

	OpenElementID = INDEX_NONE;
}

}
