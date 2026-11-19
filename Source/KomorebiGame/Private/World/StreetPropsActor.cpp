// Copyright Komorebi Project. Service plaza street props and static colliders implementation.
#include "World/StreetPropsActor.h"

#include "Components/BoxComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/DuelStateMachine.h"
#include "Game/LevelsData.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "World/KomorebiTerrainActor.h"
#include "World/NeoKyotoCityActor.h"
#include "World/NeoKyotoTerrain.h"

namespace
{
	struct FPropShapeDef
	{
		const TCHAR* Name;
		const TCHAR* AssetPath;
		double HalfX;
		double HalfY;
		double HalfZ;
	};

	// Exact SHAPES half-extents (Three.js metres) from legacy-web/src/world/props.js lines 10-14.
	static const FPropShapeDef kPropShapes[6] = {
		{ TEXT("terminal"),     TEXT("/Game/Assets/Street/SM_terminal.SM_terminal"),         0.66, 1.17, 0.53 },
		{ TEXT("supply-crate"), TEXT("/Game/Assets/Street/SM_supply_crate.SM_supply_crate"), 0.85, 0.57, 0.55 },
		{ TEXT("power-core"),   TEXT("/Game/Assets/Street/SM_power_core.SM_power_core"),     0.51, 0.92, 0.50 },
		{ TEXT("beacon"),       TEXT("/Game/Assets/Street/SM_beacon.SM_beacon"),             0.75, 1.50, 0.70 },
		{ TEXT("barricade"),    TEXT("/Game/Assets/Street/SM_barricade.SM_barricade"),       1.22, 0.68, 0.44 },
		{ TEXT("noodle-stall"), TEXT("/Game/Assets/Street/SM_noodle_stall.SM_noodle_stall"), 1.40, 1.35, 0.95 },
	};

	struct FPlazaEntryDef
	{
		const TCHAR* Kind;
		double Dx;
		double Dz;
	};

	// Exact service plaza layout from legacy-web/src/world/props.js lines 32-33.
	static const FPlazaEntryDef kPlazaEntries[7] = {
		{ TEXT("terminal"),     8.5, -3.0 },
		{ TEXT("beacon"),      -8.5, -3.0 },
		{ TEXT("power-core"),  -8.5,  0.0 },
		{ TEXT("supply-crate"), 9.0,  1.0 },
		{ TEXT("supply-crate"), 11.0, 1.0 },
		{ TEXT("barricade"),   -10.0, 3.0 },
		{ TEXT("noodle-stall"), 11.5, -5.5 },
	};

	const FPropShapeDef* FindPropShape(const FName& Kind)
	{
		for (const FPropShapeDef& Def : kPropShapes)
		{
			if (Kind == FName(Def.Name))
			{
				return &Def;
			}
		}
		return nullptr;
	}
}

AStreetPropsActor::AStreetPropsActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("StreetPropsRoot"));
	SetRootComponent(Root);
}

void AStreetPropsActor::BeginPlay()
{
	Super::BeginPlay();

	ResolveWorldReferences();
	LoadDefaultPropMeshes();
	RebuildProps();
}

void AStreetPropsActor::ResolveWorldReferences()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!TerrainActor)
	{
		for (TActorIterator<AKomorebiTerrainActor> It(World); It; ++It)
		{
			TerrainActor = *It;
			break;
		}
	}

	if (!CityActor)
	{
		for (TActorIterator<ANeoKyotoCityActor> It(World); It; ++It)
		{
			CityActor = *It;
			break;
		}
	}
}

void AStreetPropsActor::LoadDefaultPropMeshes()
{
	UStaticMesh* FallbackCube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	for (const FPropShapeDef& Def : kPropShapes)
	{
		const FName KindName(Def.Name);
		if (!PropMeshes.Contains(KindName) || !PropMeshes[KindName])
		{
			UStaticMesh* Loaded = LoadObject<UStaticMesh>(nullptr, Def.AssetPath);
			PropMeshes.Add(KindName, Loaded ? Loaded : FallbackCube);
		}
	}
}

