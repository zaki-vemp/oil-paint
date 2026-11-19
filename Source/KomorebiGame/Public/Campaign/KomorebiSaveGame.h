// Copyright Komorebi Project. Native UE5 SaveGame container and failure-aware persistence manager.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "KomorebiGameTypes.h"
#include "Game/AudioTheory.h"
#include "Game/OnlineMeta.h"
#include "KomorebiSaveGame.generated.h"

/**
 * Persistent cross-platform SaveGame for Komorebi: Protocol Zero-Seven.
 * Stores campaign sector progression (powers are derived from ClearedSectors, never stored),
 * collected Memory Echo Shards, procedural audio mix settings, and Online City profile stats.
 */
UCLASS(BlueprintType)
class KOMOREBIGAME_API UKomorebiSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UKomorebiSaveGame();

	// --- Campaign Progress ---------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Save|Campaign")
	int32 ClearedSectors = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Save|Campaign")
	int32 Cash = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Save|Campaign")
	TArray<EKomorebiVehicleKind> UnlockedVehicles;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Save|Campaign")
	TArray<int32> CompletedMissionSectors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Save|Campaign")
	bool bWon = false;

	/** Last checkpoint X/Z in Three.js world metres (derived from ClearedSectors on load). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Save|Campaign")
	FVector2D CheckpointXZ = FVector2D(0.0, 234.0);

	// --- Lore Echoes ---------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Save|Lore")
	TArray<FString> CollectedEchoIds;

	// --- Audio Mix -----------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Save|Audio")
	float Master = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Save|Audio")
	float Music = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Save|Audio")
	float Voice = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Save|Audio")
	float Sfx = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Save|Audio")
	bool bMuted = false;

	// --- Online Profile ------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Save|Online")
	FString PlayerName = TEXT("Courier-07");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Save|Online")
	FString EquippedTitle = TEXT("Courier");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Save|Online")
	int32 EquippedColorIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Save|Online")
	int32 Contracts = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Save|Online")
	int32 Mechs = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Save|Online")
	int32 Races = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Save|Online")
	int32 Duels = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Save|Online")
	int32 Revives = 0;

	// --- Normalization & Core Interop ----------------------------------------------------------

	/** Sanitizes all fields, clamps ranges, derives checkpoint/unlocks, and deduplicates arrays. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Save")
	void Normalize();

	/** Resets campaign progression fields while preserving audio settings and online profile. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Save")
	void ResetCampaignOnly(bool bAlsoResetEchoes = false);

	UFUNCTION(BlueprintPure, Category = "Komorebi|Save")
	bool HasCollectedEcho(const FString& EchoId) const;

	/** Adds an Echo ID if not already present; returns true if newly added. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Save")
	bool AddCollectedEcho(const FString& EchoId);

	UFUNCTION(BlueprintPure, Category = "Komorebi|Save")
	bool IsMissionSectorCompleted(int32 SectorIndex) const;

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Save")
	void MarkMissionSectorCompleted(int32 SectorIndex);

	UFUNCTION(BlueprintPure, Category = "Komorebi|Save")
	bool IsVehicleUnlocked(EKomorebiVehicleKind Kind) const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|Save")
	bool HasPower(EKomorebiPower Power) const;

	KomorebiCore::PowerSet GetDerivedPowers() const;
	KomorebiCore::AudioSettings ToCoreAudioSettings() const;
	void ApplyCoreAudioSettings(const KomorebiCore::AudioSettings& Settings);
	KomorebiCore::OnlineStats ToCoreOnlineStats() const;
	void ApplyCoreOnlineStats(const KomorebiCore::OnlineStats& Stats);
};

/**
 * Static SaveGame persistence helper. Never swallows disk/slot write failures so HUD can warn once.
 */
UCLASS()
class KOMOREBIGAME_API UKomorebiSaveManager : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	static constexpr const TCHAR* DefaultSlotName = TEXT("KomorebiSave_v1");
	static constexpr int32 DefaultUserIndex = 0;

	/** Loads an existing save slot or creates and normalizes a fresh UKomorebiSaveGame instance. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Save")
	static UKomorebiSaveGame* LoadOrCreate(
		const FString& SlotName = TEXT("KomorebiSave_v1"),
		int32 UserIndex = 0,
		bool bFreshCampaign = false);

	/**
	 * Persists the save object synchronously and returns true only when the write succeeds.
	 * On failure, logs a warning and populates OutToastWarning on the first failure in the session
	 * so UI never silently drops save errors.
	 */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Save")
	static bool SaveWithToastCheck(
		UKomorebiSaveGame* SaveGameObject,
		FString& OutToastWarning,
		const FString& SlotName = TEXT("KomorebiSave_v1"),
		int32 UserIndex = 0);

	/** Convenience overload when the caller checks the boolean return value directly. */
	static bool SaveWithToastCheck(
		UKomorebiSaveGame* SaveGameObject,
		const FString& SlotName = TEXT("KomorebiSave_v1"),
		int32 UserIndex = 0);

	/** Deletes a persisted save slot if it exists. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Save")
	static bool DeleteSlot(
		const FString& SlotName = TEXT("KomorebiSave_v1"),
		int32 UserIndex = 0);

	/** Resets the per-session save-failure toast latch (useful for automated tests). */
	static void ResetSaveWarningLatch();

private:
	static bool bHasWarnedSaveFailureThisSession;
};
