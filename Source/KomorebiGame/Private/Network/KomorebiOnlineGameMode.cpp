// Copyright Komorebi Project. 8-Player Online City GameMode for Listen Server and Dedicated Server (zero Node.js).
#include "Network/KomorebiOnlineGameMode.h"

#include "Network/KomorebiGameInstance.h"
#include "Network/KomorebiGameState.h"
#include "Network/KomorebiPlayerState.h"
#include "Network/KomorebiPlayerController.h"
#include "Network/OnlineActivitiesActor.h"
#include "World/CoordBridge.h"
#include "World/NeoKyotoTerrain.h"
#include "Game/LevelsData.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

AKomorebiOnlineGameMode::AKomorebiOnlineGameMode()
{
	ActivePlayMode = EKomorebiPlayMode::OnlineCity;
}

void AKomorebiOnlineGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	ActivePlayMode = EKomorebiPlayMode::OnlineCity;

	const FString RoomOpt = UGameplayStatics::ParseOption(Options, TEXT("room"));
	const std::string CleanRoom = KomorebiCore::SanitizeRoomName(TCHAR_TO_UTF8(*RoomOpt));
	RoomCode = KomorebiBridge::ToFString(CleanRoom);
	if (RoomCode.IsEmpty())
	{
		if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
		{
			RoomCode = GI->PendingRoomCode;
			bPrivateRoom = GI->bPendingPrivateRoom;
		}
	}
	else
	{
		bPrivateRoom = UGameplayStatics::GetIntOption(Options, TEXT("private"), 0) != 0;
	}
}

void AKomorebiOnlineGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (AKomorebiGameState* GS = GetGameState<AKomorebiGameState>())
	{
		GS->ActivePlayMode = EKomorebiPlayMode::OnlineCity;
		GS->ActiveWorldScene = ActiveScene;
		GS->ActiveRoomCode = RoomCode;
		GS->bIsPrivateRoom = bPrivateRoom;
	}

	// Neo-Kyoto hosts the 12-gate Street Race, PvP Duel Ring, District Contracts, and Siege Mech boss events.
	// Snowbound online keeps crews, revives, and titles in its own pool ("aurora").
	if (ActiveScene == EKomorebiWorldScene::NeoKyoto)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		OnlineActivitiesActor = GetWorld()->SpawnActor<AOnlineActivitiesActor>(
			AOnlineActivitiesActor::StaticClass(),
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			SpawnParams);

		InitializeDistrictContract(0);
		SiegeMechCooldown = static_cast<float>(KomorebiCore::kMechEventConfig.First);
	}

	TickSyncedDayNight();
}

void AKomorebiOnlineGameMode::PreLogin(
	const FString& Options,
	const FString& Address,
	const FUniqueNetIdRepl& UniqueId,
	FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (!ErrorMessage.IsEmpty())
	{
		return;
	}

	// Enforce 8-player room cap (matching KomorebiCore::kMaxOnlinePlayers and server/room.mjs).
	if (GetNumPlayers() >= KomorebiCore::kMaxOnlinePlayers)
	{
		ErrorMessage = FString::Printf(
			TEXT("Room is full (%d/%d players) — try Quick Match"),
			KomorebiCore::kMaxOnlinePlayers,
			KomorebiCore::kMaxOnlinePlayers);
		return;
	}

	// Keep Neo-Kyoto ("autumn") and Snowbound ("aurora") rooms strictly separated by pool.
	const FString RequestedPool = UGameplayStatics::ParseOption(Options, TEXT("pool"));
	if (!RequestedPool.IsEmpty())
	{
		const FString ExpectedPool = UKomorebiGameInstance::GetPoolNameForScene(ActiveScene);
		if (!RequestedPool.Equals(ExpectedPool, ESearchCase::IgnoreCase))
		{
			ErrorMessage = FString::Printf(TEXT("World pool mismatch (server is %s)"), *ExpectedPool);
			return;
		}
	}
}

void AKomorebiOnlineGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	if (!NewPlayer)
	{
		return;
	}

	if (AKomorebiPlayerState* PS = NewPlayer->GetPlayerState<AKomorebiPlayerState>())
	{
		PS->DisplayName = AKomorebiPlayerState::SanitizeDisplayName(PS->GetPlayerName());
		PS->PlayerHealth = static_cast<float>(KomorebiCore::kMaxPlayerHp);

		if (AKomorebiGameState* GS = GetGameState<AKomorebiGameState>())
		{
			GS->MulticastAnnounceEvent(FString::Printf(TEXT("%s joined"), *PS->DisplayName));
		}
	}
}

void AKomorebiOnlineGameMode::Logout(AController* Exiting)
{
	if (Exiting)
	{
		if (AKomorebiPlayerState* PS = Exiting->GetPlayerState<AKomorebiPlayerState>())
		{
			MechContributorHitTimes.Remove(PS);
			if (AKomorebiGameState* GS = GetGameState<AKomorebiGameState>())
			{
				GS->MulticastAnnounceEvent(FString::Printf(TEXT("%s left"), *PS->DisplayName));
			}
		}
	}

	Super::Logout(Exiting);
}

void AKomorebiOnlineGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Online City NEVER slows or pauses simulation time.
	if (UWorld* World = GetWorld())
	{
		if (AWorldSettings* WS = World->GetWorldSettings())
		{
			if (!FMath::IsNearlyEqual(WS->TimeDilation, 1.0f, 0.001f))
			{
				WS->SetTimeDilation(1.0f);
			}
		}
	}

	TickSyncedDayNight();
	TickDownedBleedouts(DeltaSeconds);

	if (ActiveScene == EKomorebiWorldScene::NeoKyoto)
	{
		TickSiegeMechTimer(DeltaSeconds);
	}
}

