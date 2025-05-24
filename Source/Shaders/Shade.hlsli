#pragma once

#include "Common.hlsli"
#include "PBR.hlsli"
#include "Shadow.hlsli"
#include "Types.hlsli"

struct ScenePixelInput
{
	float4 ClipSpacePosition : SV_POSITION;
	float3 WorldSpacePosition : POSITION0;
	float2 TextureCoordinate : TEXCOORD0;
	float3 Normal : NORMAL0;
	float4 Tangent : TANGENT0;
};

float4 Shade(Scene scene,
			 ViewMode viewMode,
			 uint primitiveIndex,
			 ScenePixelInput pixel,
#if DEFERRED
			 float2 ddx,
			 float2 ddy,
#endif
			 uint anisotropicWrapSamplerIndex,
			 uint primitiveID)
{
	pixel.Normal = normalize(pixel.Normal);
	pixel.Tangent = normalize(pixel.Tangent);

	const SamplerState anisotropicWrapSampler = ResourceDescriptorHeap[anisotropicWrapSamplerIndex];
	const ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[scene.VertexBufferIndex];
	const StructuredBuffer<Primitive> primitiveBuffer = ResourceDescriptorHeap[scene.PrimitiveBufferIndex];
	const StructuredBuffer<Material> materialBuffer = ResourceDescriptorHeap[scene.MaterialBufferIndex];
	const ConstantBuffer<DirectionalLight> directionalLightBuffer = ResourceDescriptorHeap[scene.DirectionalLightBufferIndex];
	const StructuredBuffer<PointLight> pointLightsBuffer = ResourceDescriptorHeap[scene.PointLightsBufferIndex];
	const RaytracingAccelerationStructure accelerationStructure = ResourceDescriptorHeap[scene.AccelerationStructureIndex];

	const Primitive primitive = primitiveBuffer[primitiveIndex];
	const Material material = materialBuffer[primitive.MaterialIndex];

	const Texture2D<float4> baseColorOrDiffuseTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.BaseColorOrDiffuseTextureIndex)];
	const Texture2D<float3> normalMapTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.NormalMapTextureIndex)];
	const Texture2D<float3> metallicRoughnessTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.MetallicRoughnessOrSpecularGlossinessTextureIndex)];
	const Texture2D<float4> specularGlossinessTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.MetallicRoughnessOrSpecularGlossinessTextureIndex)];

#if FORWARD
	const float4 baseColorOrDiffuse = baseColorOrDiffuseTexture.Sample(anisotropicWrapSampler, pixel.TextureCoordinate);
	const float3 normalMap = normalMapTexture.Sample(anisotropicWrapSampler, pixel.TextureCoordinate).xyz * 2.0f - 1.0f;
	const float3 metallicRoughness = metallicRoughnessTexture.Sample(anisotropicWrapSampler, pixel.TextureCoordinate);
	const float4 specularGlossiness = specularGlossinessTexture.Sample(anisotropicWrapSampler, pixel.TextureCoordinate);
#else
	const float4 baseColorOrDiffuse = baseColorOrDiffuseTexture.SampleGrad(anisotropicWrapSampler, pixel.TextureCoordinate, ddx, ddy);
	const float3 normalMap = normalMapTexture.SampleGrad(anisotropicWrapSampler, pixel.TextureCoordinate, ddx, ddy).xyz * 2.0f - 1.0f;
	const float3 metallicRoughness = metallicRoughnessTexture.SampleGrad(anisotropicWrapSampler, pixel.TextureCoordinate, ddx, ddy);
	const float4 specularGlossiness = specularGlossinessTexture.SampleGrad(anisotropicWrapSampler, pixel.TextureCoordinate, ddx, ddy);
#endif

	const float4 baseColorFactor = material.IsSpecularGlossiness ? 0.0f : material.BaseColorOrDiffuseFactor;
	const float4 diffuseFactor = material.IsSpecularGlossiness ? material.BaseColorOrDiffuseFactor : 0.0f;
	const float4 baseColor = baseColorFactor * baseColorOrDiffuse;
	const float4 diffuse = diffuseFactor * baseColorOrDiffuse;

	const float alpha = material.IsSpecularGlossiness ? diffuse.a : baseColor.a;

#if FORWARD
	[branch]
	if (alpha < material.AlphaCutoff && viewMode != ViewMode::Geometry)
	{
		discard;
	}
#endif

	const float3 bitangent = normalize(cross(pixel.Normal, pixel.Tangent.xyz) * pixel.Tangent.w);
	const float3x3 tbn = transpose(float3x3(pixel.Tangent.xyz, bitangent, pixel.Normal));

	const float3 shadeNormal = normalize(mul(tbn, normalMap));

	const float metallicFactor = material.IsSpecularGlossiness ? 0.0f : material.MetallicOrSpecularFactor.x;
	const float roughnessFactor = material.IsSpecularGlossiness ? 0.0f : material.RoughnessOrGlossinessFactor;
	const float metallic = metallicFactor * metallicRoughness.b;
	const float roughness = roughnessFactor * metallicRoughness.g;

	const float3 specularFactor = material.IsSpecularGlossiness ? material.MetallicOrSpecularFactor : 0.0f;
	const float glossinessFactor = material.IsSpecularGlossiness ? material.RoughnessOrGlossinessFactor : 0.0f;
	const float3 specular = specularFactor * specularGlossiness.rgb;
	const float glossiness = glossinessFactor * SrgbToLinear(specularGlossiness.a);

	[branch]
	switch (viewMode)
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

	const float3 viewDirection = normalize(scene.ViewPosition - pixel.WorldSpacePosition);

	float3 finalColor = 0.0f;

	[loop]
	for (uint pointLightIndex = 0; pointLightIndex < scene.PointLightsCount; ++pointLightIndex)
	{
		const PointLight pointLight = pointLightsBuffer[pointLightIndex];

		const float3 pointLightDirection = normalize(pointLight.Position - pixel.WorldSpacePosition);
		const float objectToLightDistance = distance(pointLight.Position, pixel.WorldSpacePosition);

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
		finalColor += contribution * CastShadowRay(pixel.WorldSpacePosition,
												   pointLightDirection,
												   objectToLightDistance,
												   accelerationStructure,
												   vertexBuffer,
												   primitiveBuffer,
												   materialBuffer,
												   anisotropicWrapSampler);
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
	finalColor += directionalLightContribution * CastShadowRay(pixel.WorldSpacePosition,
															   directionalLightDirection,
															   Infinity,
															   accelerationStructure,
															   vertexBuffer,
															   primitiveBuffer,
															   materialBuffer,
															   anisotropicWrapSampler);

	const float3 ambientLightContribution = 0.05f * directionalLightBuffer.IntensityLux * (material.IsSpecularGlossiness ? diffuse : baseColor).rgb;
	finalColor += ambientLightContribution;

	return float4(finalColor, alpha);
}
