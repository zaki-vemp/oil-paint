// Copyright Komorebi Project. Snowbound (Aurora) world actor implementation.
#include "World/SnowboundWorldActor.h"

#include "Components/BoxComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Particles/ParticleSystemComponent.h"
#include "ProceduralMeshComponent.h"
#include "World/DayNightAtmosphereActor.h"
#include "World/KomorebiNoise.h"
#include "World/KomorebiTerrainActor.h"
#include "World/SnowboundTerrain.h"

namespace
{
	struct FAlpineRingSpec
	{
		double Radius;
		double Top;
		double Seed;
		double OpenAz;
		double OpenLo;
		double OpenHi;
	};

	// Exact distant alpine ring parameters from legacy-web/src/night/far.js lines 8-12.
	static constexpr FAlpineRingSpec kAlpineRings[3] = {
		{ 2300.0,  620.0,  3.1, 0.0,  0.30, 1.0 },
		{ 4300.0, 1000.0, 11.7, 0.35, 0.12, 0.6 },
		{ 7600.0, 1500.0, 23.9, 0.0,  0.00, 0.0 },
	};

	double EvalAlpineCrest(int32 RingIdx, double Azimuth)
	{
		const FAlpineRingSpec& O = kAlpineRings[RingIdx];
		const double C = std::cos(Azimuth);
		const double S = std::sin(Azimuth);
		double N = 0.45
			+ 0.55 * KomorebiCore::Noise(C * 2.2 + O.Seed, S * 2.2)
			+ 0.28 * KomorebiCore::Noise(C * 6.5, S * 6.5 + O.Seed)
			+ 0.12 * KomorebiCore::Noise(C * 17.0 + O.Seed, S * 17.0)
			+ 0.05 * KomorebiCore::Noise(C * 41.0, S * 41.0 + O.Seed);

		const double Wrapped = std::fmod(Azimuth - O.OpenAz + KomorebiCore::kPi * 3.0, KomorebiCore::kTau);
		const double Turn = std::abs((Wrapped < 0.0 ? Wrapped + KomorebiCore::kTau : Wrapped) - KomorebiCore::kPi);
		const double Gap = (O.OpenHi > 0.0)
			? (0.1 + 0.9 * KomorebiCore::SmoothStep(O.OpenLo, O.OpenHi, Turn))
			: 1.0;
		return O.Top * Gap * KomorebiCore::Clamp(N, 0.1, 1.4);
	}

	double RoofDeckHeight(double LocalZ)
	{
		return 4.25 - 0.654 * std::abs(LocalZ);
	}

	FTransform CabinBoxTransform(
		const KomorebiCore::Vec3& CabinOrigin,
		double LocalX,
		double LocalY,
		double LocalZ,
		double SizeX,
		double SizeY,
		double SizeZ,
		double PitchRad = 0.0,
		double YawRad = 0.0)
	{
		const FVector Loc = KomorebiBridge::ThreeXZToFVector(
			CabinOrigin.X + LocalX,
			CabinOrigin.Y + LocalY,
			CabinOrigin.Z + LocalZ);
		const FRotator Rot(
			FMath::RadiansToDegrees(PitchRad),
			-FMath::RadiansToDegrees(YawRad),
			0.0);
		const FVector Scale(
			static_cast<float>(SizeZ),
			static_cast<float>(SizeX),
			static_cast<float>(SizeY));
		return FTransform(Rot, Loc, Scale);
	}
}

ASnowboundWorldActor::ASnowboundWorldActor()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("SnowboundRoot"));
	SetRootComponent(Root);

	FjordOceanMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FjordOceanMesh"));
	FjordOceanMesh->SetupAttachment(Root);
	FjordOceanMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FjordOceanMesh->SetCastShadow(false);

	FarAlpineRingsMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FarAlpineRingsMesh"));
	FarAlpineRingsMesh->SetupAttachment(Root);
	FarAlpineRingsMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FarAlpineRingsMesh->SetCastShadow(false);

	AuroraSkyDomeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AuroraSkyDomeMesh"));
	AuroraSkyDomeMesh->SetupAttachment(Root);
	AuroraSkyDomeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AuroraSkyDomeMesh->SetCastShadow(false);
	AuroraSkyDomeMesh->bCastDynamicShadow = false;
	AuroraSkyDomeMesh->bReceivesDecals = false;

	AuroraCurtainsMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("AuroraCurtainsMesh"));
	AuroraCurtainsMesh->SetupAttachment(Root);
	AuroraCurtainsMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AuroraCurtainsMesh->SetCastShadow(false);

	CabinRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CabinRoot"));
	CabinRoot->SetupAttachment(Root);

	CabinExteriorHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("CabinExteriorHISM"));
	CabinExteriorHISM->SetupAttachment(CabinRoot);
	CabinExteriorHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CabinExteriorHISM->SetCastShadow(true);

	CabinNearWallCutoutHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("CabinNearWallCutoutHISM"));
	CabinNearWallCutoutHISM->SetupAttachment(CabinRoot);
	CabinNearWallCutoutHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CabinNearWallCutoutHISM->SetCastShadow(true);

	CabinInteriorHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("CabinInteriorHISM"));
	CabinInteriorHISM->SetupAttachment(CabinRoot);
	CabinInteriorHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CabinInteriorHISM->SetCastShadow(false);

	HearthFireLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("HearthFireLight"));
	HearthFireLight->SetupAttachment(CabinRoot);
	HearthFireLight->SetMobility(EComponentMobility::Movable);
	HearthFireLight->SetLightColor(FLinearColor(1.0f, 0.52f, 0.16f));
	HearthFireLight->SetIntensity(3600.0f);
	HearthFireLight->SetAttenuationRadius(1400.0f);
	HearthFireLight->CastShadows = true;

	TableCandleLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("TableCandleLight"));
	TableCandleLight->SetupAttachment(CabinRoot);
	TableCandleLight->SetMobility(EComponentMobility::Movable);
	TableCandleLight->SetLightColor(FLinearColor(1.0f, 0.68f, 0.30f));
	TableCandleLight->SetIntensity(900.0f);
	TableCandleLight->SetAttenuationRadius(650.0f);
	TableCandleLight->CastShadows = false;

	PineConesHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("PineConesHISM"));
	PineConesHISM->SetupAttachment(Root);
	PineConesHISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PineConesHISM->SetCollisionObjectType(ECC_WorldStatic);
	PineConesHISM->SetCollisionResponseToAllChannels(ECR_Block);
	PineConesHISM->SetCastShadow(true);

	PineSnowCapsHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("PineSnowCapsHISM"));
	PineSnowCapsHISM->SetupAttachment(Root);
	PineSnowCapsHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PineSnowCapsHISM->SetCastShadow(true);

	DriftingSnowMesh = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("DriftingSnowMesh"));
	DriftingSnowMesh->SetupAttachment(Root);
	DriftingSnowMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DriftingSnowMesh->SetCastShadow(false);
	DriftingSnowMesh->bCastDynamicShadow = false;

	SnowParticleSystem = CreateDefaultSubobject<UParticleSystemComponent>(TEXT("SnowParticleSystem"));
	SnowParticleSystem->SetupAttachment(Root);
	SnowParticleSystem->bAutoActivate = false;
}

