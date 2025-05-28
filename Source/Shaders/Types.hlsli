#pragma once

#define SHARED_ON true
#include "Shared.hlsli"
#undef SHARED_ON

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

	PAD(12);

	Matrix NormalTransform;
};

struct DeferredRootConstants
{
	uint HDRTextureIndex;
	uint AnisotropicWrapSamplerIndex;
	uint VisibilityBufferTextureIndex;

	ViewMode ViewMode;
};

struct LuminanceHistogramRootConstants
{
	uint HDRTextureIndex;
	uint LuminanceBufferIndex;
};

struct LuminanceAverageRootConstants
{
	uint LuminanceBufferIndex;

	uint PixelCount;
};

struct ToneMapRootConstants
{
	uint HDRTextureIndex;
	uint AnisotropicWrapSamplerIndex;
	uint LuminanceBufferIndex;

	bool32 DebugViewMode;
};

struct Scene
{
	Matrix ViewProjection;
	Float3 ViewPosition;

	uint VertexBufferIndex;
	uint PrimitiveBufferIndex;
	uint NodeBufferIndex;
	uint MaterialBufferIndex;
	uint DrawCallBufferIndex;
	uint DirectionalLightBufferIndex;
	uint PointLightsBufferIndex;
	uint AccelerationStructureIndex;

	uint PointLightsCount;

	PAD(144);
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
	Matrix Transform;
	Matrix NormalTransform;
};

struct DrawCall
{
	uint NodeIndex;
	uint PrimitiveIndex;
};

struct Material
{
	uint BaseColorOrDiffuseTextureIndex;
	Float4 BaseColorOrDiffuseFactor;

	uint NormalMapTextureIndex;

	uint MetallicRoughnessOrSpecularGlossinessTextureIndex;
	Float3 MetallicOrSpecularFactor;
	float RoughnessOrGlossinessFactor;
	bool32 IsSpecularGlossiness;

	float AlphaCutoff;
};

struct DirectionalLight
{
	Float3 Color;
	float IntensityLux;

	Float3 Direction;

	PAD(228);
};

struct PointLight
{
	Float3 Color;
	float IntensityCandela;

	Float3 Position;
};

struct Character
{
	Float4 Color;

	Float2 ScreenPosition;

	Float2 AtlasPosition;
	Float2 AtlasSize;

	Float2 PlanePosition;
	Float2 PlaneSize;

	float Scale;
};

struct TextRootConstants
{
	Matrix ViewProjection;
	Float2 UnitRange;

	uint CharacterBufferIndex;
	uint Texture;
	uint Sampler;
};

#define SHARED_OFF true
#include "Shared.hlsli"
#undef SHARED_OFF
