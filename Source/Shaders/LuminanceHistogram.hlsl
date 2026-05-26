#include "LuminanceHistogram.hlsli"
#include "Common.hlsli"
#include "Types.hlsli"

ConstantBuffer<LuminanceHistogramRootConstants> RootConstants : register(b0);

groupshared uint32 HistogramShared[LuminanceHistogramBinsCount];

[numthreads(16, 16, 1)]
void ComputeStart(uint32 groupIndex : SV_GroupIndex, uint32x3 dispatchThreadID : SV_DispatchThreadID)
{
	RWByteAddressBuffer luminanceBuffer = ResourceDescriptorHeap[RootConstants.LuminanceBufferIndex];

	const Texture2D<float32x3> hdrTexture = ResourceDescriptorHeap[RootConstants.HDRTextureIndex];

	HistogramShared[groupIndex] = 0;

	GroupMemoryBarrierWithGroupSync();

	uint32x2 hdrTextureDimensions;
	hdrTexture.GetDimensions(hdrTextureDimensions.x, hdrTextureDimensions.y);

	if (all(dispatchThreadID.xy < hdrTextureDimensions))
	{
		const float32x3 hdrRGB = hdrTexture.Load(uint32x3(dispatchThreadID.xy, 0));

		const uint32 binIndex = HDRToHistogramBin(hdrRGB);
		InterlockedAdd(HistogramShared[binIndex], 1);
	}

	GroupMemoryBarrierWithGroupSync();

	luminanceBuffer.InterlockedAdd(groupIndex * sizeof(uint32), HistogramShared[groupIndex]);
}
