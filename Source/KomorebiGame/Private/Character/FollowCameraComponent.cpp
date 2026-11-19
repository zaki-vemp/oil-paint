// Copyright Komorebi Project. Third-person follow camera implementation matching legacy-web/src/figure/camera.js.
#include "Character/FollowCameraComponent.h"

#include "Camera/CameraComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"

namespace
{
	float DampCam(float Current, float Target, float Rate, float Dt)
	{
		return Target + (Current - Target) * FMath::Exp(-Rate * Dt);
	}
}

UFollowCameraComponent::UFollowCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UFollowCameraComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!DrivenCamera && GetOwner())
	{
		DrivenCamera = GetOwner()->FindComponentByClass<UCameraComponent>();
	}
	if (DrivenCamera)
	{
		DrivenCamera->SetFieldOfView(CameraFovDeg);
		DrivenCamera->bUsePawnControlRotation = false;
	}
	CurrentDistanceCm = DesiredDistanceCm;
}

void UFollowCameraComponent::SetCameraComponent(UCameraComponent* InCamera)
{
	DrivenCamera = InCamera;
	if (DrivenCamera)
	{
		DrivenCamera->SetFieldOfView(CameraFovDeg);
		DrivenCamera->bUsePawnControlRotation = false;
	}
}

void UFollowCameraComponent::ApplyNeoKyotoPreset()
{
	PitchDeg = FMath::RadiansToDegrees(0.22f);
	LookLiftCm = 25.0f;
	Sink = 0.0f;
	ShoulderOffsetCm = 60.0f;
	DesiredDistanceCm = 720.0f;
	CurrentDistanceCm = DesiredDistanceCm;
}

void UFollowCameraComponent::ApplySnowboundPreset()
{
	// Eye sits low and looks up past Kael toward the aurora corona (night/main.js lines 158-160)
	PitchDeg = FMath::RadiansToDegrees(-0.07f);
	LookLiftCm = 150.0f;
	Sink = 0.7f;
	ShoulderOffsetCm = 60.0f;
	DesiredDistanceCm = 720.0f;
	CurrentDistanceCm = DesiredDistanceCm;
}

void UFollowCameraComponent::SetGroundHeightProvider(TFunction<bool(float, float, float&)> InProvider)
{
	GroundHeightProvider = MoveTemp(InProvider);
}

void UFollowCameraComponent::TrackYaw(float TargetYawDeg, float DeltaTime)
{
	const float Delta = FMath::FindDeltaAngleDegrees(YawDeg, TargetYawDeg);
	YawDeg = FMath::UnwindDegrees(YawDeg + Delta * (1.0f - FMath::Exp(-11.0f * FMath::Max(0.0f, DeltaTime))));
}

void UFollowCameraComponent::OnEnterVehicle(APawn* VehiclePawn)
{
	bDrivingVehicle = true;
	IgnoredVehiclePawn = VehiclePawn;
	PreDriveDesiredDistanceCm = DesiredDistanceCm;
	VehicleLookIdleTime = 0.0f;
}

void UFollowCameraComponent::OnExitVehicle()
{
	bDrivingVehicle = false;
	IgnoredVehiclePawn.Reset();
	DesiredDistanceCm = PreDriveDesiredDistanceCm;
	SpeedDistanceBoostCm = 0.0f;
	VehicleLookIdleTime = 0.0f;
}

void UFollowCameraComponent::UpdateVehicleChase(float DeltaTime, APawn* VehiclePawn, float LookInputMagnitude)
{
	if (!VehiclePawn || !bDrivingVehicle)
	{
		SpeedDistanceBoostCm = 0.0f;
		return;
	}

	IgnoredVehiclePawn = VehiclePawn;
	VehicleLookIdleTime = (LookInputMagnitude > 1e-4f) ? 0.0f : (VehicleLookIdleTime + DeltaTime);

	const FVector Vel = VehiclePawn->GetVelocity();
	const FVector Forward = VehiclePawn->GetActorForwardVector().GetSafeNormal2D();
	const float ForwardSpeedCm = FVector::DotProduct(FVector(Vel.X, Vel.Y, 0.0f), Forward);
	const float HorizSpeedCm = Vel.Size2D();

	// Swing camera behind the vehicle after 0.7s of mouse stillness while moving > 2 m/s (ride.js lines 597-599)
	if (VehicleLookIdleTime > 0.7f && ForwardSpeedCm > 200.0f)
	{
		const float VehicleYawDeg = VehiclePawn->GetActorRotation().Yaw;
		const float Delta = FMath::FindDeltaAngleDegrees(YawDeg, VehicleYawDeg);
		YawDeg = FMath::UnwindDegrees(YawDeg + Delta * FMath::Min(1.0f, 2.4f * DeltaTime));
	}

	// Speed distance boost: min(4.2m, speed_mps * 0.13) -> cm
	SpeedDistanceBoostCm = FMath::Min(420.0f, HorizSpeedCm * 0.13f);
}

