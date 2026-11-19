// Copyright Komorebi Project. World subsystem orchestrating crew tactics via KomorebiCore::ComputeCrewOrder(), Heat & 3-Star escalation, witness mob aggro, segment clears, and respawns.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "KomorebiGameTypes.h"
#include "Game/LevelsData.h"
#include "Game/StreetConfig.h"
#include "Game/TacticsRules.h"
#include "EnemySquadSubsystem.generated.h"

class AActor;
class AKaelCharacter;
class AStreetEnemyCharacter;
class ARenBossCharacter;
class AKomorebiTerrainActor;
class UMissionSubsystem;
class UKomorebiAudioSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnSquadSegmentCleared, int32, SegmentIndex, int32, TotalClearedCount, EKomorebiPower, GrantedPower);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSquadHeatChanged, float, Heat, int32, HeatStars);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSquadToastMessage, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSquadFlashMessage, const FString&, Text);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSquadAudioCue, FName, CueName, float, Scale);

/**
 * Tickable WorldSubsystem running KomorebiCore::ComputeCrewOrder() each frame across active street crews,
 * managing Heat (0..100) and 3-Star Police/Roadblock escalation, witness mob aggro (MOB.radius=34m, MOB.time=30s),
 * 6-segment clear detection, sidewalk crowd pool recycling, and free-roam (20s) / civilian (45s) respawns.
 */
UCLASS()
class KOMOREBIGAME_API UEnemySquadSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static constexpr float kCrewRespawnSeconds = 20.0f;
	static constexpr float kCivRespawnSeconds = 45.0f;
	static constexpr float kSidewalkRecycleInterval = 0.9f;
	static constexpr float kSidewalkRecycleDistanceMeters = 52.0f;

	// --- Delegates ---

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Squad|Events")
	FOnSquadSegmentCleared OnSegmentCleared;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Squad|Events")
	FOnSquadHeatChanged OnHeatChanged;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Squad|Events")
	FOnSquadToastMessage OnToast;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Squad|Events")
	FOnSquadFlashMessage OnFlashText;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Squad|Events")
	FOnSquadAudioCue OnAudioCue;

	// --- Subsystem Lifecycle ---

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// --- Configuration & Registration ---

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Squad")
	void ConfigureSession(EKomorebiWorldScene InScene, EKomorebiPlayMode InMode, int32 InSavedClearedCount = 0);

	/** Optional predicate allowing UMissionSubsystem to require sector mission objectives before clearing a segment. */
	void SetSectorMissionDonePredicate(TFunction<bool(int32)> InPredicate);

	void RegisterEnemy(AStreetEnemyCharacter* Enemy);
	void UnregisterEnemy(AStreetEnemyCharacter* Enemy);

	void RegisterRival(ARenBossCharacter* Rival);
	void UnregisterRival(ARenBossCharacter* Rival);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Squad")
	void RegisterRelayLocation(const FVector& WorldLocation);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Squad")
	void ClearRelayLocations();

	/** Optional class overrides for spawning Police enforcers, campaign crews, and Ren bosses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Squad")
	TSubclassOf<AStreetEnemyCharacter> StreetEnemyClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Squad")
	TSubclassOf<ARenBossCharacter> RenBossClass;

	/** Spawns all 6 campaign segments' crews, 6 Mirror Rivals, and 40 sidewalk crowd entities (optional helper). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Squad")
	void SpawnCampaignPopulation(int32 AlreadyClearedCount = 0);

	// --- Heat, Witness Mob & Police Escalation ---

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Squad|Heat")
	void AddHeat(float DeltaHeat);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Squad|Heat")
	void CoolHeat(float DeltaHeat);

	/** Reports an unprovoked bystander hit or kill: adds Heat and turns witnesses with LOS within 34m hostile for 30s. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Squad|Heat")
	int32 ReportBystanderCrime(const FVector& CrimeWorldLocation, AStreetEnemyCharacter* Victim, bool bKilled, AActor* Offender);

	UFUNCTION(BlueprintPure, Category = "Komorebi|Squad|Heat")
	float GetHeat() const { return Heat; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Squad|Heat")
	int32 GetHeatStars() const { return HeatStars; }

	// --- Segment Progress & Reset ---

	UFUNCTION(BlueprintPure, Category = "Komorebi|Squad")
	bool IsSegmentAlive(int32 SegmentIndex) const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|Squad")
	bool IsSegmentDone(int32 SegmentIndex) const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|Squad")
	int32 GetClearedSegmentCount() const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|Squad")
	int32 GetRemainingHostileCount() const;

	/** True when a closed campaign gate lies between PlayerWorld and TargetWorld. */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Squad")
	bool IsGateBlockingBetween(const FVector& PlayerWorld, const FVector& TargetWorld) const;

	/** Resets street aggro, returns enemies within RadiusCm to home, and resets active duels on player respawn. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Squad")
	void ResetAfterPlayerDeath(const FVector& RespawnWorldLocation, float ClearRadiusCm = 500.0f);

	/** Returns the currently engaged Mirror Rival within 18m of the player, if any. */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Squad")
	ARenBossCharacter* GetActiveEngagedBoss() const;

	/** True when any hostile crew member or rival is actively chasing/fighting near the player (<24m). */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Squad")
	bool IsPlayerInActiveCombat() const;

	const TArray<TWeakObjectPtr<AStreetEnemyCharacter>>& GetRegisteredEnemies() const { return RegisteredEnemies; }
	const TArray<TWeakObjectPtr<ARenBossCharacter>>& GetRegisteredRivals() const { return RegisteredRivals; }

	void EmitToast(const FString& Msg);
	void EmitFlashText(const FString& Text);
	void EmitAudioCue(FName CueName, float Scale = 1.0f);

