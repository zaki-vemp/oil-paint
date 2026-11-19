// Copyright Komorebi Project. Replicated GameState for contracts, Siege Mech boss events, race records, and synced online day/night.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "KomorebiGameTypes.h"
#include "KomorebiGameState.generated.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(FKomorebiOnWorldAnnouncement, const FString& /*Toast*/, const FString& /*Flash*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FKomorebiOnContractUpdated, int32 /*Sector*/, int32 /*Kills*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FKomorebiOnSiegeMechUpdated, bool /*bActive*/, FVector /*Location*/);

/**
 * Replicated match/world state shared across Listen Server, Dedicated Server, and clients.
 * Carries district contract progress, Siege Mech world boss state, street race room bests,
 * synchronized online day/night clock, and multicast room event broadcasts.
 */
UCLASS()
class KOMOREBIGAME_API AKomorebiGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AKomorebiGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// --- District Contract State ---

	UPROPERTY(ReplicatedUsing = OnRep_ActiveContract, BlueprintReadOnly, Category = "Komorebi|Online")
	int32 ActiveContractSector = 0;

	UPROPERTY(ReplicatedUsing = OnRep_ActiveContract, BlueprintReadOnly, Category = "Komorebi|Online")
	int32 ActiveContractKills = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Online")
	int32 ActiveContractTargetCount = 0;

	// --- Siege Mech World Boss State ---

	UPROPERTY(ReplicatedUsing = OnRep_SiegeMechState, BlueprintReadOnly, Category = "Komorebi|Online")
	bool bSiegeMechActive = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Online")
	FVector SiegeMechLocation = FVector::ZeroVector;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Online")
	float SiegeMechHealthRatio = 1.0f;

	// --- Street Race Room Best ---

	UPROPERTY(ReplicatedUsing = OnRep_BestRace, BlueprintReadOnly, Category = "Komorebi|Online")
	float BestRaceTimeSeconds = 0.0f;

	UPROPERTY(ReplicatedUsing = OnRep_BestRace, BlueprintReadOnly, Category = "Komorebi|Online")
	FString BestRaceHolderName;

	// --- Synchronized Online Day / Night ---

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Atmosphere")
	float SyncedOnlineNightFactor = 0.0f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Atmosphere")
	bool bSyncedOnlineIsNight = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Atmosphere")
	FString SyncedOnlineClockMMSS = TEXT("10:00");

	// --- Room & Mode Metadata ---

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Online")
	FString ActiveRoomCode;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Online")
	bool bIsPrivateRoom = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Flow")
	EKomorebiWorldScene ActiveWorldScene = EKomorebiWorldScene::NeoKyoto;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|Flow")
	EKomorebiPlayMode ActivePlayMode = EKomorebiPlayMode::StorySectors;

	// --- Multicast Room Broadcasts ---

	UFUNCTION(NetMulticast, Reliable)
	void MulticastAnnounceEvent(const FString& ToastMessage, const FString& FlashBanner = TEXT(""));

	UFUNCTION(NetMulticast, Reliable)
	void MulticastContractCompleted(int32 CompletedSector, int32 RewardCash);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastSiegeMechSpawned(const FString& DistrictName, FVector WorldLocation);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastDuelOutcome(const FString& WinnerName, const FString& DefeatedName, int32 WinnerPlayerId);

	/** Returns the number of currently connected players in the room. */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Online")
	int32 GetConnectedPlayerCount() const { return PlayerArray.Num(); }

	/** True when running in 8-player Online City mode. */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Online")
	bool IsOnlineCityMode() const { return ActivePlayMode == EKomorebiPlayMode::OnlineCity; }

	/** Server helper to record a finished street race time if it beats the room record. */
	bool TryRecordRoomBestRace(float TimeSeconds, const FString& RunnerName);

	FKomorebiOnWorldAnnouncement OnWorldAnnouncement;
	FKomorebiOnContractUpdated OnContractUpdated;
	FKomorebiOnSiegeMechUpdated OnSiegeMechUpdated;

protected:
	UFUNCTION()
	void OnRep_ActiveContract();

	UFUNCTION()
	void OnRep_SiegeMechState();

	UFUNCTION()
	void OnRep_BestRace();

private:
	void DeliverLocalHUDAnnouncement(const FString& ToastMessage, const FString& FlashBanner) const;
};
