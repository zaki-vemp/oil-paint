// Copyright Komorebi Project. PlayerController bridging input focus, pause/showcase/day-night hotkeys, and Online City RPCs.
#include "Network/KomorebiPlayerController.h"

#include "Network/KomorebiGameInstance.h"
#include "Network/KomorebiGameMode.h"
#include "Network/KomorebiOnlineGameMode.h"
#include "Network/KomorebiGameState.h"
#include "Network/KomorebiPlayerState.h"
#include "Network/OnlineActivitiesActor.h"
#include "UI/KomorebiHUD.h"
#include "Game/OnlineMeta.h"
#include "Components/InputComponent.h"
#include "InputCoreTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

AKomorebiPlayerController::AKomorebiPlayerController()
{
	bShowMouseCursor = false;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;
}

void AKomorebiPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalController())
	{
		FInputModeGameOnly GameModeInput;
		SetInputMode(GameModeInput);
		SetShowMouseCursor(false);

		SyncLocalProfileToServer();

		if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
		{
			if (GI->bStartWithShowcaseTour)
			{
				GI->bStartWithShowcaseTour = false;
				ToggleShowcaseTour();
			}
		}
	}
}

void AKomorebiPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!InputComponent)
	{
		return;
	}

	// Pause / Showcase Exit: Escape works even while paused or focused in UI.
	{
		FInputKeyBinding& EscBind = InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AKomorebiPlayerController::HandleEscapeKeyPressed);
		EscBind.bExecuteWhenPaused = true;
		EscBind.bConsumeInput = false;
	}

	// P and H open/close Pause Menu (matching legacy-web/src/boot.js).
	{
		FInputKeyBinding& PBind = InputComponent->BindKey(EKeys::P, IE_Pressed, this, &AKomorebiPlayerController::HandlePauseHotkeyPressed);
		PBind.bExecuteWhenPaused = true;
		PBind.bConsumeInput = false;
	}
	{
		FInputKeyBinding& HBind = InputComponent->BindKey(EKeys::H, IE_Pressed, this, &AKomorebiPlayerController::HandlePauseHotkeyPressed);
		HBind.bExecuteWhenPaused = true;
		HBind.bConsumeInput = false;
	}

	// Space advances showcase shot while Investor Tour is active (does not consume during gameplay).
	{
		FInputKeyBinding& SpaceBind = InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AKomorebiPlayerController::HandleSpaceKeyPressed);
		SpaceBind.bExecuteWhenPaused = false;
		SpaceBind.bConsumeInput = false;
	}

	// K dismisses active radio comms panel.
	{
		FInputKeyBinding& KBind = InputComponent->BindKey(EKeys::K, IE_Pressed, this, &AKomorebiPlayerController::HandleDismissCommsKeyPressed);
		KBind.bExecuteWhenPaused = false;
		KBind.bConsumeInput = false;
	}

	// M toggles Story Sectors <-> Free Roam in solo Neo-Kyoto.
	{
		FInputKeyBinding& MBind = InputComponent->BindKey(EKeys::M, IE_Pressed, this, &AKomorebiPlayerController::HandleModeToggleKeyPressed);
		MBind.bExecuteWhenPaused = false;
		MBind.bConsumeInput = false;
	}

	// F9 toggles Showcase Tour; F8 toggles Day/Night shift quickly.
	{
		FInputKeyBinding& TourBind = InputComponent->BindKey(EKeys::F9, IE_Pressed, this, &AKomorebiPlayerController::ToggleShowcaseTour);
		TourBind.bExecuteWhenPaused = true;
		TourBind.bConsumeInput = false;
	}
	{
		FInputKeyBinding& DayNightBind = InputComponent->BindKey(EKeys::F8, IE_Pressed, this, &AKomorebiPlayerController::HandleDayNightKeyPressed);
		DayNightBind.bExecuteWhenPaused = false;
		DayNightBind.bConsumeInput = false;
	}
}

void AKomorebiPlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	if (IsLocalController() && !bProfileSyncedOnce)
	{
		SyncLocalProfileToServer();
	}
}

