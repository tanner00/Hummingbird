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
	uint VertexBufferIndex;
	uint PrimitiveBufferIndex;
	uint NodeBufferIndex;
	uint MaterialBufferIndex;
	uint DrawCallBufferIndex;
	uint DirectionalLightBufferIndex;
	uint PointLightsBufferIndex;
	uint AccelerationStructureIndex;

	Matrix ViewProjection;
	Float3 ViewPosition;

	bool32 TwoChannelNormalMaps;

	uint PointLightsCount;

	PAD(140);
};

struct Primitive
{
	uint PositionOffset;
	uint PositionStride;

	uint TextureCoordinateOffset;
	uint TextureCoordinateStride;

	uint NormalOffset;
	uint NormalStride;

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
	uint NormalMapTextureIndex;
	uint MetallicRoughnessOrSpecularGlossinessTextureIndex;

	Float4 BaseColorOrDiffuseFactor;
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
	uint CharacterBufferIndex;
	uint FontTextureIndex;
	uint LinearWrapSampler;

	PAD(4);

	Matrix ViewProjection;
	Float2 UnitRange;
};

#define SHARED_OFF true
#include "Shared.hlsli"
#undef SHARED_OFF
