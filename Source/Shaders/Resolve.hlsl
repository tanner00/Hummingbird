#include "Resolve.hlsli"
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
	float3 positionsLS[3];
	LoadTriangleIndices(vertexBuffer, primitive, triangleOffset, indices);
	LoadTrianglePositions(vertexBuffer, primitive, indices, positionsLS);

	const float4 currentPositionsWS[] =
	{
		TransformLocalPositionToWorld(positionsLS[0], node.LocalToWorld),
		TransformLocalPositionToWorld(positionsLS[1], node.LocalToWorld),
		TransformLocalPositionToWorld(positionsLS[2], node.LocalToWorld),
	};
	const float4 currentPositionsCS[] =
	{
		TransformWorldToClip(currentPositionsWS[0], RootConstants.WorldToClip),
		TransformWorldToClip(currentPositionsWS[1], RootConstants.WorldToClip),
		TransformWorldToClip(currentPositionsWS[2], RootConstants.WorldToClip),
	};

	float3 currentWeights;
	CalculateBarycentrics(currentPositionsCS, dispatchThreadID.xy + 0.5f, hdrTextureDimensions, currentWeights);

	const float3 currentPositionWS = LerpBarycentrics(currentWeights, currentPositionsWS[0].xyz, currentPositionsWS[1].xyz, currentPositionsWS[2].xyz);

	const float4 currentPositionCS = TransformWorldToClip(float4(currentPositionWS, 1.0f), RootConstants.WorldToClip);
	const float4 previousPositionCS = TransformWorldToClip(float4(currentPositionWS, 1.0f), RootConstants.PreviousWorldToClip);

	const float2 currentPositionUV = TransformClipToUV(currentPositionCS);
	const float2 previousPositionUV = TransformClipToUV(previousPositionCS);
	const float2 velocityUV = previousPositionUV - currentPositionUV;
	const float2 reprojectedUV = TransformTexelToUV(dispatchThreadID.xy, hdrTextureDimensions) + velocityUV;

	const Texture2D<float3> previousAccumulationTexture = ResourceDescriptorHeap[RootConstants.PreviousAccumulationTextureIndex];

	const float3 currentColor = hdrTexture.Load(uint3(dispatchThreadID.xy, 0));
	const float3 previousColor = SampleTextureCatmullRom(previousAccumulationTexture, hdrTextureDimensions, reprojectedUV);

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

	const float3 previousColorClamped = clamp(previousColor, minColor, maxColor);

	const float reprojectedFactor = frac(length(velocityUV * hdrTextureDimensions));
	const float currentFactor = RootConstants.DiscardPreviousFrame ? 1.0f : lerp(0.05f, 0.5f, reprojectedFactor);

	const float3 resolvedColor = InverseToneMapReinhard(lerp(ToneMapReinhard(previousColorClamped), ToneMapReinhard(currentColor), currentFactor));
	accumulationTexture[dispatchThreadID.xy] = resolvedColor;
}
