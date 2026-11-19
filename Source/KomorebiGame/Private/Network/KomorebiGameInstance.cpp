// Copyright Komorebi Project. Persistent UE5 GameInstance managing scene/mode selection, travel, local profile, and subsystem bridges.
#include "Network/KomorebiGameInstance.h"

#include "Network/OnlineSessionSubsystem.h"
#include "Network/KomorebiPlayerState.h"
#include "Audio/KomorebiAudioSubsystem.h"
#include "Campaign/MissionSubsystem.h"
#include "Campaign/ShowcaseCameraDirector.h"
#include "World/DayNightAtmosphereActor.h"
#include "World/GraphicsSettingsSubsystem.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace
{
	const TCHAR* kProfileConfigSection = TEXT("/Script/KomorebiGame.KomorebiLocalProfile");
}

UKomorebiGameInstance::UKomorebiGameInstance()
	: CachedAudioSettings(KomorebiCore::DefaultAudioSettings())
{
}

void UKomorebiGameInstance::Init()
{
	Super::Init();
	LoadLocalProfile();
	ParseLaunchCommandLine();
	ApplyQualityTier(CachedQualityTier);
	ApplyRenderStyleToggles(bComicInkEnabled, bOilPaintImpastoEnabled);
}

void UKomorebiGameInstance::Shutdown()
{
	SaveLocalProfile();
	Super::Shutdown();
}

void UKomorebiGameInstance::ParseLaunchCommandLine()
{
	const TCHAR* Cmd = FCommandLine::Get();
	if (!Cmd)
	{
		return;
	}

	FString SceneArg;
	if (FParse::Value(Cmd, TEXT("-scene="), SceneArg))
	{
		if (SceneArg.Equals(TEXT("aurora"), ESearchCase::IgnoreCase) ||
			SceneArg.Equals(TEXT("snowbound"), ESearchCase::IgnoreCase))
		{
			SelectedScene = EKomorebiWorldScene::Snowbound;
		}
		else
		{
			SelectedScene = EKomorebiWorldScene::NeoKyoto;
		}
	}

	FString ModeArg;
	if (FParse::Value(Cmd, TEXT("-mode="), ModeArg))
	{
		if (ModeArg.Equals(TEXT("roam"), ESearchCase::IgnoreCase))
		{
			SelectedPlayMode = (SelectedScene == EKomorebiWorldScene::NeoKyoto)
				? EKomorebiPlayMode::FreeRoam
				: EKomorebiPlayMode::StorySectors;
		}
		else if (ModeArg.Equals(TEXT("online"), ESearchCase::IgnoreCase))
		{
			SelectedPlayMode = EKomorebiPlayMode::OnlineCity;
		}
		else
		{
			SelectedPlayMode = EKomorebiPlayMode::StorySectors;
		}
	}

	FString RoomArg;
	if (FParse::Value(Cmd, TEXT("-room="), RoomArg))
	{
		const std::string Sanitized = KomorebiCore::SanitizeRoomName(TCHAR_TO_UTF8(*RoomArg));
		PendingRoomCode = KomorebiBridge::ToFString(Sanitized);
		bPendingPrivateRoom = !PendingRoomCode.IsEmpty();
	}

	if (FParse::Param(Cmd, TEXT("fresh")))
	{
		bIgnoreSavedCheckpoint = true;
	}

	if (FParse::Param(Cmd, TEXT("tour")))
	{
		bStartWithShowcaseTour = true;
	}
}

FString UKomorebiGameInstance::GetMapPathForScene(EKomorebiWorldScene Scene)
{
	return (Scene == EKomorebiWorldScene::Snowbound)
		? TEXT("/Game/Maps/L_Snowbound")
		: TEXT("/Game/Maps/L_NeoKyoto");
}

FString UKomorebiGameInstance::GetPoolNameForScene(EKomorebiWorldScene Scene)
{
	return (Scene == EKomorebiWorldScene::Snowbound)
		? TEXT("aurora")
		: TEXT("autumn");
}

