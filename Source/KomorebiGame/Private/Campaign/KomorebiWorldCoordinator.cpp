// Copyright Komorebi Project. Runtime world orchestration wiring all KomorebiGame subsystems and actors together.
#include "Campaign/KomorebiWorldCoordinator.h"

#include "AI/EnemySquadSubsystem.h"
#include "AI/RenBossCharacter.h"
#include "AI/StreetEnemyCharacter.h"
#include "Audio/KomorebiAudioSubsystem.h"
#include "Campaign/KomorebiSaveGame.h"
#include "Campaign/MissionSubsystem.h"
#include "Character/FollowCameraComponent.h"
#include "Character/KaelCharacter.h"
#include "Character/KaelMovementComponent.h"
#include "Combat/CombatComponent.h"
#include "Network/KomorebiGameInstance.h"
#include "Network/KomorebiGameMode.h"
#include "Network/KomorebiPlayerController.h"
#include "UI/KomorebiHUD.h"
#include "Vehicles/KomorebiVehiclePawn.h"
#include "Vehicles/TrafficSubsystem.h"
#include "World/DayNightAtmosphereActor.h"
#include "World/GraphicsSettingsSubsystem.h"
#include "World/KomorebiTerrainActor.h"
#include "World/NeoKyotoCityActor.h"
#include "World/SnowboundWorldActor.h"
#include "World/StreetPropsActor.h"
#include "Game/ChaptersData.h"
#include "Game/DayNightCycle.h"
#include "Game/LevelsData.h"
#include "World/CoordBridge.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

bool UKomorebiWorldCoordinator::ShouldCreateSubsystem(UObject* Outer) const
{
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

TStatId UKomorebiWorldCoordinator::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UKomorebiWorldCoordinator, STATGROUP_Tickables);
}

bool UKomorebiWorldCoordinator::IsTickable() const
{
	return bWorldWired && GetWorld() && !GetWorld()->bIsTearingDown;
}

void UKomorebiWorldCoordinator::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (const AKomorebiGameMode* GM = InWorld.GetAuthGameMode<AKomorebiGameMode>())
	{
		ActiveScene = GM->GetActiveWorldScene();
		ActiveMode = GM->GetActivePlayMode();
	}
	else if (const UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(InWorld.GetGameInstance()))
	{
		ActiveScene = GI->SelectedScene;
		ActiveMode = GI->SelectedPlayMode;
	}

	DiscoverWorldActors();
	WireSubsystemsAndActors();
	bWorldWired = true;
}

void UKomorebiWorldCoordinator::DiscoverWorldActors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TerrainActor = Cast<AKomorebiTerrainActor>(UGameplayStatics::GetActorOfClass(World, AKomorebiTerrainActor::StaticClass()));
	CityActor = Cast<ANeoKyotoCityActor>(UGameplayStatics::GetActorOfClass(World, ANeoKyotoCityActor::StaticClass()));
	StreetPropsActor = Cast<AStreetPropsActor>(UGameplayStatics::GetActorOfClass(World, AStreetPropsActor::StaticClass()));
	SnowboundActor = Cast<ASnowboundWorldActor>(UGameplayStatics::GetActorOfClass(World, ASnowboundWorldActor::StaticClass()));
	AtmosphereActor = Cast<ADayNightAtmosphereActor>(UGameplayStatics::GetActorOfClass(World, ADayNightAtmosphereActor::StaticClass()));
	ShowcaseDirector = Cast<AShowcaseCameraDirector>(UGameplayStatics::GetActorOfClass(World, AShowcaseCameraDirector::StaticClass()));
}

