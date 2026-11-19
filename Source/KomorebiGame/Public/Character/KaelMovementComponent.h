// Copyright Komorebi Project. Kael kinematic/character movement with coyote time, jump buffer, swept ground/air dash, quickstep evade, plunge slam, and decaying swept knockback.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KaelMovementComponent.generated.h"

/**
 * Character movement component porting legacy-web/src/figure/player.js TUNING and swept impulse behaviors to UE5.
 * Units are centimetres and degrees (1 m = 100 cm).
 */
UCLASS(ClassGroup = (Komorebi), meta = (BlueprintSpawnableComponent))
class KOMOREBIGAME_API UKaelMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UKaelMovementComponent(const FObjectInitializer& ObjectInitializer);

	// --- TUNING constants matching legacy-web/src/figure/player.js & animator.js ---

	static constexpr float kCapsuleRadiusCm = 28.0f;
	static constexpr float kCapsuleHalfHeightCm = 90.0f;
	static constexpr float kCapsuleHeightCm = 180.0f;
	static constexpr float kEvadeDuration = 0.28f;
	static constexpr float kDashDuration = 0.24f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float WalkSpeedCm = 145.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float RunSpeedCm = 530.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float SprintSpeedCm = 700.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float StrafeWalkSpeedCm = 145.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float StrafeJogSpeedCm = 360.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float WalkAccelCm = 1200.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float RunAccelCm = 1900.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float SprintAccelCm = 1600.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float GroundDecelCm = 2400.0f;

	/** Turn rates in deg/s (walk: 16 rad/s, run: 14 rad/s, sprint: 11 rad/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float WalkTurnRateDeg = 916.7325f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float RunTurnRateDeg = 802.1409f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float SprintTurnRateDeg = 630.2536f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float StrafeTurnRateDeg = 916.7325f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float JumpHeightCm = 125.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float RiseGravityCm = 2200.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float FallGravityCm = 3000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float AirControlAccelCm = 800.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float MaxFallSpeedCm = 4000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float CoyoteTimeDuration = 0.12f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float JumpBufferDuration = 0.14f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float DashSpeedCm = 1700.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float EvadeStartSpeedCm = 1200.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float EvadeEndSpeedCm = 300.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Komorebi|Movement")
	float KnockbackDecayRate = 9.0f;

	// --- Movement API ---

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Movement")
	void SetGaitInputs(bool bInWalk, bool bInSprint, bool bInStrafe, bool bInGuard, float InInputMagnitude, const FVector& InDesiredWorldDir, float InAimYawDeg, bool bInHasCombatAim);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Movement")
	void RequestJumpBuffered();

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Movement")
	void SetJumpHeld(bool bHeld);

	/** Starts a 0.24s swept dash along WorldDir. Permits 1 mid-air dash holding Z until landing. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Movement")
	bool StartDash(const FVector& WorldDir);

	/** Starts a 0.28s grounded quickstep evade along WorldDir. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Movement")
	bool StartEvade(const FVector& WorldDir);

	/** Drops Kael straight down while airborne (plunge attack), shedding 70% of horizontal drift. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Movement")
	void StartPlungeSlam(float DownwardSpeedCm = 2600.0f);

	/** Applies a decaying swept horizontal knockback push (cm/s) that slides cleanly along walls. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Movement")
	void ApplyKnockbackImpulse(const FVector& Direction, float StrengthCmPerSec);

	/** Clears velocities, dash/evade/knockback state, and jump/coyote timers (used on teleports/respawns). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Movement")
	void ResetMovementState();

	// --- Telemetry for Animation & Combat ---

	UFUNCTION(BlueprintPure, Category = "Komorebi|Movement")
	bool IsDashing() const { return DashTimeRemaining > 0.0f; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Movement")
	bool HasAirDashed() const { return bAirDashed; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Movement")
	bool IsEvading() const { return EvadeTimeRemaining > 0.0f; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Movement")
	float GetEvadeTimeRemaining() const { return EvadeTimeRemaining; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Movement")
	float GetEvadeAngleRad() const { return MoveAngleRad; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Movement")
	bool IsStrafing() const { return bStrafeActive; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Movement")
	float GetTurnRateRadPerSec() const { return TurnRateRadPerSec; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Movement")
	float GetForwardAccelMps2() const { return ForwardAccelMps2; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Movement")
	float GetMoveAngleRad() const { return MoveAngleRad; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Movement")
	float GetAirTime() const { return AirTime; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Movement")
	float GetLastFallSpeedCm() const { return LastFallSpeedCm; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Movement")
	float GetSprintTime() const { return SprintTime; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Movement")
	bool IsTired() const { return TiredTimeRemaining > 0.0f; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Movement")
	FVector GetKnockbackVelocity() const { return KnockbackVelocity; }

	// --- UCharacterMovementComponent Overrides ---

	virtual void InitializeComponent() override;
	virtual float GetGravityZ() const override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual void PhysWalking(float DeltaTime, int32 Iterations) override;
	virtual void PhysFalling(float DeltaTime, int32 Iterations) override;

protected:
	void PerformSweptHorizontalMove(const FVector& DeltaXY, float DeltaTime);
	void ExecuteCoyoteJump();
	void UpdateActorFacing(float DeltaTime);
	float ComputeStrafeSpeedCapCm(float AngleRad) const;

	// Gait & input snapshot
	bool bWalkMode = false;
	bool bSprintRequested = false;
	bool bStrafeActive = false;
	bool bGuardSlow = false;
	bool bJumpHeld = false;
	bool bHasCombatAim = false;
	float InputMagnitude = 0.0f;
	float AimYawDeg = 0.0f;
	FVector DesiredWorldDir = FVector::ZeroVector;
	FVector LastMoveDir = FVector::ForwardVector;

	// Coyote / jump buffer
	float CoyoteTimer = 0.0f;
	float JumpBufferTimer = 0.0f;

	// Dash / evade / knockback
	float DashTimeRemaining = 0.0f;
	FVector DashDirection = FVector::ForwardVector;
	bool bAirDashed = false;

	float EvadeTimeRemaining = 0.0f;
	FVector EvadeDirection = FVector::ForwardVector;

	FVector KnockbackVelocity = FVector::ZeroVector;

	// Animation telemetry
	float HorizontalSpeedCm = 0.0f;
	float PrevSpeedMps = 0.0f;
	float LastYawDeg = 0.0f;
	float TurnRateRadPerSec = 0.0f;
	float ForwardAccelMps2 = 0.0f;
	float MoveAngleRad = 0.0f;
	float AirTime = 0.0f;
	float LastFallSpeedCm = 0.0f;
	float SprintTime = 0.0f;
	float TiredTimeRemaining = 0.0f;
};
