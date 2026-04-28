#include "Common.hlsli"
#include "Luminance.hlsli"

uint32 HDRToHistogramBin(float32x3 hdrColor)
{
	const float32 luminance = dot(hdrColor, float32x3(0.2127f, 0.7152f, 0.0722f));

	if (luminance < 0.005f)
	{
		return 0;
	}

	const float32 luminanceLog = saturate((log2(luminance) - LuminanceLogMinimum) / LuminanceLogRange);
	return (uint32)(luminanceLog * (LuminanceHistogramBinsCount - 2) + 1.0f);
}
