// Copyright Komorebi Project. Native UE5 SaveGame container and failure-aware persistence manager.
#include "Campaign/KomorebiSaveGame.h"

#include "Game/LevelsData.h"
#include "Game/StoryDatabase.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogKomorebiSave, Log, All);

bool UKomorebiSaveManager::bHasWarnedSaveFailureThisSession = false;

UKomorebiSaveGame::UKomorebiSaveGame()
{
	Normalize();
}

void UKomorebiSaveGame::Normalize()
{
	const std::vector<KomorebiCore::LevelSpec>& Levels = KomorebiCore::GetLevels();
	const int32 LevelCount = static_cast<int32>(Levels.size());

	ClearedSectors = FMath::Clamp(ClearedSectors, 0, LevelCount);
	Cash = FMath::Max(0, Cash);
	bWon = (ClearedSectors >= LevelCount) && bWon;

	// Checkpoint is always derived from the cleared sector's relay so saves survive map rescaling.
	if (LevelCount > 0)
	{
		const int32 CpIdx = FMath::Clamp(ClearedSectors, 0, LevelCount - 1);
		CheckpointXZ = FVector2D(Levels[CpIdx].Checkpoint.X, Levels[CpIdx].Checkpoint.Z);
	}

	// Ensure baseline vehicle unlocks for the cleared sector count are present without duplicates.
	const std::vector<KomorebiCore::EVehicleKind> RequiredUnlocks = KomorebiCore::UnlocksFor(ClearedSectors);
	TArray<EKomorebiVehicleKind> CleanUnlocks;
	for (KomorebiCore::EVehicleKind CoreKind : RequiredUnlocks)
	{
		CleanUnlocks.AddUnique(static_cast<EKomorebiVehicleKind>(CoreKind));
	}
	for (EKomorebiVehicleKind Existing : UnlockedVehicles)
	{
		CleanUnlocks.AddUnique(Existing);
	}
	UnlockedVehicles = MoveTemp(CleanUnlocks);

	// Deduplicate and clamp completed mission sectors; every cleared sector has its field op completed.
	TArray<int32> CleanMissions;
	for (int32 i = 0; i < ClearedSectors; ++i)
	{
		CleanMissions.AddUnique(i);
	}
	for (int32 Sec : CompletedMissionSectors)
	{
		if (Sec >= 0 && Sec < LevelCount)
		{
			CleanMissions.AddUnique(Sec);
		}
	}
	CleanMissions.Sort();
	CompletedMissionSectors = MoveTemp(CleanMissions);

	// Deduplicate collected echo IDs and keep only valid EchoShard IDs.
	TArray<FString> CleanEchoes;
	for (const FString& Id : CollectedEchoIds)
	{
		const std::string Utf8(TCHAR_TO_UTF8(*Id));
		if (KomorebiCore::FindEchoShard(Utf8) != nullptr)
		{
			CleanEchoes.AddUnique(Id);
		}
	}
	CollectedEchoIds = MoveTemp(CleanEchoes);

	// Normalize audio volumes using KomorebiCore's canonical rules.
	const KomorebiCore::AudioSettings NormalizedAudio = KomorebiCore::NormalizeAudioSettings(ToCoreAudioSettings());
	ApplyCoreAudioSettings(NormalizedAudio);

	// Normalize online profile fields and verify unlocked title/color bounds.
	PlayerName = PlayerName.TrimStartAndEnd();
	if (PlayerName.IsEmpty())
	{
		PlayerName = TEXT("Courier-07");
	}
	if (PlayerName.Len() > 24)
	{
		PlayerName.LeftInline(24);
	}

	Contracts = FMath::Max(0, Contracts);
	Mechs = FMath::Max(0, Mechs);
	Races = FMath::Max(0, Races);
	Duels = FMath::Max(0, Duels);
	Revives = FMath::Max(0, Revives);

	const KomorebiCore::OnlineStats Stats = ToCoreOnlineStats();
	const std::vector<KomorebiCore::OnlineTitle>& Titles = KomorebiCore::GetOnlineTitles();
	bool bTitleValid = false;
	for (const KomorebiCore::OnlineTitle& Title : Titles)
	{
		const FString Candidate = KomorebiBridge::ToFString(Title.Label);
		if (Candidate.Equals(EquippedTitle, ESearchCase::IgnoreCase)
			&& KomorebiCore::IsOnlineUnlockEarned(Stats, Title))
		{
			EquippedTitle = Candidate;
			bTitleValid = true;
			break;
		}
	}
	if (!bTitleValid && !Titles.empty())
	{
		EquippedTitle = KomorebiBridge::ToFString(Titles[0].Label);
	}

	const std::vector<KomorebiCore::OnlineColor>& Colors = KomorebiCore::GetOnlineColors();
	if (Colors.empty()
		|| EquippedColorIndex < 0
		|| EquippedColorIndex >= static_cast<int32>(Colors.size())
		|| !KomorebiCore::IsOnlineUnlockEarned(Stats, Colors[EquippedColorIndex]))
	{
		EquippedColorIndex = 0;
	}
}

