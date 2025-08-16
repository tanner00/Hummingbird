#pragma once

float4 TransformLocalPositionToWorld(float3 positionLocal, matrix localToWorld)
{
	return mul(localToWorld, float4(positionLocal, 1.0f));
}

float3 TransformLocalDirectionToWorld(float3 directionLocal, matrix localToWorld)
{
	return mul((float3x3)localToWorld, directionLocal);
}

float4 TransformWorldToClip(float4 positionWorld, matrix worldToClip)
{
	return mul(worldToClip, positionWorld);
}

float2 TransformClipToNDC(float4 positionClip)
{
	return positionClip.xy / positionClip.w;
}

float4 TransformScreenToClip(float2 positionScreen, matrix screenToClip)
{
	return mul(screenToClip, float4(positionScreen, 0.0f, 1.0f));
}

float2 TransformScreenToNDC(float2 positionScreen, float2 screenSize)
{
	return float2(positionScreen * 2.0f / screenSize - 1.0f) * float2(1.0f, -1.0f);
}

float2 TransformClipToUV(float4 positionClip)
{
	return (TransformClipToNDC(positionClip) + 1.0f) / float2(2.0f, -2.0f);
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
