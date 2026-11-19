// Copyright Komorebi Project. World subsystem orchestrating KomorebiCore::ComputeCrewOrder(), Heat & 3-Star Police/Roadblock escalation, witness mob aggro, segment clears, and respawns.
#include "AI/EnemySquadSubsystem.h"

#include "AI/RenBossCharacter.h"
#include "AI/StreetEnemyCharacter.h"
#include "Character/KaelCharacter.h"
#include "Combat/CombatComponent.h"
#include "CollisionQueryParams.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Game/CombatRules.h"
#include "Game/DuelStateMachine.h"
#include "Game/LevelsData.h"
#include "Game/StreetConfig.h"
#include "Game/TacticsRules.h"
#include "GameFramework/PlayerController.h"
#include "ProceduralMeshComponent.h"
#include "World/CoordBridge.h"
#include "World/NeoKyotoTerrain.h"
#include "World/WorldGrid.h"

#include <vector>

bool UEnemySquadSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

void UEnemySquadSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	StreetEnemyClass = AStreetEnemyCharacter::StaticClass();
	RenBossClass = ARenBossCharacter::StaticClass();
}

void UEnemySquadSubsystem::Deinitialize()
{
	DestroyRoadblock();
	RegisteredEnemies.Reset();
	RegisteredRivals.Reset();
	SpawnedPolice.Reset();
	RelayWorldLocations.Reset();
	Super::Deinitialize();
}

TStatId UEnemySquadSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UEnemySquadSubsystem, STATGROUP_Tickables);
}

void UEnemySquadSubsystem::ConfigureSession(EKomorebiWorldScene InScene, EKomorebiPlayMode InMode, int32 InSavedClearedCount)
{
	WorldScene = InScene;
	PlayMode = InMode;
	LastKnownClearedCount = FMath::Max(0, InSavedClearedCount);
	Heat = 0.0f;
	HeatStars = 0;
	bHeatIntroSpoken = false;
	DestroyRoadblock();
}

void UEnemySquadSubsystem::SetSectorMissionDonePredicate(TFunction<bool(int32)> InPredicate)
{
	SectorMissionDonePredicate = MoveTemp(InPredicate);
}

void UEnemySquadSubsystem::RegisterEnemy(AStreetEnemyCharacter* Enemy)
{
	if (Enemy)
	{
		RegisteredEnemies.AddUnique(TWeakObjectPtr<AStreetEnemyCharacter>(Enemy));
	}
}

void UEnemySquadSubsystem::UnregisterEnemy(AStreetEnemyCharacter* Enemy)
{
	if (Enemy)
	{
		const TWeakObjectPtr<AStreetEnemyCharacter> WeakPtr(Enemy);
		RegisteredEnemies.Remove(WeakPtr);
		SpawnedPolice.Remove(WeakPtr);
	}
}

void UEnemySquadSubsystem::RegisterRival(ARenBossCharacter* Rival)
{
	if (Rival)
	{
		RegisteredRivals.AddUnique(TWeakObjectPtr<ARenBossCharacter>(Rival));
	}
}

void UEnemySquadSubsystem::UnregisterRival(ARenBossCharacter* Rival)
{
	if (Rival)
	{
		RegisteredRivals.Remove(TWeakObjectPtr<ARenBossCharacter>(Rival));
	}
}

void UEnemySquadSubsystem::RegisterRelayLocation(const FVector& WorldLocation)
{
	RelayWorldLocations.Add(WorldLocation);
}

void UEnemySquadSubsystem::ClearRelayLocations()
{
	RelayWorldLocations.Reset();
}

void UEnemySquadSubsystem::EmitToast(const FString& Msg)
{
	if (!Msg.IsEmpty())
	{
		OnToast.Broadcast(Msg);
	}
}

void UEnemySquadSubsystem::EmitFlashText(const FString& Text)
{
	if (!Text.IsEmpty())
	{
		OnFlashText.Broadcast(Text);
	}
}

void UEnemySquadSubsystem::EmitAudioCue(FName CueName, float Scale)
{
	OnAudioCue.Broadcast(CueName, Scale);
}

AKaelCharacter* UEnemySquadSubsystem::FindLocalPlayerCharacter() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	if (const APlayerController* PC = World->GetFirstPlayerController())
	{
		return Cast<AKaelCharacter>(PC->GetPawn());
	}
	return nullptr;
}

float UEnemySquadSubsystem::QueryGroundHeightCm(float WorldXCm, float WorldYCm, float ReferenceZCm) const
{
	if (const UWorld* World = GetWorld())
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SquadGroundCheck), false);
		const FVector Start(WorldXCm, WorldYCm, ReferenceZCm + 2500.0f);
		const FVector End(WorldXCm, WorldYCm, ReferenceZCm - 2500.0f);
		if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
		{
			return Hit.ImpactPoint.Z;
		}
	}
	const KomorebiCore::Vec3 Three = KomorebiBridge::FVectorToThree(FVector(WorldXCm, WorldYCm, ReferenceZCm));
	const double WayD = KomorebiCore::WayDist(Three.X, Three.Z);
	return static_cast<float>(KomorebiCore::NeoKyotoRawHeight(Three.X, Three.Z, WayD) * 100.0);
}

int32 UEnemySquadSubsystem::ComputeHeatStars(float CurrentHeat) const
{
	int32 Stars = 0;
	for (const double Threshold : KomorebiCore::kHeatConfig.Stars)
	{
		if (CurrentHeat >= static_cast<float>(Threshold))
		{
			++Stars;
		}
	}
	return Stars;
}

