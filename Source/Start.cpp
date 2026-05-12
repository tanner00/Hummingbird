#include "CameraController.hpp"
#include "Editor.hpp"
#include "Renderer.hpp"

void Start()
{
	Platform::Window* window = Platform::CreateWindow("Hummingbird"_view, 1920, 1080);

	static bool needsResize = false;
	Platform::InstallResizeHandler([](Platform::Window*) -> void
	{
		needsResize = true;
	});

#if DEBUG
	const Array<String> arguments = Platform::GetCommandLineArguments();
	const bool validation = (arguments.GetCount() >= 1) ? (arguments.First() == "rhi-validation"_view) : false;
#else
	static constexpr bool validation = false;
#endif

	Renderer renderer(window, validation);
	CameraController cameraController;
	Editor editor(window, &renderer, &cameraController);

	Platform::ShowWindow(window, true);

	float64 timeLast = 0.0;

	while (!Platform::IsQuitRequested())
	{
		Platform::ProcessEvents();

		const bool setDefault = (Platform::IsKeyPressedOnce(Platform::Key::Escape) && Platform::GetInputMode() == Platform::InputMode::Captured) ||
								!Platform::IsWindowFocused(window);
		const bool quit = Platform::IsKeyPressedOnce(Platform::Key::Escape) && Platform::GetInputMode() == Platform::InputMode::Default;

		if (setDefault)
		{
			Platform::SetInputMode(window, Platform::InputMode::Default);
		}
		if (quit)
		{
			break;
		}

		if (window->DrawWidth == 0 || window->DrawHeight == 0)
		{
			continue;
		}

		if (needsResize)
		{
			renderer.ResizeSwapChain(window->DrawWidth, window->DrawHeight);
			needsResize = false;
		}

		const float64 timeNow = Platform::GetTime();
		const float64 timeDelta = timeNow - timeLast;
		timeLast = timeNow;

		cameraController.Update(static_cast<float32>(timeDelta));
		editor.Update();
		renderer.Update(cameraController, static_cast<float32>(timeDelta), timeNow);
	}

	Platform::DestroyWindow(window);
}
