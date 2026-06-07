#pragma once

#include "Base.hlsli"

template<typename T>
T SRGBToLinear(T x)
{
	return select(x < 0.04045f, x / 12.92f, pow((x + 0.055f) / 1.055f, 2.4f));
}

template<typename T>
T LinearToSRGB(T x)
{
	return select(x <= 0.0031308f, x * 12.92f, 1.055f * pow(x, 1.0f / 2.4f) - 0.055f);
}

float32x3 UInt32ToRGB(uint32 x)
{
	return float32x3((x >> 0) & 0xff, (x >> 8) & 0xff, (x >> 16) & 0xff) / 255.0f;
}

float32x3 RGBToYCoCg(float32x3 x)
{
	return float32x3(x.r * 0.25f + x.g * 0.5f + x.b * 0.25f, x.r * 0.5f - x.b * 0.5f, -x.r * 0.25f + x.g * 0.5f - x.b * 0.25f);
}

float32x3 YCoCgToRGB(float32x3 x)
{
	return float32x3(x.x + x.y - x.z, x.x + x.z, x.x - x.y - x.z);
}
