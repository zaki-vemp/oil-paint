// Copyright Komorebi Project. Graphics scalability & stylized finish subsystem implementation.
#include "World/GraphicsSettingsSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/PlatformMisc.h"
#include "UObject/UObjectGlobals.h"
#include "World/DayNightAtmosphereActor.h"
#include "World/NeoKyotoCityActor.h"
#include "World/SnowboundWorldActor.h"
#include "World/StreetPropsActor.h"

namespace
{
	// Profiles matching legacy-web/src/quality.js PROFILES (low / medium / high).
	static const int32 kAutoMinIndex = 0;
	static const int32 kAutoMaxIndex = 2;
}

const UGraphicsSettingsSubsystem::FQualityProfile& UGraphicsSettingsSubsystem::ProfileForTier(EKomorebiQualityTier Tier)
{
	static const FQualityProfile kLow{
		EKomorebiQualityTier::Low,
		TEXT("Low"),
		0.67f,
		0.35f,
		900,
		60.0f,
		0
	};
	static const FQualityProfile kMedium{
		EKomorebiQualityTier::Medium,
		TEXT("Balanced"),
		0.85f,
		0.65f,
		1500,
		75.0f,
		1
	};
	static const FQualityProfile kHigh{
		EKomorebiQualityTier::High,
		TEXT("High"),
		1.00f,
		1.00f,
		2400,
		85.0f,
		3
	};

	switch (Tier)
	{
	case EKomorebiQualityTier::Low:
		return kLow;
	case EKomorebiQualityTier::High:
		return kHigh;
	case EKomorebiQualityTier::Medium:
	case EKomorebiQualityTier::Auto:
	default:
		return kMedium;
	}
}

EKomorebiQualityTier UGraphicsSettingsSubsystem::TierFromAutoIndex(int32 Index)
{
	switch (FMath::Clamp(Index, kAutoMinIndex, kAutoMaxIndex))
	{
	case 0:
		return EKomorebiQualityTier::Low;
	case 2:
		return EKomorebiQualityTier::High;
	case 1:
	default:
		return EKomorebiQualityTier::Medium;
	}
}

int32 UGraphicsSettingsSubsystem::AutoIndexFromTier(EKomorebiQualityTier Tier)
{
	switch (Tier)
	{
	case EKomorebiQualityTier::Low:
		return 0;
	case EKomorebiQualityTier::High:
		return 2;
	case EKomorebiQualityTier::Medium:
	case EKomorebiQualityTier::Auto:
	default:
		return 1;
	}
}

void UGraphicsSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Matches legacy-web/src/quality.js line 42: start conservatively on 4-core or smaller hardware.
	const bool bModestHardware = (FPlatformMisc::NumberOfCores() <= 4);
	RequestedTier = EKomorebiQualityTier::Auto;
	ResetAutoPolicy(bModestHardware ? 0 : 1);

	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
		this,
		&UGraphicsSettingsSubsystem::HandlePostLoadMap);

	ApplyActiveProfile();
}

void UGraphicsSettingsSubsystem::Deinitialize()
{
	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
		PostLoadMapHandle.Reset();
	}

	Super::Deinitialize();
}

void UGraphicsSettingsSubsystem::HandlePostLoadMap(UWorld* /*LoadedWorld*/)
{
	ApplyToCurrentWorld();
}

void UGraphicsSettingsSubsystem::ResetAutoPolicy(int32 InitialIndex)
{
	AutoLevelIndex = FMath::Clamp(InitialIndex, kAutoMinIndex, kAutoMaxIndex);
	AverageFrameMs = 0.0f;
	WarmupSeconds = 3.0f;
	CooldownSeconds = 0.0f;
	SlowSeconds = 0.0f;
	FastSeconds = 0.0f;
}

EKomorebiQualityTier UGraphicsSettingsSubsystem::GetEffectiveTier() const
{
	if (RequestedTier != EKomorebiQualityTier::Auto)
	{
		return RequestedTier;
	}
	return TierFromAutoIndex(AutoLevelIndex);
}

int32 UGraphicsSettingsSubsystem::GetSmoothedFps() const
{
	return (AverageFrameMs > 0.0f) ? FMath::RoundToInt(1000.0f / AverageFrameMs) : 0;
}

FString UGraphicsSettingsSubsystem::GetQualityStatusLabel() const
{
	const FQualityProfile& Profile = ProfileForTier(GetEffectiveTier());
	if (RequestedTier == EKomorebiQualityTier::Auto)
	{
		return FString::Printf(TEXT("Auto · %s detail"), Profile.Label);
	}
	return FString::Printf(TEXT("%s detail"), Profile.Label);
}

void UGraphicsSettingsSubsystem::SetQualityTier(EKomorebiQualityTier InTier)
{
	const EKomorebiQualityTier PrevEffective = GetEffectiveTier();
	RequestedTier = InTier;
	ResetAutoPolicy(AutoIndexFromTier(PrevEffective));
	ApplyActiveProfile();
}