void ASnowboundWorldActor::BeginPlay()
{
	Super::BeginPlay();

	ResolveWorldReferences();
	RebuildSnowboundWorld();
}

void ASnowboundWorldActor::ResolveWorldReferences()
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

double ASnowboundWorldActor::SampleHeight(double ThreeX, double ThreeZ) const
{
	if (TerrainActor && TerrainActor->IsTerrainPrepared())
	{
		return TerrainActor->GetHeightAtThree(ThreeX, ThreeZ);
	}
	return KomorebiCore::SnowboundTerrainMap::RawHeight(ThreeX, ThreeZ);
}

void ASnowboundWorldActor::SampleLand(double ThreeX, double ThreeZ, double& OutRock, double& OutHorn) const
{
	if (TerrainActor && TerrainActor->IsTerrainPrepared())
	{
		TerrainActor->GetSnowboundLandAtThree(ThreeX, ThreeZ, OutRock, OutHorn);
	}
	else
	{
		OutRock = 0.0;
		OutHorn = 0.0;
	}
}

KomorebiCore::Vec3 ASnowboundWorldActor::SampleNormal(double ThreeX, double ThreeZ) const
{
	if (TerrainActor && TerrainActor->IsTerrainPrepared())
	{
		return TerrainActor->GetNormalAtThree(ThreeX, ThreeZ);
	}
	constexpr double Eps = 1.2;
	const double Nx = SampleHeight(ThreeX - Eps, ThreeZ) - SampleHeight(ThreeX + Eps, ThreeZ);
	const double Ny = 2.0 * Eps;
	const double Nz = SampleHeight(ThreeX, ThreeZ - Eps) - SampleHeight(ThreeX, ThreeZ + Eps);
	const double Len = std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
	return Len > 1e-9 ? KomorebiCore::Vec3{ Nx / Len, Ny / Len, Nz / Len } : KomorebiCore::Vec3{ 0.0, 1.0, 0.0 };
}

void ASnowboundWorldActor::RebuildSnowboundWorld()
{
	ResolveWorldReferences();
	if (TerrainActor)
	{
		TerrainActor->EnsureTerrainBuilt();
	}
	BuildFjordOceanMesh();
	BuildFarAlpineRingsMesh();
	BuildAuroraSkyAndCurtains();
	BuildCabinAndInterior();
	BuildSnowPines();
	InitializeSnowTrackTarget();
	InitializeDriftingSnow();

	// Lay Kael's initial footprints coming up from the south (legacy-web/src/night/main.js line 164).
	const KomorebiCore::Vec2 Start = KomorebiCore::SnowStart();
	LayInitialTrail(Start.Z + 16.0, Start.Z + 0.4);
}

void ASnowboundWorldActor::BuildFjordOceanMesh()
{
	if (!FjordOceanMesh)
	{
		return;
	}
	FjordOceanMesh->ClearAllMeshSections();

	// Exact 4 water ring bands from legacy-web/src/night/far.js line 36 (y = 0).
	static constexpr double kBands[4][2] = {
		{ 60.0, 2200.0 },
		{ 2200.0, 4200.0 },
		{ 4200.0, 7500.0 },
		{ 7500.0, 15500.0 },
	};
	constexpr int32 Segs = 128;

	for (int32 BandIdx = 0; BandIdx < 4; ++BandIdx)
	{
		const double InnerR = kBands[BandIdx][0];
		const double OuterR = kBands[BandIdx][1];

		TArray<FVector> Verts;
		TArray<int32> Tris;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FLinearColor> Colors;
		TArray<FProcMeshTangent> Tangents;

		Verts.Reserve((Segs + 1) * 2);
		Normals.Reserve((Segs + 1) * 2);
		UVs.Reserve((Segs + 1) * 2);
		Colors.Reserve((Segs + 1) * 2);
		Tangents.Reserve((Segs + 1) * 2);
		Tris.Reserve(Segs * 6);

		const FLinearColor DeepArcticWater(0.005f, 0.015f, 0.028f, 1.0f);
		for (int32 I = 0; I <= Segs; ++I)
		{
			const double Frac = static_cast<double>(I) / static_cast<double>(Segs);
			const double Ang = Frac * KomorebiCore::kTau;
			const double CosA = std::cos(Ang);
			const double SinA = std::sin(Ang);

			// UE horizontal plane at Z = 0: (+X north, +Y east).
			Verts.Add(FVector(
				static_cast<float>(CosA * InnerR * KomorebiCore::kMetersToCm),
				static_cast<float>(SinA * InnerR * KomorebiCore::kMetersToCm),
				0.0f));
			Verts.Add(FVector(
				static_cast<float>(CosA * OuterR * KomorebiCore::kMetersToCm),
				static_cast<float>(SinA * OuterR * KomorebiCore::kMetersToCm),
				0.0f));

			Normals.Add(FVector::UpVector);
			Normals.Add(FVector::UpVector);
			UVs.Add(FVector2D(static_cast<float>(Frac * 8.0), 0.0f));
			UVs.Add(FVector2D(static_cast<float>(Frac * 8.0), 1.0f));
			Colors.Add(DeepArcticWater);
			Colors.Add(DeepArcticWater);
			Tangents.Add(FProcMeshTangent(FVector(-SinA, CosA, 0.0), false));
			Tangents.Add(FProcMeshTangent(FVector(-SinA, CosA, 0.0), false));

			if (I < Segs)
			{
				const int32 A = I * 2;
				const int32 B = A + 1;
				const int32 C = A + 2;
				const int32 D = A + 3;
				// Upward (+Z) facing CCW winding in UE left-handed space.
				Tris.Add(A);
				Tris.Add(C);
				Tris.Add(B);

				Tris.Add(B);
				Tris.Add(C);
				Tris.Add(D);
			}
		}

		FjordOceanMesh->CreateMeshSection_LinearColor(
			BandIdx, Verts, Tris, Normals, UVs, Colors, Tangents, false);
		if (FjordWaterMaterial)
		{
			FjordOceanMesh->SetMaterial(BandIdx, FjordWaterMaterial);
		}
	}
}

