#include "Luminance.hlsli"
#include "Types.hlsli"

ConstantBuffer<LuminanceHistogramRootConstants> RootConstants : register(b0);

groupshared uint HistogramShared[LuminanceHistogramBinsCount];

uint HdrToHistogramBin(float3 hdrColor)
{
	const float luminance = dot(hdrColor, float3(0.2127f, 0.7152f, 0.0722f));

	[branch]
	if (luminance < 0.005f)
	{
		return 0;
	}

	const float luminanceLog = saturate((log2(luminance) - LuminanceLogMinimum) / LuminanceLogRange);
	return (uint)(luminanceLog * (LuminanceHistogramBinsCount - 2) + 1.0f);
}

[numthreads(16, 16, 1)]
void ComputeStart(uint groupIndex : SV_GroupIndex, uint3 dispatchThreadID : SV_DispatchThreadID)
{
	HistogramShared[groupIndex] = 0;

	GroupMemoryBarrierWithGroupSync();

	const Texture2D<float3> hdrTexture = ResourceDescriptorHeap[RootConstants.HDRTextureIndex];

	uint2 hdrTextureDimensions;
	hdrTexture.GetDimensions(hdrTextureDimensions.x, hdrTextureDimensions.y);

	[branch]
	if (all(dispatchThreadID.xy < hdrTextureDimensions))
	{
		const float3 hdrColor = hdrTexture.Load(uint3(dispatchThreadID.xy, 0));

		const uint binIndex = HdrToHistogramBin(hdrColor);
		InterlockedAdd(HistogramShared[binIndex], 1);
	}

	GroupMemoryBarrierWithGroupSync();

	RWByteAddressBuffer luminanceBuffer = ResourceDescriptorHeap[RootConstants.LuminanceBufferIndex];
	luminanceBuffer.InterlockedAdd(groupIndex * sizeof(uint), HistogramShared[groupIndex]);
}