void AStreetPropsActor::ClearSpawnedComponents()
{
	for (UBoxComponent* Box : StaticColliders)
	{
		if (Box)
		{
			Box->DestroyComponent();
		}
	}
	StaticColliders.Reset();

	for (USceneComponent* Comp : OwnedDynamicComponents)
	{
		if (Comp)
		{
			Comp->DestroyComponent();
		}
	}
	OwnedDynamicComponents.Reset();
	ServiceAreas.Reset();
	Placements.Reset();
}

double AStreetPropsActor::SampleHeight(double ThreeX, double ThreeZ) const
{
	if (TerrainActor && TerrainActor->IsTerrainPrepared())
	{
		return TerrainActor->GetHeightAtThree(ThreeX, ThreeZ);
	}
	return KomorebiCore::NeoKyotoRawHeight(ThreeX, ThreeZ, KomorebiCore::WayDist(ThreeX, ThreeZ));
}

void AStreetPropsActor::RebuildProps()
{
	ResolveWorldReferences();
	if (TerrainActor)
	{
		TerrainActor->EnsureTerrainBuilt();
	}
	LoadDefaultPropMeshes();
	ClearSpawnedComponents();

	// Ensure city solids exist if NeoKyotoCityActor hasn't built yet.
	if (CityActor && !CityActor->IsCityBuilt())
	{
		CityActor->RebuildCity();
	}

	const auto& Levels = KomorebiCore::GetLevels();
	const auto& Duels = KomorebiCore::GetDuelConfigs();
	const TArray<FKomorebiCitySolid>* CitySolids = CityActor ? &CityActor->GetSolidColliders() : nullptr;

	// Build fight & gate clearance list matching legacy-web/src/world/props.js line 20.
	TArray<KomorebiCore::Vec2> KeepClear;
	KeepClear.Reserve(96);
	for (const auto& Duel : Duels)
	{
		KeepClear.Add(KomorebiCore::Vec2{ Duel.X, Duel.Z });
	}
	for (const auto& Level : Levels)
	{
		for (const auto& Spot : Level.Spots)
		{
			KeepClear.Add(KomorebiCore::Vec2{ Spot.X, Spot.Z });
		}
		if (Level.Gate.has_value())
		{
			KeepClear.Add(*Level.Gate);
		}
	}

	TArray<FLocalCollider> PlacedColliders;
	PlacedColliders.Reserve(48);

	auto IsClear = [&](double X, double Z, double Radius) -> bool
	{
		if (KomorebiCore::WayDist(X, Z) < KomorebiCore::kRoad + Radius + 0.5)
		{
			return false;
		}
		for (const KomorebiCore::Vec2& Pt : KeepClear)
		{
			if (std::hypot(Pt.X - X, Pt.Z - Z) < Radius + 3.0)
			{
				return false;
			}
		}

		if (CitySolids)
		{
			for (const FKomorebiCitySolid& S : *CitySolids)
			{
				if (!S.bIsBox)
				{
					if (std::hypot(S.X - X, S.Z - Z) < Radius + S.Radius)
					{
						return false;
					}
				}
				else
				{
					const double Dx = X - S.X;
					const double Dz = Z - S.Z;
					const double CosR = std::cos(S.YawRad);
					const double SinR = std::sin(S.YawRad);
					if (std::abs(Dx * CosR - Dz * SinR) < S.BoxHalfExtents.X + Radius
						&& std::abs(Dx * SinR + Dz * CosR) < S.BoxHalfExtents.Z + Radius)
					{
						return false;
					}
				}
			}
		}

		for (const FLocalCollider& C : PlacedColliders)
		{
			const double Dx = X - C.X;
			const double Dz = Z - C.Z;
			const double CosR = std::cos(C.YawRad);
			const double SinR = std::sin(C.YawRad);
			if (std::abs(Dx * CosR - Dz * SinR) < C.BoxHalfExtents.X + Radius
				&& std::abs(Dx * SinR + Dz * CosR) < C.BoxHalfExtents.Z + Radius)
			{
				return false;
			}
		}

		return true;
	};

	static constexpr double kShiftOffsets[9] = { 0.0, 4.0, -4.0, 8.0, -8.0, 12.0, -12.0, 18.0, -18.0 };
	UStaticMesh* FallbackCube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));

	for (size_t LevelIdx = 0; LevelIdx < Levels.size(); ++LevelIdx)
	{
		const KomorebiCore::NamedPoint& Cp = Levels[LevelIdx].Checkpoint;

		FServiceArea Area;
		Area.CheckpointThreeX = Cp.X;
		Area.CheckpointThreeZ = Cp.Z;
		Area.bVisible = true;

		const FName AreaName(*FString::Printf(TEXT("ServiceArea_%d"), static_cast<int32>(LevelIdx + 1)));
		USceneComponent* AreaRoot = NewObject<USceneComponent>(this, AreaName);
		AreaRoot->SetupAttachment(GetRootComponent());
		AreaRoot->RegisterComponent();
		OwnedDynamicComponents.Add(AreaRoot);
		Area.AreaRoot = AreaRoot;

		TMap<FName, TArray<FTransform>> Bins;

		for (const FPlazaEntryDef& Entry : kPlazaEntries)
		{
			const FName KindName(Entry.Kind);
			const FPropShapeDef* Shape = FindPropShape(KindName);
			if (!Shape)
			{
				continue;
			}

			const double Radius = FMath::Max(Shape->HalfX, Shape->HalfZ);
			bool bPlaced = false;
			double FoundX = 0.0;
			double FoundZ = 0.0;
			for (const double Shift : kShiftOffsets)
			{
				const double CandidateX = Cp.X + Entry.Dx;
				const double CandidateZ = Cp.Z + Entry.Dz + Shift;
				if (IsClear(CandidateX, CandidateZ, Radius))
				{
					FoundX = CandidateX;
					FoundZ = CandidateZ;
					bPlaced = true;
					break;
				}
			}
			if (!bPlaced)
			{
				continue;
			}

			const double FoundY = SampleHeight(FoundX, FoundZ);
			const double ThreeYawRad = std::atan2(Cp.X - FoundX, Cp.Z - FoundZ);
			const double UEYawDeg = KomorebiCore::ThreeYawToUEYawDeg(ThreeYawRad);

			const FVector BaseLocUE = KomorebiBridge::ThreeXZToFVector(FoundX, FoundY, FoundZ);
			const FRotator RotUE(0.0f, static_cast<float>(UEYawDeg), 0.0f);

			UStaticMesh* MeshAsset = PropMeshes.FindRef(KindName);
			const bool bUsingFallbackCube = (!MeshAsset || MeshAsset == FallbackCube);

			// Fallback unit cube scales to exact box dimensions; authored street meshes already have true-scale cm dimensions.
			FTransform InstanceTransform;
			if (bUsingFallbackCube)
			{
				const FVector CenterUE = KomorebiBridge::ThreeXZToFVector(FoundX, FoundY + Shape->HalfY, FoundZ);
				const FVector CubeScale(
					static_cast<float>(Shape->HalfZ * 2.0),
					static_cast<float>(Shape->HalfX * 2.0),
					static_cast<float>(Shape->HalfY * 2.0));
				InstanceTransform = FTransform(RotUE, CenterUE, CubeScale);
			}
			else
			{
				InstanceTransform = FTransform(RotUE, BaseLocUE, FVector::OneVector);
			}

			Bins.FindOrAdd(KindName).Add(InstanceTransform);

			FKomorebiStreetPropPlacement Placement;
			Placement.PropKind = KindName;
			Placement.LevelIndex = static_cast<int32>(LevelIdx);
			Placement.WorldLocationUE = BaseLocUE;
			Placement.YawDegreesUE = static_cast<float>(UEYawDeg);
			Placements.Add(Placement);

			FLocalCollider Col;
			Col.X = FoundX;
			Col.Y = FoundY + Shape->HalfY;
			Col.Z = FoundZ;
			Col.BoxHalfExtents = FVector(Shape->HalfX, Shape->HalfY, Shape->HalfZ);
			Col.YawRad = ThreeYawRad;
			PlacedColliders.Add(Col);

			// Create static UBoxComponent collider matching exact oriented half-extents.
			const FName BoxName(*FString::Printf(
				TEXT("PropCol_%d_%s_%d"),
				static_cast<int32>(LevelIdx + 1),
				*KindName.ToString(),
				StaticColliders.Num()));
			UBoxComponent* BoxCol = NewObject<UBoxComponent>(this, BoxName);
			BoxCol->SetupAttachment(GetRootComponent());
			BoxCol->SetBoxExtent(FVector(
				static_cast<float>(Shape->HalfZ * KomorebiCore::kMetersToCm),
				static_cast<float>(Shape->HalfX * KomorebiCore::kMetersToCm),
				static_cast<float>(Shape->HalfY * KomorebiCore::kMetersToCm)));
			BoxCol->SetWorldLocationAndRotation(
				KomorebiBridge::ThreeXZToFVector(FoundX, FoundY + Shape->HalfY, FoundZ),
				RotUE);
			BoxCol->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			BoxCol->SetCollisionObjectType(ECC_WorldStatic);
			BoxCol->SetCollisionResponseToAllChannels(ECR_Block);
			BoxCol->SetCanEverAffectNavigation(true);
			BoxCol->SetHiddenInGame(true);
			BoxCol->RegisterComponent();
			StaticColliders.Add(BoxCol);
		}

		for (const auto& Pair : Bins)
		{
			if (Pair.Value.Num() == 0)
			{
				continue;
			}
			UStaticMesh* MeshAsset = PropMeshes.FindRef(Pair.Key);
			if (!MeshAsset)
			{
				continue;
			}

			const FName BatchName(*FString::Printf(
				TEXT("Area%d_%s_HISM"),
				static_cast<int32>(LevelIdx + 1),
				*Pair.Key.ToString()));
			UHierarchicalInstancedStaticMeshComponent* HISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, BatchName);
			HISM->SetupAttachment(AreaRoot);
			HISM->SetStaticMesh(MeshAsset);
			HISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			HISM->SetCastShadow(true);
			const int32 CullCm = FMath::RoundToInt(CullDistanceMeters * 100.0f);
			HISM->SetCullDistances(FMath::RoundToInt(CullCm * 0.85f), CullCm);
			HISM->RegisterComponent();
			HISM->AddInstances(Pair.Value, false, true);

			OwnedDynamicComponents.Add(HISM);
			Area.VisualBatches.Add(HISM);
		}

		ServiceAreas.Add(MoveTemp(Area));
	}
}

