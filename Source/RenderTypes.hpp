#pragma once

#include "RHI/RHI.hpp"

#include "Luft/Array.hpp"
#include "Luft/Math.hpp"

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

struct MaterialTexture
{
	Texture Texture;
	Sampler Sampler;
};

struct Material
{
	MaterialTexture BaseColorTexture;
	Float4 BaseColorFactor;

	MaterialTexture NormalMapTexture;

	bool RequiresBlend;
	float AlphaCutoff;
};

namespace Hlsl
{

struct Scene
{
	Matrix ViewProjection;

	uint32 NodeBufferIndex;
	uint32 MaterialBufferIndex;
	uint32 DirectionalLightBufferIndex;

	PAD(180);
};

struct SceneRootConstants
{
	uint32 NodeIndex;
	uint32 MaterialIndex;

	ViewMode ViewMode;

	PAD(4);

	Matrix NormalTransform;
};

struct Node
{
	Matrix Transform;
};

struct Material
{
	uint32 BaseColorTextureIndex;
	uint32 BaseColorSamplerIndex;
	Float4 BaseColorFactor;

	uint32 NormalMapTextureIndex;
	uint32 NormalMapSamplerIndex;

	float AlphaCutoff;
};

struct DirectionalLight
{
	Float3 Direction;

	PAD(244);
};

}