void AKomorebiPlayerController::SyncLocalProfileToServer()
{
	if (!IsLocalController())
	{
		return;
	}

	UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance());
	AKomorebiPlayerState* PS = GetPlayerState<AKomorebiPlayerState>();
	if (!GI || !PS)
	{
		return;
	}

	const KomorebiCore::OnlineStats& LocalStats = GI->GetLocalOnlineStats();
	PS->ServerSyncInitialStats(
		LocalStats.Contracts,
		LocalStats.Mechs,
		LocalStats.Races,
		LocalStats.Duels,
		LocalStats.Revives);
	PS->ServerSetProfile(
		GI->LocalPlayerNickname,
		GI->LocalEquippedTitle,
		GI->LocalEquippedColorIndex);
	bProfileSyncedOnce = true;
}

bool AKomorebiPlayerController::IsOnlineCityMode() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const AKomorebiGameState* GS = World->GetGameState<AKomorebiGameState>())
		{
			return GS->IsOnlineCityMode();
		}
		if (World->GetNetMode() != NM_Standalone)
		{
			return true;
		}
	}
	if (const UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
	{
		return GI->SelectedPlayMode == EKomorebiPlayMode::OnlineCity;
	}
	return false;
}

AKomorebiHUD* AKomorebiPlayerController::GetKomorebiHUD() const
{
	return Cast<AKomorebiHUD>(GetHUD());
}

bool AKomorebiPlayerController::IsEditingTextWidget() const
{
	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}
	TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
	if (!Focused.IsValid())
	{
		return false;
	}
	const FName WidgetType = Focused->GetType();
	return WidgetType == FName(TEXT("SEditableText")) || WidgetType == FName(TEXT("SEditableTextBox")) || WidgetType == FName(TEXT("SMultiLineEditableText"));
}

void AKomorebiPlayerController::TogglePauseMenu()
{
	SetPauseMenuOpen(!IsPauseMenuOpen());
}

void AKomorebiPlayerController::SetPauseMenuOpen(bool bOpen)
{
	AKomorebiHUD* KomorebiHUD = GetKomorebiHUD();
	if (KomorebiHUD)
	{
		KomorebiHUD->SetPauseMenuOpen(bOpen);
	}

	if (bOpen)
	{
		SetShowMouseCursor(true);
		bEnableClickEvents = true;
		bEnableMouseOverEvents = true;

		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		SetInputMode(InputMode);

		// Online City NEVER pauses or slows the simulation (legacy-web rule).
		if (!IsOnlineCityMode())
		{
			SetPause(true);
		}
	}
	else
	{
		if (!IsOnlineCityMode())
		{
			SetPause(false);
		}

		// Keep cursor visible if Investor Showcase overlay is currently active.
		const bool bShowcaseOn = KomorebiHUD && KomorebiHUD->IsShowcaseActive();
		if (bShowcaseOn)
		{
			SetShowMouseCursor(true);
			FInputModeGameAndUI InputMode;
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			SetInputMode(InputMode);
		}
		else
		{
			SetShowMouseCursor(false);
			bEnableClickEvents = false;
			bEnableMouseOverEvents = false;
			FInputModeGameOnly InputMode;
			SetInputMode(InputMode);
		}
	}
}

bool AKomorebiPlayerController::IsPauseMenuOpen() const
{
	const AKomorebiHUD* KomorebiHUD = GetKomorebiHUD();
	return KomorebiHUD && KomorebiHUD->IsPauseMenuOpen();
}

void AKomorebiPlayerController::ToggleShowcaseTour()
{
	if (AKomorebiHUD* KomorebiHUD = GetKomorebiHUD())
	{
		if (KomorebiHUD->IsPauseMenuOpen())
		{
			SetPauseMenuOpen(false);
		}
		const bool bNextActive = !KomorebiHUD->IsShowcaseActive();
		KomorebiHUD->SetShowcaseActive(bNextActive);
		SetShowMouseCursor(bNextActive);
		if (bNextActive)
		{
			FInputModeGameAndUI InputMode;
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			SetInputMode(InputMode);
		}
		else
		{
			FInputModeGameOnly InputMode;
			SetInputMode(InputMode);
		}
	}

	if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
	{
		GI->RequestShowcaseCommand(-2); // Toggle
	}
}

void AKomorebiPlayerController::AdvanceShowcaseShot()
{
	if (AKomorebiHUD* KomorebiHUD = GetKomorebiHUD())
	{
		if (KomorebiHUD->IsShowcaseActive())
		{
			KomorebiHUD->AdvanceShowcaseShotLocal();
		}
	}

	if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
	{
		GI->RequestShowcaseCommand(-3); // Next shot
	}
}

