struct VertexInput
{
	float3 Position : POSITION0;
	float2 TextureCoordinate : TEXCOORD0;
	float3 Normal : NORMAL0;
	float4 Tangent : TANGENT0;
};

struct PixelInput
{
	float4 Position : SV_POSITION;
	float2 TextureCoordinate : TEXCOORD0;
	float3 Normal : NORMAL0;
	float4 Tangent: TANGENT0;
};

enum class ViewMode : uint
{
	Lit,
	Unlit,
	Geometry,
	Normal,
};

struct RootConstants
{
	uint NodeIndex;
	uint MaterialIndex;

	ViewMode ViewMode;

	matrix NormalTransform;
};
ConstantBuffer<RootConstants> RootConstants : register(b0);

struct Scene
{
	matrix ViewProjection;

	uint DefaultSamplerIndex;

	uint NodeBufferIndex;
	uint MaterialBufferIndex;
	uint DirectionalLightBufferIndex;
};
ConstantBuffer<Scene> Scene : register(b1);

struct Node
{
	matrix Transform;
};

struct Material
{
	uint BaseColorOrDiffuseTextureIndex;
	float4 BaseColorOrDiffuseFactor;

	uint NormalMapTextureIndex;

	uint MetallicRoughnessOrSpecularGlossinessTextureIndex;
	float3 MetallicOrSpecularFactor;
	float RoughnessOrGlossinessFactor;
	bool IsSpecularGlossiness;

	float AlphaCutoff;
};

struct DirectionalLight
{
	float3 Direction;
};

PixelInput VertexStart(VertexInput input)
{
	const StructuredBuffer<Node> nodeBuffer = ResourceDescriptorHeap[Scene.NodeBufferIndex];
	const Node node = nodeBuffer[RootConstants.NodeIndex];

	PixelInput result;
	result.Position = mul(Scene.ViewProjection, mul(node.Transform, float4(input.Position, 1.0f)));
	result.TextureCoordinate = input.TextureCoordinate;
	result.Normal = mul((float3x3)RootConstants.NormalTransform, input.Normal);
	result.Tangent = float4(mul((float3x3)RootConstants.NormalTransform, input.Tangent.xyz), input.Tangent.w);
	return result;
}

uint Hash(uint v)
{
	v ^= 2747636419;
	v *= 2654435769;
	v ^= v >> 16;
	v *= 2654435769;
	v ^= v >> 16;
	v *= 2654435769;
	return v;
}

float4 ToColor(uint v)
{
	return float4
	(
		float((v >>  0) & 0xFF) / 255.0f,
		float((v >>  8) & 0xFF) / 255.0f,
		float((v >> 16) & 0xFF) / 255.0f,
		1.0f
	);
}

float3 SrgbToLinear(float3 x)
{
	return select(x < 0.04045f, x / 12.92f, pow((x + 0.055f) / 1.055f, 2.4f));
}

float4 PixelStart(PixelInput input, uint primitiveID : SV_PrimitiveID) : SV_TARGET
{
	const SamplerState defaultSampler = ResourceDescriptorHeap[Scene.DefaultSamplerIndex];

	const StructuredBuffer<Material> materialBuffer = ResourceDescriptorHeap[Scene.MaterialBufferIndex];
	const Material material = materialBuffer[RootConstants.MaterialIndex];

	const Texture2D<float4> baseColorOrDiffuseTexture = ResourceDescriptorHeap[material.BaseColorOrDiffuseTextureIndex];
	const float4 baseColorOrDiffuse = baseColorOrDiffuseTexture.Sample(defaultSampler, input.TextureCoordinate);

	const float4 diffuseFactor = material.IsSpecularGlossiness ? material.BaseColorOrDiffuseFactor : 0.0f;
	const float4 baseColorFactor = !material.IsSpecularGlossiness ? material.BaseColorOrDiffuseFactor : 0.0f;

	const float4 diffuse = diffuseFactor * baseColorOrDiffuse;
	const float4 baseColor = baseColorFactor * baseColorOrDiffuse;

	const float alpha = max(diffuse.a, baseColor.a);
	if (alpha < material.AlphaCutoff && RootConstants.ViewMode != ViewMode::Geometry)
	{
		discard;
	}

	const Texture2D<float3> normalMapTexture = ResourceDescriptorHeap[material.NormalMapTextureIndex];

	const float3 normal = normalize(input.Normal);
	const float3 tangent = normalize(input.Tangent.xyz);
	const float3 bitangent = normalize(cross(normal, tangent) * input.Tangent.w);

	const float3x3 tbn = transpose(float3x3(tangent, bitangent, normal));
	const float3 normalMap = normalMapTexture.Sample(defaultSampler, input.TextureCoordinate).xyz * 2.0f - 1.0f;
	const float3 shadeNormal = mul(tbn, normalMap);

	switch (RootConstants.ViewMode)
	{
	case ViewMode::Unlit:
		return max(diffuse, baseColor);
	case ViewMode::Geometry:
		return ToColor(Hash(primitiveID));
	case ViewMode::Normal:
		return float4(SrgbToLinear(shadeNormal * 0.5f + 0.5f), 1.0f);
	default:
		break;
	}

	const ConstantBuffer<DirectionalLight> directionalLight = ResourceDescriptorHeap[Scene.DirectionalLightBufferIndex];

	const float3 lightDirection = normalize(-directionalLight.Direction);

	const float3 diffuseStrength = saturate(dot(shadeNormal, lightDirection));
	const float3 ambientStrength = 0.15f;

	float4 finalColor;
	if (material.IsSpecularGlossiness)
	{
		finalColor = float4(diffuse.rgb * (diffuseStrength + ambientStrength), diffuse.a);
	}
	else
	{
		finalColor = float4(baseColor.rgb * (diffuseStrength + ambientStrength), baseColor.a);
	}
	return finalColor;
}
