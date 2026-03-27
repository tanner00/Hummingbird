#include "CameraController.hpp"
#include "GLTF.hpp"

#include "Luft/Platform.hpp"

static constexpr float DefaultMovementSpeed = 4.0f;
static constexpr float FastMovementSpeed = 10.0f;

static constexpr float RotationSpeedRadians = 8.0f * DegreesToRadians;

CameraController::CameraController()
	: PositionWS(Vector::Zero)
	, OrientationWS(Quaternion::Identity)
	, PitchRadians(0.0f)
	, FieldOfViewYRadians(0.0f)
	, AspectRatio(0.0f)
	, NearZ(0.0f)
	, FarZ(0.0f)
{
}

void CameraController::Update(float timeDelta)
{
	if (Platform::GetInputMode() == Platform::InputMode::Captured)
	{
		const float yawDeltaRadians =   -static_cast<float>(Platform::GetMouseX()) * RotationSpeedRadians * timeDelta;
		float pitchDeltaRadians =       -static_cast<float>(Platform::GetMouseY()) * RotationSpeedRadians * timeDelta;

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

		OrientationWS = Quaternion::AxisAngle(Vector { 0.0f, 1.0f, 0.0f }, yawDeltaRadians) * OrientationWS;
		OrientationWS = OrientationWS.GetNormalized();
		OrientationWS = Quaternion::AxisAngle(OrientationWS.Rotate(Vector { 1.0f, 0.0f, 0.0f }), pitchDeltaRadians) * OrientationWS;
		OrientationWS = OrientationWS.GetNormalized();
	}

	const Vector forwardWS = OrientationWS.Rotate(GLTF::DefaultDirectionLS);
	const Vector upWS = OrientationWS.Rotate(Vector { 0.0f, 1.0f, 0.0f });
	const Vector sideWS = upWS.Cross(OrientationWS.Rotate(GLTF::DefaultDirectionLS));

	Vector movementWS = Vector::Zero;
	bool moving = false;

	if (Platform::IsKeyPressed(Platform::Key::W))
	{
		movementWS = forwardWS;
		moving = true;
	}
	else if (Platform::IsKeyPressed(Platform::Key::S))
	{
		movementWS = -forwardWS;
		moving = true;
	}

	if (Platform::IsKeyPressed(Platform::Key::A))
	{
		movementWS = movementWS + sideWS;
		moving = true;
	}
	else if (Platform::IsKeyPressed(Platform::Key::D))
	{
		movementWS = movementWS - sideWS;
		moving = true;
	}

	if (moving)
	{
		const float movementSpeed = Platform::IsKeyPressed(Platform::Key::Shift) ? FastMovementSpeed : DefaultMovementSpeed;
		PositionWS = PositionWS + movementWS.GetNormalized() * movementSpeed * timeDelta;
	}
}

void CameraController::SetCamera(const GLTF::Camera& camera)
{
	DecomposeTransform(camera.LocalToWorld, &PositionWS, &OrientationWS, nullptr);

	PitchRadians = 0.0f;

	FieldOfViewYRadians = camera.FieldOfViewYRadians;
	AspectRatio = camera.AspectRatio;
	NearZ = camera.NearZ;
	FarZ = camera.FarZ;
}
