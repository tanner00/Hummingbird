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

	GpuDevice Device;
	GraphicsContext Graphics;

	Texture SwapChainTextures[FramesInFlight];
	Texture DepthTexture;

	Texture HdrTexture;

	Texture WhiteTexture;
	Texture DefaultNormalMapTexture;
	Sampler DefaultSampler;

	Buffer SceneBuffer;
	Buffer SceneVertexBuffer;
	Buffer SceneNodeBuffer;
	Buffer SceneMaterialBuffer;
	Buffer SceneDirectionalLightBuffer;
	Buffer ScenePointLightsBuffer;
	Buffer LuminanceBuffer;

	Array<Mesh> SceneMeshes;
	Array<Node> SceneNodes;
	Array<Material> SceneMaterials;

	GraphicsPipeline SceneOpaquePipeline;
	GraphicsPipeline SceneBlendPipeline;

	ComputePipeline LuminanceHistogramPipeline;
	ComputePipeline LuminanceAveragePipeline;

	GraphicsPipeline ToneMapPipeline;

#if !RELEASE
	double AverageCpuTime;
	double AverageGpuTime;
#endif
};
