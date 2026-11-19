// Copyright Komorebi Project. Procedural Neo-Kyoto city architecture, foliage, and street-light field implementation.
#include "World/NeoKyotoCityActor.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Game/ChaptersData.h"
#include "Game/DuelStateMachine.h"
#include "Game/LevelsData.h"
#include "Game/StoryDatabase.h"
#include "Materials/MaterialInterface.h"
#include "World/DayNightAtmosphereActor.h"
#include "World/KomorebiNoise.h"
#include "World/KomorebiTerrainActor.h"
#include "World/NeoKyotoTerrain.h"
#include "World/WorldGrid.h"

namespace
{
	constexpr int32 kStreetLightMapSize = 512;
	constexpr double kTowerSinkMeters = 3.0;

	int32 LightAccentAt(double X, double Z)
	{
		const int64 Ix = static_cast<int64>(std::floor(X * 2.0 + 0.223));
		const int64 Iz = static_cast<int64>(std::floor(Z * 3.0 + 0.223));
		return static_cast<int32>((Ix + Iz) & 1);
	}

	void ConfigureBatchHISM(
		UHierarchicalInstancedStaticMeshComponent* HISM,
		USceneComponent* Parent,
		bool bEnableCollision,
		bool bCastShadow)
	{
		if (!HISM)
		{
			return;
		}
		HISM->SetupAttachment(Parent);
		HISM->SetMobility(EComponentMobility::Static);
		HISM->SetCastShadow(bCastShadow);
		HISM->bAffectDistanceFieldLighting = false;
		if (bEnableCollision)
		{
			HISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			HISM->SetCollisionObjectType(ECC_WorldStatic);
			HISM->SetCollisionResponseToAllChannels(ECR_Block);
		}
		else
		{
			HISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			HISM->SetGenerateOverlapEvents(false);
		}
	}

	FRotator ThreeEulerToUERotator(double RyRad, double RzRad)
	{
		// Three.js Y-up rotation +Ry maps to UE Z-up Yaw = -Ry.
		// Three.js Z-axis tilt +Rz maps to UE X-axis Roll = -Rz.
		return FRotator(
			0.0,
			-FMath::RadiansToDegrees(RyRad),
			-FMath::RadiansToDegrees(RzRad));
	}
}

ANeoKyotoCityActor::ANeoKyotoCityActor()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("CityRoot"));
	SetRootComponent(Root);

	TowersHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Towers"));
	SignsHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Signs"));
	LampsHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Lamps"));
	LampPolesHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("LampPoles"));
	SkybridgesHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Skybridges"));
	ToriiGatesHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("ToriiGates"));
	BroadleafTrunksHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("BroadleafTrunks"));
	BroadleafCanopiesHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("BroadleafCanopies"));
	PineTrunksHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("PineTrunks"));
	SprucePinesHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("SprucePines"));
	GraniteBouldersHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("GraniteBoulders"));
	GrassTuftsHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("GrassTufts"));
	CratesHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Crates"));

	ConfigureBatchHISM(TowersHISM, Root, true, true);
	ConfigureBatchHISM(SignsHISM, Root, false, false);
	ConfigureBatchHISM(LampsHISM, Root, false, false);
	ConfigureBatchHISM(LampPolesHISM, Root, false, true);
	ConfigureBatchHISM(SkybridgesHISM, Root, true, true);
	ConfigureBatchHISM(ToriiGatesHISM, Root, true, true);
	ConfigureBatchHISM(BroadleafTrunksHISM, Root, true, true);
	ConfigureBatchHISM(BroadleafCanopiesHISM, Root, false, true);
	ConfigureBatchHISM(PineTrunksHISM, Root, true, true);
	ConfigureBatchHISM(SprucePinesHISM, Root, false, true);
	ConfigureBatchHISM(GraniteBouldersHISM, Root, true, true);
	ConfigureBatchHISM(GrassTuftsHISM, Root, false, false);
	ConfigureBatchHISM(CratesHISM, Root, true, true);
}

void ANeoKyotoCityActor::BeginPlay()
{
	Super::BeginPlay();

	ResolveWorldReferences();
	AssignDefaultMeshesAndMaterials();
	RebuildCity();

	const KomorebiCore::Vec2 StartPt = KomorebiCore::Start();
	UpdateDistrictPalette(10.0f, StartPt.Z, 0, true);
}

void ANeoKyotoCityActor::ResolveWorldReferences()
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

	if (!AtmosphereActor)
	{
		for (TActorIterator<ADayNightAtmosphereActor> It(World); It; ++It)
		{
			AtmosphereActor = *It;
			break;
		}
	}
}

void ANeoKyotoCityActor::AssignDefaultMeshesAndMaterials()
{
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* ConeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));

	auto EnsureMesh = [](UHierarchicalInstancedStaticMeshComponent* Comp, UStaticMesh* Fallback)
	{
		if (Comp && !Comp->GetStaticMesh() && Fallback)
		{
			Comp->SetStaticMesh(Fallback);
		}
	};

	EnsureMesh(TowersHISM, CubeMesh);
	EnsureMesh(SignsHISM, CubeMesh);
	EnsureMesh(LampsHISM, CubeMesh);
	EnsureMesh(LampPolesHISM, CylinderMesh);
	EnsureMesh(SkybridgesHISM, CubeMesh);
	EnsureMesh(ToriiGatesHISM, CubeMesh);
	EnsureMesh(BroadleafTrunksHISM, CylinderMesh);
	EnsureMesh(BroadleafCanopiesHISM, SphereMesh);
	EnsureMesh(PineTrunksHISM, CylinderMesh);
	EnsureMesh(SprucePinesHISM, ConeMesh);
	EnsureMesh(GraniteBouldersHISM, SphereMesh);
	EnsureMesh(GrassTuftsHISM, ConeMesh);
	EnsureMesh(CratesHISM, CubeMesh);

	if (!BuildingMaterial)
	{
		BuildingMaterial = LoadObject<UMaterialInterface>(
			nullptr,
			TEXT("/Game/Materials/M_BuildingParallax.M_BuildingParallax"));
	}
	if (!SignalMaterial)
	{
		SignalMaterial = LoadObject<UMaterialInterface>(
			nullptr,
			TEXT("/Game/Materials/M_SignalEmissive.M_SignalEmissive"));
	}

	if (BuildingMaterial)
	{
		TowersHISM->SetMaterial(0, BuildingMaterial);
		SkybridgesHISM->SetMaterial(0, BuildingMaterial);
		ToriiGatesHISM->SetMaterial(0, BuildingMaterial);
	}
	if (SignalMaterial)
	{
		SignsHISM->SetMaterial(0, SignalMaterial);
		LampsHISM->SetMaterial(0, SignalMaterial);
	}
}