void AStreetPropsActor::SetCullDistanceMeters(float Meters)
{
	CullDistanceMeters = FMath::Clamp(Meters, 20.0f, 300.0f);
	const int32 CullCm = FMath::RoundToInt(CullDistanceMeters * 100.0f);

	for (FServiceArea& Area : ServiceAreas)
	{
		for (UHierarchicalInstancedStaticMeshComponent* Batch : Area.VisualBatches)
		{
			if (Batch)
			{
				Batch->SetCullDistances(FMath::RoundToInt(CullCm * 0.85f), CullCm);
			}
		}
	}
}

void AStreetPropsActor::UpdateVisibility(const FVector& FocusUE)
{
	const KomorebiCore::Vec3 FocusThree = KomorebiBridge::FVectorToThree(FocusUE);
	const double MaxDist = static_cast<double>(CullDistanceMeters);

	for (FServiceArea& Area : ServiceAreas)
	{
		const double Dist = std::hypot(Area.CheckpointThreeX - FocusThree.X, Area.CheckpointThreeZ - FocusThree.Z);
		const bool bShouldShow = Dist < MaxDist;
		if (Area.bVisible != bShouldShow)
		{
			Area.bVisible = bShouldShow;
			if (Area.AreaRoot)
			{
				Area.AreaRoot->SetVisibility(bShouldShow, true);
			}
		}
	}
}

void AStreetPropsActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bAutoTrackPlayerFocus || !GetWorld())
	{
		return;
	}

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (const APawn* Pawn = PC->GetPawn())
		{
			UpdateVisibility(Pawn->GetActorLocation());
		}
	}
}
