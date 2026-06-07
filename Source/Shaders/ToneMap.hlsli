#include "Base.hlsli"

static const float32 SensorSensitivity = 100.0f;

float32 ConvertAverageLuminanceToEv100(float32 averageLuminance)
{
	static const float32 calibrationFactor = 12.5f;

	return log2((averageLuminance * SensorSensitivity) / calibrationFactor);
}

float32 ConvertEv100ToExposure(float32 ev100)
{
	static const float32 middleGrayFactor = 78.0f;
	static const float32 lensVignetteAttenuation = 0.65f;

	const float32 maxLuminance = (middleGrayFactor / (SensorSensitivity * lensVignetteAttenuation)) * exp2(ev100);
	return 1.0f / maxLuminance;
}

float32x3 ToneMapACES(float32x3 hdrRGB)
{
	static const float32x3x3 RGBToXYZToD65ToD60ToAP1ToRRTSAT =
	{
		{ 0.59719f, 0.35458f, 0.04823f },
		{ 0.07600f, 0.90834f, 0.01566f },
		{ 0.02840f, 0.13383f, 0.83777f },
	};
	static const float32x3x3 ODTSATToXYZToD60ToD65ToRGB =
	{
		{ 1.604750f, -0.531080f, -0.07367f },
		{ -0.10208f, 1.1081300f, -0.00605f },
		{ -0.00327f, -0.072760f, 1.076020f },
	};

	const float32x3 aces = mul(RGBToXYZToD65ToD60ToAP1ToRRTSAT, hdrRGB);
	const float32x3 curvedACES = (aces * (aces + 0.0245786f) - 0.000090537f) / (aces * (0.983729f * aces + 0.4329510f) + 0.238081f);
	return mul(ODTSATToXYZToD60ToD65ToRGB, curvedACES);
}
