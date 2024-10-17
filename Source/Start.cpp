#include "Luft/Platform.hpp"

void Start()
{
	Platform::Window* window = Platform::MakeWindow("Hummingbird", 1920, 1080);
	Platform::ShowWindow(window);

	while (!Platform::IsQuitRequested())
	{
		Platform::ProcessEvents();
	}

	Platform::DestroyWindow(window);
}
