// Copyright Komorebi Project. Native UE5 IOnlineSession wrapper for LAN/Direct-IP/EOS 8-player Online City matchmaking.
#include "Network/OnlineSessionSubsystem.h"

#include "Network/KomorebiGameInstance.h"
#include "Game/OnlineMeta.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "OnlineSessionSettings.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Guid.h"
#include "Engine/World.h"

namespace
{
	const FName kSettingPoolKey(TEXT("KOMOREBI_POOL"));
	const FName kSettingRoomKey(TEXT("KOMOREBI_ROOM"));
	const FName kSettingPrivateKey(TEXT("KOMOREBI_PRIVATE"));
	const FName kSettingMaxPlayersKey(TEXT("KOMOREBI_MAX"));
}

void UOnlineSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	StatusText = TEXT("Offline · ready to connect");
}

void UOnlineSessionSubsystem::Deinitialize()
{
	if (IOnlineSessionPtr SessionInterface = GetSessionInterface())
	{
		SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionHandle);
		SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsHandle);
		SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionHandle);
		SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionHandle);
	}
	Super::Deinitialize();
}

IOnlineSessionPtr UOnlineSessionSubsystem::GetSessionInterface() const
{
	IOnlineSubsystem* Subsystem = Online::GetSubsystem(GetWorld());
	if (!Subsystem)
	{
		Subsystem = IOnlineSubsystem::Get();
	}
	return Subsystem ? Subsystem->GetSessionInterface() : nullptr;
}

FString UOnlineSessionSubsystem::GenerateRoomCode(bool bPrivate) const
{
	const FString Hex = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
	return bPrivate
		? Hex.Left(8)
		: FString::Printf(TEXT("pub-%s"), *Hex.Left(6));
}

void UOnlineSessionSubsystem::SetStatus(const FString& NewStatus)
{
	StatusText = NewStatus;
	OnSessionStatusChanged.Broadcast(StatusText, CurrentRoomCode);
}

void UOnlineSessionSubsystem::HostOnlineCity(bool bPrivate, const FString& RoomCode, EKomorebiWorldScene Scene)
{
	PendingScene = Scene;
	bCurrentRoomPrivate = bPrivate;

	const std::string Sanitized = KomorebiCore::SanitizeRoomName(TCHAR_TO_UTF8(*RoomCode));
	CurrentRoomCode = Sanitized.empty() ? GenerateRoomCode(bPrivate) : KomorebiBridge::ToFString(Sanitized);

	if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
	{
		GI->SelectedScene = Scene;
		GI->SelectedPlayMode = EKomorebiPlayMode::OnlineCity;
		GI->PendingRoomCode = CurrentRoomCode;
		GI->bPendingPrivateRoom = bPrivate;
	}

	IOnlineSessionPtr SessionInterface = GetSessionInterface();
	if (!SessionInterface.IsValid())
	{
		// Fallback directly to Listen Server travel without an online subsystem.
		SetStatus(FString::Printf(TEXT("Hosting listen server · room %s"), *CurrentRoomCode));
		bSessionActive = true;
		const FString MapPath = UKomorebiGameInstance::GetMapPathForScene(Scene);
		const FString Options = FString::Printf(
			TEXT("listen?game=/Script/KomorebiGame.KomorebiOnlineGameMode?mode=online?pool=%s?room=%s?private=%d"),
			*UKomorebiGameInstance::GetPoolNameForScene(Scene),
			*CurrentRoomCode,
			bPrivate ? 1 : 0);
		UGameplayStatics::OpenLevel(this, FName(*MapPath), true, Options);
		return;
	}

	if (SessionInterface->GetNamedSession(NAME_GameSession) != nullptr)
	{
		bPendingHostAfterDestroy = true;
		SetStatus(TEXT("Closing previous session…"));
		SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionHandle);
		DestroySessionHandle = SessionInterface->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &UOnlineSessionSubsystem::HandleDestroySessionComplete));
		SessionInterface->DestroySession(NAME_GameSession);
		return;
	}

	CreateSessionInternal();
}

