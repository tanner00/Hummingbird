#pragma once

#include "Math.hlsli"
#include "Surface.hlsli"

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

float32x3 BRDFLambertianDiffuse(float32x3 diffuseReflectanceRGB)
{
	return diffuseReflectanceRGB / Pi;
}

float32x3 BRDFCookTorrance(float32x3 diffuseReflectanceRGB,
						   float32x3 f0,
						   float32 roughness,
						   float32x3 normal,
						   float32x3 viewDirection,
						   float32x3 lightDirection,
						   float32x3 lightIlluminanceRGB)
{
	const float32x3 halfwayDirection = normalize(viewDirection + lightDirection);

	const float32 nDotL = saturate(dot(normal, lightDirection));
	const float32 nDotV = saturate(dot(normal, viewDirection));

	const float32 d = NDFTrowbridgeReitzGGX(normal, halfwayDirection, roughness);
	const float32x3 f = FresnelSchlick(f0, halfwayDirection, viewDirection);
	const float32 g = GeometrySmith(viewDirection, lightDirection, normal, roughness);

	const float32x3 specularBRDF = (d * f * g) / ToSafeDenominator(4.0f * nDotV * nDotL);
	const float32x3 diffuseBRDF = (1.0f - f) * BRDFLambertianDiffuse(diffuseReflectanceRGB);

	return (diffuseBRDF + specularBRDF) * lightIlluminanceRGB * nDotL;
}

float32x3 DiffuseReflectance(Surface surface)
{
	return surface.IsSpecularGlossiness ? lerp(surface.DiffuseRGB, 0.0f, max(surface.Specular.r, max(surface.Specular.g, surface.Specular.b)))
										: lerp(surface.BaseColorRGB, 0.0f, surface.Metallic);
}

float32x3 PBRMetallicRoughness(Surface surface, float32x3 viewDirection, float32x3 lightDirection, float32x3 lightIlluminanceRGB)
{
	const float32x3 f0 = lerp(DielectricSpecular, surface.BaseColorRGB, surface.Metallic);
	return BRDFCookTorrance(DiffuseReflectance(surface), f0, surface.Roughness, surface.ShadeNormalWS, viewDirection, lightDirection, lightIlluminanceRGB);
}

float32x3 PBRSpecularGlossiness(Surface surface, float32x3 viewDirection, float32x3 lightDirection, float32x3 lightIlluminanceRGB)
{
	const float32x3 f0 = surface.Specular;
	const float32 roughness = 1.0f - surface.Glossiness;
	return BRDFCookTorrance(DiffuseReflectance(surface), f0, roughness, surface.ShadeNormalWS, viewDirection, lightDirection, lightIlluminanceRGB);
}

float32x3 PBR(Surface surface, float32x3 viewDirection, float32x3 lightDirection, float32x3 lightIlluminanceRGB)
{
	return surface.IsSpecularGlossiness ? PBRSpecularGlossiness(surface, viewDirection, lightDirection, lightIlluminanceRGB)
										: PBRMetallicRoughness(surface, viewDirection, lightDirection, lightIlluminanceRGB);
}
