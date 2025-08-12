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

	bool ShouldAntiAlias() const { return TemporalAntiAliasing.Enabled && ViewMode == HLSL::ViewMode::Lit; }

	RHI::Device Device;
	RHI::GraphicsContext Graphics;

	ReadTexture SwapChainTextures[RHI::FramesInFlight];
	ReadTexture DepthTexture;

	ReadTexture WhiteTexture;
	ReadTexture DefaultNormalMapTexture;

	RHI::Sampler PointClampSampler;
	RHI::Sampler LinearClampSampler;
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
	ReadBuffer SceneLuminanceBuffer;
	ReadBuffer SceneBuffers[RHI::FramesInFlight];

	RHI::Resource SceneAccelerationStructureResource;
	RHI::AccelerationStructure SceneAccelerationStructure;

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
