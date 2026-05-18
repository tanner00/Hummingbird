#include "Editor.hpp"
#include "CameraController.hpp"
#include "DDS.hpp"
#include "GLTF.hpp"
#include "Renderer.hpp"
#include "UI.hpp"

using namespace RHI;
using namespace UI;

static const StringView Scenes[] =
{
	"Assets/Scenes/Sponza/Sponza.gltf"_view,
	"Assets/Scenes/Bistro/Bistro.gltf"_view,
	"Assets/Scenes/EmeraldSquare/EmeraldSquare_Day.gltf"_view,
	"Assets/Scenes/SunTemple/SunTemple.gltf"_view,
};

namespace Theme
{

static constexpr float32x4 BackgroundSRGBA = { 14 / 255.0f, 15 / 255.0f, 18 / 255.0f, 1.0f };
static constexpr float32x4 BackgroundWidgetSRGBA = { 35 / 255.0f, 38 / 255.0f, 46 / 255.0f, 1.0f };
static constexpr float32x4 BorderSRGBA = { 52 / 255.0f, 56 / 255.0f, 66 / 255.0f, 1.0f };
static constexpr float32x4 TextSRGBA = { 226 / 255.0f, 228 / 255.0f, 236 / 255.0f, 1.0f };
static constexpr float32x4 AccentSRGBA = { 77 / 255.0f, 156 / 255.0f, 232 / 255.0f, 1.0f };

}

static bool CheckButton(StringView label, bool* state)
{
	CHECK(state);

	const ID id = NameToID(label);

	Container(
	{
		.ID = id,
		.Layout =
		{
			.AlignmentX = Alignment::Center,
			.AlignmentY = Alignment::Center,
			.PaddingSS = { 10.0f, 10.0f },
		},
		.Style =
		{
			.SRGBA = *state || IsHovered(id) ? Theme::AccentSRGBA : Theme::BackgroundWidgetSRGBA,
			.BorderSRGBA = Theme::BorderSRGBA,
			.BorderSizeSS = 2.0f,
		},
	},
	[label]
	{
		Text(label, 24.0f, { .Style = { .SRGBA = Theme::TextSRGBA } });
	});

	const bool pressed = IsPressedOnce(id);
	if (pressed)
	{
		*state = !(*state);
	}

	return pressed;
}

static bool DropDown(ArrayView<StringView> items, usize* selectedIndex)
{
	CHECK(!items.IsEmpty());
	CHECK(selectedIndex);
	CHECK(*selectedIndex < items.GetCount());

	const ID id = NamesToID(items);
	const ID openID = NameCombine(id, "Open"_view);

	const bool open = DoesExist(openID);

	bool anyPressed = false;

	Container(
	{
		.ID = id,
	},
	[items, selectedIndex, id, openID, open, &anyPressed]
	{
		Container(
		{
			.Layout =
			{
				.PaddingSS = { 10.0f, 10.0f },
			},
			.Style =
			{
				.SRGBA = open || IsHovered(id, true) ? Theme::AccentSRGBA : Theme::BackgroundWidgetSRGBA,
				.BorderSRGBA = Theme::BorderSRGBA,
				.BorderSizeSS = 2.0f,
			},
		},
		[items, selectedIndex]
		{
			Text(items[*selectedIndex], 24.0f, { .Style = { .SRGBA = Theme::TextSRGBA } });
		});

		for (usize itemIndex = 0; itemIndex < items.GetCount(); ++itemIndex)
		{
			const StringView item = items[itemIndex];
			const ID itemID = NameToID(item);

			if (IsPressedOnce(itemID))
			{
				*selectedIndex = itemIndex;
				anyPressed = true;
				return;
			}
		}

		if (!open || (Platform::IsMouseButtonPressedOnce(Platform::MouseButton::Left) && !anyPressed))
		{
			return;
		}

		Container(
		{
			.ID = openID,
			.Layout =
			{
				.AlignmentY = Alignment::Center,
				.PaddingSS = { 2.0f, 2.0f },
				.SpacingSS = 2.0f,
				.Floating = true,
			},
			.Style =
			{
				.SRGBA = Theme::BackgroundWidgetSRGBA,
				.BorderSRGBA = Theme::BorderSRGBA,
				.BorderSizeSS = 2.0f,
				.BetweenSRGBA = Theme::BorderSRGBA,
				.BetweenSizeSS = 2.0f,
			},
		},
		[items, &selectedIndex, &anyPressed]
		{
			for (const StringView item : items)
			{
				const ID itemID = NameToID(item);

				Container(
				{
					.ID = itemID,
					.Layout =
					{
						.SizeX = Grow(),
						.PaddingSS = { 10.0f, 10.0f },
					},
					.Style =
					{
						.SRGBA = IsHovered(itemID) ? Theme::AccentSRGBA : Theme::BackgroundWidgetSRGBA,
					}
				},
				[item]
				{
					Text(item, 24.0f, { .Style = { .SRGBA = Theme::TextSRGBA } });
				});
			}
		});
	});

	if (!open && IsPressedOnce(id, true))
	{
		Container({ .ID = openID, .Layout = { .Floating = true } }, []{});
	}

	return anyPressed;
}

