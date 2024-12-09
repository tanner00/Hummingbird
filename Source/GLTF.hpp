#pragma once

#include "DDS.hpp"

#include "Luft/Array.hpp"
#include "Luft/HashTable.hpp"
#include "Luft/Math.hpp"

enum class GltfTargetType
{
	ArrayBuffer,
	ElementArrayBuffer,
};

enum class GltfComponentType
{
	Int8,
	Uint8,
	Int16,
	Uint16,
	Uint32,
	Float32,
	Count,
};

enum class GltfAccessorType
{
	Scalar,
	Vector2,
	Vector3,
	Vector4,
	Matrix2,
	Matrix3,
	Matrix4,
	Count,
};

enum class GltfAttributeType
{
	Position,
	Normal,
	Tangent,
	Texcoord0,
};

template<>
struct Hash<GltfAttributeType>
{
	uint64 operator()(const GltfAttributeType key) const
	{
		return StringHash(&key, sizeof(key));
	}
};

enum class GltfFilter
{
	Nearest,
	Linear,

	NearestMipMapNearest,
	LinearMipMapNearest,
	NearestMipMapLinear,
	LinearMipMapLinear,
};

enum class GltfAddress
{
	Repeat,
	ClampToEdge,
	MirroredRepeat,
};

enum class GltfAlphaMode
{
	Opaque,
	Mask,
	Blend,
};

struct GltfNode
{
	usize Mesh;
	Matrix Transform;

	usize Parent;
	Array<usize> ChildNodes;
};

struct GltfBuffer
{
	uint8* Data;
	usize Size;
};

struct GltfBufferView
{
	usize Buffer;
	usize Size;
	usize Offset;
	GltfTargetType Target;
};

struct GltfPrimitive
{
	HashTable<GltfAttributeType, usize> Attributes;
	usize Indices;
	usize Material;
};

struct GltfMesh
{
	Array<GltfPrimitive> Primitives;
};

struct GltfAccessor
{
	usize BufferView;
	usize Count;
	usize Offset;
	GltfComponentType ComponentType;
	GltfAccessorType AccessorType;
};

struct GltfImage
{
	DdsImage Image;
};

struct GltfTexture
{
	usize Image;
	usize Sampler;
};

struct GltfMaterial
{
	usize BaseColorTexture;
	Float4 BaseColorFactor;

	GltfAlphaMode AlphaMode;
	float AlphaCutoff;
};

struct GltfSampler
{
	GltfFilter MinificationFilter;
	GltfFilter MagnificationFilter;
	GltfAddress HorizontalAddress;
	GltfAddress VerticalAddress;
};

struct GltfCamera
{
	Matrix Transform;

	float FieldOfViewYRadians;
	float AspectRatio;

	float NearZ;
	float FarZ;
};

struct GltfScene
{
	Array<usize> TopLevelNodes;
	Array<GltfNode> Nodes;

	Array<GltfBuffer> Buffers;
	Array<GltfBufferView> BufferViews;
	Array<GltfMesh> Meshes;

	Array<GltfImage> Images;
	Array<GltfTexture> Textures;
	Array<GltfSampler> Samplers;
	Array<GltfMaterial> Materials;

	Array<GltfAccessor> Accessors;

	GltfCamera Camera;
	bool DefaultCamera;
};

GltfScene LoadGltfScene(StringView filePath);
void UnloadGltfScene(GltfScene* scene);

Matrix CalculateGltfGlobalTransform(const GltfScene& scene, usize nodeIndex);

inline usize GetGltfAccessorSize(GltfAccessorType accessorType)
{
	CHECK(static_cast<usize>(accessorType) < static_cast<usize>(GltfAccessorType::Count));
	static constexpr usize accessorSizes[] =
	{
		1,
		2,
		3,
		4,
		4,
		9,
		16,
	};
	return accessorSizes[static_cast<usize>(accessorType)];
}

inline usize GetGltfComponentSize(GltfComponentType componentType)
{
	CHECK(static_cast<usize>(componentType) < static_cast<usize>(GltfComponentType::Count));

	static constexpr usize componentSizes[] =
	{
		sizeof(int8),
		sizeof(uint8),
		sizeof(int16),
		sizeof(uint16),
		sizeof(uint32),
		sizeof(float),
	};
	return componentSizes[static_cast<usize>(componentType)];
}

inline usize GetGltfElementSize(GltfAccessorType accessorType, GltfComponentType componentType)
{
	return GetGltfAccessorSize(accessorType) * GetGltfComponentSize(componentType);
}
