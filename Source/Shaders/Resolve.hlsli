#pragma once

#include "Common.hlsli"

float32x3 SampleTextureCatmullRom(Texture2D<float32x3> texture, uint2 textureDimensions, float32x2 uv)
{
	const float32x2 sampleTexel = uv * textureDimensions - 0.5f;
	const float32x2 f = frac(sampleTexel);

	const float32x2 weights[] =
	{
		float32x2(-0.5f * f.x * f.x * f.x + f.x * f.x - 0.5f * f.x, -0.5f * f.y * f.y * f.y + f.y * f.y - 0.5f * f.y),
		float32x2(1.5f * f.x * f.x * f.x - 2.5f * f.x * f.x + 1.0f, 1.5f * f.y * f.y * f.y - 2.5f * f.y * f.y + 1.0f),
		float32x2(-1.5f * f.x * f.x * f.x + 2.0f * f.x * f.x + 0.5f * f.x, -1.5f * f.y * f.y * f.y + 2.0f * f.y * f.y + 0.5f * f.y),
		float32x2(0.5f * f.x * f.x * f.x - 0.5f * f.x * f.x, 0.5f * f.y * f.y * f.y - 0.5f * f.y * f.y),
	};

	float32x3 sample = 0.0f;
	for (uint32 j = 0; j < 4; ++j)
	{
		const uint32 y = clamp(int32(sampleTexel.y) + (j - 1), 0, textureDimensions.y - 1);

		float32x3 row = 0.0f;
		for (uint32 i = 0; i < 4; ++i)
		{
			const uint32 x = clamp(int32(sampleTexel.x) + (i - 1), 0, textureDimensions.x - 1);

			row += texture.Load(uint3(x, y, 0)) * weights[i].x * weights[j].y;
		}

		sample += row;
	}

	return sample;
}

float32x3 RGBToYCoCg(float32x3 x)
{
	return float32x3(x.r * 0.25f + x.g * 0.5f + x.b * 0.25f, x.r * 0.5f - x.b * 0.5f, -x.r * 0.25f + x.g * 0.5f - x.b * 0.25f);
}

float32x3 YCoCgToRGB(float32x3 x)
{
	return float32x3(x.x + x.y - x.z, x.x + x.z, x.x - x.y - x.z);
}

float32x3 ToneMapReinhardYCoCg(float32x3 x)
{
	return float32x3(x.x / (1.0f + x.x), x.y, x.z);
}

float32x3 InverseToneMapReinhardYCoCg(float32x3 x)
{
	return float32x3(x.x / (1.0f - x.x), x.y, x.z);
}
