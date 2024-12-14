#pragma once

#include "GLTF.hpp"
#include "RenderTypes.hpp"

#include "RHI/RHI.hpp"

#include "Luft/NoCopy.hpp"

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

	Texture WhiteTexture;
	Sampler DefaultSampler;

	Buffer SceneVertexBuffer;
	Buffer InstanceSceneBuffer;
	Buffer InstanceNodeBuffer;

	Array<Mesh> SceneMeshes;
	Array<Material> SceneMaterials;
	Array<Node> SceneNodes;

	GraphicsPipeline SceneOpaquePipeline;
	GraphicsPipeline SceneBlendPipeline;

#if !RELEASE
	double AverageCpuTime;
	double AverageGpuTime;
#endif
};
