#pragma once

#define SHARED_ON true
#include "Shared.hlsli"
#undef SHARED_ON

static const uint32 LinearClampSamplerIndex = 1;
static const uint32 AnisotropicWrapSamplerIndex = 2;
static const uint32 SamplerCount = AnisotropicWrapSamplerIndex + 1;

enum class ViewMode : uint32
{
	Lit,
	Unlit,
	Geometry,
	Normal,
};

struct SceneRootConstants
{
	uint32 DrawCallIndex;
	uint32 PrimitiveIndex;
	uint32 NodeIndex;

	ViewMode ViewMode;

	Matrix NormalLocalToWorld;
};

struct DeferredRootConstants
{
	uint32 HDRTextureIndex;

	uint32 VisibilityTextureIndex;

	ViewMode ViewMode;
};

struct ResolveRootConstants
{
	uint32 AccumulationTextureIndex;

	uint32 HDRTextureIndex;
	uint32 PreviousAccumulationTextureIndex;
	uint32 VisibilityTextureIndex;
	uint32 VertexBufferIndex;
	uint32 PrimitiveBufferIndex;
	uint32 NodeBufferIndex;
	uint32 DrawCallBufferIndex;

	bool32 DiscardPreviousFrame;

	PAD(12);

	Matrix WorldToClip;
	Matrix PreviousWorldToClip;
};

struct LuminanceHistogramRootConstants
{
	uint32 LuminanceBufferIndex;

	uint32 HDRTextureIndex;
};

struct LuminanceAverageRootConstants
{
	uint32 LuminanceBufferIndex;

	uint32 PixelCount;
};

struct ToneMapRootConstants
{
	uint32 HDRTextureIndex;
	uint32 LuminanceBufferIndex;

	bool32 DebugViewMode;
};

struct PathTraceRootConstants
{
	uint32 HDRTextureIndex;
};

struct Scene
{
	uint32 VertexBufferIndex;
	uint32 PrimitiveBufferIndex;
	uint32 NodeBufferIndex;
	uint32 MaterialBufferIndex;
	uint32 DrawCallBufferIndex;
	uint32 DirectionalLightBufferIndex;
	uint32 PointLightsBufferIndex;
	uint32 AccelerationStructureIndex;

	Matrix WorldToClip;
	Matrix ClipToWorld;
	Matrix JitterWorldToClip;
	float32x3 ViewPositionWS;

	bool32 TwoChannelNormalMaps;

	uint32 PointLightsCount;

	PAD(12);
};

struct Primitive
{
	uint32 MaterialIndex;

	uint32 PositionOffset;
	uint32 PositionStride;

	uint32 TextureCoordinateOffset;
	uint32 TextureCoordinateStride;

	uint32 NormalOffset;
	uint32 NormalStride;

	uint32 IndexOffset;
	uint32 IndexStride;
};

struct Node
{
	Matrix LocalToWorld;
	Matrix NormalLocalToWorld;
};

struct DrawCall
{
	uint32 NodeIndex;
	uint32 PrimitiveIndex;
};

struct Material
{
	uint32 BaseColorOrDiffuseTextureIndex;
	uint32 MetallicRoughnessOrSpecularGlossinessTextureIndex;

	float32x4 BaseColorOrDiffuseFactor;
	float32x3 MetallicOrSpecularFactor;
	float32 RoughnessOrGlossinessFactor;

	bool32 IsSpecularGlossiness;

	uint32 NormalMapTextureIndex;

	uint32 EmissiveTextureIndex;
	float32x3 EmissiveFactor;
	float32 EmissiveStrength;

	float32 AlphaCutoff;

	bool32 DoubleSided;
};

struct DirectionalLight
{
	float32x3 RGB;
	float32 IntensityLux;

	float32x3 DirectionWS;

	PAD(228);
};

struct PointLight
{
	float32x3 RGB;
	float32 IntensityCandela;

	float32x3 PositionWS;
};

struct UIRootConstants
{
	uint32 UIDrawBufferIndex;
	uint32 FontTextureIndex;

	PAD(8);

	Matrix ScreenToClip;
	float32x2 UnitRange;
};

enum class UIDrawType : uint32
{
	Rectangle,
	Character,
	Image,
};

struct UIDraw
{
	float32x2 PositionSS;
	float32x2 SizeSS;

	float32x2 ScissorMinSS;
	float32x2 ScissorMaxSS;

	float32x4 SRGBA;

	UIDrawType Type;

	float32x4 BorderSRGBA;
	float32 BorderSizeSS;
	float32x4 CornerRadiiSS;

	float32x2 AtlasPosition;
	float32x2 AtlasSize;
	float32x2 PlanePosition;
	float32x2 PlaneSize;
	float32 Scale;

	uint32 ImageIndex;

	uint32 Layer;
};

#define SHARED_OFF true
#include "Shared.hlsli"
#undef SHARED_OFF