void UFollowCameraComponent::SetShake(float InShake)
{
	ShakeAmount = FMath::Max(0.0f, InShake);
}

void UFollowCameraComponent::AddShake(float InShake)
{
	ShakeAmount = FMath::Max(ShakeAmount, InShake);
}

void UFollowCameraComponent::SetFreeView(const FVector& EyeWorldLocation, const FVector& TargetWorldLocation)
{
	bFreeViewActive = true;
	FreeViewEye = EyeWorldLocation;
	FreeViewTarget = TargetWorldLocation;
}

void UFollowCameraComponent::ClearFreeView()
{
	bFreeViewActive = false;
}

void UFollowCameraComponent::ResetPivot()
{
	bPivotInitialized = false;
	PivotWorld = FVector::ZeroVector;
}

FVector UFollowCameraComponent::GetCameraWorldLocation() const
{
	return DrivenCamera ? DrivenCamera->GetComponentLocation() : GetComponentLocation();
}

FVector UFollowCameraComponent::GetCameraForwardVector() const
{
	return DrivenCamera ? DrivenCamera->GetForwardVector() : GetForwardVector();
}

float UFollowCameraComponent::QueryGroundHeightCm(float WorldXCm, float WorldYCm, float ReferenceZCm) const
{
	if (GroundHeightProvider)
	{
		float GroundZ = ReferenceZCm;
		if (GroundHeightProvider(WorldXCm, WorldYCm, GroundZ))
		{
			return GroundZ;
		}
	}

	if (const UWorld* World = GetWorld())
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FollowCameraGround), false, GetOwner());
		if (const APawn* IgnoredPawn = IgnoredVehiclePawn.Get())
		{
			Params.AddIgnoredActor(IgnoredPawn);
		}
		const FVector Start(WorldXCm, WorldYCm, ReferenceZCm + 5000.0f);
		const FVector End(WorldXCm, WorldYCm, ReferenceZCm - 5000.0f);
		if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
		{
			return Hit.ImpactPoint.Z;
		}
	}

	return ReferenceZCm - TargetHeightOffsetCm;
}

