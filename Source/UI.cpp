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
	Array<ID> FloatingChildrenIDs;

	float32x2 PositionSS;
	float32x2 SizeSS;

	usize Layer;

	Description Description;

	String Text;
	float32 TextScale;

	TextureView Image;
};

static HashTable<ID, ElementStorage> Elements(32, Allocator);
static Array<ID> RootIDs(Allocator);

static HashTable<ID, ElementStorage> LastFrameElements(32, Allocator);
static Array<ID> LastFrameRootIDs(Allocator);

static ID OpenID = INDEX_NONE;

static usize MaxLayer = 0;

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
		float32x2 atlasPosition = { 0.0f, 0.0f };
		float32x2 atlasSize = { 0.0f, 0.0f };
		float32x2 planePosition = { 0.0f, 0.0f };
		float32x2 planeSize = { 0.0f, 0.0f };

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

void DrawRectangle(float32x2 positionSS, float32x2 sizeSS, float32x4 rgba, usize layer)
{
	DrawBorderedRoundedRectangle(positionSS, sizeSS, 0.0f, float32x4 {}, rgba, float32x4 {}, layer);
}

void DrawRoundedRectangle(float32x2 positionSS, float32x2 sizeSS, float32x4 cornerRadiiSS, float32x4 rgba, usize layer)
{
	DrawBorderedRoundedRectangle(positionSS, sizeSS, 0.0f, cornerRadiiSS, rgba, float32x4 {}, layer);
}

void DrawBorderedRectangle(float32x2 positionSS, float32x2 sizeSS, float32 borderSizeSS, float32x4 rgba, float32x4 borderRGBA, usize layer)
{
	DrawBorderedRoundedRectangle(positionSS, sizeSS, borderSizeSS, float32x4 {}, rgba, borderRGBA, layer);
}

void DrawBorderedRoundedRectangle(float32x2 positionSS, float32x2 sizeSS, float32 borderSizeSS, float32x4 cornerRadiiSS, float32x4 rgba, float32x4 borderRGBA, usize layer)
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
		.CornerRadiiSS = cornerRadiiSS,
		.Layer = static_cast<uint32>(layer),
	};
	++DrawIndex;
}

void DrawText(StringView text, float32x2 positionSS, float32 scale, float32x4 rgba, usize layer)
{
	CHECK(DrawIndex + text.GetLength() <= MaxUIDrawsPerFrame);

	float32x2 currentPositionSS = { positionSS.X, positionSS.Y - Font::Ascender * scale };
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
	return float32x2 { GetTextWidth(text, scale), GetTextHeight(scale) };
}

ID BeginElement(const Description& description)
{
	const bool hasParent = Elements.Contains(OpenID);

	ID id;
	if (description.ID == 0)
	{
		usize parentChildrenCount = 0;
		if (hasParent)
		{
			const ElementStorage& parent = Elements[OpenID];
			parentChildrenCount += parent.ChildrenIDs.GetCount() + parent.FloatingChildrenIDs.GetCount();
		}
		id = (HashCombine(Hash<ID>{}(OpenID), Hash<usize>{}(parentChildrenCount)));
	}
	else
	{
		id = description.ID;
	}

	CHECK(!Elements.Contains(id));

	usize layer;
	if (hasParent)
	{
		ElementStorage& parent = Elements[OpenID];
		if (description.Layout.Floating || description.Layout.Absolute)
		{
			parent.FloatingChildrenIDs.Add(id);
		}
		else
		{
			parent.ChildrenIDs.Add(id);
		}

		static constexpr usize floatingLayerOffset = 1000000;

		layer = parent.Layer;
		if (description.Layout.Floating)
		{
			layer += floatingLayerOffset;
		}
		else
		{
			++layer;
		}
	}
	else
	{
		CHECK(!description.Layout.Floating);

		RootIDs.Add(id);

		layer = MaxLayer + 1;
	}
	MaxLayer = Max(MaxLayer, layer);

	Elements.Add(id, ElementStorage
	{
		.ParentID = OpenID,
		.ChildrenIDs = Array<ID>(Allocator),
		.FloatingChildrenIDs = Array<ID>(Allocator),
		.Layer = layer,
		.Description = description,
	});

	OpenID = id;

	return id;
}

void EndElement()
{
	CHECK(OpenID != INDEX_NONE);
	const ElementStorage& element = Elements[OpenID];

	OpenID = element.ParentID;
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
	Elements[OpenID].Text = String(text, Allocator);
	Elements[OpenID].TextScale = scale;
	EndElement();
	return id;
}

ID Image(const TextureView& image, const Description& description)
{
	const ID id = BeginElement(description);
	Elements[OpenID].Image = image;
	EndElement();
	return id;
}

