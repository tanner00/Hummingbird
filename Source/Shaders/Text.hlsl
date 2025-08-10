#include "Text.hlsli"
#include "Types.hlsli"

struct PixelInput
{
	float4 PositionClip : SV_POSITION;
	float2 UV : TEXCOORD0;
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

	const float2 positionScreen = c.PositionScreen + c.Scale * (c.PlanePosition + c.PlaneSize * vertices[vertexIndex]);

	PixelInput result;
	result.PositionClip = mul(RootConstants.ScreenToClip, float4(positionScreen, 0.0f, 1.0f));
	result.UV = c.AtlasPosition + c.AtlasSize * vertices[vertexIndex];
	result.Color = c.Color;
	return result;
}

float4 PixelStart(PixelInput input) : SV_TARGET
{
	const Texture2D<float3> fontTexture = ResourceDescriptorHeap[RootConstants.FontTextureIndex];
	const SamplerState linearWrapSampler = SamplerDescriptorHeap[RootConstants.LinearWrapSampler];

	const float3 multiChannelSignedDistance = fontTexture.Sample(linearWrapSampler, input.UV);
	const float signedDistance = Median(multiChannelSignedDistance);
	const float distanceScreen = CalculateDistanceFieldRangeScreen(input.UV, RootConstants.UnitRange) * (signedDistance - 0.5f);

	const float insideBlend = saturate(distanceScreen + 0.5f);

	return float4(input.Color.rgb, insideBlend * input.Color.a);
}
