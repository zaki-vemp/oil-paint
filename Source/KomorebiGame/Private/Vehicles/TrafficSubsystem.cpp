// Copyright Komorebi Project. World subsystem managing parked fleet, autonomous yielding traffic, and pairwise vehicle collisions.
#include "Vehicles/TrafficSubsystem.h"

#include "Audio/KomorebiAudioSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/LevelsData.h"
#include "Game/VehiclePhysics.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "World/WorldGrid.h"

#include <cmath>
#include <limits>

DEFINE_LOG_CATEGORY_STATIC(LogKomorebiTraffic, Log, All);

void UTrafficSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UTrafficSubsystem::Deinitialize()
{
	ClearCityFleet();
	Super::Deinitialize();
}

void UTrafficSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (InWorld.IsGameWorld() && !bFleetSpawned)
	{
		SpawnCityFleet(true);
	}
}

bool UTrafficSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World && World->IsGameWorld() && !World->bIsTearingDown && ManagedVehicles.Num() > 0;
}

TStatId UTrafficSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTrafficSubsystem, STATGROUP_Tickables);
}

void UTrafficSubsystem::SetVehicleClassOverride(EKomorebiVehicleKind Kind, TSubclassOf<AKomorebiVehiclePawn> VehicleClass)
{
	if (VehicleClass)
	{
		VehicleClassOverrides.Add(Kind, VehicleClass);
	}
	else
	{
		VehicleClassOverrides.Remove(Kind);
	}
}

void UTrafficSubsystem::SetTerrainHeightProvider(TFunction<double(double, double)> InHeightProvider)
{
	CustomHeightProvider = MoveTemp(InHeightProvider);
	for (AKomorebiVehiclePawn* V : ManagedVehicles)
	{
		if (V)
		{
			V->SetTerrainHeightProvider(CustomHeightProvider);
		}
	}
}

double UTrafficSubsystem::SampleTerrainHeight(double ThreeX, double ThreeZ) const
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

// --- Fleet Spawning (1:1 Port of legacy-web/src/game/ride.js) ----------------------------------

