#pragma once

void CalculateScreenSpaceBarycentrics(const float4 clipSpacePositions[3],
									  float2 screenSpacePixel,
									  float2 screenSize,
									  out float3 weights,
									  out float3 ddxWeights,
									  out float3 ddyWeights)
{
	const float3 inverseW = 1.0f / float3(clipSpacePositions[0].w, clipSpacePositions[1].w, clipSpacePositions[2].w);

	const float2 ndcSpacePositions[] =
	{
		clipSpacePositions[0].xy * inverseW.x,
		clipSpacePositions[1].xy * inverseW.y,
		clipSpacePositions[2].xy * inverseW.z,
	};

	float2 ndcSpacePixel = float2(screenSpacePixel * 2.0f / screenSize - 1.0f);
	ndcSpacePixel.y = -ndcSpacePixel.y;

	const float inverseDeterminant = 1.0f / determinant(float2x2(ndcSpacePositions[2] - ndcSpacePositions[1],
																 ndcSpacePositions[0] - ndcSpacePositions[1]));
	const float3 ddxBarycentrics = float3(ndcSpacePositions[2].y - ndcSpacePositions[1].y,
										  ndcSpacePositions[0].y - ndcSpacePositions[2].y,
										  ndcSpacePositions[1].y - ndcSpacePositions[0].y) * inverseDeterminant * inverseW * -1.0f;
	const float3 ddyBarycentrics = float3(ndcSpacePositions[2].x - ndcSpacePositions[1].x,
										  ndcSpacePositions[0].x - ndcSpacePositions[2].x,
										  ndcSpacePositions[1].x - ndcSpacePositions[0].x) * inverseDeterminant * inverseW;

	const float ddxBarycentricsSum = ddxBarycentrics.x + ddxBarycentrics.y + ddxBarycentrics.z;
	const float ddyBarycentricsSum = ddyBarycentrics.x + ddyBarycentrics.y + ddyBarycentrics.z;

	const float2 ndcSpaceOffset = ndcSpacePixel - ndcSpacePositions[0];

	const float interpolatedInverseW = inverseW.x + ndcSpaceOffset.x * ddxBarycentricsSum + ndcSpaceOffset.y * ddyBarycentricsSum;
	const float interpolatedW = 1.0f / interpolatedInverseW;

	weights.x = interpolatedW * (inverseW.x + ndcSpaceOffset.x * ddxBarycentrics.x + ndcSpaceOffset.y * ddyBarycentrics.x);
	weights.y = interpolatedW * (0.0f       + ndcSpaceOffset.x * ddxBarycentrics.y + ndcSpaceOffset.y * ddyBarycentrics.y);
	weights.z = interpolatedW * (0.0f       + ndcSpaceOffset.x * ddxBarycentrics.z + ndcSpaceOffset.y * ddyBarycentrics.z);

	const float ddxInterpolatedW = 1.0f / (interpolatedInverseW + ( ddxBarycentricsSum * 2.0f / screenSize.x));
	const float ddyInterpolatedW = 1.0f / (interpolatedInverseW + (-ddyBarycentricsSum * 2.0f / screenSize.y));

	ddxWeights = ddxInterpolatedW * (weights * interpolatedInverseW + ( ddxBarycentrics * 2.0f / screenSize.x)) - weights;
	ddyWeights = ddyInterpolatedW * (weights * interpolatedInverseW + (-ddyBarycentrics * 2.0f / screenSize.y)) - weights;
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
