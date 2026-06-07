#include "Samplers.hlsli"
#include "Transform.hlsli"
#include "Types.hlsli"

struct VertexInput
{
	float32x3 PositionLS : POSITION0;
	float32x2 UV : TEXCOORD0;
};

struct PixelInput
{
	float32x4 PositionCS : SV_POSITION;
	float32x2 UV : TEXCOORD0;
};

ConstantBuffer<SceneRootConstants> RootConstants : register(b0);
ConstantBuffer<Scene> Scene : register(b1);

PixelInput VertexStart(VertexInput input)
{
	const StructuredBuffer<Node> nodeBuffer = ResourceDescriptorHeap[Scene.NodeBufferIndex];
	const Node node = nodeBuffer[RootConstants.NodeIndex];

	PixelInput result;
	result.PositionCS = TransformWorldToClip(TransformLocalPositionToWorld(input.PositionLS, node.LocalToWorld), Scene.JitterWorldToClip);
	result.UV = input.UV;
	return result;
}

uint32x2 PixelStart(PixelInput input, uint32 primitiveID : SV_PrimitiveID) : SV_Target
{
	const StructuredBuffer<Primitive> primitiveBuffer = ResourceDescriptorHeap[Scene.PrimitiveBufferIndex];
	const StructuredBuffer<Material> materialBuffer = ResourceDescriptorHeap[Scene.MaterialBufferIndex];

	const Primitive primitive = primitiveBuffer[RootConstants.PrimitiveIndex];
	const Material material = materialBuffer[primitive.MaterialIndex];

	const Texture2D<float32x4> baseColorOrDiffuseTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.BaseColorOrDiffuseTextureIndex)];
	const float32 alpha = baseColorOrDiffuseTexture.Sample(GetAnisotropicWrapSampler(), input.UV).a * material.BaseColorOrDiffuseFactor.a;

	if (alpha < material.AlphaCutoff && RootConstants.ViewMode != ViewMode::Geometry)
	{
		discard;
	}

	return uint32x2(RootConstants.DrawCallIndex + 1, primitiveID + 1);
}