void UKomorebiWorldCoordinator::WireSubsystemsAndActors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TFunction<double(double, double)> TerrainHeightThreeFn;
	if (AKomorebiTerrainActor* Terrain = TerrainActor.Get())
	{
		TerrainHeightThreeFn = [WeakTerrain = TWeakObjectPtr<AKomorebiTerrainActor>(Terrain)](double X, double Z) -> double
		{
			if (const AKomorebiTerrainActor* T = WeakTerrain.Get())
			{
				return T->GetHeightAtThree(X, Z);
			}
			return 0.0;
		};
	}

	// Day/Night Atmosphere
	if (ADayNightAtmosphereActor* Atmo = AtmosphereActor.Get())
	{
		Atmo->SetOnlineClock(ActiveMode == EKomorebiPlayMode::OnlineCity);
		Atmo->OnPhaseShifted.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleDayNightPhaseShifted);
	}

	// Showcase Director
	if (AShowcaseCameraDirector* Director = ShowcaseDirector.Get())
	{
		if (TerrainHeightThreeFn)
		{
			Director->SetTerrainHeightProvider(TerrainHeightThreeFn);
		}
		Director->OnShowcaseStateChanged.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleShowcaseStateChanged);
		Director->OnShowcaseShotChanged.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleShowcaseShotChanged);
		Director->OnShowcaseProgressUpdated.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleShowcaseProgressUpdated);
		Director->OnShowcaseDayPhasePreview.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleShowcaseDayPhasePreview);
	}

	// Neo-Kyoto Campaigns, Crews, Duels, Missions, Vehicles
	if (ActiveScene == EKomorebiWorldScene::NeoKyoto)
	{
		const bool bFresh = (Cast<UKomorebiGameInstance>(World->GetGameInstance()) &&
			Cast<UKomorebiGameInstance>(World->GetGameInstance())->bIgnoreSavedCheckpoint);
		int32 SavedCleared = 0;

		if (UMissionSubsystem* Missions = World->GetSubsystem<UMissionSubsystem>())
		{
			if (TerrainHeightThreeFn)
			{
				Missions->SetTerrainHeightProvider(TerrainHeightThreeFn);
			}
			Missions->InitializeCampaign(ActiveMode, bFresh);
			SavedCleared = Missions->GetClearedSectors();

			Missions->OnToast.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleMissionToast);
			Missions->OnFlashText.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleMissionFlash);
			Missions->OnCommsMessageStarted.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleMissionCommsStarted);
			Missions->OnRewardCash.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleMissionRewardCash);
			Missions->OnHealPlayer.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleMissionHealPlayer);
			Missions->OnReduceHeat.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleMissionReduceHeat);
			Missions->OnCheckpointActivated.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleCheckpointActivated);
			Missions->OnSectorGateOpened.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleSectorGateOpened);
		}

		if (UEnemySquadSubsystem* Squad = World->GetSubsystem<UEnemySquadSubsystem>())
		{
			Squad->ConfigureSession(ActiveScene, ActiveMode, SavedCleared);
			Squad->SetSectorMissionDonePredicate([WeakWorld = TWeakObjectPtr<UWorld>(World)](int32 Seg) -> bool
			{
				if (UWorld* W = WeakWorld.Get())
				{
					if (const UMissionSubsystem* M = W->GetSubsystem<UMissionSubsystem>())
					{
						return M->IsSectorFieldOpDone(Seg);
					}
				}
				return true;
			});

			for (const KomorebiCore::LevelSpec& Lvl : KomorebiCore::GetLevels())
			{
				Squad->RegisterRelayLocation(KomorebiBridge::ThreeXZToFVector(Lvl.Checkpoint.X, 0.0, Lvl.Checkpoint.Z));
			}

			if (World->GetNetMode() != NM_Client)
			{
				Squad->SpawnCampaignPopulation(SavedCleared);
			}

			Squad->OnSegmentCleared.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleSquadSegmentCleared);
			Squad->OnToast.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleSquadToast);
			Squad->OnFlashText.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleSquadFlash);
			Squad->OnAudioCue.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleSquadAudioCue);
		}

		if (UTrafficSubsystem* Traffic = World->GetSubsystem<UTrafficSubsystem>())
		{
			if (TerrainHeightThreeFn)
			{
				Traffic->SetTerrainHeightProvider(TerrainHeightThreeFn);
			}
			if (World->GetNetMode() != NM_Client && Traffic->GetAllVehicles().Num() == 0)
			{
				Traffic->SpawnCityFleet(true);
			}
			Traffic->OnVehicleCarjacked.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleTrafficCarjacked);
			Traffic->OnToast.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleTrafficToast);
			Traffic->OnFlashText.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleTrafficFlash);

			for (AKomorebiVehiclePawn* Veh : Traffic->GetAllVehicles())
			{
				if (Veh)
				{
					Veh->OnVehicleRamPawn.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleVehicleRamPawn);
					Veh->OnDriverCrashDamage.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleVehicleCrashDamage);
					Veh->OnCameraJolt.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleVehicleCameraJolt);
				}
			}
		}
	}

	// Sync initial echoes & audio settings into GameInstance / HUD
	if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(World->GetGameInstance()))
	{
		if (const UMissionSubsystem* Missions = World->GetSubsystem<UMissionSubsystem>())
		{
			TSet<FString> EchoSet;
			for (const FString& Id : Missions->GetCollectedEchoIds())
			{
				EchoSet.Add(Id);
			}
			GI->SetUnlockedEchoIds(EchoSet);
		}
		if (GI->bStartTourOnLaunch && ActiveScene == EKomorebiWorldScene::NeoKyoto)
		{
			GI->bStartTourOnLaunch = false;
			if (AShowcaseCameraDirector* Director = ShowcaseDirector.Get())
			{
				Director->StartTour(0);
			}
		}
	}
}

