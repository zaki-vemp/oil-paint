// Copyright Komorebi Project. AHUD host mounting pure Slate HUD, Pause Menu + 3-Tab Lore Codex, and Showcase Overlay.
#include "UI/KomorebiHUD.h"

#include "UI/SKomorebiHUD.h"
#include "UI/SKomorebiPauseMenu.h"
#include "UI/SKomorebiShowcaseOverlay.h"
#include "Network/KomorebiGameInstance.h"
#include "Network/KomorebiGameState.h"
#include "Network/KomorebiPlayerState.h"
#include "Network/KomorebiPlayerController.h"
#include "Network/OnlineActivitiesActor.h"
#include "Network/OnlineSessionSubsystem.h"
#include "World/CoordBridge.h"
#include "World/WorldGrid.h"
#include "Game/ChaptersData.h"
#include "Game/LevelsData.h"
#include "Game/AudioTheory.h"
#include "Game/ShowcaseData.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/SWeakWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"

namespace
{
	struct FSpeakerMetaEntry
	{
		const TCHAR* Id;
		const TCHAR* Callsign;
		const TCHAR* Role;
		const TCHAR* Freq;
		const TCHAR* HexColor;
	};

	const FSpeakerMetaEntry& ResolveSpeakerMeta(const FString& SpeakerId)
	{
		static const FSpeakerMetaEntry Table[] = {
			{ TEXT("sora"), TEXT("Sora"), TEXT("Ghost-Line Handler"), TEXT("CH-07 // RESISTANCE LINK"), TEXT("#00f0ff") },
			{ TEXT("mira"), TEXT("Dr. Mira"), TEXT("Archive Imprint"), TEXT("SEED // NEURAL ARCHIVE"), TEXT("#19ffc6") },
			{ TEXT("ren"), TEXT("Ren"), TEXT("Chief Architect"), TEXT("SP-01 // SPECTER CHANNEL"), TEXT("#ff2a6d") },
			{ TEXT("kael"), TEXT("Kael // 07"), TEXT("Free Courier"), TEXT("SUIT // COLLAR LINK"), TEXT("#fcee0a") },
			{ TEXT("dispatch"), TEXT("Grid Dispatch"), TEXT("Security Overwatch"), TEXT("AE-99 // CIVIC ALERT"), TEXT("#ffb000") },
			{ TEXT("hana"), TEXT("Hana"), TEXT("Amber Echo"), TEXT("MEM-00 // CONSERVATORY"), TEXT("#ffb000") },
		};

		for (const FSpeakerMetaEntry& Entry : Table)
		{
			if (SpeakerId.Equals(Entry.Id, ESearchCase::IgnoreCase))
			{
				return Entry;
			}
		}
		return Table[0];
	}
}

AKomorebiHUD::AKomorebiHUD()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

void AKomorebiHUD::BeginPlay()
{
	Super::BeginPlay();

	if (IsRunningDedicatedServer() || !PlayerOwner || !PlayerOwner->IsLocalController())
	{
		return;
	}

	MountViewportWidgets();

	if (const UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
	{
		ViewData.bIsNeoKyoto = (GI->SelectedScene == EKomorebiWorldScene::NeoKyoto);
		ViewData.Core.bRoamMode = (GI->SelectedPlayMode == EKomorebiPlayMode::FreeRoam);
		ViewData.Core.bOnlineMode = (GI->SelectedPlayMode == EKomorebiPlayMode::OnlineCity);
		ViewData.Core.EchoesCollected = GI->GetUnlockedEchoIds().Num();

		if (ViewData.bIsNeoKyoto)
		{
			ShowChapterBanner(0, false);
		}
		else
		{
			ViewData.ChapterAccent = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#5ef2e0")));
			ViewData.SectorLabel = TEXT("Side op · Snowbound");
			ViewData.ChapterName = TEXT("Under the aurora");
			ViewData.MapZoneLabel = TEXT("Scan · snowbound");
		}
	}

	RefreshPauseMenuBindings();
}

void AKomorebiHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnmountViewportWidgets();
	Super::EndPlay(EndPlayReason);
}

