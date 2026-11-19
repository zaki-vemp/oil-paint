// Copyright Komorebi Project. Curated 6-shot cinematic investor showcase camera director.
#include "Campaign/ShowcaseCameraDirector.h"

#include "Camera/CameraComponent.h"
#include "Campaign/MissionSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "KomorebiGameTypes.h"
#include "Vehicles/TrafficSubsystem.h"

#include <cmath>

AShowcaseCameraDirector::AShowcaseCameraDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = false;

	DirectorRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DirectorRoot"));
	SetRootComponent(DirectorRoot);

	ShowcaseCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ShowcaseCamera"));
	ShowcaseCamera->SetupAttachment(DirectorRoot);
	ShowcaseCamera->SetFieldOfView(54.0f);
	ShowcaseCamera->bUsePawnControlRotation = false;
}

void AShowcaseCameraDirector::BeginPlay()
{
	Super::BeginPlay();

	if (!CustomHeightProvider && !FallbackTerrain.IsPrepared())
	{
		FallbackTerrain.Prepare();
	}
}

void AShowcaseCameraDirector::SetTerrainHeightProvider(TFunction<double(double, double)> InHeightProvider)
{
	CustomHeightProvider = MoveTemp(InHeightProvider);
}

double AShowcaseCameraDirector::SampleTerrainHeight(double ThreeX, double ThreeZ) const
{
	if (CustomHeightProvider)
	{
		return CustomHeightProvider(ThreeX, ThreeZ);
	}
	if (FallbackTerrain.IsPrepared())
	{
		return FallbackTerrain.HeightAt(ThreeX, ThreeZ);
	}
	return 0.0;
}

void AShowcaseCameraDirector::StartTour(int32 StartShotIndex)
{
	if (!CustomHeightProvider && !FallbackTerrain.IsPrepared())
	{
		FallbackTerrain.Prepare();
	}

	const bool bWasActive = bTourActive;
	bTourActive = true;

	if (UWorld* World = GetWorld())
	{
		TourStartedWorldTime = World->GetTimeSeconds();
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (!bWasActive)
			{
				PreviousViewTarget = PC->GetViewTarget();
			}
			PC->SetViewTargetWithBlend(this, ViewTargetBlendSeconds, EViewTargetBlendFunction::VTBlend_Cubic);
		}
	}

	if (!bWasActive)
	{
		OnShowcaseStateChanged.Broadcast(true);
	}

	JumpToShot(StartShotIndex);
}

void AShowcaseCameraDirector::StopTour(bool bSilent)
{
	if (!bTourActive)
	{
		return;
	}

	bTourActive = false;

	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			AActor* RestoreTarget = PreviousViewTarget.IsValid() ? PreviousViewTarget.Get() : static_cast<AActor*>(PC->GetPawn());
			if (RestoreTarget && RestoreTarget != this)
			{
				PC->SetViewTargetWithBlend(RestoreTarget, ViewTargetBlendSeconds, EViewTargetBlendFunction::VTBlend_Cubic);
			}
		}
	}
	PreviousViewTarget.Reset();

	// Release Day/Night preview override so the live 10-minute cycle resumes seamlessly.
	OnDayPhasePreviewChanged.Broadcast(false, false);
	OnShowcaseStateChanged.Broadcast(false);

	if (!bSilent)
	{
		OnShowcaseToast.Broadcast(TEXT("Tour exited — live controls active"));
	}
}

bool AShowcaseCameraDirector::ToggleTour()
{
	if (bTourActive)
	{
		StopTour(false);
	}
	else
	{
		StartTour(0);
	}
	return bTourActive;
}

void AShowcaseCameraDirector::NextShot()
{
	if (!bTourActive)
	{
		StartTour(0);
	}
	else
	{
		JumpToShot(CurrentShotIndex + 1);
	}
}

void AShowcaseCameraDirector::PreviousShot()
{
	if (!bTourActive)
	{
		StartTour(0);
	}
	else
	{
		JumpToShot(CurrentShotIndex - 1);
	}
}

void AShowcaseCameraDirector::JumpToShot(int32 ShotIndex)
{
	const int32 Total = static_cast<int32>(KomorebiCore::GetShowcaseShots().size());
	if (Total <= 0)
	{
		return;
	}

	CurrentShotIndex = ((ShotIndex % Total) + Total) % Total;
	ShotElapsed = 0.0f;

	ApplyCurrentShotMetadata();
	SampleShotPose(0.0f);
	RevealWorldAroundCamera();
}

bool AShowcaseCameraDirector::HandleInputKey(FKey PressedKey)
{
	if (!bTourActive)
	{
		return false;
	}

	if (PressedKey == EKeys::SpaceBar)
	{
		NextShot();
		return true;
	}

	if (PressedKey == EKeys::W
		|| PressedKey == EKeys::A
		|| PressedKey == EKeys::S
		|| PressedKey == EKeys::D
		|| PressedKey == EKeys::Up
		|| PressedKey == EKeys::Down
		|| PressedKey == EKeys::Left
		|| PressedKey == EKeys::Right
		|| PressedKey == EKeys::Escape)
	{
		StopTour(false);
		return true;
	}

	return false;
}