void UKomorebiWorldCoordinator::WirePlayerCharacter(AKaelCharacter* Kael)
{
	if (!Kael || BoundKael.Get() == Kael)
	{
		return;
	}
	BoundKael = Kael;

	if (Kael->FollowCamera)
	{
		Kael->FollowCamera->SetTerrainActor(TerrainActor.Get());
		if (ActiveScene == EKomorebiWorldScene::Snowbound)
		{
			Kael->FollowCamera->ApplySnowboundPreset();
		}
		else
		{
			Kael->FollowCamera->ApplyNeoKyotoPreset();
		}
	}

	Kael->SetRightHandLanternMode(ActiveScene == EKomorebiWorldScene::Snowbound);
	Kael->OnRideRequested.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleRideToggleRequested);
	Kael->OnPauseRequested.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandlePauseToggleRequested);

	if (UCombatComponent* Combat = Kael->CombatComponent)
	{
		Combat->WorldScene = ActiveScene;
		Combat->PlayMode = ActiveMode;
		if (const UMissionSubsystem* Missions = GetWorld()->GetSubsystem<UMissionSubsystem>())
		{
			Combat->ClearedSectors = Missions->GetClearedSectors();
		}

		Combat->OnToast.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleCombatToast);
		Combat->OnFlashText.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleCombatFlash);
		Combat->OnAudioCue.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleCombatAudioCue);
		Combat->OnMissionStrike.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleCombatMissionStrike);
		Combat->OnPlayerDied.AddUniqueDynamic(this, &UKomorebiWorldCoordinator::HandleCombatPlayerDied);
	}

	LastFootstepWorldLoc = Kael->GetActorLocation();
}

