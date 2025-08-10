#include "ToneMap.hlsli"
#include "Luminance.hlsli"
#include "Types.hlsli"

struct PixelInput
{
	float4 PositionClip : SV_POSITION;
	float2 UV : TEXCOORD0;
};

ConstantBuffer<ToneMapRootConstants> RootConstants : register(b0);

PixelInput VertexStart(uint vertexID : SV_VertexID)
{
	const float2 uv = float2(((2 - vertexID) << 1) & 2, (2 - vertexID) & 2);

	PixelInput result;
	result.UV = uv;
	result.PositionClip = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
	return result;
}

float4 PixelStart(PixelInput input) : SV_TARGET
{
	const Texture2D<float3> hdrTexture = ResourceDescriptorHeap[RootConstants.HDRTextureIndex];
	const SamplerState anisotropicWrapSampler = ResourceDescriptorHeap[RootConstants.AnisotropicWrapSamplerIndex];

	if (RootConstants.DebugViewMode)
	{
		const float3 ldrColor = hdrTexture.Sample(anisotropicWrapSampler, input.UV);
		return float4(ldrColor, 1.0f);
	}

	RWByteAddressBuffer luminanceBuffer = ResourceDescriptorHeap[RootConstants.LuminanceBufferIndex];

	const float3 hdrColor = hdrTexture.Sample(anisotropicWrapSampler, input.UV);

	const float averageLuminance = luminanceBuffer.Load<float>(LuminanceHistogramBinsCount * sizeof(uint));

	const float3 exposed = hdrColor * ConvertEv100ToExposure(ConvertAverageLuminanceToEv100(averageLuminance));
	return float4(ToneMapAcesApproximate(exposed), 1.0f);
}
