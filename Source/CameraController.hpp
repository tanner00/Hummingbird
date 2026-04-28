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

	Vector GetPositionWS() const
	{
		return PositionWS;
	}

	Matrix GetViewToWorld() const
	{
		return Matrix::Translation(PositionWS) * OrientationWS.ToMatrix();
	}

	float32 GetFieldOfViewYRadians() const { return FieldOfViewYRadians; }
	float32 GetAspectRatio() const { return AspectRatio; }
	float32 GetNearZ() const { return NearZ; }
	float32 GetFarZ() const { return FarZ; }

private:
	Vector PositionWS;
	Quaternion OrientationWS;

	float32 PitchRadians;

	float32 FieldOfViewYRadians;
	float32 AspectRatio;
	float32 NearZ;
	float32 FarZ;
};
