#include "Barycentrics.hlsli"
#include "Geometry.hlsli"
#include "Transform.hlsli"
#include "Types.hlsli"

ConstantBuffer<ResolveRootConstants> RootConstants : register(b0);

[numthreads(16, 16, 1)]
void ComputeStart(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	const Texture2D<float3> hdrTexture = ResourceDescriptorHeap[RootConstants.HDRTextureIndex];

	uint2 hdrTextureDimensions;
	hdrTexture.GetDimensions(hdrTextureDimensions.x, hdrTextureDimensions.y);

	if (any(dispatchThreadID.xy >= hdrTextureDimensions))
	{
		return;
	}

	RWTexture2D<float3> accumulationTexture = ResourceDescriptorHeap[RootConstants.AccumulationTextureIndex];

	const Texture2D<uint2> visibilityTexture = ResourceDescriptorHeap[RootConstants.VisibilityTextureIndex];
	const uint2 visibility = visibilityTexture.Load(uint3(dispatchThreadID.xy, 0));

	if (all(visibility.xy == 0))
	{
		accumulationTexture[dispatchThreadID.xy] = 0.0f;
		return;
	}
	const uint drawCallIndex = visibility.x - 1;
	const uint triangleIndex = visibility.y - 1;

	const StructuredBuffer<DrawCall> drawCallBuffer = ResourceDescriptorHeap[RootConstants.DrawCallBufferIndex];
	const DrawCall drawCall = drawCallBuffer[drawCallIndex];

	const StructuredBuffer<Node> nodeBuffer = ResourceDescriptorHeap[RootConstants.NodeBufferIndex];
	const Node node = nodeBuffer[drawCall.NodeIndex];

	const StructuredBuffer<Primitive> primitiveBuffer = ResourceDescriptorHeap[RootConstants.PrimitiveBufferIndex];
	const Primitive primitive = primitiveBuffer[drawCall.PrimitiveIndex];

	const ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[RootConstants.VertexBufferIndex];
	const uint triangleOffset = triangleIndex * primitive.IndexStride * 3;

	uint indices[3];
	float3 positionsLocal[3];
	LoadTriangleIndices(vertexBuffer, primitive, triangleOffset, indices);
	LoadTrianglePositions(vertexBuffer, primitive, indices, positionsLocal);

	const float4 currentPositionsWorld[] =
	{
		TransformLocalPositionToWorld(positionsLocal[0], node.LocalToWorld),
		TransformLocalPositionToWorld(positionsLocal[1], node.LocalToWorld),
		TransformLocalPositionToWorld(positionsLocal[2], node.LocalToWorld),
	};
	const float4 currentPositionsClip[] =
	{
		TransformWorldToClip(currentPositionsWorld[0], RootConstants.WorldToClip),
		TransformWorldToClip(currentPositionsWorld[1], RootConstants.WorldToClip),
		TransformWorldToClip(currentPositionsWorld[2], RootConstants.WorldToClip),
	};

	float3 currentWeights;
	CalculateBarycentrics(currentPositionsClip, dispatchThreadID.xy + 0.5f, hdrTextureDimensions, currentWeights);

	const float3 currentPositionWorld = LerpBarycentrics(currentWeights, currentPositionsWorld[0].xyz, currentPositionsWorld[1].xyz, currentPositionsWorld[2].xyz);

	const float4 currentPositionClip = TransformWorldToClip(float4(currentPositionWorld, 1.0f), RootConstants.WorldToClip);
	const float4 previousPositionClip = TransformWorldToClip(float4(currentPositionWorld, 1.0f), RootConstants.PreviousWorldToClip);

	const float2 currentPositionUV = TransformClipToUV(currentPositionClip);
	const float2 previousPositionUV = TransformClipToUV(previousPositionClip);
	const float2 velocityUV = previousPositionUV - currentPositionUV;
	const float2 reprojectedUV = TransformTexelToUV(dispatchThreadID.xy, hdrTextureDimensions) + velocityUV;

	const Texture2D<float3> previousAccumulationTexture = ResourceDescriptorHeap[RootConstants.PreviousAccumulationTextureIndex];

	const float3 current = hdrTexture.Load(uint3(dispatchThreadID.xy, 0));
	const float3 previous = previousAccumulationTexture.Sample(linearClampSampler, reprojectedUV);

	float3 minColor = Infinity;
	float3 maxColor = -Infinity;
	for (int x = -1; x <= 1; ++x)
	{
		for (int y = -1; y <= 1; ++y)
		{
			const float3 color = hdrTexture.Load(uint3(dispatchThreadID.xy + int2(x, y), 0));
			minColor = min(minColor, color);
			maxColor = max(maxColor, color);
		}
	}

	const float3 previousClamped = clamp(previous, minColor, maxColor);

	const float currentFactor = RootConstants.DiscardPreviousFrame ? 1.0f : 0.1f;
	const float previousFactor = RootConstants.DiscardPreviousFrame ? 0.0f : 0.9f;

	accumulationTexture[dispatchThreadID.xy] = current * currentFactor + previousClamped * previousFactor;
}
