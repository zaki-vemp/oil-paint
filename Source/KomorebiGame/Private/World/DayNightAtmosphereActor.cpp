// Copyright Komorebi Project. Ten-minute day/night atmosphere and MPC_KomorebiShared driver implementation.
#include "World/DayNightAtmosphereActor.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/World.h"
#include "Game/ChaptersData.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "World/SnowboundTerrain.h"
#include "World/WorldGrid.h"

namespace
{
	// Neo-Kyoto golden-hour sun vector from legacy-web/src/oilpaint/common.js: normalize(-0.44, 0.5, -0.75)
	FVector GetNeoKyotoSunDirUE()
	{
		const KomorebiCore::Vec3 Raw{ -0.44, 0.50, -0.75 };
		const double Len = std::sqrt(Raw.X * Raw.X + Raw.Y * Raw.Y + Raw.Z * Raw.Z);
		const KomorebiCore::Vec3 Norm{ Raw.X / Len, Raw.Y / Len, Raw.Z / Len };
		return KomorebiBridge::ThreeDirToFVector(Norm).GetSafeNormal();
	}

	// Snowbound moonlight vector from KomorebiCore::MoonDir(): normalize(-0.78, 0.44, 0.3)
	FVector GetSnowboundMoonDirUE()
	{
		return KomorebiBridge::ThreeDirToFVector(KomorebiCore::MoonDir()).GetSafeNormal();
	}
}

ADayNightAtmosphereActor::ADayNightAtmosphereActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("AtmosphereRoot"));
	SetRootComponent(Root);

	SunMoonLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("SunMoonLight"));
	SunMoonLight->SetupAttachment(Root);
	SunMoonLight->SetMobility(EComponentMobility::Movable);
	SunMoonLight->SetAtmosphereSunLight(true);
	SunMoonLight->SetIntensity(7.5f);
	SunMoonLight->SetLightColor(FLinearColor(1.0f, 0.94f, 0.84f));
	SunMoonLight->CastShadows = true;
	SunMoonLight->DynamicShadowDistanceMovableLight = 24000.0f;
	SunMoonLight->DynamicShadowCascades = 4;

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(Root);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->SourceType = ESkyLightSourceType::SLS_CapturedScene;
	SkyLight->bRealTimeCapture = true;
	SkyLight->SetIntensity(1.15f);

	SkyAtmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("SkyAtmosphere"));
	SkyAtmosphere->SetupAttachment(Root);

	HeightFog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("HeightFog"));
	HeightFog->SetupAttachment(Root);
	HeightFog->SetFogDensity(0.008f);
	HeightFog->SetFogHeightFalloff(0.18f);
	HeightFog->SetFogInscatteringColor(FLinearColor(0.70f, 0.81f, 0.91f));
}

void ADayNightAtmosphereActor::BeginPlay()
{
	Super::BeginPlay();

	if (bIsSnowbound)
	{
		WorldScene = EKomorebiWorldScene::Snowbound;
	}
	else if (WorldScene == EKomorebiWorldScene::Snowbound)
	{
		bIsSnowbound = true;
	}

	if (!SharedMPC)
	{
		SharedMPC = LoadObject<UMaterialParameterCollection>(
			nullptr,
			TEXT("/Game/Materials/MPC_KomorebiShared.MPC_KomorebiShared"));
	}

	// Seed default chapter palette from Chapter 0 (Sunleaf Row) for Neo-Kyoto.
	const auto& Chapters = KomorebiCore::GetChapters();
	if (!Chapters.empty() && WorldScene == EKomorebiWorldScene::NeoKyoto)
	{
		const KomorebiCore::ChapterSpec& First = Chapters[0];
		ChapterGrass = KomorebiBridge::ToLinearColor(First.Day.Grass);
		ChapterPath = KomorebiBridge::ToLinearColor(First.Day.Path);
		ChapterFoliage = KomorebiBridge::ToLinearColor(First.Foliage);
		ChapterLeafLight = KomorebiBridge::ToLinearColor(First.LeafLight);
		SkyHighColor = KomorebiBridge::ToLinearColor(First.Day.Sky);
		HazeColor = KomorebiBridge::ToLinearColor(First.Day.Haze);
	}
	else if (WorldScene == EKomorebiWorldScene::Snowbound)
	{
		LampColor = FLinearColor(1.0f, 0.60f, 0.24f, 1.0f);
		HazeColor = FLinearColor(0.018f, 0.04f, 0.07f, 1.0f);
		SkyHighColor = FLinearColor(0.005f, 0.014f, 0.036f, 1.0f);
	}

	EnsureMPCInstance();
	InitializeController();
	ApplyAtmosphereAndMPC(Controller.Update(ElapsedWorldTime));
}

