#include "Barycentrics.hlsli"
#include "Geometry.hlsli"
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
		mul(node.LocalToWorld, float4(positionsLocal[0], 1.0f)),
		mul(node.LocalToWorld, float4(positionsLocal[1], 1.0f)),
		mul(node.LocalToWorld, float4(positionsLocal[2], 1.0f)),
	};
	const float4 currentPositionsClip[] =
	{
		mul(RootConstants.WorldToClip, currentPositionsWorld[0]),
		mul(RootConstants.WorldToClip, currentPositionsWorld[1]),
		mul(RootConstants.WorldToClip, currentPositionsWorld[2]),
	};

	float3 currentWeights;
	CalculateBarycentrics(currentPositionsClip, dispatchThreadID.xy + 0.5f, hdrTextureDimensions, currentWeights);

	const float3 currentPositionWorld = LerpBarycentrics(currentWeights, currentPositionsWorld[0].xyz, currentPositionsWorld[1].xyz, currentPositionsWorld[2].xyz);

	const float4 currentPositionClip = mul(RootConstants.WorldToClip, float4(currentPositionWorld, 1.0f));
	const float4 previousPositionClip = mul(RootConstants.PreviousWorldToClip, float4(currentPositionWorld, 1.0f));

	const float2 currentPositionUV = ((currentPositionClip.xy / currentPositionClip.w) + 1.0f) / float2(2.0f, -2.0f);
	const float2 previousPositionUV = ((previousPositionClip.xy / previousPositionClip.w) + 1.0f) / float2(2.0f, -2.0f);
	const float2 velocityUV = previousPositionUV - currentPositionUV;
	const float2 reprojectedUV = (dispatchThreadID.xy + 0.5f) / hdrTextureDimensions + velocityUV;

	const Texture2D<float3> previousAccumulationTexture = ResourceDescriptorHeap[RootConstants.PreviousAccumulationTextureIndex];
	const SamplerState linearClampSampler = ResourceDescriptorHeap[RootConstants.LinearClampSamplerIndex];

	const float3 current = hdrTexture.Load(uint3(dispatchThreadID.xy, 0));
	const float3 previous = previousAccumulationTexture.Sample(linearClampSampler, reprojectedUV);

	float3 minColor = Infinity;
	float3 maxColor = -Infinity;
	for (int x = -1; x <= 1; ++x)
	{
		for (int y = -1; y <= 1; ++y)
		{
			const float3 color = hdrTexture.Load(uint3(dispatchThreadID.xy + uint2(x, y), 0));
			minColor = min(minColor, color);
			maxColor = max(maxColor, color);
		}
	}

	const float3 previousClamped = clamp(previous, minColor, maxColor);

	accumulationTexture[dispatchThreadID.xy] = current * 0.1f + previousClamped * 0.9f;
}
