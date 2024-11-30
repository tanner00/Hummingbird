#pragma once

#include "GLTF.hpp"
#include "RenderTypes.hpp"

#include "RHI/RHI.hpp"

#include "Luft/NoCopy.hpp"

class Renderer : public NoCopy
{
public:
	explicit Renderer(const Platform::Window* window);
	~Renderer();

	void Update();

	void Resize(uint32 width, uint32 height);

private:
#if !RELEASE
	void UpdateFrameTimes(double startCpuTime);
#endif

	void LoadScene(usize sceneIndex);
	void UnloadScene();

	void CreateScreenTextures(uint32 width, uint32 height);
	void DestroyScreenTextures();

	GpuDevice Device;
	GraphicsContext Graphics;

	Texture SwapChainTextures[FramesInFlight];
	Texture DepthTexture;

	Texture DefaultTexture;
	Sampler DefaultSampler;

	Buffer SceneVertexBuffer;
	Buffer InstanceSceneBuffer;
	Buffer InstanceNodeBuffer;

	Array<Mesh> SceneMeshes;
	Array<Material> SceneMaterials;
	Array<Node> SceneNodes;

	GltfCamera SceneCamera;

	GraphicsPipeline SceneOpaquePipeline;
	GraphicsPipeline SceneBlendPipeline;

#if !RELEASE
	double AverageCpuTime;
	double AverageGpuTime;
#endif
};