void UTrafficSubsystem::SpawnCityFleet(bool bIncludeOuterGridParked)
{
	ClearCityFleet();

	if (!CustomHeightProvider && !FallbackTerrain.IsPrepared())
	{
		FallbackTerrain.Prepare();
	}

	const std::vector<KomorebiCore::Vec2>& Route = KomorebiCore::WaySamples();
	const std::vector<KomorebiCore::LevelSpec>& Levels = KomorebiCore::GetLevels();

	// 1. Two vehicles parked just past every checkpoint relay, rotating across all 5 vehicle classes (12 parked).
	static constexpr EKomorebiVehicleKind kParkedPairs[6][2] = {
		{ EKomorebiVehicleKind::Car,     EKomorebiVehicleKind::Bike },
		{ EKomorebiVehicleKind::Sports,  EKomorebiVehicleKind::Bicycle },
		{ EKomorebiVehicleKind::Pickup,  EKomorebiVehicleKind::Car },
		{ EKomorebiVehicleKind::Bike,    EKomorebiVehicleKind::Sports },
		{ EKomorebiVehicleKind::Bicycle, EKomorebiVehicleKind::Pickup },
		{ EKomorebiVehicleKind::Car,     EKomorebiVehicleKind::Bike },
	};

	const int32 RouteCount = static_cast<int32>(Route.size());
	if (RouteCount >= 2)
	{
		const int32 CheckpointCount = FMath::Min(6, static_cast<int32>(Levels.size()));
		for (int32 n = 0; n < CheckpointCount; ++n)
		{
			const KomorebiCore::NamedPoint& Cp = Levels[n].Checkpoint;
			int32 ClosestIdx = 0;
			double BestDist = std::hypot(Route[0].X - Cp.X, Route[0].Z - Cp.Z);
			for (int32 k = 1; k < RouteCount - 1; ++k)
			{
				const double D = std::hypot(Route[k].X - Cp.X, Route[k].Z - Cp.Z);
				if (D < BestDist)
				{
					BestDist = D;
					ClosestIdx = k;
				}
			}

			for (int32 k = 0; k < 2; ++k)
			{
				const int32 J = FMath::Min(RouteCount - 2, ClosestIdx + (k ? 2 : 5));
				const double Ax = Route[J].X;
				const double Az = Route[J].Z;
				const double Bx = Route[J + 1].X;
				const double Bz = Route[J + 1].Z;
				const double SegLen = std::max(1.0e-5, std::hypot(Bx - Ax, Bz - Az));
				const double Side = k ? -2.8 : 2.4;
				const double Px = Ax + (Bz - Az) / SegLen * Side;
				const double Pz = Az - (Bx - Ax) / SegLen * Side;
				const double Yaw = std::atan2(Bx - Ax, Bz - Az);
				SpawnVehicleInternal(kParkedPairs[n][k], Px, Pz, Yaw);
			}
		}
	}

	// 2. Outer grid parked fleet (6 extra vehicles on outer district avenues).
	if (bIncludeOuterGridParked)
	{
		struct FOuterPark { double X; double Z; EKomorebiVehicleKind Kind; double Yaw; };
		static constexpr FOuterPark kOuterParked[6] = {
			{ -132.0,  204.2, EKomorebiVehicleKind::Bike,    -KomorebiCore::kPi * 0.5 },
			{  132.0,  204.2, EKomorebiVehicleKind::Car,      KomorebiCore::kPi * 0.5 },
			{ -132.0,  124.2, EKomorebiVehicleKind::Bike,    -KomorebiCore::kPi * 0.5 },
			{  132.0,   44.2, EKomorebiVehicleKind::Bicycle,  KomorebiCore::kPi * 0.5 },
			{ -132.0,  -35.8, EKomorebiVehicleKind::Car,     -KomorebiCore::kPi * 0.5 },
			{  132.0, -115.8, EKomorebiVehicleKind::Bike,     KomorebiCore::kPi * 0.5 },
		};
		for (const FOuterPark& P : kOuterParked)
		{
			SpawnVehicleInternal(P.Kind, P.X, P.Z, P.Yaw);
		}
	}

	// 3. Six autonomous district traffic vehicles driving along right-hand lanes of grid avenues & streets.
	struct FTrafficBeat
	{
		EKomorebiVehicleKind Kind;
		bool bAxisX;
		double Line;
		int32 Dir;
		double At;
	};
	static constexpr FTrafficBeat kBeats[6] = {
		{ EKomorebiVehicleKind::Car,    true,  160.0,  1,  20.0 },
		{ EKomorebiVehicleKind::Sports, true,   80.0, -1, -24.0 },
		{ EKomorebiVehicleKind::Pickup, true,   40.0,  1,  56.0 },
		{ EKomorebiVehicleKind::Car,    true,    0.0, -1, -48.0 },
		{ EKomorebiVehicleKind::Sports, true,  -40.0,  1,  16.0 },
		{ EKomorebiVehicleKind::Pickup, false, 120.0, -1,  24.0 },
	};

	for (const FTrafficBeat& B : kBeats)
	{
		const double Side = 1.6 * (B.bAxisX ? static_cast<double>(-B.Dir) : static_cast<double>(B.Dir));
		const double X = B.bAxisX ? B.At : (B.Line + Side);
		const double Z = B.bAxisX ? (B.Line + Side) : B.At;
		const double Yaw = B.bAxisX
			? (B.Dir > 0 ? KomorebiCore::kPi * 0.5 : -KomorebiCore::kPi * 0.5)
			: (B.Dir > 0 ? 0.0 : KomorebiCore::kPi);

		if (AKomorebiVehiclePawn* V = SpawnVehicleInternal(B.Kind, X, Z, Yaw))
		{
			V->SetHasTrafficDriver(true);
			FTrafficAIState AI;
			AI.bAxisX = B.bAxisX;
			AI.Dir = B.Dir;
			AI.Line = B.Line;
			AI.CruiseSpeed = 8.0;
			AI.BeatStartAt = B.At;
			TrafficDrivers.Add(V, AI);
		}
	}

	bFleetSpawned = true;
}

void UTrafficSubsystem::ClearCityFleet()
{
	DrivenVehicle.Reset();
	TrafficDrivers.Empty();

	for (AKomorebiVehiclePawn* V : ManagedVehicles)
	{
		if (V && IsValid(V))
		{
			V->Destroy();
		}
	}
	ManagedVehicles.Empty();
	bFleetSpawned = false;
}

