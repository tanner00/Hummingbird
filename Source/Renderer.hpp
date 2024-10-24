#pragma once

#include "Luft/NoCopy.hpp"

#include "RHI/GpuDevice.hpp"

class Renderer : public NoCopy
{
public:
	explicit Renderer(const Platform::Window* window);
	~Renderer();

	void Update();

	void Resize(uint32 width, uint32 height);

private:
	void CreateScreenTextures(uint32 width, uint32 height);
	void DestroyScreenTextures();

	GpuDevice Device;
	GraphicsContext Graphics;

	TextureHandle SwapChainTextures[FramesInFlight];
	TextureHandle DepthTexture;
};
