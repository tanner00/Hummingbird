#include "Base.hlsli"

float32 Median(float32x3 v)
{
	return max(min(v.x, v.y), min(max(v.x, v.y), v.z));
}

float32 CalculateDistanceFieldRangeSS(float32x2 uv, float32x2 unitRange)
{
	const float32x2 textureScreenSize = 1.0f / fwidth(uv);
	return max(0.5f * dot(unitRange, textureScreenSize), 1.0f);
}

float32 RoundedRectangleSDF(float32x2 centerPosition, float32x2 halfSize, float32x4 cornerRadii)
{
	const float32 cornerRadius = centerPosition.x > 0.0f ? centerPosition.y > 0.0f ? cornerRadii.w : cornerRadii.y
														 : centerPosition.y > 0.0f ? cornerRadii.z : cornerRadii.x;

	const float32x2 innerOffset = abs(centerPosition) - halfSize + cornerRadius;
	return min(max(innerOffset.x, innerOffset.y), 0.0f) + length(max(innerOffset, 0.0f)) - cornerRadius;
}