AKomorebiVehiclePawn* UTrafficSubsystem::SpawnVehicleInternal(
	EKomorebiVehicleKind Kind,
	double ThreeX,
	double ThreeZ,
	double ThreeYawRad)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	UClass* SpawnClass = AKomorebiVehiclePawn::StaticClass();
	if (const TSubclassOf<AKomorebiVehiclePawn>* Override = VehicleClassOverrides.Find(Kind))
	{
		if (*Override)
		{
			SpawnClass = Override->Get();
		}
	}

	const double GroundY = SampleTerrainHeight(ThreeX, ThreeZ);
	const FVector SpawnLoc = KomorebiBridge::ThreeXZToFVector(ThreeX, GroundY, ThreeZ);
	const FRotator SpawnRot(0.0f, static_cast<float>(KomorebiCore::ThreeYawToUEYawDeg(ThreeYawRad)), 0.0f);
	const FTransform SpawnTransform(SpawnRot, SpawnLoc);

	AKomorebiVehiclePawn* Vehicle = World->SpawnActorDeferred<AKomorebiVehiclePawn>(
		SpawnClass,
		SpawnTransform,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Vehicle)
	{
		UE_LOG(LogKomorebiTraffic, Warning, TEXT("Failed to spawn vehicle of kind %d"), static_cast<int32>(Kind));
		return nullptr;
	}

	Vehicle->VehicleKind = Kind;
	if (CustomHeightProvider)
	{
		Vehicle->SetTerrainHeightProvider(CustomHeightProvider);
	}
	else
	{
		Vehicle->SetTerrainHeightProvider([this](double X, double Z) { return SampleTerrainHeight(X, Z); });
	}
	Vehicle->FinishSpawning(SpawnTransform);
	if (!Vehicle->IsInitialized())
	{
		Vehicle->InitializeVehicle(Kind, SpawnLoc, static_cast<float>(ThreeYawRad));
	}
	Vehicle->SetNightPhase(CachedNightPhase);
	RegisterVehicle(Vehicle);
	return Vehicle;
}

void UTrafficSubsystem::RegisterVehicle(AKomorebiVehiclePawn* Vehicle)
{
	if (!Vehicle || ManagedVehicles.Contains(Vehicle))
	{
		return;
	}
	Vehicle->SetManagedByTrafficSubsystem(true);
	Vehicle->OnVehicleEntered.AddUniqueDynamic(this, &UTrafficSubsystem::HandleVehicleEntered);
	Vehicle->OnVehicleExited.AddUniqueDynamic(this, &UTrafficSubsystem::HandleVehicleExited);
	Vehicle->OnVehicleWrecked.AddUniqueDynamic(this, &UTrafficSubsystem::HandleVehicleWrecked);
	ManagedVehicles.Add(Vehicle);
}

void UTrafficSubsystem::UnregisterVehicle(AKomorebiVehiclePawn* Vehicle)
{
	if (!Vehicle)
	{
		return;
	}
	Vehicle->SetManagedByTrafficSubsystem(false);
	ManagedVehicles.Remove(Vehicle);
	TrafficDrivers.Remove(Vehicle);
	if (DrivenVehicle.Get() == Vehicle)
	{
		DrivenVehicle.Reset();
	}
}

void UTrafficSubsystem::SetFleetNightPhase(float Night01)
{
	CachedNightPhase = FMath::Clamp(Night01, 0.0f, 1.0f);
	for (AKomorebiVehiclePawn* V : ManagedVehicles)
	{
		if (V && IsValid(V))
		{
			V->SetNightPhase(CachedNightPhase);
		}
	}
}

void UTrafficSubsystem::RevealAroundShowcaseCamera(FVector FocusWorldLocation, FVector CameraWorldLocation)
{
	const KomorebiCore::Vec3 F = KomorebiBridge::FVectorToThree(FocusWorldLocation);
	const KomorebiCore::Vec3 C = KomorebiBridge::FVectorToThree(CameraWorldLocation);

	for (AKomorebiVehiclePawn* V : ManagedVehicles)
	{
		if (!V || !IsValid(V))
		{
			continue;
		}
		const KomorebiCore::VehicleState& S = V->GetSimState();
		const double D = FMath::Min(std::hypot(S.X - F.X, S.Z - F.Z), std::hypot(S.X - C.X, S.Z - C.Z));
		if (D < 120.0)
		{
			V->SetActorHiddenInGame(false);
		}
	}
}

// --- Event Handlers (Carjack / AI Release / Wreck) ---------------------------------------------

void UTrafficSubsystem::HandleVehicleEntered(AKomorebiVehiclePawn* Vehicle, APawn* Driver, bool bWasCarjacked)
{
	DrivenVehicle = Vehicle;

	// Taking a traffic vehicle immediately removes its autonomous AI.
	const bool bHadAI = TrafficDrivers.Remove(Vehicle) > 0;
	if (bWasCarjacked || bHadAI)
	{
		OnVehicleCarjacked.Broadcast(Vehicle, Driver);
	}
}

void UTrafficSubsystem::HandleVehicleExited(AKomorebiVehiclePawn* Vehicle, APawn* /*Driver*/)
{
	if (DrivenVehicle.Get() == Vehicle)
	{
		DrivenVehicle.Reset();
	}
}

void UTrafficSubsystem::HandleVehicleWrecked(AKomorebiVehiclePawn* Vehicle)
{
	// Keep traffic entry paused for out-of-sight recycling; clear active driver flag on vehicle.
	if (Vehicle)
	{
		Vehicle->SetHasTrafficDriver(false);
	}
}

