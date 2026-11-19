// Copyright Komorebi Project. Replicated player combat component porting legacy-web/src/game/hunt.js combat, powers, guard/parry, bolts, lock-on, hit-stop, and online downed/revive.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "KomorebiGameTypes.h"
#include "Game/CombatRules.h"
#include "Game/LevelsData.h"
#include "CombatComponent.generated.h"

class AActor;
class AKaelCharacter;
class AStreetEnemyCharacter;
class ARenBossCharacter;
class UKaelMovementComponent;
class UMissionSubsystem;
class UKomorebiAudioSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnKomorebiCombatToast, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnKomorebiCombatFlash, const FString&, Text);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnKomorebiCombatAudio, FName, CueName, float, PitchOrScale);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnKomorebiCombatCombo, int32, ComboCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnKomorebiCombatPlayerDied, bool, bEnteredOnlineDowned);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FOnKomorebiMissionStrike, FVector, WorldLocation, float, RadiusCm, float, Damage, FName, Source, int32, ClearedSegment);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnKomorebiCombatPulseVFX, FVector, WorldLocation, FLinearColor, Color, bool, bGroundRing);

USTRUCT(BlueprintType)
struct FKomorebiCombatFrameInput
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Combat")
	FVector2D MoveInput = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Combat")
	float CameraYawDeg = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Combat")
	bool bAttackPressed = false;

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Combat")
	bool bHeavyPressed = false;

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Combat")
	bool bSparkPressed = false;

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Combat")
	bool bDashPressed = false;

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Combat")
	bool bFlarePressed = false;

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Combat")
	bool bWhirlPressed = false;

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Combat")
	bool bGuardHeld = false;

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Combat")
	bool bEvadePressed = false;

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Combat")
	bool bLockTargetPressed = false;

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Combat")
	bool bNextTargetPressed = false;
};

USTRUCT()
struct FKomorebiActiveBolt
{
	GENERATED_BODY()

	FVector Location = FVector::ZeroVector;
	FVector VelocityCm = FVector::ZeroVector;
	float GravityCm = 700.0f;
	float LifeRemaining = 1.05f;
	float HostileDamage = 0.0f; // > 0 => enemy gunner shot; 0 => player spark / deflected bolt
	TWeakObjectPtr<AActor> Shooter;
};

/**
 * Replicated combat component owning HP (100), 3-charge regenerating Spark, 7-move attack flow + 3-hit combo chain,
 * 5 unlockable powers, Guard/Guard Break/Counter, Parry, Quickstep Evade, Plunge shockwave, Bolt simulation & deflection,
 * Lock-on targeting, Hit-stop & Perfect-Dodge slow-motion, Online Downed/Revive, and Vehicle Crash / Guilt penalties.
 */
