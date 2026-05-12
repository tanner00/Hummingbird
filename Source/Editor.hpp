#pragma once

#include "Luft/Base.hpp"
#include "Luft/NoCopy.hpp"

class CameraController;
class Renderer;

namespace Platform
{
struct Window;
}

class Editor : public NoCopy
{
public:
	Editor(Platform::Window* window, Renderer* renderer, CameraController* cameraController);

	void Update();

private:
	void SetScene(usize sceneIndex);

	Platform::Window* Window;
	Renderer* Renderer;
	CameraController* CameraController;
};