#if WITH_EDITOR
void ADayNightAtmosphereActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropName = PropertyChangedEvent.GetPropertyName();
	if (PropName == GET_MEMBER_NAME_CHECKED(ADayNightAtmosphereActor, bIsSnowbound))
	{
		WorldScene = bIsSnowbound ? EKomorebiWorldScene::Snowbound : EKomorebiWorldScene::NeoKyoto;
	}
	else if (PropName == GET_MEMBER_NAME_CHECKED(ADayNightAtmosphereActor, WorldScene))
	{
		bIsSnowbound = (WorldScene == EKomorebiWorldScene::Snowbound);
	}
}
#endif

void ADayNightAtmosphereActor::InitializeController()
{
	const KomorebiCore::EDayPhase StartPhase = (WorldScene == EKomorebiWorldScene::Snowbound)
		? KomorebiCore::EDayPhase::Night
		: KomorebiCore::EDayPhase::Day;

	Controller = KomorebiCore::DayNightController(
		StartPhase,
		bOnlineClock,
		[this](KomorebiCore::EDayPhase NewPhase, const KomorebiCore::DayNightState& NewState)
		{
			HandlePhaseShift(NewPhase, NewState);
		});
}

void ADayNightAtmosphereActor::HandlePhaseShift(
	KomorebiCore::EDayPhase NewPhase,
	const KomorebiCore::DayNightState& /*NewState*/)
{
	const bool bNowNight = (NewPhase == KomorebiCore::EDayPhase::Night);
	FString Title;
	if (WorldScene == EKomorebiWorldScene::Snowbound)
	{
		Title = bNowNight ? TEXT("Polar Night") : TEXT("Arctic Dawn");
	}
	else
	{
		Title = bNowNight ? TEXT("Nightfall") : TEXT("Daybreak");
	}
	OnPhaseShifted.Broadcast(bNowNight, Title);
}

void ADayNightAtmosphereActor::ShiftPhase()
{
	const KomorebiCore::EDayPhase Target = (Controller.GetPhase() == KomorebiCore::EDayPhase::Night)
		? KomorebiCore::EDayPhase::Day
		: KomorebiCore::EDayPhase::Night;
	const KomorebiCore::DayNightState& State = Controller.SetPhase(Target, ElapsedWorldTime);
	ApplyAtmosphereAndMPC(State);
}

void ADayNightAtmosphereActor::SetPreviewPhase(TOptional<KomorebiCore::EDayPhase> PreviewPhase)
{
	std::optional<KomorebiCore::EDayPhase> CoreOpt;
	if (PreviewPhase.IsSet())
	{
		CoreOpt = PreviewPhase.GetValue();
	}
	const KomorebiCore::DayNightState& State = Controller.PreviewPhase(CoreOpt, ElapsedWorldTime);
	ApplyAtmosphereAndMPC(State);
}

void ADayNightAtmosphereActor::SetInkAndImpasto(float Ink, float Impasto)
{
	InkStrength = FMath::Clamp(Ink, 0.0f, 1.0f);
	ImpastoStrength = FMath::Clamp(Impasto, 0.0f, 2.0f);
	ApplyAtmosphereAndMPC(Controller.GetState());
}

void ADayNightAtmosphereActor::SetOnlineClock(bool bOnline)
{
	if (bOnlineClock == bOnline)
	{
		return;
	}
	bOnlineClock = bOnline;
	InitializeController();
	ApplyAtmosphereAndMPC(Controller.Update(ElapsedWorldTime));
}

void ADayNightAtmosphereActor::SetDistrictPaletteColors(
	const FLinearColor& InGrass,
	const FLinearColor& InPath,
	const FLinearColor& InFoliage,
	const FLinearColor& InLeafLight,
	const FLinearColor& InSkyHigh,
	const FLinearColor& InHaze)
{
	ChapterGrass = InGrass;
	ChapterPath = InPath;
	ChapterFoliage = InFoliage;
	ChapterLeafLight = InLeafLight;
	SkyHighColor = InSkyHigh;
	HazeColor = InHaze;
}

