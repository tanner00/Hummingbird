#pragma once

#include "RHI/RHI.hpp"

namespace UI
{

inline float32x4 Black = { 0.0f, 0.0f, 0.0f, 1.0f };
inline float32x4 White = { 1.0f, 1.0f, 1.0f, 1.0f };

using ID = uint64;

enum class Mode : uint8
{
	Fit,
	Grow,
};

struct MinMax
{
	float32 Min;
	float32 Max;
};

struct Size
{
	Mode Mode;
	MinMax MinMax;
};

enum class Direction : uint8
{
	Vertical,
	Horizontal,
};

enum class Alignment : uint8
{
	Left,
	Top = Left,

	Center,

	Right,
	Bottom = Right,
};

struct Description
{
	ID ID;

	struct LayoutDescription
	{
		Size SizeX;
		Size SizeY;

		Direction Direction;

		Alignment AlignmentX;
		Alignment AlignmentY;

		float32x2 PaddingSS;
		float32 SpacingSS;
	} Layout;

	struct StyleDescription
	{
		float32x4 RGBA;

		float32x4 BorderRGBA;
		float32 BorderSizeSS;
		float32x4 BetweenRGBA;
		float32 BetweenSizeSS;

		float32 CornerRadiusSS;
	} Style;
};

inline float32x4 RGB(float32 r, float32 g, float32 b)
{
	return float32x4 { r, g, b, 1.0f };
}

inline float32x4 RGB(float32x3 rgb)
{
	return RGB(rgb.X, rgb.Y, rgb.Z);
}

void Init();
void Shutdown();

void CreatePipeline();
void DestroyPipeline();

void DrawRectangle(float32x2 positionSS, float32x2 sizeSS, float32x4 rgba, usize layer = 0);
void DrawRoundedRectangle(float32x2 positionSS, float32x2 sizeSS, float32 cornerRadiusSS, float32x4 rgba, usize layer = 0);
void DrawBorderedRectangle(float32x2 positionSS, float32x2 sizeSS, float32 borderSizeSS, float32x4 rgba, float32x4 borderRGBA, usize layer = 0);
void DrawBorderedRoundedRectangle(float32x2 positionSS, float32x2 sizeSS, float32 borderSizeSS, float32 cornerRadiusSS, float32x4 rgba, float32x4 borderRGBA, usize layer = 0);

void DrawText(StringView text, float32x2 positionSS, float32 scale, float32x4 rgba, usize layer = 0);

void DrawImage(const RHI::TextureView& image, float32x2 positionSS, float32x2 sizeSS, usize layer = 0);

inline ID NameToID(StringView name)
{
	return Hash<StringView>{}(name);
}

inline ID NameCombine(ID id, StringView name)
{
	return HashCombine(id, NameToID(name));
}

template<typename... Args>
ID NamesToID(const Args&... names)
{
	ID id = 0;
	((id = NameCombine(id, names)), ...);
	return id;
}

inline ID NamesToID(ArrayView<StringView> names)
{
	ID id = 0;
	for (const StringView name : names)
	{
		id = NameCombine(id, name);
	}
	return id;
}

inline Size Fit(float32 minSS = 0.0f, float32 maxSS = 0.0f)
{
	return Size { Mode::Fit, { minSS, maxSS } };
}

inline Size Fixed(float32 sizeSS = 0.0f)
{
	return Size { Mode::Fit, { sizeSS, sizeSS } };
}

inline Size Grow(float32 minSS = 0.0f, float32 maxSS = 0.0f)
{
	return Size { Mode::Grow, { minSS, maxSS } };
}

float32 GetCharacterWidth(char c, float32 scale);
float32 GetTextWidth(StringView text, float32 scale);
float32 GetTextHeight(float32 scale);
float32x2 GetTextSize(StringView text, float32 scale);

ID BeginElement(const Description& description);
void EndElement();

template<typename F>
ID Container(const Description& description, const F& children)
{
	const ID id = BeginElement(description);
	children();
	EndElement();
	return id;
}

ID Rectangle(const Description& description);
ID Text(StringView text, float32 scale, const Description& description);
ID Image(const RHI::TextureView& image, const Description& description);

float32 GetWidth(ID id);
float32 GetHeight(ID id);
float32x2 GetSize(ID id);

float32x2 GetPosition(ID id);

bool IsHovered(ID id);
bool IsPressed(ID id);
bool IsPressedOnce(ID id);

void Submit(uint32 screenWidth, uint32 screenHeight);

}