void AKomorebiHUD::MountViewportWidgets()
{
	if (!GEngine || !GEngine->GameViewport)
	{
		return;
	}

	// 1. Main Cyberpunk HUD (Z-order 10)
	HUDWidget = SNew(SKomorebiHUD)
		.OnDismissComms(FKomorebiOnHUDDismissComms::CreateUObject(this, &AKomorebiHUD::DismissComms));
	HUDViewportContainer = SNew(SWeakWidget).PossiblyNullContent(HUDWidget.ToSharedRef());
	GEngine->GameViewport->AddViewportWidgetContent(HUDViewportContainer.ToSharedRef(), 10);

	// 2. Investor Showcase Overlay (Z-order 20, hidden by default)
	ShowcaseOverlayWidget = SNew(SKomorebiShowcaseOverlay)
		.OnNextShot(FKomorebiOnShowcaseAction::CreateLambda([this]()
		{
			if (AKomorebiPlayerController* KPC = Cast<AKomorebiPlayerController>(PlayerOwner))
			{
				KPC->AdvanceShowcaseShot();
			}
			else
			{
				AdvanceShowcaseShotLocal();
			}
		}))
		.OnExitTour(FKomorebiOnShowcaseAction::CreateLambda([this]()
		{
			if (AKomorebiPlayerController* KPC = Cast<AKomorebiPlayerController>(PlayerOwner))
			{
				KPC->ExitShowcaseTour();
			}
			else
			{
				SetShowcaseActive(false);
			}
		}))
		.OnSelectShot(FKomorebiOnShowcaseSelectShot::CreateUObject(this, &AKomorebiHUD::SelectShowcaseShotLocal));
	ShowcaseOverlayWidget->SetVisibility(EVisibility::Collapsed);
	ShowcaseViewportContainer = SNew(SWeakWidget).PossiblyNullContent(ShowcaseOverlayWidget.ToSharedRef());
	GEngine->GameViewport->AddViewportWidgetContent(ShowcaseViewportContainer.ToSharedRef(), 20);

	// 3. Modal Pause Menu + 3-Tab Lore Codex (Z-order 30, hidden by default)
	PauseMenuWidget = SNew(SKomorebiPauseMenu)
		.OnResume(FKomorebiMenuVoidDelegate::CreateLambda([this]()
		{
			if (AKomorebiPlayerController* KPC = Cast<AKomorebiPlayerController>(PlayerOwner))
			{
				KPC->SetPauseMenuOpen(false);
			}
			else
			{
				SetPauseMenuOpen(false);
			}
		}))
		.OnToggleFullscreen(FKomorebiMenuVoidDelegate::CreateLambda([this]()
		{
			if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
			{
				const bool bNowFull = Settings->GetFullscreenMode() != EWindowMode::Windowed;
				Settings->SetFullscreenMode(bNowFull ? EWindowMode::Windowed : EWindowMode::WindowedFullscreen);
				Settings->ApplySettings(false);
			}
			RefreshPauseMenuBindings();
		}))
		.OnToggleShowcase(FKomorebiMenuVoidDelegate::CreateLambda([this]()
		{
			if (AKomorebiPlayerController* KPC = Cast<AKomorebiPlayerController>(PlayerOwner))
			{
				KPC->ToggleShowcaseTour();
			}
		}))
		.OnToggleDayNight(FKomorebiMenuVoidDelegate::CreateLambda([this]()
		{
			if (AKomorebiPlayerController* KPC = Cast<AKomorebiPlayerController>(PlayerOwner))
			{
				KPC->ToggleDayNightPhase();
			}
			ViewData.Core.bIsNight = !ViewData.Core.bIsNight;
			RefreshPauseMenuBindings();
		}))
		.OnSelectMode(FKomorebiMenuModeDelegate::CreateLambda([this](EKomorebiPlayMode Mode)
		{
			if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
			{
				if (GI->SelectedPlayMode == Mode)
				{
					if (AKomorebiPlayerController* KPC = Cast<AKomorebiPlayerController>(PlayerOwner))
					{
						KPC->SetPauseMenuOpen(false);
					}
					return;
				}
				GI->TravelToSceneAndMode(GI->SelectedScene, Mode);
			}
		}))
		.OnSelectScene(FKomorebiMenuSceneDelegate::CreateLambda([this](EKomorebiWorldScene Scene)
		{
			if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
			{
				if (GI->SelectedScene == Scene)
				{
					if (AKomorebiPlayerController* KPC = Cast<AKomorebiPlayerController>(PlayerOwner))
					{
						KPC->SetPauseMenuOpen(false);
					}
					return;
				}
				GI->TravelToSceneAndMode(Scene, GI->SelectedPlayMode);
			}
		}))
		.OnProfileChanged(FKomorebiMenuProfileDelegate::CreateLambda([this](const FString& Nick, const FString& Title, int32 ColorIdx)
		{
			if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
			{
				GI->UpdateLocalOnlineProfile(Nick, Title, ColorIdx);
			}
			RefreshPauseMenuBindings();
		}))
		.OnHostListenServer(FKomorebiMenuHostDelegate::CreateLambda([this](bool bPrivate, const FString& RoomCode)
		{
			if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
			{
				if (UOnlineSessionSubsystem* SessionSub = GI->GetOnlineSessionSubsystem())
				{
					SessionSub->HostOnlineCity(bPrivate, RoomCode, GI->SelectedScene);
				}
				else
				{
					GI->TravelToSceneAndMode(GI->SelectedScene, EKomorebiPlayMode::OnlineCity, RoomCode, TEXT(""), bPrivate);
				}
			}
			RefreshPauseMenuBindings();
		}))
		.OnQuickMatch(FKomorebiMenuVoidDelegate::CreateLambda([this]()
		{
			if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
			{
				if (UOnlineSessionSubsystem* SessionSub = GI->GetOnlineSessionSubsystem())
				{
					SessionSub->QuickMatch(GI->SelectedScene);
				}
				else
				{
					GI->TravelToSceneAndMode(GI->SelectedScene, EKomorebiPlayMode::OnlineCity);
				}
			}
			RefreshPauseMenuBindings();
		}))
		.OnJoinByAddress(FKomorebiMenuJoinIPDelegate::CreateLambda([this](const FString& Address)
		{
			if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
			{
				if (UOnlineSessionSubsystem* SessionSub = GI->GetOnlineSessionSubsystem())
				{
					SessionSub->JoinByAddress(Address);
				}
				else
				{
					GI->TravelToSceneAndMode(GI->SelectedScene, EKomorebiPlayMode::OnlineCity, TEXT(""), Address);
				}
			}
			RefreshPauseMenuBindings();
		}))
		.OnCopyRoomCode(FKomorebiMenuVoidDelegate::CreateLambda([this]()
		{
			FString CodeToCopy;
			if (const UWorld* World = GetWorld())
			{
				if (const AKomorebiGameState* GS = World->GetGameState<AKomorebiGameState>())
				{
					CodeToCopy = GS->ActiveRoomCode;
				}
			}
			if (CodeToCopy.IsEmpty())
			{
				if (const UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
				{
					if (const UOnlineSessionSubsystem* SessionSub = GI->GetOnlineSessionSubsystem())
					{
						CodeToCopy = SessionSub->GetCurrentRoomCode();
					}
					if (CodeToCopy.IsEmpty())
					{
						CodeToCopy = GI->PendingRoomCode;
					}
				}
			}
			if (CodeToCopy.IsEmpty())
			{
				EnqueueToast(TEXT("Host or join a room first to copy a room code"));
				return;
			}
			FPlatformApplicationMisc::ClipboardCopy(*CodeToCopy);
			EnqueueToast(FString::Printf(TEXT("Room code copied · %s"), *CodeToCopy));
		}))
		.OnAudioSettingsChanged(FKomorebiMenuAudioDelegate::CreateLambda([this](const KomorebiCore::AudioSettings& Mix)
		{
			if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
			{
				GI->ApplyAudioSettings(Mix);
			}
		}))
		.OnQualityTierChanged(FKomorebiMenuQualityDelegate::CreateLambda([this](EKomorebiQualityTier Tier)
		{
			if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
			{
				GI->ApplyQualityTier(Tier);
			}
		}))
		.OnRenderStyleChanged(FKomorebiMenuRenderStyleDelegate::CreateLambda([this](bool bComicInk, bool bImpasto)
		{
			if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
			{
				GI->ApplyRenderStyleToggles(bComicInk, bImpasto);
			}
		}))
		.OnPlayEchoVoice(FKomorebiMenuEchoVoiceDelegate::CreateLambda([this](const FString& SpeakerId, const FString& Quote)
		{
			if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
			{
				GI->RequestVoiceReplay(SpeakerId, Quote);
			}
			ShowCommsBySpeakerId(SpeakerId, Quote);
		}));

	PauseMenuWidget->SetVisibility(EVisibility::Collapsed);
	PauseViewportContainer = SNew(SWeakWidget).PossiblyNullContent(PauseMenuWidget.ToSharedRef());
	GEngine->GameViewport->AddViewportWidgetContent(PauseViewportContainer.ToSharedRef(), 30);

	// Subscribe to live online session status changes so the Pause Menu updates in real time.
	if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
	{
		if (UOnlineSessionSubsystem* SessionSub = GI->GetOnlineSessionSubsystem())
		{
			SessionSub->OnSessionStatusChanged.AddWeakLambda(this, [this](const FString&, const FString&)
			{
				if (bPauseMenuOpen)
				{
					RefreshPauseMenuBindings();
				}
			});
		}
	}
}

void AKomorebiHUD::UnmountViewportWidgets()
{
	if (GEngine && GEngine->GameViewport)
	{
		if (HUDViewportContainer.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(HUDViewportContainer.ToSharedRef());
		}
		if (ShowcaseViewportContainer.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(ShowcaseViewportContainer.ToSharedRef());
		}
		if (PauseViewportContainer.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(PauseViewportContainer.ToSharedRef());
		}
	}
	HUDViewportContainer.Reset();
	ShowcaseViewportContainer.Reset();
	PauseViewportContainer.Reset();
	HUDWidget.Reset();
	ShowcaseOverlayWidget.Reset();
	PauseMenuWidget.Reset();
}

void AKomorebiHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsRunningDedicatedServer() || !HUDWidget.IsValid())
	{
		return;
	}

	const float ClampedDt = FMath::Min(DeltaSeconds, 0.1f);
	RefreshAutoTelemetry(ClampedDt);
	UpdateProjectedMarkers();
	UpdateTimersAndQueues(ClampedDt);

	if (bShowcaseActive && ShowcaseOverlayWidget.IsValid())
	{
		ShowcaseElapsed += ClampedDt;
		const float ShotDur = static_cast<float>(KomorebiCore::kShotDuration);
		if (ShowcaseElapsed >= ShotDur)
		{
			ShowcaseElapsed -= ShotDur;
			ShowcaseShotIndex = (ShowcaseShotIndex + 1) % static_cast<int32>(KomorebiCore::GetShowcaseShots().size());
		}
		ShowcaseOverlayWidget->UpdateShowcase(ShowcaseShotIndex, FMath::Clamp(ShowcaseElapsed / ShotDur, 0.0f, 1.0f));
	}

	HUDWidget->SetVisibility(bShowcaseActive ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible);
	HUDWidget->RefreshFromViewData(ViewData);
}