void UKomorebiWorldCoordinator::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	AKaelCharacter* Kael = GetLocalKael();
	if (Kael && BoundKael.Get() != Kael)
	{
		WirePlayerCharacter(Kael);
	}

	const bool bPaused = UGameplayStatics::IsGamePaused(World);
	if (UGraphicsSettingsSubsystem* Gfx = World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UGraphicsSettingsSubsystem>() : nullptr)
	{
		Gfx->SampleFrameTime(DeltaTime, !bPaused);
	}

	const FVector FocusLoc = Kael ? Kael->GetActorLocation() : FVector::ZeroVector;
	const KomorebiCore::Vec3 FocusThree = KomorebiBridge::FVectorToThree(FocusLoc);
	const bool bRoam = (ActiveMode == EKomorebiPlayMode::FreeRoam || ActiveMode == EKomorebiPlayMode::OnlineCity);
	int32 ClearedSectors = 0;
	if (const UMissionSubsystem* Missions = World->GetSubsystem<UMissionSubsystem>())
	{
		ClearedSectors = Missions->GetClearedSectors();
	}

	// Atmosphere, City Palette, Props Culling, Snowbound Tracks/Lantern
	float NightFactor = 0.0f;
	if (ADayNightAtmosphereActor* Atmo = AtmosphereActor.Get())
	{
		Atmo->SetFocusWorldLocation(FocusLoc);
		NightFactor = static_cast<float>(Atmo->GetCurrentState().Night);
	}

	if (ActiveScene == EKomorebiWorldScene::NeoKyoto)
	{
		if (ANeoKyotoCityActor* City = CityActor.Get())
		{
			City->UpdateDistrictPalette(DeltaTime, FocusThree.Z, ClearedSectors, bRoam);
		}
		if (AStreetPropsActor* Props = StreetPropsActor.Get())
		{
			Props->UpdateVisibility(FocusLoc);
		}
		if (UTrafficSubsystem* Traffic = World->GetSubsystem<UTrafficSubsystem>())
		{
			Traffic->SetFleetNightPhase(NightFactor);
		}
	}
	else if (ASnowboundWorldActor* Snow = SnowboundActor.Get())
	{
		if (Kael)
		{
			Snow->UpdateLanternLight(Kael->GetActorLocation() + FVector(0.0f, 35.0f, 95.0f), 1.0f);
			const float MovedMeters = FVector::Dist2D(FocusLoc, LastFootstepWorldLoc) * 0.01f;
			if (MovedMeters >= 0.45f)
			{
				Snow->StampFootprint(FocusLoc, 24.0f);
				LastFootstepWorldLoc = FocusLoc;
			}
		}
	}

	// Footstep audio on foot
	if (Kael && !Kael->IsDriving())
	{
		const float SpeedMps = Kael->GetVelocity().Size2D() * 0.01f;
		if (SpeedMps > 1.2f)
		{
			FootstepAccumMeters += SpeedMps * DeltaTime;
			if (FootstepAccumMeters >= 2.2f)
			{
				FootstepAccumMeters = 0.0f;
				if (UKomorebiAudioSubsystem* Audio = World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UKomorebiAudioSubsystem>() : nullptr)
				{
					Audio->PlaySfx(EKomorebiSfx::Footstep, FMath::Min(1.4f, SpeedMps / 5.0f));
				}
			}
		}
	}

	// Feed live combat snapshot to UMissionSubsystem
	if (ActiveScene == EKomorebiWorldScene::NeoKyoto)
	{
		if (UMissionSubsystem* Missions = World->GetSubsystem<UMissionSubsystem>())
		{
			if (UEnemySquadSubsystem* Squad = World->GetSubsystem<UEnemySquadSubsystem>())
			{
				FKomorebiMissionWorldContext Ctx;
				Ctx.bValid = (Kael != nullptr);
				Ctx.PlayerLocation = FocusLoc;
				Ctx.PlayerHp = (Kael && Kael->CombatComponent) ? Kael->CombatComponent->Hp : 100.0f;
				Ctx.bDriving = Kael && Kael->IsDriving();
				Ctx.Heat = Squad->GetHeat();
				Ctx.bBossEngaged = (Squad->GetActiveEngagedBoss() != nullptr);

				Ctx.Sectors.SetNum(6);
				for (const TWeakObjectPtr<AStreetEnemyCharacter>& WeakE : Squad->GetRegisteredEnemies())
				{
					const AStreetEnemyCharacter* E = WeakE.Get();
					if (!E)
					{
						continue;
					}
					if (E->EnemyKind == EKomorebiEnemyKind::Thief && E->SegmentIndex == 1 && !E->bIsDead && !E->bIsGone)
					{
						Ctx.bCourierAlive = true;
						Ctx.CourierLocation = E->GetActorLocation();
					}
					if (E->SegmentIndex >= 0 && E->SegmentIndex < 6 && !E->IsCiv())
					{
						FKomorebiSectorCombatSnapshot& Snap = Ctx.Sectors[E->SegmentIndex];
						Snap.TotalCrewCount++;
						if (!E->bIsDead && !E->bIsGone)
						{
							Snap.AliveCrewCount++;
							const float Dist = FVector::DistSquared(FocusLoc, E->GetActorLocation());
							if (!Snap.bHasNearestCrew || Dist < FVector::DistSquared(FocusLoc, Snap.NearestCrewLocation))
							{
								Snap.bHasNearestCrew = true;
								Snap.NearestCrewLocation = E->GetActorLocation();
							}
						}
					}
				}
				for (const TWeakObjectPtr<ARenBossCharacter>& WeakR : Squad->GetRegisteredRivals())
				{
					if (const ARenBossCharacter* R = WeakR.Get())
					{
						const int32 Seg = R->GetDuelIndex();
						if (Seg >= 0 && Seg < 6)
						{
							Ctx.Sectors[Seg].bRivalAlive = !R->IsDead();
							Ctx.Sectors[Seg].RivalLocation = R->GetActorLocation();
							Ctx.Sectors[Seg].RivalName = R->GetBossName();
						}
					}
				}
				Missions->UpdateWorldContext(Ctx);
			}
		}
	}

	// Adaptive Music Update
	if (UKomorebiAudioSubsystem* Audio = World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UKomorebiAudioSubsystem>() : nullptr)
	{
		const UEnemySquadSubsystem* Squad = World->GetSubsystem<UEnemySquadSubsystem>();
		const UTrafficSubsystem* Traffic = World->GetSubsystem<UTrafficSubsystem>();
		const AKomorebiVehiclePawn* Driven = Traffic ? Traffic->GetDrivenVehicle() : nullptr;
		const int32 Chapter = KomorebiCore::ChapterAt(FocusThree.Z, ClearedSectors, bRoam);
		const bool bCombat = Squad && Squad->IsPlayerInActiveCombat();
		const bool bBoss = Squad && (Squad->GetActiveEngagedBoss() != nullptr);
		const float DriveSpeed = Driven ? Driven->GetTelemetry().SpeedMps : 0.0f;
		Audio->UpdateMusicStateDetailed(Chapter, bCombat ? 1.0f : 0.0f, bBoss, ActiveScene == EKomorebiWorldScene::Snowbound, bPaused, Driven != nullptr, DriveSpeed);
	}

	PushLiveHUDState(Kael, DeltaTime);
}