void ASnowboundWorldActor::BuildFarAlpineRingsMesh()
{
	if (!FarAlpineRingsMesh)
	{
		return;
	}
	FarAlpineRingsMesh->ClearAllMeshSections();

	constexpr int32 Segs = 720;
	for (int32 RingIdx = 0; RingIdx < 3; ++RingIdx)
	{
		const FAlpineRingSpec& Spec = kAlpineRings[RingIdx];

		TArray<FVector> Verts;
		TArray<int32> Tris;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FLinearColor> Colors;
		TArray<FProcMeshTangent> Tangents;

		Verts.Reserve((Segs + 1) * 2);
		Normals.Reserve((Segs + 1) * 2);
		UVs.Reserve((Segs + 1) * 2);
		Colors.Reserve((Segs + 1) * 2);
		Tangents.Reserve((Segs + 1) * 2);
		Tris.Reserve(Segs * 6);

		const FLinearColor BaseRock(0.025f, 0.042f, 0.068f, 1.0f);
		const FLinearColor PeakSnow(0.28f, 0.42f, 0.56f, 1.0f);

		for (int32 I = 0; I <= Segs; ++I)
		{
			const double U = static_cast<double>(I) / static_cast<double>(Segs);
			const double Az = U * KomorebiCore::kTau;
			const double ThreeX = std::sin(Az) * Spec.Radius;
			const double ThreeZ = -std::cos(Az) * Spec.Radius;
			const double TopH = EvalAlpineCrest(RingIdx, Az);

			const FVector BottomUE = KomorebiBridge::ThreeXZToFVector(ThreeX, -20.0, ThreeZ);
			const FVector TopUE = KomorebiBridge::ThreeXZToFVector(ThreeX, TopH, ThreeZ);
			const FVector InwardNormal = KomorebiBridge::ThreeDirToFVector(
				KomorebiCore::Vec3{ -std::sin(Az), 0.35, std::cos(Az) }).GetSafeNormal();
			const FVector Tan = FVector::CrossProduct(FVector::UpVector, InwardNormal).GetSafeNormal();

			Verts.Add(BottomUE);
			Verts.Add(TopUE);
			Normals.Add(InwardNormal);
			Normals.Add(InwardNormal);
			UVs.Add(FVector2D(static_cast<float>(U), 0.0f));
			UVs.Add(FVector2D(static_cast<float>(U), 1.0f));
			Colors.Add(BaseRock);
			Colors.Add(PeakSnow);
			Tangents.Add(FProcMeshTangent(Tan, false));
			Tangents.Add(FProcMeshTangent(Tan, false));

			if (I < Segs)
			{
				const int32 A = I * 2;
				const int32 B = A + 1;
				const int32 C = A + 2;
				const int32 D = A + 3;
				// Inward-facing ring triangles visible from the ridge at center.
				Tris.Add(A);
				Tris.Add(B);
				Tris.Add(C);

				Tris.Add(B);
				Tris.Add(D);
				Tris.Add(C);
			}
		}

		FarAlpineRingsMesh->CreateMeshSection_LinearColor(
			RingIdx, Verts, Tris, Normals, UVs, Colors, Tangents, false);
	}
}

void ASnowboundWorldActor::BuildAuroraSkyAndCurtains()
{
	if (AuroraSkyDomeMesh)
	{
		UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		if (SphereMesh && !AuroraSkyDomeMesh->GetStaticMesh())
		{
			AuroraSkyDomeMesh->SetStaticMesh(SphereMesh);
		}
		if (!AuroraSkyMaterial)
		{
			AuroraSkyMaterial = LoadObject<UMaterialInterface>(
				nullptr,
				TEXT("/Game/Materials/M_AuroraSky.M_AuroraSky"));
		}
		if (AuroraSkyMaterial)
		{
			AuroraSkyDomeMesh->SetMaterial(0, AuroraSkyMaterial);
		}
		// 18km radius dome sits cleanly behind the 2.3/4.3/7.6km alpine rings and 15.5km fjord water.
		// (/Engine/BasicShapes/Sphere has 50cm radius -> scale 36000 = 1,800,000 cm.)
		AuroraSkyDomeMesh->SetWorldScale3D(FVector(36000.0f));
	}

	if (!AuroraCurtainsMesh)
	{
		return;
	}
	AuroraCurtainsMesh->ClearAllMeshSections();

	// Three billowing aurora ribbon geometries above the northern horizon (sky.js lines 91-139).
	constexpr int32 SegU = 96;
	constexpr int32 SegV = 6;
	for (int32 K = 0; K < 3; ++K)
	{
		TArray<FVector> Verts;
		TArray<int32> Tris;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FLinearColor> Colors;
		TArray<FProcMeshTangent> Tangents;

		Verts.Reserve((SegU + 1) * (SegV + 1));
		Normals.Reserve((SegU + 1) * (SegV + 1));
		UVs.Reserve((SegU + 1) * (SegV + 1));
		Colors.Reserve((SegU + 1) * (SegV + 1));
		Tangents.Reserve((SegU + 1) * (SegV + 1));
		Tris.Reserve(SegU * SegV * 6);

		const double Tall = (K == 1) ? 2800.0 : 3400.0;
		for (int32 J = 0; J <= SegV; ++J)
		{
			const double S = static_cast<double>(J) / static_cast<double>(SegV);
			const FLinearColor CurtainCol = FMath::Lerp(
				FLinearColor(0.06f, 1.15f, 0.66f, 1.0f),
				FLinearColor(0.46f, 0.16f, 1.05f, 1.0f),
				static_cast<float>(S));

			for (int32 I = 0; I <= SegU; ++I)
			{
				const double FracU = static_cast<double>(I) / static_cast<double>(SegU);
				const double U = FracU * 2.0 - 1.0;

				double Fx = 0.0;
				double Fy = 2150.0;
				double Fz = -8800.0;
				if (K == 0)
				{
					const double Ph = U * 7.2;
					Fx = 11500.0 * U + 1600.0 * std::sin(Ph);
					Fy = 2150.0 + 260.0 * std::sin(U * 4.5) + 95.0 * std::sin(U * 19.0);
					Fz = -8800.0 + 2400.0 * std::cos(Ph * 0.82 + 0.5) + 850.0 * std::sin(U * 15.0);
				}
				else if (K == 1)
				{
					Fx = 16500.0 * U + 1100.0 * std::sin(U * 5.2);
					Fy = 2650.0 + 160.0 * std::sin(U * 16.0);
					Fz = -16200.0 + 2100.0 * std::sin(U * 4.6 + 1.3) + 650.0 * std::sin(U * 21.0);
				}
				else
				{
					const double Z = 2500.0 * std::tan(KomorebiCore::Lerp(-1.42, 0.95, FracU));
					Fx = 1800.0 + 1650.0 * std::sin(Z / 2500.0) + 460.0 * std::sin(Z / 680.0);
					Fy = 2550.0 + 140.0 * std::sin(Z / 520.0);
					Fz = Z;
				}

				const double Px = Fx + std::sin(U * 9.0 + S * 2.5) * 180.0 * S;
				const double Py = Fy + S * Tall;
				const double Pz = Fz + std::cos(U * 7.0 - S * 2.0) * 180.0 * S;
				const double Len = FMath::Max(1.0, std::sqrt(Px * Px + Py * Py + Pz * Pz));

				// Project onto 11km celestial sphere radius matching sky.js line 137 (behind 7.6km far alpine ring).
				const FVector PosUE = KomorebiBridge::ThreeXZToFVector(
					(Px / Len) * 11000.0,
					(Py / Len) * 11000.0,
					(Pz / Len) * 11000.0);

				Verts.Add(PosUE);
				Normals.Add((-PosUE).GetSafeNormal());
				UVs.Add(FVector2D(static_cast<float>(FracU), static_cast<float>(S)));
				Colors.Add(CurtainCol);
				Tangents.Add(FProcMeshTangent(FVector::RightVector, false));
			}
		}

		const int32 Stride = SegU + 1;
		for (int32 J = 0; J < SegV; ++J)
		{
			for (int32 I = 0; I < SegU; ++I)
			{
				const int32 A = I + J * Stride;
				const int32 B = A + 1;
				const int32 C = A + Stride;
				const int32 D = C + 1;
				Tris.Add(A);
				Tris.Add(C);
				Tris.Add(B);

				Tris.Add(B);
				Tris.Add(C);
				Tris.Add(D);
			}
		}

		AuroraCurtainsMesh->CreateMeshSection_LinearColor(
			K, Verts, Tris, Normals, UVs, Colors, Tangents, false);
		if (AuroraSkyMaterial)
		{
			AuroraCurtainsMesh->SetMaterial(K, AuroraSkyMaterial);
		}
	}
}

