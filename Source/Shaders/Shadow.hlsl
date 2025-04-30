float CastShadowRay(RaytracingAccelerationStructure accelerationStructure, float3 origin, float3 direction, float maxDistance)
{
	RayDesc ray;
	ray.Origin = origin;
	ray.TMin = 0.001f;
	ray.Direction = direction;
	ray.TMax = maxDistance;

	const RAY_FLAG rayFlags = RAY_FLAG_FORCE_OPAQUE;

	RayQuery<rayFlags> rayQuery;
	rayQuery.TraceRayInline(accelerationStructure, rayFlags, 0xFF, ray);
	rayQuery.Proceed();

	const bool hit = rayQuery.CommittedStatus() != COMMITTED_NOTHING;
	const float shadowFactor = hit ? 0.0f : 1.0f;
	return shadowFactor;
}
