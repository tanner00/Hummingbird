#include "PBR.hlsli"

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
	float3 WorldPosition : POSITION0;
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
	float3 ViewPosition;

	uint DefaultSamplerIndex;

	uint NodeBufferIndex;
	uint MaterialBufferIndex;
	uint DirectionalLightBufferIndex;
	uint PointLightsBufferIndex;

	uint PointLightsCount;
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
	float3 Color;
	float IntensityLux;

	float3 Direction;
};

struct PointLight
{
	float3 Color;
	float IntensityCandela;

	float3 Position;
};

PixelInput VertexStart(VertexInput input)
{
	const StructuredBuffer<Node> nodeBuffer = ResourceDescriptorHeap[Scene.NodeBufferIndex];
	const Node node = nodeBuffer[RootConstants.NodeIndex];

	const float4 worldPosition = mul(node.Transform, float4(input.Position, 1.0f));

	PixelInput result;
	result.Position = mul(Scene.ViewProjection, worldPosition);
	result.WorldPosition = worldPosition.xyz;
	result.TextureCoordinate = input.TextureCoordinate;
	result.Normal = mul((float3x3)RootConstants.NormalTransform, input.Normal);
	result.Tangent = float4(mul((float3x3)RootConstants.NormalTransform, input.Tangent.xyz), input.Tangent.w);
	return result;
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

	[branch]
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
	const float3 shadeNormal = normalize(mul(tbn, normalMap));

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

	const Texture2D<float4> metallicRoughnessTexture = ResourceDescriptorHeap[material.MetallicRoughnessOrSpecularGlossinessTextureIndex];
	const float4 metallicRoughness = metallicRoughnessTexture.Sample(defaultSampler, input.TextureCoordinate);

	const float metallicFactor = !material.IsSpecularGlossiness ? material.MetallicOrSpecularFactor.x : 0.0f;
	const float roughnessFactor = !material.IsSpecularGlossiness ? material.RoughnessOrGlossinessFactor : 0.0f;
	const float metallic = metallicFactor * metallicRoughness.b;
	const float roughness = roughnessFactor * metallicRoughness.g;

	const Texture2D<float4> specularGlossinessTexture = ResourceDescriptorHeap[material.MetallicRoughnessOrSpecularGlossinessTextureIndex];
	const float4 specularGlossiness = specularGlossinessTexture.Sample(defaultSampler, input.TextureCoordinate);

	const float3 specularFactor = material.IsSpecularGlossiness ? material.MetallicOrSpecularFactor : 0.0f;
	const float glossinessFactor = material.IsSpecularGlossiness ? material.RoughnessOrGlossinessFactor : 0.0f;
	const float3 specular = specularFactor * specularGlossiness.rgb;
	const float glossiness = glossinessFactor * SrgbToLinear(specularGlossiness.a);

	const float3 viewDirection = normalize(Scene.ViewPosition - input.WorldPosition);

	float3 finalColor = 0.0f;

	[branch]
	if (Scene.PointLightsCount != 0)
	{
		const StructuredBuffer<PointLight> pointLightsBuffer = ResourceDescriptorHeap[Scene.PointLightsBufferIndex];

		[loop]
		for (uint i = 0; i < Scene.PointLightsCount; ++i)
		{
			const PointLight pointLight = pointLightsBuffer[i];

			const float objectToLightDistance = distance(input.WorldPosition, pointLight.Position);
			const float attenuation = 1.0f / (objectToLightDistance * objectToLightDistance);

			const float3 contribution = Pbr(baseColor.rgb,
											diffuse.rgb,
											metallic,
											specular,
											roughness,
											glossiness,
											shadeNormal,
											viewDirection,
											normalize(pointLight.Position - input.WorldPosition),
											attenuation * pointLight.IntensityCandela * pointLight.Color,
											material.IsSpecularGlossiness);
			finalColor += contribution;
		}
	}

	const ConstantBuffer<DirectionalLight> directionalLightBuffer = ResourceDescriptorHeap[Scene.DirectionalLightBufferIndex];

	const float3 directionalLightContribution = Pbr(baseColor.rgb,
													diffuse.rgb,
													metallic,
													specular,
													roughness,
													glossiness,
													shadeNormal,
													viewDirection,
													normalize(directionalLightBuffer.Direction),
													directionalLightBuffer.IntensityLux * directionalLightBuffer.Color,
													material.IsSpecularGlossiness);
	finalColor += directionalLightContribution;

	const float3 ambientLightContribution = 0.05f * directionalLightBuffer.IntensityLux * max(baseColor.rgb, diffuse.rgb);
	finalColor += ambientLightContribution;

	return float4(finalColor, alpha);
}
