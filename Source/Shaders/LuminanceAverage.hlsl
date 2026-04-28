#include "Common.hlsli"
#include "Luminance.hlsli"
#include "Types.hlsli"

ConstantBuffer<LuminanceAverageRootConstants> RootConstants : register(b0);

groupshared uint32 HistogramShared[LuminanceHistogramBinsCount];

[numthreads(LuminanceHistogramBinsCount, 1, 1)]
void ComputeStart(uint32 groupIndex : SV_GroupIndex)
{
	RWByteAddressBuffer luminanceBuffer = ResourceDescriptorHeap[RootConstants.LuminanceBufferIndex];

	const uint32 binCount = luminanceBuffer.Load(groupIndex * sizeof(uint32));
	HistogramShared[groupIndex] = binCount * groupIndex;

	GroupMemoryBarrierWithGroupSync();

	luminanceBuffer.Store(groupIndex * sizeof(uint32), 0);

	for (uint32 luminanceHistogramBinIndex = LuminanceHistogramBinsCount >> 1; luminanceHistogramBinIndex > 0; luminanceHistogramBinIndex >>= 1)
	{
		if (groupIndex < luminanceHistogramBinIndex)
		{
			HistogramShared[groupIndex] += HistogramShared[groupIndex + luminanceHistogramBinIndex];
		}

		GroupMemoryBarrierWithGroupSync();
	}

	if (groupIndex == 0)
	{
		const float32 luminanceWeightedLog = HistogramShared[0] / max((float32)RootConstants.PixelCount - binCount, 1.0f) - 1.0f;
		const float32 luminanceAverage = exp2((luminanceWeightedLog / (LuminanceHistogramBinsCount - 2)) * LuminanceLogRange + LuminanceLogMinimum);

		luminanceBuffer.Store<float32>(LuminanceHistogramBinsCount * sizeof(uint32), luminanceAverage);
	}
}