void UKomorebiSaveGame::ResetCampaignOnly(bool bAlsoResetEchoes)
{
	ClearedSectors = 0;
	Cash = 0;
	UnlockedVehicles.Reset();
	CompletedMissionSectors.Reset();
	bWon = false;
	if (bAlsoResetEchoes)
	{
		CollectedEchoIds.Reset();
	}
	Normalize();
}

bool UKomorebiSaveGame::HasCollectedEcho(const FString& EchoId) const
{
	return CollectedEchoIds.Contains(EchoId);
}

bool UKomorebiSaveGame::AddCollectedEcho(const FString& EchoId)
{
	const std::string Utf8(TCHAR_TO_UTF8(*EchoId));
	if (KomorebiCore::FindEchoShard(Utf8) == nullptr)
	{
		return false;
	}
	if (CollectedEchoIds.Contains(EchoId))
	{
		return false;
	}
	CollectedEchoIds.Add(EchoId);
	return true;
}

bool UKomorebiSaveGame::IsMissionSectorCompleted(int32 SectorIndex) const
{
	return SectorIndex < ClearedSectors || CompletedMissionSectors.Contains(SectorIndex);
}

void UKomorebiSaveGame::MarkMissionSectorCompleted(int32 SectorIndex)
{
	const int32 LevelCount = static_cast<int32>(KomorebiCore::GetLevels().size());
	if (SectorIndex >= 0 && SectorIndex < LevelCount)
	{
		CompletedMissionSectors.AddUnique(SectorIndex);
	}
}

bool UKomorebiSaveGame::IsVehicleUnlocked(EKomorebiVehicleKind Kind) const
{
	return UnlockedVehicles.Contains(Kind);
}

bool UKomorebiSaveGame::HasPower(EKomorebiPower Power) const
{
	return GetDerivedPowers().Has(KomorebiBridge::ToCorePowerKind(Power));
}

KomorebiCore::PowerSet UKomorebiSaveGame::GetDerivedPowers() const
{
	return KomorebiCore::PowersFor(ClearedSectors);
}

KomorebiCore::AudioSettings UKomorebiSaveGame::ToCoreAudioSettings() const
{
	return KomorebiCore::AudioSettings{
		static_cast<double>(Master),
		static_cast<double>(Music),
		static_cast<double>(Voice),
		static_cast<double>(Sfx),
		bMuted,
	};
}

void UKomorebiSaveGame::ApplyCoreAudioSettings(const KomorebiCore::AudioSettings& Settings)
{
	const KomorebiCore::AudioSettings Clean = KomorebiCore::NormalizeAudioSettings(Settings);
	Master = static_cast<float>(Clean.Master);
	Music = static_cast<float>(Clean.Music);
	Voice = static_cast<float>(Clean.Voice);
	Sfx = static_cast<float>(Clean.Sfx);
	bMuted = Clean.bMuted;
}

KomorebiCore::OnlineStats UKomorebiSaveGame::ToCoreOnlineStats() const
{
	return KomorebiCore::OnlineStats{
		Contracts,
		Mechs,
		Races,
		Duels,
		Revives,
	};
}

