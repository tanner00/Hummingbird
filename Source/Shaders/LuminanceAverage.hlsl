#include "Luminance.hlsli"
#include "Types.hlsli"

ConstantBuffer<LuminanceAverageRootConstants> RootConstants : register(b0);

groupshared uint HistogramShared[LuminanceHistogramBinsCount];

[numthreads(LuminanceHistogramBinsCount, 1, 1)]
void ComputeStart(uint groupIndex : SV_GroupIndex)
{
	RWByteAddressBuffer luminanceBuffer = ResourceDescriptorHeap[RootConstants.LuminanceBufferIndex];

	const uint binCount = luminanceBuffer.Load(groupIndex * sizeof(uint));
	HistogramShared[groupIndex] = binCount * groupIndex;

	GroupMemoryBarrierWithGroupSync();

	luminanceBuffer.Store(groupIndex * sizeof(uint), 0);

	[unroll]
	for (uint i = LuminanceHistogramBinsCount >> 1; i > 0; i >>= 1)
	{
		[branch]
		if (groupIndex < i)
		{
			HistogramShared[groupIndex] += HistogramShared[groupIndex + i];
		}

		GroupMemoryBarrierWithGroupSync();
	}

	[branch]
	if (groupIndex == 0)
	{
		const float luminanceWeightedLog = HistogramShared[0] / max((float)RootConstants.PixelCount - binCount, 1.0f) - 1.0f;
		const float luminanceAverage = exp2((luminanceWeightedLog / (LuminanceHistogramBinsCount - 2)) * LuminanceLogRange + LuminanceLogMinimum);

		luminanceBuffer.Store<float>(LuminanceHistogramBinsCount * sizeof(uint), luminanceAverage);
	}
}
