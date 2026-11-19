// Copyright Komorebi Project. Solo Story Sectors and Free Roam GameMode enforcing spawn/checkpoint respawns and world limits.
#include "Network/KomorebiGameMode.h"

#include "Network/KomorebiGameInstance.h"
#include "Network/KomorebiGameState.h"
#include "Network/KomorebiPlayerState.h"
#include "Network/KomorebiPlayerController.h"
#include "Character/KaelCharacter.h"
#include "UI/KomorebiHUD.h"
#include "World/CoordBridge.h"
#include "Game/LevelsData.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

AKomorebiGameMode::AKomorebiGameMode()
{
	PrimaryActorTick.bCanEverTick = true;

	PlayerControllerClass = AKomorebiPlayerController::StaticClass();
	HUDClass = AKomorebiHUD::StaticClass();
	GameStateClass = AKomorebiGameState::StaticClass();
	PlayerStateClass = AKomorebiPlayerState::StaticClass();
	DefaultPawnClass = AKaelCharacter::StaticClass();
}

void AKomorebiGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	DetectSceneFromMapName(MapName);

	const FString ModeOpt = UGameplayStatics::ParseOption(Options, TEXT("mode"));
	if (ModeOpt.Equals(TEXT("roam"), ESearchCase::IgnoreCase) && ActiveScene == EKomorebiWorldScene::NeoKyoto)
	{
		ActivePlayMode = EKomorebiPlayMode::FreeRoam;
	}
	else if (ModeOpt.Equals(TEXT("online"), ESearchCase::IgnoreCase))
	{
		ActivePlayMode = EKomorebiPlayMode::OnlineCity;
	}
	else if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
	{
		ActivePlayMode = GI->SelectedPlayMode;
		if (ActiveScene == EKomorebiWorldScene::Snowbound && ActivePlayMode == EKomorebiPlayMode::FreeRoam)
		{
			ActivePlayMode = EKomorebiPlayMode::StorySectors;
		}
		ClearedSectors = GI->bIgnoreSavedCheckpoint ? 0 : FMath::Clamp(GI->CachedClearedSectors, 0, 6);
	}

	PrepareTerrainMap();

	const FTransform StartTransform = GetStartSpawnTransform();
	ActiveCheckpointLocation = StartTransform.GetLocation();
	ActiveCheckpointRotation = StartTransform.Rotator();

	if (ActiveScene == EKomorebiWorldScene::NeoKyoto && ActivePlayMode == EKomorebiPlayMode::StorySectors && ClearedSectors > 0)
	{
		SetClearedSectors(ClearedSectors);
	}
}

void AKomorebiGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (AKomorebiGameState* GS = GetGameState<AKomorebiGameState>())
	{
		GS->ActiveWorldScene = ActiveScene;
		GS->ActivePlayMode = ActivePlayMode;
	}

	if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
	{
		GI->SelectedScene = ActiveScene;
		GI->SelectedPlayMode = ActivePlayMode;
	}
}

void AKomorebiGameMode::DetectSceneFromMapName(const FString& MapName)
{
	if (MapName.Contains(TEXT("Snowbound"), ESearchCase::IgnoreCase) ||
		MapName.Contains(TEXT("Aurora"), ESearchCase::IgnoreCase))
	{
		ActiveScene = EKomorebiWorldScene::Snowbound;
	}
	else
	{
		ActiveScene = EKomorebiWorldScene::NeoKyoto;
	}
}

void AKomorebiGameMode::PrepareTerrainMap()
{
	if (ActiveScene == EKomorebiWorldScene::Snowbound)
	{
		if (!SnowboundTerrain.IsPrepared())
		{
			SnowboundTerrain.Prepare();
		}
	}
	else
	{
		if (!NeoKyotoTerrain.IsPrepared())
		{
			NeoKyotoTerrain.Prepare();
		}
	}
}