void UKomorebiGameInstance::TravelToSceneAndMode(
	EKomorebiWorldScene Scene,
	EKomorebiPlayMode Mode,
	const FString& RoomCode,
	const FString& ServerAddress,
	bool bPrivateRoom)
{
	// Snowbound does not have a separate Free Roam mode; fall back to Solo Story/Prologue.
	if (Scene == EKomorebiWorldScene::Snowbound && Mode == EKomorebiPlayMode::FreeRoam)
	{
		Mode = EKomorebiPlayMode::StorySectors;
	}

	SelectedScene = Scene;
	SelectedPlayMode = Mode;
	const std::string CleanRoom = KomorebiCore::SanitizeRoomName(TCHAR_TO_UTF8(*RoomCode));
	PendingRoomCode = KomorebiBridge::ToFString(CleanRoom);
	PendingServerAddress = ServerAddress.TrimStartAndEnd();
	bPendingPrivateRoom = bPrivateRoom;

	SaveLocalProfile();

	UOnlineSessionSubsystem* SessionSub = GetOnlineSessionSubsystem();

	if (Mode == EKomorebiPlayMode::OnlineCity)
	{
		if (SessionSub)
		{
			if (!PendingServerAddress.IsEmpty())
			{
				SessionSub->JoinByAddress(PendingServerAddress);
				return;
			}
			if (bPrivateRoom || !PendingRoomCode.IsEmpty())
			{
				SessionSub->HostOnlineCity(bPrivateRoom, PendingRoomCode, Scene);
				return;
			}
			SessionSub->QuickMatch(Scene);
			return;
		}

		// Fallback listen-server travel if OnlineSessionSubsystem is unavailable.
		const FString MapPath = GetMapPathForScene(Scene);
		FString Options = FString::Printf(
			TEXT("listen?game=/Script/KomorebiGame.KomorebiOnlineGameMode?mode=online?pool=%s"),
			*GetPoolNameForScene(Scene));
		if (!PendingRoomCode.IsEmpty())
		{
			Options += FString::Printf(TEXT("?room=%s"), *PendingRoomCode);
		}
		if (bPrivateRoom)
		{
			Options += TEXT("?private=1");
		}
		UGameplayStatics::OpenLevel(this, FName(*MapPath), true, Options);
		return;
	}

	// Leaving Online City for Solo / Free Roam destroys any active session first.
	if (SessionSub && SessionSub->IsSessionActive())
	{
		SessionSub->LeaveSession();
	}

	const FString MapPath = GetMapPathForScene(Scene);
	const FString ModeParam = (Mode == EKomorebiPlayMode::FreeRoam) ? TEXT("roam") : TEXT("level");
	const FString Options = FString::Printf(
		TEXT("game=/Script/KomorebiGame.KomorebiGameMode?mode=%s"),
		*ModeParam);
	UGameplayStatics::OpenLevel(this, FName(*MapPath), true, Options);
}

UOnlineSessionSubsystem* UKomorebiGameInstance::GetOnlineSessionSubsystem() const
{
	return GetSubsystem<UOnlineSessionSubsystem>();
}

UGameInstanceSubsystem* UKomorebiGameInstance::FindSiblingSubsystem(FName ClassName) const
{
	if (ClassName.IsNone())
	{
		return nullptr;
	}

	UPackage* GamePackage = FindPackage(nullptr, TEXT("/Script/KomorebiGame"));
	UClass* SubClass = GamePackage ? FindObject<UClass>(GamePackage, *ClassName.ToString()) : nullptr;
	if (!SubClass || !SubClass->IsChildOf(UGameInstanceSubsystem::StaticClass()))
	{
		return nullptr;
	}

	return GetSubsystemBase(SubClass);
}

UKomorebiAudioSubsystem* UKomorebiGameInstance::GetKomorebiAudioSubsystem() const
{
	return GetSubsystem<UKomorebiAudioSubsystem>();
}

UGraphicsSettingsSubsystem* UKomorebiGameInstance::GetGraphicsSettingsSubsystem() const
{
	return GetSubsystem<UGraphicsSettingsSubsystem>();
}

UMissionSubsystem* UKomorebiGameInstance::GetMissionSubsystem() const
{
	if (UWorld* World = GetWorld())
	{
		return World->GetSubsystem<UMissionSubsystem>();
	}
	return nullptr;
}

void UKomorebiGameInstance::SetLocalOnlineStats(const KomorebiCore::OnlineStats& InStats)
{
	LocalOnlineStats = InStats;
	SaveLocalProfile();
	OnProfileUpdated.Broadcast();
}

