#pragma once

#include "RenderTypes.hpp"

#include "Luft/NoCopy.hpp"

struct GltfScene;
class CameraController;

class Renderer : public NoCopy
{
public:
	explicit Renderer(const Platform::Window* window);
	~Renderer();

	void Update(const CameraController& cameraController);

	void Resize(uint32 width, uint32 height);

	void SetScene(const GltfScene& scene)
	{
		LoadScene(scene);
	}

private:
#if !RELEASE
	void UpdateFrameTimes(double startCpuTime);
#endif

	void LoadScene(const GltfScene& scene);
	void UnloadScene();

	void CreatePipelines();
	void DestroyPipelines();

	void CreateScreenTextures(uint32 width, uint32 height);
	void DestroyScreenTextures();

	RHI::Device Device;
	RHI::GraphicsContext Graphics;

	Texture SwapChainTextures[RHI::FramesInFlight];
	Texture DepthTexture;
	Texture WhiteTexture;
	Texture DefaultNormalMapTexture;

	Buffer SceneBuffers[RHI::FramesInFlight];
	Buffer SceneVertexBuffer;
	Buffer SceneNodeBuffer;
	Buffer SceneMaterialBuffer;
	Buffer SceneDirectionalLightBuffer;
	Buffer ScenePointLightsBuffer;
	Buffer SceneLuminanceBuffer;

	RHI::Resource HdrTexture;
	RHI::TextureView HdrTextureRenderTargetView;
	RHI::TextureView HdrTextureShaderResourceView;

	RHI::Sampler DefaultSampler;

	RHI::GraphicsPipeline SceneOpaquePipeline;
	RHI::GraphicsPipeline SceneBlendPipeline;

	RHI::ComputePipeline LuminanceHistogramPipeline;
	RHI::ComputePipeline LuminanceAveragePipeline;

	RHI::GraphicsPipeline ToneMapPipeline;

	Array<Mesh> SceneMeshes;
	Array<Node> SceneNodes;
	Array<Material> SceneMaterials;

#if !RELEASE
	double AverageCpuTime;
	double AverageGpuTime;
#endif
};