void UEnemySquadSubsystem::AddHeat(float DeltaHeat)
{
	if (DeltaHeat <= 0.0f)
	{
		return;
	}
	const float MaxHeat = static_cast<float>(KomorebiCore::kHeatConfig.Max);
	Heat = FMath::Clamp(Heat + DeltaHeat, 0.0f, MaxHeat);
	const int32 NewStars = ComputeHeatStars(Heat);
	if (NewStars != HeatStars || DeltaHeat > 0.0f)
	{
		HeatStars = NewStars;
		OnHeatChanged.Broadcast(Heat, HeatStars);
	}
	if (!bHeatIntroSpoken && Heat >= static_cast<float>(KomorebiCore::kHeatConfig.Stars[0]))
	{
		bHeatIntroSpoken = true;
		EmitAudioCue(TEXT("comms_heat"));
	}
}

void UEnemySquadSubsystem::CoolHeat(float DeltaHeat)
{
	if (DeltaHeat <= 0.0f || Heat <= 0.0f)
	{
		return;
	}
	Heat = FMath::Max(0.0f, Heat - DeltaHeat);
	const int32 NewStars = ComputeHeatStars(Heat);
	if (NewStars != HeatStars)
	{
		HeatStars = NewStars;
	}
	OnHeatChanged.Broadcast(Heat, HeatStars);
}

bool UEnemySquadSubsystem::HasWitnessLineOfSight(const AStreetEnemyCharacter* Witness, const FVector& PlayerFeetWorld) const
{
	if (!Witness || !GetWorld())
	{
		return false;
	}
	const FVector WitnessFeet = Witness->GetFeetWorldLocation();
	const float DistM = FVector::Dist2D(WitnessFeet, PlayerFeetWorld) * 0.01f;
	if (DistM > static_cast<float>(KomorebiCore::kWitnessConfig.Radius))
	{
		return false;
	}
	if (DistM < static_cast<float>(KomorebiCore::kWitnessConfig.Close))
	{
		return true;
	}

	const FVector Start = WitnessFeet + FVector(0.0f, 0.0f, 140.0f);
	const FVector Dir = (PlayerFeetWorld - WitnessFeet).GetSafeNormal2D();
	const float TraceDistCm = FMath::Max(10.0f, DistM * 100.0f - 120.0f);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WitnessLOS), false, Witness);
	return !GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Dir * TraceDistCm, ECC_WorldStatic, Params);
}

int32 UEnemySquadSubsystem::ReportBystanderCrime(
	const FVector& CrimeWorldLocation,
	AStreetEnemyCharacter* Victim,
	bool bKilled,
	AActor* Offender)
{
	const FVector PlayerFeet = Offender
		? (Cast<AKaelCharacter>(Offender) ? Cast<AKaelCharacter>(Offender)->GetFeetWorldLocation() : Offender->GetActorLocation())
		: CrimeWorldLocation;

	// Only add Heat directly here if offender has no UCombatComponent (otherwise ApplyGuiltAndCarjack already added it)
	if (!Offender || !Offender->FindComponentByClass<UCombatComponent>())
	{
		AddHeat(static_cast<float>(bKilled ? KomorebiCore::kHeatConfig.Kill : KomorebiCore::kHeatConfig.Hurt));
	}

	if (!bKilled)
	{
		return 0;
	}

	int32 WitnessCount = 0;
	const float MobTime = static_cast<float>(KomorebiCore::kMobConfig.Time);
	const float MobRadiusCm = static_cast<float>(KomorebiCore::kMobConfig.Radius * 100.0);

	for (const TWeakObjectPtr<AStreetEnemyCharacter>& WeakEnemy : RegisteredEnemies)
	{
		AStreetEnemyCharacter* Candidate = WeakEnemy.Get();
		if (!Candidate || Candidate == Victim || !Candidate->IsCiv() || Candidate->bIsThief || Candidate->IsDead() || Candidate->IsGone())
		{
			continue;
		}
		if (FVector::Dist2D(Candidate->GetFeetWorldLocation(), PlayerFeet) > MobRadiusCm)
		{
			continue;
		}
		if (!HasWitnessLineOfSight(Candidate, PlayerFeet))
		{
			continue;
		}
		Candidate->ProvokeWitness(MobTime);
		++WitnessCount;
	}

	if (WitnessCount > 0)
	{
		EmitToast(FString::Printf(TEXT("The street saw that — %d coming for you"), WitnessCount));
	}
	else
	{
		EmitToast(TEXT("A citizen is dead. That one is on you."));
	}

	return WitnessCount;
}

bool UEnemySquadSubsystem::IsSegmentAlive(int32 SegmentIndex) const
{
	for (const TWeakObjectPtr<AStreetEnemyCharacter>& WeakEnemy : RegisteredEnemies)
	{
		const AStreetEnemyCharacter* Enemy = WeakEnemy.Get();
		if (Enemy && Enemy->SegmentIndex == SegmentIndex && !Enemy->IsDead())
		{
			return true;
		}
	}
	for (const TWeakObjectPtr<ARenBossCharacter>& WeakBoss : RegisteredRivals)
	{
		const ARenBossCharacter* Boss = WeakBoss.Get();
		if (Boss && Boss->GetDuelIndex() == SegmentIndex && !Boss->IsDead())
		{
			return true;
		}
	}
	return false;
}

bool UEnemySquadSubsystem::IsSegmentDone(int32 SegmentIndex) const
{
	if (IsSegmentAlive(SegmentIndex))
	{
		return false;
	}
	if (PlayMode == EKomorebiPlayMode::StorySectors && SectorMissionDonePredicate)
	{
		return SectorMissionDonePredicate(SegmentIndex);
	}
	return true;
}

