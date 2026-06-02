#pragma once

#include "Common.hlsli"

float32x3 PerspectiveDivide(float32x4 homogeneous)
{
	return homogeneous.xyz / homogeneous.w;
}

float32x4 TransformLocalPositionToWorld(float32x3 positionLS, float32x4x4 localToWorld)
{
	return mul(localToWorld, float32x4(positionLS, 1.0f));
}

float32x3 TransformLocalDirectionToWorld(float32x3 directionLS, float32x4x4 localToWorld)
{
	return mul((float32x3x3)localToWorld, directionLS);
}

float32x4 TransformWorldToClip(float32x4 positionWS, float32x4x4 worldToClip)
{
	return mul(worldToClip, positionWS);
}

float32x3 TransformClipToWorld(float32x4 positionCS, float32x4x4 clipToWorld)
{
	return PerspectiveDivide(mul(clipToWorld, positionCS));
}

float32x3 TransformClipToNDC(float32x4 positionCS)
{
	return PerspectiveDivide(positionCS);
}

float32x4 TransformScreenToClip(float32x2 positionSS, float32x4x4 screenToClip)
{
	return mul(screenToClip, float32x4(positionSS, 0.0f, 1.0f));
}

float32x2 TransformScreenToNDC(float32x2 positionSS, float32x2 screenSize)
{
	return float32x2(positionSS * 2.0f / screenSize - 1.0f) * float32x2(1.0f, -1.0f);
}

float32x2 TransformClipToUV(float32x4 positionCS)
{
	return (TransformClipToNDC(positionCS).xy + 1.0f) / float32x2(2.0f, -2.0f);
}

float32x4 TransformUVToClip(float32x2 uv)
{
	return float32x4(uv * float32x2(2.0f, -2.0f) + float32x2(-1.0f, 1.0f), 0.0f, 1.0f);
}

float32x2 TransformTexelToUV(float32x2 texel, uint32x2 textureDimensions)
{
	return (texel + 0.5f) / textureDimensions;
}

float32x2 TransformUVToTexel(float32x2 uv, uint32x2 textureDimensions)
{
	return uv * textureDimensions - 0.5f;
}

float32x2 TransformVertexIDToUV(uint32 vertexID)
{
	return float32x2(((2 - vertexID) << 1) & 2, (2 - vertexID) & 2);
}
