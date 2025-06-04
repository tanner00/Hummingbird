#include "CameraController.hpp"
#include "GLTF.hpp"
#include "Renderer.hpp"

#include "Luft/Platform.hpp"

static const StringView scenes[] =
{
	"Assets/Scenes/Sponza/Sponza.gltf"_view,
	"Assets/Scenes/Bistro/Bistro.gltf"_view,
	"Assets/Scenes/EmeraldSquare/EmeraldSquare_Day.gltf"_view,
	"Assets/Scenes/SunTemple/SunTemple.gltf"_view,
};

static bool NeedsResize = false;

void Start()
{
	const auto setScene = [](usize sceneIndex, Renderer* renderer, CameraController* cameraController) -> void
	{
		const double start = Platform::GetTime();

		GLTF::Scene scene = GLTF::LoadScene(scenes[sceneIndex]);

		const GLTF::Camera defaultCamera =
		{
			.Transform = Matrix::Identity,
			.FieldOfViewYRadians = Pi / 3.0f,
			.AspectRatio = 16.0f / 9.0f,
			.NearZ = 0.1f,
			.FarZ = 1000.0f,
		};
		const GLTF::Camera camera = scene.Cameras.IsEmpty() ? defaultCamera : scene.Cameras[0];

		cameraController->SetCamera(camera);
		renderer->SetScene(scene);

		GLTF::UnloadScene(&scene);

		const double end = Platform::GetTime();
		Platform::LogFormatted("Scene took %.2fs to load\n", end - start);
	};

	Platform::Window* window = Platform::MakeWindow("Hummingbird", 1920, 1080);

	Renderer renderer(window);
	CameraController cameraController;
	setScene(0, &renderer, &cameraController);

	Platform::ShowWindow(window);
	Platform::InstallResizeHandler([](Platform::Window*) -> void
	{
		NeedsResize = true;
	});

	double timeLast = 0.0;

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
				setScene(i, &renderer, &cameraController);
			}
		}

		const double timeNow = Platform::GetTime();
		const double timeDelta = timeNow - timeLast;
		timeLast = timeNow;

		cameraController.Update(static_cast<float>(timeDelta));
		renderer.Update(cameraController);
	}

	Platform::DestroyWindow(window);
}
