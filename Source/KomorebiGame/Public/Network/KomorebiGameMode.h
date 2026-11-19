// Copyright Komorebi Project. Solo Story Sectors and Free Roam GameMode enforcing spawn/checkpoint respawns and world limits.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "KomorebiGameTypes.h"
#include "World/NeoKyotoTerrain.h"
#include "World/SnowboundTerrain.h"
#include "KomorebiGameMode.generated.h"

class AKaelCharacter;
class AKomorebiHUD;
class AKomorebiPlayerController;

/**
 * Primary solo GameMode for Story Sectors and Free Roam.
 * Spawns Kael at START or active checkpoint, samples exact KomorebiCore heightfields,
 * clamps players inside EXTENT - 16 m, respawns on fall/death, and supports M-key mode toggle.
 */
UCLASS()
class KOMOREBIGAME_API AKomorebiGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AKomorebiGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;
	virtual void RestartPlayer(AController* NewPlayer) override;

	/** Exact ground elevation in UE centimetres at a given UE world (X, Y) position. */
	UFUNCTION(BlueprintPure, Category = "Komorebi|World")
	float SampleTerrainHeightUE(float WorldXCm, float WorldYCm) const;

	/** Computes the default START transform in UE world coordinates for the active scene. */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Flow")
	FTransform GetStartSpawnTransform() const;

	/** Updates the active respawn checkpoint location (e.g., when activating a shrine/cairn). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Flow")
	void SetActiveCheckpoint(const FVector& WorldLocation);

	/** Updates checkpoint based on cleared sector count (0..5) from KomorebiCore::GetLevels(). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Flow")
	void SetClearedSectors(int32 ClearedCount);

	/** Teleports the player's pawn cleanly back to the active checkpoint and restores vitals. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Flow")
	void RespawnPlayerAtCheckpoint(AController* Controller);

	/** Toggles between Story Sectors and Free Roam in Neo-Kyoto solo play (M key). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Flow")
	virtual void ToggleStoryAndRoamMode(AController* Requestor);

	UFUNCTION(BlueprintPure, Category = "Komorebi|Flow")
	EKomorebiWorldScene GetActiveWorldScene() const { return ActiveScene; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Flow")
	EKomorebiPlayMode GetActivePlayMode() const { return ActivePlayMode; }

protected:
	void DetectSceneFromMapName(const FString& MapName);
	void PrepareTerrainMap();
	void EnforcePlayerWorldBounds();

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Flow")
	EKomorebiWorldScene ActiveScene = EKomorebiWorldScene::NeoKyoto;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Flow")
	EKomorebiPlayMode ActivePlayMode = EKomorebiPlayMode::StorySectors;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Flow")
	FVector ActiveCheckpointLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Flow")
	FRotator ActiveCheckpointRotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Flow")
	int32 ClearedSectors = 0;

	KomorebiCore::NeoKyotoTerrainMap NeoKyotoTerrain;
	KomorebiCore::SnowboundTerrainMap SnowboundTerrain;

private:
	TMap<TWeakObjectPtr<AController>, double> LastLimitFlashTime;
};