void ASnowboundWorldActor::ClearDynamicColliders()
{
	for (UBoxComponent* Box : CabinColliders)
	{
		if (Box)
		{
			Box->DestroyComponent();
		}
	}
	CabinColliders.Reset();
}

void ASnowboundWorldActor::BuildCabinAndInterior()
{
	ClearDynamicColliders();

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh)
	{
		if (CabinExteriorHISM && !CabinExteriorHISM->GetStaticMesh())
		{
			CabinExteriorHISM->SetStaticMesh(CubeMesh);
		}
		if (CabinNearWallCutoutHISM && !CabinNearWallCutoutHISM->GetStaticMesh())
		{
			CabinNearWallCutoutHISM->SetStaticMesh(CubeMesh);
		}
		if (CabinInteriorHISM && !CabinInteriorHISM->GetStaticMesh())
		{
			CabinInteriorHISM->SetStaticMesh(CubeMesh);
		}
	}

	if (CabinExteriorHISM)
	{
		CabinExteriorHISM->ClearInstances();
	}
	if (CabinNearWallCutoutHISM)
	{
		CabinNearWallCutoutHISM->ClearInstances();
	}
	if (CabinInteriorHISM)
	{
		CabinInteriorHISM->ClearInstances();
	}

	const KomorebiCore::Vec3 Cabin = KomorebiCore::SnowCabin();

	TArray<FTransform> ExteriorBoxes;
	TArray<FTransform> CutoutBoxes;
	TArray<FTransform> InteriorBoxes;

	// --- Cabin exterior (cabin.js lines 57-93) ---
	// North wall (z = -2.55), West wall (x = -3.15), East wall (x = +3.15) remain permanent.
	ExteriorBoxes.Add(CabinBoxTransform(Cabin, 0.0, 1.2, -2.55, 6.4, 2.4, 0.2));
	ExteriorBoxes.Add(CabinBoxTransform(Cabin, -3.15, 1.2, 0.0, 0.2, 2.4, 5.2));
	ExteriorBoxes.Add(CabinBoxTransform(Cabin, 3.15, 1.2, 0.0, 0.2, 2.4, 5.2));
	// West stone chimney + snow cap.
	ExteriorBoxes.Add(CabinBoxTransform(Cabin, -3.65, 2.45, 0.0, 0.9, 4.9, 1.0));
	ExteriorBoxes.Add(CabinBoxTransform(Cabin, -3.65, 4.99, 0.0, 1.02, 0.18, 1.12));
	// Stone doorstep, open front door, and split wood stack against south wall.
	ExteriorBoxes.Add(CabinBoxTransform(Cabin, 1.75, 0.03, 3.0, 1.5, 0.06, 0.7));
	ExteriorBoxes.Add(CabinBoxTransform(Cabin, 2.42, 1.0, 3.12, 0.07, 1.96, 1.0));
	ExteriorBoxes.Add(CabinBoxTransform(Cabin, -2.6, 0.45, 2.85, 1.1, 0.9, 0.45));
	ExteriorBoxes.Add(CabinBoxTransform(Cabin, -2.6, 0.95, 2.87, 1.2, 0.14, 0.55));

	// --- Near-wall (south entrance wall) & sloped snow-laden roof cutout (hidden on doorstep) ---
	// South wall left & right of open doorway [1.2..2.3], plus lintel above doorway.
	CutoutBoxes.Add(CabinBoxTransform(Cabin, -0.975, 1.2, 2.55, 4.45, 2.4, 0.2));
	CutoutBoxes.Add(CabinBoxTransform(Cabin, 2.75, 1.2, 2.55, 0.9, 2.4, 0.2));
	CutoutBoxes.Add(CabinBoxTransform(Cabin, 1.75, 2.2, 2.55, 1.2, 0.4, 0.2));
	// Sloped pitched roof panels + deep snow drifts (roof.js SLOPE = 0.654, pitch ~33.2 deg).
	const double RoofPitch = std::atan(0.654);
	const double SlopeLen = std::hypot(3.0, 1.962);
	const double MidRoofY = (RoofDeckHeight(0.0) + RoofDeckHeight(3.0)) * 0.5;
	CutoutBoxes.Add(CabinBoxTransform(Cabin, 0.0, MidRoofY, 1.5, 7.1, 0.28, SlopeLen, -RoofPitch, 0.0));
	CutoutBoxes.Add(CabinBoxTransform(Cabin, 0.0, MidRoofY, -1.5, 7.1, 0.28, SlopeLen, RoofPitch, 0.0));
	CutoutBoxes.Add(CabinBoxTransform(Cabin, 0.0, MidRoofY + 0.26, 1.52, 7.36, 0.26, SlopeLen + 0.24, -RoofPitch, 0.0));
	CutoutBoxes.Add(CabinBoxTransform(Cabin, 0.0, MidRoofY + 0.26, -1.52, 7.36, 0.26, SlopeLen + 0.24, RoofPitch, 0.0));

	// --- Cozy cabin interior (cabin.js lines 144-281) ---
	// Wooden floor planks.
	InteriorBoxes.Add(CabinBoxTransform(Cabin, 0.0, 0.01, 0.0, 6.0, 0.04, 4.8));
	// Stone hearth, mantel, and glowing ember bed on west wall.
	InteriorBoxes.Add(CabinBoxTransform(Cabin, -2.8, 0.6, -0.78, 0.4, 1.2, 0.34));
	InteriorBoxes.Add(CabinBoxTransform(Cabin, -2.8, 0.6, 0.78, 0.4, 1.2, 0.34));
	InteriorBoxes.Add(CabinBoxTransform(Cabin, -2.8, 1.36, 0.0, 0.4, 0.32, 1.9));
	InteriorBoxes.Add(CabinBoxTransform(Cabin, -2.86, 2.0, 0.0, 0.28, 0.96, 1.5));
	InteriorBoxes.Add(CabinBoxTransform(Cabin, -2.5, 0.04, 0.0, 1.0, 0.08, 2.0));
	InteriorBoxes.Add(CabinBoxTransform(Cabin, -2.68, 1.55, 0.0, 0.6, 0.06, 2.06));
	InteriorBoxes.Add(CabinBoxTransform(Cabin, -2.7, 0.14, 0.0, 0.34, 0.12, 0.6));
	// Layered hearth rug.
	InteriorBoxes.Add(CabinBoxTransform(Cabin, -0.95, 0.035, 0.1, 2.3, 0.02, 1.6));
	// Fireside wooden chair.
	InteriorBoxes.Add(CabinBoxTransform(Cabin, -1.45, 0.42, -1.0, 0.5, 0.06, 0.5));
	InteriorBoxes.Add(CabinBoxTransform(Cabin, -1.2, 0.83, -1.0, 0.06, 0.82, 0.5));
	// Dining table + stools.
	InteriorBoxes.Add(CabinBoxTransform(Cabin, 0.9, 0.76, 0.6, 1.3, 0.06, 0.8));
	InteriorBoxes.Add(CabinBoxTransform(Cabin, 0.95, 0.225, 1.38, 0.34, 0.45, 0.34));
	InteriorBoxes.Add(CabinBoxTransform(Cabin, -0.05, 0.225, 0.95, 0.34, 0.45, 0.34));
	// Bed + headboard + blanket chest along north wall.
	InteriorBoxes.Add(CabinBoxTransform(Cabin, 1.95, 0.26, -1.86, 2.0, 0.52, 1.0));
	InteriorBoxes.Add(CabinBoxTransform(Cabin, 2.94, 0.62, -1.86, 0.08, 1.24, 1.0));
	InteriorBoxes.Add(CabinBoxTransform(Cabin, 0.62, 0.24, -1.86, 0.5, 0.48, 0.9));
	// North wall bookshelves & south-west dresser.
	InteriorBoxes.Add(CabinBoxTransform(Cabin, -1.97, 1.25, -2.28, 1.46, 0.04, 0.22));
	InteriorBoxes.Add(CabinBoxTransform(Cabin, -1.97, 1.75, -2.28, 1.46, 0.04, 0.22));
	InteriorBoxes.Add(CabinBoxTransform(Cabin, -2.74, 0.45, 1.75, 0.5, 0.9, 1.15));
	InteriorBoxes.Add(CabinBoxTransform(Cabin, -2.86, 1.4, 1.75, 0.26, 1.0, 1.15));

	if (CabinExteriorHISM)
	{
		CabinExteriorHISM->AddInstances(ExteriorBoxes, false, true);
	}
	if (CabinNearWallCutoutHISM)
	{
		CabinNearWallCutoutHISM->AddInstances(CutoutBoxes, false, true);
	}
	if (CabinInteriorHISM)
	{
		CabinInteriorHISM->AddInstances(InteriorBoxes, false, true);
	}

	// Position flickering hearth firelight & table candle light (cabin.js line 24).
	if (HearthFireLight)
	{
		HearthFireLight->SetWorldLocation(
			KomorebiBridge::ThreeXZToFVector(Cabin.X - 2.4, Cabin.Y + 0.6, Cabin.Z));
	}
	if (TableCandleLight)
	{
		TableCandleLight->SetWorldLocation(
			KomorebiBridge::ThreeXZToFVector(Cabin.X + 0.6, Cabin.Y + 1.03, Cabin.Z + 0.5));
	}

	// Exact 13 cabin solid colliders matching legacy-web/src/night/cabin.js lines 413-419.
	static constexpr double kSolidBoxes[13][6] = {
		{  0.0,   1.2,  -2.5,  3.2,   1.2, 0.1  },
		{ -3.1,   1.2,   0.0,  0.1,   1.2, 2.6  },
		{  3.1,   1.2,   0.0,  0.1,   1.2, 2.6  },
		{ -0.975, 1.2,   2.5,  2.225, 1.2, 0.1  },
		{  2.725, 1.2,   2.5,  0.475, 1.2, 0.1  },
		{ -2.72,  0.7,   0.0,  0.3,   0.7, 1.0  },
		{  0.9,   0.4,   0.6,  0.62,  0.4, 0.38 },
		{  1.95,  0.3,  -1.86, 1.0,   0.3, 0.5  },
		{ -1.4,   0.4,  -1.0,  0.28,  0.4, 0.28 },
		{ -2.74,  0.8,   1.75, 0.27,  0.8, 0.58 },
		{  0.62,  0.25, -1.86, 0.26,  0.25, 0.46 },
		{ -3.65,  1.2,   0.0,  0.45,  1.2, 0.5  },
		{ -2.6,   0.45,  2.85, 0.55,  0.45, 0.25 },
	};

	for (int32 I = 0; I < 13; ++I)
	{
		const auto& B = kSolidBoxes[I];
		const FName ColName(*FString::Printf(TEXT("CabinSolid_%d"), I));
		UBoxComponent* Col = NewObject<UBoxComponent>(this, ColName);
		Col->SetupAttachment(CabinRoot);
		Col->SetBoxExtent(FVector(
			static_cast<float>(B[5] * KomorebiCore::kMetersToCm),
			static_cast<float>(B[3] * KomorebiCore::kMetersToCm),
			static_cast<float>(B[4] * KomorebiCore::kMetersToCm)));
		Col->SetWorldLocation(KomorebiBridge::ThreeXZToFVector(Cabin.X + B[0], Cabin.Y + B[1], Cabin.Z + B[2]));
		Col->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Col->SetCollisionObjectType(ECC_WorldStatic);
		Col->SetCollisionResponseToAllChannels(ECR_Block);
		Col->SetHiddenInGame(true);
		Col->RegisterComponent();
		CabinColliders.Add(Col);
	}
}

