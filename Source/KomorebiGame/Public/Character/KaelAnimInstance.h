// Copyright Komorebi Project. Kael & Ren AnimInstance exposing locomotion, turn-lean, foot IK raycasts, and procedural sword-pose outputs from KomorebiCore::SampleAttack().
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "KomorebiGameTypes.h"
#include "KaelAnimInstance.generated.h"

class UKaelMovementComponent;

/**
 * Animation instance driving locomotion blend spaces, turn/acceleration lean, two-foot ground raycast IK,
 * and procedural katana/guard/evade/lantern upper-body pose outputs sampled from KomorebiCore::SampleAttack().
 */
UCLASS(Blueprintable, BlueprintType)
class KOMOREBIGAME_API UKaelAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UKaelAnimInstance();

	// --- Bone / Socket Configuration ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|IK")
	FName LeftFootBoneName = TEXT("foot_l");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|IK")
	FName RightFootBoneName = TEXT("foot_r");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|IK")
	FName LeftBallBoneName = TEXT("ball_l");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|IK")
	FName RightBallBoneName = TEXT("ball_r");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|IK")
	bool bEnableFootIK = true;

	// --- Locomotion Outputs ---

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Locomotion")
	float GroundSpeed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Locomotion")
	float NormalizedSpeed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Locomotion")
	float MoveDirectionDeg = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Locomotion")
	float VerticalVelocity = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Locomotion")
	float AirTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Locomotion")
	float GaitPhase = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Locomotion")
	bool bIsGrounded = true;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Locomotion")
	bool bIsStrafing = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Locomotion")
	bool bIsDashing = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Locomotion")
	bool bIsEvading = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Locomotion")
	bool bIsDriving = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Locomotion")
	bool bIsDowned = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Locomotion")
	bool bIsTired = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Locomotion")
	float TiredMix = 0.0f;

	// --- Turn Lean & Posture Outputs ---

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Posture")
	float TurnLeanRad = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Posture")
	float TurnLeanDeg = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Posture")
	float PitchLeanRad = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Posture")
	float PitchLeanDeg = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Posture")
	float LookYawDeg = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Posture")
	float LookPitchDeg = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Posture")
	float FingerCurl = 0.42f;

	// --- Foot IK Outputs (cm & slope rotators) ---

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|IK")
	float LeftFootOffset = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|IK")
	float RightFootOffset = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|IK")
	float PelvisOffset = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|IK")
	FRotator LeftFootSlopeRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|IK")
	FRotator RightFootSlopeRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|IK")
	float FootIKAlpha = 1.0f;

	// --- Procedural Sword & Combat Pose Outputs (UE Actor-local cm / deg) ---

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Combat")
	FVector SwordGripLocal = FVector(12.0f, -34.0f, 98.0f);

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Combat")
	FVector SwordBladeDir = FVector(0.215666f, -0.107833f, -0.970495f);

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Combat")
	FRotator SwordLocalRotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Combat")
	float TrunkTwistDeg = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Combat")
	float SpinePitchDeg = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Combat")
	float SpineRollDeg = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Combat")
	float GuardBlend = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Combat")
	float EvadeDuck = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Combat")
	float AttackWeight = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Combat")
	float SpinYawOffsetDeg = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Combat")
	FVector OffHandTargetLocal = FVector(-12.0f, 40.0f, 120.0f);

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Combat")
	bool bUseOffHandIK = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Combat")
	bool bRightHandLanternMode = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Combat")
	float HipLanternSwayDeg = -5.73f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Combat")
	EKomorebiAttackType ActiveAttackType = EKomorebiAttackType::Cut;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Combat")
	float ActiveAttackU = -1.0f;

	// --- Public Methods ---

	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	/** Samples KomorebiCore::SampleAttack() and blends guard, hurt, cast, and evade offsets matching combat.js. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Combat")
	void UpdateCombatPose(
		float DeltaSeconds,
		float AttackU,
		EKomorebiAttackType AttackType,
		float HurtPose,
		float CastPose,
		bool bGuarding,
		float GuardImpact,
		float EvadeTimeRemaining,
		float EvadeAngleRad,
		bool bLanternInRightHand = false);

	/** Updates head/neck look-at angles from world camera forward direction. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Posture")
	void UpdateCameraLookAt(float DeltaSeconds, const FVector& WorldCameraForward);

	/** Clears smoothed foot IK and pelvis offsets (call on teleport/respawn). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|IK")
	void ResetFootIK();

	/** Drives basic locomotion state externally (used for Ren rivals & online remote proxies). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Locomotion")
	void SetExternalLocomotion(float InGroundSpeedCm, float InMoveAngleRad, bool bInGrounded, bool bInStrafing, bool bInDriving, bool bInDowned);

protected:
	void UpdateFootIKRaycasts(float DeltaSeconds, float Weight);
	bool TraceFootGround(const FVector& FootWorld, const FVector& BallWorld, float RootZ, float& OutGroundZ, FVector& OutNormal) const;

	bool bUseExternalLocomotion = false;
	float LookYawRad = 0.0f;
	float LookPitchRad = 0.0f;
};