float AKomorebiGameMode::SampleTerrainHeightUE(float WorldXCm, float WorldYCm) const
{
	const KomorebiCore::Vec3 ThreeMeters = KomorebiCore::UEToThree(KomorebiCore::Vec3{ WorldXCm, WorldYCm, 0.0 });
	double HeightMeters = 0.0;

	if (ActiveScene == EKomorebiWorldScene::Snowbound)
	{
		HeightMeters = SnowboundTerrain.IsPrepared()
			? SnowboundTerrain.HeightAt(ThreeMeters.X, ThreeMeters.Z)
			: KomorebiCore::SnowboundTerrainMap::RawHeight(ThreeMeters.X, ThreeMeters.Z);
	}
	else
	{
		if (NeoKyotoTerrain.IsPrepared())
		{
			HeightMeters = NeoKyotoTerrain.HeightAt(ThreeMeters.X, ThreeMeters.Z);
		}
		else
		{
			const double WayD = KomorebiCore::WayDist(ThreeMeters.X, ThreeMeters.Z);
			HeightMeters = KomorebiCore::NeoKyotoRawHeight(ThreeMeters.X, ThreeMeters.Z, WayD);
		}
	}

	return static_cast<float>(HeightMeters * KomorebiCore::kMetersToCm);
}

FTransform AKomorebiGameMode::GetStartSpawnTransform() const
{
	const KomorebiCore::Vec2 StartXZ = (ActiveScene == EKomorebiWorldScene::Snowbound)
		? KomorebiCore::SnowStart()
		: KomorebiCore::Start();

	const FVector GroundPos = KomorebiBridge::ThreeXZToFVector(StartXZ.X, 0.0, StartXZ.Z);
	const float GroundZ = SampleTerrainHeightUE(GroundPos.X, GroundPos.Y);
	const FVector SpawnLoc(GroundPos.X, GroundPos.Y, GroundZ + 96.0f);

	// Three.js yaw = PI (facing -Z / north) maps to UE yaw = 0 deg (+X / north).
	const float SpawnYawDeg = static_cast<float>(KomorebiCore::ThreeYawToUEYawDeg(KomorebiCore::kPi));
	return FTransform(FRotator(0.0f, SpawnYawDeg, 0.0f), SpawnLoc);
}

UClass* AKomorebiGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	if (UPackage* GamePackage = FindPackage(nullptr, TEXT("/Script/KomorebiGame")))
	{
		if (UClass* KaelClass = FindObject<UClass>(GamePackage, TEXT("KaelCharacter")))
		{
			if (KaelClass->IsChildOf(APawn::StaticClass()))
			{
				return KaelClass;
			}
		}
	}
	return Super::GetDefaultPawnClassForController_Implementation(InController);
}

void AKomorebiGameMode::RestartPlayer(AController* NewPlayer)
{
	if (!NewPlayer || NewPlayer->IsPendingKillPending())
	{
		return;
	}

	const FTransform SpawnTransform(ActiveCheckpointRotation, ActiveCheckpointLocation);
	RestartPlayerAtTransform(NewPlayer, SpawnTransform);
}

void AKomorebiGameMode::SetActiveCheckpoint(const FVector& WorldLocation)
{
	const float GroundZ = SampleTerrainHeightUE(WorldLocation.X, WorldLocation.Y);
	ActiveCheckpointLocation = FVector(WorldLocation.X, WorldLocation.Y, FMath::Max(WorldLocation.Z, GroundZ + 96.0f));
}

void AKomorebiGameMode::SetClearedSectors(int32 ClearedCount)
{
	const auto& Levels = KomorebiCore::GetLevels();
	ClearedSectors = FMath::Clamp(ClearedCount, 0, static_cast<int32>(Levels.size()));

	if (ActiveScene == EKomorebiWorldScene::NeoKyoto && !Levels.empty())
	{
		const int32 CpIdx = FMath::Clamp(ClearedSectors, 0, static_cast<int32>(Levels.size()) - 1);
		const KomorebiCore::NamedPoint& Cp = Levels[static_cast<size_t>(CpIdx)].Checkpoint;
		const FVector Pos = KomorebiBridge::ThreeXZToFVector(Cp.X, 0.0, Cp.Z);
		SetActiveCheckpoint(Pos);
	}
}