double ANeoKyotoCityActor::SampleHeight(double ThreeX, double ThreeZ) const
{
	if (TerrainActor && TerrainActor->IsTerrainPrepared())
	{
		return TerrainActor->GetHeightAtThree(ThreeX, ThreeZ);
	}
	return KomorebiCore::NeoKyotoRawHeight(ThreeX, ThreeZ, KomorebiCore::WayDist(ThreeX, ThreeZ));
}

FTransform ANeoKyotoCityActor::MakeBoxTransform(const FPlacementRecord& Rec) const
{
	const FVector Loc = KomorebiBridge::ThreeXZToFVector(Rec.X, Rec.Y, Rec.Z);
	const FRotator Rot = ThreeEulerToUERotator(Rec.Ry, Rec.Rz);
	// /Engine/BasicShapes/Cube is 100cm (1m); UE X aligns with Three Z, UE Y with Three X, UE Z with Three Y.
	const FVector Scale(static_cast<float>(Rec.SZ), static_cast<float>(Rec.SX), static_cast<float>(Rec.SY));
	return FTransform(Rot, Loc, Scale);
}

FTransform ANeoKyotoCityActor::MakeCylinderTransform(
	const FPlacementRecord& Rec,
	double RadiusScale,
	double CenterZOffsetFrac) const
{
	const double EffectiveY = Rec.Y + Rec.SY * CenterZOffsetFrac;
	const FVector Loc = KomorebiBridge::ThreeXZToFVector(Rec.X, EffectiveY, Rec.Z);
	const FRotator Rot = ThreeEulerToUERotator(Rec.Ry, Rec.Rz);
	// /Engine/BasicShapes/Cylinder has radius 50cm (0.5m) and height 100cm (1m).
	const FVector Scale(
		static_cast<float>(Rec.SZ * RadiusScale),
		static_cast<float>(Rec.SX * RadiusScale),
		static_cast<float>(Rec.SY));
	return FTransform(Rot, Loc, Scale);
}

FTransform ANeoKyotoCityActor::MakeSphereTransform(const FPlacementRecord& Rec) const
{
	const FVector Loc = KomorebiBridge::ThreeXZToFVector(Rec.X, Rec.Y, Rec.Z);
	const FRotator Rot = ThreeEulerToUERotator(Rec.Ry, Rec.Rz);
	// /Engine/BasicShapes/Sphere has radius 50cm (0.5m); Three.js unit sphere has radius 1.0m.
	const FVector Scale(
		static_cast<float>(Rec.SZ * 2.0),
		static_cast<float>(Rec.SX * 2.0),
		static_cast<float>(Rec.SY * 2.0));
	return FTransform(Rot, Loc, Scale);
}

