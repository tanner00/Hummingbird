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

	Vector GetPositionWorld() const
	{
		return PositionWorld;
	}

	Matrix GetViewToWorld() const
	{
		return Matrix::Translation(PositionWorld) * OrientationWorld.ToMatrix();
	}

	float GetFieldOfViewYRadians() const { return FieldOfViewYRadians; }
	float GetAspectRatio() const { return AspectRatio; }
	float GetNearZ() const { return NearZ; }
	float GetFarZ() const { return FarZ; }

private:
	Vector PositionWorld;
	Quaternion OrientationWorld;

	float PitchRadians;

	float FieldOfViewYRadians;
	float AspectRatio;
	float NearZ;
	float FarZ;
};
