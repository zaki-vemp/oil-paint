// Copyright Komorebi Project. Replicated Neo-Kyoto online street activities: 12-Gate Race, PvP Duel Ring, and Proximity Revive.
#include "Network/OnlineActivitiesActor.h"

#include "Network/KomorebiGameState.h"
#include "Network/KomorebiPlayerState.h"
#include "Network/KomorebiPlayerController.h"
#include "UI/KomorebiHUD.h"
#include "World/NeoKyotoTerrain.h"
#include "World/CoordBridge.h"
#include "Game/CombatRules.h"
#include "Game/LevelsData.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

AOnlineActivitiesActor::AOnlineActivitiesActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	StartGateMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("StartGateMesh"));
	StartGateMesh->SetupAttachment(SceneRoot);
	StartGateMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StartGateMesh->SetCastShadow(false);

	ActiveGateMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ActiveGateMesh"));
	ActiveGateMesh->SetupAttachment(SceneRoot);
	ActiveGateMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ActiveGateMesh->SetCastShadow(false);

	DuelRingMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("DuelRingMesh"));
	DuelRingMesh->SetupAttachment(SceneRoot);
	DuelRingMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DuelRingMesh->SetCastShadow(false);
}

void AOnlineActivitiesActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
}

void AOnlineActivitiesActor::BeginPlay()
{
	Super::BeginPlay();
	BuildWayCoordinates();
	if (!IsRunningDedicatedServer())
	{
		BuildVisualRings();
	}
}

void AOnlineActivitiesActor::BuildWayCoordinates()
{
	KomorebiCore::NeoKyotoTerrainMap Terrain;
	Terrain.Prepare();

	const std::vector<KomorebiCore::Vec2>& Way = KomorebiCore::WaySamples();
	if (Way.size() < 4)
	{
		return;
	}

	auto SamplePosAndRot = [&](int32 Idx, double LiftMeters, FVector& OutPos, FRotator& OutRot)
	{
		const int32 Clamped = FMath::Clamp(Idx, 1, static_cast<int32>(Way.size()) - 2);
		const KomorebiCore::Vec2& Cur = Way[static_cast<size_t>(Clamped)];
		const KomorebiCore::Vec2& Prev = Way[static_cast<size_t>(Clamped - 1)];
		const KomorebiCore::Vec2& Next = Way[static_cast<size_t>(Clamped + 1)];

		const double GroundY = Terrain.HeightAt(Cur.X, Cur.Z);
		OutPos = KomorebiBridge::ThreeXZToFVector(Cur.X, GroundY + LiftMeters, Cur.Z);

		const double ThreeYaw = std::atan2(Next.X - Prev.X, Next.Z - Prev.Z);
		const double UEYawDeg = KomorebiCore::ThreeYawToUEYawDeg(ThreeYaw);
		OutRot = FRotator(0.0f, static_cast<float>(UEYawDeg), 0.0f);
	};

	FRotator UnusedRot;
	SamplePosAndRot(KomorebiCore::kDuelRingConfig.At, 0.08, DuelRingCenterUE, UnusedRot);

	const double GateLiftMeters = KomorebiCore::kRaceConfig.Radius - 1.3;
	SamplePosAndRot(KomorebiCore::kRaceConfig.Start, GateLiftMeters, RaceStartLocationUE, RaceStartRotationUE);

	RaceGateLocationsUE.Reset();
	RaceGateRotationsUE.Reset();
	for (int32 GateWayIndex : KomorebiCore::kRaceConfig.Gates)
	{
		FVector GatePos;
		FRotator GateRot;
		SamplePosAndRot(GateWayIndex, GateLiftMeters, GatePos, GateRot);
		RaceGateLocationsUE.Add(GatePos);
		RaceGateRotationsUE.Add(GateRot);
	}
}

