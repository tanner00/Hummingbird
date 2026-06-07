#pragma once

#include "Base.hlsli"
#include "Transform.hlsli"

float32x3 SampleTextureCatmullRom(Texture2D<float32x3> texture, uint32x2 textureDimensions, float32x2 uv)
{
	const float32x2 samplePositionTS = TransformUVToTexel(uv, textureDimensions);
	const float32x2 f = frac(samplePositionTS);

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
		const uint32 yTS = clamp(int32(samplePositionTS.y) + (j - 1), 0, textureDimensions.y - 1);

		float32x3 row = 0.0f;
		for (uint32 i = 0; i < 4; ++i)
		{
			const uint32 xTS = clamp(int32(samplePositionTS.x) + (i - 1), 0, textureDimensions.x - 1);

			row += texture.Load(uint3(xTS, yTS, 0)) * weights[i].x * weights[j].y;
		}

		sample += row;
	}

	return sample;
}

float32x3 ToneMapReinhardYCoCg(float32x3 x)
{
	return float32x3(x.x / (1.0f + x.x), x.y, x.z);
}

float32x3 InverseToneMapReinhardYCoCg(float32x3 x)
{
	return float32x3(x.x / (1.0f - x.x), x.y, x.z);
}
