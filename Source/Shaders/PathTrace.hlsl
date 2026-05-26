#include "PathTrace.hlsli"
#include "Transform.hlsli"
#include "Types.hlsli"

ConstantBuffer<PathTraceRootConstants> RootConstants : register(b0);
ConstantBuffer<Scene> Scene : register(b1);

[numthreads(16, 16, 1)]
void ComputeStart(uint32x3 dispatchThreadID : SV_DispatchThreadID)
{
	RWTexture2D<float32x3> hdrTexture = ResourceDescriptorHeap[RootConstants.HDRTextureIndex];
	const RaytracingAccelerationStructure accelerationStructure = ResourceDescriptorHeap[Scene.AccelerationStructureIndex];

	uint32x2 hdrTextureDimensions;
	hdrTexture.GetDimensions(hdrTextureDimensions.x, hdrTextureDimensions.y);

	if (any(dispatchThreadID.xy >= hdrTextureDimensions))
	{
		return;
	}

	const float32x3 rayPositionWS = TransformClipToWorld(TransformUVToClip(TransformTexelToUV(dispatchThreadID.xy, hdrTextureDimensions)), Scene.ClipToWorld);
	const float32x3 rayDirectionWS = normalize(rayPositionWS - Scene.ViewPositionWS);

	RayDesc ray;
	ray.Origin = Scene.ViewPositionWS;
	ray.TMin = 0.001f;
	ray.Direction = rayDirectionWS;
	ray.TMax = Infinity;

	const RAY_FLAG flags = RAY_FLAG_FORCE_OPAQUE;
	RayQuery<flags> query;
	query.TraceRayInline(accelerationStructure, flags, 0xff, ray);

	query.Proceed();

	float32x3 rgb = 0.0f;

	if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
	{
		rgb = ToRGB(Hash(query.CommittedInstanceID()));
	}

	hdrTexture[dispatchThreadID.xy] = rgb;
}
