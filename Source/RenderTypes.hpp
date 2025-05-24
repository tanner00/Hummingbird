#pragma once

#include "RHI/RHI.hpp"

#include "Luft/Array.hpp"
#include "Luft/Math.hpp"

static constexpr RHI::ResourceFormat HDRFormat = RHI::ResourceFormat::RGBA32Float;

struct BasicBuffer
{
	RHI::Resource Resource;
	RHI::BufferView View;
};

struct BasicTexture
{
	RHI::Resource Resource;
	RHI::TextureView View;
};

struct RenderTarget
{
	RHI::Resource Resource;
	RHI::TextureView RenderTargetView;
	RHI::TextureView ShaderResourceView;
	RHI::TextureView UnorderedAccessView;
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
	usize GlobalIndex;

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
	BasicTexture DiffuseTexture;
	Float4 DiffuseFactor;

	BasicTexture SpecularGlossinessTexture;
	Float3 SpecularFactor;
	float GlossinessFactor;
};

struct MetallicRoughness
{
	BasicTexture BaseColorTexture;
	Float4 BaseColorFactor;

	BasicTexture MetallicRoughnessTexture;
	float MetallicFactor;
	float RoughnessFactor;
};

struct Material
{
	BasicTexture NormalMapTexture;

	union
	{
		SpecularGlossiness SpecularGlossiness;
		MetallicRoughness MetallicRoughness;
	};
	bool IsSpecularGlossiness;

	bool Translucent;
	float AlphaCutoff;
};

namespace HLSL
{

struct SceneRootConstants
{
	uint32 AnisotropicWrapSamplerIndex;

	uint32 DrawCallIndex;
	uint32 PrimitiveIndex;
	uint32 NodeIndex;

	ViewMode ViewMode;

	PAD(12);

	Matrix NormalTransform;
};

struct DeferredRootConstants
{
	uint32 HDRTextureIndex;
	uint32 AnisotropicWrapSamplerIndex;
	uint32 VisibilityBufferTextureIndex;

	ViewMode ViewMode;
};

struct LuminanceHistogramRootConstants
{
	uint32 HDRTextureIndex;
	uint32 LuminanceBufferIndex;
};

struct LuminanceAverageRootConstants
{
	uint32 LuminanceBufferIndex;

	uint32 PixelCount;
};

struct ToneMapRootConstants
{
	uint32 HDRTextureIndex;
	uint32 AnisotropicWrapSamplerIndex;
	uint32 LuminanceBufferIndex;

	bool32 DebugViewMode;
};

struct Scene
{
	Matrix ViewProjection;
	Float3 ViewPosition;

	uint32 VertexBufferIndex;
	uint32 PrimitiveBufferIndex;
	uint32 NodeBufferIndex;
	uint32 MaterialBufferIndex;
	uint32 DrawCallBufferIndex;
	uint32 DirectionalLightBufferIndex;
	uint32 PointLightsBufferIndex;
	uint32 AccelerationStructureIndex;

	uint32 PointLightsCount;

	PAD(144);
};

struct Primitive
{
	uint32 PositionOffset;
	uint32 PositionStride;

	uint32 TextureCoordinateOffset;
	uint32 TextureCoordinateStride;

	uint32 NormalOffset;
	uint32 NormalStride;

	uint32 TangentOffset;
	uint32 TangentStride;

	uint32 IndexOffset;
	uint32 IndexStride;

	uint32 MaterialIndex;
};

struct Node
{
	Matrix Transform;
	Matrix NormalTransform;
};

struct DrawCall
{
	uint32 NodeIndex;
	uint32 PrimitiveIndex;
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
