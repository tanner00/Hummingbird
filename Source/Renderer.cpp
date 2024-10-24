#include "Renderer.hpp"
#include "DrawText.hpp"

Renderer::Renderer(const Platform::Window* window)
	: Device(window)
	, Graphics(Device.CreateGraphicsContext())
{
	CreateScreenTextures(window->DrawWidth, window->DrawHeight);

	DrawText::Get().Init(&Device);
}

Renderer::~Renderer()
{
	DrawText::Get().Shutdown();

	DestroyScreenTextures();
}

void Renderer::Update()
{
	const TextureHandle& frameTexture = SwapChainTextures[Device.GetFrameIndex()];

	Graphics.Begin();

	Graphics.SetViewport(frameTexture.GetWidth(), frameTexture.GetHeight());

	Graphics.TextureBarrier
	(
		{ BarrierStage::None, BarrierStage::RenderTarget },
		{ BarrierAccess::NoAccess, BarrierAccess::RenderTarget },
		{ BarrierLayout::Undefined, BarrierLayout::RenderTarget },
		frameTexture
	);

	Graphics.ClearRenderTarget(frameTexture, { 0.0f, 0.0f, 0.0f, 1.0f });
	Graphics.ClearDepthStencil(DepthTexture);

	Graphics.SetRenderTarget(frameTexture, DepthTexture);

	DrawText::Get().Submit(Graphics, frameTexture.GetWidth(), frameTexture.GetHeight());

	Graphics.TextureBarrier
	(
		{ BarrierStage::RenderTarget, BarrierStage::None },
		{ BarrierAccess::RenderTarget, BarrierAccess::NoAccess },
		{ BarrierLayout::RenderTarget, BarrierLayout::Present },
		frameTexture
	);

	Graphics.End();

	Device.Submit(Graphics);
	Device.Present();
}

void Renderer::Resize(uint32 width, uint32 height)
{
	Device.WaitForIdle();

	DestroyScreenTextures();
	Device.ReleaseAllDeletes();

	Device.ResizeSwapChain(width, height);
	CreateScreenTextures(width, height);
}

void Renderer::CreateScreenTextures(uint32 width, uint32 height)
{
	for (usize i = 0; i < FramesInFlight; ++i)
	{
		SwapChainTextures[i] = Device.CreateTexture("SwapChain Render Target"_view, BarrierLayout::Undefined,
		{
			.Width = width,
			.Height = height,
			.Type = TextureType::Rectangle,
			.Format = TextureFormat::Rgba8Srgb,
			.RenderTarget = true,
		},
		Device.GetSwapChainResource(i));
	}
	DepthTexture = Device.CreateTexture("Depth Buffer"_view, BarrierLayout::DepthStencilWrite,
	{
		.Width = width,
		.Height = height,
		.Type = TextureType::Rectangle,
		.Format = TextureFormat::Depth32,
	});
}

void Renderer::DestroyScreenTextures()
{
	for (TextureHandle& texture : SwapChainTextures)
	{
		Device.DestroyTexture(texture);
	}
	Device.DestroyTexture(DepthTexture);
}