void ANeoKyotoCityActor::RebuildCity()
{
	ResolveWorldReferences();
	if (TerrainActor)
	{
		TerrainActor->EnsureTerrainBuilt();
	}
	AssignDefaultMeshesAndMaterials();

	// Clear every batch before deterministic regeneration.
	UHierarchicalInstancedStaticMeshComponent* AllBatches[] = {
		TowersHISM, SignsHISM, LampsHISM, LampPolesHISM, SkybridgesHISM, ToriiGatesHISM,
		BroadleafTrunksHISM, BroadleafCanopiesHISM, PineTrunksHISM, SprucePinesHISM,
		GraniteBouldersHISM, GrassTuftsHISM, CratesHISM
	};
	for (UHierarchicalInstancedStaticMeshComponent* Batch : AllBatches)
	{
		if (Batch)
		{
			Batch->ClearInstances();
		}
	}
	SolidColliders.Reset();
	FullGrassTuftTransforms.Reset();

	// Build exact KeepClear list from legacy-web/src/world/anime.js line 593.
	const KomorebiCore::NamedPoint& Goal = KomorebiCore::GetGoal();
	const auto& Duels = KomorebiCore::GetDuelConfigs();
	const auto& ExtraKeep = KomorebiCore::GetExtraKeep();
	const auto& Levels = KomorebiCore::GetLevels();

	TArray<KomorebiCore::Vec2> KeepClear;
	KeepClear.Reserve(128);
	KeepClear.Add(KomorebiCore::Vec2{ Goal.X, Goal.Z });
	for (const auto& Duel : Duels)
	{
		KeepClear.Add(KomorebiCore::Vec2{ Duel.X, Duel.Z });
	}
	for (const auto& Pt : ExtraKeep)
	{
		KeepClear.Add(Pt);
	}
	for (const auto& Level : Levels)
	{
		KeepClear.Add(KomorebiCore::Vec2{ Level.Checkpoint.X, Level.Checkpoint.Z });
		for (const auto& Spot : Level.Spots)
		{
			KeepClear.Add(KomorebiCore::Vec2{ Spot.X, Spot.Z });
		}
		if (Level.Gate.has_value())
		{
			KeepClear.Add(*Level.Gate);
		}
	}

	auto IsNearKeep = [&KeepClear](double X, double Z, double MinDist) -> bool
	{
		for (const KomorebiCore::Vec2& Pt : KeepClear)
		{
			if (std::hypot(Pt.X - X, Pt.Z - Z) < MinDist)
			{
				return true;
			}
		}
		return false;
	};

	auto IsNearAnyGateBand = [&Levels](double Z, double HalfBand) -> bool
	{
		for (const auto& Level : Levels)
		{
			if (Level.Gate.has_value() && std::abs(Level.Gate->Z - Z) < HalfBand)
			{
				return true;
			}
		}
		return false;
	};

	KomorebiCore::Rng Rand(71029u);
	KomorebiCore::Rng CrownRand(91837u);
	static constexpr double kCapH[4] = { 28.0, 16.0, 18.0, 9.0 };

	TArray<FPlacementRecord> TowerRecords;
	TArray<FPlacementRecord> SignRecords;
	TArray<FPlacementRecord> PoleRecords;
	TArray<FPlacementRecord> LightRecords;
	TArray<FPlacementRecord> ToriiRecords;
	TArray<FPlacementRecord> SkybridgeRecords;

	// 1. Procedural building lots from KomorebiCore::BlockLots() (anime.js lines 598-638).
	const std::vector<KomorebiCore::BlockLot> Lots = KomorebiCore::BlockLots();
	TowerRecords.Reserve(static_cast<int32>(Lots.size()));

	for (const KomorebiCore::BlockLot& Lot : Lots)
	{
		const double X = Lot.X + (Rand() - 0.5) * 2.0;
		const double Z = Lot.Z + (Rand() - 0.5) * 2.0;
		const double W = 4.2 + Rand() * 2.4;
		const double D = 4.2 + Rand() * 2.4;
		const double Reach = FMath::Max(W, D) * 0.5;

		if (KomorebiCore::ForestAt(X, Z) > 0.25 || KomorebiCore::MountainHeight(X, Z) > 0.25)
		{
			continue;
		}
		const double Wd = KomorebiCore::WayDist(X, Z);
		if (Wd < KomorebiCore::kRoad + 3.2 + Reach || KomorebiCore::ClearingAt(X, Z) > 0.35)
		{
			continue;
		}
		if (IsNearKeep(X, Z, 16.0 + Reach) || IsNearAnyGateBand(Z, Reach + 4.0))
		{
			continue;
		}

		const double Y = SampleHeight(X, Z);
		const int32 ClampedZone = FMath::Clamp(Lot.Zone, 0, 3);
		double H = kCapH[ClampedZone] * (0.5 + Rand() * 0.55);
		if (Lot.bHeart)
		{
			H *= 1.35;
		}
		const double Ry = (Rand() - 0.5) * 0.1;

		const double CenterY = Y + (H - kTowerSinkMeters) * 0.5;
		TowerRecords.Add(FPlacementRecord{ X, CenterY, Z, W, H + kTowerSinkMeters, D, Ry, 0.0, Wd, ClampedZone });

		FKomorebiCitySolid BoxSolid;
		BoxSolid.X = X;
		BoxSolid.Y = CenterY;
		BoxSolid.Z = Z;
		BoxSolid.BoxHalfExtents = FVector(W * 0.5, (H + kTowerSinkMeters) * 0.5, D * 0.5);
		BoxSolid.YawRad = Ry;
		BoxSolid.bIsBox = true;
		SolidColliders.Add(BoxSolid);

		// Mounted HDR neon signboard on one facade of the tower.
		const int32 SideK = static_cast<int32>(std::floor(Rand() * 4.0));
		const bool bAlongX = (SideK % 2 == 0);
		const double OutDist = (SideK < 2 ? 1.0 : -1.0) * ((bAlongX ? W : D) * 0.5 + 0.16);
		const double Nx = bAlongX ? std::cos(Ry) : std::sin(Ry);
		const double Nz = bAlongX ? -std::sin(Ry) : std::cos(Ry);
		const double SignH = FMath::Min(H * 0.45, 2.2 + Rand() * 3.2);
		const double SignW = FMath::Min((bAlongX ? D : W) * 0.4, 0.8 + Rand() * 1.1);
		const double SignY = Y + SignH * 0.5 + 2.2 + Rand() * FMath::Max(0.0, H - SignH - 3.0);
		const double SignRy = bAlongX ? Ry : (Ry + KomorebiCore::kPi * 0.5);
		SignRecords.Add(FPlacementRecord{ X + Nx * OutDist, SignY, Z + Nz * OutDist, 0.2, SignH, SignW, SignRy, 0.0, Wd, ClampedZone });

		// Rooftop mechanical crown, perimeter neon rim, and comms spires on tall / downtown towers.
		if (Lot.bHeart || H > 15.5)
		{
			const double RoofY = Y + H;
			const double Cr = std::cos(Ry);
			const double Sr = std::sin(Ry);
			const double Cw = FMath::Min(W, D) * (0.30 + CrownRand() * 0.10);
			const double Ch = 1.1 + CrownRand() * 1.1;
			PoleRecords.Add(FPlacementRecord{ X, RoofY + Ch * 0.5, Z, Cw, Ch, Cw, Ry, 0.0, Wd, ClampedZone });

			const double RimW = W * 0.72;
			const double RimD = D * 0.72;
			const double RimY = RoofY + Ch * 0.88;
			for (const double Sign : { -1.0, 1.0 })
			{
				LightRecords.Add(FPlacementRecord{ X + Sr * (RimD * 0.5) * Sign, RimY, Z + Cr * (RimD * 0.5) * Sign, RimW, 0.16, 0.14, Ry, 0.0, Wd, ClampedZone });
				LightRecords.Add(FPlacementRecord{ X + Cr * (RimW * 0.5) * Sign, RimY, Z - Sr * (RimW * 0.5) * Sign, 0.14, 0.16, RimD, Ry, 0.0, Wd, ClampedZone });
			}

			const double SpireH = (Lot.bHeart ? 6.5 : 4.2) + CrownRand() * 4.5;
			const double Ox = (CrownRand() - 0.5) * W * 0.28;
			const double Oz = (CrownRand() - 0.5) * D * 0.28;
			const double Sx = X + Cr * Ox + Sr * Oz;
			const double Sz = Z - Sr * Ox + Cr * Oz;
			PoleRecords.Add(FPlacementRecord{ Sx, RoofY + Ch + SpireH * 0.28, Sz, 0.11, SpireH * 0.56, 0.11, 0.0, 0.0, Wd, ClampedZone });
			PoleRecords.Add(FPlacementRecord{ Sx, RoofY + Ch + SpireH * 0.76, Sz, 0.048, SpireH * 0.48, 0.048, 0.0, 0.0, Wd, ClampedZone });
			PoleRecords.Add(FPlacementRecord{ Sx, RoofY + Ch + SpireH * 0.58, Sz, 0.038, 1.35, 0.038, Ry, KomorebiCore::kPi * 0.5, Wd, ClampedZone });
			LightRecords.Add(FPlacementRecord{ Sx, RoofY + Ch + SpireH + 0.14, Sz, 0.22, 0.28, 0.22, Ry, 0.0, Wd, ClampedZone });
			LightRecords.Add(FPlacementRecord{ Sx, RoofY + Ch + SpireH * 0.58, Sz, 0.26, 0.14, 0.26, Ry, 0.0, Wd, ClampedZone });

			if (H > 21.0)
			{
				const double S2 = SpireH * 0.62;
				const double Sx2 = X - Cr * Ox * 0.85;
				const double Sz2 = Z + Sr * Ox * 0.85;
				PoleRecords.Add(FPlacementRecord{ Sx2, RoofY + Ch + S2 * 0.5, Sz2, 0.055, S2, 0.055, 0.0, 0.0, Wd, ClampedZone });
				LightRecords.Add(FPlacementRecord{ Sx2, RoofY + Ch + S2 + 0.12, Sz2, 0.18, 0.22, 0.18, Ry, 0.0, Wd, ClampedZone });
			}
		}
	}

	// 2. Eight Cyber-Torii street gates (anime.js lines 639-652).
	static constexpr double kToriiPts[8][2] = {
		{ -120.0, 180.0 }, { 120.0, 180.0 }, { -160.0, 60.0 }, { 160.0, 60.0 },
		{ -120.0, -60.0 }, { 120.0, -60.0 }, { -160.0, -140.0 }, { 120.0, -220.0 }
	};
	for (const auto& Pt : kToriiPts)
	{
		const double Gx = Pt[0];
		const double Gz = Pt[1];
		const double Gy = SampleHeight(Gx, Gz);
		for (const double S : { -1.0, 1.0 })
		{
			const double Px = Gx + S * 5.2;
			ToriiRecords.Add(FPlacementRecord{ Px, Gy + 3.6, Gz, 0.85, 8.2, 0.85, 0.0, 0.0, 6.0, 0 });
			FKomorebiCitySolid PillarSolid;
			PillarSolid.X = Px;
			PillarSolid.Y = Gy + 3.6;
			PillarSolid.Z = Gz;
			PillarSolid.BoxHalfExtents = FVector(0.45, 4.1, 0.45);
			PillarSolid.bIsBox = true;
			SolidColliders.Add(PillarSolid);
			LightRecords.Add(FPlacementRecord{ Px, Gy + 4.1, Gz + 0.46, 0.22, 6.2, 0.08, 0.0, 0.0, 6.0, 0 });
		}
		ToriiRecords.Add(FPlacementRecord{ Gx, Gy + 7.6, Gz, 13.6, 0.72, 1.05, 0.0, 0.0, 6.0, 0 });
		ToriiRecords.Add(FPlacementRecord{ Gx, Gy + 6.4, Gz, 11.4, 0.44, 0.7, 0.0, 0.0, 6.0, 0 });
		LightRecords.Add(FPlacementRecord{ Gx, Gy + 8.05, Gz, 13.2, 0.2, 1.15, 0.0, 0.0, 6.0, 0 });
		SignRecords.Add(FPlacementRecord{ Gx, Gy + 6.95, Gz + 0.52, 0.16, 1.1, 4.2, KomorebiCore::kPi * 0.5, 0.0, 6.0, 0 });
	}

	// 3. Six elevated skybridges spanning major avenues (anime.js lines 653-658).
	static constexpr double kSkybridges[6][3] = {
		{ -40.0, 180.0, 16.0 }, { 40.0, 180.0, 19.0 }, { -80.0, 180.0, 17.0 },
		{ 80.0, 180.0, 18.0 }, { -40.0, -60.0, 15.0 }, { 80.0, -140.0, 17.0 }
	};
	for (const auto& Br : kSkybridges)
	{
		const double Bx = Br[0];
		const double Bz = Br[1];
		const double By = SampleHeight(Bx, Bz) + Br[2];
		SkybridgeRecords.Add(FPlacementRecord{ Bx, By, Bz, 18.0, 2.1, 3.4, 0.0, 0.0, 14.0, 0 });
		LightRecords.Add(FPlacementRecord{ Bx, By - 0.8, Bz + 1.75, 17.4, 0.2, 0.16, 0.0, 0.0, 14.0, 0 });
		LightRecords.Add(FPlacementRecord{ Bx, By - 0.8, Bz - 1.75, 17.4, 0.2, 0.16, 0.0, 0.0, 14.0, 0 });
	}

	// 4. Amber Sanctuary monumental crossed Torii shrine at GOAL (anime.js lines 659-672).
	{
		const double Sy = SampleHeight(Goal.X, Goal.Z);
		for (int32 I = 0; I < 4; ++I)
		{
			const double Angle = static_cast<double>(I) * (KomorebiCore::kPi * 0.5) + (KomorebiCore::kPi * 0.25);
			const double Px = Goal.X + std::cos(Angle) * 16.5;
			const double Pz = Goal.Z + std::sin(Angle) * 16.5;
			const double Py = SampleHeight(Px, Pz);
			ToriiRecords.Add(FPlacementRecord{ Px, Py + 9.5, Pz, 1.6, 20.0, 1.6, Angle, 0.0, 2.0, 0 });

			FKomorebiCitySolid PillarSolid;
			PillarSolid.X = Px;
			PillarSolid.Y = Py + 9.5;
			PillarSolid.Z = Pz;
			PillarSolid.BoxHalfExtents = FVector(0.85, 10.0, 0.85);
			PillarSolid.YawRad = Angle;
			PillarSolid.bIsBox = true;
			SolidColliders.Add(PillarSolid);

			LightRecords.Add(FPlacementRecord{ Px - std::cos(Angle) * 0.85, Py + 10.0, Pz - std::sin(Angle) * 0.85, 0.25, 16.0, 0.25, Angle, 0.0, 2.0, 0 });
		}
		ToriiRecords.Add(FPlacementRecord{ Goal.X, Sy + 18.5, Goal.Z, 25.0, 1.1, 1.8, KomorebiCore::kPi * 0.25, 0.0, 2.0, 0 });
		ToriiRecords.Add(FPlacementRecord{ Goal.X, Sy + 18.5, Goal.Z, 25.0, 1.1, 1.8, -KomorebiCore::kPi * 0.25, 0.0, 2.0, 0 });
		LightRecords.Add(FPlacementRecord{ Goal.X, Sy + 19.3, Goal.Z, 24.0, 0.3, 1.9, KomorebiCore::kPi * 0.25, 0.0, 2.0, 0 });
		LightRecords.Add(FPlacementRecord{ Goal.X, Sy + 19.3, Goal.Z, 24.0, 0.3, 1.9, -KomorebiCore::kPi * 0.25, 0.0, 2.0, 0 });
	}

	// Sort towers street-nearest first for early-Z fill efficiency (exact AGENTS.md / anime.js line 674 invariant).
	TowerRecords.Sort([](const FPlacementRecord& A, const FPlacementRecord& B)
	{
		return A.WayDist < B.WayDist;
	});

	// 5. Streetlamps & illuminated road lane studs along spine, avenues, and streets (anime.js lines 680-723).
	const auto& Way = KomorebiCore::WaySamples();
	for (size_t I = 2; I + 1 < Way.size(); I += 2)
	{
		const double Ax = Way[I].X;
		const double Az = Way[I].Z;
		const double Bx = Way[I + 1].X;
		const double Bz = Way[I + 1].Z;
		const double Len = FMath::Max(1e-6, std::hypot(Bx - Ax, Bz - Az));
		const double Nx = (Bz - Az) / Len;
		const double Nz = -(Bx - Ax) / Len;

		for (const double S : { -1.0, 1.0 })
		{
			const double X = Ax + Nx * (KomorebiCore::kRoad - 0.5) * S;
			const double Z = Az + Nz * (KomorebiCore::kRoad - 0.5) * S;
			LightRecords.Add(FPlacementRecord{ X, SampleHeight(X, Z) + 0.03, Z, 0.12, 0.05, 0.6, std::atan2(Bx - Ax, Bz - Az), 0.0, 0.0, 0 });
		}
		if (I % 8 != 2)
		{
			continue;
		}
		const double S = (I % 16 == 2) ? 1.0 : -1.0;
		const double X = Ax + Nx * (KomorebiCore::kRoad + 3.0) * S;
		const double Z = Az + Nz * (KomorebiCore::kRoad + 3.0) * S;
		const double Y = SampleHeight(X, Z);
		PoleRecords.Add(FPlacementRecord{ X, Y + 2.1, Z, 0.07, 4.2, 0.07, 0.0, 0.0, 0.0, 0 });
		LightRecords.Add(FPlacementRecord{ X - Nx * 0.7 * S, Y + 4.2, Z - Nz * 0.7 * S, 1.5, 0.12, 0.3, std::atan2(-Nz, Nx), 0.0, 0.0, 0 });
	}

	const auto& Avenues = KomorebiCore::Avenues();
	const auto& Streets = KomorebiCore::Streets();
	auto IsCrossing = [&Avenues, &Streets](double X, double Z, bool bOnAvenue) -> bool
	{
		const auto& CrossAxis = bOnAvenue ? Streets : Avenues;
		const double Coord = bOnAvenue ? Z : X;
		for (const double V : CrossAxis)
		{
			if (std::abs(Coord - V) < 6.0)
			{
				return true;
			}
		}
		return false;
	};

	for (const double X : Avenues)
	{
		int32 N = 0;
		for (double Z = KomorebiCore::kGridMin; Z <= KomorebiCore::kGridMax; Z += 12.0, ++N)
		{
			if (IsCrossing(X, Z, true) || KomorebiCore::MountainHeight(X, Z) > 0.6)
			{
				continue;
			}
			for (const double Side : { -1.0, 1.0 })
			{
				const double Lx = X + (KomorebiCore::kRoad - 0.55) * Side;
				if (KomorebiCore::MountainHeight(Lx, Z) > 0.6)
				{
					continue;
				}
				LightRecords.Add(FPlacementRecord{ Lx, SampleHeight(Lx, Z) + 0.045, Z, 0.1, 0.04, 0.55, 0.0, 0.0, 0.0, 0 });
			}
			if (N % 3 != 0)
			{
				continue;
			}
			const double Side = (N % 6 == 0) ? 1.0 : -1.0;
			const double Lx = X + (KomorebiCore::kRoad + 2.4) * Side;
			if (KomorebiCore::MountainHeight(Lx, Z) > 0.6)
			{
				continue;
			}
			const double Y = SampleHeight(Lx, Z);
			PoleRecords.Add(FPlacementRecord{ Lx, Y + 2.1, Z, 0.07, 4.2, 0.07, 0.0, 0.0, 0.0, 0 });
			LightRecords.Add(FPlacementRecord{ Lx - 0.7 * Side, Y + 4.2, Z, 1.5, 0.12, 0.3, KomorebiCore::kPi * 0.5, 0.0, 0.0, 0 });
		}
	}

	for (const double Z : Streets)
	{
		int32 N = 0;
		for (double X = KomorebiCore::kGridMin; X <= KomorebiCore::kGridMax; X += 12.0, ++N)
		{
			if (IsCrossing(X, Z, false) || KomorebiCore::MountainHeight(X, Z) > 0.6)
			{
				continue;
			}
			for (const double Side : { -1.0, 1.0 })
			{
				const double Lz = Z + (KomorebiCore::kRoad - 0.55) * Side;
				if (KomorebiCore::MountainHeight(X, Lz) > 0.6)
				{
					continue;
				}
				LightRecords.Add(FPlacementRecord{ X, SampleHeight(X, Lz) + 0.045, Lz, 0.55, 0.04, 0.1, 0.0, 0.0, 0.0, 0 });
			}
			if (N % 3 != 0)
			{
				continue;
			}
			const double Side = (N % 6 == 0) ? 1.0 : -1.0;
			const double Lz = Z + (KomorebiCore::kRoad + 2.4) * Side;
			if (KomorebiCore::MountainHeight(X, Lz) > 0.6)
			{
				continue;
			}
			const double Y = SampleHeight(X, Lz);
			PoleRecords.Add(FPlacementRecord{ X, Y + 2.1, Lz, 0.07, 4.2, 0.07, 0.0, 0.0, 0.0, 0 });
			LightRecords.Add(FPlacementRecord{ X, Y + 4.2, Lz - 0.7 * Side, 0.3, 0.12, 1.5, 0.0, 0.0, 0.0, 0 });
		}
	}

	// 6. Broadleaf trees, alpine spruce pines, and granite boulders (anime.js lines 729-777).
	TArray<KomorebiCore::Vec2> PlacedTrees;
	PlacedTrees.Reserve(420);
	TArray<FPlacementRecord> BroadleafTrunkRecords;
	TArray<FPlacementRecord> BroadleafCanopyRecords;
	TArray<FPlacementRecord> PineTrunkRecords;
	TArray<FPlacementRecord> SprucePineRecords;
	TArray<FPlacementRecord> BoulderRecords;

	for (int32 I = 0; I < 3200 && PlacedTrees.Num() < 420; ++I)
	{
		const double X = -325.0 + Rand() * 650.0;
		const double Z = -325.0 + Rand() * 650.0;
		const double F = KomorebiCore::ForestAt(X, Z);
		const double Mh = KomorebiCore::MountainHeight(X, Z);
		if (F <= 0.35 && (Mh <= 0.3 || Mh >= 24.0))
		{
			continue;
		}
		if (KomorebiCore::WayDist(X, Z) < KomorebiCore::kRoad + 3.4 || KomorebiCore::ClearingAt(X, Z) > 0.25)
		{
			continue;
		}
		if (std::hypot(X + 20.0, Z - 305.0) < 28.0)
		{
			continue;
		}
		if (IsNearKeep(X, Z, 12.0) || IsNearAnyGateBand(Z, 4.5))
		{
			continue;
		}

		bool bNearTorii = false;
		for (const auto& Pt : kToriiPts)
		{
			if (std::hypot(Pt[0] - X, Pt[1] - Z) < 9.0)
			{
				bNearTorii = true;
				break;
			}
		}
		if (bNearTorii)
		{
			continue;
		}

		const double Y = SampleHeight(X, Z);
		const double Sx = (SampleHeight(X + 1.5, Z) - SampleHeight(X - 1.5, Z)) / 3.0;
		const double Sz = (SampleHeight(X, Z + 1.5) - SampleHeight(X, Z - 1.5)) / 3.0;
		if (std::hypot(Sx, Sz) > 0.75)
		{
			continue;
		}

		bool bTooCloseTree = false;
		for (const KomorebiCore::Vec2& T : PlacedTrees)
		{
			if (std::hypot(T.X - X, T.Z - Z) < 5.5)
			{
				bTooCloseTree = true;
				break;
			}
		}
		if (bTooCloseTree)
		{
			continue;
		}

		PlacedTrees.Add(KomorebiCore::Vec2{ X, Z });
		const bool bBroadleaf = (Mh < 3.5 && Rand() < 0.72);
		const double Th = bBroadleaf ? (5.2 + Rand() * 4.2) : (6.2 + Rand() * 5.2);
		const double Tr = bBroadleaf ? (0.30 + Rand() * 0.18) : (0.22 + Rand() * 0.15);
		const double Yaw = Rand() * 6.28;

		if (bBroadleaf)
		{
			BroadleafTrunkRecords.Add(FPlacementRecord{ X, Y - 0.15, Z, Tr, Th * 0.85, Tr, Yaw, 0.0, 0.0, 0 });
			const double Cs = 2.6 + Rand() * 1.7;
			BroadleafCanopyRecords.Add(FPlacementRecord{ X, Y + Th * 0.84, Z, Cs, Cs * 0.84, Cs, Yaw, 0.0, 0.0, 0 });
		}
		else
		{
			PineTrunkRecords.Add(FPlacementRecord{ X, Y + Th * 0.44 - 0.15, Z, Tr, Th * 0.88, Tr, Yaw, 0.0, 0.0, 0 });
			const double Pr = 2.15 + Rand() * 0.95;
			SprucePineRecords.Add(FPlacementRecord{ X, Y + Th * 0.56, Z, Pr, Th * 0.86, Pr, Yaw, 0.0, 0.0, 0 });
		}

		if (FMath::Max(std::abs(X), std::abs(Z)) < 305.0 && Y < 30.0)
		{
			FKomorebiCitySolid TrunkSolid;
			TrunkSolid.X = X;
			TrunkSolid.Y = Y;
			TrunkSolid.Z = Z;
			TrunkSolid.Radius = Tr * 1.05;
			TrunkSolid.Height = 4.5;
			TrunkSolid.bIsBox = false;
			SolidColliders.Add(TrunkSolid);
		}
	}

	for (int32 I = 0; I < 650 && BoulderRecords.Num() < 110; ++I)
	{
		const double X = -335.0 + Rand() * 670.0;
		const double Z = -335.0 + Rand() * 670.0;
		const double F = KomorebiCore::ForestAt(X, Z);
		const double Mh = KomorebiCore::MountainHeight(X, Z);
		if ((Mh <= 0.4 || Mh >= 32.0) && F <= 0.45)
		{
			continue;
		}
		if (KomorebiCore::WayDist(X, Z) < KomorebiCore::kRoad + 3.2 || KomorebiCore::ClearingAt(X, Z) > 0.25)
		{
			continue;
		}
		if (std::hypot(X + 20.0, Z - 305.0) < 28.0)
		{
			continue;
		}
		if (IsNearKeep(X, Z, 10.0) || IsNearAnyGateBand(Z, 4.5))
		{
			continue;
		}

		bool bTooCloseTree = false;
		for (const KomorebiCore::Vec2& T : PlacedTrees)
		{
			if (std::hypot(T.X - X, T.Z - Z) < 3.2)
			{
				bTooCloseTree = true;
				break;
			}
		}
		if (bTooCloseTree)
		{
			continue;
		}

		const double S = 0.9 + Rand() * 1.9;
		const double Y = SampleHeight(X, Z);
		BoulderRecords.Add(FPlacementRecord{ X, Y + S * 0.30, Z, S * 1.18, S * 0.76, S * 1.02, Rand() * 6.28, 0.0, 0.0, 0 });
		if (FMath::Max(std::abs(X), std::abs(Z)) < 305.0 && Y < 30.0)
		{
			FKomorebiCitySolid RockSolid;
			RockSolid.X = X;
			RockSolid.Y = Y + S * 0.2;
			RockSolid.Z = Z;
			RockSolid.Radius = S * 0.85;
			RockSolid.bIsRock = true;
			SolidColliders.Add(RockSolid);
		}
	}

	// 7. Woodland grass tufts and industrial street crates (anime.js lines 801-818).
	TArray<FPlacementRecord> CrateRecords;
	FullGrassTuftTransforms.Reserve(6500);
	for (int32 I = 0; I < 8500 && FullGrassTuftTransforms.Num() < 6500; ++I)
	{
		const double X = -325.0 + Rand() * 650.0;
		const double Z = -325.0 + Rand() * 650.0;
		const double Wd = KomorebiCore::WayDist(X, Z);
		const double F = KomorebiCore::ForestAt(X, Z);
		const double Mh = KomorebiCore::MountainHeight(X, Z);
		if (Wd < KomorebiCore::kRoad + 1.2 || Mh > 14.0 || F <= 0.38)
		{
			continue;
		}
		const double S = 0.5 + Rand() * 0.7;
		const double Y = SampleHeight(X, Z) - 0.015;
		const double Yaw = Rand() * 6.28;
		const FVector Loc = KomorebiBridge::ThreeXZToFVector(X, Y + 0.18 * S, Z);
		const FRotator Rot = ThreeEulerToUERotator(Yaw, 0.0);
		FullGrassTuftTransforms.Add(FTransform(Rot, Loc, FVector(0.32 * S, 0.32 * S, 0.42 * S)));
	}

	for (int32 I = 0; I < 700 && CrateRecords.Num() < 280; ++I)
	{
		const double X = KomorebiCore::kGridMin + Rand() * (KomorebiCore::kGridMax - KomorebiCore::kGridMin);
		const double Z = KomorebiCore::kGridMin + Rand() * (KomorebiCore::kGridMax - KomorebiCore::kGridMin);
		if (KomorebiCore::ForestAt(X, Z) > 0.3 || KomorebiCore::MountainHeight(X, Z) > 0.25)
		{
			continue;
		}
		if (KomorebiCore::WayDist(X, Z) < KomorebiCore::kRoad + 2.5)
		{
			continue;
		}
		if (IsNearKeep(X, Z, 8.0))
		{
			continue;
		}
		const double S = 0.5 + Rand() * 1.0;
		const double Y = SampleHeight(X, Z);
		CrateRecords.Add(FPlacementRecord{ X, Y + S * 0.4, Z, S * 1.5, S, S * 1.2, Rand() * 6.28, 0.0, 0.0, 0 });
		FKomorebiCitySolid CrateSolid;
		CrateSolid.X = X;
		CrateSolid.Y = Y + S * 0.15;
		CrateSolid.Z = Z;
		CrateSolid.Radius = S * 0.8;
		CrateSolid.bIsRock = true;
		SolidColliders.Add(CrateSolid);
	}

	// Upload batched HISM instance transforms in bulk (single BVH build per batch).
	auto PopulateBoxBatch = [this](UHierarchicalInstancedStaticMeshComponent* Batch, const TArray<FPlacementRecord>& Records)
	{
		if (!Batch || Records.Num() == 0)
		{
			return;
		}
		TArray<FTransform> Transforms;
		Transforms.Reserve(Records.Num());
		for (const FPlacementRecord& Rec : Records)
		{
			Transforms.Add(MakeBoxTransform(Rec));
		}
		Batch->AddInstances(Transforms, false, false);
	};

	PopulateBoxBatch(TowersHISM, TowerRecords);
	PopulateBoxBatch(SignsHISM, SignRecords);
	PopulateBoxBatch(LampsHISM, LightRecords);
	PopulateBoxBatch(SkybridgesHISM, SkybridgeRecords);
	PopulateBoxBatch(ToriiGatesHISM, ToriiRecords);
	PopulateBoxBatch(CratesHISM, CrateRecords);

	if (LampPolesHISM && PoleRecords.Num() > 0)
	{
		TArray<FTransform> Transforms;
		Transforms.Reserve(PoleRecords.Num());
		for (const FPlacementRecord& Rec : PoleRecords)
		{
			Transforms.Add(MakeCylinderTransform(Rec, 2.0, 0.0));
		}
		LampPolesHISM->AddInstances(Transforms, false, false);
	}

	if (BroadleafTrunksHISM && BroadleafTrunkRecords.Num() > 0)
	{
		TArray<FTransform> Transforms;
		Transforms.Reserve(BroadleafTrunkRecords.Num());
		for (const FPlacementRecord& Rec : BroadleafTrunkRecords)
		{
			Transforms.Add(MakeCylinderTransform(Rec, 2.0, 0.5));
		}
		BroadleafTrunksHISM->AddInstances(Transforms, false, false);
	}

	if (BroadleafCanopiesHISM && BroadleafCanopyRecords.Num() > 0)
	{
		TArray<FTransform> Transforms;
		Transforms.Reserve(BroadleafCanopyRecords.Num());
		for (const FPlacementRecord& Rec : BroadleafCanopyRecords)
		{
			Transforms.Add(MakeSphereTransform(Rec));
		}
		BroadleafCanopiesHISM->AddInstances(Transforms, false, false);
	}

	if (PineTrunksHISM && PineTrunkRecords.Num() > 0)
	{
		TArray<FTransform> Transforms;
		Transforms.Reserve(PineTrunkRecords.Num());
		for (const FPlacementRecord& Rec : PineTrunkRecords)
		{
			Transforms.Add(MakeCylinderTransform(Rec, 2.0, 0.0));
		}
		PineTrunksHISM->AddInstances(Transforms, false, false);
	}

	if (SprucePinesHISM && SprucePineRecords.Num() > 0)
	{
		TArray<FTransform> Transforms;
		Transforms.Reserve(SprucePineRecords.Num());
		for (const FPlacementRecord& Rec : SprucePineRecords)
		{
			Transforms.Add(MakeCylinderTransform(Rec, 2.0, 0.0));
		}
		SprucePinesHISM->AddInstances(Transforms, false, false);
	}

	if (GraniteBouldersHISM && BoulderRecords.Num() > 0)
	{
		TArray<FTransform> Transforms;
		Transforms.Reserve(BoulderRecords.Num());
		for (const FPlacementRecord& Rec : BoulderRecords)
		{
			Transforms.Add(MakeSphereTransform(Rec));
		}
		GraniteBouldersHISM->AddInstances(Transforms, false, false);
	}

	// Bake and upload 512x512 street-light radiance field to terrain.
	BakeAndUploadStreetLights(LightRecords, SignRecords);

	bCityBuilt = true;
	SetDecorationQuality(CurrentDecorationDensity);
}

