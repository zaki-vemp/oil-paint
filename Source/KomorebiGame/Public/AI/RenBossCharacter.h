// Copyright Komorebi Project. Mirror Rival boss character Ren driving all 6 duels via KomorebiCore::DuelState.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "KomorebiGameTypes.h"
#include "Game/DuelStateMachine.h"
#include "RenBossCharacter.generated.h"

class UStaticMeshComponent;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class UBladeTrailComponent;
class AKaelCharacter;
class AKomorebiTerrainActor;

UENUM(BlueprintType)
enum class ERenDuelPhase : uint8
{
	Waiting,
	Intro,
	Approach,
	Guard,
	Sidestep,
	Windup,
	Active,
	Recovery
};

UENUM(BlueprintType)
enum class ERenDuelEvent : uint8
{
	None,
	Intro,
	Reset,
	Windup,
	Strike,
	Opening
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnRenDuelEventSignature, int32, DuelIndex, ERenDuelEvent, Event, const FString&, BossName, const FString&, Lesson);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnRenDuelCompletedSignature, int32, DuelIndex, const FString&, BossName, const FString&, Lesson);

/**
 * Mirror Rival boss character driving all 6 chapter duels via KomorebiCore::DuelState.
 * Owns Rival katana + Crimson UBladeTrailComponent, terrain-conforming arena ring mesh,
 * dynamic danger/opening telegraph arc sector mesh matching duelVisual(), aura ring, and duel beacon.
 */
UCLASS(Blueprintable)
class KOMOREBIGAME_API ARenBossCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ARenBossCharacter();

	static constexpr float kArenaOuterRadiusCm = static_cast<float>(KomorebiCore::kArenaRadius * 100.0);
	static constexpr float kArenaInnerRadiusCm = static_cast<float>((KomorebiCore::kArenaRadius - 0.54) * 100.0);

	// --- Equipment & Visual Telegraph Components ---

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Boss")
	TObjectPtr<UStaticMeshComponent> RivalKatanaMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Boss")
	TObjectPtr<UBladeTrailComponent> BladeTrailComponent;

	/** Terrain-conforming outer arena ring at kArenaRadius (8.6m). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Boss")
	TObjectPtr<UProceduralMeshComponent> ArenaRingMesh;

	/** Dynamic danger/opening arc sector mesh matching KomorebiCore::GetDuelVisual(). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Boss")
	TObjectPtr<UProceduralMeshComponent> TelegraphSectorMesh;

	/** Expanding charge aura ring around Ren during windup/active/opening. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Boss")
	TObjectPtr<UProceduralMeshComponent> AuraRingMesh;

	/** Tall beacon column visible when ready to duel. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Boss")
	TObjectPtr<UProceduralMeshComponent> BeaconMesh;

	// --- Configuration & Replicated Summary ---

	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "Komorebi|Boss")
	int32 DuelIndex = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Boss")
	float ReplicatedHp = 120.0f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Boss")
	float ReplicatedMaxHp = 120.0f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Boss")
	bool bIsDead = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Boss")
	ERenDuelPhase ReplicatedPhase = ERenDuelPhase::Waiting;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Boss")
	FName KatanaSocketName = TEXT("hand_l");

	// --- Delegates ---

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Boss|Events")
	FOnRenDuelEventSignature OnDuelEvent;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Boss|Events")
	FOnRenDuelCompletedSignature OnDuelCompleted;

	// --- Public API ---

	/** Initializes KomorebiCore::DuelState for chapter index (0..5), places Ren & builds arena geometry. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Boss")
	void InitializeDuel(int32 InDuelIndex);

	/** Steps KomorebiCore::StepDuel(), resolves DuelHit() against player, and updates telegraph/blade meshes. */
	void TickDuel(float DeltaTime, AKaelCharacter* PlayerCharacter, bool bInReady, bool bGateBlocked, bool bFreeRoam = false);

	/** Applies guard/punish/guard-break rules via KomorebiCore::DamageRival() and returns final integer damage. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Boss")
	int32 ReceivePlayerDamage(float RawAmount, FName Source);

	/** Resets duel state to Waiting (called on player death or roam respawn). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Boss")
	void ResetDuel(bool bReviveIfDead = false);

	// --- Queries ---

	UFUNCTION(BlueprintPure, Category = "Komorebi|Boss")
	int32 GetDuelIndex() const { return DuelIndex; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Boss")
	FString GetBossName() const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|Boss")
	FString GetBossLesson() const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|Boss")
	FString GetCurrentMoveCue() const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|Boss")
	float GetHp() const { return static_cast<float>(CoreState.Hp); }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Boss")
	float GetMaxHp() const { return static_cast<float>(CoreState.MaxHp); }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Boss")
	bool IsDead() const { return CoreState.Dead; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Boss")
	bool IsReady() const { return CoreState.Ready; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Boss")
	bool IsEngaged() const { return CoreState.Ready && !CoreState.Dead && CoreState.Phase != KomorebiCore::EDuelPhase::Waiting; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Boss")
	ERenDuelPhase GetDuelPhase() const { return static_cast<ERenDuelPhase>(CoreState.Phase); }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Boss")
	float GetWindupRemaining() const { return static_cast<float>(CoreState.Windup); }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Boss")
	float GetCurrentMoveReachMeters() const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|Boss")
	FVector GetFeetWorldLocation() const;

	const KomorebiCore::DuelState& GetCoreState() const { return CoreState; }

	// --- ACharacter Overrides ---

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	void BuildArenaRingMesh();
	void BuildBeaconMesh();
	void UpdateTelegraphSectorMesh(const KomorebiCore::DuelVisualState& Vis, float GroundZ, float Alpha);
	void UpdateAuraRingMesh(const KomorebiCore::DuelVisualState& Vis, float RadiusCm, float GroundZ, float Alpha);
	void UpdateVisualsAndBlade(float DeltaTime, AKaelCharacter* PlayerCharacter);
	void SyncActorTransformFromCoreState(float SpiralExtraYawDeg = 0.0f);
	float QueryGroundHeightCm(float WorldXCm, float WorldYCm, float FallbackZCm) const;

	KomorebiCore::DuelState CoreState{};
	bool bInitialized = false;
	bool bArenaBuilt = false;
	bool bBeaconBuilt = false;
	bool bDeadBroadcasted = false;

	float DuelSpinAccum = 0.0f;
	float FlashTimer = 0.0f;
	float DeadElapsed = 0.0f;
	float RespawnTimer = 0.0f;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DynamicMaterials;
};
