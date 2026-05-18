#include "UI.hlsli"
#include "Transform.hlsli"
#include "Types.hlsli"

struct PixelInput
{
	float32x4 PositionCS : SV_POSITION;
	float32x2 UV : TEXCOORD0;
	uint32 DrawIndex : DRAW_INDEX;
};

ConstantBuffer<UIRootConstants> RootConstants : register(b0);

PixelInput VertexStart(uint32 vertexID : SV_VertexID)
{
	const StructuredBuffer<UIDraw> drawBuffer = ResourceDescriptorHeap[RootConstants.UIDrawBufferIndex];

	static const float32x2 vertices[] =
	{
		{ 0.0f, 1.0f },
		{ 1.0f, 0.0f },
		{ 0.0f, 0.0f },

		{ 0.0f, 1.0f },
		{ 1.0f, 1.0f },
		{ 1.0f, 0.0f },
	};
	static const uint32 verticesPerQuad = 6;

	const uint32 drawIndex = vertexID / verticesPerQuad;
	const uint32 vertexIndex = vertexID % verticesPerQuad;

	const UIDraw draw = drawBuffer[drawIndex];

	PixelInput result = (PixelInput)0;
	result.DrawIndex = drawIndex;
	switch (draw.Type)
	{
	case UIDrawType::Rectangle:
	case UIDrawType::Image:
	{
		const float32x2 positionSnappedSS = round(draw.PositionSS + draw.SizeSS * vertices[vertexIndex]);
		result.PositionCS = TransformScreenToClip(positionSnappedSS, RootConstants.ScreenToClip);
		result.UV = vertices[vertexIndex];
		break;
	}
	case UIDrawType::Character:
	{
		const float32x2 positionSS = draw.PositionSS + draw.Scale * (draw.PlanePosition + draw.PlaneSize * vertices[vertexIndex]);
		result.PositionCS = TransformScreenToClip(positionSS, RootConstants.ScreenToClip);
		result.UV = draw.AtlasPosition + draw.AtlasSize * vertices[vertexIndex];
		break;
	}
	}
	return result;
}

float32x4 PixelStart(PixelInput input) : SV_TARGET
{
	const StructuredBuffer<UIDraw> drawBuffer = ResourceDescriptorHeap[RootConstants.UIDrawBufferIndex];
	const UIDraw draw = drawBuffer[input.DrawIndex];

	const bool inside = (input.PositionCS.x >= draw.ScissorMinSS.x) && (input.PositionCS.y >= draw.ScissorMinSS.y) &&
						(input.PositionCS.x <= draw.ScissorMaxSS.x) && (input.PositionCS.y <= draw.ScissorMaxSS.y);
	if (!inside)
	{
		return 0.0f;
	}

	float32x4 color = 0.0f;
	switch (draw.Type)
	{
	case UIDrawType::Rectangle:
	{
		const float32 edgeSoftnessSS = all(draw.CornerRadiiSS <= 1.0f) ? 0.0f : 1.0f;
		const float32 borderSoftnessSS = (all(draw.CornerRadiiSS <= 1.0f) || draw.BorderSizeSS <= 1.0f) ? 0.0f : 1.0f;

		const float32x2 halfSizeSS = draw.SizeSS * 0.5f;
		const float32x2 centerPositionSS = draw.SizeSS * input.UV - halfSizeSS;

		const float32 distanceSS = RoundedRectangleSDF(centerPositionSS, halfSizeSS, draw.CornerRadiiSS);

		const float32 insideMask = 1.0f - smoothstep(0.0f, edgeSoftnessSS, distanceSS);
		const float32 borderMask = 1.0f - smoothstep(draw.BorderSizeSS - borderSoftnessSS, draw.BorderSizeSS, abs(distanceSS));

		color = lerp(draw.RGBA, draw.BorderRGBA, borderMask) * float32x4(1.0f, 1.0f, 1.0f, insideMask);
		break;
	}
	case UIDrawType::Character:
	{
		const Texture2D<float32x3> fontTexture = ResourceDescriptorHeap[RootConstants.FontTextureIndex];
		const SamplerState linearClampSampler = SamplerDescriptorHeap[RootConstants.LinearClampSamplerIndex];

		const float32x3 multiChannelSignedDistance = fontTexture.Sample(linearClampSampler, input.UV);
		const float32 signedDistance = Median(multiChannelSignedDistance);
		const float32 distanceSS = CalculateDistanceFieldRangeSS(input.UV, RootConstants.UnitRange) * (signedDistance - 0.5f);

		const float32 insideMask = saturate(distanceSS + 0.5f);

		color = draw.RGBA * float32x4(1.0f, 1.0f, 1.0f, insideMask);
		break;
	}
	case UIDrawType::Image:
	{
		const Texture2D<float32x4> imageTexture = ResourceDescriptorHeap[draw.ImageIndex];
		const SamplerState linearClampSampler = SamplerDescriptorHeap[RootConstants.LinearClampSamplerIndex];

		color = imageTexture.Sample(linearClampSampler, input.UV) * draw.RGBA;
		break;
	}
	}

	if (draw.Type != UIDrawType::Image)
	{
		color = float32x4(SRGBToLinear(color.rgb), color.a);
	}

	return color;
}