void AOnlineActivitiesActor::BuildVisualRings()
{
	const float GateRadiusCm = static_cast<float>((KomorebiCore::kRaceConfig.Radius - 1.5) * KomorebiCore::kMetersToCm);
	const float TubeRadiusCm = 16.0f;

	// Lime green Start Gate (#9dff6a)
	BuildTorusGateMesh(StartGateMesh, GateRadiusCm, TubeRadiusCm, FColor(0x9d, 0xff, 0x6a, 210));
	StartGateMesh->SetWorldLocationAndRotation(RaceStartLocationUE, RaceStartRotationUE);
	StartGateMesh->SetVisibility(true);

	// Yellow Active Race Gate (#fcee0a)
	BuildTorusGateMesh(ActiveGateMesh, GateRadiusCm, TubeRadiusCm, FColor(0xfc, 0xee, 0x0a, 220));
	ActiveGateMesh->SetVisibility(false);

	// Pink PvP Duel Ring (#ff2a6d)
	const float RingOuterCm = static_cast<float>(KomorebiCore::kDuelRingConfig.Radius * KomorebiCore::kMetersToCm);
	const float RingInnerCm = RingOuterCm - 35.0f;
	BuildFlatRingMesh(DuelRingMesh, RingInnerCm, RingOuterCm, FColor(0xff, 0x2a, 0x6d, 190));
	DuelRingMesh->SetWorldLocation(DuelRingCenterUE);
	DuelRingMesh->SetVisibility(true);
}

void AOnlineActivitiesActor::BuildTorusGateMesh(
	UProceduralMeshComponent* TargetMesh,
	float RadiusCm,
	float TubeCm,
	const FColor& VertexColor)
{
	if (!TargetMesh)
	{
		return;
	}

	constexpr int32 MajorSegs = 36;
	constexpr int32 MinorSegs = 8;
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> Colors;

	Vertices.Reserve(MajorSegs * MinorSegs);
	Normals.Reserve(MajorSegs * MinorSegs);
	UVs.Reserve(MajorSegs * MinorSegs);
	Colors.Reserve(MajorSegs * MinorSegs);

	// Ring lies in local YZ plane (so player drives through along local X).
	for (int32 i = 0; i < MajorSegs; ++i)
	{
		const float Phi = (static_cast<float>(i) / MajorSegs) * 2.0f * PI;
		const float CosP = FMath::Cos(Phi);
		const float SinP = FMath::Sin(Phi);

		for (int32 j = 0; j < MinorSegs; ++j)
		{
			const float Theta = (static_cast<float>(j) / MinorSegs) * 2.0f * PI;
			const float CosT = FMath::Cos(Theta);
			const float SinT = FMath::Sin(Theta);

			const float R = RadiusCm + TubeCm * CosT;
			const FVector Pos(TubeCm * SinT, R * CosP, R * SinP);
			const FVector Norm(SinT, CosT * CosP, CosT * SinP);

			Vertices.Add(Pos);
			Normals.Add(Norm.GetSafeNormal());
			UVs.Add(FVector2D(static_cast<float>(i) / MajorSegs, static_cast<float>(j) / MinorSegs));
			Colors.Add(VertexColor);
		}
	}

	for (int32 i = 0; i < MajorSegs; ++i)
	{
		const int32 NextI = (i + 1) % MajorSegs;
		for (int32 j = 0; j < MinorSegs; ++j)
		{
			const int32 NextJ = (j + 1) % MinorSegs;
			const int32 A = i * MinorSegs + j;
			const int32 B = NextI * MinorSegs + j;
			const int32 C = NextI * MinorSegs + NextJ;
			const int32 D = i * MinorSegs + NextJ;

			Triangles.Add(A); Triangles.Add(B); Triangles.Add(C);
			Triangles.Add(A); Triangles.Add(C); Triangles.Add(D);
		}
	}

	TargetMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, Colors, TArray<FProcMeshTangent>(), false);
}

void AOnlineActivitiesActor::BuildFlatRingMesh(
	UProceduralMeshComponent* TargetMesh,
	float InnerRadiusCm,
	float OuterRadiusCm,
	const FColor& VertexColor)
{
	if (!TargetMesh)
	{
		return;
	}

	constexpr int32 Segs = 64;
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> Colors;

	for (int32 i = 0; i < Segs; ++i)
	{
		const float Ang = (static_cast<float>(i) / Segs) * 2.0f * PI;
		const float C = FMath::Cos(Ang);
		const float S = FMath::Sin(Ang);

		Vertices.Add(FVector(InnerRadiusCm * C, InnerRadiusCm * S, 0.0f));
		Vertices.Add(FVector(OuterRadiusCm * C, OuterRadiusCm * S, 0.0f));
		Normals.Add(FVector::UpVector);
		Normals.Add(FVector::UpVector);
		UVs.Add(FVector2D(static_cast<float>(i) / Segs, 0.0f));
		UVs.Add(FVector2D(static_cast<float>(i) / Segs, 1.0f));
		Colors.Add(VertexColor);
		Colors.Add(VertexColor);
	}

	for (int32 i = 0; i < Segs; ++i)
	{
		const int32 Next = (i + 1) % Segs;
		const int32 I0 = i * 2;
		const int32 O0 = i * 2 + 1;
		const int32 I1 = Next * 2;
		const int32 O1 = Next * 2 + 1;

		Triangles.Add(I0); Triangles.Add(O0); Triangles.Add(O1);
		Triangles.Add(I0); Triangles.Add(O1); Triangles.Add(I1);
	}

	TargetMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, Colors, TArray<FProcMeshTangent>(), false);
}

void AOnlineActivitiesActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority())
	{
		TickServerProximityRevives(DeltaSeconds);
	}

	if (!IsRunningDedicatedServer())
	{
		TickLocalStreetRace(DeltaSeconds);
		TickLocalDuelRingNotice();
	}
}

bool AOnlineActivitiesActor::IsLocationInsideDuelRing(const FVector& WorldLocation) const
{
	const float RingRadiusCm = static_cast<float>(KomorebiCore::kDuelRingConfig.Radius * KomorebiCore::kMetersToCm);
	const float DistSq2D = FVector::DistSquared2D(WorldLocation, DuelRingCenterUE);
	return DistSq2D < (RingRadiusCm * RingRadiusCm);
}

FVector AOnlineActivitiesActor::GetRaceGateLocation(int32 GateIndex) const
{
	return RaceGateLocationsUE.IsValidIndex(GateIndex) ? RaceGateLocationsUE[GateIndex] : FVector::ZeroVector;
}

void AOnlineActivitiesActor::TickLocalStreetRace(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	AKomorebiPlayerController* LocalPC = World ? Cast<AKomorebiPlayerController>(World->GetFirstPlayerController()) : nullptr;
	AKomorebiPlayerState* LocalPS = LocalPC ? LocalPC->GetPlayerState<AKomorebiPlayerState>() : nullptr;
	APawn* LocalPawn = LocalPC ? LocalPC->GetPawn() : nullptr;
	if (!LocalPC || !LocalPS || !LocalPawn || RaceGateLocationsUE.Num() == 0)
	{
		return;
	}

	const FVector PlayerLoc = LocalPawn->GetActorLocation();
	const bool bDriving = LocalPS->bIsDrivingVehicle;
	const float GateRadiusCm = static_cast<float>(KomorebiCore::kRaceConfig.Radius * KomorebiCore::kMetersToCm);
	const float GateRadiusSq = GateRadiusCm * GateRadiusCm;

	AKomorebiHUD* HUD = Cast<AKomorebiHUD>(LocalPC->GetHUD());
	AKomorebiGameState* GS = World->GetGameState<AKomorebiGameState>();

	if (!LocalRaceProgress.bActive)
	{
		if (bDriving && FVector::DistSquared2D(PlayerLoc, RaceStartLocationUE) < GateRadiusSq)
		{
			LocalRaceProgress.bActive = true;
			LocalRaceProgress.CurrentGateIndex = 0;
			LocalRaceProgress.ElapsedSeconds = 0.0f;
			LocalRaceProgress.OutOfVehicleSeconds = 0.0f;

			if (HUD)
			{
				const float Best = GS ? GS->BestRaceTimeSeconds : 0.0f;
				const FString BestSuffix = (Best > 0.0f) ? FString::Printf(TEXT(" · room best %.1fs"), Best) : TEXT("");
				HUD->EnqueueToast(FString::Printf(TEXT("Street race · %d gates%s"), RaceGateLocationsUE.Num(), *BestSuffix));
			}
		}
	}
	else
	{
		LocalRaceProgress.ElapsedSeconds += DeltaSeconds;
		LocalRaceProgress.OutOfVehicleSeconds = bDriving ? 0.0f : (LocalRaceProgress.OutOfVehicleSeconds + DeltaSeconds);

		const FVector TargetGate = RaceGateLocationsUE[LocalRaceProgress.CurrentGateIndex];
		if (bDriving && FVector::DistSquared2D(PlayerLoc, TargetGate) < GateRadiusSq)
		{
			++LocalRaceProgress.CurrentGateIndex;
		}

		if (LocalRaceProgress.CurrentGateIndex >= RaceGateLocationsUE.Num())
		{
			const float FinalTime = LocalRaceProgress.ElapsedSeconds;
			LocalRaceProgress = FKomorebiRaceProgress{};
			LocalPC->ServerSubmitRaceTime(FinalTime);
		}
		else if (LocalRaceProgress.OutOfVehicleSeconds > 4.0f ||
			LocalRaceProgress.ElapsedSeconds > static_cast<float>(KomorebiCore::kRaceConfig.LimitSeconds))
		{
			LocalRaceProgress = FKomorebiRaceProgress{};
			if (HUD)
			{
				HUD->EnqueueToast(TEXT("Race abandoned"));
			}
		}
	}

	if (StartGateMesh)
	{
		StartGateMesh->SetVisibility(!LocalRaceProgress.bActive);
	}
	if (ActiveGateMesh)
	{
		const bool bShowGate = LocalRaceProgress.bActive && RaceGateLocationsUE.IsValidIndex(LocalRaceProgress.CurrentGateIndex);
		ActiveGateMesh->SetVisibility(bShowGate);
		if (bShowGate)
		{
			ActiveGateMesh->SetWorldLocationAndRotation(
				RaceGateLocationsUE[LocalRaceProgress.CurrentGateIndex],
				RaceGateRotationsUE[LocalRaceProgress.CurrentGateIndex]);
		}
	}
}