void AKomorebiHUD::RefreshAutoTelemetry(float DeltaSeconds)
{
	ViewData.CriticalPulseTime += DeltaSeconds;

	UWorld* World = GetWorld();
	AKomorebiGameState* GS = World ? World->GetGameState<AKomorebiGameState>() : nullptr;
	AKomorebiPlayerState* LocalPS = PlayerOwner ? PlayerOwner->GetPlayerState<AKomorebiPlayerState>() : nullptr;
	APawn* LocalPawn = PlayerOwner ? PlayerOwner->GetPawn() : nullptr;

	if (GS)
	{
		ViewData.bIsNeoKyoto = (GS->ActiveWorldScene == EKomorebiWorldScene::NeoKyoto);
		ViewData.Core.bOnlineMode = GS->IsOnlineCityMode();
		ViewData.Core.bRoamMode = (GS->ActivePlayMode == EKomorebiPlayMode::FreeRoam);
		ViewData.OnlinePlayerCount = FMath::Max(1, GS->GetConnectedPlayerCount());
		ViewData.OnlineStatusPill = FString::Printf(TEXT("Online · %d"), ViewData.OnlinePlayerCount);

		if (ViewData.Core.bOnlineMode && ViewData.bIsNeoKyoto)
		{
			ViewData.Core.bIsNight = GS->bSyncedOnlineIsNight;
			ViewData.Core.DayClockLabel = FString::Printf(
				TEXT("%s · %s"),
				GS->bSyncedOnlineIsNight ? TEXT("☾ Night") : TEXT("☀ Day"),
				*GS->SyncedOnlineClockMMSS);
		}
	}

	if (LocalPS && ViewData.Core.bOnlineMode)
	{
		ViewData.Core.Cash = LocalPS->OnlineCash;
		ViewData.Core.bDriving = LocalPS->bIsDrivingVehicle;
	}

	if (LocalPawn)
	{
		const FVector Loc = LocalPawn->GetActorLocation();
		const KomorebiCore::Vec3 ThreePos = KomorebiBridge::FVectorToThree(Loc);
		ViewData.PlayerThreeXZ = FVector2D(ThreePos.X, ThreePos.Z);
		ViewData.PlayerThreeYawRad = static_cast<float>(KomorebiCore::UEYawDegToThreeYaw(LocalPawn->GetActorRotation().Yaw));

		if (ViewData.bIsNeoKyoto)
		{
			const int32 ZoneIdx = KomorebiCore::ZoneAt(ThreePos.X, ThreePos.Z);
			if (ZoneIdx >= 0 && ZoneIdx < static_cast<int32>(KomorebiCore::Zones.size()))
			{
				ViewData.MapZoneLabel = FString::Printf(TEXT("Scan · %s"), UTF8_TO_TCHAR(KomorebiCore::Zones[static_cast<size_t>(ZoneIdx)].Name));
			}

			const int32 LiveChapter = KomorebiCore::ChapterAt(ThreePos.Z, ViewData.Core.ClearedSectors, ViewData.Core.bRoamMode || ViewData.Core.bOnlineMode);
			const auto& Chapters = KomorebiCore::GetChapters();
			if (LiveChapter >= 0 && LiveChapter < static_cast<int32>(Chapters.size()))
			{
				ViewData.Core.ActiveChapter = LiveChapter;
				const KomorebiCore::ChapterSpec& Ch = Chapters[static_cast<size_t>(LiveChapter)];
				ViewData.ChapterName = KomorebiBridge::ToFString(Ch.Name);
				ViewData.ChapterAccent = FLinearColor::FromSRGBColor(FColor::FromHex(KomorebiBridge::ToFString(Ch.ColorHex)));
				ViewData.SectorLabel = ViewData.Core.bOnlineMode
					? TEXT("Online city · 8-player")
					: (ViewData.Core.bRoamMode
						? TEXT("Free roam · all systems")
						: (ViewData.Core.bWon
							? TEXT("Run complete")
							: FString::Printf(TEXT("Sector %02d / %02d"), LiveChapter + 1, static_cast<int32>(Chapters.size()))));

				if (LastDisplayedChapter != -1 && LastDisplayedChapter != LiveChapter && !ViewData.Core.bBossActive)
				{
					ShowChapterBanner(LiveChapter, ViewData.Core.bIsNight);
				}
				LastDisplayedChapter = LiveChapter;
			}

			// In Online City, pull dynamic objective string from AOnlineActivitiesActor if present.
			if (ViewData.Core.bOnlineMode)
			{
				if (AOnlineActivitiesActor* Activities = Cast<AOnlineActivitiesActor>(
					UGameplayStatics::GetActorOfClass(World, AOnlineActivitiesActor::StaticClass())))
				{
					ViewData.Core.ObjectiveTitle = Activities->BuildOnlineObjectiveTaskText(LocalPS, Loc);
				}
			}
		}
	}

	// Derive power unlocks from ClearedSectors via KomorebiCore::PowersFor()
	const KomorebiCore::PowerSet DerivedPowers = KomorebiCore::PowersFor(ViewData.Core.ClearedSectors);
	ViewData.bHasPower[0] = DerivedPowers.Spark;
	ViewData.bHasPower[1] = DerivedPowers.Dash;
	ViewData.bHasPower[2] = DerivedPowers.Heavy;
	ViewData.bHasPower[3] = DerivedPowers.Flare;
	ViewData.bHasPower[4] = DerivedPowers.Whirl;
}

