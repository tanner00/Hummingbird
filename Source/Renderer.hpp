#pragma once

#include "RenderTypes.hpp"

class CameraController;

namespace GLTF
{
struct Scene;
}

class Renderer : public NoCopy
{
public:
	explicit Renderer(const Platform::Window* window);
	~Renderer();

	void Update(const CameraController& cameraController);

	void Resize(uint32 width, uint32 height);

	void SetScene(const GLTF::Scene& scene)
	{
		LoadScene(scene);
	}

private:
	void UpdateScene(const RHI::GraphicsPipeline& pipeline);

#if !RELEASE
	void UpdateFrameTimes(double startCpuTime);
#endif

	void LoadScene(const GLTF::Scene& scene);
	void UnloadScene();

	void CreatePipelines();
	void DestroyPipelines();

	void CreateScreenTextures(uint32 width, uint32 height);
	void DestroyScreenTextures();

	RHI::Device Device;
	RHI::GraphicsContext Graphics;

	BasicTexture SwapChainTextures[RHI::FramesInFlight];
	BasicTexture DepthTexture;

	BasicTexture WhiteTexture;
	BasicTexture DefaultNormalMapTexture;

	RHI::Sampler AnisotropicWrapSampler;
	RHI::Sampler PointClampSampler;

	RenderTarget HDRRenderTarget;
	RenderTarget VisibilityRenderTarget;

	BasicBuffer SceneVertexBuffer;
	BasicBuffer ScenePrimitiveBuffer;
	BasicBuffer SceneNodeBuffer;
	BasicBuffer SceneDrawCallBuffer;
	BasicBuffer SceneMaterialBuffer;
	BasicBuffer SceneDirectionalLightBuffer;
	BasicBuffer ScenePointLightsBuffer;
	BasicBuffer SceneLuminanceBuffer;
	BasicBuffer SceneBuffers[RHI::FramesInFlight];

	RHI::Resource SceneAccelerationStructureResource;
	RHI::AccelerationStructure SceneAccelerationStructure;

	Array<Mesh> SceneMeshes;
	Array<Node> SceneNodes;
	Array<Material> SceneMaterials;
	bool SceneTwoChannelNormalMaps;

	RHI::GraphicsPipeline VisibilityPipeline;
	RHI::ComputePipeline DeferredPipeline;

	RHI::ComputePipeline LuminanceHistogramPipeline;
	RHI::ComputePipeline LuminanceAveragePipeline;

	RHI::GraphicsPipeline ToneMapPipeline;

	HLSL::ViewMode ViewMode;

#if !RELEASE
	double AverageCpuTime;
	double AverageGpuTime;
#endif
};
