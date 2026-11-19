// Copyright Komorebi Project. Snowbound (Aurora) arctic fjord, alpine rings, log cabin, pines, snow tracks, aurora sky, and drifting snow.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KomorebiGameTypes.h"
#include "SnowboundWorldActor.generated.h"

class ADayNightAtmosphereActor;
class AKomorebiTerrainActor;
class UBoxComponent;
class UCanvasRenderTarget2D;
class UHierarchicalInstancedStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UParticleSystemComponent;
class UPointLightComponent;
class UProceduralMeshComponent;
class UStaticMeshComponent;

/**
 * Mirrors legacy-web/src/night/: constructs the arctic fjord ocean (y = 0),
 * 3 distant alpine silhouette rings, Brothers' log cabin exterior & cozy interior
 * with flickering hearth firelight and doorstep near-wall cutout, HISM snow-laden pines,
 * dynamic snow-track UCanvasRenderTarget2D, Aurora sky dome, and drifting snow particles.
 */
UCLASS(BlueprintType, Blueprintable)
class KOMOREBIGAME_API ASnowboundWorldActor : public AActor
{
	GENERATED_BODY()

public:
	ASnowboundWorldActor();

	/** Arctic fjord ocean concentric ring bands at y = 0 (far.js). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Snowbound")
	TObjectPtr<UProceduralMeshComponent> FjordOceanMesh;

	/** Three distant stratified alpine mountain silhouette rings (far.js). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Snowbound")
	TObjectPtr<UProceduralMeshComponent> FarAlpineRingsMesh;

	/** Inverted sky sphere rendering stars, cratered moon, and volumetric aurora (sky.js). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Snowbound")
	TObjectPtr<UStaticMeshComponent> AuroraSkyDomeMesh;

	/** Three billowing aurora curtain ribbon strips above the northern ridge (sky.js). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Snowbound")
	TObjectPtr<UProceduralMeshComponent> AuroraCurtainsMesh;

	/** Master aurora sky material (/Game/Materials/M_AuroraSky). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Snowbound|Materials")
	TObjectPtr<UMaterialInterface> AuroraSkyMaterial;

	/** Optional water material for the arctic fjord ocean. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Snowbound|Materials")
	TObjectPtr<UMaterialInterface> FjordWaterMaterial;

	/** Root for the Brothers' Cabin positioned on KomorebiCore::SnowCabin(). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Snowbound|Cabin")
	TObjectPtr<USceneComponent> CabinRoot;

	/** Permanent exterior log walls, chimney, doorstep, and woodpile. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Snowbound|Cabin")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> CabinExteriorHISM;

	/** South near-wall and snow-drifted roof hidden automatically when player steps onto the doorstep. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Snowbound|Cabin")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> CabinNearWallCutoutHISM;

	/** Cozy furnished interior: floor, hearth, mantel, layered rug, chair, table, bed & quilt, shelves, dresser. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Snowbound|Cabin")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> CabinInteriorHISM;

	/** Warm flickering hearth firelight inside the west fireplace. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Snowbound|Cabin")
	TObjectPtr<UPointLightComponent> HearthFireLight;

	/** Warm candle light on the cabin wooden table. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Snowbound|Cabin")
	TObjectPtr<UPointLightComponent> TableCandleLight;

	/** Dark evergreen conifer under-cones along the ridge and flanks (pines.js). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Snowbound|Foliage")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> PineConesHISM;

	/** Heavy snow-laden bough layers crowning every spruce pine (pines.js). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Snowbound|Foliage")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> PineSnowCapsHISM;

	/** Dynamic snow-track canvas render target covering [-kSnowExtent, kSnowExtent]. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Snowbound|Tracks")
	TObjectPtr<UCanvasRenderTarget2D> SnowTrackRenderTarget;

	/** Instanced drifting snow crystals and wind-lifted spindrift around the player (drift.js). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Snowbound|Weather")
	TObjectPtr<UInstancedStaticMeshComponent> DriftingSnowMesh;

	/** Optional Cascade particle system component for editor-authored drifting snow. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Snowbound|Weather")
	TObjectPtr<UParticleSystemComponent> SnowParticleSystem;

	/** Optional reference to the scene terrain actor (auto-discovered if null). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Snowbound")
	TObjectPtr<AKomorebiTerrainActor> TerrainActor;

	/** Optional reference to the day/night atmosphere actor (auto-discovered if null). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Snowbound")
	TObjectPtr<ADayNightAtmosphereActor> AtmosphereActor;

	/** Resolution of the dynamic snow-track render target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Snowbound|Tracks", meta = (ClampMin = "256", ClampMax = "2048"))
	int32 SnowTrackResolution = 1024;

	/** Rebuilds ocean, alpine rings, cabin, pines, aurora curtains, snow tracks, and drift particles. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Snowbound")
	void RebuildSnowboundWorld();

	/** Stamps a shaded snow footprint hollow + moonlit snow lip onto SnowTrackRenderTarget. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Snowbound|Tracks")
	void StampFootprint(const FVector& WorldPos, float RadiusCm = 24.0f);

	/** Stamps pre-existing footprints along CrestX from Z0 to Z1 metres (matching tracks.lay). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Snowbound|Tracks")
	void LayInitialTrail(double StartZMeters, double EndZMeters);

	/** Returns true if PlayerPosUE lies inside the Brothers' Cabin interior bounds. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Komorebi|Snowbound|Cabin")
	bool IsInsideCabin(const FVector& PlayerPosUE) const;

	/** Returns true if PlayerPosUE is inside the cabin or standing on its south doorstep. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Komorebi|Snowbound|Cabin")
	bool IsAtCabinThreshold(const FVector& PlayerPosUE) const;

	/** Updates near-wall & roof cutout visibility based on player proximity to the doorstep. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Snowbound|Cabin")
	void UpdateCabinCutoutForLocation(const FVector& PlayerPosUE);

	/** Updates drifting snow instance budget (900 Low, 1500 Balanced, 2400 High). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Snowbound|Weather")
	void SetSnowDriftCount(int32 Count);

	/** Adjusts aurora ribbon curtain detail for Low/Medium/High quality tiers. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Snowbound")
	void SetAuroraQuality(EKomorebiQualityTier EffectiveTier);

	/** Updates carried lantern light parameters forwarded to MPC_KomorebiShared. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Snowbound")
	void UpdateLanternLight(const FVector& LanternWorldPosUE, float Strength = 1.0f);

	virtual void Tick(float DeltaTime) override;

protected:
	virtual void BeginPlay() override;

private:
	struct FDriftParticleSeed
	{
		float Rx = 0.0f;
		float Ry = 0.0f;
		float Rz = 0.0f;
		float Rw = 0.0f;
		float Qx = 0.0f;
		float Qy = 0.0f;
		float Qz = 0.0f;
		float Qw = 0.0f;
		bool bFlake = true;
	};

	void ResolveWorldReferences();
	void BuildFjordOceanMesh();
	void BuildFarAlpineRingsMesh();
	void BuildAuroraSkyAndCurtains();
	void BuildCabinAndInterior();
	void BuildSnowPines();
	void InitializeSnowTrackTarget();
	void InitializeDriftingSnow();
	void UpdateDriftingSnow(float TimeSec, const FVector& FocusUE);
	void UpdatePlayerStrideTracks(const FVector& PlayerPosUE, float PlayerYawDeg, float SpeedMps, bool bGrounded);
	void ClearDynamicColliders();
	double SampleHeight(double ThreeX, double ThreeZ) const;
	void SampleLand(double ThreeX, double ThreeZ, double& OutRock, double& OutHorn) const;
	KomorebiCore::Vec3 SampleNormal(double ThreeX, double ThreeZ) const;

	double ElapsedTime = 0.0;
	bool bNearWallHidden = false;
	int32 ActiveDriftCount = 2400;
	int32 MaxDriftCount = 2400;

	// Stride tracking state matching legacy-web/src/night/tracks.js.
	bool bHasLastTrackPos = false;
	double LastTrackThreeX = 0.0;
	double LastTrackThreeZ = 0.0;
	double DistSinceLastPrint = 0.0;
	bool bLeftFootNext = false;

	TArray<FDriftParticleSeed> DriftSeeds;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBoxComponent>> CabinColliders;
};