// --- Player Interaction Helpers ----------------------------------------------------------------

AKomorebiVehiclePawn* UTrafficSubsystem::FindNearestInteractableVehicle(FVector PlayerLocation, float MaxDistMeters) const
{
	if (DrivenVehicle.IsValid())
	{
		return nullptr;
	}

	const KomorebiCore::Vec3 PlayerThree = KomorebiBridge::FVectorToThree(PlayerLocation);
	double Best = static_cast<double>(MaxDistMeters);
	AKomorebiVehiclePawn* Nearest = nullptr;

	for (AKomorebiVehiclePawn* V : ManagedVehicles)
	{
		if (!V || !IsValid(V))
		{
			continue;
		}
		const KomorebiCore::VehicleState& S = V->GetSimState();
		const KomorebiCore::VehicleSpec& Spec = V->GetVehicleSpec();
		const double EdgeDist = std::hypot(S.X - PlayerThree.X, S.Z - PlayerThree.Z) - Spec.Wid * 0.5;
		if (EdgeDist < Best)
		{
			Best = EdgeDist;
			Nearest = V;
		}
	}
	return Nearest;
}

bool UTrafficSubsystem::TryPlayerInteractVehicle(
	APawn* PlayerPawn,
	bool bRoamMode,
	int32 ClearedSectors,
	float PlayerHp,
	FString& OutFeedbackMessage)
{
	OutFeedbackMessage.Reset();

	if (AKomorebiVehiclePawn* Active = DrivenVehicle.Get())
	{
		return Active->TryExitVehicle(false, OutFeedbackMessage);
	}

	if (!PlayerPawn || PlayerHp <= 0.0f)
	{
		return false;
	}

	AKomorebiVehiclePawn* Nearest = FindNearestInteractableVehicle(PlayerPawn->GetActorLocation(), 3.4f);
	if (!Nearest)
	{
		return false;
	}

	return Nearest->TryEnterVehicle(PlayerPawn, bRoamMode, ClearedSectors, OutFeedbackMessage);
}

TArray<AKomorebiVehiclePawn*> UTrafficSubsystem::GetAllVehicles() const
{
	TArray<AKomorebiVehiclePawn*> Out;
	Out.Reserve(ManagedVehicles.Num());
	for (AKomorebiVehiclePawn* V : ManagedVehicles)
	{
		if (V && IsValid(V))
		{
			Out.Add(V);
		}
	}
	return Out;
}

void UTrafficSubsystem::PopulateHUDState(FKomorebiHUDState& InOutHud, FVector PlayerLocation) const
{
	AKomorebiVehiclePawn* Active = DrivenVehicle.Get();
	AKomorebiVehiclePawn* Near = Active ? nullptr : FindNearestInteractableVehicle(PlayerLocation, 3.4f);
	AKomorebiVehiclePawn* Focus = Active ? Active : Near;

	if (!Focus)
	{
		InOutHud.bDriving = false;
		InOutHud.VehicleName.Reset();
		InOutHud.VehicleKmh = 0.0f;
		InOutHud.bVehiclePromptVisible = false;
		InOutHud.VehiclePromptText.Reset();
		return;
	}

	const FKomorebiVehicleTelemetry Tel = Focus->GetTelemetry();
	InOutHud.bDriving = (Active != nullptr);
	InOutHud.VehicleName = Tel.DisplayName;
	InOutHud.VehicleKmh = Tel.SpeedKmh;
	InOutHud.bVehiclePromptVisible = true;

	if (Active)
	{
		InOutHud.VehiclePromptText = FString::Printf(TEXT("G · Exit %s"), *Tel.DisplayName);
	}
	else if (Tel.DamageState == EKomorebiVehicleDamageState::Wrecked)
	{
		InOutHud.VehiclePromptText = FString::Printf(TEXT("%s (Wrecked)"), *Tel.DisplayName);
	}
	else if (Tel.bTipped)
	{
		InOutHud.VehiclePromptText = FString::Printf(TEXT("G · Right %s"), *Tel.DisplayName);
	}
	else
	{
		InOutHud.VehiclePromptText = FString::Printf(TEXT("G · Drive %s"), *Tel.DisplayName);
	}
}

// --- Autonomous Traffic AI & Recycling ---------------------------------------------------------

FVector UTrafficSubsystem::GetViewerWorldLocation() const
{
	if (DrivenVehicle.IsValid())
	{
		return DrivenVehicle->GetActorLocation();
	}
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (const APawn* Pawn = PC->GetPawn())
			{
				return Pawn->GetActorLocation();
			}
		}
	}
	return FVector::ZeroVector;
}

