#include "Transform.hlsli"
#include "Types.hlsli"

struct VertexInput
{
	float3 PositionLocal : POSITION0;
	float2 UV : TEXCOORD0;
};

struct PixelInput
{
	float4 PositionClip : SV_POSITION;
	float2 UV : TEXCOORD0;
};

ConstantBuffer<SceneRootConstants> RootConstants : register(b0);
ConstantBuffer<Scene> Scene : register(b1);

PixelInput VertexStart(VertexInput input)
{
	const StructuredBuffer<Node> nodeBuffer = ResourceDescriptorHeap[Scene.NodeBufferIndex];
	const Node node = nodeBuffer[RootConstants.NodeIndex];

	PixelInput result;
	result.PositionClip = TransformWorldToClip(TransformLocalPositionToWorld(input.PositionLocal, node.LocalToWorld), Scene.JitterWorldToClip);
	result.UV = input.UV;
	return result;
}

uint2 PixelStart(PixelInput input, uint primitiveID : SV_PrimitiveID) : SV_Target
{
	const SamplerState anisotropicWrapSampler = ResourceDescriptorHeap[RootConstants.AnisotropicWrapSamplerIndex];
	const StructuredBuffer<Primitive> primitiveBuffer = ResourceDescriptorHeap[Scene.PrimitiveBufferIndex];
	const StructuredBuffer<Material> materialBuffer = ResourceDescriptorHeap[Scene.MaterialBufferIndex];

	const Primitive primitive = primitiveBuffer[RootConstants.PrimitiveIndex];
	const Material material = materialBuffer[primitive.MaterialIndex];

	const Texture2D<float4> baseColorOrDiffuseTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.BaseColorOrDiffuseTextureIndex)];
	const float alpha = baseColorOrDiffuseTexture.Sample(anisotropicWrapSampler, input.UV).a * material.BaseColorOrDiffuseFactor.a;

	if (alpha < material.AlphaCutoff && RootConstants.ViewMode != ViewMode::Geometry)
	{
		discard;
	}

	return uint2(RootConstants.DrawCallIndex + 1, primitiveID + 1);
}