int32 UEnemySquadSubsystem::GetClearedSegmentCount() const
{
	if (WorldScene != EKomorebiWorldScene::NeoKyoto)
	{
		return 0;
	}
	const int32 TotalSegments = static_cast<int32>(KomorebiCore::GetLevels().size());
	int32 Cleared = 0;
	while (Cleared < TotalSegments && IsSegmentDone(Cleared))
	{
		++Cleared;
	}
	return Cleared;
}

int32 UEnemySquadSubsystem::GetRemainingHostileCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AStreetEnemyCharacter>& WeakEnemy : RegisteredEnemies)
	{
		const AStreetEnemyCharacter* Enemy = WeakEnemy.Get();
		if (Enemy && !Enemy->IsDead() && !Enemy->IsCiv())
		{
			++Count;
		}
	}
	for (const TWeakObjectPtr<ARenBossCharacter>& WeakBoss : RegisteredRivals)
	{
		const ARenBossCharacter* Boss = WeakBoss.Get();
		if (Boss && !Boss->IsDead())
		{
			++Count;
		}
	}
	return Count;
}

bool UEnemySquadSubsystem::IsGateBlockingBetween(const FVector& PlayerWorld, const FVector& TargetWorld) const
{
	if (WorldScene != EKomorebiWorldScene::NeoKyoto || PlayMode != EKomorebiPlayMode::StorySectors)
	{
		return false;
	}

	const int32 Cleared = GetClearedSegmentCount();
	const std::vector<KomorebiCore::LevelSpec>& Levels = KomorebiCore::GetLevels();
	const double PlayerThreeZ = KomorebiBridge::FVectorToThree(PlayerWorld).Z;
	const double TargetThreeZ = KomorebiBridge::FVectorToThree(TargetWorld).Z;

	for (int32 i = Cleared; i < static_cast<int32>(Levels.size()); ++i)
	{
		if (!Levels[static_cast<std::size_t>(i)].Gate.has_value())
		{
			continue;
		}
		const double GateZ = Levels[static_cast<std::size_t>(i)].Gate->Z;
		if ((TargetThreeZ - GateZ) * (PlayerThreeZ - GateZ) < 0.0)
		{
			return true;
		}
	}
	return false;
}

void UEnemySquadSubsystem::ResetAfterPlayerDeath(const FVector& RespawnWorldLocation, float ClearRadiusCm)
{
	for (const TWeakObjectPtr<AStreetEnemyCharacter>& WeakEnemy : RegisteredEnemies)
	{
		AStreetEnemyCharacter* Enemy = WeakEnemy.Get();
		if (!Enemy || Enemy->IsDead())
		{
			continue;
		}
		if (Enemy->IsCiv())
		{
			Enemy->ResetToHome(false);
		}
		else if (FVector::Dist2D(Enemy->GetFeetWorldLocation(), RespawnWorldLocation) < ClearRadiusCm)
		{
			Enemy->ResetToHome(false);
		}
	}

	for (const TWeakObjectPtr<ARenBossCharacter>& WeakBoss : RegisteredRivals)
	{
		if (ARenBossCharacter* Boss = WeakBoss.Get())
		{
			if (!Boss->IsDead())
			{
				Boss->ResetDuel(false);
			}
		}
	}
}

ARenBossCharacter* UEnemySquadSubsystem::GetActiveEngagedBoss() const
{
	const AKaelCharacter* PlayerChar = FindLocalPlayerCharacter();
	if (!PlayerChar)
	{
		return nullptr;
	}
	const FVector PlayerFeet = PlayerChar->GetFeetWorldLocation();

	for (const TWeakObjectPtr<ARenBossCharacter>& WeakBoss : RegisteredRivals)
	{
		ARenBossCharacter* Boss = WeakBoss.Get();
		if (Boss && Boss->IsEngaged() && FVector::Dist2D(Boss->GetFeetWorldLocation(), PlayerFeet) < 1800.0f)
		{
			return Boss;
		}
	}
	return nullptr;
}

bool UEnemySquadSubsystem::IsPlayerInActiveCombat() const
{
	const AKaelCharacter* PlayerChar = FindLocalPlayerCharacter();
	if (!PlayerChar)
	{
		return false;
	}
	const FVector PlayerFeet = PlayerChar->GetFeetWorldLocation();

	for (const TWeakObjectPtr<AStreetEnemyCharacter>& WeakEnemy : RegisteredEnemies)
	{
		const AStreetEnemyCharacter* Enemy = WeakEnemy.Get();
		if (Enemy && !Enemy->IsDead() && !Enemy->IsGone() && Enemy->AIMode == EStreetEnemyAIMode::Chase
			&& FVector::Dist2D(Enemy->GetFeetWorldLocation(), PlayerFeet) < 2400.0f)
		{
			return true;
		}
	}
	for (const TWeakObjectPtr<ARenBossCharacter>& WeakBoss : RegisteredRivals)
	{
		const ARenBossCharacter* Boss = WeakBoss.Get();
		if (Boss && Boss->IsEngaged() && FVector::Dist2D(Boss->GetFeetWorldLocation(), PlayerFeet) < 2400.0f)
		{
			return true;
		}
	}
	return false;
}

