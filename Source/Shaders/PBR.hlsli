#include "Common.hlsli"

static const float3 DielectricSpecular = 0.04f;

float3 FresnelSchlick(float3 f0, float3 halfwayDirection, float3 viewDirection)
{
	return f0 + (1.0f - f0) * pow(saturate(1.0f - abs(dot(viewDirection, halfwayDirection))), 5.0f);
}

float NDFTrowbridgeReitzGGX(float3 normal, float3 halfwayDirection, float roughness)
{
	const float alphaSquared = max(roughness * roughness * roughness * roughness, 0.00001f);
	const float nDotH = dot(normal, halfwayDirection);
	return alphaSquared / (Pi * pow(max((nDotH * nDotH) * (alphaSquared - 1.0f) + 1.0f, 0.0001f), 2.0f));
}

float GeometrySchlickGGX(float3 normal, float3 ray, float roughness)
{
	const float k = (roughness * roughness) / 2.0f;
	const float nDotRay = saturate(dot(normal, ray));
	return nDotRay / max(nDotRay * (1.0f - k) + k, 0.0001f);
}

float GeometrySmith(float3 viewDirection, float3 lightDirection, float3 normal, float roughness)
{
	const float gView = GeometrySchlickGGX(normal, viewDirection, roughness);
	const float gLight = GeometrySchlickGGX(normal, lightDirection, roughness);
	return gView * gLight;
}

float3 PBRImplementation(float3 cDiffuse,
						 float3 f0,
						 float roughness,
						 float3 normal,
						 float3 viewDirection,
						 float3 lightDirection,
						 float3 lightRadiance)
{
	const float3 halfwayDirection = normalize(viewDirection + lightDirection);

	const float nDotL = saturate(dot(normal, lightDirection));
	const float nDotV = saturate(dot(normal, viewDirection));

	const float d = NDFTrowbridgeReitzGGX(normal, halfwayDirection, roughness);
	const float3 f = FresnelSchlick(f0, halfwayDirection, viewDirection);
	const float g = GeometrySmith(viewDirection, lightDirection, normal, roughness);

	const float3 brdf = (d * f * g) / max(4.0f * nDotV * nDotL, 0.0001f);

	const float3 diffuse = (1.0f - f) * (cDiffuse / Pi);

	return (diffuse + brdf) * lightRadiance * nDotL;
}

float3 PBRMetallicRoughness(float3 baseColor,
							float metallic,
							float roughness,
							float3 normal,
							float3 viewDirection,
							float3 lightDirection,
							float3 lightRadiance)
{
	const float3 cDiffuse = lerp(baseColor.rgb, 0.0f, metallic);
	const float3 f0 = lerp(DielectricSpecular, baseColor.rgb, metallic);
	return PBRImplementation(cDiffuse, f0, roughness, normal, viewDirection, lightDirection, lightRadiance);
}

float3 PBRSpecularGlossiness(float3 diffuse,
							 float3 specular,
							 float glossiness,
							 float3 normal,
							 float3 viewDirection,
							 float3 lightDirection,
							 float3 lightRadiance)
{
	const float3 cDiffuse = lerp(diffuse, 0.0f, max(specular.r, max(specular.g, specular.b)));
	const float3 f0 = specular;
	const float roughness = 1.0f - glossiness;
	return PBRImplementation(cDiffuse, f0, roughness, normal, viewDirection, lightDirection, lightRadiance);
}

float3 PBR(float3 baseColor,
		   float3 diffuse,
		   float metallic,
		   float3 specular,
		   float roughness,
		   float glossiness,
		   float3 normal,
		   float3 viewDirection,
		   float3 lightDirection,
		   float3 lightRadiance,
		   bool isSpecularGlossiness)
{
	return isSpecularGlossiness ? PBRSpecularGlossiness(diffuse.rgb, specular, glossiness, normal, viewDirection, lightDirection, lightRadiance)
								: PBRMetallicRoughness(baseColor.rgb, metallic, roughness, normal, viewDirection, lightDirection, lightRadiance);
}
