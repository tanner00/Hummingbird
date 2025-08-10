#include "Types.hlsli"

ConstantBuffer<ResolveRootConstants> RootConstants : register(b0);

[numthreads(16, 16, 1)]
void ComputeStart(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	const Texture2D<float3> hdrTexture = ResourceDescriptorHeap[RootConstants.HDRTextureIndex];

	uint2 hdrTextureDimensions;
	hdrTexture.GetDimensions(hdrTextureDimensions.x, hdrTextureDimensions.y);

	[branch]
	if (any(dispatchThreadID.xy >= hdrTextureDimensions))
	{
		return;
	}

	const Texture2D<float3> previousAccumulationTexture = ResourceDescriptorHeap[RootConstants.PreviousAccumulationTextureIndex];

	const float3 current = hdrTexture.Load(uint3(dispatchThreadID.xy, 0));
	const float3 previous = previousAccumulationTexture.Load(uint3(dispatchThreadID.xy, 0));

	RWTexture2D<float3> accumulationTexture = ResourceDescriptorHeap[RootConstants.AccumulationTextureIndex];
	accumulationTexture[dispatchThreadID.xy] = current * 0.1f + previous * 0.9f;
}
