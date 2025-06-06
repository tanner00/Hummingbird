#include "CameraController.hpp"
#include "GLTF.hpp"

static constexpr float DefaultMovementSpeed = 4.0f;
static constexpr float FastMovementSpeed = 10.0f;

static constexpr float RotationSpeedRadians = 8.0f * DegreesToRadians;

CameraController::CameraController()
	: Position(Vector::Zero)
	, Orientation(Quaternion::Identity)
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

		Orientation = Quaternion::AxisAngle(Vector { +0.0f, +1.0f, +0.0f }, yawDeltaRadians) * Orientation;
		Orientation = Orientation.GetNormalized();
		Orientation = Quaternion::AxisAngle(Orientation.Rotate(Vector { +1.0f, +0.0f, +0.0f }), pitchDeltaRadians) * Orientation;
		Orientation = Orientation.GetNormalized();
	}

	const Vector forward = Orientation.Rotate(GLTF::DefaultDirection);
	const Vector up = Orientation.Rotate(Vector { 0.0f, +1.0f, +0.0f });
	const Vector side = up.Cross(Orientation.Rotate(GLTF::DefaultDirection));

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
		Position = Position + movement.GetNormalized() * movementSpeed * timeDelta;
	}
}

void CameraController::SetCamera(const GLTF::Camera& camera)
{
	DecomposeTransform(camera.Transform, &Position, &Orientation, nullptr);

	PitchRadians = 0.0f;

	FieldOfViewYRadians = camera.FieldOfViewYRadians;
	AspectRatio = camera.AspectRatio;
	NearZ = camera.NearZ;
	FarZ = camera.FarZ;
}
