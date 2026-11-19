// Copyright Komorebi Project. Kael & Ren AnimInstance implementation porting animator.js and combat.js pose sampling.
#include "Character/KaelAnimInstance.h"

#include "Character/KaelMovementComponent.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Game/CombatRules.h"

namespace
{
	float DampAnim(float Current, float Target, float Rate, float Dt)
	{
		return Target + (Current - Target) * FMath::Exp(-Rate * Dt);
	}

	/** Converts character-local Three.js metres (+X left, +Y up, +Z forward) to UE Actor-local cm (+X forward, +Y right, +Z up). */
	FVector ThreeCharLocalMetersToUECm(double ThreeX, double ThreeY, double ThreeZ)
	{
		return FVector(
			static_cast<float>(ThreeZ * 100.0),
			static_cast<float>(-ThreeX * 100.0),
			static_cast<float>(ThreeY * 100.0));
	}

	/** Converts character-local Three.js direction (+X left, +Y up, +Z forward) to UE Actor-local direction (+X forward, +Y right, +Z up). */
	FVector ThreeCharLocalDirToUE(double ThreeX, double ThreeY, double ThreeZ)
	{
		return FVector(
			static_cast<float>(ThreeZ),
			static_cast<float>(-ThreeX),
			static_cast<float>(ThreeY)).GetSafeNormal();
	}
}

UKaelAnimInstance::UKaelAnimInstance()
{
}

void UKaelAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	UpdateCombatPose(0.0f, -1.0f, EKomorebiAttackType::Cut, 0.0f, 0.0f, false, 0.0f, 0.0f, 0.0f, false);
}

void UKaelAnimInstance::SetExternalLocomotion(
	float InGroundSpeedCm,
	float InMoveAngleRad,
	bool bInGrounded,
	bool bInStrafing,
	bool bInDriving,
	bool bInDowned)
{
	bUseExternalLocomotion = true;
	GroundSpeed = FMath::Max(0.0f, InGroundSpeedCm);
	NormalizedSpeed = FMath::Clamp(GroundSpeed / 700.0f, 0.0f, 1.0f);
	MoveDirectionDeg = FMath::RadiansToDegrees(InMoveAngleRad);
	bIsGrounded = bInGrounded;
	bIsStrafing = bInStrafing;
	bIsDriving = bInDriving;
	bIsDowned = bInDowned;
}

void UKaelAnimInstance::ResetFootIK()
{
	LeftFootOffset = 0.0f;
	RightFootOffset = 0.0f;
	PelvisOffset = 0.0f;
	LeftFootSlopeRot = FRotator::ZeroRotator;
	RightFootSlopeRot = FRotator::ZeroRotator;
}

void UKaelAnimInstance::UpdateCameraLookAt(float DeltaSeconds, const FVector& WorldCameraForward)
{
	const APawn* OwnerPawn = TryGetPawnOwner();
	if (!OwnerPawn || DeltaSeconds <= 0.0f)
	{
		return;
	}

	const float SpeedMps = GroundSpeed * 0.01f;
	const float Weight = bIsGrounded ? FMath::Clamp(1.0f - (SpeedMps - 1.5f) * 0.5f, 0.0f, 1.0f) : 0.0f;

	const FVector LocalDir = OwnerPawn->GetActorTransform().InverseTransformVectorNoScale(WorldCameraForward).GetSafeNormal();
	const float RawYawRad = FMath::Atan2(LocalDir.Y, LocalDir.X);
	const bool bLookingBack = FMath::Abs(RawYawRad) > 1.9f;
	const float ClampedYaw = FMath::Clamp(RawYawRad, -1.0f, 1.0f);
	const float ClampedPitch = FMath::Clamp(FMath::Asin(FMath::Clamp(LocalDir.Z, -1.0f, 1.0f)) * 0.5f, -0.2f, 0.3f);

	LookYawRad = DampAnim(LookYawRad, bLookingBack ? 0.0f : ClampedYaw * Weight, 5.0f, DeltaSeconds);
	LookPitchRad = DampAnim(LookPitchRad, bLookingBack ? 0.0f : (ClampedPitch + 0.08f) * Weight, 5.0f, DeltaSeconds);

	LookYawDeg = FMath::RadiansToDegrees(LookYawRad);
	LookPitchDeg = FMath::RadiansToDegrees(LookPitchRad);
}

