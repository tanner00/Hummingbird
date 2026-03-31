#pragma once

#include "Geometry.hlsli"
#include "Types.hlsli"

float CastShadowRay(float3 originWS,
					float3 directionWS,
					float maxDistance,
					RaytracingAccelerationStructure accelerationStructure,
					ByteAddressBuffer vertexBuffer,
					StructuredBuffer<Primitive> primitiveBuffer,
					StructuredBuffer<Material> materialBuffer,
					SamplerState sampler)
{
	RayDesc ray;
	ray.Origin = originWS;
	ray.TMin = 0.001f;
	ray.Direction = directionWS;
	ray.TMax = maxDistance;

	const RAY_FLAG flags = RAY_FLAG_NONE;
	RayQuery<flags> query;
	query.TraceRayInline(accelerationStructure, flags, 0xFF, ray);

	while (query.Proceed())
	{
		const uint primitiveIndex = query.CandidateInstanceID();
		const Primitive primitive = primitiveBuffer[primitiveIndex];

		const uint triangleIndex = query.CandidatePrimitiveIndex();
		const uint triangleOffset = triangleIndex * primitive.IndexStride * 3;

		uint indices[3];
		float2 uvs[3];
		LoadTriangleIndices(vertexBuffer, primitive, triangleOffset, indices);
		LoadTriangleTextureCoordinates(vertexBuffer, primitive, indices, uvs);

		const float2 barycentrics = query.CandidateTriangleBarycentrics();
		const float3 weights = float3(1.0f - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
		const float2 uv = uvs[0] * weights.x
						+ uvs[1] * weights.y
						+ uvs[2] * weights.z;

		const Material material = materialBuffer[primitive.MaterialIndex];
		const Texture2D<float4> baseColorOrDiffuseTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.BaseColorOrDiffuseTextureIndex)];
		const float alpha = baseColorOrDiffuseTexture.SampleLevel(sampler, uv, 0).a * material.BaseColorOrDiffuseFactor.a;

		if (alpha >= material.AlphaCutoff)
		{
			return 0.0f;
		}
	}

	if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
	{
		return 0.0f;
	}

	return 1.0f;
}