void ASnowboundWorldActor::BuildSnowPines()
{
	UStaticMesh* ConeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (ConeMesh)
	{
		if (PineConesHISM && !PineConesHISM->GetStaticMesh())
		{
			PineConesHISM->SetStaticMesh(ConeMesh);
		}
		if (PineSnowCapsHISM && !PineSnowCapsHISM->GetStaticMesh())
		{
			PineSnowCapsHISM->SetStaticMesh(ConeMesh);
		}
	}

	if (PineConesHISM)
	{
		PineConesHISM->ClearInstances();
	}
	if (PineSnowCapsHISM)
	{
		PineSnowCapsHISM->ClearInstances();
	}

	struct FPineTree
	{
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
		double H = 4.0;
		double R = 1.0;
		bool bFull = false;
	};

	KomorebiCore::Rng Rand(31337u);
	TArray<FPineTree> Trees;
	Trees.Reserve(1800);

	auto HasRoom = [&Trees](double X, double Z, double Gap) -> bool
	{
		const double GapSq = Gap * Gap;
		for (const FPineTree& T : Trees)
		{
			const double Dx = T.X - X;
			const double Dz = T.Z - Z;
			if (Dx * Dx + Dz * Dz <= GapSq)
			{
				return false;
			}
		}
		return true;
	};

	auto StandTree = [&](double X, double Z, double H, bool bFull)
	{
		const double Y = SampleHeight(X, Z);
		const double R = H * (0.13 + 0.07 * Rand()) + 0.25;
		Trees.Add(FPineTree{ X, Y - 0.25, Z, H, R, bFull });
	};

	// South timberline wood with central ridge aisle (pines.js lines 27-39).
	for (double Z = 172.0; Z < 350.0; Z += 3.6)
	{
		for (double Off = -96.0; Off <= 96.0; Off += 3.6)
		{
			const double X = KomorebiCore::CrestX(Z) + Off + (Rand() - 0.5) * 3.0;
			const double Zz = Z + (Rand() - 0.5) * 3.0;
			const double D = std::abs(X - KomorebiCore::CrestX(Zz));
			const double Drift = 0.5 + 0.5 * KomorebiCore::Noise(X / 23.0 + 4.0, Zz / 23.0 - 9.0);
			const double Thick = KomorebiCore::SmoothStep(176.0, 250.0, Zz)
				* (0.2 + 0.8 * Drift)
				* (1.0 - 0.5 * KomorebiCore::SmoothStep(40.0, 96.0, D));
			if (Rand() > Thick || D < 2.7)
			{
				continue;
			}
			const double Y = SampleHeight(X, Zz);
			double Rock = 0.0;
			double Horn = 0.0;
			SampleLand(X, Zz, Rock, Horn);
			if (Y < 12.0 || Rock > 0.4 || SampleNormal(X, Zz).Y < 0.66)
			{
				continue;
			}
			const double H = (3.0 + 8.5 * std::pow(Rand(), 1.4))
				* (0.55 + 0.45 * KomorebiCore::SmoothStep(180.0, 240.0, Zz))
				* (0.7 + 0.3 * KomorebiCore::SmoothStep(2.7, 7.0, D));
			if (HasRoom(X, Zz, 1.6 + H * 0.16))
			{
				StandTree(X, Zz, H, true);
			}
		}
	}

	// Six sentinel spruces standing out onto the knoll against the aurora (pines.js line 41).
	static constexpr double kKnollSentinels[6][3] = {
		{ -7.5, 143.0, 5.6 }, { 12.5, 137.0, 4.1 }, { -11.0, 122.0, 3.3 },
		{  8.5, 104.0, 2.7 }, { -6.0, 166.0, 4.6 }, {  15.0, 171.0, 6.2 },
	};
	for (const auto& S : kKnollSentinels)
	{
		StandTree(KomorebiCore::CrestX(S[1]) + S[0], S[1], S[2], true);
	}

	// Clumps down the sheltered flanks (pines.js lines 43-48).
	for (int32 I = 0; I < 9000; ++I)
	{
		const double X = (Rand() * 2.0 - 1.0) * 440.0;
		const double Z = -330.0 + Rand() * 780.0;
		const double Y = SampleHeight(X, Z);
		if (Y < 10.0 || Y > 150.0 + 70.0 * KomorebiCore::SmoothStep(100.0, 250.0, Z)
			|| (std::abs(X - KomorebiCore::CrestX(Z)) < 100.0 && Z > 165.0))
		{
			continue;
		}
		double Rock = 0.0;
		double Horn = 0.0;
		SampleLand(X, Z, Rock, Horn);
		if (KomorebiCore::Noise(X / 37.0 + 1.0, Z / 37.0 + 6.0) < 0.12
			|| Rock > 0.4
			|| SampleNormal(X, Z).Y < 0.7)
		{
			continue;
		}
		if (HasRoom(X, Z, 3.0))
		{
			StandTree(X, Z, 3.0 + 6.0 * Rand(), false);
		}
	}

	// Sort north-to-south matching pines.js line 50.
	Trees.Sort([](const FPineTree& A, const FPineTree& B)
	{
		return A.Z < B.Z;
	});

	TArray<FTransform> ConeTransforms;
	TArray<FTransform> SnowCapTransforms;
	ConeTransforms.Reserve(Trees.Num());
	SnowCapTransforms.Reserve(Trees.Num() * 2);

	for (const FPineTree& T : Trees)
	{
		const double Yaw = Rand() * 360.0;
		const FRotator Rot(0.0, Yaw, 0.0);

		// Main evergreen spire cone.
		const FVector ConeLoc = KomorebiBridge::ThreeXZToFVector(T.X, T.Y + T.H * 0.49, T.Z);
		const FVector ConeScale(
			static_cast<float>(T.R * 1.44),
			static_cast<float>(T.R * 1.44),
			static_cast<float>(T.H * 0.98));
		ConeTransforms.Add(FTransform(Rot, ConeLoc, ConeScale));

		// Snow-laden upper crown cap & mid-tier snow mantle.
		const FVector TopCapLoc = KomorebiBridge::ThreeXZToFVector(T.X, T.Y + T.H * 0.76, T.Z);
		const FVector TopCapScale(
			static_cast<float>(T.R * 1.08),
			static_cast<float>(T.R * 1.08),
			static_cast<float>(T.H * 0.52));
		SnowCapTransforms.Add(FTransform(Rot, TopCapLoc, TopCapScale));

		if (T.bFull)
		{
			const FVector MidCapLoc = KomorebiBridge::ThreeXZToFVector(T.X, T.Y + T.H * 0.44, T.Z);
			const FVector MidCapScale(
				static_cast<float>(T.R * 1.56),
				static_cast<float>(T.R * 1.56),
				static_cast<float>(T.H * 0.36));
			SnowCapTransforms.Add(FTransform(Rot, MidCapLoc, MidCapScale));
		}
	}

	if (PineConesHISM && ConeTransforms.Num() > 0)
	{
		PineConesHISM->AddInstances(ConeTransforms, false, true);
	}
	if (PineSnowCapsHISM && SnowCapTransforms.Num() > 0)
	{
		PineSnowCapsHISM->AddInstances(SnowCapTransforms, false, true);
	}
}

