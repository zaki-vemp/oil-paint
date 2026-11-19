// Copyright Komorebi Project. Kael movement implementation matching legacy-web/src/figure/player.js TUNING.
#include "Character/KaelMovementComponent.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"

namespace
{
	float DampValue(float Current, float Target, float Rate, float Dt)
	{
		return Target + (Current - Target) * FMath::Exp(-Rate * Dt);
	}

	float ApproachValue(float Current, float Target, float MaxStep)
	{
		if (Current < Target)
		{
			return FMath::Min(Target, Current + MaxStep);
		}
		return FMath::Max(Target, Current - MaxStep);
	}
}

UKaelMovementComponent::UKaelMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bWantsInitializeComponent = true;

	// Match TUNING from legacy-web/src/figure/player.js (converted to cm/deg)
	MaxWalkSpeed = RunSpeedCm;
	MaxAcceleration = RunAccelCm;
	BrakingDecelerationWalking = GroundDecelCm;
	GroundFriction = 8.0f;
	BrakingFrictionFactor = 1.0f;
	bUseSeparateBrakingFriction = false;

	MaxStepHeight = 30.0f; // 0.3 m
	SetWalkableFloorAngle(46.0f); // 46 deg
	PerchRadiusThreshold = 8.0f;
	bMaintainHorizontalGroundVelocity = true;

	JumpZVelocity = FMath::Sqrt(2.0f * RiseGravityCm * JumpHeightCm);
	AirControl = 0.35f;
	AirControlBoostMultiplier = 1.0f;
	FallingLateralFriction = 0.0f;

	// We handle actor yaw rotation directly in UpdateActorFacing() to match player.js turn rates and strafe feel.
	bOrientRotationToMovement = false;
	bUseControllerDesiredRotation = false;
	RotationRate = FRotator(0.0f, RunTurnRateDeg, 0.0f);
}

void UKaelMovementComponent::InitializeComponent()
{
	Super::InitializeComponent();
	JumpZVelocity = FMath::Sqrt(2.0f * RiseGravityCm * JumpHeightCm);
	MaxStepHeight = 30.0f;
	SetWalkableFloorAngle(46.0f);

	if (const ACharacter* OwnerChar = GetCharacterOwner())
	{
		LastYawDeg = OwnerChar->GetActorRotation().Yaw;
		LastMoveDir = OwnerChar->GetActorForwardVector().GetSafeNormal2D();
	}
	CoyoteTimer = CoyoteTimeDuration;
}

float UKaelMovementComponent::GetGravityZ() const
{
	// An air dash holds Kael's altitude while it lasts (player.js line 163).
	if (DashTimeRemaining > 0.0f && !IsMovingOnGround())
	{
		return 0.0f;
	}
	return (Velocity.Z < 0.0f) ? -FallGravityCm : -RiseGravityCm;
}

void UKaelMovementComponent::SetGaitInputs(
	bool bInWalk,
	bool bInSprint,
	bool bInStrafe,
	bool bInGuard,
	float InInputMagnitude,
	const FVector& InDesiredWorldDir,
	float InAimYawDeg,
	bool bInHasCombatAim)
{
	bWalkMode = bInWalk;
	bSprintRequested = bInSprint;
	bGuardSlow = bInGuard;
	bStrafeActive = bInStrafe || bInGuard || (EvadeTimeRemaining > 0.0f);
	InputMagnitude = FMath::Clamp(InInputMagnitude, 0.0f, 1.0f);
	AimYawDeg = InAimYawDeg;
	bHasCombatAim = bInHasCombatAim;

	const FVector FlatDir = FVector(InDesiredWorldDir.X, InDesiredWorldDir.Y, 0.0f);
	DesiredWorldDir = FlatDir.SizeSquared() > KINDA_SMALL_NUMBER ? FlatDir.GetSafeNormal() : FVector::ZeroVector;
}

void UKaelMovementComponent::RequestJumpBuffered()
{
	JumpBufferTimer = JumpBufferDuration;
	bJumpHeld = true;
}

void UKaelMovementComponent::SetJumpHeld(bool bHeld)
{
	bJumpHeld = bHeld;
}

