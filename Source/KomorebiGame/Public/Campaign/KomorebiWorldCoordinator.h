// Copyright Komorebi Project. Central tickable WorldSubsystem wiring Terrain, City/Snowbound, Kael, Combat,
// EnemySquad, Ren Duels, Vehicles/Traffic, Missions/Echoes, DayNight, Showcase, Audio, Graphics, and Cyberpunk HUD.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "KomorebiGameTypes.h"
#include "Campaign/ShowcaseCameraDirector.h"
#include "KomorebiWorldCoordinator.generated.h"

class AKaelCharacter;
class AKomorebiHUD;
class AKomorebiTerrainActor;
class AKomorebiVehiclePawn;
class ANeoKyotoCityActor;
class AStreetPropsActor;
class ASnowboundWorldActor;
class ADayNightAtmosphereActor;
class AShowcaseCameraDirector;
class UCombatComponent;
class UEnemySquadSubsystem;
class UMissionSubsystem;
class UTrafficSubsystem;
class UKomorebiAudioSubsystem;
class UGraphicsSettingsSubsystem;

/**
 * Runtime glue subsystem matching the orchestration loop of legacy-web/src/main.js and src/night/main.js.
 * Automatically discovers level actors & subsystems on OnWorldBeginPlay, connects all cross-subsystem
 * gameplay delegates, drives per-frame environment/audio/HUD synchronization, and keeps zero circular
 * header coupling between individual gameplay systems.
 */
UCLASS()
class KOMOREBIGAME_API UKomorebiWorldCoordinator : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

private:
	void DiscoverWorldActors();
	void WirePlayerCharacter(AKaelCharacter* Kael);
	void WireSubsystemsAndActors();
	void PushLiveHUDState(AKaelCharacter* Kael, float DeltaTime);

	// --- Delegate Handlers ---

	UFUNCTION()
	void HandleRideToggleRequested(AKaelCharacter* Kael);

	UFUNCTION()
	void HandlePauseToggleRequested();

	UFUNCTION()
	void HandleCombatToast(const FString& Message);

	UFUNCTION()
	void HandleCombatFlash(const FString& Text);

	UFUNCTION()
	void HandleCombatAudioCue(FName CueName, float PitchOrScale);

	UFUNCTION()
	void HandleCombatMissionStrike(FVector WorldLocation, float RadiusCm, float Damage, FName Source, int32 ClearedSegment);

	UFUNCTION()
	void HandleCombatPlayerDied(bool bEnteredOnlineDowned);

	UFUNCTION()
	void HandleSquadSegmentCleared(int32 SegmentIndex, int32 TotalClearedCount, EKomorebiPower GrantedPower);

	UFUNCTION()
	void HandleSquadToast(const FString& Message);

	UFUNCTION()
	void HandleSquadFlash(const FString& Text);

	UFUNCTION()
	void HandleSquadAudioCue(FName CueName, float Scale);

	UFUNCTION()
	void HandleMissionToast(const FString& Message);

	UFUNCTION()
	void HandleMissionFlash(const FString& Text);

	UFUNCTION()
	void HandleMissionCommsStarted(const FKomorebiCommsMessage& Message);

	UFUNCTION()
	void HandleMissionRewardCash(int32 Amount, const FString& Reason);

	UFUNCTION()
	void HandleMissionHealPlayer(float HealAmount);

	UFUNCTION()
	void HandleMissionReduceHeat(float HeatDelta);

	UFUNCTION()
	void HandleCheckpointActivated(int32 SectorIndex, FVector RespawnWorldLocation);

	UFUNCTION()
	void HandleSectorGateOpened(int32 SectorIndex, FString UnlockedPowerName);

	UFUNCTION()
	void HandleTrafficCarjacked(AKomorebiVehiclePawn* Vehicle, APawn* Driver);

	UFUNCTION()
	void HandleTrafficToast(const FString& Message);

	UFUNCTION()
	void HandleTrafficFlash(const FString& Text);

	UFUNCTION()
	void HandleVehicleRamPawn(AKomorebiVehiclePawn* Vehicle, APawn* HitPawn, float SpeedMps, FVector ImpactDirectionUE, float SuggestedDamage);

	UFUNCTION()
	void HandleVehicleCrashDamage(AKomorebiVehiclePawn* Vehicle, float DamageAmount);

	UFUNCTION()
	void HandleVehicleCameraJolt(AKomorebiVehiclePawn* Vehicle, float Intensity);

	UFUNCTION()
	void HandleShowcaseStateChanged(bool bTourActive);

	UFUNCTION()
	void HandleShowcaseShotChanged(const FKomorebiShowcaseShotMeta& ShotMeta);

	UFUNCTION()
	void HandleShowcaseProgressUpdated(int32 ShotIndex, float Progress01);

	UFUNCTION()
	void HandleShowcaseDayPhasePreview(bool bOverrideActive, bool bPreviewNight);

	UFUNCTION()
	void HandleDayNightPhaseShifted(bool bIsNowNight);

	// --- Cached References ---

	AKomorebiHUD* GetLocalHUD() const;
	AKaelCharacter* GetLocalKael() const;

	EKomorebiWorldScene ActiveScene = EKomorebiWorldScene::NeoKyoto;
	EKomorebiPlayMode ActiveMode = EKomorebiPlayMode::StorySectors;
	bool bWorldWired = false;
	float FootstepAccumMeters = 0.0f;
	FVector LastFootstepWorldLoc = FVector::ZeroVector;
	int32 LastHUDChapter = -1;

	TWeakObjectPtr<AKomorebiTerrainActor> TerrainActor;
	TWeakObjectPtr<ANeoKyotoCityActor> CityActor;
	TWeakObjectPtr<AStreetPropsActor> StreetPropsActor;
	TWeakObjectPtr<ASnowboundWorldActor> SnowboundActor;
	TWeakObjectPtr<ADayNightAtmosphereActor> AtmosphereActor;
	TWeakObjectPtr<AShowcaseCameraDirector> ShowcaseDirector;
	TWeakObjectPtr<AKaelCharacter> BoundKael;
};