void AKomorebiHUD::UpdateProjectedMarkers()
{
	if (!PlayerOwner)
	{
		return;
	}

	int32 ViewW = 0;
	int32 ViewH = 0;
	PlayerOwner->GetViewportSize(ViewW, ViewH);
	if (ViewW <= 0 || ViewH <= 0)
	{
		return;
	}

	const FVector CameraLoc = PlayerOwner->PlayerCameraManager ? PlayerOwner->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
	const FVector CameraFwd = PlayerOwner->PlayerCameraManager ? PlayerOwner->PlayerCameraManager->GetActorForwardVector() : FVector::ForwardVector;
	const APawn* LocalPawn = PlayerOwner->GetPawn();

	// 1. 3D World Waypoint Projection + Viewport Edge Clamping (matching hud.js lines 375-406)
	ViewData.WaypointMarker = FKomorebiProjectedMarker{};
	if (ViewData.Core.bHasWaypoint && !ViewData.Core.bBossActive)
	{
		const FVector WorldTarget = ViewData.Core.WaypointWorldLocation + FVector(0.0f, 0.0f, 235.0f);
		const FVector ToTarget = WorldTarget - CameraLoc;
		const bool bInFront = FVector::DotProduct(ToTarget, CameraFwd) > 15.0f;

		FVector2D ScreenPos = FVector2D::ZeroVector;
		const bool bProjected = PlayerOwner->ProjectWorldLocationToScreen(WorldTarget, ScreenPos, true);

		if (!bInFront || !bProjected)
		{
			// Project direction onto camera right/up plane and push to screen edge
			const FVector CamRight = PlayerOwner->PlayerCameraManager ? PlayerOwner->PlayerCameraManager->GetActorRightVector() : FVector::RightVector;
			const FVector CamUp = PlayerOwner->PlayerCameraManager ? PlayerOwner->PlayerCameraManager->GetActorUpVector() : FVector::UpVector;
			const float DirX = FVector::DotProduct(ToTarget, CamRight);
			const float DirY = FVector::DotProduct(ToTarget, CamUp);
			const float Len = FMath::Max(1.0f, FMath::Sqrt(DirX * DirX + DirY * DirY));
			ScreenPos.X = ViewW * 0.5f + (DirX / Len) * ViewW * 0.6f;
			ScreenPos.Y = ViewH * 0.5f - (DirY / Len) * ViewH * 0.6f;
		}

		constexpr float PadX = 62.0f;
		constexpr float PadTop = 74.0f;
		constexpr float PadBot = 96.0f;
		const float ClampedX = FMath::Clamp(static_cast<float>(ScreenPos.X), PadX, static_cast<float>(ViewW) - PadX);
		const float ClampedY = FMath::Clamp(static_cast<float>(ScreenPos.Y), PadTop, static_cast<float>(ViewH) - PadBot);
		const bool bClamped = !bInFront || ScreenPos.X < PadX || ScreenPos.X > (ViewW - PadX) || ScreenPos.Y < PadTop || ScreenPos.Y > (ViewH - PadBot);

		ViewData.WaypointMarker.bVisible = true;
		ViewData.WaypointMarker.ScreenPos = FVector2D(ClampedX, ClampedY);
		ViewData.WaypointMarker.bClamped = bClamped;
		if (bClamped)
		{
			ViewData.WaypointMarker.ClampAngleDeg = FMath::RadiansToDegrees(FMath::Atan2(ClampedX - ViewW * 0.5f, -(ClampedY - ViewH * 0.5f)));
		}
		ViewData.WaypointMarker.Label = ViewData.Core.WaypointLabel.IsEmpty() ? TEXT("OBJECTIVE") : ViewData.Core.WaypointLabel;
		ViewData.WaypointMarker.Color = ViewData.ChapterAccent;
		if (LocalPawn)
		{
			const int32 DistMeters = FMath::RoundToInt(FVector::Dist2D(LocalPawn->GetActorLocation(), ViewData.Core.WaypointWorldLocation) * 0.01f);
			ViewData.WaypointMarker.SubLabel = FString::Printf(TEXT("%d m"), DistMeters);
			ViewData.DistanceLabel = FString::Printf(TEXT("%d m · target"), DistMeters);
		}
	}

	// 2. 3D Focus Target Reticle Projection
	ViewData.FocusMarker = FKomorebiProjectedMarker{};
	if (ViewData.Core.bHasFocusTarget && ViewData.bTargetLocked)
	{
		const FVector FocusWorld = ViewData.Core.FocusTargetLocation + FVector(0.0f, 0.0f, 185.0f);
		FVector2D FocusScreen = FVector2D::ZeroVector;
		if (FVector::DotProduct(FocusWorld - CameraLoc, CameraFwd) > 10.0f &&
			PlayerOwner->ProjectWorldLocationToScreen(FocusWorld, FocusScreen, true))
		{
			if (FocusScreen.X >= 0.0 && FocusScreen.X <= ViewW && FocusScreen.Y >= 0.0 && FocusScreen.Y <= ViewH)
			{
				ViewData.FocusMarker.bVisible = true;
				ViewData.FocusMarker.ScreenPos = FocusScreen;
			}
		}
	}

	// 3. Remote Peer Nameplates ("Name · Title" with unlocked neon color)
	ViewData.PeerNameplates.Reset();
	if (const UWorld* World = GetWorld())
	{
		if (const AKomorebiGameState* GS = World->GetGameState<AKomorebiGameState>())
		{
			const APlayerState* LocalPS = PlayerOwner->PlayerState;
			for (APlayerState* BasePS : GS->PlayerArray)
			{
				const AKomorebiPlayerState* PeerPS = Cast<AKomorebiPlayerState>(BasePS);
				if (!PeerPS || PeerPS == LocalPS)
				{
					continue;
				}
				const APawn* PeerPawn = PeerPS->GetPawn();
				if (!PeerPawn)
				{
					continue;
				}

				const FVector PeerHead = PeerPawn->GetActorLocation() + FVector(0.0f, 0.0f, 215.0f);
				const float DistCm = LocalPawn ? FVector::Dist(LocalPawn->GetActorLocation(), PeerPawn->GetActorLocation()) : 0.0f;
				if (DistCm > 14000.0f) // 140m nameplate visibility range (matching net.js line 370)
				{
					continue;
				}

				FVector2D TagScreen = FVector2D::ZeroVector;
				if (FVector::DotProduct(PeerHead - CameraLoc, CameraFwd) > 10.0f &&
					PlayerOwner->ProjectWorldLocationToScreen(PeerHead, TagScreen, true))
				{
					FKomorebiProjectedMarker Tag;
					Tag.bVisible = true;
					Tag.ScreenPos = TagScreen;
					Tag.Label = PeerPS->GetFormattedNameplate();
					Tag.Color = PeerPS->GetEquippedLinearColor();
					Tag.bIsDowned = PeerPS->bIsDowned;
					Tag.bIsDriving = PeerPS->bIsDrivingVehicle;
					ViewData.PeerNameplates.Add(Tag);
				}
			}
		}
	}
}

