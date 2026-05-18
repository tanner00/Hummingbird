#include "Resolve.hlsli"
#include "Barycentrics.hlsli"
#include "Geometry.hlsli"
#include "Transform.hlsli"
#include "Types.hlsli"

ConstantBuffer<ResolveRootConstants> RootConstants : register(b0);

[numthreads(16, 16, 1)]
void ComputeStart(uint32x3 dispatchThreadID : SV_DispatchThreadID)
{
	const Texture2D<float32x3> hdrTexture = ResourceDescriptorHeap[RootConstants.HDRTextureIndex];

	uint32x2 hdrTextureDimensions;
	hdrTexture.GetDimensions(hdrTextureDimensions.x, hdrTextureDimensions.y);

	if (any(dispatchThreadID.xy >= hdrTextureDimensions))
	{
		return;
	}

	RWTexture2D<float32x3> accumulationTexture = ResourceDescriptorHeap[RootConstants.AccumulationTextureIndex];

	const Texture2D<uint32x2> visibilityTexture = ResourceDescriptorHeap[RootConstants.VisibilityTextureIndex];
	const uint32x2 visibility = visibilityTexture.Load(uint32x3(dispatchThreadID.xy, 0));

	if (all(visibility.xy == 0))
	{
		accumulationTexture[dispatchThreadID.xy] = 0.0f;
		return;
	}
	const uint32 drawCallIndex = visibility.x - 1;
	const uint32 triangleIndex = visibility.y - 1;

	const StructuredBuffer<DrawCall> drawCallBuffer = ResourceDescriptorHeap[RootConstants.DrawCallBufferIndex];
	const DrawCall drawCall = drawCallBuffer[drawCallIndex];

	const StructuredBuffer<Node> nodeBuffer = ResourceDescriptorHeap[RootConstants.NodeBufferIndex];
	const Node node = nodeBuffer[drawCall.NodeIndex];

	const StructuredBuffer<Primitive> primitiveBuffer = ResourceDescriptorHeap[RootConstants.PrimitiveBufferIndex];
	const Primitive primitive = primitiveBuffer[drawCall.PrimitiveIndex];

	const ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[RootConstants.VertexBufferIndex];
	const uint32 triangleOffset = triangleIndex * primitive.IndexStride * 3;

	uint32 indices[3];
	float32x3 positionsLS[3];
	LoadTriangleIndices(vertexBuffer, primitive, triangleOffset, indices);
	LoadTrianglePositions(vertexBuffer, primitive, indices, positionsLS);

	const float32x4 currentPositionsWS[] =
	{
		TransformLocalPositionToWorld(positionsLS[0], node.LocalToWorld),
		TransformLocalPositionToWorld(positionsLS[1], node.LocalToWorld),
		TransformLocalPositionToWorld(positionsLS[2], node.LocalToWorld),
	};
	const float32x4 currentPositionsCS[] =
	{
		TransformWorldToClip(currentPositionsWS[0], RootConstants.WorldToClip),
		TransformWorldToClip(currentPositionsWS[1], RootConstants.WorldToClip),
		TransformWorldToClip(currentPositionsWS[2], RootConstants.WorldToClip),
	};

	float32x3 currentWeights;
	CalculateBarycentrics(currentPositionsCS, dispatchThreadID.xy + 0.5f, hdrTextureDimensions, currentWeights);

	const float32x3 currentPositionWS = LerpBarycentrics(currentWeights, currentPositionsWS[0].xyz, currentPositionsWS[1].xyz, currentPositionsWS[2].xyz);

	const float32x4 currentPositionCS = TransformWorldToClip(float32x4(currentPositionWS, 1.0f), RootConstants.WorldToClip);
	const float32x4 previousPositionCS = TransformWorldToClip(float32x4(currentPositionWS, 1.0f), RootConstants.PreviousWorldToClip);

	const float32x2 currentPositionUV = TransformClipToUV(currentPositionCS);
	const float32x2 previousPositionUV = TransformClipToUV(previousPositionCS);
	const float32x2 velocityUV = previousPositionUV - currentPositionUV;
	const float32x2 reprojectedUV = TransformTexelToUV(dispatchThreadID.xy, hdrTextureDimensions) + velocityUV;

	const Texture2D<float32x3> previousAccumulationTexture = ResourceDescriptorHeap[RootConstants.PreviousAccumulationTextureIndex];

	const float32x3 currentRGB = hdrTexture.Load(uint32x3(dispatchThreadID.xy, 0));
	const float32x3 previousRGB = SampleTextureCatmullRom(previousAccumulationTexture, hdrTextureDimensions, reprojectedUV);

	float32x3 minRGB = Infinity;
	float32x3 maxRGB = -Infinity;
	for (int32 x = -1; x <= 1; ++x)
	{
		for (int32 y = -1; y <= 1; ++y)
		{
			const float32x3 rgb = hdrTexture.Load(uint32x3(dispatchThreadID.xy + int32x2(x, y), 0));
			minRGB = min(minRGB, rgb);
			maxRGB = max(maxRGB, rgb);
		}
	}

	const float32x3 previousClampedRGB = clamp(previousRGB, minRGB, maxRGB);

	const float32 reprojectedFactor = frac(length(velocityUV * hdrTextureDimensions));
	const float32 currentFactor = RootConstants.DiscardPreviousFrame ? 1.0f : lerp(0.05f, 0.5f, reprojectedFactor);

	const float32x3 resolvedRGB = InverseToneMapReinhard(lerp(ToneMapReinhard(previousClampedRGB), ToneMapReinhard(currentRGB), currentFactor));
	accumulationTexture[dispatchThreadID.xy] = resolvedRGB;
}
