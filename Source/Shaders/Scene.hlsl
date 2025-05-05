#include "PBR.hlsli"
#include "Shadow.hlsli"
#include "Types.hlsli"

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

	uint VertexBufferIndex;
	uint PrimitiveBufferIndex;
	uint NodeBufferIndex;
	uint MaterialBufferIndex;
	uint DirectionalLightBufferIndex;
	uint PointLightsBufferIndex;
	uint AccelerationStructureIndex;

	uint PointLightsCount;
};
ConstantBuffer<Scene> Scene : register(b1);

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
	const ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[Scene.VertexBufferIndex];
	const StructuredBuffer<Primitive> primitiveBuffer = ResourceDescriptorHeap[Scene.PrimitiveBufferIndex];
	const StructuredBuffer<Material> materialBuffer = ResourceDescriptorHeap[Scene.MaterialBufferIndex];
	const ConstantBuffer<DirectionalLight> directionalLightBuffer = ResourceDescriptorHeap[Scene.DirectionalLightBufferIndex];
	const StructuredBuffer<PointLight> pointLightsBuffer = ResourceDescriptorHeap[Scene.PointLightsBufferIndex];
	const RaytracingAccelerationStructure accelerationStructure = ResourceDescriptorHeap[Scene.AccelerationStructureIndex];

	const Material material = materialBuffer[RootConstants.MaterialIndex];
	const Texture2D<float4> baseColorOrDiffuseTexture = ResourceDescriptorHeap[material.BaseColorOrDiffuseTextureIndex];
	const Texture2D<float3> normalMapTexture = ResourceDescriptorHeap[material.NormalMapTextureIndex];
	const Texture2D<float3> metallicRoughnessTexture = ResourceDescriptorHeap[material.MetallicRoughnessOrSpecularGlossinessTextureIndex];
	const Texture2D<float4> specularGlossinessTexture = ResourceDescriptorHeap[material.MetallicRoughnessOrSpecularGlossinessTextureIndex];

	const float4 baseColorOrDiffuse = baseColorOrDiffuseTexture.Sample(defaultSampler, input.TextureCoordinate);
	const float4 baseColorFactor = material.IsSpecularGlossiness ? 0.0f : material.BaseColorOrDiffuseFactor;
	const float4 diffuseFactor = material.IsSpecularGlossiness ? material.BaseColorOrDiffuseFactor : 0.0f;
	const float4 baseColor = baseColorFactor * baseColorOrDiffuse;
	const float4 diffuse = diffuseFactor * baseColorOrDiffuse;

	const float alpha = material.IsSpecularGlossiness ? diffuse.a : baseColor.a;

	[branch]
	if (alpha < material.AlphaCutoff && RootConstants.ViewMode != ViewMode::Geometry)
	{
		discard;
	}

	const float3 normal = normalize(input.Normal);
	const float3 tangent = normalize(input.Tangent.xyz);
	const float3 bitangent = normalize(cross(normal, tangent) * input.Tangent.w);
	const float3x3 tbn = transpose(float3x3(tangent, bitangent, normal));

	const float3 normalMap = normalMapTexture.Sample(defaultSampler, input.TextureCoordinate).xyz * 2.0f - 1.0f;
	const float3 shadeNormal = normalize(mul(tbn, normalMap));

	switch (RootConstants.ViewMode)
	{
	case ViewMode::Unlit:
		return material.IsSpecularGlossiness ? diffuse : baseColor;
	case ViewMode::Geometry:
		return ToColor(Hash(primitiveID));
	case ViewMode::Normal:
		return float4(SrgbToLinear(shadeNormal * 0.5f + 0.5f), 1.0f);
	default:
		break;
	}

	const float3 metallicRoughness = metallicRoughnessTexture.Sample(defaultSampler, input.TextureCoordinate);
	const float metallicFactor = material.IsSpecularGlossiness ? 0.0f : material.MetallicOrSpecularFactor.x;
	const float roughnessFactor = material.IsSpecularGlossiness ? 0.0f : material.RoughnessOrGlossinessFactor;
	const float metallic = metallicFactor * metallicRoughness.b;
	const float roughness = roughnessFactor * metallicRoughness.g;

	const float4 specularGlossiness = specularGlossinessTexture.Sample(defaultSampler, input.TextureCoordinate);
	const float3 specularFactor = material.IsSpecularGlossiness ? material.MetallicOrSpecularFactor : 0.0f;
	const float glossinessFactor = material.IsSpecularGlossiness ? material.RoughnessOrGlossinessFactor : 0.0f;
	const float3 specular = specularFactor * specularGlossiness.rgb;
	const float glossiness = glossinessFactor * SrgbToLinear(specularGlossiness.a);

	const float3 viewDirection = normalize(Scene.ViewPosition - input.WorldPosition);

	float3 finalColor = 0.0f;

	[loop]
	for (uint pointLightIndex = 0; pointLightIndex < Scene.PointLightsCount; ++pointLightIndex)
	{
		const PointLight pointLight = pointLightsBuffer[pointLightIndex];

		const float3 pointLightDirection = normalize(pointLight.Position - input.WorldPosition);
		const float objectToLightDistance = distance(pointLight.Position, input.WorldPosition);

		const float attenuation = 1.0f / (objectToLightDistance * objectToLightDistance);

		const float3 contribution = Pbr(baseColor.rgb,
										diffuse.rgb,
										metallic,
										specular,
										roughness,
										glossiness,
										shadeNormal,
										viewDirection,
										pointLightDirection,
										attenuation * pointLight.IntensityCandela * pointLight.Color,
										material.IsSpecularGlossiness);
		finalColor += contribution * CastShadowRay(input.WorldPosition,
												   pointLightDirection,
												   objectToLightDistance,
												   accelerationStructure,
												   vertexBuffer,
												   primitiveBuffer,
												   materialBuffer,
												   defaultSampler);
	}

	const float3 directionalLightDirection = normalize(directionalLightBuffer.Direction);

	const float3 directionalLightContribution = Pbr(baseColor.rgb,
													diffuse.rgb,
													metallic,
													specular,
													roughness,
													glossiness,
													shadeNormal,
													viewDirection,
													directionalLightDirection,
													directionalLightBuffer.IntensityLux * directionalLightBuffer.Color,
													material.IsSpecularGlossiness);
	finalColor += directionalLightContribution * CastShadowRay(input.WorldPosition,
															   directionalLightDirection,
															   Infinity,
															   accelerationStructure,
															   vertexBuffer,
															   primitiveBuffer,
															   materialBuffer,
															   defaultSampler);

	const float3 ambientLightContribution = 0.05f * directionalLightBuffer.IntensityLux * (material.IsSpecularGlossiness ? diffuse : baseColor).rgb;
	finalColor += ambientLightContribution;

	return float4(finalColor, alpha);
}