void UTrafficSubsystem::CollectNearbyPedestrians(TArray<FPedestrianSample>& OutSamples) const
{
	OutSamples.Reset();
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<APawn> It(World); It; ++It)
	{
		const APawn* Pawn = *It;
		if (!Pawn || Pawn->IsHidden() || Pawn->IsA<AKomorebiVehiclePawn>())
		{
			continue;
		}
		const KomorebiCore::Vec3 Three = KomorebiBridge::FVectorToThree(Pawn->GetActorLocation());
		const bool bIsPlayer = Pawn->IsPlayerControlled();
		OutSamples.Add(FPedestrianSample{ Three.X, Three.Z, bIsPlayer ? 0.6 : 0.45 });
	}
}

KomorebiCore::VehicleDriveInput UTrafficSubsystem::ComputeTrafficInput(
	AKomorebiVehiclePawn* Vehicle,
	FTrafficAIState& AI,
	double Dt,
	double PlayerThreeX,
	double PlayerThreeZ,
	const TArray<FPedestrianSample>& Pedestrians)
{
	const KomorebiCore::VehicleState& V = Vehicle->GetSimState();
	const KomorebiCore::VehicleSpec& Spec = Vehicle->GetVehicleSpec();
	const double Sp = std::hypot(V.Vx, V.Vz);
	const double Along = AI.bAxisX ? V.X : V.Z;

	// Turn around smoothly before the city street grid ends (ride.js line 304).
	if ((AI.Dir > 0 && Along > KomorebiCore::kGridMax - 16.0)
		|| (AI.Dir < 0 && Along < KomorebiCore::kGridMin + 16.0))
	{
		AI.Dir *= -1;
	}

	const double Ahead = 6.0 + Sp * 0.45;
	const double Side = 1.6 * (AI.bAxisX ? static_cast<double>(-AI.Dir) : static_cast<double>(AI.Dir));
	const double Tx = AI.bAxisX ? (V.X + static_cast<double>(AI.Dir) * Ahead) : (AI.Line + Side);
	const double Tz = AI.bAxisX ? (AI.Line + Side) : (V.Z + static_cast<double>(AI.Dir) * Ahead);

	const double Fx = std::sin(V.Yaw);
	const double Fz = std::cos(V.Yaw);
	double Angle = KomorebiCore::WrapAngle(std::atan2(Tx - V.X, Tz - V.Z) - V.Yaw);

	if (AI.FleeTimer > 0.0)
	{
		AI.FleeTimer = std::max(0.0, AI.FleeTimer - Dt);
		Angle += AI.SwerveSign * 0.55;
	}

	double Want = std::min(
		(AI.FleeTimer > 0.0) ? (AI.CruiseSpeed + 4.0) : AI.CruiseSpeed,
		(std::abs(Angle) > 0.6) ? 4.0 : std::numeric_limits<double>::infinity());

	// Intersection yielding: stop if another moving vehicle (< 6m) or pedestrian (< 5m) occupies the crossing.
	if (AI.FleeTimer <= 0.0)
	{
		const std::vector<double>& CrossLines = AI.bAxisX ? KomorebiCore::Avenues() : KomorebiCore::Streets();
		for (double CrossCoord : CrossLines)
		{
			const double DistAlong = (CrossCoord - Along) * static_cast<double>(AI.Dir);
			if (DistAlong > 1.0 && DistAlong < 9.0)
			{
				const double Ix = AI.bAxisX ? CrossCoord : AI.Line;
				const double Iz = AI.bAxisX ? AI.Line : CrossCoord;
				bool bBusy = false;

				for (const AKomorebiVehiclePawn* Other : ManagedVehicles)
				{
					if (!Other || Other == Vehicle) continue;
					const KomorebiCore::VehicleState& Os = Other->GetSimState();
					if (std::hypot(Os.X - Ix, Os.Z - Iz) < 6.0 && std::hypot(Os.Vx, Os.Vz) > 0.8)
					{
						bBusy = true;
						break;
					}
				}
				if (!bBusy)
				{
					for (const FPedestrianSample& Ped : Pedestrians)
					{
						if (std::hypot(Ped.ThreeX - Ix, Ped.ThreeZ - Iz) < 5.0)
						{
							bBusy = true;
							break;
						}
					}
				}
				if (bBusy)
				{
					Want = 0.0;
				}
				break;
			}
		}
	}

	// Stopping distance corridor check against pedestrians and other vehicles.
	const double Stopping = 3.0 + (Sp * Sp) / (2.0 * Spec.Brake) + Sp * 0.4;
	auto IsBlocked = [&](double Ox, double Oz, double Radius) {
		const double Dx = Ox - V.X;
		const double Dz = Oz - V.Z;
		const double Gap = Dx * Fx + Dz * Fz - Spec.Len * 0.5;
		return Gap > -0.5
			&& Gap < Stopping + Radius
			&& std::abs(Dx * Fz - Dz * Fx) < Spec.Wid * 0.5 + Radius + 0.5;
	};

	bool bStop = V.Tipped || Vehicle->IsWrecked();
	if (!bStop)
	{
		for (const FPedestrianSample& Ped : Pedestrians)
		{
			if (IsBlocked(Ped.ThreeX, Ped.ThreeZ, Ped.Radius))
			{
				bStop = true;
				break;
			}
		}
	}
	if (!bStop)
	{
		for (const AKomorebiVehiclePawn* Other : ManagedVehicles)
		{
			if (!Other || Other == Vehicle) continue;
			const KomorebiCore::VehicleState& Os = Other->GetSimState();
			if (IsBlocked(Os.X, Os.Z, Other->GetVehicleSpec().Wid * 0.5))
			{
				bStop = true;
				break;
			}
		}
	}
	if (bStop)
	{
		Want = 0.0;
	}

	AI.WaitTimer = std::max(0.0, AI.WaitTimer - Dt);
	if (AI.WaitTimer > 0.0)
	{
		Want = 0.0;
	}

	// Horn feedback after collision if within earshot (< 30m) of player.
	if (AI.HornTimer > 0.0)
	{
		AI.HornTimer = std::max(0.0, AI.HornTimer - Dt);
		if (!AI.bHonked && std::hypot(V.X - PlayerThreeX, V.Z - PlayerThreeZ) < 30.0)
		{
			AI.bHonked = true;
			OnFlashText.Broadcast(TEXT("Honk"));
			if (const UWorld* World = GetWorld())
			{
				if (UGameInstance* GI = World->GetGameInstance())
				{
					if (UKomorebiAudioSubsystem* Audio = GI->GetSubsystem<UKomorebiAudioSubsystem>())
					{
						Audio->PlaySfx(EKomorebiSfx::VehicleHorn);
					}
				}
			}
		}
	}

	// Track prolonged stalls for out-of-sight lane recycling.
	if (Sp < 0.25 && Want > 1.0)
	{
		AI.StuckTimer += Dt;
	}
	else
	{
		AI.StuckTimer = 0.0;
	}

	KomorebiCore::VehicleDriveInput Out;
	Out.Throttle = (Want == 0.0 && Sp < 0.7) ? 0.0
		: (Want > Sp + 0.3) ? 0.55
		: (Want < Sp - 0.1) ? -1.0
		: 0.15;
	Out.Steer = KomorebiCore::Clamp(-Angle * 2.4, -1.0, 1.0);
	Out.Handbrake = (Want == 0.0 && Sp < 0.7);
	return Out;
}

