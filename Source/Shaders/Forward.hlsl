#define FORWARD true

#include "Shade.hlsli"

struct VertexInput
{
	float3 Position : POSITION0;
	float2 TextureCoordinate : TEXCOORD0;
	float3 Normal : NORMAL0;
};

ConstantBuffer<SceneRootConstants> RootConstants : register(b0);
ConstantBuffer<Scene> Scene : register(b1);

ScenePixelInput VertexStart(VertexInput input)
{
	const StructuredBuffer<Node> nodeBuffer = ResourceDescriptorHeap[Scene.NodeBufferIndex];
	const Node node = nodeBuffer[RootConstants.NodeIndex];

	const float4 worldPosition = mul(node.Transform, float4(input.Position, 1.0f));

	ScenePixelInput pixel;
	pixel.ClipSpacePosition = mul(Scene.ViewProjection, worldPosition);
	pixel.WorldSpacePosition = worldPosition.xyz;
	pixel.TextureCoordinate = input.TextureCoordinate;
	pixel.Normal = mul((float3x3)RootConstants.NormalTransform, input.Normal);
	return pixel;
}

float4 PixelStart(ScenePixelInput input, uint primitiveID : SV_PrimitiveID) : SV_TARGET
{
	return float4(Shade(Scene,
						RootConstants.ViewMode,
						RootConstants.PrimitiveIndex,
						input,
						RootConstants.AnisotropicWrapSamplerIndex,
						primitiveID).rgb, 1.0f);
}