void UEnemySquadSubsystem::SpawnCampaignPopulation(int32 AlreadyClearedCount)
{
	UWorld* World = GetWorld();
	if (!World || WorldScene != EKomorebiWorldScene::NeoKyoto)
	{
		return;
	}

	UClass* EnemyCls = StreetEnemyClass ? StreetEnemyClass.Get() : AStreetEnemyCharacter::StaticClass();
	UClass* BossCls = RenBossClass ? RenBossClass.Get() : ARenBossCharacter::StaticClass();

	const bool bRoam = (PlayMode != EKomorebiPlayMode::StorySectors);
	const std::vector<KomorebiCore::LevelSpec>& Levels = KomorebiCore::GetLevels();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// 1. Spawn segment crews and register relay checkpoints
	for (int32 SegIdx = 0; SegIdx < static_cast<int32>(Levels.size()); ++SegIdx)
	{
		const KomorebiCore::LevelSpec& Level = Levels[static_cast<std::size_t>(SegIdx)];
		const FVector RelayUE = KomorebiBridge::ThreeXZToFVector(Level.Checkpoint.X, 0.0, Level.Checkpoint.Z);
		RegisterRelayLocation(FVector(RelayUE.X, RelayUE.Y, QueryGroundHeightCm(RelayUE.X, RelayUE.Y, 0.0f)));

		if (!bRoam && SegIdx < AlreadyClearedCount)
		{
			continue;
		}

		for (int32 SpotIdx = 0; SpotIdx < static_cast<int32>(Level.Spots.size()); ++SpotIdx)
		{
			const KomorebiCore::EnemySpawnSpot& Spot = Level.Spots[static_cast<std::size_t>(SpotIdx)];
			const FVector PosUE = KomorebiBridge::ThreeXZToFVector(Spot.X, 0.0, Spot.Z);
			const float GroundZ = QueryGroundHeightCm(PosUE.X, PosUE.Y, 0.0f);
			const FTransform SpawnXform(FRotator(0.0f, FMath::FRandRange(-180.0f, 180.0f), 0.0f), FVector(PosUE.X, PosUE.Y, GroundZ + 90.0f));

			if (AStreetEnemyCharacter* Enemy = World->SpawnActor<AStreetEnemyCharacter>(EnemyCls, SpawnXform, SpawnParams))
			{
				Enemy->InitializeEnemy(static_cast<EKomorebiEnemyKind>(Spot.Kind), SegIdx, SpotIdx, false, false, false);
			}
		}
	}

	// 2. Spawn 6 Mirror Rivals
	const std::vector<KomorebiCore::DuelConfig>& Duels = KomorebiCore::GetDuelConfigs();
	for (int32 i = 0; i < static_cast<int32>(Duels.size()); ++i)
	{
		if (!bRoam && i < AlreadyClearedCount)
		{
			continue;
		}
		const KomorebiCore::DuelConfig& Duel = Duels[static_cast<std::size_t>(i)];
		const FVector PosUE = KomorebiBridge::ThreeXZToFVector(Duel.X, 0.0, Duel.Z);
		const float GroundZ = QueryGroundHeightCm(PosUE.X, PosUE.Y, 0.0f);
		if (ARenBossCharacter* Boss = World->SpawnActor<ARenBossCharacter>(BossCls, FTransform(FVector(PosUE.X, PosUE.Y, GroundZ + 90.0f)), SpawnParams))
		{
			Boss->InitializeDuel(i);
		}
	}

	// 3. Spawn 40 sidewalk crowd entities (citizens & courier thieves matching hunt.js lines 240-251)
	const AKaelCharacter* PlayerChar = FindLocalPlayerCharacter();
	const KomorebiCore::Vec3 StartThree = PlayerChar
		? KomorebiBridge::FVectorToThree(PlayerChar->GetFeetWorldLocation())
		: KomorebiCore::Vec3{ KomorebiCore::Start().X, 0.0, KomorebiCore::Start().Z };

	int32 CivVariant = 0;
	for (int32 i = 0; i < 40; ++i)
	{
		const bool bRunner = (i % 5 == 0);
		const bool bThief = bRunner && (i % 10 != 0);
		const double Dir = (i % 2 != 0) ? 1.0 : -1.0;
		const double SeedX = StartThree.X + (static_cast<double>(i % 8) - 3.5) * 8.0;
		const double SeedZ = StartThree.Z - 6.0 - static_cast<double>(i / 8) * 8.0;
		const KomorebiCore::PavementStep Spot = KomorebiCore::NextPavement(SeedX, SeedZ, Dir, false);
		const FVector PosUE = KomorebiBridge::ThreeXZToFVector(Spot.X, 0.0, Spot.Z);
		const float GroundZ = QueryGroundHeightCm(PosUE.X, PosUE.Y, 0.0f);

		if (AStreetEnemyCharacter* Civ = World->SpawnActor<AStreetEnemyCharacter>(EnemyCls, FTransform(FVector(PosUE.X, PosUE.Y, GroundZ + 90.0f)), SpawnParams))
		{
			Civ->InitializeEnemy(
				bRunner ? EKomorebiEnemyKind::Thief : EKomorebiEnemyKind::Civ,
				-1,
				bRunner ? i : CivVariant++,
				false,
				bThief,
				true);
		}
	}
}