void ADayNightAtmosphereActor::SetFocusWorldLocation(const FVector& InFocusUE)
{
	FocusWorldLocation = InFocusUE;
	bHasExternalFocus = true;
}

void ADayNightAtmosphereActor::SetLampState(const FVector& LampPosUE, float Strength, const FLinearColor& Color)
{
	LampPosStrength = FLinearColor(LampPosUE.X, LampPosUE.Y, LampPosUE.Z, FMath::Max(0.0f, Strength));
	LampColor = Color;
}

FString ADayNightAtmosphereActor::GetClockLabel() const
{
	const KomorebiCore::DayNightState& State = Controller.GetState();
	const TCHAR* Prefix = (State.Phase == KomorebiCore::EDayPhase::Night) ? TEXT("Night") : TEXT("Day");
	const FString MMSS = KomorebiBridge::ToFString(State.ClockMMSS);
	return FString::Printf(TEXT("%s · %s"), Prefix, *MMSS);
}

void ADayNightAtmosphereActor::EnsureMPCInstance()
{
	if (!CachedMPCInstance && SharedMPC && GetWorld())
	{
		CachedMPCInstance = GetWorld()->GetParameterCollectionInstance(SharedMPC);
	}
}

void ADayNightAtmosphereActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const double SafeDt = FMath::Clamp(static_cast<double>(DeltaTime), 0.0, 0.1);
	ElapsedWorldTime += SafeDt;

	if (!bHasExternalFocus && GetWorld())
	{
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (const APawn* Pawn = PC->GetPawn())
			{
				FocusWorldLocation = Pawn->GetActorLocation();
			}
		}
	}

	const KomorebiCore::DayNightState& State = Controller.Update(ElapsedWorldTime);
	ApplyAtmosphereAndMPC(State);
}

