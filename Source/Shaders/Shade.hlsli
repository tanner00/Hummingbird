#pragma once

#include "Common.hlsli"
#include "PBR.hlsli"
#include "Shadow.hlsli"
#include "Types.hlsli"

void ComputeTangents(float32x3 normal,
					 float32x3 ddxPosition,
					 float32x3 ddyPosition,
					 float32x2 ddxUV,
					 float32x2 ddyUV,
					 out float32x3 tangent,
					 out float32x3 bitangent)
{
	const float32x3 ddxPositionPerpendicular = -cross(ddxPosition, normal);
	const float32x3 ddyPositionPerpendicular = cross(ddyPosition, normal);

	tangent = ddyPositionPerpendicular * ddxUV.x + ddxPositionPerpendicular * ddyUV.x;
	bitangent = ddyPositionPerpendicular * ddxUV.y + ddxPositionPerpendicular * ddyUV.y;

	const float32 inverseScale = rsqrt(max(dot(tangent, tangent), dot(bitangent, bitangent)));

	tangent *= inverseScale;
	bitangent *= inverseScale;
}

float32x4 Shade(Scene scene,
				float32x3 positionWS,
				float32x3 ddxPositionWS,
				float32x3 ddyPositionWS,
				float32x2 uv,
				float32x2 ddxUV,
				float32x2 ddyUV,
				float32x3 normalWS,
				uint32 primitiveIndex,
				uint32 primitiveID,
				ViewMode viewMode,
				uint32 anisotropicWrapSamplerIndex)
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

	const Texture2D<float32x4> baseColorOrDiffuseTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.BaseColorOrDiffuseTextureIndex)];
	const float32x4 baseColorOrDiffuse = baseColorOrDiffuseTexture.SampleGrad(anisotropicWrapSampler, uv, ddxUV, ddyUV);

	const float32 alpha = material.BaseColorOrDiffuseFactor.a * baseColorOrDiffuse.a;

	const float32x4 baseColorFactor = material.IsSpecularGlossiness ? 0.0f : material.BaseColorOrDiffuseFactor;
	const float32x4 diffuseFactor = material.IsSpecularGlossiness ? material.BaseColorOrDiffuseFactor : 0.0f;
	const float32x3 baseColorRGB = baseColorFactor.rgb * baseColorOrDiffuse.rgb;
	const float32x3 diffuseRGB = diffuseFactor.rgb * baseColorOrDiffuse.rgb;

	const float32x3 unlitRGB = material.IsSpecularGlossiness ? diffuseRGB : baseColorRGB;

	const Texture2D<float32x3> normalMapTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.NormalMapTextureIndex)];
	float32x3 normalTS = normalMapTexture.SampleGrad(anisotropicWrapSampler, uv, ddxUV, ddyUV).xyz * 2.0f - 1.0f;
	if (scene.TwoChannelNormalMaps)
	{
		normalTS.z = sqrt(1.0f - saturate(dot(normalTS.xy, normalTS.xy)));
	}

	float32x3 tangentWS;
	float32x3 bitangentWS;
	ComputeTangents(normalWS, ddxPositionWS, ddyPositionWS, ddxUV, ddyUV, tangentWS, bitangentWS);
	const float3x3 tbn = transpose(float3x3(tangentWS, bitangentWS, normalWS));

	const float32x3 shadeNormalWS = all(normalTS <= 0.0001f) ? normalWS : normalize(mul(tbn, normalTS));

	switch (viewMode)
	{
	case ViewMode::Unlit:
		return float32x4(unlitRGB, alpha);
	case ViewMode::Geometry:
		return float32x4(ToRGB(Hash(primitiveID)), alpha);
	case ViewMode::Normal:
		return float32x4(SRGBToLinear(shadeNormalWS * 0.5f + 0.5f), 1.0f);
	default:
		break;
	}

	const Texture2D<float32x3> metallicRoughnessTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.MetallicRoughnessOrSpecularGlossinessTextureIndex)];
	const Texture2D<float32x4> specularGlossinessTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.MetallicRoughnessOrSpecularGlossinessTextureIndex)];
	const float32x3 metallicRoughness = metallicRoughnessTexture.SampleGrad(anisotropicWrapSampler, uv, ddxUV, ddyUV);
	const float32x4 specularGlossiness = specularGlossinessTexture.SampleGrad(anisotropicWrapSampler, uv, ddxUV, ddyUV);

	const float32 metallicFactor = material.IsSpecularGlossiness ? 0.0f : material.MetallicOrSpecularFactor.x;
	const float32 roughnessFactor = material.IsSpecularGlossiness ? 0.0f : material.RoughnessOrGlossinessFactor;
	const float32 metallic = metallicFactor * metallicRoughness.b;
	const float32 roughness = roughnessFactor * metallicRoughness.g;

	const float32x3 specularFactor = material.IsSpecularGlossiness ? material.MetallicOrSpecularFactor : 0.0f;
	const float32 glossinessFactor = material.IsSpecularGlossiness ? material.RoughnessOrGlossinessFactor : 0.0f;
	const float32x3 specular = specularFactor * specularGlossiness.rgb;
	const float32 glossiness = glossinessFactor * SRGBToLinear(specularGlossiness.a);

	const float32x3 viewDirectionWS = normalize(scene.ViewPositionWS - positionWS);

	float32x3 finalRGB = 0.0f;

	for (uint32 pointLightIndex = 0; pointLightIndex < scene.PointLightsCount; ++pointLightIndex)
	{
		const PointLight pointLight = pointLightsBuffer[pointLightIndex];

		const float32x3 pointLightDirectionWS = normalize(pointLight.PositionWS - positionWS);
		const float32 objectToLightDistance = distance(pointLight.PositionWS, positionWS);

		const float32 attenuation = 1.0f / (objectToLightDistance * objectToLightDistance);

		const float32x3 pointLightRGB = PBR(baseColorRGB,
											diffuseRGB,
											metallic,
											specular,
											roughness,
											glossiness,
											shadeNormalWS,
											viewDirectionWS,
											pointLightDirectionWS,
											attenuation * pointLight.IntensityCandela * pointLight.RGB,
											material.IsSpecularGlossiness);
		finalRGB += pointLightRGB * CastShadowRay(positionWS,
												  pointLightDirectionWS,
												  objectToLightDistance,
												  accelerationStructure,
												  vertexBuffer,
												  primitiveBuffer,
												  materialBuffer,
												  anisotropicWrapSampler);
	}

	const float32x3 directionalLightDirectionWS = normalize(directionalLightBuffer.DirectionWS);

	const float32x3 directionalLightRGB = PBR(baseColorRGB,
											  diffuseRGB,
											  metallic,
											  specular,
											  roughness,
											  glossiness,
											  shadeNormalWS,
											  viewDirectionWS,
											  directionalLightDirectionWS,
											  directionalLightBuffer.IntensityLux * directionalLightBuffer.RGB,
											  material.IsSpecularGlossiness);
	finalRGB += directionalLightRGB * CastShadowRay(positionWS,
													directionalLightDirectionWS,
													Infinity,
													accelerationStructure,
													vertexBuffer,
													primitiveBuffer,
													materialBuffer,
													anisotropicWrapSampler);

	const float32x3 ambientLightRGB = 0.05f * directionalLightBuffer.IntensityLux * unlitRGB;
	finalRGB += ambientLightRGB;

	const Texture2D<float32x3> emissiveTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.EmissiveTextureIndex)];
	const float32x3 emissiveLightRGB = material.EmissiveStrength * material.EmissiveFactor * emissiveTexture.SampleGrad(anisotropicWrapSampler, uv, ddxUV, ddyUV);
	finalRGB += emissiveLightRGB;

	return float32x4(finalRGB, alpha);
}
