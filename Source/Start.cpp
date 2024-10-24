#include "DrawText.hpp"

#include "RHI/GpuDevice.hpp"

#include "Luft/Platform.hpp"

static bool NeedsResize = false;

static void ResizeHandler(Platform::Window*)
{
	NeedsResize = true;
}

static void CreateWindowTextures(GpuDevice& device, TextureHandle* swapChainTextures, TextureHandle& depthTexture, uint32 width, uint32 height)
{
	for (usize i = 0; i < FramesInFlight; ++i)
	{
		swapChainTextures[i] = device.CreateTexture("SwapChain Render Target"_view, BarrierLayout::Undefined,
		{
			.Width = width,
			.Height = height,
			.Type = TextureType::Rectangle,
			.Format = TextureFormat::Rgba8Srgb,
			.RenderTarget = true,
		},
		device.GetSwapChainResource(i));
	}
	depthTexture = device.CreateTexture("Depth Buffer"_view, BarrierLayout::DepthStencilWrite,
	{
		.Width = width,
		.Height = height,
		.Type = TextureType::Rectangle,
		.Format = TextureFormat::Depth32,
	});
}

void Start()
{
	Platform::Window* window = Platform::MakeWindow("Hummingbird", 1920, 1080);

	GpuDevice device(window);
	GraphicsContext graphics = device.CreateGraphicsContext();

	TextureHandle swapChainTextures[FramesInFlight];
	TextureHandle depthTexture;
	CreateWindowTextures(device, swapChainTextures, depthTexture, window->DrawWidth, window->DrawHeight);

	DrawText::Get().Init(&device);

	Platform::ShowWindow(window);
	Platform::InstallResizeHandler(ResizeHandler);

	while (!Platform::IsQuitRequested())
	{
		Platform::ProcessEvents();

		if (IsKeyPressedOnce(Key::Escape))
		{
			break;
		}
		if (window->DrawWidth == 0 || window->DrawHeight == 0)
		{
			continue;
		}

		if (NeedsResize)
		{
			device.WaitForIdle();

			for (TextureHandle& texture : swapChainTextures)
			{
				device.DestroyTexture(texture);
			}
			device.DestroyTexture(depthTexture);
			device.ReleaseAllDeletes();

			device.ResizeSwapChain(window->DrawWidth, window->DrawHeight);
			CreateWindowTextures(device, swapChainTextures, depthTexture, window->DrawWidth, window->DrawHeight);

			NeedsResize = false;
		}

		const TextureHandle& frameTexture = swapChainTextures[device.GetFrameIndex()];

		graphics.Begin();

		graphics.SetViewport(frameTexture.GetWidth(), frameTexture.GetHeight());

		graphics.TextureBarrier
		(
			{ BarrierStage::None, BarrierStage::RenderTarget },
			{ BarrierAccess::NoAccess, BarrierAccess::RenderTarget },
			{ BarrierLayout::Undefined, BarrierLayout::RenderTarget },
			frameTexture
		);

		graphics.ClearRenderTarget(frameTexture, { 0.0f, 0.0f, 0.0f, 1.0f });
		graphics.ClearDepthStencil(depthTexture);

		graphics.SetRenderTarget(frameTexture, depthTexture);

		DrawText::Get().Draw("Hello, world!"_view, Float2 { 50.0f, 400.0f }, Float3 { 0.0f, 1.0f, 0.0f }, 256.0f);

		DrawText::Get().Submit(graphics, frameTexture.GetWidth(), frameTexture.GetHeight());

		graphics.TextureBarrier
		(
			{ BarrierStage::RenderTarget, BarrierStage::None },
			{ BarrierAccess::RenderTarget, BarrierAccess::NoAccess },
			{ BarrierLayout::RenderTarget, BarrierLayout::Present },
			frameTexture
		);

		graphics.End();

		device.Submit(graphics);
		device.Present();
	}

	DrawText::Get().Shutdown();

	for (TextureHandle& texture : swapChainTextures)
	{
		device.DestroyTexture(texture);
	}
	device.DestroyTexture(depthTexture);

	Platform::DestroyWindow(window);
}