void UEnemySquadSubsystem::StepCrewTacticsOrders(AKaelCharacter* PlayerChar)
{
	const FVector PlayerFeet = PlayerChar->GetFeetWorldLocation();

	struct FOrderEntry
	{
		AStreetEnemyCharacter* Enemy = nullptr;
		int32 AttackIndex = 0;
		bool bRanged = false;
	};

	TArray<FOrderEntry> ActiveEntries;
	std::vector<KomorebiCore::CrewMemberOrder> CoreGroup;

	for (const TWeakObjectPtr<AStreetEnemyCharacter>& WeakEnemy : RegisteredEnemies)
	{
		AStreetEnemyCharacter* Enemy = WeakEnemy.Get();
		if (!Enemy || Enemy->IsDead() || Enemy->IsGone())
		{
			continue;
		}
		const KomorebiCore::EEnemyKind CoreKind = KomorebiBridge::ToCoreEnemyKind(Enemy->EnemyKind);
		const std::vector<KomorebiCore::EnemyMovePattern>& Patterns = KomorebiCore::GetEnemyPatterns(CoreKind);
		if (Patterns.empty())
		{
			Enemy->ApplySquadOrder(true, 1, 3.6f, 0, false, PlayerChar);
			continue;
		}

		const FVector EnemyFeet = Enemy->GetFeetWorldLocation();
		const float DistM = FVector::Dist2D(PlayerFeet, EnemyFeet) * 0.01f;
		if (DistM > AStreetEnemyCharacter::kCrewAIRangeMeters)
		{
			continue;
		}

		const int32 ChosenIdx = KomorebiCore::ChooseAttack(CoreKind, static_cast<double>(DistM), Enemy->HasReprisal(), Enemy->GetAttackCount());
		const int32 ActiveIdx = (Enemy->WindupRemaining > 0.0f) ? Enemy->GetAttackIndex() : ChosenIdx;
		const bool bRanged = Patterns[static_cast<std::size_t>(FMath::Clamp(ActiveIdx, 0, static_cast<int32>(Patterns.size()) - 1))].Shot;

		const float HomeDistM = FVector::Dist2D(EnemyFeet, Enemy->GetHomeWorldLocation()) * 0.01f;
		const bool bEligibleForGroup =
			DistM <= 19.0f
			&& (Enemy->AIMode == EStreetEnemyAIMode::Chase || DistM < Enemy->GetAggroRangeMeters())
			&& HomeDistM <= Enemy->GetLeashMeters()
			&& !IsGateBlockingBetween(PlayerFeet, EnemyFeet);

		if (!bEligibleForGroup)
		{
			Enemy->ApplySquadOrder(true, 1, bRanged ? 7.0f : 3.6f, ChosenIdx, bRanged, PlayerChar);
			continue;
		}

		const KomorebiCore::EnemyMovePattern& MoveSpec = Patterns[static_cast<std::size_t>(ChosenIdx)];
		KomorebiCore::CrewMemberOrder Order;
		Order.Windup = static_cast<double>(Enemy->WindupRemaining);
		Order.Ranged = bRanged;
		Order.Score = std::abs(static_cast<double>(DistM) - (bRanged ? 7.0 : (MoveSpec.Reach + MoveSpec.Rush)))
			+ static_cast<double>(Enemy->GetAttackCooldown()) * 5.0
			+ static_cast<double>(Enemy->GetDefendRemaining() + Enemy->GetStaggerRemaining()) * 7.0
			- (Enemy->IsPressuring() ? 0.65 : 0.0);

		ActiveEntries.Add(FOrderEntry{ Enemy, ChosenIdx, bRanged });
		CoreGroup.push_back(Order);
	}

	if (!CoreGroup.empty())
	{
		KomorebiCore::ComputeCrewOrder(CoreGroup);
		for (int32 i = 0; i < ActiveEntries.Num(); ++i)
		{
			const KomorebiCore::CrewMemberOrder& Result = CoreGroup[static_cast<std::size_t>(i)];
			ActiveEntries[i].Enemy->ApplySquadOrder(
				Result.Lead,
				Result.Side,
				static_cast<float>(Result.Radius),
				ActiveEntries[i].AttackIndex,
				ActiveEntries[i].bRanged,
				PlayerChar);
		}
	}
}

void UEnemySquadSubsystem::StepPairwiseSeparationAndChats(float DeltaTime, const FVector& PlayerFeetWorld, bool bFightingNearby)
{
	const int32 Count = RegisteredEnemies.Num();
	for (int32 i = 0; i < Count; ++i)
	{
		AStreetEnemyCharacter* A = RegisteredEnemies[i].Get();
		if (!A || A->IsDead() || A->IsGone())
		{
			continue;
		}
		const FVector FeetA = A->GetFeetWorldLocation();
		const float DistAP = FVector::Dist2D(FeetA, PlayerFeetWorld) * 0.01f;

		for (int32 j = i + 1; j < Count; ++j)
		{
			AStreetEnemyCharacter* B = RegisteredEnemies[j].Get();
			if (!B || B->IsDead() || B->IsGone())
			{
				continue;
			}
			const FVector FeetB = B->GetFeetWorldLocation();
			const float LimitM = (A->IsCiv() && B->IsCiv()) ? 46.0f : 36.0f;
			if (DistAP > LimitM || (FVector::Dist2D(FeetB, PlayerFeetWorld) * 0.01f) > LimitM)
			{
				continue;
			}

			const FVector Delta = FVector(FeetB.X - FeetA.X, FeetB.Y - FeetA.Y, 0.0f);
			const float DistCm = FMath::Max(0.1f, Delta.Size());
			const float DistM = DistCm * 0.01f;

			// Brief civilian sidewalk chats (hunt.js lines 1358-1368)
			if (!bFightingNearby
				&& A->EnemyKind == EKomorebiEnemyKind::Civ
				&& B->EnemyKind == EKomorebiEnemyKind::Civ
				&& DistM > 1.0f && DistM < 1.9f
				&& !A->IsAngry() && !B->IsAngry()
				&& A->GetFleeTimer() <= 0.0f && B->GetFleeTimer() <= 0.0f
				&& A->GetWaitTimer() <= 0.0f && B->GetWaitTimer() <= 0.0f
				&& A->GetChatCooldown() <= 0.0f && B->GetChatCooldown() <= 0.0f
				&& FMath::FRand() < DeltaTime * 0.55f)
			{
				A->StartStreetChat(1.2f, 14.0f, Delta.Rotation().Yaw);
				B->StartStreetChat(1.2f, 14.0f, (-Delta).Rotation().Yaw);
			}

			// Separation push (minD = 0.45 * (scaleA + scaleB) metres)
			const float MinDistCm = 45.0f * (A->GetVisualScale() + B->GetVisualScale());
			if (DistCm < MinDistCm)
			{
				const FVector PushVec = (Delta / DistCm) * ((MinDistCm - DistCm) * 0.5f);
				A->ApplySeparationPush(-PushVec);
				B->ApplySeparationPush(PushVec);
			}
		}
	}
}