UCLASS(ClassGroup = (Komorebi), meta = (BlueprintSpawnableComponent))
class KOMOREBIGAME_API UCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatComponent();

	static constexpr float kOnlineDownedDuration = 15.0f;
	static constexpr float kHitStopTimeScale = 0.12f;
	static constexpr float kSlowMoTimeScale = 0.35f;

	// --- Replicated State ---

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Combat")
	float Hp = 100.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Combat")
	float MaxHp = 100.0f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Combat")
	float SparkPower = 3.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Combat")
	float MaxSparkPower = 3.0f;

	UPROPERTY(Replicated, BlueprintReadWrite, Category = "Komorebi|Combat")
	int32 ClearedSectors = 0;

	UPROPERTY(Replicated, BlueprintReadWrite, Category = "Komorebi|Combat")
	int32 Cash = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Combat")
	int32 ComboCount = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Combat")
	bool bGuarding = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Combat")
	float GuardMeter = 1.0f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Combat")
	float GuardBreakTimer = 0.0f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Combat")
	float CounterWindow = 0.0f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Combat")
	bool bDowned = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Combat")
	float DownedTimeRemaining = 0.0f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Combat")
	EKomorebiAttackType ActiveAttackType = EKomorebiAttackType::Cut;

	/** Normalized swing progress in [0..1], or < 0 when idle. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Combat")
	float SwingU = -1.0f;

	// --- Mode Configuration ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Combat")
	EKomorebiWorldScene WorldScene = EKomorebiWorldScene::NeoKyoto;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Combat")
	EKomorebiPlayMode PlayMode = EKomorebiPlayMode::StorySectors;

	// --- Delegates ---

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Combat|Events")
	FOnKomorebiCombatToast OnToast;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Combat|Events")
	FOnKomorebiCombatFlash OnFlashText;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Combat|Events")
	FOnKomorebiCombatAudio OnAudioCue;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Combat|Events")
	FOnKomorebiCombatCombo OnComboChanged;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Combat|Events")
	FOnKomorebiCombatPlayerDied OnPlayerDied;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Combat|Events")
	FOnKomorebiMissionStrike OnMissionStrike;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Combat|Events")
	FOnKomorebiCombatPulseVFX OnPulseVFX;

	// --- Lifecycle ---

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// --- Per-Frame Stepping (called by AKaelCharacter) ---

	/** Processes lock-on, quickstep evade, and guard before movement; returns whether focus/guard constrains gait. */
	void ProcessPreMovementCombatInput(float DeltaTime, const FKomorebiCombatFrameInput& Input, bool bAllowCombat, bool& OutForceStrafe, bool& OutForceWalk, bool& OutHasCombatAimYaw, float& OutCombatAimYawDeg);

	/** Steps swing timers, melee hit windows, plunge landing, powers, bolts, hit-stop/slow-mo, and downed timer. */
	void StepCombat(float ScaledDeltaTime, float RealDeltaTime, const FKomorebiCombatFrameInput& Input, bool bAllowCombat);

	// --- Combat Interactions ---

	/** Incoming damage from an enemy strike, rival duel move, gunner bolt, or online PvP hit. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Combat")
	bool ReceiveEnemyHit(float Damage, AActor* Attacker, float ShoveMps = 5.0f, bool bUnblockable = false);

	/** Vehicle crash damage and camera jolt (hunt.js crash/jolt). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Combat")
	void ApplyVehicleCrashDamage(float Damage, float CameraJolt = 0.16f);

	/** Guilt HP/Heat cost for carjacking or harming an unprovoked citizen (hunt.js blame/carjack). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Combat")
	void ApplyGuiltAndCarjack(bool bIsCarjack, bool bWasKilled = false, bool bWasProvoked = false);

	/** Spawns a hostile gunner bolt simulated and deflectable inside this component. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Combat")
	void SpawnHostileBolt(const FVector& Origin, const FVector& VelocityCmPerSec, float Damage, AActor* Shooter);

	/** Restores HP (capped at MaxHp). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Combat")
	void Heal(float Amount);

	/** Refills HP and Spark to full (e.g. entering a Mirror Duel or touching a relay). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Combat")
	void RefillFullResources();

	/** Awards cash and optional spark refund with HUD feedback. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Combat")
	void GrantReward(int32 CashDelta, float SparkDelta, const FString& FlashLabel, const FString& ToastLabel);

	/** Online teammate revive: restores 45 HP and 1.5s invulnerability. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Combat")
	bool TryRevive();

	/** Triggers full death/respawn (or online downed state if OnlineCity and !bForceRespawn). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Combat")
	void TriggerDeath(bool bForceRespawn = false);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Combat")
	void ClearFocus();

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Combat")
	void AddCameraKick(float Amount);

	// --- Queries ---

	UFUNCTION(BlueprintPure, Category = "Komorebi|Combat")
	bool HasPower(EKomorebiPower Power) const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|Combat")
	bool IsOnlineMode() const { return PlayMode == EKomorebiPlayMode::OnlineCity; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Combat")
	bool IsRoamMode() const { return PlayMode == EKomorebiPlayMode::FreeRoam || PlayMode == EKomorebiPlayMode::OnlineCity; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Combat")
	bool IsInAttackHitWindow() const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|Combat")
	bool IsInAttackWindup() const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|Combat")
	float GetTimeScale() const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|Combat")
	float GetCameraKick() const { return CameraKick; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Combat")
	float GetHurtPose() const { return HurtPose; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Combat")
	float GetCastPose() const { return CastPose; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Combat")
	float GetGuardImpact() const { return GuardImpact; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Combat")
	float GetDashCooldown() const { return DashCooldown; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Combat")
	float GetFlareCooldown() const { return FlareCooldown; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Combat")
	float GetEvadeCooldown() const { return EvadeCooldown; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Combat")
	AActor* GetFocusTarget() const { return FocusTarget.Get(); }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Combat")
	AActor* GetVisibleFocusTarget() const { return (FocusBlockedTimer <= 0.0f) ? FocusTarget.Get() : nullptr; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Combat")
	bool IsInvulnerable() const { return InvulnTimer > 0.0f; }

	const TArray<FKomorebiActiveBolt>& GetActiveBolts() const { return ActiveBolts; }

	/** Increments combo counter and applies hit-stop/camera kick when a player blow connects. */
	void NotifyPlayerHitConnected(FName Source, bool bGuardedTarget, bool bIsBrute);

protected:
	void UpdateLockOn(float DeltaTime, const FKomorebiCombatFrameInput& Input, bool bAllowFocus);
	bool IsTargetViable(AActor* Candidate, float MaxRangeMeters) const;
	bool HasLineOfSightTo(AActor* Candidate) const;

	void TryStartWhirl();
	void TryConsumeAttackBuffer(const FKomorebiCombatFrameInput& Input);
	void EvaluateMeleeHitWindow();
	void ExecutePlungeShockwave();
	void TryStartDash(const FKomorebiCombatFrameInput& Input);
	void TryFireSpark();
	void TryCastFlare();
	void CheckPerfectDodge(bool bIsQuickstepEvade);
	void StepActiveBolts(float DeltaTime);
	void ApplyGlobalTimeDilation();

	float GetCurrentSwingMultiplier() const;
	FName GetCurrentSwingSource() const;
	FVector GetOwnerFeetWorld() const;
	float GetOwnerYawDeg() const;
	float QueryGroundHeightCm(const FVector& WorldPos) const;

	void EmitToast(const FString& Msg);
	void EmitFlash(const FString& Text);
	void EmitAudio(FName Cue, float Scale = 1.0f);

	// Combat internal state
	bool bSwingHeavy = false;
	bool bQueuedHeavy = false;
	bool bPlungeActive = false;
	int32 ComboIndex = -1;
	float ComboWindowTimer = 0.0f;
	float ComboDecayTimer = 0.0f;
	float AttackBufferTimer = 0.0f;

	float InvulnTimer = 0.0f;
	float DashCooldown = 0.0f;
	float FlareCooldown = 0.0f;
	float EvadeCooldown = 0.0f;
	float LockToastCooldown = 0.0f;

	float GuardAge = 0.0f;
	float GuardImpact = 0.0f;
	float HurtPose = 0.0f;
	float CastPose = 0.0f;

	float HitPauseTimer = 0.0f;
	float SlowMoTimer = 0.0f;
	float CameraKick = 0.0f;
	float LastAppliedTimeScale = 1.0f;

	TWeakObjectPtr<AActor> FocusTarget;
	float FocusBlockedTimer = 0.0f;

	TSet<TWeakObjectPtr<AActor>> StruckThisSwing;
	TArray<FKomorebiActiveBolt> ActiveBolts;
};