bool UKaelMovementComponent::StartDash(const FVector& WorldDir)
{
	const bool bGrounded = IsMovingOnGround();
	if (DashTimeRemaining > 0.0f || EvadeTimeRemaining > 0.0f || (!bGrounded && bAirDashed))
	{
		return false;
	}

	if (!bGrounded)
	{
		bAirDashed = true;
		Velocity.Z = FMath::Max(Velocity.Z, 0.0f);
	}

	FVector FlatDir(WorldDir.X, WorldDir.Y, 0.0f);
	if (FlatDir.SizeSquared() <= KINDA_SMALL_NUMBER)
	{
		FlatDir = CharacterOwner ? CharacterOwner->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
	}
	else
	{
		FlatDir.Normalize();
	}

	DashDirection = FlatDir;
	DashTimeRemaining = kDashDuration;
	Velocity.X = DashDirection.X * DashSpeedCm;
	Velocity.Y = DashDirection.Y * DashSpeedCm;
	HorizontalSpeedCm = DashSpeedCm;
	return true;
}

bool UKaelMovementComponent::StartEvade(const FVector& WorldDir)
{
	if (!IsMovingOnGround() || DashTimeRemaining > 0.0f || EvadeTimeRemaining > 0.0f)
	{
		return false;
	}

	FVector FlatDir(WorldDir.X, WorldDir.Y, 0.0f);
	if (FlatDir.SizeSquared() <= KINDA_SMALL_NUMBER)
	{
		FlatDir = CharacterOwner ? -CharacterOwner->GetActorForwardVector().GetSafeNormal2D() : -FVector::ForwardVector;
	}
	else
	{
		FlatDir.Normalize();
	}

	EvadeDirection = FlatDir;
	EvadeTimeRemaining = kEvadeDuration;
	bStrafeActive = true;
	JumpBufferTimer = 0.0f;
	HorizontalSpeedCm = EvadeStartSpeedCm;
	return true;
}

void UKaelMovementComponent::StartPlungeSlam(float DownwardSpeedCm)
{
	if (IsMovingOnGround())
	{
		return;
	}
	DashTimeRemaining = 0.0f;
	EvadeTimeRemaining = 0.0f;
	Velocity.X *= 0.3f;
	Velocity.Y *= 0.3f;
	Velocity.Z = -FMath::Abs(DownwardSpeedCm);
}

void UKaelMovementComponent::ApplyKnockbackImpulse(const FVector& Direction, float StrengthCmPerSec)
{
	const FVector FlatDir(Direction.X, Direction.Y, 0.0f);
	const float Len = FlatDir.Size();
	if (Len <= KINDA_SMALL_NUMBER || FMath::IsNearlyZero(StrengthCmPerSec))
	{
		return;
	}
	KnockbackVelocity = (FlatDir / Len) * StrengthCmPerSec;
}

void UKaelMovementComponent::ResetMovementState()
{
	StopMovementImmediately();
	Velocity = FVector::ZeroVector;
	KnockbackVelocity = FVector::ZeroVector;
	DashTimeRemaining = 0.0f;
	EvadeTimeRemaining = 0.0f;
	bAirDashed = false;
	JumpBufferTimer = 0.0f;
	CoyoteTimer = CoyoteTimeDuration;
	AirTime = 0.0f;
	LastFallSpeedCm = 0.0f;
	HorizontalSpeedCm = 0.0f;
	PrevSpeedMps = 0.0f;
	TurnRateRadPerSec = 0.0f;
	ForwardAccelMps2 = 0.0f;
	MoveAngleRad = 0.0f;
	SprintTime = 0.0f;
	TiredTimeRemaining = 0.0f;
	if (const ACharacter* OwnerChar = GetCharacterOwner())
	{
		LastYawDeg = OwnerChar->GetActorRotation().Yaw;
		LastMoveDir = OwnerChar->GetActorForwardVector().GetSafeNormal2D();
	}
}

float UKaelMovementComponent::ComputeStrafeSpeedCapCm(float AngleRad) const
{
	const float Fwd = FMath::Cos(AngleRad);
	if (Fwd >= 0.0f)
	{
		return FMath::Lerp(StrafeJogSpeedCm, RunSpeedCm * 0.82f, Fwd * Fwd);
	}
	return FMath::Lerp(StrafeJogSpeedCm, WalkSpeedCm * 1.15f, Fwd * Fwd);
}

void UKaelMovementComponent::ExecuteCoyoteJump()
{
	if (!CharacterOwner)
	{
		return;
	}

	JumpZVelocity = FMath::Sqrt(2.0f * RiseGravityCm * JumpHeightCm);
	Velocity.Z = JumpZVelocity;
	SetMovementMode(MOVE_Falling);
	CoyoteTimer = 0.0f;
	JumpBufferTimer = 0.0f;
	AirTime = 0.0f;
	EvadeTimeRemaining = 0.0f;
}