void UEnemySquadSubsystem::StepSidewalkPoolRecycling(float DeltaTime, const FVector& PlayerFeetWorld)
{
	SidewalkRecycleTimer -= DeltaTime;
	if (SidewalkRecycleTimer > 0.0f)
	{
		return;
	}
	SidewalkRecycleTimer = kSidewalkRecycleInterval;

	const KomorebiCore::Vec3 PlayerThree = KomorebiBridge::FVectorToThree(PlayerFeetWorld);
	for (const TWeakObjectPtr<AStreetEnemyCharacter>& WeakEnemy : RegisteredEnemies)
	{
		AStreetEnemyCharacter* Enemy = WeakEnemy.Get();
		if (!Enemy || !Enemy->IsSidewalkPool() || Enemy->IsDead() || Enemy->IsGone() || Enemy->IsAngry() || Enemy->GetWaitTimer() >= 6.0f)
		{
			continue;
		}
		if (FVector::Dist2D(Enemy->GetFeetWorldLocation(), PlayerFeetWorld) * 0.01f > kSidewalkRecycleDistanceMeters)
		{
			const int32 Seed = Enemy->GetPoolSeed();
			const double SeedX = PlayerThree.X + static_cast<double>((Seed % 5) - 2) * 7.0;
			const double SeedZ = PlayerThree.Z + static_cast<double>((Seed % 3) - 1) * 8.0;
			const KomorebiCore::PavementStep NewSpot = KomorebiCore::NextPavement(SeedX, SeedZ, Enemy->GetPoolDir(), false);
			const KomorebiCore::PavementStep NextSpot = KomorebiCore::NextPavement(NewSpot.X, NewSpot.Z, Enemy->GetPoolDir(), false);

			const FVector HomeUE = KomorebiBridge::ThreeXZToFVector(NewSpot.X, PlayerThree.Y, NewSpot.Z);
			const FVector NextUE = KomorebiBridge::ThreeXZToFVector(NextSpot.X, PlayerThree.Y, NextSpot.Z);
			Enemy->RecycleToSidewalk(
				FVector(HomeUE.X, HomeUE.Y, QueryGroundHeightCm(HomeUE.X, HomeUE.Y, PlayerFeetWorld.Z)),
				FVector(NextUE.X, NextUE.Y, QueryGroundHeightCm(NextUE.X, NextUE.Y, PlayerFeetWorld.Z)));
			break; // Recycle at most one body per 0.9s matching hunt.js line 1104
		}
	}
}

void UEnemySquadSubsystem::SpawnPoliceEnforcer(const FVector& PlayerFeetWorld)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const KomorebiCore::Vec3 PlayerThree = KomorebiBridge::FVectorToThree(PlayerFeetWorld);
	const KomorebiCore::PavementStep Spot = KomorebiCore::NextPavement(PlayerThree.X + 16.0, PlayerThree.Z - 10.0, 1.0, false);
	const FVector PosUE = KomorebiBridge::ThreeXZToFVector(Spot.X, PlayerThree.Y, Spot.Z);
	const float GroundZ = QueryGroundHeightCm(PosUE.X, PosUE.Y, PlayerFeetWorld.Z);

	UClass* EnemyCls = StreetEnemyClass ? StreetEnemyClass.Get() : AStreetEnemyCharacter::StaticClass();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (AStreetEnemyCharacter* Cop = World->SpawnActor<AStreetEnemyCharacter>(EnemyCls, FTransform(FVector(PosUE.X, PosUE.Y, GroundZ + 90.0f)), Params))
	{
		const bool bFirstActiveCop = !SpawnedPolice.ContainsByPredicate([](const TWeakObjectPtr<AStreetEnemyCharacter>& P)
		{
			return P.IsValid() && !P->IsDead() && !P->IsGone();
		});
		Cop->InitializeEnemy(EKomorebiEnemyKind::Enforcer, -1, SpawnedPolice.Num(), true, false, false);
		SpawnedPolice.Add(Cop);
		if (bFirstActiveCop)
		{
			EmitToast(TEXT("Police are in the district"));
		}
	}
}

