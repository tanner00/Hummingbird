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

	void Update(float timeDelta);

	void SetCamera(const GLTF::Camera& camera);

	Vector GetPosition() const
	{
		return Position;
	}

	Matrix GetTransform() const
	{
		return Matrix::Translation(Position) * Orientation.ToMatrix();
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