void UKomorebiWorldCoordinator::PushLiveHUDState(AKaelCharacter* Kael, float /*DeltaTime*/)
{
	AKomorebiHUD* HUD = GetLocalHUD();
	UWorld* World = GetWorld();
	if (!HUD || !World)
	{
		return;
	}

	FKomorebiHUDState State;
	State.bRoamMode = (ActiveMode == EKomorebiPlayMode::FreeRoam);
	State.bOnlineMode = (ActiveMode == EKomorebiPlayMode::OnlineCity);

	const FVector FocusLoc = Kael ? Kael->GetActorLocation() : FVector::ZeroVector;
	const KomorebiCore::Vec3 FocusThree = KomorebiBridge::FVectorToThree(FocusLoc);

	if (Kael && Kael->CombatComponent)
	{
		const UCombatComponent* C = Kael->CombatComponent;
		State.Hp = C->Hp;
		State.MaxHp = C->MaxHp;
		State.SparkPower = C->SparkPower;
		State.ClearedSectors = C->ClearedSectors;
		State.Cash = C->Cash;
		State.ComboCount = C->ComboCount;
		State.DashCooldown = C->GetDashCooldown();
		State.FlareCooldown = C->GetFlareCooldown();
		State.bGuarding = C->bGuarding;
		State.GuardMeter = C->GuardMeter;
		HUD->SetCombatDefenseDetails(C->GuardBreakTimer > 0.0f, C->CounterWindow > 0.0f, C->IsInvulnerable());

		if (AActor* FocusTarget = C->GetVisibleFocusTarget())
		{
			State.bHasFocusTarget = true;
			State.FocusTargetLocation = FocusTarget->GetActorLocation();
			if (const AStreetEnemyCharacter* E = Cast<AStreetEnemyCharacter>(FocusTarget))
			{
				State.FocusTargetName = KomorebiBridge::ToFString(KomorebiCore::EnemyKindToString(KomorebiBridge::ToCoreEnemyKind(E->EnemyKind)));
				State.FocusTargetHp = E->Hp;
				State.FocusTargetMaxHp = E->MaxHp;
				HUD->SetTargetDetails(true, E->WindupRemaining > 0.0f, FString::Printf(TEXT("%.0f / %.0f"), E->Hp, E->MaxHp), E->WindupRemaining > 0.0f ? TEXT("Incoming strike!") : TEXT(""));
			}
			else if (const ARenBossCharacter* R = Cast<ARenBossCharacter>(FocusTarget))
			{
				State.FocusTargetName = R->GetBossName();
				State.FocusTargetHp = R->GetHp();
				State.FocusTargetMaxHp = R->GetMaxHp();
				HUD->SetTargetDetails(true, R->GetDuelPhase() == ERenDuelPhase::Windup, FString::Printf(TEXT("%.0f / %.0f"), R->GetHp(), R->GetMaxHp()), R->GetCurrentMoveCue());
			}
		}
	}

	if (const UMissionSubsystem* Missions = World->GetSubsystem<UMissionSubsystem>())
	{
		Missions->PopulateHUDState(State);
	}

	if (const UTrafficSubsystem* Traffic = World->GetSubsystem<UTrafficSubsystem>())
	{
		Traffic->PopulateHUDState(State, FocusLoc);
	}

	if (const UEnemySquadSubsystem* Squad = World->GetSubsystem<UEnemySquadSubsystem>())
	{
		State.Heat = Squad->GetHeat();
		State.HeatStars = Squad->GetHeatStars();
		if (const ARenBossCharacter* Boss = Squad->GetActiveEngagedBoss())
		{
			State.bBossActive = true;
			State.BossName = Boss->GetBossName();
			State.BossHp = Boss->GetHp();
			State.BossMaxHp = Boss->GetMaxHp();
			State.BossCue = Boss->GetCurrentMoveCue();
			State.BossLesson = Boss->GetBossLesson();
			HUD->SetBossDuelDetails(Boss->GetDuelPhase() == ERenDuelPhase::Recovery, true, false, false);
		}
	}

	if (const ADayNightAtmosphereActor* Atmo = AtmosphereActor.Get())
	{
		State.bIsNight = Atmo->IsNightPhase();
		State.DayClockLabel = Atmo->GetClockLabel();
	}

	State.ActiveChapter = KomorebiCore::ChapterAt(FocusThree.Z, State.ClearedSectors, State.bRoamMode || State.bOnlineMode);
	if (ActiveScene == EKomorebiWorldScene::NeoKyoto && State.ActiveChapter != LastHUDChapter)
	{
		LastHUDChapter = State.ActiveChapter;
		HUD->ShowChapterBanner(State.ActiveChapter, State.bIsNight);
	}

	if (const UKomorebiAudioSubsystem* Audio = World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UKomorebiAudioSubsystem>() : nullptr)
	{
		HUD->SetCommsVoiceLevel(Audio->GetVoiceLevel());
	}

	HUD->UpdateHUDState(State);
}

