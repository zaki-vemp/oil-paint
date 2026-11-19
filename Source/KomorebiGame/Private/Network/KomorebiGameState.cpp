// Copyright Komorebi Project. Replicated GameState for contracts, Siege Mech boss events, race records, and synced online day/night.
#include "Network/KomorebiGameState.h"

#include "UI/KomorebiHUD.h"
#include "Game/LevelsData.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

AKomorebiGameState::AKomorebiGameState()
{
	bReplicates = true;
	bAlwaysRelevant = true;
}

void AKomorebiGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AKomorebiGameState, ActiveContractSector);
	DOREPLIFETIME(AKomorebiGameState, ActiveContractKills);
	DOREPLIFETIME(AKomorebiGameState, ActiveContractTargetCount);
	DOREPLIFETIME(AKomorebiGameState, bSiegeMechActive);
	DOREPLIFETIME(AKomorebiGameState, SiegeMechLocation);
	DOREPLIFETIME(AKomorebiGameState, SiegeMechHealthRatio);
	DOREPLIFETIME(AKomorebiGameState, BestRaceTimeSeconds);
	DOREPLIFETIME(AKomorebiGameState, BestRaceHolderName);
	DOREPLIFETIME(AKomorebiGameState, SyncedOnlineNightFactor);
	DOREPLIFETIME(AKomorebiGameState, bSyncedOnlineIsNight);
	DOREPLIFETIME(AKomorebiGameState, SyncedOnlineClockMMSS);
	DOREPLIFETIME(AKomorebiGameState, ActiveRoomCode);
	DOREPLIFETIME(AKomorebiGameState, bIsPrivateRoom);
	DOREPLIFETIME(AKomorebiGameState, ActiveWorldScene);
	DOREPLIFETIME(AKomorebiGameState, ActivePlayMode);
}

bool AKomorebiGameState::TryRecordRoomBestRace(float TimeSeconds, const FString& RunnerName)
{
	if (!HasAuthority() || TimeSeconds <= 0.0f)
	{
		return false;
	}

	if (BestRaceTimeSeconds <= 0.0f || TimeSeconds < BestRaceTimeSeconds)
	{
		BestRaceTimeSeconds = TimeSeconds;
		BestRaceHolderName = RunnerName;
		OnRep_BestRace();
		return true;
	}
	return false;
}

void AKomorebiGameState::MulticastAnnounceEvent_Implementation(const FString& ToastMessage, const FString& FlashBanner)
{
	OnWorldAnnouncement.Broadcast(ToastMessage, FlashBanner);
	DeliverLocalHUDAnnouncement(ToastMessage, FlashBanner);
}

void AKomorebiGameState::MulticastContractCompleted_Implementation(int32 CompletedSector, int32 RewardCash)
{
	const auto& Levels = KomorebiCore::GetLevels();
	const FString DistrictName = (CompletedSector >= 0 && CompletedSector < static_cast<int32>(Levels.size()))
		? KomorebiBridge::ToFString(Levels[static_cast<size_t>(CompletedSector)].Name)
		: TEXT("the district");

	const FString Toast = FString::Printf(
		TEXT("Contract complete · %s crew broken · +$%d"),
		*DistrictName,
		RewardCash);
	OnWorldAnnouncement.Broadcast(Toast, TEXT("CONTRACT COMPLETE"));
	DeliverLocalHUDAnnouncement(Toast, TEXT("CONTRACT COMPLETE"));
}

void AKomorebiGameState::MulticastSiegeMechSpawned_Implementation(const FString& DistrictName, FVector WorldLocation)
{
	const FString Toast = FString::Printf(
		TEXT("Siege mech in %s · everyone who hits it is paid"),
		*DistrictName);
	OnWorldAnnouncement.Broadcast(Toast, TEXT("SIEGE MECH"));
	OnSiegeMechUpdated.Broadcast(true, WorldLocation);
	DeliverLocalHUDAnnouncement(Toast, TEXT("SIEGE MECH"));
}

void AKomorebiGameState::MulticastDuelOutcome_Implementation(const FString& WinnerName, const FString& DefeatedName, int32 WinnerPlayerId)
{
	FString Toast;
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* LocalPC = World->GetFirstPlayerController())
		{
			if (LocalPC->PlayerState && LocalPC->PlayerState->GetPlayerId() == WinnerPlayerId)
			{
				Toast = FString::Printf(TEXT("You beat %s in the ring"), *DefeatedName);
			}
		}
	}

	if (Toast.IsEmpty())
	{
		Toast = FString::Printf(TEXT("%s beat %s in the ring"), *WinnerName, *DefeatedName);
	}

	OnWorldAnnouncement.Broadcast(Toast, TEXT(""));
	DeliverLocalHUDAnnouncement(Toast, TEXT(""));
}

void AKomorebiGameState::OnRep_ActiveContract()
{
	OnContractUpdated.Broadcast(ActiveContractSector, ActiveContractKills);
}

void AKomorebiGameState::OnRep_SiegeMechState()
{
	OnSiegeMechUpdated.Broadcast(bSiegeMechActive, SiegeMechLocation);
}

void AKomorebiGameState::OnRep_BestRace()
{
	// Consumers (HUD / OnlineActivitiesActor) read BestRaceTimeSeconds directly on tick.
}

void AKomorebiGameState::DeliverLocalHUDAnnouncement(const FString& ToastMessage, const FString& FlashBanner) const
{
	if (IsRunningDedicatedServer())
	{
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* LocalPC = World ? World->GetFirstPlayerController() : nullptr;
	AKomorebiHUD* HUD = LocalPC ? Cast<AKomorebiHUD>(LocalPC->GetHUD()) : nullptr;
	if (!HUD)
	{
		return;
	}

	if (!ToastMessage.IsEmpty())
	{
		HUD->EnqueueToast(ToastMessage);
	}
	if (!FlashBanner.IsEmpty())
	{
		HUD->ShowFlashText(FlashBanner);
	}
}
