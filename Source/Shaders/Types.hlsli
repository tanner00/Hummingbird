#pragma once

enum class ViewMode : uint
{
	Lit,
	Unlit,
	Geometry,
	Normal,
};

struct SceneRootConstants
{
	uint AnisotropicWrapSamplerIndex;

	uint DrawCallIndex;
	uint PrimitiveIndex;
	uint NodeIndex;

	ViewMode ViewMode;

	matrix NormalTransform;
};

struct Scene
{
	matrix ViewProjection;
	float3 ViewPosition;

	uint VertexBufferIndex;
	uint PrimitiveBufferIndex;
	uint NodeBufferIndex;
	uint MaterialBufferIndex;
	uint DrawCallBufferIndex;
	uint DirectionalLightBufferIndex;
	uint PointLightsBufferIndex;
	uint AccelerationStructureIndex;

	uint PointLightsCount;
};

struct Primitive
{
	uint PositionOffset;
	uint PositionStride;

	uint TextureCoordinateOffset;
	uint TextureCoordinateStride;

	uint NormalOffset;
	uint NormalStride;

	uint TangentOffset;
	uint TangentStride;

	uint IndexOffset;
	uint IndexStride;

	uint MaterialIndex;
};

struct Node
{
	matrix Transform;
	matrix NormalTransform;
};

struct DrawCall
{
	uint NodeIndex;
	uint PrimitiveIndex;
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
