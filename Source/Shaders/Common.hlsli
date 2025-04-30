#pragma once

static const float Pi = 3.14159265358979323846f;
static const float Infinity = 1.#INF;

uint Hash(uint v)
{
	v ^= 2747636419;
	v *= 2654435769;
	v ^= v >> 16;
	v *= 2654435769;
	v ^= v >> 16;
	v *= 2654435769;
	return v;
}

float4 ToColor(uint v)
{
	return float4
	(
		float((v >>  0) & 0xFF) / 255.0f,
		float((v >>  8) & 0xFF) / 255.0f,
		float((v >> 16) & 0xFF) / 255.0f,
		1.0f
	);
}

template<typename T>
T SrgbToLinear(T x)
{
	return select(x < 0.04045f, x / 12.92f, pow((x + 0.055f) / 1.055f, 2.4f));
}