void UEnemySquadSubsystem::SpawnRoadblockAheadOf(AKaelCharacter* PlayerChar)
{
	UWorld* World = GetWorld();
	if (!World || !PlayerChar || ActiveRoadblockActor.IsValid())
	{
		return;
	}

	const FVector PlayerFeet = PlayerChar->GetFeetWorldLocation();
	const FVector Forward = PlayerChar->GetActorForwardVector().GetSafeNormal2D();
	const FVector BlockXY = PlayerFeet + Forward * 1600.0f; // 16m ahead (hunt.js line 1583)
	const float GroundZ = QueryGroundHeightCm(BlockXY.X, BlockXY.Y, PlayerFeet.Z);
	const FVector BlockCenter(BlockXY.X, BlockXY.Y, GroundZ + 65.0f);
	const FRotator BlockRot(0.0f, PlayerChar->GetActorRotation().Yaw, 0.0f);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Barrier = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(BlockRot, BlockCenter), Params);
	if (!Barrier)
	{
		return;
	}

	// Solid 12m wide x 1.3m tall x 0.7m thick roadblock collider (hunt.js lines 1584-1586: half extents 35cm X, 600cm Y, 65cm Z)
	UBoxComponent* Box = NewObject<UBoxComponent>(Barrier, TEXT("RoadblockCollider"));
	Box->InitBoxExtent(FVector(35.0f, 600.0f, 65.0f));
	Box->SetCollisionProfileName(TEXT("BlockAll"));
	Box->RegisterComponent();
	Barrier->SetRootComponent(Box);
	Barrier->SetActorLocationAndRotation(BlockCenter, BlockRot);

	// Procedural dark slate + hazard-yellow stripe visual mesh
	UProceduralMeshComponent* Visual = NewObject<UProceduralMeshComponent>(Barrier, TEXT("RoadblockMesh"));
	Visual->SetupAttachment(Box);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->RegisterComponent();

	const FVector Ext(35.0f, 600.0f, 65.0f);
	TArray<FVector> Verts = {
		FVector(-Ext.X, -Ext.Y, -Ext.Z), FVector(-Ext.X,  Ext.Y, -Ext.Z),
		FVector(-Ext.X,  Ext.Y,  Ext.Z), FVector(-Ext.X, -Ext.Y,  Ext.Z),
		FVector( Ext.X, -Ext.Y, -Ext.Z), FVector( Ext.X,  Ext.Y, -Ext.Z),
		FVector( Ext.X,  Ext.Y,  Ext.Z), FVector( Ext.X, -Ext.Y,  Ext.Z)
	};
	TArray<int32> Tris = {
		0, 1, 2, 0, 2, 3, // back
		5, 4, 7, 5, 7, 6, // front
		3, 2, 6, 3, 6, 7, // top
		4, 5, 1, 4, 1, 0, // bottom
		4, 0, 3, 4, 3, 7, // left
		1, 5, 6, 1, 6, 2  // right
	};
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	Normals.Init(FVector::UpVector, 8);
	UVs.Init(FVector2D::ZeroVector, 8);
	Colors.Init(FLinearColor(0.078f, 0.110f, 0.157f, 1.0f), 8);
	TArray<FProcMeshTangent> Tangents;
	Visual->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, Colors, Tangents, false);

	ActiveRoadblockActor = Barrier;
	EmitToast(TEXT("Roadblock deployed ahead"));
}

void UEnemySquadSubsystem::DestroyRoadblock()
{
	if (AActor* Barrier = ActiveRoadblockActor.Get())
	{
		Barrier->Destroy();
	}
	ActiveRoadblockActor.Reset();
}

void UEnemySquadSubsystem::StepHeatAndPoliceEscalation(float DeltaTime, AKaelCharacter* PlayerChar)
{
	if (WorldScene != EKomorebiWorldScene::NeoKyoto || !PlayerChar)
	{
		return;
	}

	const FVector PlayerFeet = PlayerChar->GetFeetWorldLocation();

	// Decay Heat faster near relays/cairns (< 8m), or standard decay when unwatched (hunt.js lines 1567-1570)
	bool bAtRelay = false;
	for (const FVector& RelayLoc : RelayWorldLocations)
	{
		if (FVector::Dist2D(RelayLoc, PlayerFeet) < 800.0f)
		{
			bAtRelay = true;
			break;
		}
	}

	bool bWatched = false;
	if (!bAtRelay)
	{
		for (const TWeakObjectPtr<AStreetEnemyCharacter>& WeakEnemy : RegisteredEnemies)
		{
			const AStreetEnemyCharacter* Enemy = WeakEnemy.Get();
			if (Enemy && Enemy->IsAngry() && !Enemy->IsDead() && !Enemy->IsGone() && HasWitnessLineOfSight(Enemy, PlayerFeet))
			{
				bWatched = true;
				break;
			}
		}
	}

	const float OldHeat = Heat;
	if (bAtRelay)
	{
		Heat = FMath::Max(0.0f, Heat - static_cast<float>(KomorebiCore::kHeatConfig.Safe) * DeltaTime);
	}
	else if (!bWatched)
	{
		Heat = FMath::Max(0.0f, Heat - static_cast<float>(KomorebiCore::kHeatConfig.Decay) * DeltaTime);
	}

	const int32 NewStars = ComputeHeatStars(Heat);
	if (NewStars != HeatStars || !FMath::IsNearlyEqual(OldHeat, Heat, 0.05f))
	{
		HeatStars = NewStars;
		OnHeatChanged.Broadcast(Heat, HeatStars);
	}

	// Prune stale police weak pointers
	SpawnedPolice.RemoveAll([](const TWeakObjectPtr<AStreetEnemyCharacter>& P) { return !P.IsValid(); });

	// 2 Stars: spawn up to 2 Police enforcers (hunt.js lines 1572-1581)
	if (HeatStars >= 2 && SpawnedPolice.Num() < KomorebiCore::kHeatConfig.Cops)
	{
		SpawnPoliceEnforcer(PlayerFeet);
	}

	// 3 Stars: spawn roadblock barrier 16m ahead; remove once heat drops below 3 stars (hunt.js lines 1582-1590)
	if (HeatStars >= 3 && !ActiveRoadblockActor.IsValid())
	{
		SpawnRoadblockAheadOf(PlayerChar);
	}
	else if (HeatStars < 3 && ActiveRoadblockActor.IsValid())
	{
		DestroyRoadblock();
	}
}

