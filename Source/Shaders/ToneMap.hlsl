#include "ToneMap.hlsli"
#include "Luminance.hlsli"
#include "Transform.hlsli"
#include "Types.hlsli"

struct PixelInput
{
	float4 PositionCS : SV_POSITION;
	float2 UV : TEXCOORD0;
};

ConstantBuffer<ToneMapRootConstants> RootConstants : register(b0);

PixelInput VertexStart(uint vertexID : SV_VertexID)
{
	const float2 uv = TransformVertexIDToUV(vertexID);

	PixelInput result;
	result.UV = uv;
	result.PositionCS = TransformUVToClip(uv);
	return result;
}

float4 PixelStart(PixelInput input) : SV_TARGET
{
	const Texture2D<float3> hdrTexture = ResourceDescriptorHeap[RootConstants.HDRTextureIndex];
	const SamplerState linearWrapSampler = ResourceDescriptorHeap[RootConstants.LinearWrapSamplerIndex];

	if (RootConstants.DebugViewMode)
	{
		const float3 ldrColor = hdrTexture.Sample(linearWrapSampler, input.UV);
		return float4(ldrColor, 1.0f);
	}

	const float3 hdrColor = hdrTexture.Sample(linearWrapSampler, input.UV);

	RWByteAddressBuffer luminanceBuffer = ResourceDescriptorHeap[RootConstants.LuminanceBufferIndex];
	const float averageLuminance = luminanceBuffer.Load<float>(LuminanceHistogramBinsCount * sizeof(uint));

	const float3 exposed = hdrColor * ConvertEv100ToExposure(ConvertAverageLuminanceToEv100(averageLuminance));
	return float4(ToneMapACES(exposed), 1.0f);
}