void AKomorebiHUD::UpdateTimersAndQueues(float DeltaSeconds)
{
	// Toast FIFO Queue
	if (ActiveToastRemaining > 0.0f)
	{
		ActiveToastRemaining = FMath::Max(0.0f, ActiveToastRemaining - DeltaSeconds);
		const float FadeOut = FMath::Clamp(ActiveToastRemaining / 0.35f, 0.0f, 1.0f);
		ViewData.ToastAlpha = FadeOut;
	}
	if (ActiveToastRemaining <= 0.0f)
	{
		if (ToastQueue.Num() > 0)
		{
			const FQueuedToast Next = ToastQueue[0];
			ToastQueue.RemoveAt(0);
			ViewData.ActiveToast = Next.Message;
			ActiveToastDuration = FMath::Max(0.5f, Next.Duration);
			ActiveToastRemaining = ActiveToastDuration;
			ViewData.ToastAlpha = 1.0f;
		}
		else
		{
			ViewData.ActiveToast.Empty();
			ViewData.ToastAlpha = 0.0f;
		}
	}

	// Chapter Banner
	if (BannerTimeRemaining > 0.0f)
	{
		BannerTimeRemaining = FMath::Max(0.0f, BannerTimeRemaining - DeltaSeconds);
		ViewData.bBannerVisible = true;
		ViewData.BannerAlpha = FMath::Clamp(BannerTimeRemaining / 0.6f, 0.0f, 1.0f);
	}
	else
	{
		ViewData.bBannerVisible = false;
		ViewData.BannerAlpha = 0.0f;
	}

	// Flash Text
	if (FlashTimeRemaining > 0.0f)
	{
		FlashTimeRemaining = FMath::Max(0.0f, FlashTimeRemaining - DeltaSeconds);
		const float Ratio = FlashTimeRemaining / FMath::Max(0.1f, FlashDuration);
		ViewData.FlashAlpha = (Ratio < 0.3f) ? (Ratio / 0.3f) : 1.0f;
	}
	else
	{
		ViewData.FlashAlpha = 0.0f;
		ViewData.FlashText.Empty();
	}

	// Radio Comms timer + equalizer animation clock
	if (ViewData.bCommsVisible)
	{
		ViewData.CommsAnimTime += DeltaSeconds;
		ViewData.CommsTimeRemaining = FMath::Max(0.0f, ViewData.CommsTimeRemaining - DeltaSeconds);
		if (ViewData.CommsTimeRemaining <= 0.0f && ViewData.CommsVoiceLevel <= 0.02f)
		{
			ViewData.bCommsVisible = false;
		}
	}

	// Damage hit vignette decay
	if (ViewData.HitFeedbackAlpha > 0.0f)
	{
		ViewData.HitFeedbackAlpha = FMath::Max(0.0f, ViewData.HitFeedbackAlpha - DeltaSeconds * 2.2f);
	}
}