void UEnemySquadSubsystem::StepSegmentClearDetection(AKaelCharacter* PlayerChar)
{
	if (WorldScene != EKomorebiWorldScene::NeoKyoto || PlayMode != EKomorebiPlayMode::StorySectors)
	{
		return;
	}

	const int32 ClearedNow = GetClearedSegmentCount();
	if (ClearedNow <= LastKnownClearedCount)
	{
		return;
	}

	const std::vector<KomorebiCore::LevelSpec>& Levels = KomorebiCore::GetLevels();
	UCombatComponent* PlayerCombat = PlayerChar ? PlayerChar->GetCombatComponent() : nullptr;
	if (PlayerCombat)
	{
		PlayerCombat->ClearedSectors = ClearedNow;
	}

	for (int32 i = LastKnownClearedCount; i < ClearedNow && i < static_cast<int32>(Levels.size()); ++i)
	{
		const KomorebiCore::LevelSpec& Level = Levels[static_cast<std::size_t>(i)];
		EKomorebiPower Granted = EKomorebiPower::Spark;
		if (Level.Grant.has_value())
		{
			Granted = static_cast<EKomorebiPower>(*Level.Grant);
			const KomorebiCore::PowerSpec& PSpec = KomorebiCore::GetPowerSpec(*Level.Grant);
			EmitToast(FString::Printf(
				TEXT("Sector %d clear — %s earned (%s)"),
				i + 1,
				*KomorebiBridge::ToFString(PSpec.Name),
				*KomorebiBridge::ToFString(PSpec.Key)));
		}
		EmitAudioCue(TEXT("gateOpen"));
		OnSegmentCleared.Broadcast(i, ClearedNow, Granted);
	}

	if (ClearedNow >= static_cast<int32>(Levels.size()))
	{
		EmitToast(TEXT("Amber Sanctuary lies ahead — enter the golden terminal"));
	}

	LastKnownClearedCount = ClearedNow;
}

void UEnemySquadSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const float Dt = FMath::Clamp(DeltaTime, 0.0f, 0.05f);
	if (Dt <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Compact stale weak pointers
	RegisteredEnemies.RemoveAll([](const TWeakObjectPtr<AStreetEnemyCharacter>& Ptr) { return !Ptr.IsValid(); });
	RegisteredRivals.RemoveAll([](const TWeakObjectPtr<ARenBossCharacter>& Ptr) { return !Ptr.IsValid(); });

	AKaelCharacter* PlayerChar = FindLocalPlayerCharacter();
	if (!PlayerChar)
	{
		return;
	}

	const FVector PlayerFeet = PlayerChar->GetFeetWorldLocation();
	const UCombatComponent* PlayerCombat = PlayerChar->GetCombatComponent();
	const bool bFreeRoam = (PlayMode != EKomorebiPlayMode::StorySectors);

	const bool bFightingNearby = (PlayerCombat && (PlayerCombat->SwingU >= 0.0f || PlayerCombat->GetCastPose() > 0.0f))
		|| IsPlayerInActiveCombat();

	// 1. Compute squad flank/lead orders across active crews via KomorebiCore::ComputeCrewOrder()
	StepCrewTacticsOrders(PlayerChar);

	// 2. Step all registered street enemies, civilians, thieves, and police
	for (int32 i = 0; i < RegisteredEnemies.Num(); ++i)
	{
		if (AStreetEnemyCharacter* Enemy = RegisteredEnemies[i].Get())
		{
			const bool bGateBlocked = IsGateBlockingBetween(PlayerFeet, Enemy->GetFeetWorldLocation());
			Enemy->StepEnemyAI(Dt, PlayerChar, bFightingNearby, bGateBlocked, bFreeRoam);
		}
	}

	// 3. Step all 6 Mirror Rivals (ready once their segment's street crew is cleared, or closest rival in roam)
	ARenBossCharacter* ClosestRoamRival = nullptr;
	if (bFreeRoam)
	{
		float BestDistSq = FLT_MAX;
		for (const TWeakObjectPtr<ARenBossCharacter>& WeakBoss : RegisteredRivals)
		{
			ARenBossCharacter* Boss = WeakBoss.Get();
			if (!Boss || Boss->IsDead())
			{
				continue;
			}
			const float D2 = FVector::DistSquared2D(Boss->GetFeetWorldLocation(), PlayerFeet);
			if (D2 < BestDistSq)
			{
				BestDistSq = D2;
				ClosestRoamRival = Boss;
			}
		}
	}

	for (const TWeakObjectPtr<ARenBossCharacter>& WeakBoss : RegisteredRivals)
	{
		ARenBossCharacter* Boss = WeakBoss.Get();
		if (!Boss)
		{
			continue;
		}
		bool bReady = false;
		if (bFreeRoam)
		{
			bReady = (Boss == ClosestRoamRival);
		}
		else
		{
			const int32 Seg = Boss->GetDuelIndex();
			bool bCrewAlive = false;
			for (const TWeakObjectPtr<AStreetEnemyCharacter>& WeakEnemy : RegisteredEnemies)
			{
				const AStreetEnemyCharacter* Enemy = WeakEnemy.Get();
				if (Enemy && Enemy->SegmentIndex == Seg && !Enemy->IsDead())
				{
					bCrewAlive = true;
					break;
				}
			}
			const bool bMissionOk = !SectorMissionDonePredicate || SectorMissionDonePredicate(Seg);
			bReady = !bCrewAlive && bMissionOk;
		}
		const bool bGateBlocked = IsGateBlockingBetween(PlayerFeet, Boss->GetFeetWorldLocation());
		Boss->TickDuel(Dt, PlayerChar, bReady, bGateBlocked, bFreeRoam);
	}

	// 4. Pairwise body separation & street chats
	StepPairwiseSeparationAndChats(Dt, PlayerFeet, bFightingNearby);

	// 5. Sidewalk crowd pool recycling
	StepSidewalkPoolRecycling(Dt, PlayerFeet);

	// 6. Heat decay & 2-Star Police / 3-Star Roadblock escalation
	StepHeatAndPoliceEscalation(Dt, PlayerChar);

	// 7. Campaign segment clear detection & power unlocks
	StepSegmentClearDetection(PlayerChar);
}
