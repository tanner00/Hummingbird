struct PixelInput
{
	float4 Position : SV_POSITION;
	float2 Uv : TEXCOORD0;
	float4 Color : COLOR0;
};

cbuffer PerFrameBuffer : register(b0)
{
	matrix ViewProjection;
	float2 UnitRange;
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

StructuredBuffer<Character> CharacterBuffer : register(t0);

PixelInput VertexMain(uint vertexID : SV_VertexID)
{
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

	const Character c = CharacterBuffer[characterIndex];

	const float2 position = c.ScreenPosition + c.Scale * (c.PlanePosition + c.PlaneSize * vertices[vertexIndex]);

	PixelInput result;
	result.Position = mul(ViewProjection, float4(position, 0.0f, 1.0f));
	result.Uv = c.AtlasPosition + c.AtlasSize * vertices[vertexIndex];
	result.Color = c.Color;
	return result;
}

Texture2D<float3> Texture : register(t1);
SamplerState Sampler : register(s0);

float Median(float3 v)
{
	return max(min(v.x, v.y), min(max(v.x, v.y), v.z));
}

float DistanceFieldRangeInScreenPixels(float2 uv)
{
	const float2 textureScreenSize = 1.0f / fwidth(uv);
	return max(0.5f * dot(UnitRange, textureScreenSize), 1.0f);
}

float4 PixelMain(PixelInput input) : SV_TARGET
{
	const float3 multiChannelSignedDistance = Texture.Sample(Sampler, input.Uv).rgb;
	const float signedDistance = Median(multiChannelSignedDistance);
	const float screenPixelDistance = DistanceFieldRangeInScreenPixels(input.Uv) * (signedDistance - 0.5f);

	const float insideBlend = clamp(screenPixelDistance + 0.5f, 0.0f, 1.0f);

	return float4(input.Color.rgb, insideBlend * input.Color.a);
}