void UKaelAnimInstance::UpdateCombatPose(
	float DeltaSeconds,
	float AttackU,
	EKomorebiAttackType AttackType,
	float HurtPose,
	float CastPose,
	bool bGuarding,
	float GuardImpact,
	float EvadeTimeRemaining,
	float EvadeAngleRad,
	bool bLanternInRightHand)
{
	const float Dt = FMath::Clamp(DeltaSeconds, 0.0f, 0.05f);
	ActiveAttackType = AttackType;
	ActiveAttackU = AttackU;
	bRightHandLanternMode = bLanternInRightHand;

	const KomorebiCore::EAttackType CoreType = KomorebiBridge::ToCoreAttackType(AttackType);
	KomorebiCore::AttackPoseFrame Frame = (AttackU >= 0.0f)
		? KomorebiCore::SampleAttack(CoreType, static_cast<double>(AttackU))
		: KomorebiCore::SampleAttack(KomorebiCore::EAttackType::Cut, 0.0);

	AttackWeight = (AttackU >= 0.0f)
		? FMath::Sin(PI * FMath::Min(1.0f, AttackU) * 0.9f)
		: 0.0f;

	// Exponential guard blend (combat.js line 249)
	const float GuardTarget = bGuarding ? 1.0f : 0.0f;
	GuardBlend += (GuardTarget - GuardBlend) * (1.0f - FMath::Exp(-20.0f * Dt));
	const float Brace = (AttackU < 0.0f) ? GuardBlend : 0.0f;

	// Blend sampled pose toward kGuardPose when bracing
	for (int32 i = 0; i < 3; ++i)
	{
		Frame.Grip[i] = FMath::Lerp(Frame.Grip[i], KomorebiCore::kGuardPose.Grip[i], static_cast<double>(Brace));
		Frame.Blade[i] = FMath::Lerp(Frame.Blade[i], KomorebiCore::kGuardPose.Blade[i], static_cast<double>(Brace));
	}
	Frame.TrunkTurn = FMath::Lerp(Frame.TrunkTurn, KomorebiCore::kGuardPose.TrunkTurn, static_cast<double>(Brace));

	// Guard impact recoil & quickstep evade duck (combat.js lines 252-255)
	Frame.Grip[1] -= static_cast<double>(Brace * GuardImpact * 0.11f);
	Frame.Grip[2] -= static_cast<double>(Brace * GuardImpact * 0.15f);

	EvadeDuck = (EvadeTimeRemaining > 0.0f)
		? FMath::Sin(PI * FMath::Clamp(EvadeTimeRemaining / UKaelMovementComponent::kEvadeDuration, 0.0f, 1.0f))
		: 0.0f;
	Frame.Grip[1] -= static_cast<double>(EvadeDuck * 0.25f);

	// Convert Three.js character-local frame to UE Actor-local cm & deg
	SwordGripLocal = ThreeCharLocalMetersToUECm(Frame.Grip[0], Frame.Grip[1], Frame.Grip[2]);
	SwordBladeDir = ThreeCharLocalDirToUE(Frame.Blade[0], Frame.Blade[1], Frame.Blade[2]);
	SwordLocalRotation = FQuat::FindBetweenNormals(FVector::UpVector, SwordBladeDir).Rotator();

	// Three.js +Y yaw turns left; UE +Z yaw turns right -> negate sign
	TrunkTwistDeg = -FMath::RadiansToDegrees(static_cast<float>(Frame.TrunkTurn));

	const bool bHeavyOrAdvance = (AttackType == EKomorebiAttackType::Heavy || AttackType == EKomorebiAttackType::Advance);
	const float SpinePitchRad =
		(bHeavyOrAdvance ? 0.18f * AttackWeight : 0.04f * AttackWeight)
		- HurtPose * 0.22f
		- Brace * GuardImpact * 0.21f
		+ EvadeDuck * (0.18f + 0.13f * FMath::Cos(EvadeAngleRad));
	const float SpineRollRad = -EvadeDuck * FMath::Sin(EvadeAngleRad) * 0.28f;

	SpinePitchDeg = FMath::RadiansToDegrees(SpinePitchRad);
	SpineRollDeg = FMath::RadiansToDegrees(SpineRollRad);

	// Whirl (Spin) full 360 smoothstep turn over [0..0.9] (hunt.js lines 838-840)
	if (AttackType == EKomorebiAttackType::Spin && AttackU >= 0.0f)
	{
		const float K = FMath::Clamp(AttackU / 0.9f, 0.0f, 1.0f);
		const float Smooth = K * K * (3.0f - 2.0f * K);
		SpinYawOffsetDeg = Smooth * 360.0f;
	}
	else
	{
		SpinYawOffsetDeg = 0.0f;
	}

	// Off-hand (right hand) balance / cast / guard / evade IK target (combat.js lines 264-267)
	bUseOffHandIK = !bRightHandLanternMode && (AttackU >= 0.0f || CastPose > 0.0f || Brace > 0.02f || EvadeDuck > 0.0f);
	const double RightThreeX = -0.4 + Brace * 0.16;
	const double RightThreeY = 1.2 + CastPose * 0.22 + Brace * 0.12 - EvadeDuck * 0.29;
	const double RightThreeZ = (CastPose > 0.0f) ? 0.55 : (-0.12 + Brace * 0.55);
	OffHandTargetLocal = ThreeCharLocalMetersToUECm(RightThreeX, RightThreeY, RightThreeZ);

	// Hip lantern sway (combat.js line 261)
	const float LanternSwayRad = -0.10f - AttackWeight * 0.08f + HurtPose * 0.12f;
	HipLanternSwayDeg = FMath::RadiansToDegrees(LanternSwayRad);
}

void UKaelAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const float Dt = FMath::Clamp(DeltaSeconds, 0.0f, 0.05f);
	const ACharacter* OwnerChar = Cast<ACharacter>(TryGetPawnOwner());
	if (!OwnerChar)
	{
		return;
	}

	if (!bUseExternalLocomotion)
	{
		const UKaelMovementComponent* KaelMove = Cast<UKaelMovementComponent>(OwnerChar->GetCharacterMovement());
		const FVector Vel = OwnerChar->GetVelocity();
		GroundSpeed = Vel.Size2D();
		VerticalVelocity = Vel.Z;

		if (KaelMove)
		{
			NormalizedSpeed = FMath::Clamp(GroundSpeed / FMath::Max(1.0f, KaelMove->SprintSpeedCm), 0.0f, 1.0f);
			MoveDirectionDeg = FMath::RadiansToDegrees(KaelMove->GetMoveAngleRad());
			bIsGrounded = KaelMove->IsMovingOnGround();
			bIsStrafing = KaelMove->IsStrafing();
			bIsDashing = KaelMove->IsDashing();
			bIsEvading = KaelMove->IsEvading();
			AirTime = KaelMove->GetAirTime();
			bIsTired = KaelMove->IsTired();

			// Turn & acceleration lean (animator.js lines 487-497)
			const float SpeedMps = GroundSpeed * 0.01f;
			const bool bFreeMove = bIsGrounded && !bIsStrafing && !bIsDashing && !bIsEvading;
			const float TargetTurnLean = bFreeMove
				? FMath::Clamp(-KaelMove->GetTurnRateRadPerSec() * SpeedMps * 0.035f, -0.22f, 0.22f)
				: 0.0f;
			const float TargetPitchLean = bFreeMove
				? FMath::Clamp(KaelMove->GetForwardAccelMps2() * 0.012f, -0.08f, 0.1f)
				: 0.0f;

			TurnLeanRad = DampAnim(TurnLeanRad, TargetTurnLean, 6.0f, Dt);
			PitchLeanRad = DampAnim(PitchLeanRad, TargetPitchLean, 5.0f, Dt);
			TurnLeanDeg = FMath::RadiansToDegrees(TurnLeanRad);
			PitchLeanDeg = FMath::RadiansToDegrees(PitchLeanRad);
		}
		else
		{
			NormalizedSpeed = FMath::Clamp(GroundSpeed / 700.0f, 0.0f, 1.0f);
			bIsGrounded = OwnerChar->GetCharacterMovement() ? OwnerChar->GetCharacterMovement()->IsMovingOnGround() : true;
		}
	}

	const float SpeedMps = GroundSpeed * 0.01f;
	TiredMix = DampAnim(TiredMix, bIsTired ? 1.0f : 0.0f, 1.2f, Dt);
	FingerCurl = FMath::Lerp(0.42f, 0.72f, FMath::Clamp((SpeedMps - 2.0f) * 0.25f, 0.0f, 1.0f));

	// Advance phase-synced gait cycle by speed / estimated blended stride
	if (bIsGrounded && SpeedMps > 0.05f)
	{
		const float ApproxStrideM = FMath::Lerp(1.35f, 2.45f, NormalizedSpeed);
		GaitPhase = FMath::Fmod(GaitPhase + (SpeedMps / ApproxStrideM) * Dt, 1.0f);
	}

	// Ground raycast foot IK
	const float DesiredIKWeight = (bEnableFootIK && bIsGrounded && !bIsDriving && !bIsDowned) ? 1.0f : 0.0f;
	FootIKAlpha = DampAnim(FootIKAlpha, DesiredIKWeight, 14.0f, Dt);
	UpdateFootIKRaycasts(Dt, FootIKAlpha);
}

