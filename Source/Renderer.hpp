#pragma once

#include "RenderTypes.hpp"

#include "Luft/NoCopy.hpp"

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
	void UpdateScene(const RHI::GraphicsPipeline& opaquePipeline, const RHI::GraphicsPipeline& blendPipeline);

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

	BasicBuffer SceneBuffers[RHI::FramesInFlight];
	BasicBuffer SceneVertexBuffer;
	BasicBuffer ScenePrimitiveBuffer;
	BasicBuffer SceneNodeBuffer;
	BasicBuffer SceneMaterialBuffer;
	BasicBuffer SceneDirectionalLightBuffer;
	BasicBuffer ScenePointLightsBuffer;
	BasicBuffer SceneLuminanceBuffer;

	RHI::Resource SceneAccelerationStructureResource;
	RHI::AccelerationStructure SceneAccelerationStructure;

	RHI::Resource HdrTexture;
	RHI::TextureView HdrTextureRenderTargetView;
	RHI::TextureView HdrTextureShaderResourceView;

	RHI::Sampler DefaultSampler;

	RHI::GraphicsPipeline SceneOpaquePipeline;
	RHI::GraphicsPipeline SceneBlendPipeline;
	RHI::GraphicsPipeline SceneDepthPrePassPipeline;

	RHI::ComputePipeline LuminanceHistogramPipeline;
	RHI::ComputePipeline LuminanceAveragePipeline;

	RHI::GraphicsPipeline ToneMapPipeline;

	Array<Mesh> SceneMeshes;
	Array<Node> SceneNodes;
	Array<Material> SceneMaterials;

	ViewMode ViewMode;

#if !RELEASE
	double AverageCpuTime;
	double AverageGpuTime;
#endif
};