void AOnlineActivitiesActor::TickLocalDuelRingNotice()
{
	UWorld* World = GetWorld();
	AKomorebiPlayerController* LocalPC = World ? Cast<AKomorebiPlayerController>(World->GetFirstPlayerController()) : nullptr;
	APawn* LocalPawn = LocalPC ? LocalPC->GetPawn() : nullptr;
	if (!LocalPC || !LocalPawn)
	{
		return;
	}

	const bool bInRing = IsLocationInsideDuelRing(LocalPawn->GetActorLocation());
	if (bInRing && !bWasLocalPlayerInDuelRing)
	{
		if (AKomorebiHUD* HUD = Cast<AKomorebiHUD>(LocalPC->GetHUD()))
		{
			HUD->EnqueueToast(TEXT("Duel ring · blades hit other players in here"));
		}
	}
	bWasLocalPlayerInDuelRing = bInRing;
}

void AOnlineActivitiesActor::TickServerProximityRevives(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	AKomorebiGameState* GS = World ? World->GetGameState<AKomorebiGameState>() : nullptr;
	if (!GS)
	{
		return;
	}

	const float ReviveRadiusCm = static_cast<float>(KomorebiCore::kReviveRadius * KomorebiCore::kMetersToCm);
	const float ReviveRadiusSq = ReviveRadiusCm * ReviveRadiusCm;

	for (APlayerState* BasePS : GS->PlayerArray)
	{
		AKomorebiPlayerState* DownedPS = Cast<AKomorebiPlayerState>(BasePS);
		if (!DownedPS || !DownedPS->bIsDowned)
		{
			ActiveReviveNotified.Remove(DownedPS);
			continue;
		}

		APawn* DownedPawn = DownedPS->GetPawn();
		if (!DownedPawn)
		{
			continue;
		}

		AKomorebiPlayerState* ClosestRescuer = nullptr;
		float BestDistSq = ReviveRadiusSq;

		for (APlayerState* OtherBase : GS->PlayerArray)
		{
			AKomorebiPlayerState* CandidatePS = Cast<AKomorebiPlayerState>(OtherBase);
			if (!CandidatePS || CandidatePS == DownedPS || CandidatePS->bIsDowned || CandidatePS->bIsDrivingVehicle)
			{
				continue;
			}
			APawn* CandidatePawn = CandidatePS->GetPawn();
			if (!CandidatePawn)
			{
				continue;
			}

			const float DistSq = FVector::DistSquared2D(DownedPawn->GetActorLocation(), CandidatePawn->GetActorLocation());
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				ClosestRescuer = CandidatePS;
			}
		}

		if (ClosestRescuer)
		{
			if (!ActiveReviveNotified.FindRef(DownedPS))
			{
				ActiveReviveNotified.Add(DownedPS, true);
				if (AKomorebiPlayerController* RescuerPC = Cast<AKomorebiPlayerController>(ClosestRescuer->GetOwner()))
				{
					RescuerPC->ClientShowFlashText(FString::Printf(TEXT("Reviving %s"), *DownedPS->DisplayName));
				}
			}

			DownedPS->ReviveProgress += DeltaSeconds;
			if (DownedPS->ReviveProgress >= static_cast<float>(KomorebiCore::kReviveTime))
			{
				DownedPS->CompleteRevive();
				ClosestRescuer->IncrementOnlineStat(TEXT("revives"));
				ActiveReviveNotified.Remove(DownedPS);

				if (AKomorebiPlayerController* DownedPC = Cast<AKomorebiPlayerController>(DownedPS->GetOwner()))
				{
					DownedPC->ClientShowToast(FString::Printf(TEXT("%s revived you"), *ClosestRescuer->DisplayName));
				}
				if (AKomorebiPlayerController* RescuerPC = Cast<AKomorebiPlayerController>(ClosestRescuer->GetOwner()))
				{
					RescuerPC->ClientShowToast(FString::Printf(TEXT("You revived %s"), *DownedPS->DisplayName));
				}
			}
		}
		else
		{
			DownedPS->ReviveProgress = 0.0f;
			ActiveReviveNotified.Remove(DownedPS);
		}
	}
}

