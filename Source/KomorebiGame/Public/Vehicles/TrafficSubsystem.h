// Copyright Komorebi Project. World subsystem managing parked fleet, autonomous yielding traffic, and pairwise vehicle collisions.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "KomorebiGameTypes.h"
#include "Vehicles/KomorebiVehiclePawn.h"
#include "World/NeoKyotoTerrain.h"

#include <functional>

#include "TrafficSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTrafficCarjacked, AKomorebiVehiclePawn*, Vehicle, APawn*, Driver);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTrafficFlashText, const FString&, Text);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTrafficToast, const FString&, Message);

/**
 * Manages Neo-Kyoto's entire street vehicle simulation (porting legacy-web/src/game/ride.js):
 * - Spawns 12 parked vehicles past the 6 sector checkpoints (plus optional outer-grid wheels).
 * - Spawns 6 autonomous district traffic vehicles navigating grid avenues & streets.
 * - Smoothly yields to pedestrians, players, intersections, and other vehicles.
 * - Recycles out-of-sight wrecked/stuck traffic into empty lanes and restores displaced parked cars.
 * - Releases traffic AI on takeover (broadcasting carjack heat/guilt if occupied).
 * - Steps all vehicles at 120 Hz fixed substeps with pairwise OBB CollideVehicles impulses.
 */
UCLASS()
class KOMOREBIGAME_API UTrafficSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	// --- Setup & Fleet Control -----------------------------------------------------------------

	/** Spawns the full Neo-Kyoto parked fleet (12 checkpoint vehicles + optional 6 outer-grid) and 6 district traffic vehicles. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Traffic")
	void SpawnCityFleet(bool bIncludeOuterGridParked = true);

	/** Destroys all spawned vehicles and clears subsystem registrations. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Traffic")
	void ClearCityFleet();

	/** Registers a vehicle actor so it participates in synchronized 120 Hz stepping and OBB collisions. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Traffic")
	void RegisterVehicle(AKomorebiVehiclePawn* Vehicle);

	/** Unregisters a vehicle from the subsystem. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Traffic")
	void UnregisterVehicle(AKomorebiVehiclePawn* Vehicle);

	/** Optional Blueprint subclass override per vehicle kind (e.g., art-dressed Kestrel/Bike Blueprints). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Traffic")
	void SetVehicleClassOverride(EKomorebiVehicleKind Kind, TSubclassOf<AKomorebiVehiclePawn> VehicleClass);

	/** Injects a custom terrain height callback in Three.js metres (defaults to NeoKyotoTerrainMap). */
	void SetTerrainHeightProvider(TFunction<double(double, double)> InHeightProvider);

	/** Updates headlight intensity on all fleet vehicles from the Day/Night cycle (0 = day, 1 = night). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Traffic")
	void SetFleetNightPhase(float Night01);

	/** Keeps vehicles within 120m of the investor showcase camera visible. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Traffic")
	void RevealAroundShowcaseCamera(FVector FocusWorldLocation, FVector CameraWorldLocation);

	// --- Player Interaction & Queries ----------------------------------------------------------

	/** Finds the nearest interactable vehicle within MaxDistMeters of PlayerLocation (edge distance). */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Traffic")
	AKomorebiVehiclePawn* FindNearestInteractableVehicle(FVector PlayerLocation, float MaxDistMeters = 3.4f) const;

	/**
	 * Handles the G-key vehicle interact action:
	 * - Exits if already driving (enforcing kBailSpeed = 9 m/s).
	 * - Enters or rights the nearest vehicle otherwise (releasing traffic AI & firing carjack event if occupied).
	 */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Traffic")
	bool TryPlayerInteractVehicle(
		APawn* PlayerPawn,
		bool bRoamMode,
		int32 ClearedSectors,
		float PlayerHp,
		FString& OutFeedbackMessage);

	/** Returns the currently player-driven vehicle, or nullptr if on foot. */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Traffic")
	AKomorebiVehiclePawn* GetDrivenVehicle() const { return DrivenVehicle.Get(); }

	/** Returns all live vehicles managed by this subsystem. */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Traffic")
	TArray<AKomorebiVehiclePawn*> GetAllVehicles() const;

	/** Writes active or nearby vehicle HUD fields into FKomorebiHUDState. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Traffic")
	void PopulateHUDState(UPARAM(ref) FKomorebiHUDState& InOutHud, FVector PlayerLocation) const;

	// --- Delegates -----------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Traffic")
	FOnTrafficCarjacked OnVehicleCarjacked;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Traffic")
	FOnTrafficFlashText OnFlashText;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Traffic")
	FOnTrafficToast OnToast;

private:
	struct FTrafficAIState
	{
		bool bAxisX = true;
		int32 Dir = 1;
		double Line = 0.0;
		double CruiseSpeed = 8.0;
		double WaitTimer = 0.0;
		double FleeTimer = 0.0;
		double SwerveSign = 1.0;
		double HornTimer = 0.0;
		bool bHonked = false;
		double StuckTimer = 0.0;
		double BeatStartAt = 0.0;
	};

	struct FPedestrianSample
	{
		double ThreeX = 0.0;
		double ThreeZ = 0.0;
		double Radius = 0.45;
	};

	double SampleTerrainHeight(double ThreeX, double ThreeZ) const;
	FVector GetViewerWorldLocation() const;
	AKomorebiVehiclePawn* SpawnVehicleInternal(EKomorebiVehicleKind Kind, double ThreeX, double ThreeZ, double ThreeYawRad);

	void CollectNearbyPedestrians(TArray<FPedestrianSample>& OutSamples) const;
	KomorebiCore::VehicleDriveInput ComputeTrafficInput(
		AKomorebiVehiclePawn* Vehicle,
		FTrafficAIState& AI,
		double Dt,
		double PlayerThreeX,
		double PlayerThreeZ,
		const TArray<FPedestrianSample>& Pedestrians);

	bool TryRecycleTrafficOutOfSight(
		AKomorebiVehiclePawn* Vehicle,
		FTrafficAIState& AI,
		double PlayerThreeX,
		double PlayerThreeZ,
		const TArray<FPedestrianSample>& Pedestrians);

	UFUNCTION()
	void HandleVehicleEntered(AKomorebiVehiclePawn* Vehicle, APawn* Driver, bool bWasCarjacked);

	UFUNCTION()
	void HandleVehicleExited(AKomorebiVehiclePawn* Vehicle, APawn* Driver);

	UFUNCTION()
	void HandleVehicleWrecked(AKomorebiVehiclePawn* Vehicle);

	bool bFleetSpawned = false;
	float CachedNightPhase = 1.0f;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AKomorebiVehiclePawn>> ManagedVehicles;

	TMap<TWeakObjectPtr<AKomorebiVehiclePawn>, FTrafficAIState> TrafficDrivers;
	TWeakObjectPtr<AKomorebiVehiclePawn> DrivenVehicle;

	UPROPERTY(Transient)
	TMap<EKomorebiVehicleKind, TSubclassOf<AKomorebiVehiclePawn>> VehicleClassOverrides;

	KomorebiCore::NeoKyotoTerrainMap FallbackTerrain;
	TFunction<double(double, double)> CustomHeightProvider;
};
