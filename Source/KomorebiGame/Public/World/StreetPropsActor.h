// Copyright Komorebi Project. Service plaza street props and static colliders beside Neo-Kyoto checkpoints.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KomorebiGameTypes.h"
#include "StreetPropsActor.generated.h"

class AKomorebiTerrainActor;
class ANeoKyotoCityActor;
class UBoxComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;

/** Single placed street prop record in Three.js metres. */
USTRUCT(BlueprintType)
struct FKomorebiStreetPropPlacement
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Props")
	FName PropKind = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Props")
	int32 LevelIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Props")
	FVector WorldLocationUE = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Props")
	float YawDegreesUE = 0.0f;
};

/**
 * Mirrors legacy-web/src/world/props.js: places HISM service plazas
 * (terminal, beacon, power-core, supply-crate, barricade, noodle-stall)
 * beside all 6 Neo-Kyoto level checkpoints with UBoxComponent static colliders,
 * clearance checks against roads/fights/city solids, and area distance culling.
 */
UCLASS(BlueprintType, Blueprintable)
class KOMOREBIGAME_API AStreetPropsActor : public AActor
{
	GENERATED_BODY()

public:
	AStreetPropsActor();

	/** Optional reference to the scene terrain actor (auto-discovered if null). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Props")
	TObjectPtr<AKomorebiTerrainActor> TerrainActor;

	/** Optional reference to the city actor for solid-collider clearance checks (auto-discovered if null). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Props")
	TObjectPtr<ANeoKyotoCityActor> CityActor;

	/** Optional per-kind static mesh overrides (defaults to /Game/Assets/Street/SM_*). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Props")
	TMap<FName, TObjectPtr<UStaticMesh>> PropMeshes;

	/** Distance in metres beyond which an entire checkpoint service plaza hides its visuals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Props", meta = (ClampMin = "20.0", ClampMax = "300.0"))
	float CullDistanceMeters = 85.0f;

	/** Automatically tracks the local player pawn position for service-area visibility culling on Tick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Props")
	bool bAutoTrackPlayerFocus = true;

	/** Rebuilds all 6 checkpoint service plazas, HISM batches, and static UBoxComponent colliders. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Props")
	void RebuildProps();

	/** Updates visibility of each service plaza relative to FocusUE (cm) using CullDistanceMeters. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Props")
	void UpdateVisibility(const FVector& FocusUE);

	/** Updates the horizontal cull distance in metres (60 Low, 75 Balanced, 85 High). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Props")
	void SetCullDistanceMeters(float Meters);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Komorebi|Props")
	float GetCullDistanceMeters() const { return CullDistanceMeters; }

	const TArray<FKomorebiStreetPropPlacement>& GetPlacements() const { return Placements; }

	virtual void Tick(float DeltaTime) override;

protected:
	virtual void BeginPlay() override;

private:
	struct FServiceArea
	{
		TObjectPtr<USceneComponent> AreaRoot = nullptr;
		double CheckpointThreeX = 0.0;
		double CheckpointThreeZ = 0.0;
		bool bVisible = true;
		TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> VisualBatches;
	};

	struct FLocalCollider
	{
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
		FVector BoxHalfExtents = FVector::ZeroVector;
		double YawRad = 0.0;
	};

	void ClearSpawnedComponents();
	void ResolveWorldReferences();
	void LoadDefaultPropMeshes();
	double SampleHeight(double ThreeX, double ThreeZ) const;

	TArray<FServiceArea> ServiceAreas;
	TArray<FKomorebiStreetPropPlacement> Placements;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBoxComponent>> StaticColliders;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> OwnedDynamicComponents;
};