void UOnlineSessionSubsystem::CreateSessionInternal()
{
	IOnlineSessionPtr SessionInterface = GetSessionInterface();
	if (!SessionInterface.IsValid())
	{
		SetStatus(TEXT("Online subsystem unavailable"));
		return;
	}

	IOnlineSubsystem* Subsystem = Online::GetSubsystem(GetWorld());
	if (!Subsystem)
	{
		Subsystem = IOnlineSubsystem::Get();
	}
	const bool bUseLan = !Subsystem || Subsystem->GetSubsystemName() == FName(TEXT("NULL"));

	FOnlineSessionSettings Settings;
	Settings.NumPublicConnections = KomorebiCore::kMaxOnlinePlayers;
	Settings.NumPrivateConnections = 0;
	Settings.bIsLANMatch = bUseLan;
	Settings.bUsesPresence = !bUseLan;
	Settings.bUseLobbiesIfAvailable = !bUseLan;
	Settings.bAllowJoinInProgress = true;
	Settings.bAllowJoinViaPresence = true;
	Settings.bShouldAdvertise = !bCurrentRoomPrivate;
	Settings.Set(kSettingPoolKey, UKomorebiGameInstance::GetPoolNameForScene(PendingScene), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(kSettingRoomKey, CurrentRoomCode, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(kSettingPrivateKey, bCurrentRoomPrivate ? 1 : 0, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(kSettingMaxPlayersKey, KomorebiCore::kMaxOnlinePlayers, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	SetStatus(FString::Printf(TEXT("Creating %s room %s…"), bCurrentRoomPrivate ? TEXT("private") : TEXT("public"), *CurrentRoomCode));

	SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionHandle);
	CreateSessionHandle = SessionInterface->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &UOnlineSessionSubsystem::HandleCreateSessionComplete));

	if (!SessionInterface->CreateSession(0, NAME_GameSession, Settings))
	{
		SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionHandle);
		SetStatus(TEXT("Failed to start Online City session"));
	}
}

void UOnlineSessionSubsystem::QuickMatch(EKomorebiWorldScene Scene)
{
	PendingScene = Scene;
	bCurrentRoomPrivate = false;

	IOnlineSessionPtr SessionInterface = GetSessionInterface();
	if (!SessionInterface.IsValid())
	{
		HostOnlineCity(false, TEXT(""), Scene);
		return;
	}

	IOnlineSubsystem* Subsystem = Online::GetSubsystem(GetWorld());
	if (!Subsystem)
	{
		Subsystem = IOnlineSubsystem::Get();
	}
	const bool bUseLan = !Subsystem || Subsystem->GetSubsystemName() == FName(TEXT("NULL"));

	LastSessionSearch = MakeShared<FOnlineSessionSearch>();
	LastSessionSearch->MaxSearchResults = 32;
	LastSessionSearch->bIsLanQuery = bUseLan;

	SetStatus(FString::Printf(TEXT("Searching open %s rooms…"), *UKomorebiGameInstance::GetPoolNameForScene(Scene)));

	SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsHandle);
	FindSessionsHandle = SessionInterface->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UOnlineSessionSubsystem::HandleFindSessionsComplete));

	if (!SessionInterface->FindSessions(0, LastSessionSearch.ToSharedRef()))
	{
		SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsHandle);
		HostOnlineCity(false, TEXT(""), Scene);
	}
}

void UOnlineSessionSubsystem::JoinByAddress(const FString& Address)
{
	FString Target = Address.TrimStartAndEnd();
	if (Target.IsEmpty())
	{
		SetStatus(TEXT("Enter a server IP or host address"));
		return;
	}

	if (!Target.Contains(TEXT(":")))
	{
		Target += TEXT(":7777");
	}

	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		SetStatus(TEXT("No local player controller available to travel"));
		return;
	}

	SetStatus(FString::Printf(TEXT("Connecting to %s…"), *Target));
	bSessionActive = true;
	PC->ClientTravel(Target, ETravelType::TRAVEL_Absolute);
}

void UOnlineSessionSubsystem::LeaveSession()
{
	bPendingHostAfterDestroy = false;
	bSessionActive = false;
	CurrentRoomCode.Empty();

	if (IOnlineSessionPtr SessionInterface = GetSessionInterface())
	{
		if (SessionInterface->GetNamedSession(NAME_GameSession) != nullptr)
		{
			SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionHandle);
			DestroySessionHandle = SessionInterface->AddOnDestroySessionCompleteDelegate_Handle(
				FOnDestroySessionCompleteDelegate::CreateUObject(this, &UOnlineSessionSubsystem::HandleDestroySessionComplete));
			SessionInterface->DestroySession(NAME_GameSession);
		}
	}

	SetStatus(TEXT("Disconnected from Online City"));
}

void UOnlineSessionSubsystem::HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (IOnlineSessionPtr SessionInterface = GetSessionInterface())
	{
		SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionHandle);
	}

	if (!bWasSuccessful)
	{
		bSessionActive = false;
		SetStatus(TEXT("Could not host room — check network permissions"));
		return;
	}

	bSessionActive = true;
	SetStatus(FString::Printf(TEXT("Hosting %s room · %s"), bCurrentRoomPrivate ? TEXT("private") : TEXT("quick match"), *CurrentRoomCode));

	const FString MapPath = UKomorebiGameInstance::GetMapPathForScene(PendingScene);
	const FString Options = FString::Printf(
		TEXT("listen?game=/Script/KomorebiGame.KomorebiOnlineGameMode?mode=online?pool=%s?room=%s?private=%d"),
		*UKomorebiGameInstance::GetPoolNameForScene(PendingScene),
		*CurrentRoomCode,
		bCurrentRoomPrivate ? 1 : 0);
	const FString TravelURL = FString::Printf(TEXT("%s?%s"), *MapPath, *Options);

	if (UWorld* World = GetWorld())
	{
		if (!World->ServerTravel(TravelURL, true))
		{
			UGameplayStatics::OpenLevel(this, FName(*MapPath), true, Options);
		}
	}
}