bool UTrafficSubsystem::TryRecycleTrafficOutOfSight(
	AKomorebiVehiclePawn* Vehicle,
	FTrafficAIState& AI,
	double PlayerThreeX,
	double PlayerThreeZ,
	const TArray<FPedestrianSample>& Pedestrians)
{
	if (!Vehicle || Vehicle == DrivenVehicle.Get() || Vehicle->IsRemoteDriven())
	{
		return false;
	}

	const KomorebiCore::VehicleState& V = Vehicle->GetSimState();
	const double DistToPlayer = std::hypot(V.X - PlayerThreeX, V.Z - PlayerThreeZ);
	const bool bNeedsRecycle = Vehicle->IsWrecked() || V.Tipped || AI.StuckTimer > 8.0;

	// Never pop a vehicle in or out while visible to the player.
	if (!bNeedsRecycle || DistToPlayer <= 110.0)
	{
		return false;
	}

	const double Side = 1.6 * (AI.bAxisX ? static_cast<double>(-AI.Dir) : static_cast<double>(AI.Dir));
	const double CandidateOffsets[4] = { AI.BeatStartAt, -140.0, 140.0, 0.0 };

	for (double AlongPos : CandidateOffsets)
	{
		const double Cx = AI.bAxisX ? AlongPos : (AI.Line + Side);
		const double Cz = AI.bAxisX ? (AI.Line + Side) : AlongPos;

		// Candidate respawn lane must also be out of sight and empty of other vehicles/pedestrians.
		if (std::hypot(Cx - PlayerThreeX, Cz - PlayerThreeZ) <= 115.0)
		{
			continue;
		}

		bool bLaneEmpty = true;
		for (const AKomorebiVehiclePawn* Other : ManagedVehicles)
		{
			if (!Other || Other == Vehicle) continue;
			const KomorebiCore::VehicleState& Os = Other->GetSimState();
			if (std::hypot(Os.X - Cx, Os.Z - Cz) < 14.0)
			{
				bLaneEmpty = false;
				break;
			}
		}
		if (!bLaneEmpty) continue;

		for (const FPedestrianSample& Ped : Pedestrians)
		{
			if (std::hypot(Ped.ThreeX - Cx, Ped.ThreeZ - Cz) < 10.0)
			{
				bLaneEmpty = false;
				break;
			}
		}
		if (!bLaneEmpty) continue;

		const double Yaw = AI.bAxisX
			? (AI.Dir > 0 ? KomorebiCore::kPi * 0.5 : -KomorebiCore::kPi * 0.5)
			: (AI.Dir > 0 ? 0.0 : KomorebiCore::kPi);

		Vehicle->TeleportThree(Cx, Cz, Yaw, true);
		Vehicle->SetHasTrafficDriver(true);
		AI.WaitTimer = 0.0;
		AI.FleeTimer = 0.0;
		AI.HornTimer = 0.0;
		AI.bHonked = false;
		AI.StuckTimer = 0.0;
		return true;
	}

	return false;
}