bool DoesExist(ID id)
{
	return LastFrameElements.Contains(id);
}

float32 GetWidth(ID id)
{
	return DoesExist(id) ? LastFrameElements[id].SizeSS.X : 0.0f;
}

float32 GetHeight(ID id)
{
	return DoesExist(id) ? LastFrameElements[id].SizeSS.Y : 0.0f;
}

float32x2 GetSize(ID id)
{
	if (!DoesExist(id))
	{
		return float32x2 {};
	}

	const ElementStorage& element = LastFrameElements[id];
	return float32x2 { element.SizeSS.X, element.SizeSS.Y };
}

float32x2 GetPosition(ID id)
{
	if (!DoesExist(id))
	{
		return float32x2 {};
	}

	const ElementStorage& element = LastFrameElements[id];
	return float32x2 { element.PositionSS.X, element.PositionSS.Y };
}

static bool IsPointInRoundedRectangle(float32 x, float32 y, float32 left, float32 right, float32 top, float32 bottom, float32x4 cornerRadii)
{
	const float32x2 center =
	{
		(left + right) * 0.5f,
		(top + bottom) * 0.5f,
	};
	const float32 cornerRadius = x > center.X ? (y > center.Y ? cornerRadii.W : cornerRadii.Y)
											  : (y > center.Y ? cornerRadii.Z : cornerRadii.X);

	const float32 rectangleX = Clamp(x, left + cornerRadius, right - cornerRadius);
	const float32 rectangleY = Clamp(y, top + cornerRadius, bottom - cornerRadius);

	const float32 differenceX = x - rectangleX;
	const float32 differenceY = y - rectangleY;

	return (differenceX * differenceX + differenceY * differenceY) <= (cornerRadius * cornerRadius);
}

bool IsHovered(ID id, bool ignoreChildren)
{
	if (!DoesExist(id))
	{
		return false;
	}

	const ElementStorage& element = LastFrameElements[id];

	if (!element.Text.IsEmpty())
	{
		return false;
	}

	const float32x2 mousePositionSS = { static_cast<float32>(Platform::GetMouseX()), static_cast<float32>(Platform::GetMouseY()) };

	const bool hovered = IsPointInRoundedRectangle(mousePositionSS.X,
												   mousePositionSS.Y,
												   element.PositionSS.X,
												   element.PositionSS.X + element.SizeSS.X,
												   element.PositionSS.Y,
												   element.PositionSS.Y + element.SizeSS.Y,
												   element.Description.Style.CornerRadiiSS);
	if (!hovered)
	{
		return false;
	}

	bool higherHovered = false;
	for (const ID rootID : LastFrameRootIDs)
	{
		Array<ID> breadthFirst(Allocator);
		breadthFirst.Add(rootID);

		while (!breadthFirst.IsEmpty() && !higherHovered)
		{
			const ID currentID = breadthFirst.First();
			breadthFirst.Remove(0);

			if (currentID == id && ignoreChildren)
			{
				continue;
			}

			const ElementStorage& currentElement = LastFrameElements[currentID];

			for (const ID childID : currentElement.ChildrenIDs)
			{
				breadthFirst.Add(childID);
			}
			for (const ID childID : currentElement.FloatingChildrenIDs)
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
													   currentElement.Description.Style.CornerRadiiSS);
		}

		if (higherHovered)
		{
			break;
		}
	}

	return hovered && !higherHovered;
}

bool IsPressed(ID id, bool ignoreChildren)
{
	return IsHovered(id, ignoreChildren) && Platform::IsMouseButtonPressed(Platform::MouseButton::Left);
}

bool IsPressedOnce(ID id, bool ignoreChildren)
{
	return IsHovered(id, ignoreChildren) && Platform::IsMouseButtonPressedOnce(Platform::MouseButton::Left);
}

