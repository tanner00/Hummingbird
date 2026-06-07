#pragma once

#include "Math.hlsli"
#include "Transform.hlsli"

float32x3 PerspectiveCorrectBarycentrics(float32x3 barycentrics, float32x3 w)
{
	const float32x3 barycentricsOverW = barycentrics / w;
	return barycentricsOverW / (barycentricsOverW.x + barycentricsOverW.y + barycentricsOverW.z);
}

void CalculateScreenBarycentrics(const float32x4 positionsCS[3],
								 float32x2 pixelSS,
								 float32x2 screenSize,
								 out float32x3 weights,
								 out float32x3 ddxWeights,
								 out float32x3 ddyWeights)
{
	const float32x2 positionsNDC[] =
	{
		PerspectiveDivide(positionsCS[0]).xy,
		PerspectiveDivide(positionsCS[1]).xy,
		PerspectiveDivide(positionsCS[2]).xy,
	};

	const float32 signedAreaNDC = determinant(float32x2x2(positionsNDC[2] - positionsNDC[1], positionsNDC[0] - positionsNDC[1]));

	const float32x3 ddxWeightsNDC = float32x3(positionsNDC[1].y - positionsNDC[2].y,
											  positionsNDC[2].y - positionsNDC[0].y,
											  positionsNDC[0].y - positionsNDC[1].y) / ToSafeDenominator(signedAreaNDC);
	const float32x3 ddyWeightsNDC = float32x3(positionsNDC[2].x - positionsNDC[1].x,
											  positionsNDC[0].x - positionsNDC[2].x,
											  positionsNDC[1].x - positionsNDC[0].x) / ToSafeDenominator(signedAreaNDC);

	const float32x2 offsetNDC = TransformScreenToNDC(pixelSS, screenSize) - positionsNDC[0];

	const float32x3 weightsNDC = float32x3(1.0f, 0.0f, 0.0f) + offsetNDC.x * ddxWeightsNDC + offsetNDC.y * ddyWeightsNDC;

	const float32x3 w = float32x3(positionsCS[0].w, positionsCS[1].w, positionsCS[2].w);

	weights = PerspectiveCorrectBarycentrics(weightsNDC, w);

	const float32 ddxNDC = 2.0f / screenSize.x;
	const float32 ddyNDC = -2.0f / screenSize.y;

	ddxWeights = PerspectiveCorrectBarycentrics(weightsNDC + ddxWeightsNDC * ddxNDC, w) - weights;
	ddyWeights = PerspectiveCorrectBarycentrics(weightsNDC + ddyWeightsNDC * ddyNDC, w) - weights;
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
