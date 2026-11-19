// Copyright Komorebi Project. Procedural Neo-Kyoto city architecture, foliage, street lighting, and district palette actor.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KomorebiGameTypes.h"
#include "NeoKyotoCityActor.generated.h"

class ADayNightAtmosphereActor;
class AKomorebiTerrainActor;
class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnDistrictChapterChanged,
	int32, ChapterIndex,
	const FString&, DistrictName,
	const FString&, Subtitle);

/** Static world collider recorded during procedural Neo-Kyoto generation (Three.js metres). */
USTRUCT(BlueprintType)
struct FKomorebiCitySolid
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|City")
	double X = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|City")
	double Y = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|City")
	double Z = 0.0;

	/** Half-extents (X, Y, Z) in Three.js metres when bIsBox is true. */
	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|City")
	FVector BoxHalfExtents = FVector::ZeroVector;

	/** Cylinder/sphere radius in metres when bIsBox is false. */
	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|City")
	double Radius = 0.5;

	/** Cylinder height in metres for tree trunks. */
	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|City")
	double Height = 4.5;

	/** Three.js Y-axis rotation in radians. */
	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|City")
	double YawRad = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|City")
	bool bIsBox = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|City")
	bool bIsRock = false;
};

/**
 * Generates Neo-Kyoto's instanced skyscrapers, neon signs, streetlamps, skybridges, torii gates,
 * broadleaf trees, alpine spruce pines, granite boulders, grass tufts, and industrial crates
 * matching legacy-web/src/world/anime.js.
 */
UCLASS(BlueprintType, Blueprintable)
class KOMOREBIGAME_API ANeoKyotoCityActor : public AActor
{
	GENERATED_BODY()

public:
	ANeoKyotoCityActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|City|Batches")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TowersHISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|City|Batches")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> SignsHISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|City|Batches")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> LampsHISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|City|Batches")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> LampPolesHISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|City|Batches")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> SkybridgesHISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|City|Batches")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> ToriiGatesHISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|City|Batches")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> BroadleafTrunksHISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|City|Batches")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> BroadleafCanopiesHISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|City|Batches")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> PineTrunksHISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|City|Batches")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> SprucePinesHISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|City|Batches")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> GraniteBouldersHISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|City|Batches")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> GrassTuftsHISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|City|Batches")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> CratesHISM;

	/** Master building parallax material (/Game/Materials/M_BuildingParallax). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|City|Materials")
	TObjectPtr<UMaterialInterface> BuildingMaterial;

	/** Emissive neon signboard & lamp head material (/Game/Materials/M_SignalEmissive). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|City|Materials")
	TObjectPtr<UMaterialInterface> SignalMaterial;

	/** Optional reference to the scene's terrain actor (auto-discovered if null). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|City")
	TObjectPtr<AKomorebiTerrainActor> TerrainActor;

	/** Optional reference to the scene's day/night atmosphere actor (auto-discovered if null). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|City")
	TObjectPtr<ADayNightAtmosphereActor> AtmosphereActor;

	/** Fired when the player's focus Z crosses into a new Neo-Kyoto chapter district. */
	UPROPERTY(BlueprintAssignable, Category = "Komorebi|City")
	FOnDistrictChapterChanged OnDistrictChapterChanged;

	/** Deterministically rebuilds all city HISM batches, solid colliders, and the 512x512 street-light field. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|City")
	void RebuildCity();

	/** Scales decorative instance density and cull distances (0.35 Low, 0.65 Balanced, 1.0 High). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|City")
	void SetDecorationQuality(float DensityMultiplier);

	/** Smoothly blends district pavement/road/neon/sky/haze palettes for FocusThreeZ and pushes them to atmosphere/MPC. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|City")
	void UpdateDistrictPalette(float DeltaTime, double FocusThreeZ, int32 ClearedSectors, bool bRoam);

	/** Returns all static city colliders (towers, torii pillars, sanctuary pillars, trees, boulders, crates). */
	const TArray<FKomorebiCitySolid>& GetSolidColliders() const { return SolidColliders; }

	/** Returns the latest baked 512x512 street-light radiance field pixels. */
	const TArray<FColor>& GetBakedStreetLightField() const { return BakedStreetLights; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Komorebi|City")
	int32 GetActiveChapterIndex() const { return ActiveChapter; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Komorebi|City")
	bool IsCityBuilt() const { return bCityBuilt; }

protected:
	virtual void BeginPlay() override;

private:
	struct FPlacementRecord
	{
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
		double SX = 1.0;
		double SY = 1.0;
		double SZ = 1.0;
		double Ry = 0.0;
		double Rz = 0.0;
		double WayDist = 0.0;
		int32 Zone = 0;
	};

	void AssignDefaultMeshesAndMaterials();
	void ResolveWorldReferences();
	double SampleHeight(double ThreeX, double ThreeZ) const;
	FTransform MakeBoxTransform(const FPlacementRecord& Rec) const;
	FTransform MakeCylinderTransform(const FPlacementRecord& Rec, double RadiusScale = 2.0, double CenterZOffsetFrac = 0.0) const;
	FTransform MakeSphereTransform(const FPlacementRecord& Rec) const;
	void BakeAndUploadStreetLights(
		const TArray<FPlacementRecord>& LightRecords,
		const TArray<FPlacementRecord>& SignRecords);

	TArray<FKomorebiCitySolid> SolidColliders;
	TArray<FTransform> FullGrassTuftTransforms;
	TArray<FColor> BakedStreetLights;

	int32 ActiveChapter = -1;
	float CurrentDecorationDensity = 1.0f;
	bool bCityBuilt = false;

	FLinearColor BlendedGrass = FLinearColor(0.60f, 0.62f, 0.65f, 1.0f);
	FLinearColor BlendedPath = FLinearColor(0.34f, 0.36f, 0.40f, 1.0f);
	FLinearColor BlendedFoliage = FLinearColor(0.0f, 0.85f, 0.95f, 1.0f);
	FLinearColor BlendedLeafLight = FLinearColor(1.0f, 0.86f, 0.45f, 1.0f);
	FLinearColor BlendedSkyHigh = FLinearColor(0.18f, 0.52f, 0.88f, 1.0f);
	FLinearColor BlendedHaze = FLinearColor(0.70f, 0.81f, 0.91f, 1.0f);
	bool bPaletteInitialized = false;
};