void AKomorebiPlayerController::ExitShowcaseTour()
{
	if (AKomorebiHUD* KomorebiHUD = GetKomorebiHUD())
	{
		if (KomorebiHUD->IsShowcaseActive())
		{
			KomorebiHUD->SetShowcaseActive(false);
			KomorebiHUD->EnqueueToast(TEXT("Tour exited — live controls active"));
			SetShowMouseCursor(false);
			FInputModeGameOnly InputMode;
			SetInputMode(InputMode);
		}
	}

	if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
	{
		GI->RequestShowcaseCommand(-1); // Stop
	}
}

void AKomorebiPlayerController::ToggleDayNightPhase()
{
	if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
	{
		if (GI->SelectedScene != EKomorebiWorldScene::NeoKyoto)
		{
			return;
		}
		GI->RequestDayNightShift();
	}
}

void AKomorebiPlayerController::ToggleSoloRoamMode()
{
	if (IsOnlineCityMode() || IsPauseMenuOpen())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (AKomorebiGameMode* GM = World->GetAuthGameMode<AKomorebiGameMode>())
		{
			GM->ToggleStoryAndRoamMode(this);
		}
	}
}

void AKomorebiPlayerController::DismissActiveComms()
{
	if (AKomorebiHUD* KomorebiHUD = GetKomorebiHUD())
	{
		KomorebiHUD->DismissComms();
	}
}

void AKomorebiPlayerController::HandleEscapeKeyPressed()
{
	AKomorebiHUD* KomorebiHUD = GetKomorebiHUD();
	if (KomorebiHUD && KomorebiHUD->IsShowcaseActive() && !KomorebiHUD->IsPauseMenuOpen())
	{
		ExitShowcaseTour();
		return;
	}
	TogglePauseMenu();
}

void AKomorebiPlayerController::HandlePauseHotkeyPressed()
{
	if (IsEditingTextWidget())
	{
		return;
	}
	TogglePauseMenu();
}

void AKomorebiPlayerController::HandleSpaceKeyPressed()
{
	AKomorebiHUD* KomorebiHUD = GetKomorebiHUD();
	if (KomorebiHUD && KomorebiHUD->IsShowcaseActive() && !KomorebiHUD->IsPauseMenuOpen())
	{
		AdvanceShowcaseShot();
	}
}

void AKomorebiPlayerController::HandleDayNightKeyPressed()
{
	if (IsEditingTextWidget())
	{
		return;
	}
	ToggleDayNightPhase();
}

void AKomorebiPlayerController::HandleModeToggleKeyPressed()
{
	if (IsEditingTextWidget())
	{
		return;
	}
	ToggleSoloRoamMode();
}

void AKomorebiPlayerController::HandleDismissCommsKeyPressed()
{
	if (IsEditingTextWidget())
	{
		return;
	}
	DismissActiveComms();
}

void AKomorebiPlayerController::ServerRequestRevive_Implementation(AKomorebiPlayerState* DownedTarget)
{
	if (!DownedTarget || !DownedTarget->bIsDowned)
	{
		return;
	}

	UWorld* World = GetWorld();
	AOnlineActivitiesActor* Activities = World
		? Cast<AOnlineActivitiesActor>(UGameplayStatics::GetActorOfClass(World, AOnlineActivitiesActor::StaticClass()))
		: nullptr;
	if (Activities)
	{
		Activities->TryServerManualRevive(this, DownedTarget);
	}
}

void AKomorebiPlayerController::ServerSubmitRaceTime_Implementation(float ElapsedSeconds)
{
	if (!FMath::IsFinite(ElapsedSeconds) || ElapsedSeconds < 5.0f || ElapsedSeconds > static_cast<float>(KomorebiCore::kRaceConfig.LimitSeconds))
	{
		return;
	}

	UWorld* World = GetWorld();
	AKomorebiGameState* GS = World ? World->GetGameState<AKomorebiGameState>() : nullptr;
	AKomorebiPlayerState* PS = GetPlayerState<AKomorebiPlayerState>();
	if (!GS || !PS)
	{
		return;
	}

	const float RoundedTime = FMath::RoundToFloat(ElapsedSeconds * 100.0f) * 0.01f;
	const bool bNewBest = GS->TryRecordRoomBestRace(RoundedTime, PS->DisplayName);
	PS->IncrementOnlineStat(TEXT("races"));

	ClientShowToast(FString::Printf(
		TEXT("Finish · %.1fs%s"),
		RoundedTime,
		bNewBest ? TEXT(" · room best") : TEXT("")));

	GS->MulticastAnnounceEvent(
		FString::Printf(TEXT("%s ran the street race · %.1fs"), *PS->DisplayName, RoundedTime),
		bNewBest ? TEXT("NEW ROOM RECORD") : TEXT(""));
}