void UFollowCameraComponent::UpdateFollowCamera(
	float RealDeltaTime,
	const FVector& TargetFeetWorld,
	FVector2D LookDeltaDeg,
	float ZoomSteps)
{
	if (!DrivenCamera)
	{
		if (GetOwner())
		{
			DrivenCamera = GetOwner()->FindComponentByClass<UCameraComponent>();
		}
		if (!DrivenCamera)
		{
			return;
		}
	}

	const float Dt = FMath::Clamp(RealDeltaTime, 0.0f, 0.05f);

	if (bFreeViewActive)
	{
		const FVector LookDir = (FreeViewTarget - FreeViewEye).GetSafeNormal();
		DrivenCamera->SetWorldLocationAndRotation(FreeViewEye, LookDir.Rotation());
		return;
	}

	YawDeg = FMath::UnwindDegrees(YawDeg + LookDeltaDeg.X);
	PitchDeg = FMath::Clamp(PitchDeg + LookDeltaDeg.Y, MinPitchDeg, MaxPitchDeg);
	DesiredDistanceCm = FMath::Clamp(DesiredDistanceCm + ZoomSteps * ZoomStepCm, MinDistanceCm, MaxDistanceCm);

	const float ReachCm = FMath::Min(MaxBoostedDistanceCm, DesiredDistanceCm + SpeedDistanceBoostCm);
	const FVector Goal = TargetFeetWorld + FVector(0.0f, 0.0f, TargetHeightOffsetCm);

	if (!bPivotInitialized)
	{
		PivotWorld = Goal;
		bPivotInitialized = true;
	}
	else
	{
		PivotWorld.X = DampCam(PivotWorld.X, Goal.X, PivotDampRateXY, Dt);
		PivotWorld.Y = DampCam(PivotWorld.Y, Goal.Y, PivotDampRateXY, Dt);
		PivotWorld.Z = DampCam(PivotWorld.Z, Goal.Z, PivotDampRateZ, Dt);
	}

	const float YawRad = FMath::DegreesToRadians(YawDeg);
	const float PitchRad = FMath::DegreesToRadians(PitchDeg);
	const float CosP = FMath::Cos(PitchRad);
	const float SinP = FMath::Sin(PitchRad);
	const float CosY = FMath::Cos(YawRad);
	const float SinY = FMath::Sin(YawRad);

	// UE basis: Forward=(CosY, SinY, 0), Right=(-SinY, CosY, 0), Up=(0, 0, 1)
	// Back points from pivot back & upward toward the camera eye.
	const FVector Back(-CosY * CosP, -SinY * CosP, SinP);
	const FVector Right(-SinY, CosY, 0.0f);

	const float ScaleK = FMath::Min(1.0f, ReachCm / 640.0f);
	const float ShoulderCm = ShoulderOffsetCm * ScaleK;

	// Sphere sweep from pivot covering near plane + 4 cm margin against ECC_WorldStatic (skipping owner & driven vehicle)
	float FreeDistCm = ReachCm;
	const float SweepLengthCm = FMath::Sqrt(ReachCm * ReachCm + ShoulderCm * ShoulderCm);
	if (SweepLengthCm > KINDA_SMALL_NUMBER && GetWorld())
	{
		const float NearCm = 20.0f; // 0.2 m near plane matching Three.js PerspectiveCamera
		const float Aspect = DrivenCamera->AspectRatio > 0.1f ? DrivenCamera->AspectRatio : (16.0f / 9.0f);
		const float NearH = NearCm * FMath::Tan(FMath::DegreesToRadians(CameraFovDeg * 0.5f));
		const float ProbeRadiusCm = FMath::Clamp(FMath::Sqrt(NearCm * NearCm + NearH * NearH + (NearH * Aspect) * (NearH * Aspect)) + 4.0f, 12.0f, 35.0f);

		const FVector SweepDir = (Back * ReachCm + Right * ShoulderCm) / SweepLengthCm;
		const FVector SweepEnd = PivotWorld + SweepDir * SweepLengthCm;

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FollowCameraSweep), false, GetOwner());
		if (const APawn* IgnoredPawn = IgnoredVehiclePawn.Get())
		{
			QueryParams.AddIgnoredActor(IgnoredPawn);
		}

		FHitResult Hit;
		if (GetWorld()->SweepSingleByChannel(
				Hit,
				PivotWorld,
				SweepEnd,
				FQuat::Identity,
				ECC_WorldStatic,
				FCollisionShape::MakeSphere(ProbeRadiusCm),
				QueryParams))
		{
			FreeDistCm = FMath::Max(6.0f, Hit.Distance * (ReachCm / SweepLengthCm));
		}
	}

	// Instant pull-in on obstruction; smooth recovery out (camera.js line 65)
	CurrentDistanceCm = FMath::Min(FreeDistCm, DampCam(CurrentDistanceCm, FreeDistCm, DistanceRecoverRate, Dt));

	const FVector FromWorld = PivotWorld + Right * (ShoulderCm * (ReachCm > KINDA_SMALL_NUMBER ? CurrentDistanceCm / ReachCm : 1.0f));
	FVector EyeWorld = FromWorld + Back * CurrentDistanceCm;

	// Terrain floor clamp + Sink
	const float FloorZ = QueryGroundHeightCm(EyeWorld.X, EyeWorld.Y, PivotWorld.Z) + FloorClearanceCm;
	float RaisedCm = 0.0f;
	if (EyeWorld.Z < FloorZ)
	{
		RaisedCm = FloorZ - EyeWorld.Z;
		EyeWorld.Z = FloorZ;
	}

	const FVector LookTarget = (FromWorld - Back * LookAheadCm) + FVector(0.0f, 0.0f, LookLiftCm * ScaleK - RaisedCm * Sink);

	// Hit-kick camera shake when unobstructed and reduced motion is off (camera.js lines 72-77)
	ShakeTime += Dt;
	if (!bReducedMotion && ShakeAmount > KINDA_SMALL_NUMBER && FMath::IsNearlyEqual(FreeDistCm, ReachCm, 1.0f))
	{
		EyeWorld += Right * (FMath::Sin(ShakeTime * 85.0f) * ShakeAmount * 30.0f);
		EyeWorld.Z += FMath::Cos(ShakeTime * 71.0f) * ShakeAmount * 20.0f;
	}

	const FVector LookDir = (LookTarget - EyeWorld).GetSafeNormal();
	DrivenCamera->SetWorldLocationAndRotation(EyeWorld, LookDir.Rotation());
}
