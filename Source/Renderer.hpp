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

	void CreateScreenTextures(uint32 width, uint32 height);
	void DestroyScreenTextures();

	GpuDevice Device;
	GraphicsContext Graphics;

	Texture SwapChainTextures[FramesInFlight];
	Texture DepthTexture;

	Buffer SceneVertexBuffer;
	Buffer InstanceSceneBuffer;
	Buffer InstanceNodeBuffer;

	Array<Mesh> SceneMeshes;
	Array<Node> SceneNodes;

	GltfCamera SceneCamera;

	GraphicsPipeline ScenePipeline;

#if !RELEASE
	double AverageCpuTime;
	double AverageGpuTime;
#endif
};
