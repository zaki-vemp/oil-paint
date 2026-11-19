// Copyright Komorebi Project. Replicated PlayerState holding sanitized nameplate, title, neon color, downed/revive state, cash, and stats.
#include "Network/KomorebiPlayerState.h"

#include "Network/KomorebiGameInstance.h"
#include "UI/KomorebiHUD.h"
#include "KomorebiGameTypes.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

AKomorebiPlayerState::AKomorebiPlayerState()
{
	bReplicates = true;
	NetUpdateFrequency = 10.0f; // Matches kOnlineSendInterval = 0.1s (10 Hz)
}

void AKomorebiPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AKomorebiPlayerState, DisplayName);
	DOREPLIFETIME(AKomorebiPlayerState, EquippedTitle);
	DOREPLIFETIME(AKomorebiPlayerState, EquippedColorIndex);
	DOREPLIFETIME(AKomorebiPlayerState, bIsDowned);
	DOREPLIFETIME(AKomorebiPlayerState, DownedTimeRemaining);
	DOREPLIFETIME(AKomorebiPlayerState, ReviveProgress);
	DOREPLIFETIME(AKomorebiPlayerState, PlayerHealth);
	DOREPLIFETIME(AKomorebiPlayerState, bIsDrivingVehicle);
	DOREPLIFETIME(AKomorebiPlayerState, OnlineCash);
	DOREPLIFETIME(AKomorebiPlayerState, StatContracts);
	DOREPLIFETIME(AKomorebiPlayerState, StatMechs);
	DOREPLIFETIME(AKomorebiPlayerState, StatRaces);
	DOREPLIFETIME(AKomorebiPlayerState, StatDuels);
	DOREPLIFETIME(AKomorebiPlayerState, StatRevives);
}

namespace
{
	FString CleanString(const FString& Raw, int32 MaxLen, const FString& Fallback)
	{
		FString Filtered;
		Filtered.Reserve(FMath::Min(Raw.Len(), MaxLen));
		for (TCHAR Ch : Raw)
		{
			if (FChar::IsAlnum(Ch) || Ch == TEXT(' ') || Ch == TEXT('_') || Ch == TEXT('.') || Ch == TEXT('-'))
			{
				Filtered.AppendChar(Ch);
			}
		}
		Filtered.TrimStartAndEndInline();
		if (Filtered.Len() > MaxLen)
		{
			Filtered.LeftInline(MaxLen);
		}
		return Filtered.IsEmpty() ? Fallback : Filtered;
	}
}

FString AKomorebiPlayerState::SanitizeDisplayName(const FString& RawName)
{
	return CleanString(RawName, 16, TEXT("Wanderer"));
}

FString AKomorebiPlayerState::SanitizeTitle(const FString& RawTitle)
{
	return CleanString(RawTitle, 24, TEXT(""));
}

FString AKomorebiPlayerState::GetFormattedNameplate() const
{
	if (EquippedTitle.IsEmpty())
	{
		return DisplayName;
	}
	return FString::Printf(TEXT("%s · %s"), *DisplayName, *EquippedTitle);
}

FLinearColor AKomorebiPlayerState::GetEquippedLinearColor() const
{
	const auto& Colors = KomorebiCore::GetOnlineColors();
	if (Colors.empty())
	{
		return FLinearColor(0.0f, 0.94f, 1.0f, 1.0f);
	}
	const int32 ClampedIdx = FMath::Clamp(EquippedColorIndex, 0, static_cast<int32>(Colors.size()) - 1);
	const FString Hex = KomorebiBridge::ToFString(Colors[static_cast<size_t>(ClampedIdx)].Hex);
	return FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
}

KomorebiCore::OnlineStats AKomorebiPlayerState::GetOnlineStats() const
{
	return KomorebiCore::OnlineStats{
		StatContracts,
		StatMechs,
		StatRaces,
		StatDuels,
		StatRevives
	};
}

void AKomorebiPlayerState::SetOnlineStats(const KomorebiCore::OnlineStats& InStats)
{
	StatContracts = FMath::Max(0, InStats.Contracts);
	StatMechs = FMath::Max(0, InStats.Mechs);
	StatRaces = FMath::Max(0, InStats.Races);
	StatDuels = FMath::Max(0, InStats.Duels);
	StatRevives = FMath::Max(0, InStats.Revives);
}

void AKomorebiPlayerState::ServerSetProfile_Implementation(const FString& InDisplayName, const FString& InTitle, int32 InColorIndex)
{
	DisplayName = SanitizeDisplayName(InDisplayName);
	SetPlayerName(DisplayName);

	const KomorebiCore::OnlineStats Stats = GetOnlineStats();

	// Validate title against unlock table (free index 0 is always allowed).
	const FString CleanTitle = SanitizeTitle(InTitle);
	FString ValidatedTitle;
	for (const KomorebiCore::OnlineTitle& Entry : KomorebiCore::GetOnlineTitles())
	{
		const FString EntryLabel = KomorebiBridge::ToFString(Entry.Label);
		if (EntryLabel.Equals(CleanTitle, ESearchCase::CaseSensitive) &&
			KomorebiCore::IsOnlineUnlockEarned(Stats, Entry))
		{
			ValidatedTitle = EntryLabel;
			break;
		}
	}
	EquippedTitle = ValidatedTitle;

	// Validate color index against unlock table (index 0 Cyan is always allowed).
	const auto& Colors = KomorebiCore::GetOnlineColors();
	const int32 CandidateColor = FMath::Clamp(InColorIndex, 0, static_cast<int32>(Colors.size()) - 1);
	if (KomorebiCore::IsOnlineUnlockEarned(Stats, Colors[static_cast<size_t>(CandidateColor)]))
	{
		EquippedColorIndex = CandidateColor;
	}
	else
	{
		EquippedColorIndex = 0;
	}

	OnRep_Profile();
}

