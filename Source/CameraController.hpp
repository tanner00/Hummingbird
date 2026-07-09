#pragma once

#include "Luft/Math.hpp"

namespace GLTF
{
struct Camera;
}

class CameraController
{
public:
	CameraController();

	void Update(float32 timeDelta);

	void SetCamera(const GLTF::Camera& camera);

	Vector GetPosition() const { return PositionWS; }

	Matrix GetViewToWorld() const { return Matrix::Translation(PositionWS) * OrientationWS.ToMatrix(); }

	float32 GetFieldOfViewYRadians() const { return FieldOfViewYRadians; }
	float32 GetAspectRatio() const { return AspectRatio; }
	float32 GetNear() const { return Near; }
	float32 GetFar() const { return Far; }

private:
	Vector PositionWS;
	Quaternion OrientationWS;

	float32 PitchRadians;

	float32 FieldOfViewYRadians;
	float32 AspectRatio;
	float32 Near;
	float32 Far;
};
