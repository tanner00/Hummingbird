#pragma once

#include "Types.hlsli"

float CastShadowRay(float3 origin,
					float3 direction,
					float maxDistance,
					RaytracingAccelerationStructure accelerationStructure,
					ByteAddressBuffer vertexBuffer,
					StructuredBuffer<Primitive> primitiveBuffer,
					StructuredBuffer<Material> materialBuffer,
					SamplerState sampler)
{
	RayDesc ray;
	ray.Origin = origin;
	ray.TMin = 0.001f;
	ray.Direction = direction;
	ray.TMax = maxDistance;

	const RAY_FLAG opaqueFlags = RAY_FLAG_CULL_NON_OPAQUE;
	RayQuery<opaqueFlags> opaqueQuery;
	opaqueQuery.TraceRayInline(accelerationStructure, opaqueFlags, 0xFF, ray);

	opaqueQuery.Proceed();

	[branch]
	if (opaqueQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
	{
		return 0.0f;
	}

	const RAY_FLAG translucentFlags = RAY_FLAG_CULL_OPAQUE;
	RayQuery<translucentFlags> translucentQuery;
	translucentQuery.TraceRayInline(accelerationStructure, translucentFlags, 0xFF, ray);

	[loop]
	while (translucentQuery.Proceed())
	{
		[branch]
		if (translucentQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
		{
			translucentQuery.CommitNonOpaqueTriangleHit();

			const uint primitiveIndex = translucentQuery.CommittedInstanceID();
			const Primitive primitive = primitiveBuffer[primitiveIndex];
			const Material material = materialBuffer[primitive.MaterialIndex];

			const uint triangleIndex = translucentQuery.CommittedPrimitiveIndex();
			const uint triangleIndexOffset = triangleIndex * primitive.IndexStride * 3;

			uint indices[3];
			[branch]
			if (primitive.IndexStride == 2)
			{
				indices[0] = vertexBuffer.Load<uint16>(primitive.IndexOffset + triangleIndexOffset + primitive.IndexStride * 0);
				indices[1] = vertexBuffer.Load<uint16>(primitive.IndexOffset + triangleIndexOffset + primitive.IndexStride * 1);
				indices[2] = vertexBuffer.Load<uint16>(primitive.IndexOffset + triangleIndexOffset + primitive.IndexStride * 2);
			}
			else if (primitive.IndexStride == 4)
			{
				indices[0] = vertexBuffer.Load<uint>(primitive.IndexOffset + triangleIndexOffset + primitive.IndexStride * 0);
				indices[1] = vertexBuffer.Load<uint>(primitive.IndexOffset + triangleIndexOffset + primitive.IndexStride * 1);
				indices[2] = vertexBuffer.Load<uint>(primitive.IndexOffset + triangleIndexOffset + primitive.IndexStride * 2);
			}
			const float2 textureCoordinates[3] =
			{
				vertexBuffer.Load<float2>(primitive.TextureCoordinateOffset + indices[0] * primitive.TextureCoordinateStride),
				vertexBuffer.Load<float2>(primitive.TextureCoordinateOffset + indices[1] * primitive.TextureCoordinateStride),
				vertexBuffer.Load<float2>(primitive.TextureCoordinateOffset + indices[2] * primitive.TextureCoordinateStride),
			};

			const float2 barycentrics = translucentQuery.CommittedTriangleBarycentrics();
			const float3 weights = float3(1.0f - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
			const float2 textureCoordinate = textureCoordinates[0] * weights.x
										   + textureCoordinates[1] * weights.y
										   + textureCoordinates[2] * weights.z;

			const Texture2D<float4> baseColorOrDiffuseTexture = ResourceDescriptorHeap[material.BaseColorOrDiffuseTextureIndex];
			const float alpha = baseColorOrDiffuseTexture.SampleLevel(sampler, textureCoordinate, 0).a * material.BaseColorOrDiffuseFactor.a;

			[branch]
			if (alpha >= material.AlphaCutoff)
			{
				return 0.0f;
			}
		}
	}

	return 1.0f;
}
