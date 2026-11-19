// Copyright Komorebi Project. Replicated PlayerState holding sanitized nameplate, title, neon color, downed/revive state, cash, and stats.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Game/OnlineMeta.h"
#include "KomorebiPlayerState.generated.h"

DECLARE_MULTICAST_DELEGATE(FKomorebiOnPlayerProfileReplicated);

/**
 * Replicated player identity and online progression state for up to 8 players.
 * Mirrors legacy-web/src/game/net.js + server/room.mjs sanitization, title/color unlocks,
 * 15s downed timer, 2s proximity revive progress, and online stats.
 */
UCLASS()
class KOMOREBIGAME_API AKomorebiPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AKomorebiPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// --- Replicated Identity ---

	UPROPERTY(ReplicatedUsing = OnRep_Profile, BlueprintReadOnly, Category = "Komorebi|Online")
	FString DisplayName = TEXT("Wanderer");

	UPROPERTY(ReplicatedUsing = OnRep_Profile, BlueprintReadOnly, Category = "Komorebi|Online")
	FString EquippedTitle;

	UPROPERTY(ReplicatedUsing = OnRep_Profile, BlueprintReadOnly, Category = "Komorebi|Online")
	int32 EquippedColorIndex = 0;

	// --- Replicated Combat & Revive State ---

	UPROPERTY(ReplicatedUsing = OnRep_DownedState, BlueprintReadOnly, Category = "Komorebi|Online")
	bool bIsDowned = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Online")
	float DownedTimeRemaining = 0.0f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Online")
	float ReviveProgress = 0.0f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Online")
	float PlayerHealth = 100.0f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Online")
	bool bIsDrivingVehicle = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Online")
	int32 OnlineCash = 0;

	// --- Replicated Online Activity Stats (KomorebiCore::OnlineStats mirror) ---

	UPROPERTY(ReplicatedUsing = OnRep_Stats, BlueprintReadOnly, Category = "Komorebi|Online")
	int32 StatContracts = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Stats, BlueprintReadOnly, Category = "Komorebi|Online")
	int32 StatMechs = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Stats, BlueprintReadOnly, Category = "Komorebi|Online")
	int32 StatRaces = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Stats, BlueprintReadOnly, Category = "Komorebi|Online")
	int32 StatDuels = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Stats, BlueprintReadOnly, Category = "Komorebi|Online")
	int32 StatRevives = 0;

	// --- Sanitization & Formatting ---

	/** Mirrors server/room.mjs clean(): keeps letters, digits, space, '_', '.', '-', max 16 chars, fallback "Wanderer". */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Online")
	static FString SanitizeDisplayName(const FString& RawName);

	/** Mirrors server/room.mjs clean(title, 24, ''): sanitized title up to 24 chars. */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Online")
	static FString SanitizeTitle(const FString& RawTitle);

	/** Formatted nameplate ("Name · Title" or "Name"). */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Online")
	FString GetFormattedNameplate() const;

	/** Linear color matching EquippedColorIndex in KomorebiCore::GetOnlineColors(). */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Online")
	FLinearColor GetEquippedLinearColor() const;

	KomorebiCore::OnlineStats GetOnlineStats() const;
	void SetOnlineStats(const KomorebiCore::OnlineStats& InStats);

	// --- RPCs ---

	/** Client-to-server profile update; server sanitizes and validates unlocks before replicating. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Komorebi|Online")
	void ServerSetProfile(const FString& InDisplayName, const FString& InTitle, int32 InColorIndex);

	/** Client-to-server initial stat sync from local saved profile when joining a room. */
	UFUNCTION(Server, Reliable)
	void ServerSyncInitialStats(int32 InContracts, int32 InMechs, int32 InRaces, int32 InDuels, int32 InRevives);

	/** Server-to-owning-client stat award notification (persists to GameInstance + unlocks toasts). */
	UFUNCTION(Client, Reliable)
	void ClientNotifyStatEarned(const FString& StatName, int32 NewValue);

	// --- Server Authority State Mutators ---

	void EnterDownedState();
	void CompleteRevive();
	void ResetAfterBleedout();
	void AddOnlineCash(int32 DeltaCash);
	void IncrementOnlineStat(const FString& StatName);
	bool IsInvulnerableNow(double WorldTimeSeconds) const { return WorldTimeSeconds < InvulnerableUntilWorldTime; }

	FKomorebiOnPlayerProfileReplicated OnProfileReplicated;

protected:
	UFUNCTION()
	void OnRep_Profile();

	UFUNCTION()
	void OnRep_DownedState();

	UFUNCTION()
	void OnRep_Stats();

private:
	double InvulnerableUntilWorldTime = 0.0;
};
