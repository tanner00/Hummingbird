#include "Types.hlsli"

struct VertexInput
{
	float3 Position : POSITION0;
	float2 TextureCoordinate : TEXCOORD0;
	float3 Normal : NORMAL0;
	float4 Tangent : TANGENT0;
};

struct ScenePixelInput
{
	float4 Position : SV_POSITION;
	float2 TextureCoordinate : TEXCOORD0;
};

ConstantBuffer<SceneRootConstants> RootConstants : register(b0);
ConstantBuffer<Scene> Scene : register(b1);

ScenePixelInput VertexStart(VertexInput input)
{
	const StructuredBuffer<Node> nodeBuffer = ResourceDescriptorHeap[Scene.NodeBufferIndex];
	const Node node = nodeBuffer[RootConstants.NodeIndex];

	const float4 worldPosition = mul(node.Transform, float4(input.Position, 1.0f));

	ScenePixelInput result;
	result.Position = mul(Scene.ViewProjection, worldPosition);
	result.TextureCoordinate = input.TextureCoordinate;
	return result;
}

uint2 PixelStart(ScenePixelInput input, uint primitiveID : SV_PrimitiveID) : SV_Target
{
	const SamplerState anisotropicWrapSampler = ResourceDescriptorHeap[RootConstants.AnisotropicWrapSamplerIndex];
	const StructuredBuffer<Primitive> primitiveBuffer = ResourceDescriptorHeap[Scene.PrimitiveBufferIndex];
	const StructuredBuffer<Material> materialBuffer = ResourceDescriptorHeap[Scene.MaterialBufferIndex];

	const Primitive primitive = primitiveBuffer[RootConstants.PrimitiveIndex];
	const Material material = materialBuffer[primitive.MaterialIndex];

	const Texture2D<float4> baseColorOrDiffuseTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.BaseColorOrDiffuseTextureIndex)];
	const float alpha = baseColorOrDiffuseTexture.Sample(anisotropicWrapSampler, input.TextureCoordinate).a * material.BaseColorOrDiffuseFactor.a;

	[branch]
	if (alpha < material.AlphaCutoff && RootConstants.ViewMode != ViewMode::Geometry)
	{
		discard;
	}

	return uint2(RootConstants.DrawCallIndex + 1, primitiveID + 1);
}