TArray<FString> UKomorebiGameInstance::EarnOnlineStat(const FString& StatName)
{
	const KomorebiCore::OnlineStats Before = LocalOnlineStats;

	if (StatName.Equals(TEXT("contracts"), ESearchCase::IgnoreCase))
	{
		++LocalOnlineStats.Contracts;
	}
	else if (StatName.Equals(TEXT("mechs"), ESearchCase::IgnoreCase))
	{
		++LocalOnlineStats.Mechs;
	}
	else if (StatName.Equals(TEXT("races"), ESearchCase::IgnoreCase))
	{
		++LocalOnlineStats.Races;
	}
	else if (StatName.Equals(TEXT("duels"), ESearchCase::IgnoreCase))
	{
		++LocalOnlineStats.Duels;
	}
	else if (StatName.Equals(TEXT("revives"), ESearchCase::IgnoreCase))
	{
		++LocalOnlineStats.Revives;
	}

	TArray<FString> UnlockToasts;
	for (const KomorebiCore::OnlineTitle& Title : KomorebiCore::GetOnlineTitles())
	{
		if (!KomorebiCore::IsOnlineUnlockEarned(Before, Title) &&
			KomorebiCore::IsOnlineUnlockEarned(LocalOnlineStats, Title))
		{
			UnlockToasts.Add(FString::Printf(TEXT("Title unlocked · %s"), *KomorebiBridge::ToFString(Title.Label)));
		}
	}

	for (const KomorebiCore::OnlineColor& Color : KomorebiCore::GetOnlineColors())
	{
		if (!KomorebiCore::IsOnlineUnlockEarned(Before, Color) &&
			KomorebiCore::IsOnlineUnlockEarned(LocalOnlineStats, Color))
		{
			UnlockToasts.Add(FString::Printf(TEXT("New name colour unlocked · %s"), *KomorebiBridge::ToFString(Color.DisplayName)));
		}
	}

	SaveLocalProfile();
	OnProfileUpdated.Broadcast();
	return UnlockToasts;
}

void UKomorebiGameInstance::UpdateLocalOnlineProfile(const FString& InNickname, const FString& InTitle, int32 InColorIndex)
{
	LocalPlayerNickname = AKomorebiPlayerState::SanitizeDisplayName(InNickname);
	LocalEquippedTitle = AKomorebiPlayerState::SanitizeTitle(InTitle);
	LocalEquippedColorIndex = FMath::Clamp(InColorIndex, 0, static_cast<int32>(KomorebiCore::GetOnlineColors().size()) - 1);

	SaveLocalProfile();
	OnProfileUpdated.Broadcast();

	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (AKomorebiPlayerState* PS = PC->GetPlayerState<AKomorebiPlayerState>())
			{
				PS->ServerSetProfile(LocalPlayerNickname, LocalEquippedTitle, LocalEquippedColorIndex);
			}
		}
	}
}

void UKomorebiGameInstance::ApplyAudioSettings(const KomorebiCore::AudioSettings& NewSettings)
{
	CachedAudioSettings = KomorebiCore::NormalizeAudioSettings(NewSettings);
	SaveLocalProfile();
	OnAudioMixChanged.Broadcast(CachedAudioSettings);
	DispatchAudioSettingsToSibling();
}

void UKomorebiGameInstance::RequestVoiceReplay(const FString& SpeakerId, const FString& Line)
{
	OnPlayVoiceRequested.Broadcast(SpeakerId, Line);

	if (UKomorebiAudioSubsystem* AudioSub = GetKomorebiAudioSubsystem())
	{
		AudioSub->SpeakRadioLine(Line, SpeakerId, true, true);
	}
}

void UKomorebiGameInstance::ApplyQualityTier(EKomorebiQualityTier Tier)
{
	CachedQualityTier = Tier;
	SaveLocalProfile();

	if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		int32 Level = 2;
		switch (Tier)
		{
			case EKomorebiQualityTier::Low: Level = 0; break;
			case EKomorebiQualityTier::Medium: Level = 1; break;
			case EKomorebiQualityTier::High: Level = 3; break;
			case EKomorebiQualityTier::Auto: default: Level = 2; break;
		}
		Settings->SetOverallScalabilityLevel(Level);
		Settings->ApplySettings(false);
	}

	OnQualityTierChanged.Broadcast(Tier);
	DispatchGraphicsSettingsToSibling();
}

void UKomorebiGameInstance::ApplyRenderStyleToggles(bool bInComicInk, bool bInOilPaintImpasto)
{
	bComicInkEnabled = bInComicInk;
	bOilPaintImpastoEnabled = bInOilPaintImpasto;
	SaveLocalProfile();

	if (IConsoleVariable* InkVar = IConsoleManager::Get().FindConsoleVariable(TEXT("komorebi.ComicInk")))
	{
		InkVar->Set(bComicInkEnabled ? 1.0f : 0.0f);
	}
	if (IConsoleVariable* ImpastoVar = IConsoleManager::Get().FindConsoleVariable(TEXT("komorebi.Impasto")))
	{
		ImpastoVar->Set(bOilPaintImpastoEnabled ? 1.0f : 0.0f);
	}

	OnRenderStyleChanged.Broadcast(bComicInkEnabled, bOilPaintImpastoEnabled);
	DispatchGraphicsSettingsToSibling();
}