void AKomorebiOnlineGameMode::TickSyncedDayNight()
{
	AKomorebiGameState* GS = GetGameState<AKomorebiGameState>();
	if (!GS)
	{
		return;
	}

	if (ActiveScene == EKomorebiWorldScene::Snowbound)
	{
		GS->SyncedOnlineNightFactor = 1.0f;
		GS->bSyncedOnlineIsNight = true;
		GS->SyncedOnlineClockMMSS = TEXT("00:00");
		return;
	}

	const KomorebiCore::DayNightState& DN = OnlineDayNight.Update(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	GS->SyncedOnlineNightFactor = static_cast<float>(DN.Night);
	GS->bSyncedOnlineIsNight = (DN.Phase == KomorebiCore::EDayPhase::Night);
	GS->SyncedOnlineClockMMSS = KomorebiBridge::ToFString(DN.ClockMMSS);
}

void AKomorebiOnlineGameMode::InitializeDistrictContract(int32 SectorIndex)
{
	AKomorebiGameState* GS = GetGameState<AKomorebiGameState>();
	const auto& Levels = KomorebiCore::GetLevels();
	if (!GS || Levels.empty())
	{
		return;
	}

	const int32 WrappedSector = ((SectorIndex % static_cast<int32>(Levels.size())) + static_cast<int32>(Levels.size())) % static_cast<int32>(Levels.size());
	GS->ActiveContractSector = WrappedSector;
	GS->ActiveContractKills = 0;
	GS->ActiveContractTargetCount = static_cast<int32>(Levels[static_cast<size_t>(WrappedSector)].Spots.size());
}

void AKomorebiOnlineGameMode::NotifyCrewMemberKilled(int32 SectorIndex)
{
	AKomorebiGameState* GS = GetGameState<AKomorebiGameState>();
	if (!GS || ActiveScene != EKomorebiWorldScene::NeoKyoto)
	{
		return;
	}

	if (SectorIndex != GS->ActiveContractSector)
	{
		return;
	}

	++GS->ActiveContractKills;
	if (GS->ActiveContractTargetCount > 0 && GS->ActiveContractKills >= GS->ActiveContractTargetCount)
	{
		RewardRoomContract(GS->ActiveContractSector);
		const int32 NextSector = (GS->ActiveContractSector + 1) % FMath::Max(1, static_cast<int32>(KomorebiCore::GetLevels().size()));
		InitializeDistrictContract(NextSector);
	}
}

void AKomorebiOnlineGameMode::RewardRoomContract(int32 SectorIndex)
{
	AKomorebiGameState* GS = GetGameState<AKomorebiGameState>();
	if (!GS)
	{
		return;
	}

	constexpr int32 ContractPay = 150;
	for (APlayerState* BasePS : GS->PlayerArray)
	{
		if (AKomorebiPlayerState* PS = Cast<AKomorebiPlayerState>(BasePS))
		{
			PS->AddOnlineCash(ContractPay);
			PS->IncrementOnlineStat(TEXT("contracts"));
		}
	}

	GS->MulticastContractCompleted(SectorIndex, ContractPay);
}

void AKomorebiOnlineGameMode::TickSiegeMechTimer(float DeltaSeconds)
{
	AKomorebiGameState* GS = GetGameState<AKomorebiGameState>();
	if (!GS || GS->bSiegeMechActive || GS->PlayerArray.Num() == 0)
	{
		return;
	}

	SiegeMechCooldown -= DeltaSeconds;
	if (SiegeMechCooldown <= 0.0f)
	{
		SiegeMechCooldown = static_cast<float>(KomorebiCore::kMechEventConfig.Every);
		SummonSiegeMechWorldBoss();
	}
}

void AKomorebiOnlineGameMode::SummonSiegeMechWorldBoss()
{
	AKomorebiGameState* GS = GetGameState<AKomorebiGameState>();
	const std::vector<KomorebiCore::Vec2>& Way = KomorebiCore::WaySamples();
	if (!GS || Way.empty())
	{
		return;
	}

	// Pick a random connected player and spawn the Siege Mech 10 way-samples ahead (matching net.js callMech).
	TArray<FVector> ActivePawnLocations;
	for (APlayerState* BasePS : GS->PlayerArray)
	{
		if (APawn* Pawn = BasePS ? BasePS->GetPawn() : nullptr)
		{
			ActivePawnLocations.Add(Pawn->GetActorLocation());
		}
	}

	KomorebiCore::Vec2 ChosenPoint = Way[Way.size() / 2];
	if (ActivePawnLocations.Num() > 0)
	{
		const FVector PickedUE = ActivePawnLocations[FMath::RandRange(0, ActivePawnLocations.Num() - 1)];
		const KomorebiCore::Vec3 PickedThree = KomorebiBridge::FVectorToThree(PickedUE);

		size_t ClosestIdx = 0;
		double BestDistSq = TNumericLimits<double>::Max();
		for (size_t i = 0; i < Way.size(); ++i)
		{
			const double Dx = Way[i].X - PickedThree.X;
			const double Dz = Way[i].Z - PickedThree.Z;
			const double D2 = Dx * Dx + Dz * Dz;
			if (D2 < BestDistSq)
			{
				BestDistSq = D2;
				ClosestIdx = i;
			}
		}

		const size_t TargetIdx = FMath::Min<size_t>(Way.size() - 1, ClosestIdx + 10);
		ChosenPoint = Way[TargetIdx];
	}

	const FVector HorizontalUE = KomorebiBridge::ThreeXZToFVector(ChosenPoint.X, 0.0, ChosenPoint.Z);
	const float GroundZ = SampleTerrainHeightUE(HorizontalUE.X, HorizontalUE.Y);
	const FVector SpawnWorldLoc(HorizontalUE.X, HorizontalUE.Y, GroundZ);
	const FString DistrictName = ResolveNearestDistrictName(ChosenPoint.X, ChosenPoint.Z);

	MechContributorHitTimes.Reset();
	GS->bSiegeMechActive = true;
	GS->SiegeMechLocation = SpawnWorldLoc;
	GS->SiegeMechHealthRatio = 1.0f;

	OnServerSummonSiegeMech.Broadcast(SpawnWorldLoc, DistrictName);
	GS->MulticastSiegeMechSpawned(DistrictName, SpawnWorldLoc);
}

void AKomorebiOnlineGameMode::RegisterSiegeMechContributor(AKomorebiPlayerState* Contributor)
{
	if (!Contributor || !GetWorld())
	{
		return;
	}
	MechContributorHitTimes.Add(Contributor, GetWorld()->GetTimeSeconds());
}

void AKomorebiOnlineGameMode::UpdateSiegeMechTelemetry(const FVector& WorldLocation, float HealthRatio)
{
	if (AKomorebiGameState* GS = GetGameState<AKomorebiGameState>())
	{
		GS->SiegeMechLocation = WorldLocation;
		GS->SiegeMechHealthRatio = FMath::Clamp(HealthRatio, 0.0f, 1.0f);
	}
}

void AKomorebiOnlineGameMode::NotifySiegeMechDefeated()
{
	AKomorebiGameState* GS = GetGameState<AKomorebiGameState>();
	UWorld* World = GetWorld();
	if (!GS || !World || !GS->bSiegeMechActive)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	const int32 MechPay = KomorebiCore::kMechEventConfig.Pay; // $200

	for (const TPair<TWeakObjectPtr<AKomorebiPlayerState>, double>& Entry : MechContributorHitTimes)
	{
		AKomorebiPlayerState* PS = Entry.Key.Get();
		if (PS && (Now - Entry.Value) <= 10.0)
		{
			PS->AddOnlineCash(MechPay);
			PS->IncrementOnlineStat(TEXT("mechs"));
			if (AKomorebiPlayerController* PC = Cast<AKomorebiPlayerController>(PS->GetOwner()))
			{
				PC->ClientShowToast(FString::Printf(TEXT("Siege mech down · +$%d"), MechPay));
			}
		}
	}

	MechContributorHitTimes.Reset();
	GS->bSiegeMechActive = false;
	GS->SiegeMechHealthRatio = 0.0f;
	SiegeMechCooldown = static_cast<float>(KomorebiCore::kMechEventConfig.Every);
}

void AKomorebiOnlineGameMode::TickDownedBleedouts(float DeltaSeconds)
{
	AKomorebiGameState* GS = GetGameState<AKomorebiGameState>();
	if (!GS)
	{
		return;
	}

	for (APlayerState* BasePS : GS->PlayerArray)
	{
		AKomorebiPlayerState* PS = Cast<AKomorebiPlayerState>(BasePS);
		if (!PS || !PS->bIsDowned)
		{
			continue;
		}

		// Pause bleedout while a teammate is actively reviving them.
		if (PS->ReviveProgress <= 0.0f)
		{
			PS->DownedTimeRemaining = FMath::Max(0.0f, PS->DownedTimeRemaining - DeltaSeconds);
		}

		if (PS->DownedTimeRemaining <= 0.0f)
		{
			PS->ResetAfterBleedout();
			if (AController* OwnerCtrl = Cast<AController>(PS->GetOwner()))
			{
				RespawnPlayerAtCheckpoint(OwnerCtrl);
				if (AKomorebiPlayerController* KPC = Cast<AKomorebiPlayerController>(OwnerCtrl))
				{
					KPC->ClientShowToast(TEXT("Respawned at sector relay"));
				}
			}
		}
	}
}

FString AKomorebiOnlineGameMode::ResolveNearestDistrictName(double ThreeX, double ThreeZ) const
{
	const auto& Levels = KomorebiCore::GetLevels();
	if (Levels.empty())
	{
		return TEXT("Neo-Kyoto");
	}

	const KomorebiCore::LevelSpec* Best = &Levels[0];
	double BestDistSq = TNumericLimits<double>::Max();
	for (const KomorebiCore::LevelSpec& Lvl : Levels)
	{
		const double Dx = Lvl.Checkpoint.X - ThreeX;
		const double Dz = Lvl.Checkpoint.Z - ThreeZ;
		const double D2 = Dx * Dx + Dz * Dz;
		if (D2 < BestDistSq)
		{
			BestDistSq = D2;
			Best = &Lvl;
		}
	}
	return KomorebiBridge::ToFString(Best->Name);
}
