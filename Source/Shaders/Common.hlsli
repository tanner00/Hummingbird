#pragma once

typedef uint16_t uint16;

typedef int int32;
typedef int2 int32x2;
typedef int3 int32x3;

typedef uint uint32;
typedef uint2 uint32x2;
typedef uint3 uint32x3;

typedef bool bool32;

typedef float float32;

typedef float2 float32x2;
typedef float3 float32x3;
typedef float4 float32x4;

typedef float2x2 float32x2x2;
typedef float3x3 float32x3x3;
typedef float4x4 float32x4x4;

static const float32 Pi = 3.14159265358979323846f;
static const float32 Infinity = 1.#INF;

uint32 Hash(uint32 x)
{
	x ^= 2747636419;
	x *= 2654435769;
	x ^= x >> 16;
	x *= 2654435769;
	x ^= x >> 16;
	x *= 2654435769;
	return x;
}

float32x3 ToRGB(uint32 x)
{
	return float32x3(float32((x >> 0) & 0xff) / 255.0f,
					 float32((x >> 8) & 0xff) / 255.0f,
					 float32((x >> 16) & 0xff) / 255.0f);
}

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