// --- Delegate Implementations ---

void UKomorebiWorldCoordinator::HandleRideToggleRequested(AKaelCharacter* Kael)
{
	UWorld* World = GetWorld();
	if (!World || !Kael)
	{
		return;
	}
	if (UTrafficSubsystem* Traffic = World->GetSubsystem<UTrafficSubsystem>())
	{
		const bool bRoam = (ActiveMode != EKomorebiPlayMode::StorySectors);
		const int32 Cleared = Kael->CombatComponent ? Kael->CombatComponent->ClearedSectors : 0;
		const float Hp = Kael->CombatComponent ? Kael->CombatComponent->Hp : 100.0f;
		FString Feedback;
		Traffic->TryPlayerInteractVehicle(Kael, bRoam, Cleared, Hp, Feedback);
		if (!Feedback.IsEmpty())
		{
			if (AKomorebiHUD* HUD = GetLocalHUD())
			{
				HUD->EnqueueToast(Feedback);
			}
		}
		Kael->SetDrivingState(Traffic->GetDrivenVehicle() != nullptr, Traffic->GetDrivenVehicle());
	}
}

void UKomorebiWorldCoordinator::HandlePauseToggleRequested()
{
	if (UWorld* World = GetWorld())
	{
		if (AKomorebiPlayerController* PC = Cast<AKomorebiPlayerController>(World->GetFirstPlayerController()))
		{
			PC->TogglePauseMenu();
		}
	}
}

void UKomorebiWorldCoordinator::HandleCombatToast(const FString& Message)
{
	if (AKomorebiHUD* HUD = GetLocalHUD()) { HUD->EnqueueToast(Message); }
}

void UKomorebiWorldCoordinator::HandleCombatFlash(const FString& Text)
{
	if (AKomorebiHUD* HUD = GetLocalHUD()) { HUD->ShowFlashText(Text); }
}

void UKomorebiWorldCoordinator::HandleCombatAudioCue(FName CueName, float PitchOrScale)
{
	if (UWorld* World = GetWorld())
	{
		if (UKomorebiAudioSubsystem* Audio = World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UKomorebiAudioSubsystem>() : nullptr)
		{
			Audio->PlaySfxByName(CueName, PitchOrScale);
		}
	}
}

void UKomorebiWorldCoordinator::HandleCombatMissionStrike(FVector WorldLocation, float RadiusCm, float Damage, FName Source, int32 /*ClearedSegment*/)
{
	if (UWorld* World = GetWorld())
	{
		if (UMissionSubsystem* Missions = World->GetSubsystem<UMissionSubsystem>())
		{
			const float RadiusMeters = RadiusCm * 0.01f;
			if (Source == TEXT("spark"))
			{
				Missions->HitBolt(WorldLocation, RadiusMeters, Damage);
			}
			else
			{
				Missions->HitAt(WorldLocation, RadiusMeters, Damage, Source);
			}
		}
	}
}

void UKomorebiWorldCoordinator::HandleCombatPlayerDied(bool bEnteredOnlineDowned)
{
	if (bEnteredOnlineDowned)
	{
		return;
	}
	UWorld* World = GetWorld();
	AKaelCharacter* Kael = GetLocalKael();
	if (!World || !Kael)
	{
		return;
	}
	if (UTrafficSubsystem* Traffic = World->GetSubsystem<UTrafficSubsystem>())
	{
		if (AKomorebiVehiclePawn* Veh = Traffic->GetDrivenVehicle())
		{
			Veh->BailOut(TEXT("Player downed"));
		}
	}
	Kael->SetDrivingState(false, nullptr);
	if (AKomorebiGameMode* GM = World->GetAuthGameMode<AKomorebiGameMode>())
	{
		GM->RespawnPlayerAtCheckpoint(Kael->GetController());
	}
	if (UEnemySquadSubsystem* Squad = World->GetSubsystem<UEnemySquadSubsystem>())
	{
		Squad->ResetAfterPlayerDeath(Kael->GetActorLocation());
	}
}

