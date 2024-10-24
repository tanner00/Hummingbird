#include "Renderer.hpp"

#include "Luft/Platform.hpp"

static bool NeedsResize = false;

static void ResizeHandler(Platform::Window*)
{
	NeedsResize = true;
}

void Start()
{
	Platform::Window* window = Platform::MakeWindow("Hummingbird", 1920, 1080);

	Renderer renderer(window);

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
			renderer.Resize(window->DrawWidth, window->DrawHeight);
			NeedsResize = false;
		}

		renderer.Update();
	}

	Platform::DestroyWindow(window);
}
