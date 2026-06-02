#pragma once

#include "Transform.hlsli"

void CalculateScreenBarycentrics(const float32x4 positionsCS[3],
								 float32x2 pixelSS,
								 float32x2 screenSize,
								 out float32x3 weights,
								 out float32x3 ddxWeights,
								 out float32x3 ddyWeights)
{
	const float32x2 pixelNDC = TransformScreenToNDC(pixelSS, screenSize);

	const float32x3 inverseW = 1.0f / float32x3(positionsCS[0].w, positionsCS[1].w, positionsCS[2].w);

	const float32x2 positionsNDC[] =
	{
		positionsCS[0].xy * inverseW.x,
		positionsCS[1].xy * inverseW.y,
		positionsCS[2].xy * inverseW.z,
	};

	const float32 inverseDeterminant = 1.0f / determinant(float32x2x2(positionsNDC[2] - positionsNDC[1],
																	  positionsNDC[0] - positionsNDC[1]));
	const float32x3 ddxBarycentrics = float32x3(positionsNDC[2].y - positionsNDC[1].y,
												positionsNDC[0].y - positionsNDC[2].y,
												positionsNDC[1].y - positionsNDC[0].y) * inverseDeterminant * inverseW * -1.0f;
	const float32x3 ddyBarycentrics = float32x3(positionsNDC[2].x - positionsNDC[1].x,
												positionsNDC[0].x - positionsNDC[2].x,
												positionsNDC[1].x - positionsNDC[0].x) * inverseDeterminant * inverseW;

	const float32 ddxBarycentricsSum = ddxBarycentrics.x + ddxBarycentrics.y + ddxBarycentrics.z;
	const float32 ddyBarycentricsSum = ddyBarycentrics.x + ddyBarycentrics.y + ddyBarycentrics.z;

	const float32x2 offsetNDC = pixelNDC - positionsNDC[0];

	const float32 interpolatedInverseW = inverseW.x + offsetNDC.x * ddxBarycentricsSum + offsetNDC.y * ddyBarycentricsSum;
	const float32 interpolatedW = 1.0f / interpolatedInverseW;

	weights.x = interpolatedW * (inverseW.x + offsetNDC.x * ddxBarycentrics.x + offsetNDC.y * ddyBarycentrics.x);
	weights.y = interpolatedW * (0.0f + offsetNDC.x * ddxBarycentrics.y + offsetNDC.y * ddyBarycentrics.y);
	weights.z = interpolatedW * (0.0f + offsetNDC.x * ddxBarycentrics.z + offsetNDC.y * ddyBarycentrics.z);

	const float32 ddxInterpolatedW = 1.0f / (interpolatedInverseW + (ddxBarycentricsSum * 2.0f / screenSize.x));
	const float32 ddyInterpolatedW = 1.0f / (interpolatedInverseW + (-ddyBarycentricsSum * 2.0f / screenSize.y));

	ddxWeights = ddxInterpolatedW * (weights * interpolatedInverseW + (ddxBarycentrics * 2.0f / screenSize.x)) - weights;
	ddyWeights = ddyInterpolatedW * (weights * interpolatedInverseW + (-ddyBarycentrics * 2.0f / screenSize.y)) - weights;
}

void CalculateScreenBarycentrics(const float32x4 positionsCS[3],
								 float32x2 pixelSS,
								 float32x2 screenSize,
								 out float32x3 weights)
{
	float32x3 unused1;
	float32x3 unused2;
	CalculateScreenBarycentrics(positionsCS, pixelSS, screenSize, weights, unused1, unused2);
}

float32 LerpBarycentrics(float32x3 weights, float32 vX, float32 vY, float32 vZ)
{
	return dot(weights, float32x3(vX, vY, vZ));
}

float32x2 LerpBarycentrics(float32x3 weights, float32x2 v1, float32x2 v2, float32x2 v3)
{
	return float32x2(LerpBarycentrics(weights, v1.x, v2.x, v3.x),
					 LerpBarycentrics(weights, v1.y, v2.y, v3.y));
}

float32x3 LerpBarycentrics(float32x3 weights, float32x3 v1, float32x3 v2, float32x3 v3)
{
	return float32x3(LerpBarycentrics(weights, v1.x, v2.x, v3.x),
					 LerpBarycentrics(weights, v1.y, v2.y, v3.y),
					 LerpBarycentrics(weights, v1.z, v2.z, v3.z));
}