bool UGraphicsSettingsSubsystem::SampleFrameTime(float DeltaSeconds, bool bActiveGameplay)
{
	if (RequestedTier != EKomorebiQualityTier::Auto)
	{
		return false;
	}

	// Exact timing policy from legacy-web/src/quality.js lines 21-36.
	if (!bActiveGameplay || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f || DeltaSeconds > 0.25f)
	{
		SlowSeconds = 0.0f;
		FastSeconds = 0.0f;
		AverageFrameMs = 0.0f;
		WarmupSeconds = 1.0f;
		return false;
	}

	const float Ms = DeltaSeconds * 1000.0f;
	AverageFrameMs = (AverageFrameMs > 0.0f)
		? (AverageFrameMs + (Ms - AverageFrameMs) * (1.0f - FMath::Exp(-DeltaSeconds)))
		: Ms;

	if (WarmupSeconds > 0.0f)
	{
		WarmupSeconds -= DeltaSeconds;
		return false;
	}

	if (CooldownSeconds > 0.0f)
	{
		CooldownSeconds -= DeltaSeconds;
		return false;
	}

	SlowSeconds = (AverageFrameMs > 23.0f) ? (SlowSeconds + DeltaSeconds) : 0.0f;
	FastSeconds = (AverageFrameMs < 17.8f) ? (FastSeconds + DeltaSeconds) : 0.0f;

	const int32 NextIndex = (SlowSeconds >= 2.5f)
		? FMath::Max(kAutoMinIndex, AutoLevelIndex - 1)
		: ((FastSeconds >= 10.0f) ? FMath::Min(kAutoMaxIndex, AutoLevelIndex + 1) : AutoLevelIndex);

	if (NextIndex == AutoLevelIndex)
	{
		return false;
	}

	AutoLevelIndex = NextIndex;
	SlowSeconds = 0.0f;
	FastSeconds = 0.0f;
	CooldownSeconds = 6.0f;

	ApplyActiveProfile();
	return true;
}

void UGraphicsSettingsSubsystem::SetStylizedFinish(bool bComicInk, float ImpastoAmount)
{
	bComicInkEnabled = bComicInk;
	ImpastoStrength = FMath::Clamp(ImpastoAmount, 0.0f, 2.0f);

	if (UWorld* World = GetWorld())
	{
		const float InkValue = bComicInkEnabled ? 0.85f : 0.0f;
		for (TActorIterator<ADayNightAtmosphereActor> It(World); It; ++It)
		{
			It->SetInkAndImpasto(InkValue, ImpastoStrength);
		}
	}
}

void UGraphicsSettingsSubsystem::SetStylization(bool bComicInk, bool bImpasto)
{
	SetStylizedFinish(bComicInk, bImpasto ? 1.0f : 0.0f);
}

void UGraphicsSettingsSubsystem::ApplyActiveProfile()
{
	const EKomorebiQualityTier EffectiveTier = GetEffectiveTier();
	const FQualityProfile& Profile = ProfileForTier(EffectiveTier);

	if (GEngine)
	{
		if (UGameUserSettings* UserSettings = GEngine->GetGameUserSettings())
		{
			const int32 GroupLvl = Profile.ScalabilityGroupLevel;
			UserSettings->SetShadowQuality(GroupLvl);
			UserSettings->SetPostProcessingQuality(GroupLvl);
			UserSettings->SetEffectsQuality(GroupLvl);
			UserSettings->SetFoliageQuality(GroupLvl);
			UserSettings->SetViewDistanceQuality(GroupLvl);
			UserSettings->SetAntiAliasingQuality(FMath::Min(GroupLvl, 2));
			UserSettings->SetResolutionScaleNormalized(Profile.ResolutionScaleNormalized);
			UserSettings->ApplyNonResolutionSettings();
		}
	}

	ApplyToCurrentWorld();
	OnQualityChanged.Broadcast(EffectiveTier, GetQualityStatusLabel());
}

void UGraphicsSettingsSubsystem::ApplyToCurrentWorld()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const EKomorebiQualityTier EffectiveTier = GetEffectiveTier();
	const FQualityProfile& Profile = ProfileForTier(EffectiveTier);

	for (TActorIterator<ANeoKyotoCityActor> It(World); It; ++It)
	{
		It->SetDecorationQuality(Profile.DecorationDensity);
	}

	for (TActorIterator<AStreetPropsActor> It(World); It; ++It)
	{
		It->SetCullDistanceMeters(Profile.StreetPropDistanceMeters);
	}

	for (TActorIterator<ASnowboundWorldActor> It(World); It; ++It)
	{
		It->SetSnowDriftCount(Profile.SnowDriftCount);
		It->SetAuroraQuality(EffectiveTier);
	}

	const float InkValue = bComicInkEnabled ? 0.85f : 0.0f;
	for (TActorIterator<ADayNightAtmosphereActor> It(World); It; ++It)
	{
		It->SetInkAndImpasto(InkValue, ImpastoStrength);
	}
}
