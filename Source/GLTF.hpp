#pragma once

#include "RHI/HLSL.hpp"

#include "Luft/Array.hpp"
#include "Luft/HashTable.hpp"
#include "Luft/Math.hpp"

namespace GLTF
{

enum class TargetType : uint8
{
	ArrayBuffer,
	ElementArrayBuffer,
};

enum class ComponentType : uint8
{
	Int8,
	Uint8,
	Int16,
	Uint16,
	Uint32,
	Float32,
	Count,
};

enum class AccessorType : uint8
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

enum class AttributeType : uint8
{
	Position,
	Normal,
	Tangent,
	Texcoord0,
};

template<>
struct Hash<AttributeType>
{
	uint64 operator()(const AttributeType key) const
	{
		return StringHash(&key, sizeof(key));
	}
};

enum class Filter : uint8
{
	Nearest,
	Linear,

	NearestMipMapNearest,
	LinearMipMapNearest,
	NearestMipMapLinear,
	LinearMipMapLinear,
};

enum class Address : uint8
{
	Repeat,
	ClampToEdge,
	MirroredRepeat,
};

enum class AlphaMode : uint8
{
	Opaque,
	Mask,
	Blend,
};

struct Node
{
	Matrix Transform;

	usize Parent;
	Array<usize> ChildNodes;

	usize Mesh;
	usize Camera;
	usize Light;
};

struct Buffer
{
	uint8* Data;
	usize Size;
};

struct BufferView
{
	usize Buffer;
	usize Size;
	usize Offset;
	TargetType Target;
};

struct Primitive
{
	HashTable<AttributeType, usize> Attributes;
	usize Indices;
	usize Material;
};

struct Mesh
{
	Array<Primitive> Primitives;
};

struct Accessor
{
	usize BufferView;
	usize Count;
	usize Offset;
	ComponentType ComponentType;
	AccessorType AccessorType;
};

struct AccessorView
{
	usize Offset;
	usize Stride;
	usize Size;
};

struct Image
{
	String Path;
};

struct Texture
{
	usize Image;
	usize Sampler;
};

struct SpecularGlossiness
{
	usize DiffuseTexture;
	Float4 DiffuseFactor;

	usize SpecularGlossinessTexture;
	Float3 SpecularFactor;
	float GlossinessFactor;
};

struct MetallicRoughness
{
	usize BaseColorTexture;
	Float4 BaseColorFactor;

	usize MetallicRoughnessTexture;
	float MetallicFactor;
	float RoughnessFactor;
};

struct Material
{
	usize NormalMapTexture;

	union
	{
		SpecularGlossiness SpecularGlossiness;
		MetallicRoughness MetallicRoughness;
	};
	bool IsSpecularGlossiness;

	AlphaMode AlphaMode;
	float AlphaCutoff;
};

struct Sampler
{
	Filter MinificationFilter;
	Filter MagnificationFilter;
	Address HorizontalAddress;
	Address VerticalAddress;
};

struct Camera
{
	Matrix Transform;

	float FieldOfViewYRadians;
	float AspectRatio;

	float NearZ;
	float FarZ;
};

enum class LightType : uint8
{
	Directional,
	Point,
};

struct Light
{
	Matrix Transform;

	float Intensity;
	Float3 Color;

	LightType Type;
};

struct Scene
{
	Array<usize> TopLevelNodes;
	Array<Node> Nodes;

	Array<Buffer> Buffers;
	Array<BufferView> BufferViews;
	Array<Mesh> Meshes;

	Array<Image> Images;
	Array<Texture> Textures;
	Array<Sampler> Samplers;
	Array<Material> Materials;

	Array<Accessor> Accessors;

	Array<Camera> Cameras;
	Array<Light> Lights;
};

Scene LoadScene(StringView filePath);
void UnloadScene(Scene* scene);

Matrix CalculateGlobalTransform(const Scene& scene, usize nodeIndex);

inline usize GetAccessorSize(AccessorType accessorType)
{
	CHECK(static_cast<usize>(accessorType) < static_cast<usize>(AccessorType::Count));
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

inline usize GetComponentSize(ComponentType componentType)
{
	CHECK(static_cast<usize>(componentType) < static_cast<usize>(ComponentType::Count));

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

inline usize GetElementSize(AccessorType accessorType, ComponentType componentType)
{
	return GetAccessorSize(accessorType) * GetComponentSize(componentType);
}

inline AccessorView GetAccessorView(const Scene& scene, usize accessorIndex)
{
	const Accessor& accessor = scene.Accessors[accessorIndex];
	const BufferView& bufferView = scene.BufferViews[accessor.BufferView];

	const Buffer& buffer = scene.Buffers[bufferView.Buffer];
	const usize offset = accessor.Offset + bufferView.Offset;
	const usize stride = GetElementSize(accessor.AccessorType, accessor.ComponentType);
	const usize size = accessor.Count * stride;
	CHECK(offset + size <= buffer.Size);

	return AccessorView { offset, stride, size };
}

}
