#pragma once

#include "Common.hlsli"
#include "PBR.hlsli"
#include "Shadow.hlsli"
#include "Types.hlsli"

void ComputeTangents(float3 normalWorld,
					 float3 ddxPositionWorld,
					 float3 ddyPositionWorld,
					 float2 ddxTextureCoordinate,
					 float2 ddyTextureCoordinate,
					 out float3 tangentWorld,
					 out float3 bitangentWorld)
{
	const float3 ddxPositionPerpendicularWorld = -cross(ddxPositionWorld, normalWorld);
	const float3 ddyPositionPerpendicularWorld = cross(ddyPositionWorld, normalWorld);

	tangentWorld = ddyPositionPerpendicularWorld * ddxTextureCoordinate.x + ddxPositionPerpendicularWorld * ddyTextureCoordinate.x;
	bitangentWorld = ddyPositionPerpendicularWorld * ddxTextureCoordinate.y + ddxPositionPerpendicularWorld * ddyTextureCoordinate.y;

	const float inverseScale = rsqrt(max(dot(tangentWorld, tangentWorld), dot(bitangentWorld, bitangentWorld)));

	tangentWorld *= inverseScale;
	bitangentWorld *= inverseScale;
}

float4 Shade(Scene scene,
			 float3 positionWorld,
			 float2 textureCoordinate,
			 float3 normalWorld,
			 uint primitiveIndex,
			 float3 ddxPositionWorld,
			 float3 ddyPositionWorld,
			 float2 ddxTextureCoordinate,
			 float2 ddyTextureCoordinate,
			 ViewMode viewMode,
			 uint primitiveID,
			 uint anisotropicWrapSamplerIndex)
{
	normalWorld = normalize(normalWorld);

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
	const float4 baseColorOrDiffuse = baseColorOrDiffuseTexture.SampleGrad(anisotropicWrapSampler, textureCoordinate, ddxTextureCoordinate, ddyTextureCoordinate);

	const float alpha = material.BaseColorOrDiffuseFactor.a * baseColorOrDiffuse.a;

	const float4 baseColorFactor = material.IsSpecularGlossiness ? 0.0f : material.BaseColorOrDiffuseFactor;
	const float4 diffuseFactor = material.IsSpecularGlossiness ? material.BaseColorOrDiffuseFactor : 0.0f;
	const float4 baseColor = baseColorFactor * baseColorOrDiffuse;
	const float4 diffuse = diffuseFactor * baseColorOrDiffuse;

	const float3 unlitColor = material.IsSpecularGlossiness ? diffuse.rgb : baseColor.rgb;

	const Texture2D<float3> normalMapTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.NormalMapTextureIndex)];
	float3 normalMap = normalMapTexture.SampleGrad(anisotropicWrapSampler, textureCoordinate, ddxTextureCoordinate, ddyTextureCoordinate).xyz * 2.0f - 1.0f;
	if (scene.TwoChannelNormalMaps)
	{
		normalMap.z = sqrt(1.0f - saturate(dot(normalMap.xy, normalMap.xy)));
	}

	float3 tangentWorld;
	float3 bitangentWorld;
	ComputeTangents(normalWorld, ddxPositionWorld, ddyPositionWorld, ddxTextureCoordinate, ddyTextureCoordinate, tangentWorld, bitangentWorld);
	const float3x3 tbn = transpose(float3x3(tangentWorld, bitangentWorld, normalWorld));

	const float3 shadeNormalWorld = all(normalMap <= 0.0001f) ? normalWorld : normalize(mul(tbn, normalMap));

	[branch]
	switch (viewMode)
	{
	case ViewMode::Unlit:
		return float4(unlitColor, alpha);
	case ViewMode::Geometry:
		return float4(ToColor(Hash(primitiveID)), alpha);
	case ViewMode::Normal:
		return float4(SRGBToLinear(shadeNormalWorld * 0.5f + 0.5f), 1.0f);
	default:
		break;
	}

	const Texture2D<float3> metallicRoughnessTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.MetallicRoughnessOrSpecularGlossinessTextureIndex)];
	const Texture2D<float4> specularGlossinessTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.MetallicRoughnessOrSpecularGlossinessTextureIndex)];
	const float3 metallicRoughness = metallicRoughnessTexture.SampleGrad(anisotropicWrapSampler, textureCoordinate, ddxTextureCoordinate, ddyTextureCoordinate);
	const float4 specularGlossiness = specularGlossinessTexture.SampleGrad(anisotropicWrapSampler, textureCoordinate, ddxTextureCoordinate, ddyTextureCoordinate);

	const float metallicFactor = material.IsSpecularGlossiness ? 0.0f : material.MetallicOrSpecularFactor.x;
	const float roughnessFactor = material.IsSpecularGlossiness ? 0.0f : material.RoughnessOrGlossinessFactor;
	const float metallic = metallicFactor * metallicRoughness.b;
	const float roughness = roughnessFactor * metallicRoughness.g;

	const float3 specularFactor = material.IsSpecularGlossiness ? material.MetallicOrSpecularFactor : 0.0f;
	const float glossinessFactor = material.IsSpecularGlossiness ? material.RoughnessOrGlossinessFactor : 0.0f;
	const float3 specular = specularFactor * specularGlossiness.rgb;
	const float glossiness = glossinessFactor * SRGBToLinear(specularGlossiness.a);

	const float3 viewDirectionWorld = normalize(scene.ViewPositionWorld - positionWorld);

	float3 finalColor = 0.0f;

	[loop]
	for (uint pointLightIndex = 0; pointLightIndex < scene.PointLightsCount; ++pointLightIndex)
	{
		const PointLight pointLight = pointLightsBuffer[pointLightIndex];

		const float3 pointLightDirectionWorld = normalize(pointLight.PositionWorld - positionWorld);
		const float objectToLightDistanceWorld = distance(pointLight.PositionWorld, positionWorld);

		const float attenuation = 1.0f / (objectToLightDistanceWorld * objectToLightDistanceWorld);

		const float3 contribution = PBR(baseColor.rgb,
										diffuse.rgb,
										metallic,
										specular,
										roughness,
										glossiness,
										shadeNormalWorld,
										viewDirectionWorld,
										pointLightDirectionWorld,
										attenuation * pointLight.IntensityCandela * pointLight.Color,
										material.IsSpecularGlossiness);
		finalColor += contribution * CastShadowRay(positionWorld,
												   pointLightDirectionWorld,
												   objectToLightDistanceWorld,
												   accelerationStructure,
												   vertexBuffer,
												   primitiveBuffer,
												   materialBuffer,
												   anisotropicWrapSampler);
	}

	const float3 directionalLightDirectionWorld = normalize(directionalLightBuffer.DirectionWorld);

	const float3 directionalLightContribution = PBR(baseColor.rgb,
													diffuse.rgb,
													metallic,
													specular,
													roughness,
													glossiness,
													shadeNormalWorld,
													viewDirectionWorld,
													directionalLightDirectionWorld,
													directionalLightBuffer.IntensityLux * directionalLightBuffer.Color,
													material.IsSpecularGlossiness);
	finalColor += directionalLightContribution * CastShadowRay(positionWorld,
															   directionalLightDirectionWorld,
															   Infinity,
															   accelerationStructure,
															   vertexBuffer,
															   primitiveBuffer,
															   materialBuffer,
															   anisotropicWrapSampler);

	const float3 ambientLightContribution = 0.05f * directionalLightBuffer.IntensityLux * unlitColor;
	finalColor += ambientLightContribution;

	return float4(finalColor, alpha);
}