// --- Synchronized 120 Hz Substep Simulation Loop -----------------------------------------------

void UTrafficSubsystem::Tick(float DeltaTime)
{
	// Prune any destroyed vehicles safely.
	ManagedVehicles.RemoveAll([](const TObjectPtr<AKomorebiVehiclePawn>& V) { return !IsValid(V); });
	if (ManagedVehicles.IsEmpty())
	{
		return;
	}

	const float Dt = FMath::Clamp(DeltaTime, 0.0f, 0.1f);
	const FVector ViewerUE = GetViewerWorldLocation();
	const KomorebiCore::Vec3 ViewerThree = KomorebiBridge::FVectorToThree(ViewerUE);
	const double Px = ViewerThree.X;
	const double Pz = ViewerThree.Z;

	TArray<FPedestrianSample> Pedestrians;
	CollectNearbyPedestrians(Pedestrians);

	AKomorebiVehiclePawn* ActiveDriven = DrivenVehicle.Get();

	// 1. Restore abandoned parked vehicles (>60m from home and >110m from player) & recycle stuck/wrecked out-of-sight traffic.
	for (AKomorebiVehiclePawn* V : ManagedVehicles)
	{
		if (V == ActiveDriven || V->IsRemoteDriven())
		{
			continue;
		}
		if (FTrafficAIState* AI = TrafficDrivers.Find(V))
		{
			TryRecycleTrafficOutOfSight(V, *AI, Px, Pz, Pedestrians);
		}
		else
		{
			const KomorebiCore::VehicleState& S = V->GetSimState();
			const double DistHome = std::hypot(S.X - V->GetHomeThreeX(), S.Z - V->GetHomeThreeZ());
			const double DistPlayer = std::hypot(S.X - Px, S.Z - Pz);
			if (DistHome > 60.0 && DistPlayer > 110.0)
			{
				V->ResetToHome();
			}
		}
	}

	// 2. Small fixed substeps (120 Hz) prevent fast opposite-moving bikes or cars from tunnelling.
	const int32 SubCount = FMath::Max(1, FMath::CeilToInt(Dt * 120.0f));
	const double SubDt = static_cast<double>(Dt) / static_cast<double>(SubCount);

	for (int32 Step = 0; Step < SubCount; ++Step)
	{
		for (AKomorebiVehiclePawn* V : ManagedVehicles)
		{
			if (V->IsRemoteDriven() && V != ActiveDriven)
			{
				continue;
			}

			if (V == ActiveDriven)
			{
				const KomorebiCore::VehicleDriveInput DriveIn = V->GetDriveInput();
				V->StepPhysicsSubstep(SubDt, DriveIn, true);
			}
			else if (FTrafficAIState* AI = TrafficDrivers.Find(V))
			{
				const KomorebiCore::VehicleState& S = V->GetSimState();
				if (!V->IsWrecked() && std::hypot(S.X - Px, S.Z - Pz) < 110.0)
				{
					const KomorebiCore::VehicleDriveInput AIInput = ComputeTrafficInput(V, *AI, SubDt, Px, Pz, Pedestrians);
					V->StepPhysicsSubstep(SubDt, AIInput, false);
				}
			}
			else
			{
				const KomorebiCore::VehicleState& S = V->GetSimState();
				const bool bNeedsCoast = std::hypot(S.Vx, S.Vz) > 0.05
					|| S.Air > 0.0
					|| std::abs(S.RollRate) > 0.005
					|| std::abs(S.Y - S.Floor) > 0.003
					|| std::abs(S.Vy) > 0.015;
				if (bNeedsCoast)
				{
					V->StepPhysicsSubstep(SubDt, KomorebiCore::VehicleDriveInput{ 0.0, 0.0, true }, false);
				}
			}
		}

		// Pairwise OBB collisions between nearby vehicles with static wall separation correction.
		const int32 NumVehicles = ManagedVehicles.Num();
		for (int32 a = 0; a < NumVehicles; ++a)
		{
			AKomorebiVehiclePawn* Va = ManagedVehicles[a];
			for (int32 b = a + 1; b < NumVehicles; ++b)
			{
				AKomorebiVehiclePawn* Vb = ManagedVehicles[b];
				KomorebiCore::VehicleState& Sa = Va->GetSimState();
				KomorebiCore::VehicleState& Sb = Vb->GetSimState();

				const double PrevAx = Sa.X;
				const double PrevAz = Sa.Z;
				const double PrevBx = Sb.X;
				const double PrevBz = Sb.Z;

				const double Impact = KomorebiCore::CollideVehicles(Sa, Va->GetVehicleSpec(), Sb, Vb->GetVehicleSpec());
				Va->CorrectAfterCollision(PrevAx, PrevAz);
				Vb->CorrectAfterCollision(PrevBx, PrevBz);

				if (Impact > 3.0)
				{
					Va->ApplyHullDamage(static_cast<float>(Vb->GetVehicleSpec().Mass), static_cast<float>(Impact));
					Vb->ApplyHullDamage(static_cast<float>(Va->GetVehicleSpec().Mass), static_cast<float>(Impact));

					if (Impact > 6.0)
					{
						for (AKomorebiVehiclePawn* HitVeh : { Va, Vb })
						{
							if (FTrafficAIState* AI = TrafficDrivers.Find(HitVeh))
							{
								AI->WaitTimer = 0.35;
								AI->FleeTimer = 2.4;
								AI->SwerveSign = FMath::RandBool() ? 1.0 : -1.0;
								AI->HornTimer = 0.5;
								AI->bHonked = false;
							}
						}
					}

					if (Va == ActiveDriven || Vb == ActiveDriven)
					{
						AKomorebiVehiclePawn* Driven = (Va == ActiveDriven) ? Va : Vb;
						Driven->OnCameraJolt.Broadcast(Driven, static_cast<float>(std::min(0.22, Impact * 0.012)));

						if (const UWorld* World = GetWorld())
						{
							if (UGameInstance* GI = World->GetGameInstance())
							{
								if (UKomorebiAudioSubsystem* Audio = GI->GetSubsystem<UKomorebiAudioSubsystem>())
								{
									Audio->PlaySfx(EKomorebiSfx::VehicleCrash, static_cast<float>(Impact / 10.0));
								}
							}
						}

						if (Impact > 11.0)
						{
							const float CrashDmg = static_cast<float>(std::min(18.0, (Impact - 11.0) * 1.4));
							Driven->OnDriverCrashDamage.Broadcast(Driven, CrashDmg);
							if (APawn* Driver = Driven->GetDriverPawn())
							{
								UGameplayStatics::ApplyDamage(Driver, CrashDmg, Driven->GetController(), Driven, nullptr);
							}
						}

						if (Driven->GetVehicleSpec().ThrowRider && Impact > 20.0 && !Driven->IsWrecked())
						{
							Driven->BailOut(TEXT("Thrown"));
						}
					}
				}
			}
		}
	}

	// 3. Smooth interpolation for remote networked peers + visual pose sync for all vehicles.
	for (AKomorebiVehiclePawn* V : ManagedVehicles)
	{
		if (V->IsRemoteDriven() && V != ActiveDriven)
		{
			V->UpdateRemoteNetInterpolation(Dt);
		}
		V->SyncVisualPose(Dt, ViewerUE);
	}

	// 4. Push continuous RPM engine & skid state into the procedural DSP audio subsystem.
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UKomorebiAudioSubsystem* Audio = GI->GetSubsystem<UKomorebiAudioSubsystem>())
			{
				const bool bPaused = UGameplayStatics::IsGamePaused(World);
				if (ActiveDriven && IsValid(ActiveDriven))
				{
					const FKomorebiVehicleTelemetry Tel = ActiveDriven->GetTelemetry();
					Audio->UpdateRideAudio(
						true,
						Tel.Kind,
						Tel.SpeedMps,
						Tel.TopSpeedMps,
						Tel.Throttle,
						Tel.bSliding,
						Tel.bAirborne,
						bPaused);
				}
				else
				{
					Audio->UpdateRideAudio(false, EKomorebiVehicleKind::Car, 0.0f, 25.0f, 0.0f, false, false, bPaused);
				}
			}
		}
	}
}
