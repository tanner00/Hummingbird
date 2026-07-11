#pragma once

#include "Color.hlsli"
#include "Samplers.hlsli"
#include "Types.hlsli"

struct Surface
{
	float32x3 DiffuseReflectanceRGB;
	float32x3 SpecularF0;
	float32 Roughness;

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
	static const float32x3 dielectricSpecularF0 = 0.04f;

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

	const float32x3 baseColorRGB = material.BaseColorOrDiffuseFactor.rgb * baseColorOrDiffuse.rgb;
	const float32 metallic = material.MetallicOrSpecularFactor.x * metallicRoughnessOrSpecularGlossiness.b;
	const float32 roughness = material.RoughnessOrGlossinessFactor * metallicRoughnessOrSpecularGlossiness.g;

	const float32x3 diffuseRGB = material.BaseColorOrDiffuseFactor.rgb * baseColorOrDiffuse.rgb;
	const float32x3 specular = material.MetallicOrSpecularFactor * metallicRoughnessOrSpecularGlossiness.rgb;
	const float32 glossiness = material.RoughnessOrGlossinessFactor * SRGBToLinear(metallicRoughnessOrSpecularGlossiness.a);

	Surface surface;

	surface.DiffuseReflectanceRGB = material.IsSpecularGlossiness ? lerp(diffuseRGB, 0.0f, max(specular.r, max(specular.g, specular.b)))
																  : lerp(baseColorRGB, 0.0f, metallic);
	surface.SpecularF0 = material.IsSpecularGlossiness ? specular : lerp(dielectricSpecularF0, baseColorRGB, metallic);
	surface.Roughness = material.IsSpecularGlossiness ? 1.0f - glossiness : roughness;

	surface.EmissiveRGB = material.EmissiveStrength * material.EmissiveFactor * emissive;

	surface.Alpha = material.BaseColorOrDiffuseFactor.a * baseColorOrDiffuse.a;

	surface.ShadeNormalWS = normalize(mul(tbn, normalTS)) * (material.DoubleSided && !frontFacing ? -1.0f : 1.0f);

	return surface;
}
