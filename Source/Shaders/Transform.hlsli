#pragma once

float4 TransformLocalPositionToWorld(float3 positionLS, matrix localToWorld)
{
	return mul(localToWorld, float4(positionLS, 1.0f));
}

float3 TransformLocalDirectionToWorld(float3 directionLS, matrix localToWorld)
{
	return mul((float3x3)localToWorld, directionLS);
}

float4 TransformWorldToClip(float4 positionWS, matrix worldToClip)
{
	return mul(worldToClip, positionWS);
}

float2 TransformClipToNDC(float4 positionCS)
{
	return positionCS.xy / positionCS.w;
}

float4 TransformScreenToClip(float2 positionSS, matrix screenToClip)
{
	return mul(screenToClip, float4(positionSS, 0.0f, 1.0f));
}

float2 TransformScreenToNDC(float2 positionSS, float2 screenSize)
{
	return float2(positionSS * 2.0f / screenSize - 1.0f) * float2(1.0f, -1.0f);
}

float2 TransformClipToUV(float4 positionCS)
{
	return (TransformClipToNDC(positionCS) + 1.0f) / float2(2.0f, -2.0f);
}

float4 TransformUVToClip(float2 uv)
{
	return float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}

float2 TransformTexelToUV(uint2 texel, uint2 textureDimensions)
{
	return (texel + 0.5f) / textureDimensions;
}

float2 TransformVertexIDToUV(uint vertexID)
{
	return float2(((2 - vertexID) << 1) & 2, (2 - vertexID) & 2);
}
