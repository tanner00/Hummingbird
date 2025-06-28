static const float SensorSensitivity = 100.0f;

float ConvertAverageLuminanceToEv100(float averageLuminance)
{
	static const float calibrationFactor = 12.5f;

	return log2((averageLuminance * SensorSensitivity) / calibrationFactor);
}

float ConvertEv100ToExposure(float ev100)
{
	static const float middleGrayFactor = 78.0f;
	static const float lensVignetteAttenuation = 0.65f;

	const float maxLuminance = (middleGrayFactor / (SensorSensitivity * lensVignetteAttenuation)) * exp2(ev100);
	return 1.0f / maxLuminance;
}

float3 ToneMapAcesApproximate(float3 x)
{
	static const float a = 2.51f;
	static const float b = 0.03f;
	static const float c = 2.43f;
	static const float d = 0.59f;
	static const float e = 0.14f;

	x *= 0.6f;
	return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}
