#include "Barycentrics.hlsli"
#include "Geometry.hlsli"
#include "Math.hlsli"
#include "PBR.hlsli"
#include "Shadow.hlsli"
#include "Surface.hlsli"
#include "Transform.hlsli"
#include "ViewMode.hlsli"

ConstantBuffer<DeferredRootConstants> RootConstants : register(b0);
ConstantBuffer<Scene> Scene : register(b1);

[numthreads(16, 16, 1)]
void ComputeStart(uint32x3 dispatchThreadID : SV_DispatchThreadID)
{
	RWTexture2D<float32x3> hdrTexture = ResourceDescriptorHeap[RootConstants.HDRTextureIndex];

	const Texture2D<uint32x2> visibilityTexture = ResourceDescriptorHeap[RootConstants.VisibilityTextureIndex];
	const ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[Scene.VertexBufferIndex];
	const StructuredBuffer<Primitive> primitiveBuffer = ResourceDescriptorHeap[Scene.PrimitiveBufferIndex];
	const StructuredBuffer<Node> nodeBuffer = ResourceDescriptorHeap[Scene.NodeBufferIndex];
	const StructuredBuffer<Material> materialBuffer = ResourceDescriptorHeap[Scene.MaterialBufferIndex];
	const StructuredBuffer<DrawCall> drawCallBuffer = ResourceDescriptorHeap[Scene.DrawCallBufferIndex];
	const ConstantBuffer<DirectionalLight> directionalLightBuffer = ResourceDescriptorHeap[Scene.DirectionalLightBufferIndex];
	const StructuredBuffer<PointLight> pointLightsBuffer = ResourceDescriptorHeap[Scene.PointLightsBufferIndex];
	const RaytracingAccelerationStructure accelerationStructure = ResourceDescriptorHeap[Scene.AccelerationStructureIndex];

	uint32x2 hdrTextureDimensions;
	hdrTexture.GetDimensions(hdrTextureDimensions.x, hdrTextureDimensions.y);

	if (any(dispatchThreadID.xy >= hdrTextureDimensions))
	{
		return;
	}

	const uint32x2 visibility = visibilityTexture.Load(uint32x3(dispatchThreadID.xy, 0));

	if (all(visibility.xy == 0))
	{
		hdrTexture[dispatchThreadID.xy] = 0.0f;
		return;
	}
	const uint32 drawCallIndex = visibility.x - 1;
	const uint32 triangleIndex = visibility.y - 1;

	const DrawCall drawCall = drawCallBuffer[drawCallIndex];
	const Node node = nodeBuffer[drawCall.NodeIndex];
	const Primitive primitive = primitiveBuffer[drawCall.PrimitiveIndex];
	const Material material = materialBuffer[primitive.MaterialIndex];

	const uint32 triangleOffset = triangleIndex * primitive.IndexStride * 3;

	uint32 indices[3];
	float32x3 positionsLS[3];
	float32x2 uvs[3];
	float32x3 normalsLS[3];
	LoadTriangleIndices(vertexBuffer, primitive, triangleOffset, indices);
	LoadTrianglePositions(vertexBuffer, primitive, indices, positionsLS);
	LoadTriangleTextureCoordinates(vertexBuffer, primitive, indices, uvs);
	LoadTriangleNormals(vertexBuffer, primitive, indices, normalsLS);

	const float32x4 positionsWS[] =
	{
		TransformLocalPositionToWorld(positionsLS[0], node.LocalToWorld),
		TransformLocalPositionToWorld(positionsLS[1], node.LocalToWorld),
		TransformLocalPositionToWorld(positionsLS[2], node.LocalToWorld),
	};
	const float32x4 positionsCS[] =
	{
		TransformWorldToClip(positionsWS[0], Scene.WorldToClip),
		TransformWorldToClip(positionsWS[1], Scene.WorldToClip),
		TransformWorldToClip(positionsWS[2], Scene.WorldToClip),
	};
	const float32x4 jitteredPositionsCS[] =
	{
		TransformWorldToClip(positionsWS[0], Scene.JitterWorldToClip),
		TransformWorldToClip(positionsWS[1], Scene.JitterWorldToClip),
		TransformWorldToClip(positionsWS[2], Scene.JitterWorldToClip),
	};

	float32x3 weights;
	float32x3 ddxWeights;
	float32x3 ddyWeights;
	CalculateScreenBarycentrics(positionsCS, dispatchThreadID.xy + 0.5f, hdrTextureDimensions, weights, ddxWeights, ddyWeights);

	const float32x2 uv = LerpBarycentrics(weights, uvs[0], uvs[1], uvs[2]);

	const float32x2 ddxUV = LerpBarycentrics(ddxWeights, uvs[0], uvs[1], uvs[2]);
	const float32x2 ddyUV = LerpBarycentrics(ddyWeights, uvs[0], uvs[1], uvs[2]);

	float32x3 jitteredWeights;
	float32x3 ddxJitteredWeights;
	float32x3 ddyJitteredWeights;
	CalculateScreenBarycentrics(jitteredPositionsCS, dispatchThreadID.xy + 0.5f, hdrTextureDimensions, jitteredWeights, ddxJitteredWeights, ddyJitteredWeights);

	const float32x3 positionWS = LerpBarycentrics(jitteredWeights, positionsWS[0].xyz, positionsWS[1].xyz, positionsWS[2].xyz);
	const float32x3 normalLS = LerpBarycentrics(jitteredWeights, normalsLS[0], normalsLS[1], normalsLS[2]);

	const float32x3 ddxPositionWS = LerpBarycentrics(ddxJitteredWeights, positionsWS[0].xyz, positionsWS[1].xyz, positionsWS[2].xyz);
	const float32x3 ddyPositionWS = LerpBarycentrics(ddyJitteredWeights, positionsWS[0].xyz, positionsWS[1].xyz, positionsWS[2].xyz);

	const Surface surface = EvaluateSurface(material,
											Scene.TwoChannelNormalMaps,
											ddxPositionWS,
											ddyPositionWS,
											uv,
											ddxUV,
											ddyUV,
											normalize(TransformLocalDirectionToWorld(normalLS, node.NormalLocalToWorld)));

	float32x3 viewModeRGB;
	if (CheckViewMode(RootConstants.ViewMode, surface, triangleIndex, viewModeRGB))
	{
		hdrTexture[dispatchThreadID.xy] = viewModeRGB;
		return;
	}

	const float32x3 viewDirectionWS = normalize(Scene.ViewPositionWS - positionWS);

	float32x3 pointLightLuminanceRGB = 0.0f;

	for (uint32 pointLightIndex = 0; pointLightIndex < Scene.PointLightsCount; ++pointLightIndex)
	{
		const PointLight pointLight = pointLightsBuffer[pointLightIndex];

		const float32x3 pointLightDirectionWS = normalize(pointLight.PositionWS - positionWS);

		const float32 objectToLightDistance = distance(pointLight.PositionWS, positionWS);
		const float32 attenuation = 1.0f / (objectToLightDistance * objectToLightDistance);

		pointLightLuminanceRGB += PBR(surface,
									  viewDirectionWS,
									  pointLightDirectionWS,
									  attenuation * pointLight.IntensityCandela * pointLight.RGB) *
								  CastShadowRay(positionWS,
								  				pointLightDirectionWS,
												objectToLightDistance,
												accelerationStructure,
												vertexBuffer,
												primitiveBuffer,
												materialBuffer);
	}

	const float32x3 directionalLightDirectionWS = normalize(directionalLightBuffer.DirectionWS);

	const float32x3 directionalLightIlluminanceRGB = directionalLightBuffer.IntensityLux * directionalLightBuffer.RGB;

	const float32x3 directionalLightLuminanceRGB = PBR(surface,
													   viewDirectionWS,
													   directionalLightDirectionWS,
													   directionalLightIlluminanceRGB) *
												   CastShadowRay(positionWS,
												   				 directionalLightDirectionWS,
												   				 Infinity,
												   				 accelerationStructure,
												   				 vertexBuffer,
												   				 primitiveBuffer,
												   				 materialBuffer);

	const float32x3 skyFacingAmbientIlluminanceRGB = 0.15f * directionalLightIlluminanceRGB;
	const float32x3 groundFacingAmbientIlluminanceRGB = 0.3f * skyFacingAmbientIlluminanceRGB;
	const float32x3 ambientIlluminanceRGB = lerp(groundFacingAmbientIlluminanceRGB, skyFacingAmbientIlluminanceRGB, surface.ShadeNormalWS.y * 0.5f + 0.5f);

	const float32x3 ambientLuminanceRGB = ambientIlluminanceRGB * BRDFLambertianDiffuse(DiffuseReflectance(surface));

	hdrTexture[dispatchThreadID.xy] = pointLightLuminanceRGB + directionalLightLuminanceRGB + ambientLuminanceRGB + surface.EmissiveRGB;
}