Editor::Editor(Platform::Window* window, ::Renderer* renderer, ::CameraController* cameraController)
	: Window(window)
	, Renderer(renderer)
	, CameraController(cameraController)
{
	SetScene(0);
}

void Editor::Update()
{
	Container({ .Layout = { .SizeX = Fixed(static_cast<float32>(Window->DrawWidth)), .SizeY = Fixed(static_cast<float32>(Window->DrawHeight)) } }, [this]
	{
		Container(
		{
			.Layout =
			{
				.SizeX = Grow(),
				.Direction = Direction::Horizontal,
				.AlignmentY = Alignment::Center,
				.PaddingSS = { 10.0f, 10.0f },
				.SpacingSS = 10.0f,
			},
			.Style =
			{
				.SRGBA = Theme::BackgroundSRGBA,
			},
		},
		[this]
		{
			static usize sceneIndex = 0;
			if (DropDown({ "Sponza"_view, "Bistro"_view, "Emerald Square"_view, "Sun Temple"_view }, &sceneIndex))
			{
				SetScene(sceneIndex);
			}

			static usize viewModeIndex = 0;
			if (DropDown({ "Lit"_view, "Unlit"_view, "Geometry"_view, "Normal"_view }, &viewModeIndex))
			{
				Renderer->ViewMode = static_cast<HLSL::ViewMode>(viewModeIndex);
			}

			if (CheckButton("TAA"_view, &Renderer->TemporalAntiAliasing.Enabled))
			{
				Renderer->TemporalAntiAliasing.DiscardPreviousFrame = true;
			}

#if !RELEASE
			Rectangle({ .Layout = { .SizeX = Grow() } });

			Container({}, [this]
			{
				char cpuTimeText[16] = {};
				Platform::StringPrint("CPU: %.2fms", cpuTimeText, sizeof(cpuTimeText), Renderer->AverageCPUTime * 1000.0);
				Text(StringView { cpuTimeText, Platform::StringLength(cpuTimeText) }, 24.0f, { .Style = { .SRGBA = Theme::TextSRGBA } });

				char gpuTimeText[16] = {};
				Platform::StringPrint("GPU: %.2fms", gpuTimeText, sizeof(gpuTimeText), Renderer->AverageGPUTime * 1000.0);
				Text(StringView { gpuTimeText, Platform::StringLength(gpuTimeText) }, 24.0f, { .Style = { .SRGBA = Theme::TextSRGBA } });
			});
#endif
		});

		Container({ .Layout = { .SizeX = Grow(), .SizeY = Grow(), .Direction = Direction::Horizontal } }, [this]
		{
			const ID viewportID = NameToID("Viewport"_view);

			const float32x2 viewportSizeSS = GetSize(viewportID);

			const uint32 viewportPixelWidth = static_cast<uint32>(viewportSizeSS.X);
			const uint32 viewportPixelHeight = static_cast<uint32>(viewportSizeSS.Y);

			const ResourceDimensions currentViewportDimensions = Renderer->FinalTextureResource.Dimensions;

			const bool resize = viewportPixelWidth != 0 && viewportPixelHeight != 0 &&
								(currentViewportDimensions.Width != viewportPixelWidth || currentViewportDimensions.Height != viewportPixelHeight);
			if (resize)
			{
				Renderer->ResizeViewport(viewportPixelWidth, viewportPixelHeight);
			}

			Image(Renderer->FinalTextureShaderResourceView, { .ID = viewportID, .Layout = { .SizeX = Grow(), .SizeY = Grow() }, .Style = White });

			if (IsPressedOnce(viewportID))
			{
				Platform::SetInputMode(Window, Platform::InputMode::Captured);
			}
		});
	});

	if (Platform::IsKeyPressedOnce(Platform::Key::R))
	{
		Renderer->RecreatePipelines();
	}
}

void Editor::SetScene(usize sceneIndex)
{
	const float64 start = Platform::GetTime();

	GLTF::Scene scene = GLTF::LoadScene(Scenes[sceneIndex]);

	const GLTF::Camera defaultCamera =
	{
		.LocalToWorld = Matrix::Identity,
		.FieldOfViewYRadians = Pi / 3.0f,
		.AspectRatio = 16.0f / 9.0f,
		.NearZ = 0.1f,
		.FarZ = 1000.0f,
	};
	const GLTF::Camera camera = scene.Cameras.IsEmpty() ? defaultCamera : scene.Cameras[0];

	CameraController->SetCamera(camera);
	Renderer->SetScene(scene);

	GLTF::UnloadScene(&scene);

	const float64 end = Platform::GetTime();
	Platform::LogFormatted("Scene took %.2fs to load\n", end - start);
}
