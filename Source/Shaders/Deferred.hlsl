#include "Barycentrics.hlsli"
#include "Geometry.hlsli"
#include "Shade.hlsli"
#include "Transform.hlsli"

ConstantBuffer<DeferredRootConstants> RootConstants : register(b0);
ConstantBuffer<Scene> Scene : register(b1);

[numthreads(16, 16, 1)]
void ComputeStart(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	RWTexture2D<float3> hdrTexture = ResourceDescriptorHeap[RootConstants.HDRTextureIndex];

	uint2 hdrTextureDimensions;
	hdrTexture.GetDimensions(hdrTextureDimensions.x, hdrTextureDimensions.y);

	if (any(dispatchThreadID.xy >= hdrTextureDimensions))
	{
		return;
	}

	const Texture2D<uint2> visibilityTexture = ResourceDescriptorHeap[RootConstants.VisibilityTextureIndex];
	const uint2 visibility = visibilityTexture.Load(uint3(dispatchThreadID.xy, 0));

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
	float3 positionsLS[3];
	float2 uvs[3];
	float3 normalsLS[3];
	LoadTriangleIndices(vertexBuffer, primitive, triangleOffset, indices);
	LoadTrianglePositions(vertexBuffer, primitive, indices, positionsLS);
	LoadTriangleTextureCoordinates(vertexBuffer, primitive, indices, uvs);
	LoadTriangleNormals(vertexBuffer, primitive, indices, normalsLS);

	const float4 positionsWS[] =
	{
		TransformLocalPositionToWorld(positionsLS[0], node.LocalToWorld),
		TransformLocalPositionToWorld(positionsLS[1], node.LocalToWorld),
		TransformLocalPositionToWorld(positionsLS[2], node.LocalToWorld),
	};
	const float4 positionsCS[] =
	{
		TransformWorldToClip(positionsWS[0], Scene.WorldToClip),
		TransformWorldToClip(positionsWS[1], Scene.WorldToClip),
		TransformWorldToClip(positionsWS[2], Scene.WorldToClip),
	};
	const float4 jitterPositionsCS[] =
	{
		TransformWorldToClip(positionsWS[0], Scene.JitterWorldToClip),
		TransformWorldToClip(positionsWS[1], Scene.JitterWorldToClip),
		TransformWorldToClip(positionsWS[2], Scene.JitterWorldToClip),
	};

	float3 weights;
	float3 ddxWeights;
	float3 ddyWeights;
	CalculateBarycentrics(positionsCS, dispatchThreadID.xy + 0.5f, hdrTextureDimensions, weights, ddxWeights, ddyWeights);

	const float2 uv = LerpBarycentrics(weights, uvs[0], uvs[1], uvs[2]);

	const float2 ddxUV = LerpBarycentrics(ddxWeights, uvs[0], uvs[1], uvs[2]);
	const float2 ddyUV = LerpBarycentrics(ddyWeights, uvs[0], uvs[1], uvs[2]);

	float3 jitterWeights;
	float3 ddxJitterWeights;
	float3 ddyJitterWeights;
	CalculateBarycentrics(jitterPositionsCS, dispatchThreadID.xy + 0.5f, hdrTextureDimensions, jitterWeights, ddxJitterWeights, ddyJitterWeights);

	const float3 positionWS = LerpBarycentrics(jitterWeights, positionsWS[0].xyz, positionsWS[1].xyz, positionsWS[2].xyz);
	const float3 normalLS = LerpBarycentrics(jitterWeights, normalsLS[0], normalsLS[1], normalsLS[2]);

	const float3 ddxPositionWS = LerpBarycentrics(ddxJitterWeights, positionsWS[0].xyz, positionsWS[1].xyz, positionsWS[2].xyz);
	const float3 ddyPositionWS = LerpBarycentrics(ddyJitterWeights, positionsWS[0].xyz, positionsWS[1].xyz, positionsWS[2].xyz);

	hdrTexture[dispatchThreadID.xy] = Shade(Scene,
											positionWS,
											uv,
											TransformLocalDirectionToWorld(normalLS, node.NormalLocalToWorld),
											drawCall.PrimitiveIndex,
											ddxPositionWS,
											ddyPositionWS,
											ddxUV,
											ddyUV,
											RootConstants.ViewMode,
											triangleIndex,
											RootConstants.AnisotropicWrapSamplerIndex).rgb;
}
