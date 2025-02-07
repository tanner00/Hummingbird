struct PixelInput
{
	float4 Position : SV_POSITION;
	float2 Uv : TEXCOORD0;
	float4 Color : COLOR0;
};

struct Character
{
	float4 Color;

	float2 ScreenPosition;

	float2 AtlasPosition;
	float2 AtlasSize;

	float2 PlanePosition;
	float2 PlaneSize;

	float Scale;
};

struct RootConstants
{
	matrix ViewProjection;
	float2 UnitRange;

	uint CharacterBuffer;
	uint Texture;
	uint Sampler;
};
ConstantBuffer<RootConstants> RootConstants : register(b0);

PixelInput VertexStart(uint vertexID : SV_VertexID)
{
	const StructuredBuffer<Character> characterBuffer = ResourceDescriptorHeap[RootConstants.CharacterBuffer];

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
	result.Uv = c.AtlasPosition + c.AtlasSize * vertices[vertexIndex];
	result.Color = c.Color;
	return result;
}

float Median(float3 v)
{
	return max(min(v.x, v.y), min(max(v.x, v.y), v.z));
}

float DistanceFieldRangeInScreenPixels(float2 uv)
{
	const float2 textureScreenSize = 1.0f / fwidth(uv);
	return max(0.5f * dot(RootConstants.UnitRange, textureScreenSize), 1.0f);
}

float4 PixelStart(PixelInput input) : SV_TARGET
{
	const Texture2D<float3> texture = ResourceDescriptorHeap[RootConstants.Texture];
	const SamplerState sampler = SamplerDescriptorHeap[RootConstants.Sampler];

	const float3 multiChannelSignedDistance = texture.Sample(sampler, input.Uv).rgb;
	const float signedDistance = Median(multiChannelSignedDistance);
	const float screenPixelDistance = DistanceFieldRangeInScreenPixels(input.Uv) * (signedDistance - 0.5f);

	const float insideBlend = clamp(screenPixelDistance + 0.5f, 0.0f, 1.0f);

	return float4(input.Color.rgb, insideBlend * input.Color.a);
}
