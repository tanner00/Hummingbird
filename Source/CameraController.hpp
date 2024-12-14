#pragma once

#include "Luft/Math.hpp"

struct GltfScene;

class CameraController
{
public:
	explicit CameraController();

	void Update(float timeDelta);

	void SetScene(const GltfScene& scene);

	Matrix GetViewTransform() const
	{
		const Matrix cameraTransform = Matrix::Translation(Position) * Orientation.ToMatrix();
		return cameraTransform.GetInverse();
	}

	float GetFieldOfViewYRadians() const { return FieldOfViewYRadians; }
	float GetAspectRatio() const { return AspectRatio; }
	float GetNearZ() const { return NearZ; }
	float GetFarZ() const { return FarZ; }

private:
	Vector Position;
	Quaternion Orientation;

	float PitchRadians;

	float FieldOfViewYRadians;
	float AspectRatio;
	float NearZ;
	float FarZ;
};
