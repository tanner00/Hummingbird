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
	const SamplerState linearWrapSampler = ResourceDescriptorHeap[RootConstants.LinearWrapSamplerIndex];

	if (RootConstants.DebugViewMode)
	{
		const float32x3 ldrColor = hdrTexture.Sample(linearWrapSampler, input.UV);
		return float32x4(ldrColor, 1.0f);
	}

	const float32x3 hdrColor = hdrTexture.Sample(linearWrapSampler, input.UV);

	RWByteAddressBuffer luminanceBuffer = ResourceDescriptorHeap[RootConstants.LuminanceBufferIndex];
	const float32 averageLuminance = luminanceBuffer.Load<float32>(LuminanceHistogramBinsCount * sizeof(uint32));

	const float32x3 exposed = hdrColor * ConvertEv100ToExposure(ConvertAverageLuminanceToEv100(averageLuminance));
	return float32x4(ToneMapACES(exposed), 1.0f);
}