void ANeoKyotoCityActor::BakeAndUploadStreetLights(
	const TArray<FPlacementRecord>& LightRecords,
	const TArray<FPlacementRecord>& SignRecords)
{
	constexpr int32 Size = kStreetLightMapSize;
	const double Step = (2.0 * KomorebiCore::kExtent) / static_cast<double>(Size);
	const auto& Chapters = KomorebiCore::GetChapters();
	if (Chapters.size() < 5)
	{
		return;
	}

	const KomorebiCore::ChapterSpec* ZonePaint[4] = {
		&Chapters[static_cast<size_t>(KomorebiCore::Zones[0].Chapter)],
		&Chapters[static_cast<size_t>(KomorebiCore::Zones[1].Chapter)],
		&Chapters[static_cast<size_t>(KomorebiCore::Zones[2].Chapter)],
		&Chapters[static_cast<size_t>(KomorebiCore::Zones[3].Chapter)],
	};

	BakedStreetLights.SetNumZeroed(Size * Size);
	for (FColor& Px : BakedStreetLights)
	{
		Px.A = 255;
	}

	auto Stamp = [&](const FPlacementRecord& P, double Radius, double Strength, int32 Accent)
	{
		const double North = KomorebiCore::SmoothStep(-6.0, 6.0, P.Z - KomorebiCore::ZoneSplit.Downtown);
		const double Market = KomorebiCore::SmoothStep(-6.0, 6.0, P.Z - KomorebiCore::ZoneSplit.Market);
		const double East = KomorebiCore::SmoothStep(-6.0, 6.0, P.X - KomorebiCore::ZoneSplit.East);
		const double South = (1.0 - North) * (1.0 - Market);
		const double Weights[4] = { North, (1.0 - North) * Market, South * East, South * (1.0 - East) };

		double ColR = 0.0;
		double ColG = 0.0;
		double ColB = 0.0;
		for (int32 I = 0; I < 4; ++I)
		{
			const KomorebiCore::RGB& Palette = Accent ? ZonePaint[I]->LeafLight : ZonePaint[I]->Foliage;
			ColR += Palette.R * Weights[I];
			ColG += Palette.G * Weights[I];
			ColB += Palette.B * Weights[I];
		}

		const int32 MinX = FMath::Max(1, static_cast<int32>(std::floor((P.X + KomorebiCore::kExtent - Radius) / Step)));
		const int32 MaxX = FMath::Min(Size - 2, static_cast<int32>(std::floor((P.X + KomorebiCore::kExtent + Radius) / Step)));
		const int32 MinZ = FMath::Max(1, static_cast<int32>(std::floor((P.Z + KomorebiCore::kExtent - Radius) / Step)));
		const int32 MaxZ = FMath::Min(Size - 2, static_cast<int32>(std::floor((P.Z + KomorebiCore::kExtent + Radius) / Step)));
		const double InvRadiusSq = 1.0 / (Radius * Radius);

		for (int32 J = MinZ; J <= MaxZ; ++J)
		{
			const double Dz = (static_cast<double>(J) + 0.5) * Step - KomorebiCore::kExtent - P.Z;
			for (int32 I = MinX; I <= MaxX; ++I)
			{
				const double Dx = (static_cast<double>(I) + 0.5) * Step - KomorebiCore::kExtent - P.X;
				const double Falloff = FMath::Max(0.0, 1.0 - (Dx * Dx + Dz * Dz) * InvRadiusSq);
				if (Falloff <= 0.0)
				{
					continue;
				}
				const double Glow = 255.0 * Strength * Falloff * Falloff;
				FColor& Dest = BakedStreetLights[I + J * Size];
				Dest.R = static_cast<uint8>(FMath::Min<int32>(255, static_cast<int32>(Dest.R) + FMath::RoundToInt(Glow * ColR)));
				Dest.G = static_cast<uint8>(FMath::Min<int32>(255, static_cast<int32>(Dest.G) + FMath::RoundToInt(Glow * ColG)));
				Dest.B = static_cast<uint8>(FMath::Min<int32>(255, static_cast<int32>(Dest.B) + FMath::RoundToInt(Glow * ColB)));
			}
		}
	};

	for (const FPlacementRecord& P : LightRecords)
	{
		const double HeightAboveGround = P.Y - SampleHeight(P.X, P.Z);
		if (HeightAboveGround > 3.0 && HeightAboveGround < 8.5)
		{
			Stamp(P, 9.6, 0.78, LightAccentAt(P.X, P.Z));
		}
	}

	for (const FPlacementRecord& P : SignRecords)
	{
		const double HeightAboveGround = P.Y - SampleHeight(P.X, P.Z);
		if (HeightAboveGround < 10.0)
		{
			Stamp(P, 6.8, 0.54, LightAccentAt(P.X, P.Z) ^ 1);
		}
	}

	if (TerrainActor)
	{
		TerrainActor->UploadStreetLightsTexture(BakedStreetLights, Size);
	}
}

