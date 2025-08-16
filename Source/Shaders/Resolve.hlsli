#pragma once

float3 SampleTextureCatmullRom(Texture2D<float3> texture, uint2 textureDimensions, float2 uv)
{
	const float2 sampleTexel = uv * textureDimensions - 0.5f;
	const float2 f = frac(sampleTexel);

	const float2 weights[] =
	{
		float2(-0.5f * f.x * f.x * f.x + f.x * f.x - 0.5f * f.x, -0.5f * f.y * f.y * f.y + f.y * f.y - 0.5f * f.y),
		float2(1.5f * f.x * f.x * f.x - 2.5f * f.x * f.x + 1.0f, 1.5f * f.y * f.y * f.y - 2.5f * f.y * f.y + 1.0f),
		float2(-1.5f * f.x * f.x * f.x + 2.0f * f.x * f.x + 0.5f * f.x, -1.5f * f.y * f.y * f.y + 2.0f * f.y * f.y + 0.5f * f.y),
		float2(0.5f * f.x * f.x * f.x - 0.5f * f.x * f.x, 0.5f * f.y * f.y * f.y - 0.5f * f.y * f.y),
	};

	float3 sample = 0.0f;
	for (uint j = 0; j < 4; ++j)
	{
		const uint y = clamp(int(sampleTexel.y) + (j - 1), 0, textureDimensions.y - 1);

		float3 row = 0.0f;
		for (uint i = 0; i < 4; ++i)
		{
			const uint x = clamp(int(sampleTexel.x) + (i - 1), 0, textureDimensions.x - 1);

			row += texture.Load(uint3(x, y, 0)) * weights[i].x * weights[j].y;
		}

		sample += row;
	}

	return sample;
}