static float32 GetTextHeightForLineWidth(const String& text, float32 widthSS, float32 scale)
{
	float32x2 positionLS = float32x2 { 0.0f, 0.0f };
	for (const StringView piece : text.Split(' ', Allocator))
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

static bool IsSameDirection(bool x, Direction direction)
{
	return (direction == Direction::Horizontal && x) || (direction == Direction::Vertical && !x);
}

static float32 GetChildrenSizeSS(const ElementStorage& element, bool x, Direction direction)
{
	const usize count = element.ChildrenIDs.GetCount();

	float32 sizeSS = (IsSameDirection(x, direction) && count > 0) ? element.Description.Layout.SpacingSS * static_cast<float32>(count - 1)
																  : 0.0f;

	for (const ID childID : element.ChildrenIDs)
	{
		const ElementStorage& childElement = Elements[childID];

		const float32 childSizeSS = x ? childElement.SizeSS.X : childElement.SizeSS.Y;
		sizeSS = IsSameDirection(x, direction) ? sizeSS + childSizeSS : Max(sizeSS, childSizeSS);
	}
	return sizeSS;
}

static float32 GetRemainingSizeSS(const ElementStorage& containerElement, bool x, float32 containedSizeSS)
{
	const float32x2 paddingSS = containerElement.Description.Layout.PaddingSS;
	return (x ? containerElement.SizeSS.X : containerElement.SizeSS.Y) - 2.0f * (x ? paddingSS.X : paddingSS.Y) - containedSizeSS;
}

static void LayoutSize(ID rootID, bool x)
{
	struct ToVisit
	{
		ID ID;
		bool Visited;
	};

	Array<ToVisit> postOrderDepthFirst(Allocator);
	postOrderDepthFirst.Add(ToVisit { .ID = rootID, .Visited = false });

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
			for (usize childIndicesIndex = element.FloatingChildrenIDs.GetCount() - 1; childIndicesIndex != INDEX_NONE; --childIndicesIndex)
			{
				postOrderDepthFirst.Add(ToVisit { .ID = element.FloatingChildrenIDs[childIndicesIndex], .Visited = false });
			}
			continue;
		}

		float32& sizeSS = x ? element.SizeSS.X : element.SizeSS.Y;
		sizeSS = GetChildrenSizeSS(element, x, elementLayout.Direction) + 2.0f * (x ? elementLayout.PaddingSS.X : elementLayout.PaddingSS.Y);

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
				for (const StringView piece : element.Text.Split(' ', Allocator))
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
	breadthFirst.Add(rootID);

	while (!breadthFirst.IsEmpty())
	{
		const ID currentID = breadthFirst.First();
		breadthFirst.Remove(0);

		ElementStorage& element = Elements[currentID];
		const Description::LayoutDescription& elementLayout = element.Description.Layout;

		const Size& elementSize = x ? elementLayout.SizeX : elementLayout.SizeY;
		if (currentID == rootID && elementSize.Mode == Mode::Grow)
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
		for (const ID childID : element.FloatingChildrenIDs)
		{
			breadthFirst.Add(childID);
		}

		const float32 layoutDirectionSizeSS = IsSameDirection(x, elementLayout.Direction) ? GetChildrenSizeSS(element, x, elementLayout.Direction)
																						  : 0.0f;
		float32 remainingSizeSS = GetRemainingSizeSS(element, x, layoutDirectionSizeSS);

		Array<ID> flexibleIDs(Allocator);

		for (const ID childID : element.ChildrenIDs)
		{
			const ElementStorage& childElement = Elements[childID];

			const Size& childSize = x ? childElement.Description.Layout.SizeX : childElement.Description.Layout.SizeY;
			if (childSize.Mode == Mode::Grow || (x && !childElement.Text.IsEmpty()) || childElement.Image.IsValid())
			{
				flexibleIDs.Add(childID);
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

				flexibleElementSizeSS = Min(remainingSizeSS, flexibleElementMaxSizeSS);
			}
		}
		else
		{
			static constexpr float32 epsilon = 1e-4f;

			while (!IsAlmostEqual(remainingSizeSS, 0.0f, epsilon) && !flexibleIDs.IsEmpty())
			{
				const bool grow = remainingSizeSS > 0.0f;

				float32 firstFlexibleSS = grow ? static_cast<float32>(INFINITY) : 0.0f;
				float32 secondFlexibleSS = firstFlexibleSS;
				float32 distributeSS = remainingSizeSS;

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

				const float32 remainingEvenSplit = remainingSizeSS / static_cast<float32>(flexibleIDs.GetCount());
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

						remainingSizeSS -= newSizeSS - flexibleElementSizeSS;
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

static void LayoutPosition(ID id, bool x, float32 cursorSS);

static void LayoutPositionChildren(const ElementStorage& element, bool x, float32 cursorSS, ArrayView<ID> childrenIDs)
{
	const Description::LayoutDescription& elementLayout = element.Description.Layout;

	const Alignment alignment = x ? elementLayout.AlignmentX : elementLayout.AlignmentY;

	if (IsSameDirection(x, elementLayout.Direction))
	{
		const float32 remainingSS = GetRemainingSizeSS(element, x, GetChildrenSizeSS(element, x, elementLayout.Direction));

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

	for (const ID childID : childrenIDs)
	{
		const ElementStorage& childElement = Elements[childID];

		float32 alignmentOffsetSS = 0.0f;
		if (!IsSameDirection(x, elementLayout.Direction))
		{
			const float32 remainingSS = GetRemainingSizeSS(element, x, x ? childElement.SizeSS.X : childElement.SizeSS.Y);

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
			cursorSS += (x ? childElement.SizeSS.X : childElement.SizeSS.Y) + elementLayout.SpacingSS;
		}
	}
}

static void LayoutPosition(ID id, bool x, float32 cursorSS)
{
	ElementStorage& element = Elements[id];
	const Description::LayoutDescription& elementLayout = element.Description.Layout;

	if (elementLayout.Absolute)
	{
		cursorSS = x ? elementLayout.AbsolutePositionSS.X : elementLayout.AbsolutePositionSS.Y;
	}

	float32& elementPositionSS = x ? element.PositionSS.X : element.PositionSS.Y;
	elementPositionSS = cursorSS;

	const float32 childrenCursorSS = cursorSS + (x ? elementLayout.PaddingSS.X : elementLayout.PaddingSS.Y);
	LayoutPositionChildren(element, x, childrenCursorSS, element.ChildrenIDs);

	const float32 floatingChildrenCursorSS = cursorSS + (x ? 0.0f : element.SizeSS.Y);
	LayoutPositionChildren(element, x, floatingChildrenCursorSS, element.FloatingChildrenIDs);
}

static void Draw(ID id);

static void DrawChildren(const ElementStorage& element, ArrayView<ID> childrenIDs)
{
	for (const ID childID : childrenIDs)
	{
		Draw(childID);
	}

	const Description::StyleDescription& parentStyle = element.Description.Style;

	const bool horizontal = element.Description.Layout.Direction == Direction::Horizontal;

	for (usize childIDIndex = 0; childIDIndex + 1 < childrenIDs.GetCount(); ++childIDIndex)
	{
		const ID childID = childrenIDs[childIDIndex];
		const ElementStorage& childElement = Elements[childID];

		const ID nextChildID = childrenIDs[childIDIndex];
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
			horizontal ? xCenterSS : (element.PositionSS.X + parentStyle.BorderSizeSS),
			horizontal ? (element.PositionSS.Y + parentStyle.BorderSizeSS) : yCenterSS,
		};
		const float32x2 betweenSizeSS =
		{
			horizontal ? parentStyle.BetweenSizeSS : (element.SizeSS.X - parentStyle.BorderSizeSS * 2.0f),
			horizontal ? (element.SizeSS.Y - parentStyle.BorderSizeSS * 2.0f) : parentStyle.BetweenSizeSS,
		};
		DrawRectangle(betweenPositionSS, betweenSizeSS, parentStyle.BetweenRGBA, element.Layer);
	}
}

static void Draw(ID id)
{
	const ElementStorage& element = Elements[id];

	if (element.Text.IsEmpty() && !element.Image.IsValid())
	{
		DrawBorderedRoundedRectangle(element.PositionSS,
									 element.SizeSS,
									 element.Description.Style.BorderSizeSS,
									 element.Description.Style.CornerRadiiSS,
									 element.Description.Style.RGBA,
									 element.Description.Style.BorderRGBA,
									 element.Layer);
	}

	if (!element.Text.IsEmpty())
	{
		float32x2 positionLS = float32x2 { 0.0f, 0.0f };
		for (const String& piece : element.Text.Split(' ', Allocator))
		{
			const float32 pieceWidth = GetTextWidth(piece, element.TextScale);
			if (positionLS.X + pieceWidth > element.SizeSS.X)
			{
				positionLS.X = 0.0f;
				positionLS.Y += GetTextHeight(element.TextScale);
			}

			DrawText(piece,
					 float32x2 { element.PositionSS.X + positionLS.X, element.PositionSS.Y + positionLS.Y },
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

	DrawChildren(element, element.ChildrenIDs);
	DrawChildren(element, element.FloatingChildrenIDs);
}

void Submit(uint32 screenWidth, uint32 screenHeight)
{
	CHECK(OpenID == INDEX_NONE);

	for (const ID rootID : RootIDs)
	{
		LayoutSize(rootID, true);
	}
	for (const ID rootID : RootIDs)
	{
		LayoutSize(rootID, false);
	}
	for (const ID rootID : RootIDs)
	{
		LayoutPosition(rootID, true, 0.0f);
	}
	for (const ID rootID : RootIDs)
	{
		LayoutPosition(rootID, false, 0.0f);
	}
	for (const ID rootID : RootIDs)
	{
		Draw(rootID);
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
	LastFrameRootIDs = RootIDs;

	Elements.Clear();
	RootIDs.Clear();

	OpenID = INDEX_NONE;

	MaxLayer = 0;
}

}