void ASnowboundWorldActor::InitializeSnowTrackTarget()
{
	const int32 Res = FMath::Clamp(SnowTrackResolution, 256, 2048);
	if (!SnowTrackRenderTarget
		|| SnowTrackRenderTarget->SizeX != Res
		|| SnowTrackRenderTarget->SizeY != Res)
	{
		SnowTrackRenderTarget = UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(
			this,
			UCanvasRenderTarget2D::StaticClass(),
			Res,
			Res);
	}

	if (SnowTrackRenderTarget)
	{
		SnowTrackRenderTarget->bShouldClearRenderTargetOnReceiveUpdate = false;
		SnowTrackRenderTarget->ClearColor = FLinearColor::Black;
		SnowTrackRenderTarget->AddressX = TA_Clamp;
		SnowTrackRenderTarget->AddressY = TA_Clamp;
		UKismetRenderingLibrary::ClearRenderTarget2D(this, SnowTrackRenderTarget, FLinearColor::Black);
		if (TerrainActor)
		{
			TerrainActor->SetSnowTrackTexture(SnowTrackRenderTarget);
		}
	}
}

void ASnowboundWorldActor::StampFootprint(const FVector& WorldPos, float RadiusCm)
{
	if (!SnowTrackRenderTarget || !GetWorld())
	{
		return;
	}

	if (IsInsideCabin(WorldPos))
	{
		return;
	}

	const KomorebiCore::Vec3 ThreePos = KomorebiBridge::FVectorToThree(WorldPos);
	double Rock = 0.0;
	double Horn = 0.0;
	SampleLand(ThreePos.X, ThreePos.Z, Rock, Horn);
	if (Rock > 0.5)
	{
		// Rock keeps no prints (legacy-web/src/night/tracks.js line 17).
		return;
	}

	const double NormU = (ThreePos.X + KomorebiCore::kSnowExtent) / (2.0 * KomorebiCore::kSnowExtent);
	const double NormV = (ThreePos.Z + KomorebiCore::kSnowExtent) / (2.0 * KomorebiCore::kSnowExtent);
	if (NormU < 0.0 || NormU > 1.0 || NormV < 0.0 || NormV > 1.0)
	{
		return;
	}

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, SnowTrackRenderTarget, Canvas, CanvasSize, Context);

	if (Canvas && CanvasSize.X > 0.0f && CanvasSize.Y > 0.0f)
	{
		const float Px = static_cast<float>(NormU) * CanvasSize.X;
		const float Py = static_cast<float>(NormV) * CanvasSize.Y;
		const float RadiusMeters = FMath::Max(0.08f, RadiusCm * 0.01f);
		const float PixelsPerMeter = CanvasSize.X / static_cast<float>(2.0 * KomorebiCore::kSnowExtent);
		const float RadiusPx = FMath::Max(1.75f, RadiusMeters * PixelsPerMeter);

		// R channel = footprint hollow depth; G channel = moonlit pushed snow lip offset away from moonlight.
		const KomorebiCore::Vec3 Moon = KomorebiCore::MoonDir();
		const float LipDx = static_cast<float>(-Moon.X) * RadiusPx * 0.55f;
		const float LipDy = static_cast<float>(-Moon.Z) * RadiusPx * 0.55f;

		Canvas->K2_DrawPolygon(
			nullptr,
			FVector2D(Px + LipDx, Py + LipDy),
			FVector2D(RadiusPx * 1.18f, RadiusPx * 1.18f),
			12,
			FLinearColor(0.0f, 0.85f, 0.0f, 0.65f));

		Canvas->K2_DrawPolygon(
			nullptr,
			FVector2D(Px, Py),
			FVector2D(RadiusPx, RadiusPx),
			12,
			FLinearColor(1.0f, 0.0f, 0.0f, 0.92f));
	}

	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
}

