#include "LuminanceHistogram.hlsli"
#include "Common.hlsli"
#include "Types.hlsli"

ConstantBuffer<LuminanceHistogramRootConstants> RootConstants : register(b0);

groupshared uint32 HistogramShared[LuminanceHistogramBinsCount];

[numthreads(16, 16, 1)]
void ComputeStart(uint32 groupIndex : SV_GroupIndex, uint32x3 dispatchThreadID : SV_DispatchThreadID)
{
	HistogramShared[groupIndex] = 0;

	GroupMemoryBarrierWithGroupSync();

	const Texture2D<float32x3> hdrTexture = ResourceDescriptorHeap[RootConstants.HDRTextureIndex];

	uint32x2 hdrTextureDimensions;
	hdrTexture.GetDimensions(hdrTextureDimensions.x, hdrTextureDimensions.y);

	if (all(dispatchThreadID.xy < hdrTextureDimensions))
	{
		const float32x3 hdrColor = hdrTexture.Load(uint32x3(dispatchThreadID.xy, 0));

		const uint32 binIndex = HDRToHistogramBin(hdrColor);
		InterlockedAdd(HistogramShared[binIndex], 1);
	}

	GroupMemoryBarrierWithGroupSync();

	RWByteAddressBuffer luminanceBuffer = ResourceDescriptorHeap[RootConstants.LuminanceBufferIndex];
	luminanceBuffer.InterlockedAdd(groupIndex * sizeof(uint32), HistogramShared[groupIndex]);
}