void AKomorebiHUD::UpdateHUDState(const FKomorebiHUDState& NewState)
{
	if (NewState.Hp < LastKnownHp - 0.1f)
	{
		ViewData.HitFeedbackAlpha = 1.0f;
	}
	LastKnownHp = NewState.Hp;
	ViewData.Core = NewState;
}

void AKomorebiHUD::SetBossDuelDetails(bool bOpening, bool bReadDone, bool bEvaded, bool bPunished)
{
	ViewData.bBossOpening = bOpening;
	ViewData.bBossReadDone = bReadDone;
	ViewData.bBossEvaded = bEvaded;
	ViewData.bBossPunished = bPunished;
}

void AKomorebiHUD::SetTargetDetails(bool bLocked, bool bWarning, const FString& SubText, const FString& Tell)
{
	ViewData.bTargetLocked = bLocked;
	ViewData.bTargetWarning = bWarning;
	ViewData.TargetSubText = SubText;
	ViewData.TargetTell = Tell;
}

void AKomorebiHUD::SetCombatDefenseDetails(bool bGuardBroken, bool bCounterReady, bool bInvulnerable)
{
	ViewData.bGuardBroken = bGuardBroken;
	ViewData.bCounterReady = bCounterReady;
	ViewData.bInvulnerable = bInvulnerable;
}

