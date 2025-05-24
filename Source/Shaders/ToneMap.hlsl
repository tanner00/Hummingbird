#include "Luminance.hlsli"

static const float SensorSensitivity = 100.0f;

struct PixelInput
{
	float4 Position : SV_POSITION;
	float2 Uv : TEXCOORD0;
};

struct RootConstants
{
	uint HDRTextureIndex;
	uint AnisotropicWrapSamplerIndex;
	uint LuminanceBufferIndex;

	bool DebugViewMode;
};
ConstantBuffer<RootConstants> RootConstants : register(b0);

PixelInput VertexStart(uint vertexID : SV_VertexID)
{
	const float2 uv = float2(((2 - vertexID) << 1) & 2, (2 - vertexID) & 2);

	PixelInput result;
	result.Uv = uv;
	result.Position = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
	return result;
}

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

float4 PixelStart(PixelInput input) : SV_TARGET
{
	const Texture2D<float3> hdrTexture = ResourceDescriptorHeap[RootConstants.HDRTextureIndex];
	const SamplerState anisotropicWrapSampler = ResourceDescriptorHeap[RootConstants.AnisotropicWrapSamplerIndex];

	if (RootConstants.DebugViewMode)
	{
		const float3 ldrColor = hdrTexture.Sample(anisotropicWrapSampler, input.Uv);
		return float4(ldrColor, 1.0f);
	}

	RWByteAddressBuffer luminanceBuffer = ResourceDescriptorHeap[RootConstants.LuminanceBufferIndex];

	const float3 hdrColor = hdrTexture.Sample(anisotropicWrapSampler, input.Uv);

	const float averageLuminance = luminanceBuffer.Load<float>(LuminanceHistogramBinsCount * sizeof(uint));

	const float3 exposed = hdrColor * ConvertEv100ToExposure(ConvertAverageLuminanceToEv100(averageLuminance));
	return float4(ToneMapAcesApproximate(exposed), 1.0f);
}