void AOnlineActivitiesActor::TryServerManualRevive(AKomorebiPlayerController* RescuerPC, AKomorebiPlayerState* DownedPS)
{
	if (!HasAuthority() || !RescuerPC || !DownedPS || !DownedPS->bIsDowned)
	{
		return;
	}

	AKomorebiPlayerState* RescuerPS = RescuerPC->GetPlayerState<AKomorebiPlayerState>();
	APawn* RescuerPawn = RescuerPC->GetPawn();
	APawn* DownedPawn = DownedPS->GetPawn();
	if (!RescuerPS || !RescuerPawn || !DownedPawn || RescuerPS == DownedPS || RescuerPS->bIsDowned || RescuerPS->bIsDrivingVehicle)
	{
		return;
	}

	const float ReviveRadiusCm = static_cast<float>(KomorebiCore::kReviveRadius * KomorebiCore::kMetersToCm);
	if (FVector::Dist2D(RescuerPawn->GetActorLocation(), DownedPawn->GetActorLocation()) > ReviveRadiusCm)
	{
		return;
	}

	// Proximity monitor automatically ticks ReviveProgress to 2.0s; manual call notifies the rescuer.
	RescuerPC->ClientShowFlashText(FString::Printf(TEXT("Reviving %s"), *DownedPS->DisplayName));
}

void AOnlineActivitiesActor::ProcessServerPvPHit(
	AKomorebiPlayerController* AttackerPC,
	AKomorebiPlayerState* VictimPS,
	EKomorebiAttackType AttackType,
	const FVector& AttackerLocation)
{
	if (!HasAuthority() || !AttackerPC || !VictimPS)
	{
		return;
	}

	UWorld* World = GetWorld();
	AKomorebiPlayerState* AttackerPS = AttackerPC->GetPlayerState<AKomorebiPlayerState>();
	APawn* AttackerPawn = AttackerPC->GetPawn();
	APawn* VictimPawn = VictimPS->GetPawn();
	if (!World || !AttackerPS || !AttackerPawn || !VictimPawn || AttackerPS == VictimPS)
	{
		return;
	}

	// Strictly gated: both players must be on foot, standing, and inside the 9.0m Duel Ring.
	if (AttackerPS->bIsDowned || AttackerPS->bIsDrivingVehicle ||
		VictimPS->bIsDowned || VictimPS->bIsDrivingVehicle)
	{
		return;
	}

	if (VictimPS->IsInvulnerableNow(World->GetTimeSeconds()))
	{
		return;
	}

	const FVector ServerAttackerLoc = AttackerPawn->GetActorLocation();
	const FVector ServerVictimLoc = VictimPawn->GetActorLocation();
	if (!IsLocationInsideDuelRing(ServerAttackerLoc) || !IsLocationInsideDuelRing(ServerVictimLoc))
	{
		return;
	}

	const KomorebiCore::AttackSpec& Spec = KomorebiCore::GetAttackSpec(KomorebiBridge::ToCoreAttackType(AttackType));
	const float MaxReachCm = static_cast<float>((Spec.Reach + 1.5) * KomorebiCore::kMetersToCm);
	if (FVector::Dist2D(ServerAttackerLoc, ServerVictimLoc) > MaxReachCm)
	{
		return;
	}

	const int32 RawDamage = KomorebiCore::kDuelRingConfig.GetDamage(KomorebiBridge::ToCoreAttackType(AttackType));
	const float Damage = static_cast<float>(FMath::Clamp(RawDamage, 0, 40));
	VictimPS->PlayerHealth = FMath::Max(0.0f, VictimPS->PlayerHealth - Damage);

	if (VictimPS->PlayerHealth <= 0.0f)
	{
		VictimPS->EnterDownedState();
		AttackerPS->IncrementOnlineStat(TEXT("duels"));

		if (AKomorebiPlayerController* VictimPC = Cast<AKomorebiPlayerController>(VictimPS->GetOwner()))
		{
			VictimPC->ClientShowToast(FString::Printf(TEXT("%s won the duel"), *AttackerPS->DisplayName));
		}

		if (AKomorebiGameState* GS = World->GetGameState<AKomorebiGameState>())
		{
			GS->MulticastDuelOutcome(AttackerPS->DisplayName, VictimPS->DisplayName, AttackerPS->GetPlayerId());
		}
	}
}