protected:
	AKaelCharacter* FindLocalPlayerCharacter() const;
	bool HasWitnessLineOfSight(const AStreetEnemyCharacter* Witness, const FVector& PlayerFeetWorld) const;
	void StepCrewTacticsOrders(AKaelCharacter* PlayerChar);
	void StepPairwiseSeparationAndChats(float DeltaTime, const FVector& PlayerFeetWorld, bool bFightingNearby);
	void StepSidewalkPoolRecycling(float DeltaTime, const FVector& PlayerFeetWorld);
	void StepHeatAndPoliceEscalation(float DeltaTime, AKaelCharacter* PlayerChar);
	void StepSegmentClearDetection(AKaelCharacter* PlayerChar);
	void SpawnPoliceEnforcer(const FVector& PlayerFeetWorld);
	void SpawnRoadblockAheadOf(AKaelCharacter* PlayerChar);
	void DestroyRoadblock();
	int32 ComputeHeatStars(float CurrentHeat) const;
	float QueryGroundHeightCm(float WorldXCm, float WorldYCm, float ReferenceZCm) const;

	EKomorebiWorldScene WorldScene = EKomorebiWorldScene::NeoKyoto;
	EKomorebiPlayMode PlayMode = EKomorebiPlayMode::StorySectors;

	TArray<TWeakObjectPtr<AStreetEnemyCharacter>> RegisteredEnemies;
	TArray<TWeakObjectPtr<ARenBossCharacter>> RegisteredRivals;
	TArray<TWeakObjectPtr<AStreetEnemyCharacter>> SpawnedPolice;
	TArray<FVector> RelayWorldLocations;
	TWeakObjectPtr<AActor> ActiveRoadblockActor;

	TFunction<bool(int32)> SectorMissionDonePredicate;

	float Heat = 0.0f;
	int32 HeatStars = 0;
	bool bHeatIntroSpoken = false;
	int32 LastKnownClearedCount = 0;
	float SidewalkRecycleTimer = 0.0f;
};
