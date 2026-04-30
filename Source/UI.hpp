#pragma once

#include "RHI/RHI.hpp"

namespace UI
{

inline float32x4 Black = { 0.0f, 0.0f, 0.0f, 1.0f };
inline float32x4 White = { 1.0f, 1.0f, 1.0f, 1.0f };

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

#define UI_SIZE_FIT(...) UI::Size { .Mode = UI::Mode::Fit, .MinMax = { __VA_ARGS__ } }
#define UI_SIZE_GROW(...) UI::Size { .Mode = UI::Mode::Grow, .MinMax = { __VA_ARGS__ } }
#define UI_SIZE_FIXED(sizeSS) UI_SIZE_FIT(sizeSS, sizeSS)

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
	StringView IDName;

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

void Init();
void Shutdown();

void CreatePipeline();
void DestroyPipeline();

void DrawRectangle(float32x2 positionSS, float32x2 sizeSS, float32x3 rgb, usize layer = 0);
void DrawRectangle(float32x2 positionSS, float32x2 sizeSS, float32x4 rgba, usize layer = 0);

void DrawRoundedRectangle(float32x2 positionSS, float32x2 sizeSS, float32 cornerRadiusSS, float32x3 rgb, usize layer = 0);
void DrawRoundedRectangle(float32x2 positionSS, float32x2 sizeSS, float32 cornerRadiusSS, float32x4 rgba, usize layer = 0);

void DrawBorderedRectangle(float32x2 positionSS, float32x2 sizeSS, float32 borderSizeSS, float32x3 rgb, float32x3 borderRGB, usize layer = 0);
void DrawBorderedRectangle(float32x2 positionSS, float32x2 sizeSS, float32 borderSizeSS, float32x4 rgba, float32x4 borderRGBA, usize layer = 0);

void DrawBorderedRoundedRectangle(float32x2 positionSS, float32x2 sizeSS, float32 borderSizeSS, float32 cornerRadiusSS, float32x3 rgb, float32x3 borderRGB, usize layer = 0);
void DrawBorderedRoundedRectangle(float32x2 positionSS, float32x2 sizeSS, float32 borderSizeSS, float32 cornerRadiusSS, float32x4 rgba, float32x4 borderRGBA, usize layer = 0);

void DrawText(StringView text, float32x2 positionSS, float32 scale, float32x3 rgb, usize layer = 0);
void DrawText(StringView text, float32x2 positionSS, float32 scale, float32x4 rgba, usize layer = 0);

void DrawImage(const RHI::TextureView& image, float32x2 positionSS, float32x2 sizeSS, usize layer = 0);

float32 GetCharacterWidth(char c, float32 scale);
float32 GetTextWidth(StringView text, float32 scale);
float32 GetTextHeight(float32 scale);
float32x2 GetTextSize(StringView text, float32 scale);

void BeginElement(const Description& description);
void EndElement();

template<typename F>
void Container(const Description& description, const F& children)
{
	BeginElement(description);
	children();
	EndElement();
}

bool Rectangle(const Description& description);
void Text(StringView text, float32 scale, const Description& description);
void Image(const RHI::TextureView& image, const Description& description);

float32 GetWidth(StringView idName);
float32 GetHeight(StringView idName);
float32x2 GetSize(StringView idName);

bool IsHovered(StringView idName);
bool IsPressed(StringView idName);
bool IsPressedOnce(StringView idName);

void Submit(uint32 screenWidth, uint32 screenHeight);

}