bool UKaelAnimInstance::TraceFootGround(
	const FVector& FootWorld,
	const FVector& BallWorld,
	float RootZ,
	float& OutGroundZ,
	FVector& OutNormal) const
{
	const UWorld* World = GetWorld();
	const AActor* OwnerActor = GetOwningActor();
	if (!World || !OwnerActor)
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(KaelFootIK), false, OwnerActor);
	FHitResult HitAnkle;
	FHitResult HitBall;

	const FVector StartA(FootWorld.X, FootWorld.Y, RootZ + 70.0f);
	const FVector EndA(FootWorld.X, FootWorld.Y, RootZ - 70.0f);
	const bool bHitA = World->LineTraceSingleByChannel(HitAnkle, StartA, EndA, ECC_WorldStatic, Params);

	const FVector StartB(BallWorld.X, BallWorld.Y, RootZ + 70.0f);
	const FVector EndB(BallWorld.X, BallWorld.Y, RootZ - 70.0f);
	const bool bHitB = World->LineTraceSingleByChannel(HitBall, StartB, EndB, ECC_WorldStatic, Params);

	if (!bHitA && !bHitB)
	{
		return false;
	}

	if (bHitA && bHitB)
	{
		OutGroundZ = FMath::Max(HitAnkle.ImpactPoint.Z, HitBall.ImpactPoint.Z);
		OutNormal = (HitAnkle.ImpactNormal + HitBall.ImpactNormal).GetSafeNormal();
	}
	else if (bHitA)
	{
		OutGroundZ = HitAnkle.ImpactPoint.Z;
		OutNormal = HitAnkle.ImpactNormal;
	}
	else
	{
		OutGroundZ = HitBall.ImpactPoint.Z;
		OutNormal = HitBall.ImpactNormal;
	}

	return true;
}

