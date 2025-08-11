#pragma once

void CalculateBarycentrics(const float4 positionsClip[3],
						   float2 pixelScreen,
						   float2 screenSize,
						   out float3 weights,
						   out float3 ddxWeights,
						   out float3 ddyWeights)
{
	const float2 pixelNDC = float2(pixelScreen * 2.0f / screenSize - 1.0f) * float2(1.0f, -1.0f);

	const float3 inverseW = 1.0f / float3(positionsClip[0].w, positionsClip[1].w, positionsClip[2].w);

	const float2 positionsNDC[] =
	{
		positionsClip[0].xy * inverseW.x,
		positionsClip[1].xy * inverseW.y,
		positionsClip[2].xy * inverseW.z,
	};

	const float inverseDeterminant = 1.0f / determinant(float2x2(positionsNDC[2] - positionsNDC[1],
																 positionsNDC[0] - positionsNDC[1]));
	const float3 ddxBarycentrics = float3(positionsNDC[2].y - positionsNDC[1].y,
										  positionsNDC[0].y - positionsNDC[2].y,
										  positionsNDC[1].y - positionsNDC[0].y) * inverseDeterminant * inverseW * -1.0f;
	const float3 ddyBarycentrics = float3(positionsNDC[2].x - positionsNDC[1].x,
										  positionsNDC[0].x - positionsNDC[2].x,
										  positionsNDC[1].x - positionsNDC[0].x) * inverseDeterminant * inverseW;

	const float ddxBarycentricsSum = ddxBarycentrics.x + ddxBarycentrics.y + ddxBarycentrics.z;
	const float ddyBarycentricsSum = ddyBarycentrics.x + ddyBarycentrics.y + ddyBarycentrics.z;

	const float2 offsetNDC = pixelNDC - positionsNDC[0];

	const float interpolatedInverseW = inverseW.x + offsetNDC.x * ddxBarycentricsSum + offsetNDC.y * ddyBarycentricsSum;
	const float interpolatedW = 1.0f / interpolatedInverseW;

	weights.x = interpolatedW * (inverseW.x + offsetNDC.x * ddxBarycentrics.x + offsetNDC.y * ddyBarycentrics.x);
	weights.y = interpolatedW * (0.0f       + offsetNDC.x * ddxBarycentrics.y + offsetNDC.y * ddyBarycentrics.y);
	weights.z = interpolatedW * (0.0f       + offsetNDC.x * ddxBarycentrics.z + offsetNDC.y * ddyBarycentrics.z);

	const float ddxInterpolatedW = 1.0f / (interpolatedInverseW + ( ddxBarycentricsSum * 2.0f / screenSize.x));
	const float ddyInterpolatedW = 1.0f / (interpolatedInverseW + (-ddyBarycentricsSum * 2.0f / screenSize.y));

	ddxWeights = ddxInterpolatedW * (weights * interpolatedInverseW + ( ddxBarycentrics * 2.0f / screenSize.x)) - weights;
	ddyWeights = ddyInterpolatedW * (weights * interpolatedInverseW + (-ddyBarycentrics * 2.0f / screenSize.y)) - weights;
}

void CalculateBarycentrics(const float4 positionsClip[3],
						   float2 pixelScreen,
						   float2 screenSize,
						   out float3 weights)
{
	float3 unused1;
	float3 unused2;
	CalculateBarycentrics(positionsClip, pixelScreen, screenSize, weights, unused1, unused2);
}

float LerpBarycentrics(float3 weights, float vX, float vY, float vZ)
{
	return dot(weights, float3(vX, vY, vZ));
}

float2 LerpBarycentrics(float3 weights, float2 v1, float2 v2, float2 v3)
{
	return float2(LerpBarycentrics(weights, v1.x, v2.x, v3.x),
				  LerpBarycentrics(weights, v1.y, v2.y, v3.y));
}

float3 LerpBarycentrics(float3 weights, float3 v1, float3 v2, float3 v3)
{
	return float3(LerpBarycentrics(weights, v1.x, v2.x, v3.x),
				  LerpBarycentrics(weights, v1.y, v2.y, v3.y),
				  LerpBarycentrics(weights, v1.z, v2.z, v3.z));
}
