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
	float32x4 DiffuseFactor;

	ReadTexture SpecularGlossinessTexture;
	float32x3 SpecularFactor;
	float32 GlossinessFactor;
};

struct MetallicRoughness
{
	ReadTexture BaseColorTexture;
	float32x4 BaseColorFactor;

	ReadTexture MetallicRoughnessTexture;
	float32 MetallicFactor;
	float32 RoughnessFactor;
};

struct Material
{
	SpecularGlossiness SpecularGlossiness;
	MetallicRoughness MetallicRoughness;
	bool IsSpecularGlossiness;

	ReadTexture NormalMapTexture;

	ReadTexture EmissiveTexture;
	float32x3 EmissiveFactor;
	float32 EmissiveStrength;

	bool Translucent;
	float32 AlphaCutoff;
};

namespace HLSL
{
#include "Shaders/Types.hlsli"
}