void UKomorebiSaveGame::ApplyCoreOnlineStats(const KomorebiCore::OnlineStats& Stats)
{
	Contracts = FMath::Max(0, Stats.Contracts);
	Mechs = FMath::Max(0, Stats.Mechs);
	Races = FMath::Max(0, Stats.Races);
	Duels = FMath::Max(0, Stats.Duels);
	Revives = FMath::Max(0, Stats.Revives);
}

UKomorebiSaveGame* UKomorebiSaveManager::LoadOrCreate(
	const FString& SlotName,
	int32 UserIndex,
	bool bFreshCampaign)
{
	const FString EffectiveSlot = SlotName.IsEmpty() ? FString(DefaultSlotName) : SlotName;
	UKomorebiSaveGame* SaveObj = nullptr;

	if (UGameplayStatics::DoesSaveGameExist(EffectiveSlot, UserIndex))
	{
		USaveGame* Loaded = UGameplayStatics::LoadGameFromSlot(EffectiveSlot, UserIndex);
		SaveObj = Cast<UKomorebiSaveGame>(Loaded);
		if (!SaveObj)
		{
			UE_LOG(LogKomorebiSave, Warning, TEXT("Save slot '%s' exists but could not be deserialized as UKomorebiSaveGame; creating fresh save."), *EffectiveSlot);
		}
	}

	if (!SaveObj)
	{
		SaveObj = Cast<UKomorebiSaveGame>(UGameplayStatics::CreateSaveGameObject(UKomorebiSaveGame::StaticClass()));
	}

	if (SaveObj)
	{
		if (bFreshCampaign)
		{
			SaveObj->ResetCampaignOnly(false);
		}
		else
		{
			SaveObj->Normalize();
		}
	}

	return SaveObj;
}

bool UKomorebiSaveManager::SaveWithToastCheck(
	UKomorebiSaveGame* SaveGameObject,
	FString& OutToastWarning,
	const FString& SlotName,
	int32 UserIndex)
{
	OutToastWarning.Reset();

	if (!SaveGameObject)
	{
		UE_LOG(LogKomorebiSave, Warning, TEXT("SaveWithToastCheck called with null SaveGameObject."));
		if (!bHasWarnedSaveFailureThisSession)
		{
			bHasWarnedSaveFailureThisSession = true;
			OutToastWarning = TEXT("Progress is not saving — storage unavailable");
		}
		return false;
	}

	SaveGameObject->Normalize();

	const FString EffectiveSlot = SlotName.IsEmpty() ? FString(DefaultSlotName) : SlotName;
	const bool bSaved = UGameplayStatics::SaveGameToSlot(SaveGameObject, EffectiveSlot, UserIndex);
	if (!bSaved)
	{
		UE_LOG(LogKomorebiSave, Error, TEXT("Failed to write Komorebi SaveGame to slot '%s' (UserIndex=%d)."), *EffectiveSlot, UserIndex);
		if (!bHasWarnedSaveFailureThisSession)
		{
			bHasWarnedSaveFailureThisSession = true;
			OutToastWarning = TEXT("Progress is not saving — check storage permissions");
		}
		return false;
	}

	return true;
}

bool UKomorebiSaveManager::SaveWithToastCheck(
	UKomorebiSaveGame* SaveGameObject,
	const FString& SlotName,
	int32 UserIndex)
{
	FString UnusedWarning;
	return SaveWithToastCheck(SaveGameObject, UnusedWarning, SlotName, UserIndex);
}

bool UKomorebiSaveManager::DeleteSlot(const FString& SlotName, int32 UserIndex)
{
	const FString EffectiveSlot = SlotName.IsEmpty() ? FString(DefaultSlotName) : SlotName;
	if (!UGameplayStatics::DoesSaveGameExist(EffectiveSlot, UserIndex))
	{
		return true;
	}
	return UGameplayStatics::DeleteGameInSlot(EffectiveSlot, UserIndex);
}

void UKomorebiSaveManager::ResetSaveWarningLatch()
{
	bHasWarnedSaveFailureThisSession = false;
}