void AKomorebiPlayerState::ServerSyncInitialStats_Implementation(int32 InContracts, int32 InMechs, int32 InRaces, int32 InDuels, int32 InRevives)
{
	StatContracts = FMath::Max(StatContracts, FMath::Clamp(InContracts, 0, 99999));
	StatMechs = FMath::Max(StatMechs, FMath::Clamp(InMechs, 0, 99999));
	StatRaces = FMath::Max(StatRaces, FMath::Clamp(InRaces, 0, 99999));
	StatDuels = FMath::Max(StatDuels, FMath::Clamp(InDuels, 0, 99999));
	StatRevives = FMath::Max(StatRevives, FMath::Clamp(InRevives, 0, 99999));
	OnRep_Stats();
}

void AKomorebiPlayerState::ClientNotifyStatEarned_Implementation(const FString& StatName, int32 NewValue)
{
	UWorld* World = GetWorld();
	UKomorebiGameInstance* GI = World ? Cast<UKomorebiGameInstance>(World->GetGameInstance()) : nullptr;
	if (!GI)
	{
		return;
	}

	const TArray<FString> UnlockToasts = GI->EarnOnlineStat(StatName);
	if (APlayerController* LocalPC = World->GetFirstPlayerController())
	{
		if (AKomorebiHUD* HUD = Cast<AKomorebiHUD>(LocalPC->GetHUD()))
		{
			for (const FString& ToastMsg : UnlockToasts)
			{
				HUD->EnqueueToast(ToastMsg);
			}
		}
	}
}

void AKomorebiPlayerState::EnterDownedState()
{
	if (!HasAuthority())
	{
		return;
	}
	bIsDowned = true;
	DownedTimeRemaining = static_cast<float>(KomorebiCore::kDownedTimeout);
	ReviveProgress = 0.0f;
	PlayerHealth = 0.0f;
	OnRep_DownedState();
}

void AKomorebiPlayerState::CompleteRevive()
{
	if (!HasAuthority())
	{
		return;
	}
	bIsDowned = false;
	DownedTimeRemaining = 0.0f;
	ReviveProgress = 0.0f;
	PlayerHealth = 45.0f; // Lifts downed teammate to 45 HP + 1.5s invuln
	if (UWorld* World = GetWorld())
	{
		InvulnerableUntilWorldTime = World->GetTimeSeconds() + 1.5;
	}
	OnRep_DownedState();
}

void AKomorebiPlayerState::ResetAfterBleedout()
{
	if (!HasAuthority())
	{
		return;
	}
	bIsDowned = false;
	DownedTimeRemaining = 0.0f;
	ReviveProgress = 0.0f;
	PlayerHealth = static_cast<float>(KomorebiCore::kMaxPlayerHp);
	if (UWorld* World = GetWorld())
	{
		InvulnerableUntilWorldTime = World->GetTimeSeconds() + 2.0;
	}
	OnRep_DownedState();
}

void AKomorebiPlayerState::AddOnlineCash(int32 DeltaCash)
{
	if (!HasAuthority())
	{
		return;
	}
	OnlineCash = FMath::Max(0, OnlineCash + DeltaCash);
}

void AKomorebiPlayerState::IncrementOnlineStat(const FString& StatName)
{
	if (!HasAuthority())
	{
		return;
	}

	int32 NewVal = 0;
	if (StatName.Equals(TEXT("contracts"), ESearchCase::IgnoreCase))
	{
		NewVal = ++StatContracts;
	}
	else if (StatName.Equals(TEXT("mechs"), ESearchCase::IgnoreCase))
	{
		NewVal = ++StatMechs;
	}
	else if (StatName.Equals(TEXT("races"), ESearchCase::IgnoreCase))
	{
		NewVal = ++StatRaces;
	}
	else if (StatName.Equals(TEXT("duels"), ESearchCase::IgnoreCase))
	{
		NewVal = ++StatDuels;
	}
	else if (StatName.Equals(TEXT("revives"), ESearchCase::IgnoreCase))
	{
		NewVal = ++StatRevives;
	}

	OnRep_Stats();
	ClientNotifyStatEarned(StatName, NewVal);
}

void AKomorebiPlayerState::OnRep_Profile()
{
	OnProfileReplicated.Broadcast();
}

void AKomorebiPlayerState::OnRep_DownedState()
{
	OnProfileReplicated.Broadcast();
}

void AKomorebiPlayerState::OnRep_Stats()
{
	OnProfileReplicated.Broadcast();
}
