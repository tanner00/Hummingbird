#pragma once

#include "RHI/Common.hpp"

#include "Luft/Array.hpp"
#include "Luft/Math.hpp"

struct Primitive
{
	usize PositionOffset;
	usize PositionSize;
	usize PositionStride;

	usize IndexOffset;
	usize IndexSize;
	usize IndexStride;
};

struct Mesh
{
	Array<Primitive> Primitives;
};

struct Node
{
	Matrix Transform;
	usize Mesh;
};

namespace Hlsl
{

struct Scene
{
	Matrix ViewProjection;

	uint32 NodeBuffer;

	PAD(188);
};

struct SceneRootConstants
{
	uint32 NodeIndex;
};

struct Node
{
	Matrix Transform;
};

}