void UKomorebiWorldCoordinator::HandleSquadSegmentCleared(int32 SegmentIndex, int32 TotalClearedCount, EKomorebiPower /*GrantedPower*/)
{
	if (UWorld* World = GetWorld())
	{
		if (UMissionSubsystem* Missions = World->GetSubsystem<UMissionSubsystem>())
		{
			Missions->TryAdvanceSectorClear(SegmentIndex, true, true);
		}
		if (AKaelCharacter* Kael = GetLocalKael())
		{
			if (Kael->CombatComponent)
			{
				Kael->CombatComponent->ClearedSectors = FMath::Max(Kael->CombatComponent->ClearedSectors, TotalClearedCount);
			}
		}
	}
}

void UKomorebiWorldCoordinator::HandleSquadToast(const FString& Message) { HandleCombatToast(Message); }
void UKomorebiWorldCoordinator::HandleSquadFlash(const FString& Text) { HandleCombatFlash(Text); }
void UKomorebiWorldCoordinator::HandleSquadAudioCue(FName CueName, float Scale) { HandleCombatAudioCue(CueName, Scale); }

void UKomorebiWorldCoordinator::HandleMissionToast(const FString& Message) { HandleCombatToast(Message); }
void UKomorebiWorldCoordinator::HandleMissionFlash(const FString& Text) { HandleCombatFlash(Text); }

void UKomorebiWorldCoordinator::HandleMissionCommsStarted(const FKomorebiCommsMessage& Message)
{
	if (AKomorebiHUD* HUD = GetLocalHUD())
	{
		HUD->ShowCommsMessage(Message);
	}
	if (UWorld* World = GetWorld())
	{
		if (UKomorebiAudioSubsystem* Audio = World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UKomorebiAudioSubsystem>() : nullptr)
		{
			Audio->SpeakRadioLine(Message.Line, Message.SpeakerId, true, true);
		}
	}
}

void UKomorebiWorldCoordinator::HandleMissionRewardCash(int32 Amount, const FString& Reason)
{
	if (AKaelCharacter* Kael = GetLocalKael())
	{
		if (Kael->CombatComponent)
		{
			Kael->CombatComponent->GrantReward(Amount, 0.0f, FString::Printf(TEXT("+$%d"), Amount), Reason);
		}
	}
}

void UKomorebiWorldCoordinator::HandleMissionHealPlayer(float HealAmount)
{
	if (AKaelCharacter* Kael = GetLocalKael())
	{
		if (Kael->CombatComponent)
		{
			Kael->CombatComponent->Heal(HealAmount);
		}
	}
}

void UKomorebiWorldCoordinator::HandleMissionReduceHeat(float HeatDelta)
{
	if (UWorld* World = GetWorld())
	{
		if (UEnemySquadSubsystem* Squad = World->GetSubsystem<UEnemySquadSubsystem>())
		{
			Squad->CoolHeat(HeatDelta);
		}
	}
}

void UKomorebiWorldCoordinator::HandleCheckpointActivated(int32 /*SectorIndex*/, FVector RespawnWorldLocation)
{
	if (UWorld* World = GetWorld())
	{
		if (AKomorebiGameMode* GM = World->GetAuthGameMode<AKomorebiGameMode>())
		{
			GM->SetActiveCheckpoint(RespawnWorldLocation);
		}
	}
}

void UKomorebiWorldCoordinator::HandleSectorGateOpened(int32 /*SectorIndex*/, FString UnlockedPowerName)
{
	if (!UnlockedPowerName.IsEmpty())
	{
		HandleCombatFlash(FString::Printf(TEXT("%s Unlocked"), *UnlockedPowerName));
	}
}

void UKomorebiWorldCoordinator::HandleTrafficCarjacked(AKomorebiVehiclePawn* /*Vehicle*/, APawn* Driver)
{
	if (AKaelCharacter* Kael = Cast<AKaelCharacter>(Driver))
	{
		if (Kael->CombatComponent)
		{
			Kael->CombatComponent->ApplyGuiltAndCarjack(true, false, false);
		}
	}
}

