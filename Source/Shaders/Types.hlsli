#pragma once

enum class ViewMode : uint
{
	Lit,
	Unlit,
	Geometry,
	Normal,
};

struct Primitive
{
	uint TextureCoordinateOffset;
	uint TextureCoordinateStride;

	uint IndexOffset;
	uint IndexStride;

	uint MaterialIndex;
};

struct Node
{
	matrix Transform;
};

struct Material
{
	uint BaseColorOrDiffuseTextureIndex;
	float4 BaseColorOrDiffuseFactor;

	uint NormalMapTextureIndex;

	uint MetallicRoughnessOrSpecularGlossinessTextureIndex;
	float3 MetallicOrSpecularFactor;
	float RoughnessOrGlossinessFactor;
	bool IsSpecularGlossiness;

	float AlphaCutoff;
};

struct DirectionalLight
{
	float3 Color;
	float IntensityLux;

	float3 Direction;
};

struct PointLight
{
	float3 Color;
	float IntensityCandela;

	float3 Position;
};
