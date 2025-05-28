#define DEFERRED true

#include "Deferred.hlsli"
#include "Geometry.hlsli"
#include "Shade.hlsli"

ConstantBuffer<DeferredRootConstants> RootConstants : register(b0);
ConstantBuffer<Scene> Scene : register(b1);

[numthreads(16, 16, 1)]
void ComputeStart(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	RWTexture2D<float3> hdrTexture = ResourceDescriptorHeap[RootConstants.HDRTextureIndex];

	uint2 hdrTextureDimensions;
	hdrTexture.GetDimensions(hdrTextureDimensions.x, hdrTextureDimensions.y);

	[branch]
	if (any(dispatchThreadID.xy >= hdrTextureDimensions))
	{
		return;
	}

	const ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[Scene.VertexBufferIndex];
	const Texture2D<uint2> visibilityBufferTexture = ResourceDescriptorHeap[RootConstants.VisibilityBufferTextureIndex];

	const uint2 visibilityBuffer = visibilityBufferTexture.Load(uint3(dispatchThreadID.xy, 0));

	[branch]
	if (all(visibilityBuffer.xy == 0))
	{
		hdrTexture[dispatchThreadID.xy] = 0.0f;
		return;
	}
	const uint drawCallIndex = visibilityBuffer.x - 1;
	const uint triangleIndex = visibilityBuffer.y - 1;

	const StructuredBuffer<DrawCall> drawCallBuffer = ResourceDescriptorHeap[Scene.DrawCallBufferIndex];
	const DrawCall drawCall = drawCallBuffer[drawCallIndex];

	const StructuredBuffer<Node> nodeBuffer = ResourceDescriptorHeap[Scene.NodeBufferIndex];
	const Node node = nodeBuffer[drawCall.NodeIndex];

	const StructuredBuffer<Primitive> primitiveBuffer = ResourceDescriptorHeap[Scene.PrimitiveBufferIndex];
	const Primitive primitive = primitiveBuffer[drawCall.PrimitiveIndex];

	const uint triangleOffset = triangleIndex * primitive.IndexStride * 3;

	uint indices[3];
	float3 positions[3];
	float2 textureCoordinates[3];
	float3 normals[3];
	float4 tangents[3];
	LoadTriangleIndices(vertexBuffer, primitive, triangleOffset, indices);
	LoadTrianglePositions(vertexBuffer, primitive, indices, positions);
	LoadTriangleTextureCoordinates(vertexBuffer, primitive, indices, textureCoordinates);
	LoadTriangleNormals(vertexBuffer, primitive, indices, normals);
	LoadTriangleTangents(vertexBuffer, primitive, indices, tangents);

	const float4 worldSpacePositions[] =
	{
		mul(node.Transform, float4(positions[0], 1.0f)),
		mul(node.Transform, float4(positions[1], 1.0f)),
		mul(node.Transform, float4(positions[2], 1.0f)),
	};
	const float4 clipSpacePositions[] =
	{
		mul(Scene.ViewProjection, worldSpacePositions[0]),
		mul(Scene.ViewProjection, worldSpacePositions[1]),
		mul(Scene.ViewProjection, worldSpacePositions[2]),
	};

	float3 weights;
	float3 ddxWeights;
	float3 ddyWeights;
	CalculateScreenSpaceBarycentrics(clipSpacePositions, dispatchThreadID.xy + 0.5f, hdrTextureDimensions, weights, ddxWeights, ddyWeights);

	const float3 worldSpacePosition = LerpBarycentrics(weights, worldSpacePositions[0].xyz, worldSpacePositions[1].xyz, worldSpacePositions[2].xyz);
	const float2 textureCoordinate = LerpBarycentrics(weights, textureCoordinates[0], textureCoordinates[1], textureCoordinates[2]);
	const float3 normal = LerpBarycentrics(weights, normals[0], normals[1], normals[2]);
	const float4 tangent = float4(LerpBarycentrics(weights, tangents[0].xyz, tangents[1].xyz, tangents[2].xyz), tangents[0].w);

	const float2 ddxTextureCoordinate = LerpBarycentrics(ddxWeights, textureCoordinates[0], textureCoordinates[1], textureCoordinates[2]);
	const float2 ddyTextureCoordinate = LerpBarycentrics(ddyWeights, textureCoordinates[0], textureCoordinates[1], textureCoordinates[2]);

	ScenePixelInput pixel;
	pixel.ClipSpacePosition = 0.0f;
	pixel.WorldSpacePosition = worldSpacePosition;
	pixel.TextureCoordinate = textureCoordinate;
	pixel.Normal = mul((float3x3)node.NormalTransform, normal);
	pixel.Tangent = float4(mul((float3x3)node.NormalTransform, tangent.xyz), tangent.w);

	hdrTexture[dispatchThreadID.xy] = Shade(Scene,
											RootConstants.ViewMode,
											drawCall.PrimitiveIndex,
											pixel,
											ddxTextureCoordinate,
											ddyTextureCoordinate,
											RootConstants.AnisotropicWrapSamplerIndex,
											triangleIndex).rgb;
}
