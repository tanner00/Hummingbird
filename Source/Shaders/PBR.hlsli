#include "Common.hlsli"

static const float32x3 DielectricSpecular = 0.04f;

float32x3 FresnelSchlick(float32x3 f0, float32x3 halfwayDirection, float32x3 viewDirection)
{
	return f0 + (1.0f - f0) * pow(saturate(1.0f - abs(dot(viewDirection, halfwayDirection))), 5.0f);
}

float32 NDFTrowbridgeReitzGGX(float32x3 normal, float32x3 halfwayDirection, float32 roughness)
{
	const float32 alphaSquared = max(roughness * roughness * roughness * roughness, 1e-5f);
	const float32 nDotH = dot(normal, halfwayDirection);
	return (alphaSquared * step(0.0f, nDotH)) / ToSafeDenominator(Pi * pow((nDotH * nDotH) * (alphaSquared - 1.0f) + 1.0f, 2.0f));
}

float32 GeometrySchlickGGX(float32x3 normal, float32x3 direction, float32 roughness)
{
	const float32 kDirectLight = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
	const float32 nDotRay = saturate(dot(normal, direction));
	return nDotRay / ToSafeDenominator(nDotRay * (1.0f - kDirectLight) + kDirectLight);
}

float32 GeometrySmith(float32x3 viewDirection, float32x3 lightDirection, float32x3 normal, float32 roughness)
{
	const float32 gView = GeometrySchlickGGX(normal, viewDirection, roughness);
	const float32 gLight = GeometrySchlickGGX(normal, lightDirection, roughness);
	return gView * gLight;
}

float32x3 BRDFCookTorrance(float32x3 cDiffuse,
						   float32x3 f0,
						   float32 roughness,
						   float32x3 normal,
						   float32x3 viewDirection,
						   float32x3 lightDirection,
						   float32x3 lightRadiance)
{
	const float32x3 halfwayDirection = normalize(viewDirection + lightDirection);

	const float32 nDotL = saturate(dot(normal, lightDirection));
	const float32 nDotV = saturate(dot(normal, viewDirection));

	const float32 d = NDFTrowbridgeReitzGGX(normal, halfwayDirection, roughness);
	const float32x3 f = FresnelSchlick(f0, halfwayDirection, viewDirection);
	const float32 g = GeometrySmith(viewDirection, lightDirection, normal, roughness);

	const float32x3 specularBRDF = (d * f * g) / ToSafeDenominator(4.0f * nDotV * nDotL);
	const float32x3 diffuseBRDF = (1.0f - f) * (cDiffuse / Pi);

	return (diffuseBRDF + specularBRDF) * lightRadiance * nDotL;
}

float32x3 PBRMetallicRoughness(float32x3 rgb,
							   float32 metallic,
							   float32 roughness,
							   float32x3 normal,
							   float32x3 viewDirection,
							   float32x3 lightDirection,
							   float32x3 lightRadiance)
{
	const float32x3 cDiffuse = lerp(rgb, 0.0f, metallic);
	const float32x3 f0 = lerp(DielectricSpecular, rgb, metallic);
	return BRDFCookTorrance(cDiffuse, f0, roughness, normal, viewDirection, lightDirection, lightRadiance);
}

float32x3 PBRSpecularGlossiness(float32x3 diffuseRGB,
								float32x3 specular,
								float32 glossiness,
								float32x3 normal,
								float32x3 viewDirection,
								float32x3 lightDirection,
								float32x3 lightRadiance)
{
	const float32x3 cDiffuse = lerp(diffuseRGB, 0.0f, max(specular.r, max(specular.g, specular.b)));
	const float32x3 f0 = specular;
	const float32 roughness = 1.0f - glossiness;
	return BRDFCookTorrance(cDiffuse, f0, roughness, normal, viewDirection, lightDirection, lightRadiance);
}

float32x3 PBR(float32x3 baseColorRGB,
			  float32x3 diffuseRGB,
			  float32 metallic,
			  float32x3 specular,
			  float32 roughness,
			  float32 glossiness,
			  float32x3 normal,
			  float32x3 viewDirection,
			  float32x3 lightDirection,
			  float32x3 lightRadiance,
			  bool32 isSpecularGlossiness)
{
	return isSpecularGlossiness ? PBRSpecularGlossiness(diffuseRGB, specular, glossiness, normal, viewDirection, lightDirection, lightRadiance)
								: PBRMetallicRoughness(baseColorRGB, metallic, roughness, normal, viewDirection, lightDirection, lightRadiance);
}