void AKomorebiHUD::SetVehicleTelemetryDetails(const FString& StatusText, bool bLoose)
{
	ViewData.VehicleStatusText = StatusText;
	ViewData.bVehicleLoose = bLoose;
}

void AKomorebiHUD::SetMinimapBlips(const TArray<FKomorebiMinimapBlip>& InBlips)
{
	ViewData.MinimapBlips = InBlips;
}

void AKomorebiHUD::SetUnlockedEchoIds(const TSet<FString>& InUnlockedIds)
{
	ViewData.Core.EchoesCollected = InUnlockedIds.Num();
	if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
	{
		GI->SetUnlockedEchoIds(InUnlockedIds);
	}
	if (bPauseMenuOpen)
	{
		RefreshPauseMenuBindings();
	}
}

void AKomorebiHUD::SetCommsVoiceLevel(float Level01)
{
	ViewData.CommsVoiceLevel = FMath::Clamp(Level01, 0.0f, 1.0f);
}

void AKomorebiHUD::EnqueueToast(const FString& Message, float DurationSeconds)
{
	if (Message.IsEmpty())
	{
		return;
	}
	if (ActiveToastRemaining <= 0.0f && ToastQueue.Num() == 0)
	{
		ViewData.ActiveToast = Message;
		ActiveToastDuration = FMath::Max(0.5f, DurationSeconds);
		ActiveToastRemaining = ActiveToastDuration;
		ViewData.ToastAlpha = 1.0f;
	}
	else
	{
		// Avoid duplicate consecutive messages flooding the queue
		if (ViewData.ActiveToast.Equals(Message) || (ToastQueue.Num() > 0 && ToastQueue.Last().Message.Equals(Message)))
		{
			return;
		}
		ToastQueue.Add(FQueuedToast{ Message, DurationSeconds });
	}
}

void AKomorebiHUD::ShowChapterBanner(int32 ChapterIndex, bool bNightOps)
{
	const auto& Chapters = KomorebiCore::GetChapters();
	if (ChapterIndex < 0 || ChapterIndex >= static_cast<int32>(Chapters.size()))
	{
		return;
	}
	const KomorebiCore::ChapterSpec& Ch = Chapters[static_cast<size_t>(ChapterIndex)];
	ViewData.ChapterAccent = FLinearColor::FromSRGBColor(FColor::FromHex(KomorebiBridge::ToFString(Ch.ColorHex)));
	ShowCustomChapterBanner(
		FString::Printf(TEXT("Sector %02d // %s"), ChapterIndex + 1, bNightOps ? TEXT("Night ops") : TEXT("Day ops")),
		KomorebiBridge::ToFString(Ch.Name),
		KomorebiBridge::ToFString(Ch.Subtitle));
}

void AKomorebiHUD::ShowCustomChapterBanner(const FString& SectorLine, const FString& Title, const FString& Subtitle)
{
	ViewData.BannerSectorLine = SectorLine;
	ViewData.BannerTitle = Title;
	ViewData.BannerSubtitle = Subtitle;
	BannerTimeRemaining = 4.5f;
	ViewData.bBannerVisible = true;
	ViewData.BannerAlpha = 1.0f;
}

void AKomorebiHUD::ShowFlashText(const FString& Text, float DurationSeconds)
{
	if (Text.IsEmpty())
	{
		return;
	}
	ViewData.FlashText = Text;
	FlashDuration = FMath::Max(0.25f, DurationSeconds);
	FlashTimeRemaining = FlashDuration;
	ViewData.FlashAlpha = 1.0f;
}

void AKomorebiHUD::ShowCommsMessage(const FKomorebiCommsMessage& Message)
{
	ViewData.Comms = Message;
	ViewData.bCommsVisible = true;
	ViewData.CommsAnimTime = 0.0f;
	ViewData.CommsTimeRemaining = FMath::Clamp(static_cast<float>(Message.Line.Len()) * 0.068f, 6.2f, 10.0f);
}

void AKomorebiHUD::ShowCommsBySpeakerId(const FString& SpeakerId, const FString& Line)
{
	const FSpeakerMetaEntry& Meta = ResolveSpeakerMeta(SpeakerId);
	FKomorebiCommsMessage Msg;
	Msg.SpeakerId = Meta.Id;
	Msg.SpeakerName = Meta.Callsign;
	Msg.SpeakerRole = Meta.Role;
	Msg.Frequency = Meta.Freq;
	Msg.Line = Line;
	Msg.AccentColor = FLinearColor::FromSRGBColor(FColor::FromHex(Meta.HexColor));
	ShowCommsMessage(Msg);
}

void AKomorebiHUD::DismissComms()
{
	ViewData.bCommsVisible = false;
	ViewData.CommsTimeRemaining = 0.0f;
	ViewData.CommsVoiceLevel = 0.0f;
}

void AKomorebiHUD::SetPauseMenuOpen(bool bOpen)
{
	bPauseMenuOpen = bOpen;
	if (PauseMenuWidget.IsValid())
	{
		if (bOpen)
		{
			RefreshPauseMenuBindings();
		}
		PauseMenuWidget->SetVisibility(bOpen ? EVisibility::Visible : EVisibility::Collapsed);
	}
}

