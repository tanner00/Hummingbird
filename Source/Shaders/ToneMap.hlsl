#include "ToneMap.hlsli"
#include "Luminance.hlsli"
#include "Transform.hlsli"
#include "Types.hlsli"

struct PixelInput
{
	float32x4 PositionCS : SV_POSITION;
	float32x2 UV : TEXCOORD0;
};

ConstantBuffer<ToneMapRootConstants> RootConstants : register(b0);

PixelInput VertexStart(uint32 vertexID : SV_VertexID)
{
	const float32x2 uv = TransformVertexIDToUV(vertexID);

	PixelInput result;
	result.UV = uv;
	result.PositionCS = TransformUVToClip(uv);
	return result;
}

float32x4 PixelStart(PixelInput input) : SV_TARGET
{
	const Texture2D<float32x3> hdrTexture = ResourceDescriptorHeap[RootConstants.HDRTextureIndex];
	const SamplerState linearClampSampler = ResourceDescriptorHeap[RootConstants.LinearClampSamplerIndex];

	if (RootConstants.DebugViewMode)
	{
		const float32x3 ldrRGB = hdrTexture.Sample(linearClampSampler, input.UV);
		return float32x4(ldrRGB, 1.0f);
	}

	const float32x3 hdrRGB = hdrTexture.Sample(linearClampSampler, input.UV);

	const RWByteAddressBuffer luminanceBuffer = ResourceDescriptorHeap[RootConstants.LuminanceBufferIndex];
	const float32 averageLuminance = luminanceBuffer.Load<float32>(LuminanceHistogramBinsCount * sizeof(uint32));

	const float32x3 exposedRGB = hdrRGB * ConvertEv100ToExposure(ConvertAverageLuminanceToEv100(averageLuminance));
	return float32x4(ToneMapACES(exposedRGB), 1.0f);
}
