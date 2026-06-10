#pragma once

#include "Color.hlsli"
#include "Samplers.hlsli"
#include "Types.hlsli"

struct Surface
{
	float32x3 BaseColorRGB;
	float32 Metallic;
	float32 Roughness;

	float32x3 DiffuseRGB;
	float32x3 Specular;
	float32 Glossiness;

	bool32 IsSpecularGlossiness;

	float32x3 EmissiveRGB;

	float32 Alpha;

	float32x3 ShadeNormalWS;
};

void ComputeTangents(float32x3 normal,
					 float32x3 ddxPosition,
					 float32x3 ddyPosition,
					 float32x2 ddxUV,
					 float32x2 ddyUV,
					 out float32x3 tangent,
					 out float32x3 bitangent)
{
	const float32x3 ddxPerpendicularPosition = -cross(ddxPosition, normal);
	const float32x3 ddyPerpendicularPosition = cross(ddyPosition, normal);

	tangent = ddyPerpendicularPosition * ddxUV.x + ddxPerpendicularPosition * ddyUV.x;
	bitangent = ddyPerpendicularPosition * ddxUV.y + ddxPerpendicularPosition * ddyUV.y;

	const float32 inverseScale = rsqrt(max(dot(tangent, tangent), dot(bitangent, bitangent)));

	tangent *= inverseScale;
	bitangent *= inverseScale;
}

Surface EvaluateSurface(Material material,
						bool32 frontFacing,
						bool32 twoChannelNormalMaps,
						float32x3 ddxPositionWS,
						float32x3 ddyPositionWS,
						float32x2 uv,
						float32x2 ddxUV,
						float32x2 ddyUV,
						float32x3 normalWS)
{
	const Texture2D<float32x4> baseColorOrDiffuseTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.BaseColorOrDiffuseTextureIndex)];
	const Texture2D<float32x4> metallicRoughnessOrSpecularGlossinessTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.MetallicRoughnessOrSpecularGlossinessTextureIndex)];
	const Texture2D<float32x3> emissiveTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.EmissiveTextureIndex)];
	const Texture2D<float32x3> normalMapTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.NormalMapTextureIndex)];

	const float32x4 baseColorOrDiffuse = baseColorOrDiffuseTexture.SampleGrad(GetAnisotropicWrapSampler(), uv, ddxUV, ddyUV);
	const float32x4 metallicRoughnessOrSpecularGlossiness = metallicRoughnessOrSpecularGlossinessTexture.SampleGrad(GetAnisotropicWrapSampler(), uv, ddxUV, ddyUV);
	const float32x3 emissive = emissiveTexture.SampleGrad(GetAnisotropicWrapSampler(), uv, ddxUV, ddyUV);
	float32x3 normalTS = normalMapTexture.SampleGrad(GetAnisotropicWrapSampler(), uv, ddxUV, ddyUV).xyz * 2.0f - 1.0f;
	if (twoChannelNormalMaps)
	{
		normalTS.z = sqrt(1.0f - saturate(dot(normalTS.xy, normalTS.xy)));
	}

	float32x3 tangentWS;
	float32x3 bitangentWS;
	ComputeTangents(normalWS, ddxPositionWS, ddyPositionWS, ddxUV, ddyUV, tangentWS, bitangentWS);
	const float32x3x3 tbn = transpose(float32x3x3(tangentWS, bitangentWS, normalWS));

	Surface surface;
	surface.BaseColorRGB = material.BaseColorOrDiffuseFactor.rgb * baseColorOrDiffuse.rgb;
	surface.Metallic = material.MetallicOrSpecularFactor.x * metallicRoughnessOrSpecularGlossiness.b;
	surface.Roughness = material.RoughnessOrGlossinessFactor * metallicRoughnessOrSpecularGlossiness.g;

	surface.DiffuseRGB = material.BaseColorOrDiffuseFactor.rgb * baseColorOrDiffuse.rgb;
	surface.Specular = material.MetallicOrSpecularFactor * metallicRoughnessOrSpecularGlossiness.rgb;
	surface.Glossiness = material.RoughnessOrGlossinessFactor * SRGBToLinear(metallicRoughnessOrSpecularGlossiness.a);

	surface.IsSpecularGlossiness = material.IsSpecularGlossiness;

	surface.EmissiveRGB = material.EmissiveStrength * material.EmissiveFactor * emissive;

	surface.Alpha = material.BaseColorOrDiffuseFactor.a * baseColorOrDiffuse.a;

	surface.ShadeNormalWS = normalize(mul(tbn, normalTS)) * (material.DoubleSided && !frontFacing ? -1.0f : 1.0f);

	return surface;
}