void UKomorebiGameInstance::RequestDayNightShift()
{
	if (UWorld* World = GetWorld())
	{
		if (ADayNightAtmosphereActor* Atmo = Cast<ADayNightAtmosphereActor>(
			UGameplayStatics::GetActorOfClass(World, ADayNightAtmosphereActor::StaticClass())))
		{
			Atmo->ShiftPhase();
		}
	}
	OnDayNightShiftRequested.Broadcast();
}

void UKomorebiGameInstance::RequestShowcaseCommand(int32 Command)
{
	if (UWorld* World = GetWorld())
	{
		if (AShowcaseCameraDirector* Director = Cast<AShowcaseCameraDirector>(
			UGameplayStatics::GetActorOfClass(World, AShowcaseCameraDirector::StaticClass())))
		{
			if (Command == -1)
			{
				Director->StopTour(false);
			}
			else if (Command == -2)
			{
				Director->ToggleTour();
			}
			else if (Command == -3)
			{
				Director->NextShot();
			}
			else if (Command >= 0)
			{
				Director->JumpToShot(Command);
			}
		}
	}
	OnShowcaseCommand.Broadcast(Command);
}

void UKomorebiGameInstance::MarkEchoUnlocked(const FString& EchoId)
{
	if (!EchoId.IsEmpty())
	{
		UnlockedEchoIds.Add(EchoId);
		SaveLocalProfile();
	}
}

void UKomorebiGameInstance::SetUnlockedEchoIds(const TSet<FString>& InEchoIds)
{
	UnlockedEchoIds = InEchoIds;
	SaveLocalProfile();
}

void UKomorebiGameInstance::DispatchAudioSettingsToSibling()
{
	if (UKomorebiAudioSubsystem* AudioSub = GetKomorebiAudioSubsystem())
	{
		AudioSub->SetAudioSettings(FKomorebiAudioSettings::FromCore(CachedAudioSettings), true);
	}
}

void UKomorebiGameInstance::DispatchGraphicsSettingsToSibling()
{
	if (UGraphicsSettingsSubsystem* GfxSub = GetGraphicsSettingsSubsystem())
	{
		GfxSub->SetQualityTier(CachedQualityTier);
		GfxSub->SetStylization(bComicInkEnabled, bOilPaintImpastoEnabled);
	}
}

void UKomorebiGameInstance::LoadLocalProfile()
{
	if (GConfig)
	{
		GConfig->GetString(kProfileConfigSection, TEXT("Nickname"), LocalPlayerNickname, GGameUserSettingsIni);
		GConfig->GetString(kProfileConfigSection, TEXT("Title"), LocalEquippedTitle, GGameUserSettingsIni);
		GConfig->GetInt(kProfileConfigSection, TEXT("ColorIndex"), LocalEquippedColorIndex, GGameUserSettingsIni);
		GConfig->GetInt(kProfileConfigSection, TEXT("StatContracts"), LocalOnlineStats.Contracts, GGameUserSettingsIni);
		GConfig->GetInt(kProfileConfigSection, TEXT("StatMechs"), LocalOnlineStats.Mechs, GGameUserSettingsIni);
		GConfig->GetInt(kProfileConfigSection, TEXT("StatRaces"), LocalOnlineStats.Races, GGameUserSettingsIni);
		GConfig->GetInt(kProfileConfigSection, TEXT("StatDuels"), LocalOnlineStats.Duels, GGameUserSettingsIni);
		GConfig->GetInt(kProfileConfigSection, TEXT("StatRevives"), LocalOnlineStats.Revives, GGameUserSettingsIni);

		float Master = static_cast<float>(CachedAudioSettings.Master);
		float Music = static_cast<float>(CachedAudioSettings.Music);
		float Voice = static_cast<float>(CachedAudioSettings.Voice);
		float Sfx = static_cast<float>(CachedAudioSettings.Sfx);
		bool bMuted = CachedAudioSettings.bMuted;
		GConfig->GetFloat(kProfileConfigSection, TEXT("VolMaster"), Master, GGameUserSettingsIni);
		GConfig->GetFloat(kProfileConfigSection, TEXT("VolMusic"), Music, GGameUserSettingsIni);
		GConfig->GetFloat(kProfileConfigSection, TEXT("VolVoice"), Voice, GGameUserSettingsIni);
		GConfig->GetFloat(kProfileConfigSection, TEXT("VolSfx"), Sfx, GGameUserSettingsIni);
		GConfig->GetBool(kProfileConfigSection, TEXT("AudioMuted"), bMuted, GGameUserSettingsIni);
		CachedAudioSettings = KomorebiCore::NormalizeAudioSettings({ Master, Music, Voice, Sfx, bMuted });

		int32 QualityInt = static_cast<int32>(CachedQualityTier);
		GConfig->GetInt(kProfileConfigSection, TEXT("QualityTier"), QualityInt, GGameUserSettingsIni);
		CachedQualityTier = static_cast<EKomorebiQualityTier>(FMath::Clamp(QualityInt, 0, 3));
		GConfig->GetBool(kProfileConfigSection, TEXT("ComicInk"), bComicInkEnabled, GGameUserSettingsIni);
		GConfig->GetBool(kProfileConfigSection, TEXT("OilPaintImpasto"), bOilPaintImpastoEnabled, GGameUserSettingsIni);

		FString EchoCsv;
		if (GConfig->GetString(kProfileConfigSection, TEXT("UnlockedEchoes"), EchoCsv, GGameUserSettingsIni))
		{
			TArray<FString> Parts;
			EchoCsv.ParseIntoArray(Parts, TEXT(","), true);
			UnlockedEchoIds.Reset();
			for (const FString& Part : Parts)
			{
				UnlockedEchoIds.Add(Part.TrimStartAndEnd());
			}
		}
	}

	if (LocalPlayerNickname.IsEmpty())
	{
		LocalPlayerNickname = FString::Printf(TEXT("Wanderer-%d"), FMath::RandRange(1000, 9999));
	}
	else
	{
		LocalPlayerNickname = AKomorebiPlayerState::SanitizeDisplayName(LocalPlayerNickname);
	}
	LocalEquippedColorIndex = FMath::Clamp(LocalEquippedColorIndex, 0, static_cast<int32>(KomorebiCore::GetOnlineColors().size()) - 1);
}

