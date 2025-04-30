#pragma once

#include "RHI/RHI.hpp"

#include "Luft/Array.hpp"
#include "Luft/Math.hpp"

static constexpr RHI::ResourceFormat HdrFormat = RHI::ResourceFormat::Rgba32Float;

struct Buffer
{
	RHI::Resource Resource;
	RHI::BufferView View;
};

struct Texture
{
	RHI::Resource Resource;
	RHI::TextureView View;
};

enum class ViewMode : uint32
{
	Lit,
	Unlit,
	Geometry,
	Normal,
};

struct Primitive
{
	usize PositionOffset;
	usize PositionStride;
	usize PositionSize;

	usize TextureCoordinateOffset;
	usize TextureCoordinateStride;
	usize TextureCoordinateSize;

	usize NormalOffset;
	usize NormalStride;
	usize NormalSize;

	usize TangentOffset;
	usize TangentStride;
	usize TangentSize;

	usize IndexOffset;
	usize IndexStride;
	usize IndexSize;

	usize MaterialIndex;

	RHI::Resource AccelerationStructureResource;
};

struct Mesh
{
	Array<Primitive> Primitives;
};

struct Node
{
	Matrix Transform;
	usize MeshIndex;
};

struct SpecularGlossiness
{
	Texture DiffuseTexture;
	Float4 DiffuseFactor;

	Texture SpecularGlossinessTexture;
	Float3 SpecularFactor;
	float GlossinessFactor;
};

struct MetallicRoughness
{
	Texture BaseColorTexture;
	Float4 BaseColorFactor;

	Texture MetallicRoughnessTexture;
	float MetallicFactor;
	float RoughnessFactor;
};

struct Material
{
	Texture NormalMapTexture;

	union
	{
		SpecularGlossiness SpecularGlossiness;
		MetallicRoughness MetallicRoughness;
	};
	bool IsSpecularGlossiness;

	bool RequiresBlend;
	float AlphaCutoff;
};

namespace HLSL
{

struct SceneRootConstants
{
	uint32 NodeIndex;
	uint32 MaterialIndex;

	ViewMode ViewMode;

	PAD(4);

	Matrix NormalTransform;
};

struct LuminanceHistogramRootConstants
{
	uint32 LuminanceBufferIndex;
	uint32 HdrTextureIndex;
};

struct LuminanceAverageRootConstants
{
	uint32 LuminanceBufferIndex;

	uint32 PixelCount;
};

struct ToneMapRootConstants
{
	uint32 HdrTextureIndex;
	uint32 DefaultSamplerIndex;
	uint32 LuminanceBufferIndex;

	bool32 DebugViewMode;
};

struct Scene
{
	Matrix ViewProjection;
	Float3 ViewPosition;

	uint32 DefaultSamplerIndex;
	uint32 NodeBufferIndex;
	uint32 MaterialBufferIndex;
	uint32 DirectionalLightBufferIndex;
	uint32 PointLightsBufferIndex;
	uint32 AccelerationStructureIndex;

	uint32 PointLightsCount;

	PAD(152);
};

struct Node
{
	Matrix Transform;
};

struct Material
{
	uint32 BaseColorOrDiffuseTextureIndex;
	Float4 BaseColorOrDiffuseFactor;

	uint32 NormalMapTextureIndex;

	uint32 MetallicRoughnessOrSpecularGlossinessTextureIndex;
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

}
