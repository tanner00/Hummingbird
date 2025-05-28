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
#include "Shaders/Types.hlsli"
}
