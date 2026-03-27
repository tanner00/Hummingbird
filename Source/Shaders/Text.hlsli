float Median(float3 v)
{
	return max(min(v.x, v.y), min(max(v.x, v.y), v.z));
}

float CalculateDistanceFieldRangeSS(float2 uv, float2 unitRange)
{
	const float2 textureScreenSize = 1.0f / fwidth(uv);
	return max(0.5f * dot(unitRange, textureScreenSize), 1.0f);
}
