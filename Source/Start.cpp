#include "CameraController.hpp"
#include "Renderer.hpp"

#include "Luft/Platform.hpp"

static bool NeedsResize = false;

static const StringView scenes[] =
{
	"Resources/GLTF/Sponza/Sponza.gltf"_view,
	"Resources/GLTF/Bistro/Bistro.gltf"_view,
};

static void ResizeHandler(Platform::Window*)
{
	NeedsResize = true;
}

void Start()
{
	const auto setScene = [](usize sceneIndex, Renderer* renderer, CameraController* camera)
	{
		const double start = Platform::GetTime();

		GltfScene scene = LoadGltfScene(scenes[sceneIndex]);

		CHECK(!scene.DefaultCamera);
		camera->SetScene(scene);
		renderer->SetScene(scene);

		UnloadGltfScene(&scene);

		const double end = Platform::GetTime();
		Platform::LogFormatted("Scene took %.2fs to load\n", end - start);
	};

	Platform::Window* window = Platform::MakeWindow("Hummingbird", 1920, 1080);

	Renderer renderer(window);
	CameraController camera;
	setScene(0, &renderer, &camera);

	Platform::ShowWindow(window);
	Platform::InstallResizeHandler(ResizeHandler);

	float timeLast = 0.0f;

	while (!Platform::IsQuitRequested())
	{
		Platform::ProcessEvents();

		const bool setCaptured = IsMouseButtonPressedOnce(MouseButton::Left);
		const bool setDefault = (IsKeyPressedOnce(Key::Escape) && Platform::GetInputMode() == InputMode::Captured) ||
								!Platform::IsWindowFocused(window);
		const bool quit = IsKeyPressedOnce(Key::Escape) && Platform::GetInputMode() == InputMode::Default;

		if (setCaptured)
		{
			Platform::SetInputMode(window, InputMode::Captured);
		}
		else if (setDefault)
		{
			Platform::SetInputMode(window, InputMode::Default);
		}
		else if (quit)
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

		for (usize i = 0; i < ARRAY_COUNT(scenes); ++i)
		{
			if (IsKeyPressedOnce(static_cast<Key>(i + static_cast<usize>(Key::One))))
			{
				setScene(i, &renderer, &camera);
			}
		}

		const float timeNow = static_cast<float>(Platform::GetTime());
		const float timeDelta = timeNow - timeLast;
		timeLast = timeNow;

		camera.Update(timeDelta);
		renderer.Update(camera);
	}

	Platform::DestroyWindow(window);
}