void ANeoKyotoCityActor::SetDecorationQuality(float DensityMultiplier)
{
	CurrentDecorationDensity = FMath::Clamp(DensityMultiplier, 0.1f, 1.0f);

	if (GrassTuftsHISM)
	{
		GrassTuftsHISM->ClearInstances();
		const int32 TargetCount = FMath::Clamp(
			FMath::FloorToInt(static_cast<float>(FullGrassTuftTransforms.Num()) * CurrentDecorationDensity),
			0,
			FullGrassTuftTransforms.Num());
		if (TargetCount > 0)
		{
			TArray<FTransform> Subset;
			Subset.Append(FullGrassTuftTransforms.GetData(), TargetCount);
			GrassTuftsHISM->AddInstances(Subset, false, false);
		}
		const int32 GrassCullCm = FMath::RoundToInt(FMath::Lerp(4500.0f, 11000.0f, CurrentDecorationDensity));
		GrassTuftsHISM->SetCullDistances(FMath::RoundToInt(GrassCullCm * 0.75f), GrassCullCm);
	}

	const int32 FoliageCullCm = FMath::RoundToInt(FMath::Lerp(14000.0f, 32000.0f, CurrentDecorationDensity));
	if (BroadleafCanopiesHISM)
	{
		BroadleafCanopiesHISM->SetCullDistances(FMath::RoundToInt(FoliageCullCm * 0.8f), FoliageCullCm);
	}
	if (SprucePinesHISM)
	{
		SprucePinesHISM->SetCullDistances(FMath::RoundToInt(FoliageCullCm * 0.8f), FoliageCullCm);
	}
	if (GraniteBouldersHISM)
	{
		GraniteBouldersHISM->SetCullDistances(FMath::RoundToInt(FoliageCullCm * 0.7f), FoliageCullCm);
	}
}

