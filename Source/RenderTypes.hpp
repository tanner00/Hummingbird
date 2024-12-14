#pragma once

#include "RHI/RHI.hpp"

#include "Luft/Array.hpp"
#include "Luft/Math.hpp"

struct Material
{
	Texture BaseColorTexture;
	Sampler BaseColorSampler;
	Float4 BaseColorFactor;

	bool RequiresBlend;
	float AlphaCutoff;
};

struct Primitive
{
	usize PositionOffset;
	usize PositionStride;
	usize PositionSize;

	usize TextureCoordinateOffset;
	usize TextureCoordinateStride;
	usize TextureCoordinateSize;

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

	float AlphaCutoff;

	uint32 BaseColorTextureIndex;
	uint32 BaseColorSamplerIndex;
	Float4 BaseColorFactor;

	uint32 GeometryView;
};

struct Node
{
	Matrix Transform;
};

}