void AKomorebiHUD::TogglePauseMenu()
{
	if (AKomorebiPlayerController* KPC = Cast<AKomorebiPlayerController>(PlayerOwner))
	{
		KPC->TogglePauseMenu();
	}
	else
	{
		SetPauseMenuOpen(!bPauseMenuOpen);
	}
}

void AKomorebiHUD::SetShowcaseActive(bool bActive)
{
	bShowcaseActive = bActive;
	if (bShowcaseActive)
	{
		ShowcaseShotIndex = 0;
		ShowcaseElapsed = 0.0f;
	}
	if (ShowcaseOverlayWidget.IsValid())
	{
		ShowcaseOverlayWidget->SetVisibility(bActive ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
		if (bActive)
		{
			ShowcaseOverlayWidget->UpdateShowcase(ShowcaseShotIndex, 0.0f);
		}
	}
	if (bPauseMenuOpen)
	{
		RefreshPauseMenuBindings();
	}
}

void AKomorebiHUD::UpdateShowcaseState(int32 ShotIndex, float ShotProgress01)
{
	ShowcaseShotIndex = ShotIndex;
	ShowcaseElapsed = FMath::Clamp(ShotProgress01, 0.0f, 1.0f) * static_cast<float>(KomorebiCore::kShotDuration);
	if (ShowcaseOverlayWidget.IsValid())
	{
		ShowcaseOverlayWidget->UpdateShowcase(ShotIndex, ShotProgress01);
	}
}

void AKomorebiHUD::AdvanceShowcaseShotLocal()
{
	const int32 Total = static_cast<int32>(KomorebiCore::GetShowcaseShots().size());
	if (Total <= 0)
	{
		return;
	}
	ShowcaseShotIndex = (ShowcaseShotIndex + 1) % Total;
	ShowcaseElapsed = 0.0f;
	if (ShowcaseOverlayWidget.IsValid())
	{
		ShowcaseOverlayWidget->UpdateShowcase(ShowcaseShotIndex, 0.0f);
	}
}

void AKomorebiHUD::SelectShowcaseShotLocal(int32 ShotIndex)
{
	const int32 Total = static_cast<int32>(KomorebiCore::GetShowcaseShots().size());
	if (Total <= 0)
	{
		return;
	}
	ShowcaseShotIndex = ((ShotIndex % Total) + Total) % Total;
	ShowcaseElapsed = 0.0f;
	if (ShowcaseOverlayWidget.IsValid())
	{
		ShowcaseOverlayWidget->UpdateShowcase(ShowcaseShotIndex, 0.0f);
	}
	if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
	{
		GI->RequestShowcaseCommand(ShowcaseShotIndex);
	}
}

void AKomorebiHUD::RefreshPauseMenuBindings()
{
	if (!PauseMenuWidget.IsValid())
	{
		return;
	}

	FKomorebiPauseMenuState MenuState;
	MenuState.ClearedSectors = ViewData.Core.ClearedSectors;
	MenuState.bShowcaseActive = bShowcaseActive;
	MenuState.bIsNight = ViewData.Core.bIsNight;

	if (const UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		MenuState.bFullscreen = Settings->GetFullscreenMode() != EWindowMode::Windowed;
	}

	if (const UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
	{
		MenuState.Scene = GI->SelectedScene;
		MenuState.Mode = GI->SelectedPlayMode;
		MenuState.Nickname = GI->LocalPlayerNickname;
		MenuState.EquippedTitle = GI->LocalEquippedTitle;
		MenuState.EquippedColorIndex = GI->LocalEquippedColorIndex;
		MenuState.OnlineStats = GI->GetLocalOnlineStats();
		MenuState.Audio = GI->GetAudioSettings();
		MenuState.QualityTier = GI->CachedQualityTier;
		MenuState.bComicInk = GI->bComicInkEnabled;
		MenuState.bOilPaintImpasto = GI->bOilPaintImpastoEnabled;
		MenuState.UnlockedEchoIds = GI->GetUnlockedEchoIds();

		if (const UOnlineSessionSubsystem* SessionSub = GI->GetOnlineSessionSubsystem())
		{
			MenuState.OnlineStatusText = SessionSub->GetStatusText();
			MenuState.CurrentRoomCode = SessionSub->GetCurrentRoomCode();
		}
	}

	if (const UWorld* World = GetWorld())
	{
		if (const AKomorebiGameState* GS = World->GetGameState<AKomorebiGameState>())
		{
			MenuState.Scene = GS->ActiveWorldScene;
			MenuState.Mode = GS->ActivePlayMode;
			if (!GS->ActiveRoomCode.IsEmpty())
			{
				MenuState.CurrentRoomCode = GS->ActiveRoomCode;
			}
			if (GS->IsOnlineCityMode())
			{
				const int32 Players = FMath::Max(1, GS->GetConnectedPlayerCount());
				MenuState.OnlineStatusText = FString::Printf(
					TEXT("Online · %d %s%s"),
					Players,
					Players == 1 ? TEXT("player") : TEXT("players"),
					MenuState.CurrentRoomCode.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" · room %s"), *MenuState.CurrentRoomCode));
			}
		}
	}

	PauseMenuWidget->RefreshState(MenuState);
}
