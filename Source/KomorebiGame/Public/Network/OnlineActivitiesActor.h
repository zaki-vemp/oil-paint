// Copyright Komorebi Project. Replicated Neo-Kyoto online street activities: 12-Gate Race, PvP Duel Ring, and Proximity Revive.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KomorebiGameTypes.h"
#include "Game/OnlineMeta.h"
#include "OnlineActivitiesActor.generated.h"

class AKomorebiPlayerController;
class AKomorebiPlayerState;
class UProceduralMeshComponent;

USTRUCT(BlueprintType)
struct FKomorebiRaceProgress
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Online")
	bool bActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Online")
	int32 CurrentGateIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Online")
	float ElapsedSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Online")
	float OutOfVehicleSeconds = 0.0f;
};

/**
 * Replicated actor orchestrating all Online City street activities in Neo-Kyoto:
 * - 12-Gate Street Race along KomorebiCore::WaySamples() (start=30, r=6.5m, 150s limit)
 * - PvP Duel Ring (at=10, r=9.0m, server-validated on-foot blade damage)
 * - Proximity Revive Monitor (2.4m radius x 2.0s hold lifts downed teammate to 45 HP + 1.5s invuln)
 */
UCLASS()
class KOMOREBIGAME_API AOnlineActivitiesActor : public AActor
{
	GENERATED_BODY()

public:
	AOnlineActivitiesActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** True when a UE world location lies horizontally inside the 9.0m PvP Duel Ring. */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Online")
	bool IsLocationInsideDuelRing(const FVector& WorldLocation) const;

	/** UE world location of the PvP Duel Ring center. */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Online")
	FVector GetDuelRingCenter() const { return DuelRingCenterUE; }

	/** UE world location of the Street Race start ring. */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Online")
	FVector GetRaceStartLocation() const { return RaceStartLocationUE; }

	/** UE world location of a specific race gate (0..11). */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Online")
	FVector GetRaceGateLocation(int32 GateIndex) const;

	/** Returns the active local player's race progress snapshot for HUD display. */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Online")
	const FKomorebiRaceProgress& GetLocalRaceProgress() const { return LocalRaceProgress; }

	/** Builds the live online objective banner text (Downed / Race / Siege Mech / District Contract). */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Online")
	FString BuildOnlineObjectiveTaskText(const AKomorebiPlayerState* LocalPS, const FVector& PlayerWorldLocation) const;

	/** Server-authoritative PvP blade hit validation strictly gated inside the Duel Ring while on foot. */
	void ProcessServerPvPHit(
		AKomorebiPlayerController* AttackerPC,
		AKomorebiPlayerState* VictimPS,
		EKomorebiAttackType AttackType,
		const FVector& AttackerLocation);

	/** Server-authoritative manual revive trigger check (2.4m radius). */
	void TryServerManualRevive(AKomorebiPlayerController* RescuerPC, AKomorebiPlayerState* DownedPS);

protected:
	UPROPERTY(VisibleAnywhere, Category = "Komorebi|Online")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Komorebi|Online")
	TObjectPtr<UProceduralMeshComponent> StartGateMesh;

	UPROPERTY(VisibleAnywhere, Category = "Komorebi|Online")
	TObjectPtr<UProceduralMeshComponent> ActiveGateMesh;

	UPROPERTY(VisibleAnywhere, Category = "Komorebi|Online")
	TObjectPtr<UProceduralMeshComponent> DuelRingMesh;

private:
	void BuildWayCoordinates();
	void BuildVisualRings();
	void BuildTorusGateMesh(UProceduralMeshComponent* TargetMesh, float RadiusCm, float TubeCm, const FColor& VertexColor);
	void BuildFlatRingMesh(UProceduralMeshComponent* TargetMesh, float InnerRadiusCm, float OuterRadiusCm, const FColor& VertexColor);
	void TickLocalStreetRace(float DeltaSeconds);
	void TickLocalDuelRingNotice();
	void TickServerProximityRevives(float DeltaSeconds);

	FVector DuelRingCenterUE = FVector::ZeroVector;
	FVector RaceStartLocationUE = FVector::ZeroVector;
	FRotator RaceStartRotationUE = FRotator::ZeroRotator;
	TArray<FVector> RaceGateLocationsUE;
	TArray<FRotator> RaceGateRotationsUE;

	FKomorebiRaceProgress LocalRaceProgress;
	bool bWasLocalPlayerInDuelRing = false;

	/** Server-side revive flash tracking so "Reviving <Name>" fires once per contact. */
	TMap<TWeakObjectPtr<AKomorebiPlayerState>, bool> ActiveReviveNotified;
};
