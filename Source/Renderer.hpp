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
	Renderer(Platform::Window* window, bool validation);
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
	void UpdateFrameTimes(double startCPUTime);
#endif

	void LoadScene(const GLTF::Scene& scene);
	void UnloadScene();

	void CreatePipelines();
	void DestroyPipelines();

	void CreateScreenTextures(uint32 width, uint32 height);
	void DestroyScreenTextures();

	bool ShouldAntiAlias() const { return TemporalAntiAliasing.Enabled && ViewMode == HLSL::ViewMode::Lit; }

	RHI::Resource SwapChainTextureResources[RHI::FramesInFlight];
	RHI::TextureView SwapChainTextureViews[RHI::FramesInFlight];

	RHI::Resource DepthTextureResource;
	RHI::TextureView DepthTextureView;

	ReadTexture WhiteTexture;
	ReadTexture DefaultNormalMapTexture;

	RHI::Sampler AnisotropicWrapSampler;

	RenderTarget VisibilityRenderTarget;

	WriteTexture HDRTexture;
	WriteTexture AccumulationTexture;
	WriteTexture PreviousAccumulationTexture;

	ReadBuffer SceneVertexBuffer;
	ReadBuffer ScenePrimitiveBuffer;
	ReadBuffer SceneNodeBuffer;
	ReadBuffer SceneDrawCallBuffer;
	ReadBuffer SceneMaterialBuffer;
	ReadBuffer SceneDirectionalLightBuffer;
	ReadBuffer ScenePointLightsBuffer;

	RHI::Resource SceneBuffers[RHI::FramesInFlight];
	RHI::BufferView SceneBufferViews[RHI::FramesInFlight];

	RHI::Resource SceneLuminanceBuffer;
	RHI::BufferView SceneLuminanceBufferView;

	RHI::Resource SceneAccelerationStructureResource;
	RHI::RayTracingAccelerationStructure SceneAccelerationStructure;

	Array<Mesh> SceneMeshes;
	Array<Node> SceneNodes;
	Array<Material> SceneMaterials;
	bool SceneTwoChannelNormalMaps;

	RHI::GraphicsPipeline VisibilityPipeline;
	RHI::ComputePipeline DeferredPipeline;

	RHI::ComputePipeline ResolvePipeline;

	RHI::ComputePipeline LuminanceHistogramPipeline;
	RHI::ComputePipeline LuminanceAveragePipeline;

	RHI::GraphicsPipeline ToneMapPipeline;

	HLSL::ViewMode ViewMode;

	struct TemporalAntiAliasing
	{
		bool Enabled;
		bool DiscardPreviousFrame;

		Matrix PreviousWorldToClip;

		uint32 FrameCount;
	} TemporalAntiAliasing;

#if !RELEASE
	double AverageCpuTime;
	double AverageGpuTime;
#endif
};