void UKomorebiGameInstance::SaveLocalProfile() const
{
	if (!GConfig)
	{
		return;
	}

	GConfig->SetString(kProfileConfigSection, TEXT("Nickname"), *LocalPlayerNickname, GGameUserSettingsIni);
	GConfig->SetString(kProfileConfigSection, TEXT("Title"), *LocalEquippedTitle, GGameUserSettingsIni);
	GConfig->SetInt(kProfileConfigSection, TEXT("ColorIndex"), LocalEquippedColorIndex, GGameUserSettingsIni);
	GConfig->SetInt(kProfileConfigSection, TEXT("StatContracts"), LocalOnlineStats.Contracts, GGameUserSettingsIni);
	GConfig->SetInt(kProfileConfigSection, TEXT("StatMechs"), LocalOnlineStats.Mechs, GGameUserSettingsIni);
	GConfig->SetInt(kProfileConfigSection, TEXT("StatRaces"), LocalOnlineStats.Races, GGameUserSettingsIni);
	GConfig->SetInt(kProfileConfigSection, TEXT("StatDuels"), LocalOnlineStats.Duels, GGameUserSettingsIni);
	GConfig->SetInt(kProfileConfigSection, TEXT("StatRevives"), LocalOnlineStats.Revives, GGameUserSettingsIni);

	GConfig->SetFloat(kProfileConfigSection, TEXT("VolMaster"), static_cast<float>(CachedAudioSettings.Master), GGameUserSettingsIni);
	GConfig->SetFloat(kProfileConfigSection, TEXT("VolMusic"), static_cast<float>(CachedAudioSettings.Music), GGameUserSettingsIni);
	GConfig->SetFloat(kProfileConfigSection, TEXT("VolVoice"), static_cast<float>(CachedAudioSettings.Voice), GGameUserSettingsIni);
	GConfig->SetFloat(kProfileConfigSection, TEXT("VolSfx"), static_cast<float>(CachedAudioSettings.Sfx), GGameUserSettingsIni);
	GConfig->SetBool(kProfileConfigSection, TEXT("AudioMuted"), CachedAudioSettings.bMuted, GGameUserSettingsIni);

	GConfig->SetInt(kProfileConfigSection, TEXT("QualityTier"), static_cast<int32>(CachedQualityTier), GGameUserSettingsIni);
	GConfig->SetBool(kProfileConfigSection, TEXT("ComicInk"), bComicInkEnabled, GGameUserSettingsIni);
	GConfig->SetBool(kProfileConfigSection, TEXT("OilPaintImpasto"), bOilPaintImpastoEnabled, GGameUserSettingsIni);

	TArray<FString> EchoArray = UnlockedEchoIds.Array();
	EchoArray.Sort();
	GConfig->SetString(kProfileConfigSection, TEXT("UnlockedEchoes"), *FString::Join(EchoArray, TEXT(",")), GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}