void AShowcaseCameraDirector::PollPlayerControllerExitKeys()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}

	if (PC->WasInputKeyJustPressed(EKeys::SpaceBar))
	{
		NextShot();
		return;
	}

	static const FKey kExitKeys[] = {
		EKeys::W,
		EKeys::A,
		EKeys::S,
		EKeys::D,
		EKeys::Up,
		EKeys::Down,
		EKeys::Left,
		EKeys::Right,
		EKeys::Escape,
	};
	for (const FKey& Key : kExitKeys)
	{
		if (PC->WasInputKeyJustPressed(Key))
		{
			StopTour(false);
			return;
		}
	}

	// Allow click-to-exit after the 180ms startup grace period (matching showcase.js).
	if ((World->GetTimeSeconds() - TourStartedWorldTime) > 0.18f && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		StopTour(false);
	}
}

void AShowcaseCameraDirector::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bTourActive)
	{
		return;
	}

	PollPlayerControllerExitKeys();
	if (!bTourActive)
	{
		return;
	}

	const float Duration = FMath::Max(0.5f, ShotDurationSeconds);
	const int32 Total = static_cast<int32>(KomorebiCore::GetShowcaseShots().size());

	ShotElapsed += FMath::Max(0.0f, DeltaTime);
	if (ShotElapsed >= Duration && Total > 0)
	{
		CurrentShotIndex = (CurrentShotIndex + 1) % Total;
		ShotElapsed -= Duration;
		ApplyCurrentShotMetadata();
	}

	const float U = FMath::Clamp(ShotElapsed / Duration, 0.0f, 1.0f);
	SampleShotPose(U);
	RevealWorldAroundCamera();
	OnShowcaseProgressUpdated.Broadcast(CurrentShotIndex, U);
}

void AShowcaseCameraDirector::ApplyCurrentShotMetadata()
{
	const auto& Shots = KomorebiCore::GetShowcaseShots();
	const KomorebiCore::ShowcaseShot& Shot = Shots[FMath::Clamp(CurrentShotIndex, 0, static_cast<int32>(Shots.size()) - 1)];
	const bool bIsNight = (Shot.Phase == KomorebiCore::EDayPhase::Night);

	OnDayPhasePreviewChanged.Broadcast(true, bIsNight);
	OnShowcaseShotChanged.Broadcast(GetCurrentShotMeta());
}

void AShowcaseCameraDirector::SampleShotPose(float Progress01)
{
	const auto& Shots = KomorebiCore::GetShowcaseShots();
	const KomorebiCore::ShowcaseShot& Shot = Shots[FMath::Clamp(CurrentShotIndex, 0, static_cast<int32>(Shots.size()) - 1)];

	KomorebiCore::Vec3 ThreePos;
	KomorebiCore::Vec3 ThreeTarget;
	KomorebiCore::EvaluateShot(
		Shot,
		static_cast<double>(Progress01),
		[this](double X, double Z) { return SampleTerrainHeight(X, Z); },
		ThreePos,
		ThreeTarget);

	CurrentEyeLocationUE = KomorebiBridge::ThreeToFVector(ThreePos);
	CurrentTargetLocationUE = KomorebiBridge::ThreeToFVector(ThreeTarget);

	const FVector LookDir = (CurrentTargetLocationUE - CurrentEyeLocationUE).GetSafeNormal();
	const FRotator LookRot = LookDir.IsNearlyZero() ? FRotator::ZeroRotator : LookDir.Rotation();
	SetActorLocationAndRotation(CurrentEyeLocationUE, LookRot);
}

void AShowcaseCameraDirector::RevealWorldAroundCamera() const
{
	if (const UWorld* World = GetWorld())
	{
		if (UMissionSubsystem* Missions = World->GetSubsystem<UMissionSubsystem>())
		{
			Missions->RevealAroundShowcaseCamera(CurrentTargetLocationUE, CurrentEyeLocationUE);
		}
		if (UTrafficSubsystem* Traffic = World->GetSubsystem<UTrafficSubsystem>())
		{
			Traffic->RevealAroundShowcaseCamera(CurrentTargetLocationUE, CurrentEyeLocationUE);
		}
	}
}

float AShowcaseCameraDirector::GetCurrentShotProgress() const
{
	return FMath::Clamp(ShotElapsed / FMath::Max(0.5f, ShotDurationSeconds), 0.0f, 1.0f);
}

FKomorebiShowcaseShotMeta AShowcaseCameraDirector::GetCurrentShotMeta() const
{
	const auto& Shots = KomorebiCore::GetShowcaseShots();
	const int32 Total = static_cast<int32>(Shots.size());
	const int32 SafeIdx = FMath::Clamp(CurrentShotIndex, 0, FMath::Max(0, Total - 1));
	const KomorebiCore::ShowcaseShot& Shot = Shots[SafeIdx];

	FKomorebiShowcaseShotMeta Meta;
	Meta.ShotIndex = SafeIdx;
	Meta.TotalShots = Total;
	Meta.CounterText = FString::Printf(TEXT("%02d / %02d"), SafeIdx + 1, Total);
	Meta.ShotId = UTF8_TO_TCHAR(Shot.Id);
	Meta.Kicker = UTF8_TO_TCHAR(Shot.Kicker);
	Meta.Title = UTF8_TO_TCHAR(Shot.Title);
	Meta.Subtitle = UTF8_TO_TCHAR(Shot.Subtitle);
	Meta.bIsNight = (Shot.Phase == KomorebiCore::EDayPhase::Night);
	Meta.PhaseLabel = Meta.bIsNight ? TEXT("Neon Night") : TEXT("Daylight Preview");
	Meta.AccentColor = FLinearColor(FColor::FromHex(UTF8_TO_TCHAR(Shot.AccentHex)));
	Meta.Progress01 = GetCurrentShotProgress();
	Meta.bActive = bTourActive;
	return Meta;
}
