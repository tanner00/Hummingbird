#pragma once

#include "RenderTypes.hpp"

class CameraController;
class Editor;

namespace GLTF
{
struct Scene;
}

class Renderer : public NoCopy
{
public:
	Renderer(Platform::Window* window, bool validation);
	~Renderer();

	void Update(const CameraController& cameraController, float32 timeDelta, float64 frameStartCPUTime);

	void ResizeSwapChain(uint32 width, uint32 height);
	void ResizeViewport(uint32 width, uint32 height);

	void SetScene(const GLTF::Scene& scene)
	{
		LoadScene(scene);
	}

private:
	void UpdateViewport(const CameraController& cameraController);
	void UpdateRasterization(const Matrix& worldToClip);
	void UpdatePathTracing();

#if !RELEASE
	void UpdateFrameTimes(float64 frameStartTimeCPU);
#endif

	void LoadScene(const GLTF::Scene& scene);
	void UnloadScene();

	void CreatePipelines();
	void DestroyPipelines();
	void RecreatePipelines();

	void CreateSwapChainTextures(uint32 width, uint32 height);
	void DestroySwapChainTextures();

	void CreateViewportTextures(uint32 width, uint32 height);
	void DestroyViewportTextures();

	bool ShouldAntiAlias() const { return TemporalAntiAliasing.Enabled && ViewMode == HLSL::ViewMode::Lit && !PathTrace; }

	Array<Mesh> SceneMeshes;
	Array<Node> SceneNodes;
	Array<Material> SceneMaterials;
	bool SceneTwoChannelNormalMaps;

	HLSL::ViewMode ViewMode;

	struct
	{
		bool Enabled;
		bool DiscardPreviousFrame;

		Matrix PreviousWorldToClip;

		uint32 FrameCount;
	} TemporalAntiAliasing;

	bool PathTrace;

#if !RELEASE
	float64 AverageTimeCPU;
	float64 AverageTimeGPU;
#endif

	RHI::Resource SwapChainTextureResources[RHI::FramesInFlight];
	RHI::TextureView SwapChainTextureViews[RHI::FramesInFlight];

	RHI::Resource DepthTextureResource;
	RHI::TextureView DepthTextureView;

	ReadTexture WhiteTexture;
	ReadTexture DefaultNormalMapTexture;

	RHI::Sampler AnisotropicWrapSampler;

	RHI::Resource VisibilityTextureResource;
	RHI::TextureView VisibilityTextureRenderTargetView;
	RHI::TextureView VisibilityTextureShaderResourceView;

	WriteTexture HDRTexture;
	WriteTexture AccumulationTexture;
	WriteTexture PreviousAccumulationTexture;

	RHI::Resource FinalTextureResource;
	RHI::TextureView FinalTextureRenderTargetView;
	RHI::TextureView FinalTextureShaderResourceView;

	ReadBuffer SceneVertexBuffer;
	ReadBuffer ScenePrimitiveBuffer;
	ReadBuffer SceneNodeBuffer;
	ReadBuffer SceneDrawCallBuffer;
	ReadBuffer SceneMaterialBuffer;
	ReadBuffer SceneDirectionalLightBuffer;
	ReadBuffer ScenePointLightsBuffer;

	RHI::Resource SceneBufferResources[RHI::FramesInFlight];
	RHI::BufferView SceneBufferViews[RHI::FramesInFlight];

	RHI::Resource SceneLuminanceBufferResource;
	RHI::BufferView SceneLuminanceBufferView;

	RHI::Resource SceneAccelerationStructureResource;
	RHI::RayTracingAccelerationStructure SceneAccelerationStructure;

	RHI::GraphicsPipeline VisibilityPipeline;
	RHI::GraphicsPipeline VisibilityDoubleSidedPipeline;
	RHI::ComputePipeline DeferredPipeline;

	RHI::ComputePipeline ResolvePipeline;

	RHI::ComputePipeline LuminanceHistogramPipeline;
	RHI::ComputePipeline LuminanceAveragePipeline;

	RHI::GraphicsPipeline ToneMapPipeline;

	RHI::ComputePipeline PathTracePipeline;

	friend Editor;
};