void ADayNightAtmosphereActor::ApplyAtmosphereAndMPC(const KomorebiCore::DayNightState& State)
{
	const float Night = static_cast<float>(State.Night);
	const float Twilight = static_cast<float>(State.Twilight);
	const float TimeSec = static_cast<float>(ElapsedWorldTime);
	const bool bSnow = (WorldScene == EKomorebiWorldScene::Snowbound);

	// Aurora breathing curve from legacy-web/src/night/main.js line 433.
	if (bSnow)
	{
		const double Breath = 0.62
			+ 0.17 * std::sin(ElapsedWorldTime * 0.11)
			+ 0.09 * std::sin(ElapsedWorldTime * 0.37 + 1.0)
			+ 0.05 * std::sin(ElapsedWorldTime * 1.1 + 2.0);
		AuroraIntensity = static_cast<float>(FMath::Max(0.0, Breath) * State.Night);
	}
	else
	{
		AuroraIntensity = 0.0f;
	}

	const FVector SunDirUE = bSnow ? GetSnowboundMoonDirUE() : GetNeoKyotoSunDirUE();

	// Orient directional light FROM sun/moon TOWARD the ground (-SunDirUE).
	if (SunMoonLight)
	{
		SunMoonLight->SetWorldRotation((-SunDirUE).Rotation());

		const FLinearColor DayLightCol = bSnow
			? FLinearColor(0.95f, 0.96f, 1.0f)
			: FLinearColor(1.0f, 0.93f, 0.80f);
		const FLinearColor TwilightCol = FLinearColor(1.0f, 0.56f, 0.28f);
		const FLinearColor NightLightCol = bSnow
			? FLinearColor(0.46f, 0.62f, 0.86f)
			: FLinearColor(0.38f, 0.50f, 0.78f);

		FLinearColor LightCol = FMath::Lerp(DayLightCol, NightLightCol, Night);
		LightCol = FMath::Lerp(LightCol, TwilightCol, Twilight * 0.72f);
		SunMoonLight->SetLightColor(LightCol);

		const float DayIntensity = bSnow ? 6.2f : 8.5f;
		const float NightIntensity = bSnow ? 1.45f : 0.95f;
		const float Intensity = FMath::Lerp(DayIntensity, NightIntensity, Night) * (1.0f - 0.22f * Twilight);
		SunMoonLight->SetIntensity(Intensity);
	}

	if (SkyLight)
	{
		const float DaySkyInt = bSnow ? 1.10f : 1.25f;
		const float NightSkyInt = bSnow ? 0.52f : 0.38f;
		SkyLight->SetIntensity(FMath::Lerp(DaySkyInt, NightSkyInt, Night));

		const FLinearColor DaySkyTint(0.86f, 0.92f, 1.0f);
		const FLinearColor NightSkyTint = bSnow
			? FLinearColor(0.28f, 0.58f, 0.66f)
			: FLinearColor(0.28f, 0.34f, 0.52f);
		SkyLight->SetLightColor(FMath::Lerp(DaySkyTint, NightSkyTint, Night));
	}

	if (HeightFog)
	{
		FLinearColor FogCol;
		if (bSnow)
		{
			// Exact Snowbound arctic haze interpolation from legacy-web/src/night/main.js lines 404, 423.
			const FLinearColor DayHaze(0.62f, 0.78f, 0.90f, 1.0f);
			const FLinearColor NightHaze(0.018f, 0.04f, 0.07f, 1.0f);
			FogCol = FMath::Lerp(DayHaze, NightHaze, Night);
			HazeColor = FogCol;
			HeightFog->SetFogDensity(0.0021f);
		}
		else
		{
			FogCol = HazeColor;
			if (Twilight > 0.001f)
			{
				FogCol.R = FMath::Min(1.0f, FogCol.R + 0.20f * Twilight);
				FogCol.G = FMath::Min(1.0f, FogCol.G + 0.10f * Twilight);
				FogCol.B = FMath::Min(1.0f, FogCol.B + 0.04f * Twilight);
			}
			// Matches legacy-web/src/world/anime.js line 1045: 0.0018 + 0.0024 * n
			HeightFog->SetFogDensity(0.0018f + 0.0024f * Night);
		}
		HeightFog->SetFogInscatteringColor(FogCol);
	}

	EnsureMPCInstance();
	if (CachedMPCInstance)
	{
		CachedMPCInstance->SetScalarParameterValue(TEXT("Time"), TimeSec);
		CachedMPCInstance->SetScalarParameterValue(TEXT("Night"), Night);
		CachedMPCInstance->SetScalarParameterValue(TEXT("Twilight"), Twilight);
		CachedMPCInstance->SetScalarParameterValue(TEXT("Aurora"), AuroraIntensity);
		CachedMPCInstance->SetScalarParameterValue(TEXT("Exposure"), bSnow ? 1.15f : 1.08f);
		CachedMPCInstance->SetScalarParameterValue(TEXT("InkStrength"), InkStrength);
		CachedMPCInstance->SetScalarParameterValue(TEXT("ImpastoStrength"), ImpastoStrength);

		CachedMPCInstance->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(SunDirUE.X, SunDirUE.Y, SunDirUE.Z, 0.0f));
		CachedMPCInstance->SetVectorParameterValue(TEXT("ChapterGrass"), ChapterGrass);
		CachedMPCInstance->SetVectorParameterValue(TEXT("ChapterPath"), ChapterPath);
		CachedMPCInstance->SetVectorParameterValue(TEXT("ChapterFoliage"), ChapterFoliage);
		CachedMPCInstance->SetVectorParameterValue(TEXT("ChapterLeafLight"), ChapterLeafLight);
		CachedMPCInstance->SetVectorParameterValue(TEXT("SkyHigh"), SkyHighColor);
		CachedMPCInstance->SetVectorParameterValue(TEXT("HazeColor"), HazeColor);
		CachedMPCInstance->SetVectorParameterValue(
			TEXT("FocusPos"),
			FLinearColor(FocusWorldLocation.X, FocusWorldLocation.Y, FocusWorldLocation.Z, 0.0f));

		FLinearColor EffectiveLamp = LampPosStrength;
		if (bSnow)
		{
			// Matches legacy-web/src/night/main.js line 429: lantern dims subtly in arctic daylight.
			EffectiveLamp.A *= (0.15f + 0.85f * Night);
		}
		CachedMPCInstance->SetVectorParameterValue(TEXT("LampPosStrength"), EffectiveLamp);
		CachedMPCInstance->SetVectorParameterValue(TEXT("LampColor"), LampColor);
		CachedMPCInstance->SetVectorParameterValue(
			TEXT("ZoneSplit"),
			FLinearColor(
				static_cast<float>(KomorebiCore::ZoneSplit.Downtown),
				static_cast<float>(KomorebiCore::ZoneSplit.Market),
				static_cast<float>(KomorebiCore::ZoneSplit.East),
				0.0f));
	}
}
