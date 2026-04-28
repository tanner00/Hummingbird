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

float32x3 ToneMapReinhard(float32x3 x)
{
	return x / (1.0f + x);
}

float32x3 InverseToneMapReinhard(float32x3 x)
{
	return x / (1.0f - x);
}