void ASnowboundWorldActor::LayInitialTrail(double StartZMeters, double EndZMeters)
{
	const double Dir = (EndZMeters >= StartZMeters) ? 1.0 : -1.0;
	bool bLeft = false;

	for (double Z = StartZMeters; (EndZMeters - Z) * Dir > 0.0; Z += Dir * 0.72)
	{
		const double X = KomorebiCore::CrestX(Z);
		const double Yaw = std::atan2(KomorebiCore::CrestX(Z + Dir) - X, Dir);
		bLeft = !bLeft;
		const double Side = (bLeft ? 0.1 : -0.1) + std::sin(Z * 12.9898) * 0.03;
		const double PrintX = X + std::cos(Yaw) * Side;
		const double PrintZ = Z - std::sin(Yaw) * Side;
		const double PrintY = SampleHeight(PrintX, PrintZ);
		StampFootprint(KomorebiBridge::ThreeXZToFVector(PrintX, PrintY, PrintZ), 22.0f);
	}
}

void ASnowboundWorldActor::UpdatePlayerStrideTracks(
	const FVector& PlayerPosUE,
	float PlayerYawDeg,
	float SpeedMps,
	bool bGrounded)
{
	const KomorebiCore::Vec3 ThreePos = KomorebiBridge::FVectorToThree(PlayerPosUE);
	if (!bHasLastTrackPos)
	{
		LastTrackThreeX = ThreePos.X;
		LastTrackThreeZ = ThreePos.Z;
		bHasLastTrackPos = true;
		return;
	}

	const double Moved = std::hypot(ThreePos.X - LastTrackThreeX, ThreePos.Z - LastTrackThreeZ);
	LastTrackThreeX = ThreePos.X;
	LastTrackThreeZ = ThreePos.Z;

	if (!bGrounded || Moved > 3.0)
	{
		return;
	}

	DistSinceLastPrint += Moved;
	const double Stride = 0.62 + 0.09 * FMath::Min<double>(SpeedMps, 6.0);
	if (DistSinceLastPrint < Stride)
	{
		return;
	}
	DistSinceLastPrint = 0.0;
	bLeftFootNext = !bLeftFootNext;

	const double ThreeYaw = KomorebiCore::UEYawDegToThreeYaw(PlayerYawDeg);
	const double Side = bLeftFootNext ? 0.1 : -0.1;
	const double PrintX = ThreePos.X + std::cos(ThreeYaw) * Side;
	const double PrintZ = ThreePos.Z - std::sin(ThreeYaw) * Side;
	StampFootprint(KomorebiBridge::ThreeXZToFVector(PrintX, ThreePos.Y, PrintZ), 23.0f);
}

bool ASnowboundWorldActor::IsInsideCabin(const FVector& PlayerPosUE) const
{
	const KomorebiCore::Vec3 P = KomorebiBridge::FVectorToThree(PlayerPosUE);
	const KomorebiCore::Vec3 Cabin = KomorebiCore::SnowCabin();
	return std::abs(P.X - Cabin.X) < 3.0 && std::abs(P.Z - Cabin.Z) < 2.45;
}

bool ASnowboundWorldActor::IsAtCabinThreshold(const FVector& PlayerPosUE) const
{
	if (IsInsideCabin(PlayerPosUE))
	{
		return true;
	}
	const KomorebiCore::Vec3 P = KomorebiBridge::FVectorToThree(PlayerPosUE);
	const KomorebiCore::Vec3 Cabin = KomorebiCore::SnowCabin();
	return std::abs(P.X - Cabin.X - 1.75) < 0.9
		&& (P.Z - Cabin.Z) >= 2.45
		&& (P.Z - Cabin.Z) < 4.3;
}

void ASnowboundWorldActor::UpdateCabinCutoutForLocation(const FVector& PlayerPosUE)
{
	const bool bShouldHideNearWall = IsAtCabinThreshold(PlayerPosUE);
	if (bShouldHideNearWall != bNearWallHidden)
	{
		bNearWallHidden = bShouldHideNearWall;
		if (CabinNearWallCutoutHISM)
		{
			CabinNearWallCutoutHISM->SetVisibility(!bNearWallHidden, true);
		}
	}
}

void ASnowboundWorldActor::InitializeDriftingSnow()
{
	if (!DriftingSnowMesh)
	{
		return;
	}

	UStaticMesh* FlakeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (FlakeMesh && !DriftingSnowMesh->GetStaticMesh())
	{
		DriftingSnowMesh->SetStaticMesh(FlakeMesh);
	}

	MaxDriftCount = 2400;
	ActiveDriftCount = FMath::Clamp(ActiveDriftCount, 200, MaxDriftCount);
	DriftSeeds.SetNumUninitialized(MaxDriftCount);

	KomorebiCore::Rng Rand(84211u);
	for (int32 I = 0; I < MaxDriftCount; ++I)
	{
		FDriftParticleSeed& Seed = DriftSeeds[I];
		Seed.Rx = static_cast<float>(Rand());
		Seed.Ry = static_cast<float>(Rand());
		Seed.Rz = static_cast<float>(Rand());
		Seed.Rw = static_cast<float>(Rand());
		Seed.Qx = static_cast<float>(Rand());
		Seed.Qy = static_cast<float>(Rand());
		Seed.Qz = static_cast<float>(Rand());
		Seed.Qw = static_cast<float>(Rand());
		Seed.bFlake = (Seed.Qx > 0.14f);
	}

	DriftingSnowMesh->ClearInstances();
	TArray<FTransform> InitialTransforms;
	InitialTransforms.Init(FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector(0.02f)), ActiveDriftCount);
	DriftingSnowMesh->AddInstances(InitialTransforms, false, true);
}

