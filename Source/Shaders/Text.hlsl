#include "Types.hlsli"

struct PixelInput
{
	float4 Position : SV_POSITION;
	float2 TextureCoordinate : TEXCOORD0;
	float4 Color : COLOR0;
};

ConstantBuffer<TextRootConstants> RootConstants : register(b0);

PixelInput VertexStart(uint vertexID : SV_VertexID)
{
	const StructuredBuffer<Character> characterBuffer = ResourceDescriptorHeap[RootConstants.CharacterBufferIndex];

	static const float2 vertices[] =
	{
		{ 0.0f, 1.0f },
		{ 1.0f, 0.0f },
		{ 0.0f, 0.0f },

		{ 0.0f, 1.0f },
		{ 1.0f, 1.0f },
		{ 1.0f, 0.0f },
	};
	static const uint verticesPerQuad = 6;

	const uint characterIndex = vertexID / verticesPerQuad;
	const uint vertexIndex = vertexID % verticesPerQuad;

	const Character c = characterBuffer[characterIndex];

	const float2 position = c.ScreenPosition + c.Scale * (c.PlanePosition + c.PlaneSize * vertices[vertexIndex]);

	PixelInput result;
	result.Position = mul(RootConstants.ViewProjection, float4(position, 0.0f, 1.0f));
	result.TextureCoordinate = c.AtlasPosition + c.AtlasSize * vertices[vertexIndex];
	result.Color = c.Color;
	return result;
}

float Median(float3 v)
{
	return max(min(v.x, v.y), min(max(v.x, v.y), v.z));
}

float DistanceFieldRangeInScreenPixels(float2 textureCoordinate)
{
	const float2 textureScreenSize = 1.0f / fwidth(textureCoordinate);
	return max(0.5f * dot(RootConstants.UnitRange, textureScreenSize), 1.0f);
}

float4 PixelStart(PixelInput input) : SV_TARGET
{
	const Texture2D<float3> fontTexture = ResourceDescriptorHeap[RootConstants.FontTextureIndex];
	const SamplerState linearWrapSampler = SamplerDescriptorHeap[RootConstants.LinearWrapSampler];

	const float3 multiChannelSignedDistance = fontTexture.Sample(linearWrapSampler, input.TextureCoordinate).rgb;
	const float signedDistance = Median(multiChannelSignedDistance);
	const float screenPixelDistance = DistanceFieldRangeInScreenPixels(input.TextureCoordinate) * (signedDistance - 0.5f);

	const float insideBlend = saturate(screenPixelDistance + 0.5f);

	return float4(input.Color.rgb, insideBlend * input.Color.a);
}
