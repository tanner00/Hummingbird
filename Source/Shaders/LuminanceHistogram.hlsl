#include "LuminanceHistogram.hlsli"
#include "Types.hlsli"

ConstantBuffer<LuminanceHistogramRootConstants> RootConstants : register(b0);

groupshared uint HistogramShared[LuminanceHistogramBinsCount];

[numthreads(16, 16, 1)]
void ComputeStart(uint groupIndex : SV_GroupIndex, uint3 dispatchThreadID : SV_DispatchThreadID)
{
	HistogramShared[groupIndex] = 0;

	GroupMemoryBarrierWithGroupSync();

	const Texture2D<float3> hdrTexture = ResourceDescriptorHeap[RootConstants.HDRTextureIndex];

	uint2 hdrTextureDimensions;
	hdrTexture.GetDimensions(hdrTextureDimensions.x, hdrTextureDimensions.y);

	if (all(dispatchThreadID.xy < hdrTextureDimensions))
	{
		const float3 hdrColor = hdrTexture.Load(uint3(dispatchThreadID.xy, 0));

		const uint binIndex = HDRToHistogramBin(hdrColor);
		InterlockedAdd(HistogramShared[binIndex], 1);
	}

	GroupMemoryBarrierWithGroupSync();

	RWByteAddressBuffer luminanceBuffer = ResourceDescriptorHeap[RootConstants.LuminanceBufferIndex];
	luminanceBuffer.InterlockedAdd(groupIndex * sizeof(uint), HistogramShared[groupIndex]);
}
