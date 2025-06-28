#include "CameraController.hpp"
#include "GLTF.hpp"

static constexpr float DefaultMovementSpeed = 4.0f;
static constexpr float FastMovementSpeed = 10.0f;

static constexpr float RotationSpeedRadians = 8.0f * DegreesToRadians;

CameraController::CameraController()
	: PositionWorld(Vector::Zero)
	, OrientationWorld(Quaternion::Identity)
	, PitchRadians(0.0f)
	, FieldOfViewYRadians(0.0f)
	, AspectRatio(0.0f)
	, NearZ(0.0f)
	, FarZ(0.0f)
{
}

void CameraController::Update(float timeDelta)
{
	if (Platform::GetInputMode() == InputMode::Captured)
	{
		const float yawDeltaRadians =   -static_cast<float>(GetMouseX()) * RotationSpeedRadians * timeDelta;
		float pitchDeltaRadians =       -static_cast<float>(GetMouseY()) * RotationSpeedRadians * timeDelta;

		PitchRadians += pitchDeltaRadians;
		if (PitchRadians > +Pi / 2.0f)
		{
			pitchDeltaRadians -= (PitchRadians - Pi / 2.0f);
			PitchRadians = +Pi / 2.0f;
		}
		else if (PitchRadians < -Pi / 2.0f)
		{
			pitchDeltaRadians -= (PitchRadians + Pi / 2.0f);
			PitchRadians = -Pi / 2.0f;
		}

		OrientationWorld = Quaternion::AxisAngle(Vector { 0.0f, 1.0f, 0.0f }, yawDeltaRadians) * OrientationWorld;
		OrientationWorld = OrientationWorld.GetNormalized();
		OrientationWorld = Quaternion::AxisAngle(OrientationWorld.Rotate(Vector { 1.0f, 0.0f, 0.0f }), pitchDeltaRadians) * OrientationWorld;
		OrientationWorld = OrientationWorld.GetNormalized();
	}

	const Vector forward = OrientationWorld.Rotate(GLTF::DefaultDirection);
	const Vector up = OrientationWorld.Rotate(Vector { 0.0f, 1.0f, 0.0f });
	const Vector side = up.Cross(OrientationWorld.Rotate(GLTF::DefaultDirection));

	Vector movement = Vector::Zero;
	bool moving = false;

	if (IsKeyPressed(Key::W))
	{
		movement = forward;
		moving = true;
	}
	else if (IsKeyPressed(Key::S))
	{
		movement = -forward;
		moving = true;
	}

	if (IsKeyPressed(Key::A))
	{
		movement = movement + side;
		moving = true;
	}
	else if (IsKeyPressed(Key::D))
	{
		movement = movement - side;
		moving = true;
	}

	if (moving)
	{
		const float movementSpeed = IsKeyPressed(Key::Shift) ? FastMovementSpeed : DefaultMovementSpeed;
		PositionWorld = PositionWorld + movement.GetNormalized() * movementSpeed * timeDelta;
	}
}

void CameraController::SetCamera(const GLTF::Camera& camera)
{
	DecomposeTransform(camera.LocalToWorld, &PositionWorld, &OrientationWorld, nullptr);

	PitchRadians = 0.0f;

	FieldOfViewYRadians = camera.FieldOfViewYRadians;
	AspectRatio = camera.AspectRatio;
	NearZ = camera.NearZ;
	FarZ = camera.FarZ;
}