void ASnowboundWorldActor::SetSnowDriftCount(int32 Count)
{
	const int32 Clamped = FMath::Clamp(Count, 0, MaxDriftCount);
	if (Clamped == ActiveDriftCount && DriftingSnowMesh && DriftingSnowMesh->GetInstanceCount() == Clamped)
	{
		return;
	}

	ActiveDriftCount = Clamped;
	if (!DriftingSnowMesh)
	{
		return;
	}

	DriftingSnowMesh->ClearInstances();
	if (ActiveDriftCount > 0)
	{
		TArray<FTransform> Transforms;
		Transforms.Init(FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector(0.02f)), ActiveDriftCount);
		DriftingSnowMesh->AddInstances(Transforms, false, true);
	}
}

void ASnowboundWorldActor::UpdateDriftingSnow(float TimeSec, const FVector& FocusUE)
{
	if (!DriftingSnowMesh || ActiveDriftCount <= 0 || DriftSeeds.Num() < ActiveDriftCount)
	{
		return;
	}

	// Exact 56m x 14m x 56m wrapping air box and wind vector (0.8, 0.6) from legacy-web/src/night/drift.js.
	constexpr double BoxX = 56.0;
	constexpr double BoxY = 14.0;
	constexpr double BoxZ = 56.0;
	constexpr double WindX = 0.8;
	constexpr double WindZ = 0.6;

	const KomorebiCore::Vec3 FocusThree = KomorebiBridge::FVectorToThree(FocusUE);
	const KomorebiCore::Vec3 Cabin = KomorebiCore::SnowCabin();

	auto WrapAxis = [](double Val, double Origin, double Span) -> double
	{
		const double Half = Span * 0.5;
		double Rel = std::fmod(Val - Origin + Half, Span);
		if (Rel < 0.0)
		{
			Rel += Span;
		}
		return Rel - Half + Origin;
	};

	TArray<FTransform> BatchTransforms;
	BatchTransforms.SetNumUninitialized(ActiveDriftCount);

	for (int32 I = 0; I < ActiveDriftCount; ++I)
	{
		const FDriftParticleSeed& S = DriftSeeds[I];
		double Px = S.Rx * BoxX + WindX * TimeSec * (S.bFlake ? (1.1 + 1.4 * S.Qy) : (3.2 + 3.6 * S.Qy));
		double Py = S.Ry * BoxY;
		double Pz = S.Rz * BoxZ + WindZ * TimeSec * (S.bFlake ? (1.1 + 1.4 * S.Qy) : (3.2 + 3.6 * S.Qy));

		if (S.bFlake)
		{
			Py -= TimeSec * (0.35 + 0.4 * S.Rw);
			Px += std::sin(TimeSec * (0.7 + S.Qz) + S.Qw * 20.0) * 0.5;
			Pz += std::cos(TimeSec * (0.9 + S.Qw) + S.Qz * 20.0) * 0.5;
		}

		Px = WrapAxis(Px, FocusThree.X, BoxX);
		Py = WrapAxis(Py, FocusThree.Y + 5.0, BoxY);
		Pz = WrapAxis(Pz, FocusThree.Z, BoxZ);

		const bool bInsideRoom = std::abs(Px - Cabin.X) < 3.15 && std::abs(Pz - Cabin.Z) < 2.55;
		const double GroundY = SampleHeight(Px, Pz);

		if (!S.bFlake)
		{
			Py = GroundY + 0.12 + 0.45 * S.Ry * S.Ry;
		}

		if (bInsideRoom || Py < GroundY + 0.05)
		{
			BatchTransforms[I] = FTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, -100000.0f), FVector::ZeroVector);
			continue;
		}

		const FVector LocUE = KomorebiBridge::ThreeXZToFVector(Px, Py, Pz);
		const float SizeScale = S.bFlake ? (0.045f + 0.035f * S.Qz) : (0.04f + 0.06f * S.Qz);
		const FVector Scale = S.bFlake
			? FVector(SizeScale)
			: FVector(SizeScale * 3.0f, SizeScale * 0.6f, SizeScale * 0.6f);
		BatchTransforms[I] = FTransform(FRotator::ZeroRotator, LocUE, Scale);
	}

	DriftingSnowMesh->BatchUpdateInstancesTransforms(0, BatchTransforms, true, true, true);
}

void ASnowboundWorldActor::SetAuroraQuality(EKomorebiQualityTier EffectiveTier)
{
	if (AuroraCurtainsMesh && AuroraCurtainsMesh->GetNumSections() > 1)
	{
		// Matches legacy-web/src/night/sky.js line 198: hide secondary veil curtain on Low.
		AuroraCurtainsMesh->SetMeshSectionVisible(1, EffectiveTier != EKomorebiQualityTier::Low);
	}
}

void ASnowboundWorldActor::UpdateLanternLight(const FVector& LanternWorldPosUE, float Strength)
{
	if (!AtmosphereActor)
	{
		ResolveWorldReferences();
	}
	if (AtmosphereActor)
	{
		AtmosphereActor->SetLampState(
			LanternWorldPosUE,
			Strength,
			FLinearColor(1.0f, 0.60f, 0.24f, 1.0f));
	}
}

void ASnowboundWorldActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const double SafeDt = FMath::Clamp(static_cast<double>(DeltaTime), 0.0, 0.1);
	ElapsedTime += SafeDt;

	// Warm hearth firelight organic flicker matching legacy-web/src/night/cabin.js line 429.
	const double FireFlicker = 0.92
		+ 0.10 * std::sin(ElapsedTime * 9.0)
		+ 0.06 * std::sin(ElapsedTime * 23.0 + 1.0)
		+ 0.05 * std::sin(ElapsedTime * 3.7);
	if (HearthFireLight)
	{
		HearthFireLight->SetIntensity(static_cast<float>(3800.0 * FireFlicker));
	}
	if (TableCandleLight)
	{
		const double CandleFlicker = 0.90 + 0.10 * std::sin(ElapsedTime * 11.0) * std::cos(ElapsedTime * 7.3);
		TableCandleLight->SetIntensity(static_cast<float>(920.0 * CandleFlicker));
	}

	FVector FocusUE = KomorebiBridge::ThreeXZToFVector(
		KomorebiCore::SnowStart().X,
		SampleHeight(KomorebiCore::SnowStart().X, KomorebiCore::SnowStart().Z),
		KomorebiCore::SnowStart().Z);

	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (const APawn* Pawn = PC->GetPawn())
			{
				FocusUE = Pawn->GetActorLocation();
				UpdateCabinCutoutForLocation(FocusUE);

				const FVector Vel = Pawn->GetVelocity();
				const float HorizSpeedMps = static_cast<float>(Vel.Size2D() * KomorebiCore::kCmToMeters);
				const bool bGrounded = FMath::Abs(Vel.Z) < 35.0f;
				UpdatePlayerStrideTracks(FocusUE, Pawn->GetActorRotation().Yaw, HorizSpeedMps, bGrounded);
			}
		}
	}

	if (AuroraSkyDomeMesh)
	{
		AuroraSkyDomeMesh->SetWorldLocation(FocusUE);
	}
	if (AuroraCurtainsMesh)
	{
		AuroraCurtainsMesh->SetWorldLocation(FocusUE);
	}

	UpdateDriftingSnow(static_cast<float>(ElapsedTime), FocusUE);
}
