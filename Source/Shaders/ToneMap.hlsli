#include "Common.hlsli"

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

float32x3 ToneMapACES(float32x3 x)
{
	static const float32 a = 2.51f;
	static const float32 b = 0.03f;
	static const float32 c = 2.43f;
	static const float32 d = 0.59f;
	static const float32 e = 0.14f;

	x *= 0.6f;
	return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}