void UKaelMovementComponent::UpdateActorFacing(float DeltaTime)
{
	if (!CharacterOwner || DeltaTime <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float CurrentYawDeg = CharacterOwner->GetActorRotation().Yaw;
	float TargetYawDeg = CurrentYawDeg;
	float TurnRateDeg = RunTurnRateDeg;

	if (bStrafeActive)
	{
		if (InputMagnitude > 0.05f || HorizontalSpeedCm > 20.0f || bHasCombatAim || bGuardSlow)
		{
			TargetYawDeg = AimYawDeg;
			TurnRateDeg = IsMovingOnGround() ? StrafeTurnRateDeg : FMath::RadiansToDegrees(8.0f);
		}
	}
	else if (InputMagnitude > 0.05f && DesiredWorldDir.SizeSquared() > KINDA_SMALL_NUMBER)
	{
		TargetYawDeg = DesiredWorldDir.Rotation().Yaw;
		if (!IsMovingOnGround())
		{
			TurnRateDeg = FMath::RadiansToDegrees(2.5f);
		}
		else if (bSprintRequested)
		{
			TurnRateDeg = SprintTurnRateDeg;
		}
		else if (bWalkMode)
		{
			TurnRateDeg = WalkTurnRateDeg;
		}
		else
		{
			TurnRateDeg = RunTurnRateDeg;
		}
	}

	const float DeltaYawDeg = FMath::FindDeltaAngleDegrees(CurrentYawDeg, TargetYawDeg);
	const float MaxStepDeg = TurnRateDeg * DeltaTime;
	const float ClampedStep = FMath::Clamp(DeltaYawDeg, -MaxStepDeg, MaxStepDeg);
	const float NewYawDeg = FMath::UnwindDegrees(CurrentYawDeg + ClampedStep);

	FRotator NewRot = CharacterOwner->GetActorRotation();
	NewRot.Yaw = NewYawDeg;
	CharacterOwner->SetActorRotation(NewRot);
}

void UKaelMovementComponent::PerformSweptHorizontalMove(const FVector& DeltaXY, float DeltaTime)
{
	if (!UpdatedComponent || DeltaXY.SizeSquared2D() <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	FHitResult Hit(1.0f);
	SafeMoveUpdatedComponent(FVector(DeltaXY.X, DeltaXY.Y, 0.0f), UpdatedComponent->GetComponentQuat(), true, Hit);
	if (Hit.IsValidBlockingHit())
	{
		HandleImpact(Hit, DeltaTime, FVector(DeltaXY.X, DeltaXY.Y, 0.0f));
		SlideAlongSurface(FVector(DeltaXY.X, DeltaXY.Y, 0.0f), 1.0f - Hit.Time, Hit.Normal, Hit, true);
	}
}

void UKaelMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	const float ClampedDt = FMath::Clamp(DeltaTime, 0.0f, 0.05f);
	const bool bGroundedBefore = IsMovingOnGround();

	// Coyote time & jump buffer timers
	CoyoteTimer = bGroundedBefore ? CoyoteTimeDuration : FMath::Max(0.0f, CoyoteTimer - ClampedDt);
	if (JumpBufferTimer > 0.0f)
	{
		JumpBufferTimer = FMath::Max(0.0f, JumpBufferTimer - ClampedDt);
	}
	if (bGroundedBefore)
	{
		bAirDashed = false;
	}

	// Execute buffered jump if within coyote window and not dashing/evading
	if (JumpBufferTimer > 0.0f && CoyoteTimer > 0.0f && DashTimeRemaining <= 0.0f && EvadeTimeRemaining <= 0.0f)
	{
		ExecuteCoyoteJump();
	}

	// Variable jump height cut on release while ascending (player.js line 281)
	if (!IsMovingOnGround() && !bJumpHeld && Velocity.Z > 0.0f && DashTimeRemaining <= 0.0f)
	{
		Velocity.Z *= FMath::Exp(-9.0f * ClampedDt);
	}

	// Update active gait speed ceiling & acceleration
	if (bGuardSlow || bWalkMode)
	{
		MaxWalkSpeed = WalkSpeedCm;
		MaxAcceleration = WalkAccelCm;
	}
	else if (bSprintRequested && !bStrafeActive)
	{
		MaxWalkSpeed = SprintSpeedCm;
		MaxAcceleration = SprintAccelCm;
	}
	else
	{
		MaxWalkSpeed = RunSpeedCm;
		MaxAcceleration = RunAccelCm;
	}
	BrakingDecelerationWalking = GroundDecelCm;

	// Step Evade or Dash state before base tick
	if (EvadeTimeRemaining > 0.0f)
	{
		EvadeTimeRemaining = IsMovingOnGround() ? FMath::Max(0.0f, EvadeTimeRemaining - ClampedDt) : 0.0f;
		const float Alpha = EvadeTimeRemaining / kEvadeDuration;
		const float EvadeSpeed = FMath::Lerp(EvadeEndSpeedCm, EvadeStartSpeedCm, Alpha);
		HorizontalSpeedCm = EvadeSpeed;
		Velocity.X = EvadeDirection.X * EvadeSpeed;
		Velocity.Y = EvadeDirection.Y * EvadeSpeed;
		if (CharacterOwner)
		{
			const float EvadeYawRad = FMath::Atan2(EvadeDirection.Y, EvadeDirection.X);
			const float FacingYawRad = FMath::DegreesToRadians(CharacterOwner->GetActorRotation().Yaw);
			MoveAngleRad = FMath::UnwindRadians(FacingYawRad - EvadeYawRad);
		}
	}
	else if (DashTimeRemaining > 0.0f)
	{
		DashTimeRemaining = FMath::Max(0.0f, DashTimeRemaining - ClampedDt);
		const float SpeedNow = DashTimeRemaining > 0.0f ? DashSpeedCm : RunSpeedCm;
		HorizontalSpeedCm = SpeedNow;
		Velocity.X = DashDirection.X * SpeedNow;
		Velocity.Y = DashDirection.Y * SpeedNow;
		if (!IsMovingOnGround())
		{
			Velocity.Z = FMath::Max(Velocity.Z, 0.0f);
		}
	}
	else
	{
		UpdateActorFacing(ClampedDt);
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Apply decaying swept knockback (never clips through walls)
	if (KnockbackVelocity.SizeSquared2D() > 1.0f)
	{
		PerformSweptHorizontalMove(KnockbackVelocity * ClampedDt, ClampedDt);
		KnockbackVelocity *= FMath::Exp(-KnockbackDecayRate * ClampedDt);
		if (KnockbackVelocity.SizeSquared2D() <= 1.0f)
		{
			KnockbackVelocity = FVector::ZeroVector;
		}
	}

	// Clamp terminal fall speed
	if (!IsMovingOnGround() && Velocity.Z < -MaxFallSpeedCm)
	{
		Velocity.Z = -MaxFallSpeedCm;
	}

	// Update animation telemetry (matching player.js lines 429-442)
	const bool bGroundedAfter = IsMovingOnGround();
	if (!bGroundedAfter)
	{
		AirTime += ClampedDt;
		LastFallSpeedCm = -Velocity.Z;
	}
	else
	{
		AirTime = 0.0f;
		bAirDashed = false;
	}

	const float ActualHorizSpeedCm = Velocity.Size2D();
	HorizontalSpeedCm = ActualHorizSpeedCm;
	const float SpeedMps = ActualHorizSpeedCm * 0.01f;

	if (SpeedMps > (RunSpeedCm * 0.01f) + 0.3f)
	{
		SprintTime += ClampedDt;
	}
	else
	{
		if (SprintTime > 1.5f && SpeedMps < 0.2f)
		{
			TiredTimeRemaining = 6.0f;
		}
		SprintTime = FMath::Max(0.0f, SprintTime - ClampedDt * 0.5f);
	}
	if (TiredTimeRemaining > 0.0f)
	{
		TiredTimeRemaining = FMath::Max(0.0f, TiredTimeRemaining - ClampedDt);
	}

	if (CharacterOwner && ClampedDt > KINDA_SMALL_NUMBER)
	{
		const float CurrentYawDeg = CharacterOwner->GetActorRotation().Yaw;
		// Three.js yaw increases counter-clockwise; negate UE delta so TurnLean sign matches animator.js
		const float DeltaYawRad = -FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(LastYawDeg, CurrentYawDeg));
		TurnRateRadPerSec = DampValue(TurnRateRadPerSec, DeltaYawRad / ClampedDt, 10.0f, ClampedDt);
		ForwardAccelMps2 = DampValue(ForwardAccelMps2, (SpeedMps - PrevSpeedMps) / ClampedDt, 6.0f, ClampedDt);
		LastYawDeg = CurrentYawDeg;
		PrevSpeedMps = SpeedMps;
	}
}

void UKaelMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	if (EvadeTimeRemaining > 0.0f)
	{
		const float Alpha = EvadeTimeRemaining / kEvadeDuration;
		const float Speed = FMath::Lerp(EvadeEndSpeedCm, EvadeStartSpeedCm, Alpha);
		Velocity.X = EvadeDirection.X * Speed;
		Velocity.Y = EvadeDirection.Y * Speed;
		return;
	}

	if (DashTimeRemaining > 0.0f)
	{
		Velocity.X = DashDirection.X * DashSpeedCm;
		Velocity.Y = DashDirection.Y * DashSpeedCm;
		return;
	}

	if (!IsMovingOnGround())
	{
		// Keep momentum with bounded air control (player.js lines 283-288)
		if (InputMagnitude > 0.05f && DesiredWorldDir.SizeSquared() > KINDA_SMALL_NUMBER)
		{
			const float TargetSpeed = FMath::Max(MaxWalkSpeed * InputMagnitude, 150.0f);
			const FVector WantVel = DesiredWorldDir * TargetSpeed;
			Velocity.X = ApproachValue(Velocity.X, WantVel.X, AirControlAccelCm * DeltaTime);
			Velocity.Y = ApproachValue(Velocity.Y, WantVel.Y, AirControlAccelCm * DeltaTime);
		}
		return;
	}

	// Grounded responsive locomotion matching player.js lines 215-241
	const FVector FacingDir = CharacterOwner ? CharacterOwner->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;

	float DesiredSpeed = 0.0f;
	if (InputMagnitude >= 0.05f)
	{
		if (bGuardSlow || bWalkMode)
		{
			DesiredSpeed = WalkSpeedCm * FMath::Min(1.0f, InputMagnitude * 1.4f);
		}
		else if (bSprintRequested && !bStrafeActive)
		{
			DesiredSpeed = SprintSpeedCm;
		}
		else
		{
			DesiredSpeed = RunSpeedCm * InputMagnitude;
		}
	}

	if (bStrafeActive)
	{
		FVector MoveDir = LastMoveDir;
		if (InputMagnitude > 0.05f && DesiredWorldDir.SizeSquared() > KINDA_SMALL_NUMBER)
		{
			MoveDir = DesiredWorldDir;
			LastMoveDir = MoveDir;
			const float FwdDot = FVector::DotProduct(FacingDir, MoveDir);
			const FVector RightDir(-FacingDir.Y, FacingDir.X, 0.0f);
			const float LeftDot = -FVector::DotProduct(RightDir, MoveDir); // + = character's left
			MoveAngleRad = FMath::Atan2(LeftDot, FwdDot);
		}

		const float StrafeCap = ComputeStrafeSpeedCapCm(MoveAngleRad);
		const float TargetSpeed = FMath::Min(DesiredSpeed, StrafeCap);
		const float CurSpeed = Velocity.Size2D();
		const float Rate = TargetSpeed > CurSpeed ? RunAccelCm : GroundDecelCm;
		const float NextSpeed = ApproachValue(CurSpeed, TargetSpeed, Rate * DeltaTime);
		Velocity.X = MoveDir.X * NextSpeed;
		Velocity.Y = MoveDir.Y * NextSpeed;
	}
	else
	{
		float OffRad = 0.0f;
		if (InputMagnitude > 0.05f && DesiredWorldDir.SizeSquared() > KINDA_SMALL_NUMBER)
		{
			const float CosA = FMath::Clamp(FVector::DotProduct(FacingDir, DesiredWorldDir), -1.0f, 1.0f);
			OffRad = FMath::Acos(CosA);
		}

		// Carve ordinary turns at speed; only near-reversals slow him (player.js line 236)
		const float TurnSlowdown = (OffRad < 1.0f) ? 1.0f : FMath::Clamp(1.45f - OffRad / 2.2f, 0.4f, 1.0f);
		const float TargetSpeed = DesiredSpeed * TurnSlowdown;
		const float CurSpeed = Velocity.Size2D();
		const float AccelRate = bSprintRequested ? SprintAccelCm : (bWalkMode ? WalkAccelCm : RunAccelCm);
		const float Rate = TargetSpeed > CurSpeed ? AccelRate : GroundDecelCm;
		const float NextSpeed = ApproachValue(CurSpeed, TargetSpeed, Rate * DeltaTime);

		MoveAngleRad = 0.0f;
		LastMoveDir = FacingDir;
		Velocity.X = FacingDir.X * NextSpeed;
		Velocity.Y = FacingDir.Y * NextSpeed;
	}
}

void UKaelMovementComponent::PhysWalking(float DeltaTime, int32 Iterations)
{
	Super::PhysWalking(DeltaTime, Iterations);
}

void UKaelMovementComponent::PhysFalling(float DeltaTime, int32 Iterations)
{
	if (DashTimeRemaining > 0.0f)
	{
		// During an air dash, hold height and sweep horizontally
		Velocity.Z = FMath::Max(Velocity.Z, 0.0f);
		PerformSweptHorizontalMove(FVector(DashDirection.X, DashDirection.Y, 0.0f) * DashSpeedCm * DeltaTime, DeltaTime);
		return;
	}
	Super::PhysFalling(DeltaTime, Iterations);
}
