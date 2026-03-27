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

	Vector GetPositionWS() const
	{
		return PositionWS;
	}

	Matrix GetViewToWorld() const
	{
		return Matrix::Translation(PositionWS) * OrientationWS.ToMatrix();
	}

	float GetFieldOfViewYRadians() const { return FieldOfViewYRadians; }
	float GetAspectRatio() const { return AspectRatio; }
	float GetNearZ() const { return NearZ; }
	float GetFarZ() const { return FarZ; }

private:
	Vector PositionWS;
	Quaternion OrientationWS;

	float PitchRadians;

	float FieldOfViewYRadians;
	float AspectRatio;
	float NearZ;
	float FarZ;
};