void AKomorebiPlayerController::ServerReportPvPHit_Implementation(
	AKomorebiPlayerState* TargetPlayer,
	EKomorebiAttackType AttackType,
	FVector AttackerLocation)
{
	UWorld* World = GetWorld();
	AOnlineActivitiesActor* Activities = World
		? Cast<AOnlineActivitiesActor>(UGameplayStatics::GetActorOfClass(World, AOnlineActivitiesActor::StaticClass()))
		: nullptr;
	if (Activities)
	{
		Activities->ProcessServerPvPHit(this, TargetPlayer, AttackType, AttackerLocation);
	}
}

void AKomorebiPlayerController::ServerRequestVehiclePossess_Implementation(APawn* VehiclePawn, bool bEnterVehicle)
{
	AKomorebiPlayerState* PS = GetPlayerState<AKomorebiPlayerState>();
	if (!PS || PS->bIsDowned)
	{
		return;
	}

	if (bEnterVehicle)
	{
		if (!VehiclePawn || VehiclePawn->GetController() != nullptr)
		{
			return;
		}
		APawn* CurrentPawn = GetPawn();
		if (CurrentPawn && CurrentPawn != VehiclePawn)
		{
			const float DistCm = FVector::Dist(CurrentPawn->GetActorLocation(), VehiclePawn->GetActorLocation());
			if (DistCm > 600.0f) // 6 m max enter reach
			{
				return;
			}
			CachedOnFootPawn = CurrentPawn;
			CurrentPawn->SetActorHiddenInGame(true);
			CurrentPawn->SetActorEnableCollision(false);
		}
		Possess(VehiclePawn);
		PS->bIsDrivingVehicle = true;
	}
	else
	{
		APawn* CurrentVehicle = GetPawn();
		APawn* OnFoot = CachedOnFootPawn.Get();
		if (OnFoot && CurrentVehicle && OnFoot != CurrentVehicle)
		{
			const FVector ExitLoc = CurrentVehicle->GetActorLocation() + CurrentVehicle->GetActorRightVector() * 220.0f + FVector(0.0f, 0.0f, 35.0f);
			OnFoot->SetActorLocation(ExitLoc, false, nullptr, ETeleportType::TeleportPhysics);
			OnFoot->SetActorHiddenInGame(false);
			OnFoot->SetActorEnableCollision(true);
			Possess(OnFoot);
		}
		CachedOnFootPawn.Reset();
		PS->bIsDrivingVehicle = false;
	}
}

void AKomorebiPlayerController::ServerRegisterSiegeMechHit_Implementation()
{
	UWorld* World = GetWorld();
	AKomorebiOnlineGameMode* OnlineGM = World ? World->GetAuthGameMode<AKomorebiOnlineGameMode>() : nullptr;
	AKomorebiPlayerState* PS = GetPlayerState<AKomorebiPlayerState>();
	if (OnlineGM && PS)
	{
		OnlineGM->RegisterSiegeMechContributor(PS);
	}
}

void AKomorebiPlayerController::ClientShowToast_Implementation(const FString& Message)
{
	if (AKomorebiHUD* KomorebiHUD = GetKomorebiHUD())
	{
		KomorebiHUD->EnqueueToast(Message);
	}
}

void AKomorebiPlayerController::ClientShowFlashText_Implementation(const FString& Text)
{
	if (AKomorebiHUD* KomorebiHUD = GetKomorebiHUD())
	{
		KomorebiHUD->ShowFlashText(Text);
	}
}

void AKomorebiPlayerController::ClientShowComms_Implementation(const FKomorebiCommsMessage& Message)
{
	if (AKomorebiHUD* KomorebiHUD = GetKomorebiHUD())
	{
		KomorebiHUD->ShowCommsMessage(Message);
	}
}