void UOnlineSessionSubsystem::HandleFindSessionsComplete(bool bWasSuccessful)
{
	IOnlineSessionPtr SessionInterface = GetSessionInterface();
	if (SessionInterface.IsValid())
	{
		SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsHandle);
	}

	if (!bWasSuccessful || !LastSessionSearch.IsValid() || LastSessionSearch->SearchResults.Num() == 0)
	{
		SetStatus(TEXT("No open rooms found — hosting a new room…"));
		HostOnlineCity(false, TEXT(""), PendingScene);
		return;
	}

	const FString WantedPool = UKomorebiGameInstance::GetPoolNameForScene(PendingScene);
	int32 BestIndex = INDEX_NONE;
	int32 MostOccupied = -1;

	// Fill the fullest open public room matching the requested scene pool (matching room.mjs).
	for (int32 i = 0; i < LastSessionSearch->SearchResults.Num(); ++i)
	{
		const FOnlineSessionSearchResult& Result = LastSessionSearch->SearchResults[i];
		if (!Result.IsValid() || Result.Session.NumOpenPublicConnections <= 0)
		{
			continue;
		}

		FString PoolName;
		if (!Result.Session.SessionSettings.Get(kSettingPoolKey, PoolName) || !PoolName.Equals(WantedPool, ESearchCase::IgnoreCase))
		{
			continue;
		}

		int32 IsPrivateFlag = 0;
		if (Result.Session.SessionSettings.Get(kSettingPrivateKey, IsPrivateFlag) && IsPrivateFlag != 0)
		{
			continue;
		}

		const int32 Occupied = Result.Session.SessionSettings.NumPublicConnections - Result.Session.NumOpenPublicConnections;
		if (Occupied > MostOccupied)
		{
			MostOccupied = Occupied;
			BestIndex = i;
		}
	}

	if (BestIndex == INDEX_NONE || !SessionInterface.IsValid())
	{
		SetStatus(TEXT("No open room for this world — hosting a new room…"));
		HostOnlineCity(false, TEXT(""), PendingScene);
		return;
	}

	const FOnlineSessionSearchResult& Chosen = LastSessionSearch->SearchResults[BestIndex];
	FString FoundRoom;
	if (Chosen.Session.SessionSettings.Get(kSettingRoomKey, FoundRoom))
	{
		CurrentRoomCode = FoundRoom;
	}

	SetStatus(FString::Printf(TEXT("Joining room %s…"), *CurrentRoomCode));
	SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionHandle);
	JoinSessionHandle = SessionInterface->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &UOnlineSessionSubsystem::HandleJoinSessionComplete));

	if (!SessionInterface->JoinSession(0, NAME_GameSession, Chosen))
	{
		SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionHandle);
		HostOnlineCity(false, TEXT(""), PendingScene);
	}
}

void UOnlineSessionSubsystem::HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	IOnlineSessionPtr SessionInterface = GetSessionInterface();
	if (SessionInterface.IsValid())
	{
		SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionHandle);
	}

	if (Result != EOnJoinSessionCompleteResult::Success || !SessionInterface.IsValid())
	{
		if (Result == EOnJoinSessionCompleteResult::SessionIsFull)
		{
			SetStatus(TEXT("Room is full — opening a new room…"));
			HostOnlineCity(false, TEXT(""), PendingScene);
		}
		else
		{
			SetStatus(TEXT("Could not join room — try Quick Match again"));
		}
		return;
	}

	FString ConnectString;
	if (SessionInterface->GetResolvedConnectString(SessionName, ConnectString))
	{
		bSessionActive = true;
		SetStatus(FString::Printf(TEXT("Joined · room %s"), *CurrentRoomCode));
		if (UWorld* World = GetWorld())
		{
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				PC->ClientTravel(ConnectString, ETravelType::TRAVEL_Absolute);
			}
		}
	}
	else
	{
		SetStatus(TEXT("Could not resolve host address"));
	}
}

void UOnlineSessionSubsystem::HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (IOnlineSessionPtr SessionInterface = GetSessionInterface())
	{
		SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionHandle);
	}

	if (bPendingHostAfterDestroy)
	{
		bPendingHostAfterDestroy = false;
		CreateSessionInternal();
	}
}
