#pragma once

#include "Barycentrics.hlsli"
#include "Geometry.hlsli"
#include "Samplers.hlsli"
#include "Types.hlsli"

float32 CastShadowRay(float32x3 originWS,
					  float32x3 directionWS,
					  float32 maxDistance,
					  RaytracingAccelerationStructure accelerationStructure,
					  ByteAddressBuffer vertexBuffer,
					  StructuredBuffer<Primitive> primitiveBuffer,
					  StructuredBuffer<Material> materialBuffer)
{
	RayDesc rayDescription;
	rayDescription.Origin = originWS;
	rayDescription.TMin = 0.001f;
	rayDescription.Direction = directionWS;
	rayDescription.TMax = maxDistance;

	const RAY_FLAG flags = RAY_FLAG_NONE;
	RayQuery<flags> query;
	query.TraceRayInline(accelerationStructure, flags, 0xff, rayDescription);

	while (query.Proceed())
	{
		const uint32 primitiveIndex = query.CandidateInstanceID();
		const Primitive primitive = primitiveBuffer[primitiveIndex];

		const uint32 triangleIndex = query.CandidatePrimitiveIndex();
		const uint32 triangleOffset = triangleIndex * primitive.IndexStride * 3;

		uint32 indices[3];
		float32x2 uvs[3];
		LoadTriangleIndices(vertexBuffer, primitive, triangleOffset, indices);
		LoadTriangleTextureCoordinates(vertexBuffer, primitive, indices, uvs);

		const float32x2 barycentrics = query.CandidateTriangleBarycentrics();
		const float32x3 weights = float32x3(1.0f - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
		const float32x2 uv = LerpBarycentrics(weights, uvs[0], uvs[1], uvs[2]);

		const Material material = materialBuffer[primitive.MaterialIndex];
		const Texture2D<float32x4> baseColorOrDiffuseTexture = ResourceDescriptorHeap[NonUniformResourceIndex(material.BaseColorOrDiffuseTextureIndex)];
		const float32 alpha = baseColorOrDiffuseTexture.SampleLevel(GetAnisotropicWrapSampler(), uv, 0).a * material.BaseColorOrDiffuseFactor.a;

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