void ANeoKyotoCityActor::UpdateDistrictPalette(
	float DeltaTime,
	double FocusThreeZ,
	int32 ClearedSectors,
	bool bRoam)
{
	ResolveWorldReferences();

	const auto& Chapters = KomorebiCore::GetChapters();
	if (Chapters.empty())
	{
		return;
	}

	const int32 ChapterIdx = FMath::Clamp(
		KomorebiCore::ChapterAt(FocusThreeZ, ClearedSectors, bRoam),
		0,
		static_cast<int32>(Chapters.size()) - 1);
	const KomorebiCore::ChapterSpec& Spec = Chapters[static_cast<size_t>(ChapterIdx)];

	if (ChapterIdx != ActiveChapter)
	{
		ActiveChapter = ChapterIdx;
		OnDistrictChapterChanged.Broadcast(
			ActiveChapter,
			KomorebiBridge::ToFString(Spec.Name),
			KomorebiBridge::ToFString(Spec.Subtitle));
	}

	const float Night = AtmosphereActor ? AtmosphereActor->GetNightFactor() : 0.0f;
	const float Twilight = AtmosphereActor ? AtmosphereActor->GetTwilightFactor() : 0.0f;
	const float Blend = bPaletteInitialized
		? (1.0f - FMath::Exp(-FMath::Max(0.0f, DeltaTime) * 2.0f))
		: 1.0f;
	bPaletteInitialized = true;

	auto MixDayNight = [Night](const KomorebiCore::RGB& DayRgb, const KomorebiCore::RGB& NightRgb) -> FLinearColor
	{
		return FLinearColor(
			static_cast<float>(DayRgb.R + (NightRgb.R - DayRgb.R) * Night),
			static_cast<float>(DayRgb.G + (NightRgb.G - DayRgb.G) * Night),
			static_cast<float>(DayRgb.B + (NightRgb.B - DayRgb.B) * Night),
			1.0f);
	};

	const FLinearColor TargetGrass = MixDayNight(Spec.Day.Grass, Spec.Grass);
	const FLinearColor TargetPath = MixDayNight(Spec.Day.Path, Spec.Path);
	const FLinearColor TargetSky = MixDayNight(Spec.Day.Sky, Spec.Sky);
	FLinearColor TargetHaze = MixDayNight(Spec.Day.Haze, Spec.Haze);

	// Matches legacy-web/src/world/anime.js lines 1033-1037 twilight warmth boost.
	if (Twilight > 0.001f)
	{
		TargetHaze.R = FMath::Min(1.0f, TargetHaze.R + 0.32f * Twilight);
		TargetHaze.G = FMath::Min(1.0f, TargetHaze.G + 0.16f * Twilight);
		TargetHaze.B = FMath::Min(1.0f, TargetHaze.B + 0.08f * Twilight);
	}

	const FLinearColor TargetFoliage = KomorebiBridge::ToLinearColor(Spec.Foliage);
	const FLinearColor TargetLeafLight = KomorebiBridge::ToLinearColor(Spec.LeafLight);

	BlendedGrass = FMath::Lerp(BlendedGrass, TargetGrass, Blend);
	BlendedPath = FMath::Lerp(BlendedPath, TargetPath, Blend);
	BlendedSkyHigh = FMath::Lerp(BlendedSkyHigh, TargetSky, Blend);
	BlendedHaze = FMath::Lerp(BlendedHaze, TargetHaze, Blend);
	BlendedFoliage = FMath::Lerp(BlendedFoliage, TargetFoliage, Blend);
	BlendedLeafLight = FMath::Lerp(BlendedLeafLight, TargetLeafLight, Blend);

	if (AtmosphereActor)
	{
		AtmosphereActor->SetDistrictPaletteColors(
			BlendedGrass,
			BlendedPath,
			BlendedFoliage,
			BlendedLeafLight,
			BlendedSkyHigh,
			BlendedHaze);
	}
}
