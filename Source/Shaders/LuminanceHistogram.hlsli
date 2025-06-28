#include "Luminance.hlsli"

uint HDRToHistogramBin(float3 hdrColor)
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