FString AOnlineActivitiesActor::BuildOnlineObjectiveTaskText(
	const AKomorebiPlayerState* LocalPS,
	const FVector& PlayerWorldLocation) const
{
	if (LocalPS && LocalPS->bIsDowned)
	{
		return FString::Printf(TEXT("Down · revive in reach · %ds"), FMath::CeilToInt(FMath::Max(0.0f, LocalPS->DownedTimeRemaining)));
	}

	const UWorld* World = GetWorld();
	const AKomorebiGameState* GS = World ? World->GetGameState<AKomorebiGameState>() : nullptr;

	if (LocalRaceProgress.bActive)
	{
		const float Best = GS ? GS->BestRaceTimeSeconds : 0.0f;
		const FString BestText = (Best > 0.0f) ? FString::Printf(TEXT(" · best %.1fs"), Best) : TEXT("");
		return FString::Printf(
			TEXT("Race · gate %d/%d · %.1fs%s"),
			LocalRaceProgress.CurrentGateIndex + 1,
			RaceGateLocationsUE.Num(),
			LocalRaceProgress.ElapsedSeconds,
			*BestText);
	}

	if (GS && GS->bSiegeMechActive)
	{
		const int32 DistM = FMath::RoundToInt(FVector::Dist2D(PlayerWorldLocation, GS->SiegeMechLocation) * 0.01f);
		return FString::Printf(
			TEXT("Siege mech · %d%% · %d m"),
			FMath::RoundToInt(FMath::Clamp(GS->SiegeMechHealthRatio, 0.0f, 1.0f) * 100.0f),
			DistM);
	}

	if (GS)
	{
		const auto& Levels = KomorebiCore::GetLevels();
		if (GS->ActiveContractSector >= 0 && GS->ActiveContractSector < static_cast<int32>(Levels.size()))
		{
			const KomorebiCore::LevelSpec& Lvl = Levels[static_cast<size_t>(GS->ActiveContractSector)];
			const FVector CheckpointUE = KomorebiBridge::ThreeXZToFVector(Lvl.Checkpoint.X, 0.0, Lvl.Checkpoint.Z);
			const int32 DistM = FMath::RoundToInt(FVector::Dist2D(PlayerWorldLocation, CheckpointUE) * 0.01f);
			const int32 TotalCrew = (GS->ActiveContractTargetCount > 0)
				? GS->ActiveContractTargetCount
				: static_cast<int32>(Lvl.Spots.size());
			const FString DistSuffix = (DistM > 30) ? FString::Printf(TEXT(" · %d m"), DistM) : TEXT("");
			return FString::Printf(
				TEXT("Contract · %s crew · %d/%d%s"),
				*KomorebiBridge::ToFString(Lvl.Name),
				FMath::Min(GS->ActiveContractKills, TotalCrew),
				TotalCrew,
				*DistSuffix);
		}
	}

	return TEXT("Roam with the room · race ring at the start");
}
