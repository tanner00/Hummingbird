#include "Deferred.hlsli"
#include "Geometry.hlsli"
#include "Shade.hlsli"

ConstantBuffer<DeferredRootConstants> RootConstants : register(b0);
ConstantBuffer<Scene> Scene : register(b1);

[numthreads(16, 16, 1)]
void ComputeStart(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	RWTexture2D<float3> hdrTexture = ResourceDescriptorHeap[RootConstants.HDRTextureIndex];

	uint2 hdrTextureDimensions;
	hdrTexture.GetDimensions(hdrTextureDimensions.x, hdrTextureDimensions.y);

	[branch]
	if (any(dispatchThreadID.xy >= hdrTextureDimensions))
	{
		return;
	}

	const Texture2D<uint2> visibilityTexture = ResourceDescriptorHeap[RootConstants.VisibilityTextureIndex];
	const uint2 visibility = visibilityTexture.Load(uint3(dispatchThreadID.xy, 0));

	[branch]
	if (all(visibility.xy == 0))
	{
		hdrTexture[dispatchThreadID.xy] = 0.0f;
		return;
	}
	const uint drawCallIndex = visibility.x - 1;
	const uint triangleIndex = visibility.y - 1;

	const StructuredBuffer<DrawCall> drawCallBuffer = ResourceDescriptorHeap[Scene.DrawCallBufferIndex];
	const DrawCall drawCall = drawCallBuffer[drawCallIndex];

	const StructuredBuffer<Node> nodeBuffer = ResourceDescriptorHeap[Scene.NodeBufferIndex];
	const Node node = nodeBuffer[drawCall.NodeIndex];

	const StructuredBuffer<Primitive> primitiveBuffer = ResourceDescriptorHeap[Scene.PrimitiveBufferIndex];
	const Primitive primitive = primitiveBuffer[drawCall.PrimitiveIndex];

	const ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[Scene.VertexBufferIndex];
	const uint triangleOffset = triangleIndex * primitive.IndexStride * 3;

	uint indices[3];
	float3 positionsLocal[3];
	float2 textureCoordinates[3];
	float3 normalsLocal[3];
	LoadTriangleIndices(vertexBuffer, primitive, triangleOffset, indices);
	LoadTrianglePositions(vertexBuffer, primitive, indices, positionsLocal);
	LoadTriangleTextureCoordinates(vertexBuffer, primitive, indices, textureCoordinates);
	LoadTriangleNormals(vertexBuffer, primitive, indices, normalsLocal);

	const float4 positionsWorld[] =
	{
		mul(node.LocalToWorld, float4(positionsLocal[0], 1.0f)),
		mul(node.LocalToWorld, float4(positionsLocal[1], 1.0f)),
		mul(node.LocalToWorld, float4(positionsLocal[2], 1.0f)),
	};
	const float4 positionsClip[] =
	{
		mul(Scene.WorldToClip, positionsWorld[0]),
		mul(Scene.WorldToClip, positionsWorld[1]),
		mul(Scene.WorldToClip, positionsWorld[2]),
	};

	float3 weights;
	float3 ddxWeights;
	float3 ddyWeights;
	CalculateBarycentrics(positionsClip, dispatchThreadID.xy + 0.5f, hdrTextureDimensions, weights, ddxWeights, ddyWeights);

	const float3 positionWorld = LerpBarycentrics(weights, positionsWorld[0].xyz, positionsWorld[1].xyz, positionsWorld[2].xyz);
	const float2 textureCoordinate = LerpBarycentrics(weights, textureCoordinates[0], textureCoordinates[1], textureCoordinates[2]);
	const float3 normalLocal = LerpBarycentrics(weights, normalsLocal[0], normalsLocal[1], normalsLocal[2]);

	const float3 ddxPositionWorld = LerpBarycentrics(ddxWeights, positionsWorld[0].xyz, positionsWorld[1].xyz, positionsWorld[2].xyz);
	const float3 ddyPositionWorld = LerpBarycentrics(ddyWeights, positionsWorld[0].xyz, positionsWorld[1].xyz, positionsWorld[2].xyz);

	const float2 ddxTextureCoordinate = LerpBarycentrics(ddxWeights, textureCoordinates[0], textureCoordinates[1], textureCoordinates[2]);
	const float2 ddyTextureCoordinate = LerpBarycentrics(ddyWeights, textureCoordinates[0], textureCoordinates[1], textureCoordinates[2]);

	hdrTexture[dispatchThreadID.xy] = Shade(Scene,
											positionWorld,
											textureCoordinate,
											mul((float3x3)node.NormalLocalToWorld, normalLocal),
											drawCall.PrimitiveIndex,
											ddxPositionWorld,
											ddyPositionWorld,
											ddxTextureCoordinate,
											ddyTextureCoordinate,
											RootConstants.ViewMode,
											triangleIndex,
											RootConstants.AnisotropicWrapSamplerIndex).rgb;
}
