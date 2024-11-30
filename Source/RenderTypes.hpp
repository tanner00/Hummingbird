#pragma once

#include "RHI/RHI.hpp"

#include "Luft/Array.hpp"
#include "Luft/Math.hpp"

struct Material
{
	Texture BaseColorTexture;
	Float4 BaseColorFactor;

	Sampler Sampler;

	bool RequiresBlend;
	float AlphaCutoff;
};

struct Primitive
{
	usize PositionOffset;
	usize PositionSize;
	usize PositionStride;

	usize TextureCoordinateOffset;
	usize TextureCoordinateSize;
	usize TextureCoordinateStride;

	usize IndexOffset;
	usize IndexSize;
	usize IndexStride;

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

namespace Hlsl
{

struct Scene
{
	Matrix ViewProjection;

	uint32 NodeBufferIndex;

	PAD(188);
};

struct SceneRootConstants
{
	uint32 NodeIndex;

	uint32 SamplerIndex;

	float AlphaCutoff;

	uint32 BaseColorTextureIndex;
	Float4 BaseColorFactor;

	uint32 GeometryView;
};

struct Node
{
	Matrix Transform;
};

}
