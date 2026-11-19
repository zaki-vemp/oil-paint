// Copyright Komorebi Project. Procedural chunked terrain actor for Neo-Kyoto and Snowbound worlds.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KomorebiGameTypes.h"
#include "World/NeoKyotoTerrain.h"
#include "World/SnowboundTerrain.h"
#include "KomorebiTerrainActor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class UTexture;
class UTexture2D;

using FNeoKyotoTerrainMap = KomorebiCore::NeoKyotoTerrainMap;
using FSnowboundTerrainMap = KomorebiCore::SnowboundTerrainMap;

/**
 * Generates and renders the 1025x1025 KomorebiCore heightfield for either Neo-Kyoto
 * or Snowbound as chunked procedural mesh sections with Chaos collision enabled,
 * vertex-encoded land mask channels, and uploaded RGBA data textures.
 */
UCLASS(BlueprintType, Blueprintable)
class KOMOREBIGAME_API AKomorebiTerrainActor : public AActor
{
	GENERATED_BODY()

public:
	AKomorebiTerrainActor();

	/** Active scene terrain heightfield to generate (Neo-Kyoto city or Snowbound alpine ridge). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Terrain")
	EKomorebiWorldScene WorldScene = EKomorebiWorldScene::NeoKyoto;

	/** Convenience flag synced with WorldScene for automated map builders. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Terrain")
	bool bIsSnowbound = false;

	/** Chunked procedural mesh holding terrain geometry and static collision. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Terrain")
	TObjectPtr<UProceduralMeshComponent> TerrainMesh;

	/** Baked 1025x1025 RGBA land-mask texture (Neo-Kyoto: road/clearing/moss/nature; Snowbound: rock/horn/level/1). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Terrain")
	TObjectPtr<UTexture2D> LandMaskTexture;

	/** Baked 512x512 RGBA street-light radiance field uploaded by ANeoKyotoCityActor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Terrain")
	TObjectPtr<UTexture2D> StreetLightsTexture;

	/** Terrain master material (defaults to M_TerrainNeoKyoto or M_TerrainSnowbound). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Terrain")
	TObjectPtr<UMaterialInterface> TerrainMaterial;

	/** Number of procedural mesh chunks along each horizontal axis (ChunkCountPerAxis^2 sections). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Terrain", meta = (ClampMin = "1", ClampMax = "16"))
	int32 ChunkCountPerAxis = 8;

	/** Heightmap sample stride for mesh tessellation (4 -> 257x257 vertices over the 1025x1025 heightfield). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Terrain", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MeshGridStride = 4;

	/** Prepares the KomorebiCore terrain map, builds chunked mesh sections with collision, and uploads LandMaskTexture. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Terrain")
	void RebuildTerrain();

	/** Ensures terrain heightfield and procedural mesh have been built at least once (BeginPlay-order safe). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Terrain")
	void EnsureTerrainBuilt();

	/** Binds an optional dynamic snow-track render target texture onto the terrain dynamic material. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Terrain")
	void SetSnowTrackTexture(UTexture* TrackTexture);

	/** Sets the active scene and rebuilds the terrain immediately. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Terrain")
	void SetWorldScene(EKomorebiWorldScene InScene);

	/** Queries terrain elevation in Unreal centimetres at a world-space UE location. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Komorebi|Terrain")
	float GetHeightAtUE(const FVector& UELocation) const;

	/** Queries exact bilinearly-interpolated terrain elevation in Three.js metres at (X, Z). */
	double GetHeightAtThree(double X, double Z) const;

	/** Queries Snowbound [rock, horn] land mask channels at Three.js (X, Z) metres. */
	void GetSnowboundLandAtThree(double X, double Z, double& OutRock, double& OutHorn) const;

	/** Returns the unit surface normal in Three.js space at (X, Z) metres. */
	KomorebiCore::Vec3 GetNormalAtThree(double X, double Z) const;

	/** Uploads a Size x Size RGBA street-light radiance texture (typically 512x512 from ANeoKyotoCityActor). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Terrain")
	void UploadStreetLightsTexture(const TArray<FColor>& Data, int32 Size);

	/** Returns true once the underlying heightfield has been prepared. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Komorebi|Terrain")
	bool IsTerrainPrepared() const;

	const FNeoKyotoTerrainMap& GetNeoKyotoMap() const { return NeoKyotoMap; }
	const FSnowboundTerrainMap& GetSnowboundMap() const { return SnowboundMap; }

protected:
	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void EnsureTerrainPrepared();
	void UploadLandMaskTexture(const std::vector<uint8_t>& LandMaskBytes, int32 Resolution);
	void BuildChunkedMesh(
		const std::vector<float>& Heights,
		const std::vector<uint8_t>& LandMaskBytes,
		int32 Resolution,
		double ExtentMeters);
	void ResolveAndBindMaterial();

	FNeoKyotoTerrainMap NeoKyotoMap;
	FSnowboundTerrainMap SnowboundMap;
	bool bTerrainBuilt = false;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TerrainMID;

	UPROPERTY(Transient)
	TObjectPtr<UTexture> SnowTrackTexture;
};
