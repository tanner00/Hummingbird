#pragma once

#include "RHI/RHI.hpp"

#include "Luft/Array.hpp"
#include "Luft/Math.hpp"

static constexpr RHI::ResourceFormat HDRFormat = RHI::ResourceFormat::RGBA32Float;

struct ReadBuffer
{
	RHI::Resource Resource;
	RHI::BufferView View;
};

struct ReadTexture
{
	RHI::Resource Resource;
	RHI::TextureView View;
};

struct WriteTexture
{
	RHI::Resource Resource;
	RHI::TextureView ShaderResourceView;
	RHI::TextureView UnorderedAccessView;
};

struct Primitive
{
	usize GlobalIndex;
	usize MaterialIndex;

	usize PositionOffset;
	usize PositionStride;
	usize PositionSize;

	usize TextureCoordinateOffset;
	usize TextureCoordinateStride;
	usize TextureCoordinateSize;

	usize NormalOffset;
	usize NormalStride;
	usize NormalSize;

	usize IndexOffset;
	usize IndexStride;
	usize IndexSize;

	RHI::Resource AccelerationStructureResource;
};

struct Mesh
{
	Array<Primitive> Primitives;
};

struct Node
{
	Matrix LocalToWorld;
	usize MeshIndex;
};

struct SpecularGlossiness
{
	ReadTexture DiffuseTexture;
	Float4 DiffuseFactor;

	ReadTexture SpecularGlossinessTexture;
	Float3 SpecularFactor;
	float GlossinessFactor;
};

struct MetallicRoughness
{
	ReadTexture BaseColorTexture;
	Float4 BaseColorFactor;

	ReadTexture MetallicRoughnessTexture;
	float MetallicFactor;
	float RoughnessFactor;
};

struct Material
{
	ReadTexture NormalMapTexture;

	SpecularGlossiness SpecularGlossiness;
	MetallicRoughness MetallicRoughness;
	bool IsSpecularGlossiness;

	bool Translucent;
	float AlphaCutoff;
};

namespace HLSL
{
#include "Shaders/Types.hlsli"
}