void UKomorebiWorldCoordinator::HandleTrafficToast(const FString& Message) { HandleCombatToast(Message); }
void UKomorebiWorldCoordinator::HandleTrafficFlash(const FString& Text) { HandleCombatFlash(Text); }

void UKomorebiWorldCoordinator::HandleVehicleRamPawn(AKomorebiVehiclePawn* Vehicle, APawn* HitPawn, float SpeedMps, FVector ImpactDirectionUE, float /*SuggestedDamage*/)
{
	if (AStreetEnemyCharacter* Enemy = Cast<AStreetEnemyCharacter>(HitPawn))
	{
		const bool bLiable = Vehicle && (Vehicle->IsPlayerDriven() || Vehicle->GetSimState().Liable > 0.0);
		const float MassScale = Vehicle ? static_cast<float>(Vehicle->GetVehicleSpec().Mass / 1200.0) : 1.0f;
		Enemy->ReceiveRamImpact(SpeedMps * MassScale, ImpactDirectionUE, bLiable);
	}
}

void UKomorebiWorldCoordinator::HandleVehicleCrashDamage(AKomorebiVehiclePawn* /*Vehicle*/, float DamageAmount)
{
	if (AKaelCharacter* Kael = GetLocalKael())
	{
		if (Kael->CombatComponent)
		{
			Kael->CombatComponent->ApplyVehicleCrashDamage(DamageAmount, 0.16f);
		}
	}
}

void UKomorebiWorldCoordinator::HandleVehicleCameraJolt(AKomorebiVehiclePawn* /*Vehicle*/, float Intensity)
{
	if (AKaelCharacter* Kael = GetLocalKael())
	{
		if (Kael->CombatComponent)
		{
			Kael->CombatComponent->AddCameraKick(Intensity);
		}
	}
}

void UKomorebiWorldCoordinator::HandleShowcaseStateChanged(bool bTourActive)
{
	if (AKomorebiHUD* HUD = GetLocalHUD())
	{
		HUD->SetShowcaseActive(bTourActive);
	}
	if (!bTourActive)
	{
		if (AKaelCharacter* Kael = GetLocalKael())
		{
			if (Kael->FollowCamera)
			{
				Kael->FollowCamera->ClearFreeView();
			}
		}
	}
}

void UKomorebiWorldCoordinator::HandleShowcaseShotChanged(const FKomorebiShowcaseShotMeta& ShotMeta)
{
	if (AKomorebiHUD* HUD = GetLocalHUD())
	{
		HUD->UpdateShowcaseState(ShotMeta.ShotIndex, ShotMeta.Progress01);
	}
}

void UKomorebiWorldCoordinator::HandleShowcaseProgressUpdated(int32 ShotIndex, float Progress01)
{
	if (AKomorebiHUD* HUD = GetLocalHUD())
	{
		HUD->UpdateShowcaseState(ShotIndex, Progress01);
	}
}

void UKomorebiWorldCoordinator::HandleShowcaseDayPhasePreview(bool bOverrideActive, bool bPreviewNight)
{
	if (ADayNightAtmosphereActor* Atmo = AtmosphereActor.Get())
	{
		if (bOverrideActive)
		{
			Atmo->SetPreviewPhase(bPreviewNight ? KomorebiCore::EDayPhase::Night : KomorebiCore::EDayPhase::Day);
		}
		else
		{
			Atmo->SetPreviewPhase(TOptional<KomorebiCore::EDayPhase>());
		}
	}
}

void UKomorebiWorldCoordinator::HandleDayNightPhaseShifted(bool bIsNowNight)
{
	if (AKomorebiHUD* HUD = GetLocalHUD())
	{
		HUD->EnqueueToast(bIsNowNight ? TEXT("Night falls — 10 min until dawn") : TEXT("Dawn breaks — 10 min until nightfall"));
		HUD->ShowFlashText(bIsNowNight ? TEXT("Nightfall") : TEXT("Daybreak"));
	}
}

AKomorebiHUD* UKomorebiWorldCoordinator::GetLocalHUD() const
{
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			return Cast<AKomorebiHUD>(PC->GetHUD());
		}
	}
	return nullptr;
}

AKaelCharacter* UKomorebiWorldCoordinator::GetLocalKael() const
{
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (AKaelCharacter* Direct = Cast<AKaelCharacter>(PC->GetPawn()))
			{
				return Direct;
			}
		}
	}
	return BoundKael.Get();
}