void AKomorebiGameMode::RespawnPlayerAtCheckpoint(AController* Controller)
{
	if (!Controller)
	{
		return;
	}

	// If driving a vehicle, exit cleanly before teleporting on-foot character.
	if (AKomorebiPlayerController* KPC = Cast<AKomorebiPlayerController>(Controller))
	{
		if (AKomorebiPlayerState* PS = KPC->GetPlayerState<AKomorebiPlayerState>())
		{
			if (PS->bIsDrivingVehicle)
			{
				KPC->ServerRequestVehiclePossess(nullptr, false);
			}
			PS->PlayerHealth = static_cast<float>(KomorebiCore::kMaxPlayerHp);
		}
	}

	APawn* Pawn = Controller->GetPawn();
	if (!Pawn)
	{
		RestartPlayer(Controller);
		return;
	}

	const float GroundZ = SampleTerrainHeightUE(ActiveCheckpointLocation.X, ActiveCheckpointLocation.Y);
	const FVector SafeLoc(ActiveCheckpointLocation.X, ActiveCheckpointLocation.Y, GroundZ + 96.0f);
	Pawn->SetActorLocationAndRotation(SafeLoc, ActiveCheckpointRotation, false, nullptr, ETeleportType::TeleportPhysics);

	if (ACharacter* Char = Cast<ACharacter>(Pawn))
	{
		if (UCharacterMovementComponent* MoveComp = Char->GetCharacterMovement())
		{
			MoveComp->StopMovementImmediately();
		}
	}
}

void AKomorebiGameMode::ToggleStoryAndRoamMode(AController* Requestor)
{
	if (ActivePlayMode == EKomorebiPlayMode::OnlineCity || ActiveScene != EKomorebiWorldScene::NeoKyoto)
	{
		return;
	}

	ActivePlayMode = (ActivePlayMode == EKomorebiPlayMode::FreeRoam)
		? EKomorebiPlayMode::StorySectors
		: EKomorebiPlayMode::FreeRoam;

	if (AKomorebiGameState* GS = GetGameState<AKomorebiGameState>())
	{
		GS->ActivePlayMode = ActivePlayMode;
	}

	if (UKomorebiGameInstance* GI = Cast<UKomorebiGameInstance>(GetGameInstance()))
	{
		GI->SelectedPlayMode = ActivePlayMode;
	}

	if (AKomorebiPlayerController* KPC = Cast<AKomorebiPlayerController>(Requestor))
	{
		KPC->ClientShowToast(ActivePlayMode == EKomorebiPlayMode::FreeRoam
			? TEXT("Free Roam active · all powers unlocked, gates open")
			: TEXT("Story Sectors active · progression restored"));
	}
}

void AKomorebiGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	EnforcePlayerWorldBounds();
}

void AKomorebiGameMode::EnforcePlayerWorldBounds()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double ExtentMeters = (ActiveScene == EKomorebiWorldScene::Snowbound)
		? KomorebiCore::kSnowExtent
		: KomorebiCore::kExtent;
	const float LimitCm = static_cast<float>((ExtentMeters - 16.0) * KomorebiCore::kMetersToCm);
	const float SafeClampCm = static_cast<float>((ExtentMeters - 17.0) * KomorebiCore::kMetersToCm);
	const double Now = World->GetTimeSeconds();

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!Pawn)
		{
			continue;
		}

		// Skip vehicle boundary teleport; vehicle physics handles perimeter walls.
		if (const AKomorebiPlayerState* PS = PC->GetPlayerState<AKomorebiPlayerState>())
		{
			if (PS->bIsDrivingVehicle)
			{
				continue;
			}
		}

		const FVector Loc = Pawn->GetActorLocation();
		const float GroundZ = SampleTerrainHeightUE(Loc.X, Loc.Y);

		if (Loc.Z < GroundZ - 600.0f)
		{
			RespawnPlayerAtCheckpoint(PC);
			continue;
		}

		if (FMath::Abs(Loc.X) > LimitCm || FMath::Abs(Loc.Y) > LimitCm)
		{
			const float ClampedX = FMath::Clamp(Loc.X, -SafeClampCm, SafeClampCm);
			const float ClampedY = FMath::Clamp(Loc.Y, -SafeClampCm, SafeClampCm);
			const float ClampedZ = SampleTerrainHeightUE(ClampedX, ClampedY) + 96.0f;
			Pawn->SetActorLocation(FVector(ClampedX, ClampedY, ClampedZ), false, nullptr, ETeleportType::TeleportPhysics);

			double& LastFlash = LastLimitFlashTime.FindOrAdd(PC);
			if (Now - LastFlash > 1.5)
			{
				LastFlash = Now;
				if (AKomorebiPlayerController* KPC = Cast<AKomorebiPlayerController>(PC))
				{
					KPC->ClientShowFlashText(ActiveScene == EKomorebiWorldScene::Snowbound ? TEXT("Ridge limits") : TEXT("City limits"));
				}
			}
		}
	}
}