void UKaelAnimInstance::UpdateFootIKRaycasts(float DeltaSeconds, float Weight)
{
	const USkeletalMeshComponent* SkelMesh = GetSkelMeshComponent();
	const ACharacter* OwnerChar = Cast<ACharacter>(TryGetPawnOwner());
	if (!SkelMesh || !OwnerChar || Weight <= KINDA_SMALL_NUMBER)
	{
		LeftFootOffset = DampAnim(LeftFootOffset, 0.0f, 18.0f, DeltaSeconds);
		RightFootOffset = DampAnim(RightFootOffset, 0.0f, 18.0f, DeltaSeconds);
		PelvisOffset = DampAnim(PelvisOffset, 0.0f, 12.0f, DeltaSeconds);
		LeftFootSlopeRot = FRotator::ZeroRotator;
		RightFootSlopeRot = FRotator::ZeroRotator;
		return;
	}

	const UCapsuleComponent* Capsule = OwnerChar->GetCapsuleComponent();
	const float RootZ = OwnerChar->GetActorLocation().Z - (Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f);

	auto SolveOneFoot = [&](FName FootBone, FName BallBone, float& InOutOffset, FRotator& OutSlopeRot)
	{
		if (SkelMesh->GetBoneIndex(FootBone) == INDEX_NONE)
		{
			InOutOffset = DampAnim(InOutOffset, 0.0f, 18.0f, DeltaSeconds);
			OutSlopeRot = FRotator::ZeroRotator;
			return;
		}

		const FVector AnkleWorld = SkelMesh->GetSocketLocation(FootBone);
		const FVector BallWorld = (SkelMesh->GetBoneIndex(BallBone) != INDEX_NONE)
			? SkelMesh->GetSocketLocation(BallBone)
			: AnkleWorld + OwnerChar->GetActorForwardVector() * 14.0f;

		float GroundZ = RootZ;
		FVector Normal = FVector::UpVector;
		TraceFootGround(AnkleWorld, BallWorld, RootZ, GroundZ, Normal);

		// Planted vs swing blend matching animator.js lines 566-571 (units in cm)
		const float LiftCm = AnkleWorld.Z - RootZ;
		const float Planted = FMath::Clamp(1.0f - (LiftCm - 11.0f) / 10.0f, 0.0f, 1.0f);
		const float OffPlanted = FMath::Clamp(GroundZ - RootZ, -45.0f, 45.0f);
		const float OffSwing = FMath::Clamp(GroundZ + 10.0f - AnkleWorld.Z, 0.0f, 45.0f);
		const float TargetOffset = (OffPlanted * Planted + OffSwing * (1.0f - Planted)) * Weight;

		InOutOffset = DampAnim(InOutOffset, TargetOffset, 18.0f, DeltaSeconds);

		const float SlopeBlend = FMath::Clamp(1.0f - (LiftCm - 9.0f) / 12.0f, 0.0f, 1.0f) * Weight;
		if (Normal.Z < 0.75f)
		{
			Normal = FMath::Lerp(Normal, FVector::UpVector, 0.5f).GetSafeNormal();
		}
		const FQuat SlopeQuat = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(FVector::UpVector, Normal), SlopeBlend);
		OutSlopeRot = SlopeQuat.Rotator();
	};

	SolveOneFoot(LeftFootBoneName, LeftBallBoneName, LeftFootOffset, LeftFootSlopeRot);
	SolveOneFoot(RightFootBoneName, RightBallBoneName, RightFootOffset, RightFootSlopeRot);

	const float DesiredPelvis = FMath::Min3(0.0f, LeftFootOffset, RightFootOffset);
	PelvisOffset = DampAnim(PelvisOffset, DesiredPelvis, 12.0f, DeltaSeconds);
}
