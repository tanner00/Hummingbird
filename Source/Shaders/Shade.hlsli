#pragma once

#include "Common.hlsli"
#include "PBR.hlsli"
#include "Shadow.hlsli"
#include "Types.hlsli"

void ComputeTangents(float3 normal,
					 float3 ddxPosition,
					 float3 ddyPosition,
					 float2 ddxUV,
					 float2 ddyUV,
					 out float3 tangent,
					 out float3 bitangent)
{
	const float3 ddxPositionPerpendicular = -cross(ddxPosition, normal);
	const float3 ddyPositionPerpendicular = cross(ddyPosition, normal);

	tangent = ddyPositionPerpendicular * ddxUV.x + ddxPositionPerpendicular * ddyUV.x;
	bitangent = ddyPositionPerpendicular * ddxUV.y + ddxPositionPerpendicular * ddyUV.y;

	const float inverseScale = rsqrt(max(dot(tangent, tangent), dot(bitangent, bitangent)));

	tangent *= inverseScale;
	bitangent *= inverseScale;
}

float4 Shade(Scene scene,
			 float3 positionWS,
			 float2 uv,
			 float3 normalWS,
			 uint primitiveIndex,
			 float3 ddxPositionWS,
			 float3 ddyPositionWS,
			 float2 ddxUV,
			 float2 ddyUV,
			 ViewMode viewMode,
			 uint primitiveID,
			 uint anisotropicWrapSamplerIndex)
{
	normalWS = normalize(normalWS);

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
	const float4 baseColorOrDiffuse = baseColorOrDiffuseTexture.SampleGrad(anisotropicWrapSampler, uv, ddxUV, ddyUV);

	const float alpha = material.BaseColorOrDiffuseFactor.a * baseColorOrDiffuse.a;

	const float4 baseColorFactor = material.IsSpecularGlossiness ? 0.0f : material.BaseColorOrDiffuseFactor;
	const float4 diffuseFactor = material.IsSpecularGlossiness ? material.BaseColorOrDiffuseFactor : 0.0f;
	const float4 baseColor = baseColorFactor * baseColorOrDiffuse;
	const float4 diffuse = diffuseFactor * baseColorOrDiffuse;

	const float3 unlitColor = material.IsSpecularGlossiness ? diffuse.rgb : baseColor.rgb;

	const Texture2D<float3> normalMapTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.NormalMapTextureIndex)];
	float3 normalTS = normalMapTexture.SampleGrad(anisotropicWrapSampler, uv, ddxUV, ddyUV).xyz * 2.0f - 1.0f;
	if (scene.TwoChannelNormalMaps)
	{
		normalTS.z = sqrt(1.0f - saturate(dot(normalTS.xy, normalTS.xy)));
	}

	float3 tangentWS;
	float3 bitangentWS;
	ComputeTangents(normalWS, ddxPositionWS, ddyPositionWS, ddxUV, ddyUV, tangentWS, bitangentWS);
	const float3x3 tbn = transpose(float3x3(tangentWS, bitangentWS, normalWS));

	const float3 shadeNormalWS = all(normalTS <= 0.0001f) ? normalWS : normalize(mul(tbn, normalTS));

	switch (viewMode)
	{
	case ViewMode::Unlit:
		return float4(unlitColor, alpha);
	case ViewMode::Geometry:
		return float4(ToColor(Hash(primitiveID)), alpha);
	case ViewMode::Normal:
		return float4(SRGBToLinear(shadeNormalWS * 0.5f + 0.5f), 1.0f);
	default:
		break;
	}

	const Texture2D<float3> metallicRoughnessTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.MetallicRoughnessOrSpecularGlossinessTextureIndex)];
	const Texture2D<float4> specularGlossinessTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.MetallicRoughnessOrSpecularGlossinessTextureIndex)];
	const float3 metallicRoughness = metallicRoughnessTexture.SampleGrad(anisotropicWrapSampler, uv, ddxUV, ddyUV);
	const float4 specularGlossiness = specularGlossinessTexture.SampleGrad(anisotropicWrapSampler, uv, ddxUV, ddyUV);

	const float metallicFactor = material.IsSpecularGlossiness ? 0.0f : material.MetallicOrSpecularFactor.x;
	const float roughnessFactor = material.IsSpecularGlossiness ? 0.0f : material.RoughnessOrGlossinessFactor;
	const float metallic = metallicFactor * metallicRoughness.b;
	const float roughness = roughnessFactor * metallicRoughness.g;

	const float3 specularFactor = material.IsSpecularGlossiness ? material.MetallicOrSpecularFactor : 0.0f;
	const float glossinessFactor = material.IsSpecularGlossiness ? material.RoughnessOrGlossinessFactor : 0.0f;
	const float3 specular = specularFactor * specularGlossiness.rgb;
	const float glossiness = glossinessFactor * SRGBToLinear(specularGlossiness.a);

	const float3 viewDirectionWS = normalize(scene.ViewPositionWS - positionWS);

	float3 finalColor = 0.0f;

	for (uint pointLightIndex = 0; pointLightIndex < scene.PointLightsCount; ++pointLightIndex)
	{
		const PointLight pointLight = pointLightsBuffer[pointLightIndex];

		const float3 pointLightDirectionWS = normalize(pointLight.PositionWS - positionWS);
		const float objectToLightDistance = distance(pointLight.PositionWS, positionWS);

		const float attenuation = 1.0f / (objectToLightDistance * objectToLightDistance);

		const float3 contribution = PBR(baseColor.rgb,
										diffuse.rgb,
										metallic,
										specular,
										roughness,
										glossiness,
										shadeNormalWS,
										viewDirectionWS,
										pointLightDirectionWS,
										attenuation * pointLight.IntensityCandela * pointLight.Color,
										material.IsSpecularGlossiness);
		finalColor += contribution * CastShadowRay(positionWS,
												   pointLightDirectionWS,
												   objectToLightDistance,
												   accelerationStructure,
												   vertexBuffer,
												   primitiveBuffer,
												   materialBuffer,
												   anisotropicWrapSampler);
	}

	const float3 directionalLightDirectionWS = normalize(directionalLightBuffer.DirectionWS);

	const float3 directionalLightContribution = PBR(baseColor.rgb,
													diffuse.rgb,
													metallic,
													specular,
													roughness,
													glossiness,
													shadeNormalWS,
													viewDirectionWS,
													directionalLightDirectionWS,
													directionalLightBuffer.IntensityLux * directionalLightBuffer.Color,
													material.IsSpecularGlossiness);
	finalColor += directionalLightContribution * CastShadowRay(positionWS,
															   directionalLightDirectionWS,
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
