// Copyright Komorebi Project. World subsystem driving 6 sector ops, relays, gates, Amber Sanctuary, 12 Echo Shrines, 8 kiosks, and radio comms.
#include "Campaign/MissionSubsystem.h"

#include "Audio/KomorebiAudioSubsystem.h"
#include "Campaign/KomorebiSaveGame.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Game/AudioTheory.h"
#include "Game/DuelStateMachine.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "World/WorldGrid.h"

#include <cmath>
#include <string>

DEFINE_LOG_CATEGORY_STATIC(LogKomorebiMission, Log, All);

namespace
{
	const FLinearColor kColorMission(1.0f, 0.165f, 0.427f, 1.0f);   // #ff2a6d
	const FLinearColor kColorRing(0.0f, 0.941f, 1.0f, 1.0f);        // #00f0ff
	const FLinearColor kColorTerminal(0.988f, 0.933f, 0.039f, 1.0f);// #fcee0a
	const FLinearColor kColorKiosk(0.259f, 0.961f, 0.608f, 1.0f);   // #42f59b
	const FLinearColor kColorRival(1.0f, 0.353f, 0.302f, 1.0f);     // #ff5a4d
	const FLinearColor kColorGoal(1.0f, 0.690f, 0.0f, 1.0f);        // #ffb000
	const FLinearColor kColorEchoCyan(0.098f, 1.0f, 0.776f, 1.0f);  // #19ffc6
	const FLinearColor kColorEchoGold(1.0f, 0.690f, 0.0f, 1.0f);    // #ffb000
	const FLinearColor kColorRelay(0.259f, 0.851f, 0.937f, 1.0f);   // #42d9ef
	const FLinearColor kColorGateVeil(0.612f, 0.863f, 0.800f, 1.0f);// #9cdccc

	FLinearColor HexToLinear(std::string_view Hex, const FLinearColor& Fallback)
	{
		if (Hex.empty())
		{
			return Fallback;
		}
		const FString HexStr = KomorebiBridge::ToFString(Hex);
		return FLinearColor(FColor::FromHex(HexStr));
	}

	double SnapToRoadLine(double Value, const std::vector<double>& Lines)
	{
		if (Lines.empty())
		{
			return Value;
		}
		double Best = Lines[0];
		double BestDist = std::abs(Best - Value);
		for (double Candidate : Lines)
		{
			const double Dist = std::abs(Candidate - Value);
			if (Dist < BestDist)
			{
				Best = Candidate;
				BestDist = Dist;
			}
		}
		return Best;
	}
}

// --- FSpawnedVisual ----------------------------------------------------------------------------

void UMissionSubsystem::FSpawnedVisual::SetVisible(bool bVis) const
{
	if (Actor.IsValid())
	{
		Actor->SetActorHiddenInGame(!bVis);
	}
}

void UMissionSubsystem::FSpawnedVisual::DisableCollider() const
{
	if (ColliderComp.IsValid())
	{
		ColliderComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void UMissionSubsystem::FSpawnedVisual::DestroyActor()
{
	if (Actor.IsValid() && !Actor->IsActorBeingDestroyed())
	{
		Actor->Destroy();
	}
	Actor.Reset();
	ModelComp.Reset();
	RingComp.Reset();
	SecondaryRingComp.Reset();
	BeamComp.Reset();
	SecondaryBeamComp.Reset();
	ColliderComp.Reset();
	LightComp.Reset();
}

// --- Lifecycle ---------------------------------------------------------------------------------

void UMissionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UMissionSubsystem::Deinitialize()
{
	ClearSpawnedWorldActors();
	Super::Deinitialize();
}

void UMissionSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (InWorld.IsGameWorld() && !bCampaignInitialized)
	{
		InitializeCampaign(EKomorebiPlayMode::StorySectors, false);
	}
}

bool UMissionSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return bCampaignInitialized && World && World->IsGameWorld() && !World->bIsTearingDown;
}

TStatId UMissionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMissionSubsystem, STATGROUP_Tickables);
}

// --- Campaign Setup ----------------------------------------------------------------------------

void UMissionSubsystem::InitializeCampaign(EKomorebiPlayMode PlayMode, bool bFreshCampaign)
{
	ClearSpawnedWorldActors();

	CurrentPlayMode = PlayMode;
	const bool bRoam = (PlayMode != EKomorebiPlayMode::StorySectors);

	if (!CustomHeightProvider && !FallbackTerrain.IsPrepared())
	{
		FallbackTerrain.Prepare();
	}

	ActiveSaveGame = UKomorebiSaveManager::LoadOrCreate(
		UKomorebiSaveManager::DefaultSlotName,
		UKomorebiSaveManager::DefaultUserIndex,
		bFreshCampaign);

	DoneSectorOps.Empty();
	AnnouncedFieldClearSectors.Empty();
	CollectedEchoIds.Empty();
	CommQueue.Empty();
	CommHoldTimer = 0.0f;
	bHasActiveComms = false;
	bHasActiveContract = false;
	bCourierDefeated = false;
	bRunTold = false;

	if (ActiveSaveGame)
	{
		ClearedSectors = bRoam ? 0 : ActiveSaveGame->ClearedSectors;
		bWon = bRoam ? false : ActiveSaveGame->bWon;

		for (int32 i = 0; i < ClearedSectors; ++i)
		{
			DoneSectorOps.Add(i);
		}
		for (int32 CompletedSec : ActiveSaveGame->CompletedMissionSectors)
		{
			if (!bRoam)
			{
				DoneSectorOps.Add(CompletedSec);
			}
		}
		for (const FString& EchoId : ActiveSaveGame->CollectedEchoIds)
		{
			CollectedEchoIds.Add(EchoId);
		}
	}
	else
	{
		ClearedSectors = 0;
		bWon = false;
	}

	const std::vector<KomorebiCore::LevelSpec>& Levels = KomorebiCore::GetLevels();
	if (!Levels.empty())
	{
		const int32 CpIdx = FMath::Clamp(ClearedSectors, 0, static_cast<int32>(Levels.size()) - 1);
		const KomorebiCore::NamedPoint& Cp = Levels[CpIdx].Checkpoint;
		ActiveRespawnLocation = ThreeToUE(Cp.X, SampleTerrainHeight(Cp.X, Cp.Z), Cp.Z);
	}

	BuildSectorOperations();
	BuildEchoShrines();
	BuildContractKiosks();
	BuildRelaysAndSanctuary();
	BuildBarrierGates();

	bCampaignInitialized = true;

	const KomorebiCore::Vec3 PlayerThree = KomorebiBridge::FVectorToThree(GetLivePlayerLocation());
	RebuildCachedHUDData(PlayerThree.X, PlayerThree.Z);
}

void UMissionSubsystem::SetPropMeshOverride(FName AssetKey, UStaticMesh* Mesh)
{
	if (Mesh)
	{
		PropMeshOverrides.Add(AssetKey, Mesh);
	}
	else
	{
		PropMeshOverrides.Remove(AssetKey);
	}
}

void UMissionSubsystem::SetGlowMaterialOverride(UMaterialInterface* GlowMat)
{
	GlowMaterialOverride = GlowMat;
}

void UMissionSubsystem::SetTerrainHeightProvider(TFunction<double(double, double)> InHeightProvider)
{
	CustomHeightProvider = MoveTemp(InHeightProvider);
}

double UMissionSubsystem::SampleTerrainHeight(double ThreeX, double ThreeZ) const
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

FVector UMissionSubsystem::ThreeToUE(double ThreeX, double ThreeY, double ThreeZ) const
{
	return KomorebiBridge::ThreeXZToFVector(ThreeX, ThreeY, ThreeZ);
}

FVector UMissionSubsystem::GetLivePlayerLocation() const
{
	if (WorldContext.bValid)
	{
		return WorldContext.PlayerLocation;
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
	return ActiveRespawnLocation;
}

void UMissionSubsystem::ClearSpawnedWorldActors()
{
	for (FSectorOpRuntime& Op : SectorOps)
	{
		for (FMissionTargetRuntime& Target : Op.Targets)
		{
			Target.Visual.DestroyActor();
		}
	}
	SectorOps.Empty();

	for (FEchoShrineRuntime& Shrine : EchoShrines)
	{
		Shrine.Visual.DestroyActor();
	}
	EchoShrines.Empty();

	for (FKioskRuntime& Kiosk : Kiosks)
	{
		Kiosk.Visual.DestroyActor();
	}
	Kiosks.Empty();

	for (FRelayRuntime& Relay : Relays)
	{
		Relay.Visual.DestroyActor();
	}
	Relays.Empty();

	for (FBarrierGateRuntime& Gate : Gates)
	{
		Gate.Visual.DestroyActor();
	}
	Gates.Empty();

	if (bHasActiveContract)
	{
		for (FMissionTargetRuntime& Target : ActiveContract.Targets)
		{
			Target.Visual.DestroyActor();
		}
		ActiveContract.Targets.Empty();
		bHasActiveContract = false;
	}
}

// --- Visual Builders ---------------------------------------------------------------------------

UStaticMesh* UMissionSubsystem::ResolveMesh(FName AssetKey, const TCHAR* FallbackEngineMeshPath) const
{
	if (const TObjectPtr<UStaticMesh>* Found = PropMeshOverrides.Find(AssetKey))
	{
		if (*Found)
		{
			return Found->Get();
		}
	}
	return LoadObject<UStaticMesh>(nullptr, FallbackEngineMeshPath);
}

UMaterialInstanceDynamic* UMissionSubsystem::CreateEmissiveMaterial(
	UObject* Outer,
	const FLinearColor& Color,
	float EmissiveBoost) const
{
	UMaterialInterface* BaseMat = GlowMaterialOverride.Get();
	if (!BaseMat)
	{
		BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	if (!BaseMat)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Dyn = UMaterialInstanceDynamic::Create(BaseMat, Outer);
	if (Dyn)
	{
		const FLinearColor Boosted(Color.R * EmissiveBoost, Color.G * EmissiveBoost, Color.B * EmissiveBoost, Color.A);
		Dyn->SetVectorParameterValue(TEXT("Color"), Boosted);
		Dyn->SetVectorParameterValue(TEXT("BaseColor"), Color);
		Dyn->SetVectorParameterValue(TEXT("EmissiveColor"), Boosted);
	}
	return Dyn;
}

UMissionSubsystem::FSpawnedVisual UMissionSubsystem::SpawnPropVisual(
	FName AssetKey,
	double ThreeX,
	double ThreeZ,
	const FLinearColor& Accent,
	float RingRadiusMeters,
	float BeamHeightMeters,
	bool bWithCollider,
	float PropHeightMeters)
{
	FSpawnedVisual Vis;
	UWorld* World = GetWorld();
	if (!World)
	{
		return Vis;
	}

	const double GroundY = SampleTerrainHeight(ThreeX, ThreeZ);
	const FVector Location = ThreeToUE(ThreeX, GroundY, ThreeZ);

	FActorSpawnParameters Params;
	Params.ObjectFlags = RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator, Params);
	if (!Actor)
	{
		return Vis;
	}
	Actor->SetActorTickEnabled(false);

	USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();
	Actor->SetActorLocation(Location);

	// Prop body mesh
	UStaticMeshComponent* Model = NewObject<UStaticMeshComponent>(Actor, TEXT("PropModel"));
	Model->SetupAttachment(Root);
	Model->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	const bool bIsCylinder = (AssetKey == TEXT("power-core") || AssetKey == TEXT("beacon"));
	if (UStaticMesh* Mesh = ResolveMesh(AssetKey, bIsCylinder ? TEXT("/Engine/BasicShapes/Cylinder.Cylinder") : TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		Model->SetStaticMesh(Mesh);
	}
	const bool bUsingOverride = PropMeshOverrides.Contains(AssetKey);
	if (!bUsingOverride)
	{
		Model->SetRelativeLocation(FVector(0.0f, 0.0f, PropHeightMeters * 50.0f));
		Model->SetRelativeScale3D(FVector(0.95f, 0.95f, PropHeightMeters));
		if (UMaterialInstanceDynamic* BodyMat = CreateEmissiveMaterial(Model, Accent * 0.45f, 1.0f))
		{
			Model->SetMaterial(0, BodyMat);
		}
	}
	Model->RegisterComponent();

	// Glowing ground ring (flattened emissive disc rim)
	UStaticMeshComponent* Ring = NewObject<UStaticMeshComponent>(Actor, TEXT("GroundRing"));
	Ring->SetupAttachment(Root);
	Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ring->SetCastShadow(false);
	if (UStaticMesh* CylMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
	{
		Ring->SetStaticMesh(CylMesh);
	}
	Ring->SetRelativeLocation(FVector(0.0f, 0.0f, 6.0f));
	Ring->SetRelativeScale3D(FVector(RingRadiusMeters * 2.0f, RingRadiusMeters * 2.0f, 0.02f));
	if (UMaterialInstanceDynamic* RingMat = CreateEmissiveMaterial(Ring, Accent, 2.4f))
	{
		Ring->SetMaterial(0, RingMat);
	}
	Ring->RegisterComponent();

	// Additive-style skyward beacon beam
	UStaticMeshComponent* Beam = NewObject<UStaticMeshComponent>(Actor, TEXT("BeaconBeam"));
	Beam->SetupAttachment(Root);
	Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Beam->SetCastShadow(false);
	if (UStaticMesh* CylMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
	{
		Beam->SetStaticMesh(CylMesh);
	}
	Beam->SetRelativeLocation(FVector(0.0f, 0.0f, BeamHeightMeters * 50.0f));
	Beam->SetRelativeScale3D(FVector(0.28f, 0.28f, BeamHeightMeters));
	if (UMaterialInstanceDynamic* BeamMat = CreateEmissiveMaterial(Beam, Accent, 3.0f))
	{
		Beam->SetMaterial(0, BeamMat);
	}
	Beam->RegisterComponent();

	// Soft neon ground wash point light
	UPointLightComponent* Light = NewObject<UPointLightComponent>(Actor, TEXT("AccentLight"));
	Light->SetupAttachment(Root);
	Light->SetRelativeLocation(FVector(0.0f, 0.0f, 160.0f));
	Light->SetLightColor(Accent);
	Light->SetIntensity(2200.0f);
	Light->SetAttenuationRadius(FMath::Max(450.0f, RingRadiusMeters * 260.0f));
	Light->SetCastShadows(false);
	Light->RegisterComponent();

	if (bWithCollider)
	{
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor, TEXT("Collider"));
		Box->SetupAttachment(Root);
		Box->SetBoxExtent(FVector(65.0f, 65.0f, 90.0f));
		Box->SetRelativeLocation(FVector(0.0f, 0.0f, 90.0f));
		Box->SetCollisionProfileName(TEXT("BlockAll"));
		Box->RegisterComponent();
		Vis.ColliderComp = Box;
	}

	Vis.Actor = Actor;
	Vis.ModelComp = Model;
	Vis.RingComp = Ring;
	Vis.BeamComp = Beam;
	Vis.LightComp = Light;
	return Vis;
}

UMissionSubsystem::FSpawnedVisual UMissionSubsystem::SpawnRingGateVisual(
	double ThreeX,
	double ThreeZ,
	double ThreeYawRad,
	const FLinearColor& Accent)
{
	FSpawnedVisual Vis;
	UWorld* World = GetWorld();
	if (!World)
	{
		return Vis;
	}

	const double GroundY = SampleTerrainHeight(ThreeX, ThreeZ);
	const FVector Location = ThreeToUE(ThreeX, GroundY, ThreeZ);
	const FRotator Rotation(0.0f, static_cast<float>(KomorebiCore::ThreeYawToUEYawDeg(ThreeYawRad)), 0.0f);

	FActorSpawnParameters Params;
	Params.ObjectFlags = RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), Location, Rotation, Params);
	if (!Actor)
	{
		return Vis;
	}
	Actor->SetActorTickEnabled(false);

	USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();
	Actor->SetActorLocationAndRotation(Location, Rotation);

	// Floating ring hoop proxy (rotated cylinder/disc arch at 2.6m height)
	UStaticMeshComponent* Hoop = NewObject<UStaticMeshComponent>(Actor, TEXT("RingHoop"));
	Hoop->SetupAttachment(Root);
	Hoop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Hoop->SetCastShadow(false);
	if (UStaticMesh* CylMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
	{
		Hoop->SetStaticMesh(CylMesh);
	}
	Hoop->SetRelativeLocation(FVector(0.0f, 0.0f, 260.0f));
	Hoop->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
	Hoop->SetRelativeScale3D(FVector(6.8f, 6.8f, 0.22f));
	if (UMaterialInstanceDynamic* Mat = CreateEmissiveMaterial(Hoop, Accent, 3.0f))
	{
		Hoop->SetMaterial(0, Mat);
	}
	Hoop->RegisterComponent();

	// Ground pad (4.2m radius)
	UStaticMeshComponent* Pad = NewObject<UStaticMeshComponent>(Actor, TEXT("GroundPad"));
	Pad->SetupAttachment(Root);
	Pad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Pad->SetCastShadow(false);
	if (UStaticMesh* CylMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
	{
		Pad->SetStaticMesh(CylMesh);
	}
	Pad->SetRelativeLocation(FVector(0.0f, 0.0f, 6.0f));
	Pad->SetRelativeScale3D(FVector(8.4f, 8.4f, 0.02f));
	if (UMaterialInstanceDynamic* Mat = CreateEmissiveMaterial(Pad, Accent, 2.2f))
	{
		Pad->SetMaterial(0, Mat);
	}
	Pad->RegisterComponent();

	// Vertical beam (14m)
	UStaticMeshComponent* Beam = NewObject<UStaticMeshComponent>(Actor, TEXT("RingBeam"));
	Beam->SetupAttachment(Root);
	Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Beam->SetCastShadow(false);
	if (UStaticMesh* CylMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
	{
		Beam->SetStaticMesh(CylMesh);
	}
	Beam->SetRelativeLocation(FVector(0.0f, 0.0f, 700.0f));
	Beam->SetRelativeScale3D(FVector(0.24f, 0.24f, 14.0f));
	if (UMaterialInstanceDynamic* Mat = CreateEmissiveMaterial(Beam, Accent, 3.0f))
	{
		Beam->SetMaterial(0, Mat);
	}
	Beam->RegisterComponent();

	Vis.Actor = Actor;
	Vis.ModelComp = Hoop;
	Vis.RingComp = Pad;
	Vis.BeamComp = Beam;
	return Vis;
}

UMissionSubsystem::FSpawnedVisual UMissionSubsystem::SpawnEchoShrineVisual(double ThreeX, double ThreeZ, bool bInitiallyVisible)
{
	FSpawnedVisual Vis = SpawnPropVisual(TEXT("echo-shrine"), ThreeX, ThreeZ, kColorEchoCyan, 2.5f, 13.0f, false, 1.5f);
	if (AActor* Actor = Vis.Actor.Get())
	{
		USceneComponent* Root = Actor->GetRootComponent();

		// Inner gold ring (1.5m radius)
		UStaticMeshComponent* GoldRing = NewObject<UStaticMeshComponent>(Actor, TEXT("GoldRing"));
		GoldRing->SetupAttachment(Root);
		GoldRing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		GoldRing->SetCastShadow(false);
		if (UStaticMesh* CylMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
		{
			GoldRing->SetStaticMesh(CylMesh);
		}
		GoldRing->SetRelativeLocation(FVector(0.0f, 0.0f, 7.5f));
		GoldRing->SetRelativeScale3D(FVector(3.0f, 3.0f, 0.02f));
		if (UMaterialInstanceDynamic* Mat = CreateEmissiveMaterial(GoldRing, kColorEchoGold, 3.0f))
		{
			GoldRing->SetMaterial(0, Mat);
		}
		GoldRing->RegisterComponent();

		// Inner gold core beam (9.5m)
		UStaticMeshComponent* CoreBeam = NewObject<UStaticMeshComponent>(Actor, TEXT("GoldCoreBeam"));
		CoreBeam->SetupAttachment(Root);
		CoreBeam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		CoreBeam->SetCastShadow(false);
		if (UStaticMesh* CylMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
		{
			CoreBeam->SetStaticMesh(CylMesh);
		}
		CoreBeam->SetRelativeLocation(FVector(0.0f, 0.0f, 475.0f));
		CoreBeam->SetRelativeScale3D(FVector(0.14f, 0.14f, 9.5f));
		if (UMaterialInstanceDynamic* Mat = CreateEmissiveMaterial(CoreBeam, kColorEchoGold, 3.5f))
		{
			CoreBeam->SetMaterial(0, Mat);
		}
		CoreBeam->RegisterComponent();

		Vis.SecondaryRingComp = GoldRing;
		Vis.SecondaryBeamComp = CoreBeam;
		Vis.SetVisible(bInitiallyVisible);
	}
	return Vis;
}

UMissionSubsystem::FSpawnedVisual UMissionSubsystem::SpawnRelayVisual(double ThreeX, double ThreeZ, bool bIsGoal)
{
	const FLinearColor Color = bIsGoal ? kColorGoal : kColorRelay;
	// Ground ring sits centered on checkpoint (7m radius); relay pedestal offset +8m East matching hunt.js line 400.
	FSpawnedVisual Vis = SpawnPropVisual(TEXT("relay"), ThreeX, ThreeZ, Color, 7.0f, bIsGoal ? 20.0f : 18.0f, false, 2.7f);
	if (Vis.ModelComp.IsValid())
	{
		// Shift pedestal +800cm along UE +Y (Three.js +X) so vehicles don't crash into the center of the road.
		const double PedestalY = SampleTerrainHeight(ThreeX + 8.0, ThreeZ) - SampleTerrainHeight(ThreeX, ThreeZ);
		Vis.ModelComp->SetRelativeLocation(FVector(0.0f, 800.0f, static_cast<float>(PedestalY * 100.0 + 135.0)));
	}
	if (Vis.BeamComp.IsValid())
	{
		Vis.BeamComp->SetRelativeLocation(FVector(0.0f, 800.0f, bIsGoal ? 1000.0f : 900.0f));
	}
	return Vis;
}

UMissionSubsystem::FSpawnedVisual UMissionSubsystem::SpawnBarrierGateVisual(double ThreeX, double ThreeZ)
{
	FSpawnedVisual Vis;
	UWorld* World = GetWorld();
	if (!World)
	{
		return Vis;
	}

	const double GroundY = SampleTerrainHeight(ThreeX, ThreeZ);
	const FVector Location = ThreeToUE(ThreeX, GroundY, ThreeZ);

	FActorSpawnParameters Params;
	Params.ObjectFlags = RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator, Params);
	if (!Actor)
	{
		return Vis;
	}
	Actor->SetActorTickEnabled(false);

	USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("GateRoot"));
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();
	Actor->SetActorLocation(Location);

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* CylMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	// Torii twin pillars at ±5.3m (UE Y = ±530cm)
	for (int32 Side : { -1, 1 })
	{
		const FName PillarName = (Side < 0) ? TEXT("PillarWest") : TEXT("PillarEast");
		UStaticMeshComponent* Pillar = NewObject<UStaticMeshComponent>(Actor, PillarName);
		Pillar->SetupAttachment(Root);
		Pillar->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (CylMesh) Pillar->SetStaticMesh(CylMesh);
		Pillar->SetRelativeLocation(FVector(0.0f, Side * 530.0f, 240.0f));
		Pillar->SetRelativeScale3D(FVector(0.42f, 0.42f, 4.8f));
		if (UMaterialInstanceDynamic* Mat = CreateEmissiveMaterial(Pillar, kColorMission, 1.4f))
		{
			Pillar->SetMaterial(0, Mat);
		}
		Pillar->RegisterComponent();
	}

	// Upper lintel beam (13m wide at 4.8m height)
	UStaticMeshComponent* Lintel = NewObject<UStaticMeshComponent>(Actor, TEXT("GateLintel"));
	Lintel->SetupAttachment(Root);
	Lintel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (CubeMesh) Lintel->SetStaticMesh(CubeMesh);
	Lintel->SetRelativeLocation(FVector(0.0f, 0.0f, 480.0f));
	Lintel->SetRelativeScale3D(FVector(0.55f, 13.0f, 0.30f));
	if (UMaterialInstanceDynamic* Mat = CreateEmissiveMaterial(Lintel, kColorMission, 1.8f))
	{
		Lintel->SetMaterial(0, Mat);
	}
	Lintel->RegisterComponent();

	// Energy veil spanning the road gateway (10.5m wide x 3.4m tall)
	UStaticMeshComponent* Veil = NewObject<UStaticMeshComponent>(Actor, TEXT("EnergyVeil"));
	Veil->SetupAttachment(Root);
	Veil->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Veil->SetCastShadow(false);
	if (CubeMesh) Veil->SetStaticMesh(CubeMesh);
	Veil->SetRelativeLocation(FVector(0.0f, 0.0f, 190.0f));
	Veil->SetRelativeScale3D(FVector(0.12f, 10.5f, 3.4f));
	if (UMaterialInstanceDynamic* Mat = CreateEmissiveMaterial(Veil, kColorGateVeil, 2.2f))
	{
		Veil->SetMaterial(0, Mat);
	}
	Veil->RegisterComponent();

	// Wide solid street-crossing barrier collider matching hunt.js cuboid(360, 70, 0.4).
	UBoxComponent* BarrierBox = NewObject<UBoxComponent>(Actor, TEXT("GateBarrierCollider"));
	BarrierBox->SetupAttachment(Root);
	BarrierBox->SetBoxExtent(FVector(40.0f, 36000.0f, 7000.0f));
	BarrierBox->SetRelativeLocation(FVector(0.0f, 0.0f, 6600.0f));
	BarrierBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BarrierBox->SetCollisionObjectType(ECC_WorldStatic);
	BarrierBox->SetCollisionResponseToAllChannels(ECR_Block);
	BarrierBox->RegisterComponent();

	Vis.Actor = Actor;
	Vis.ModelComp = Lintel;
	Vis.BeamComp = Veil;
	Vis.ColliderComp = BarrierBox;
	return Vis;
}

// --- World Element Builders --------------------------------------------------------------------

void UMissionSubsystem::BuildSectorOperations()
{
	const bool bRoam = (CurrentPlayMode != EKomorebiPlayMode::StorySectors);
	const std::vector<KomorebiCore::SectorMission>& Specs = KomorebiCore::GetSectorMissions();
	SectorOps.Reserve(static_cast<int32>(Specs.size()));

	for (const KomorebiCore::SectorMission& Spec : Specs)
	{
		FSectorOpRuntime Op;
		Op.Seg = Spec.Seg;
		Op.Type = KomorebiBridge::ToFString(Spec.Type);
		Op.OpTitle = KomorebiBridge::ToFString(Spec.OpTitle);
		Op.StepLabel = KomorebiBridge::ToFString(Spec.StepLabel);
		Op.Tip = KomorebiBridge::ToFString(Spec.Tip);
		Op.bDone = bRoam || DoneSectorOps.Contains(Spec.Seg);

		const int32 TargetCount = static_cast<int32>(Spec.Targets.size());
		for (int32 i = 0; i < TargetCount; ++i)
		{
			const KomorebiCore::MissionTarget& Pt = Spec.Targets[i];
			const KomorebiCore::MissionTarget& NextPt = Spec.Targets[FMath::Min(TargetCount - 1, i + 1)];
			const KomorebiCore::MissionTarget& PrevPt = Spec.Targets[FMath::Max(0, i - 1)];
			const double YawRad = std::atan2(NextPt.X - PrevPt.X, NextPt.Z - PrevPt.Z);

			FMissionTargetRuntime Runtime;
			Runtime.Index = i;
			Runtime.Label = KomorebiBridge::ToFString(Pt.Label);
			Runtime.ThreeX = Pt.X;
			Runtime.ThreeZ = Pt.Z;
			Runtime.ThreeY = SampleTerrainHeight(Pt.X, Pt.Z);
			Runtime.bDone = Op.bDone;

			if (Op.Type == TEXT("rings"))
			{
				Runtime.Kind = ETargetKind::Ring;
				Runtime.Hp = 1.0f;
				Runtime.MaxHp = 1.0f;
				Runtime.Radius = 4.6f;
				Runtime.Visual = SpawnRingGateVisual(Pt.X, Pt.Z, YawRad != 0.0 ? YawRad : KomorebiCore::kPi, kColorRing);
				Runtime.Visual.SetVisible(!Op.bDone && i == 0);
			}
			else if (Op.Type == TEXT("intercept") && Pt.Role == "courier")
			{
				Runtime.Kind = ETargetKind::Courier;
				Runtime.Hp = 50.0f;
				Runtime.MaxHp = 50.0f;
				Runtime.Radius = 1.1f;
				Runtime.Visual = SpawnPropVisual(TEXT("courier-halo"), Pt.X, Pt.Z, kColorMission, 1.1f, 12.0f, false, 0.2f);
				if (Runtime.Visual.ModelComp.IsValid())
				{
					Runtime.Visual.ModelComp->SetVisibility(false);
				}
				Runtime.Visual.SetVisible(false);
			}
			else if (Op.Type == TEXT("intercept") && Pt.Role == "terminal")
			{
				Runtime.Kind = ETargetKind::Terminal;
				Runtime.Hp = 1.0f;
				Runtime.MaxHp = 1.0f;
				Runtime.Radius = 4.5f;
				Runtime.Visual = SpawnPropVisual(TEXT("terminal"), Pt.X, Pt.Z, kColorTerminal, 3.8f, 15.0f, !Op.bDone, 1.6f);
				Runtime.Visual.SetVisible(!Op.bDone);
			}
			else
			{
				Runtime.Kind = ETargetKind::Destructible;
				const FName AssetKey = (Op.Type == TEXT("jammer")) ? TEXT("power-core")
					: (Op.Type == TEXT("beacon")) ? TEXT("beacon")
					: (Op.Type == TEXT("cache")) ? TEXT("supply-crate")
					: TEXT("power-core");
				const float MaxHp = (Op.Type == TEXT("shield")) ? 80.0f
					: (Op.Type == TEXT("cache")) ? 65.0f
					: 55.0f;
				Runtime.MaxHp = MaxHp;
				Runtime.Hp = Op.bDone ? 0.0f : MaxHp;
				Runtime.Radius = 1.5f;
				Runtime.Visual = SpawnPropVisual(AssetKey, Pt.X, Pt.Z, kColorMission, 2.2f, 16.0f, !Op.bDone, (Op.Type == TEXT("shield")) ? 2.4f : 1.7f);
				Runtime.Visual.SetVisible(!Op.bDone);
			}

			Op.Targets.Add(MoveTemp(Runtime));
		}

		SectorOps.Add(MoveTemp(Op));
	}
}

void UMissionSubsystem::BuildEchoShrines()
{
	const std::vector<KomorebiCore::EchoShard>& Shards = KomorebiCore::GetEchoShards();
	EchoShrines.Reserve(static_cast<int32>(Shards.size()));

	for (int32 i = 0; i < static_cast<int32>(Shards.size()); ++i)
	{
		const KomorebiCore::EchoShard& Shard = Shards[i];
		FEchoShrineRuntime Runtime;
		Runtime.Id = KomorebiBridge::ToFString(Shard.Id);
		Runtime.Seg = Shard.Seg;
		Runtime.Title = KomorebiBridge::ToFString(Shard.Title);
		Runtime.Speaker = KomorebiBridge::ToFString(Shard.Speaker);
		Runtime.Quote = KomorebiBridge::ToFString(Shard.Quote);
		Runtime.ThreeX = Shard.X;
		Runtime.ThreeZ = Shard.Z;
		Runtime.ThreeY = SampleTerrainHeight(Shard.X, Shard.Z);
		Runtime.bCollected = CollectedEchoIds.Contains(Runtime.Id);
		Runtime.PhaseOffset = static_cast<float>(i) * 0.85f;
		Runtime.Visual = SpawnEchoShrineVisual(Shard.X, Shard.Z, !Runtime.bCollected);

		EchoShrines.Add(MoveTemp(Runtime));
	}
}

void UMissionSubsystem::BuildContractKiosks()
{
	const std::vector<KomorebiCore::KioskSpot>& Spots = KomorebiCore::GetKioskSpots();
	Kiosks.Reserve(static_cast<int32>(Spots.size()));

	for (const KomorebiCore::KioskSpot& Spot : Spots)
	{
		FKioskRuntime Runtime;
		Runtime.Id = KomorebiBridge::ToFString(Spot.Id);
		Runtime.Name = KomorebiBridge::ToFString(Spot.Name);
		Runtime.Kind = KomorebiBridge::ToFString(Spot.Kind);
		Runtime.ThreeX = Spot.X;
		Runtime.ThreeZ = Spot.Z;
		Runtime.ThreeY = SampleTerrainHeight(Spot.X, Spot.Z);
		Runtime.DirX = Spot.DirX;
		Runtime.DirZ = Spot.DirZ;
		Runtime.Cooldown = 0.0f;
		Runtime.Visual = SpawnPropVisual(TEXT("terminal"), Spot.X, Spot.Z, kColorKiosk, 3.6f, 12.0f, true, 1.65f);

		Kiosks.Add(MoveTemp(Runtime));
	}
}

void UMissionSubsystem::BuildRelaysAndSanctuary()
{
	const std::vector<KomorebiCore::LevelSpec>& Levels = KomorebiCore::GetLevels();
	Relays.Reserve(static_cast<int32>(Levels.size()) + 1);

	for (int32 i = 0; i < static_cast<int32>(Levels.size()); ++i)
	{
		const KomorebiCore::NamedPoint& Cp = Levels[i].Checkpoint;
		FRelayRuntime Relay;
		Relay.SectorIndex = i;
		Relay.Name = KomorebiBridge::ToFString(Cp.Name);
		Relay.bIsGoal = false;
		Relay.ThreeX = Cp.X;
		Relay.ThreeZ = Cp.Z;
		Relay.ThreeY = SampleTerrainHeight(Cp.X, Cp.Z);
		// Player spawns on the current cleared sector relay in Story mode: mark reached relays used so standing still does not toast.
		Relay.bUsed = (CurrentPlayMode == EKomorebiPlayMode::StorySectors) && (i <= ClearedSectors);
		Relay.Visual = SpawnRelayVisual(Cp.X, Cp.Z, false);
		Relays.Add(MoveTemp(Relay));
	}

	const KomorebiCore::NamedPoint& Goal = KomorebiCore::GetGoal();
	FRelayRuntime Sanctuary;
	Sanctuary.SectorIndex = static_cast<int32>(Levels.size());
	Sanctuary.Name = KomorebiBridge::ToFString(Goal.Name);
	Sanctuary.bIsGoal = true;
	Sanctuary.ThreeX = Goal.X;
	Sanctuary.ThreeZ = Goal.Z;
	Sanctuary.ThreeY = SampleTerrainHeight(Goal.X, Goal.Z);
	Sanctuary.bUsed = bWon;
	Sanctuary.Visual = SpawnRelayVisual(Goal.X, Goal.Z, true);
	Relays.Add(MoveTemp(Sanctuary));
}

void UMissionSubsystem::BuildBarrierGates()
{
	if (CurrentPlayMode != EKomorebiPlayMode::StorySectors)
	{
		return;
	}

	const std::vector<KomorebiCore::LevelSpec>& Levels = KomorebiCore::GetLevels();
	for (int32 i = 0; i < static_cast<int32>(Levels.size()); ++i)
	{
		if (!Levels[i].Gate.has_value())
		{
			continue;
		}
		const KomorebiCore::Vec2& GatePos = Levels[i].Gate.value();
		FBarrierGateRuntime Gate;
		Gate.SectorIndex = i;
		Gate.ThreeX = GatePos.X;
		Gate.ThreeZ = GatePos.Z;
		Gate.ThreeY = SampleTerrainHeight(GatePos.X, GatePos.Z);
		Gate.bOpen = (i < ClearedSectors) || bWon;
		Gate.OpenAnim = Gate.bOpen ? 1.0f : 0.0f;
		Gate.Visual = SpawnBarrierGateVisual(GatePos.X, GatePos.Z);
		if (Gate.bOpen)
		{
			Gate.Visual.DisableCollider();
			Gate.Visual.SetVisible(false);
		}
		Gates.Add(MoveTemp(Gate));
	}
}

// --- Frame Tick --------------------------------------------------------------------------------

void UMissionSubsystem::Tick(float DeltaTime)
{
	if (!bCampaignInitialized)
	{
		return;
	}

	const float Dt = FMath::Clamp(DeltaTime, 0.0f, 0.1f);
	TotalElapsedTime += static_cast<double>(Dt);
	GateToastCooldown = FMath::Max(0.0f, GateToastCooldown - Dt);

	AdvanceCommsQueue(Dt);

	const FVector PlayerUE = GetLivePlayerLocation();
	const KomorebiCore::Vec3 PlayerThree = KomorebiBridge::FVectorToThree(PlayerUE);
	const double Px = PlayerThree.X;
	const double Pz = PlayerThree.Z;
	const float PlayerHp = WorldContext.bValid ? WorldContext.PlayerHp : 100.0f;

	// 1. Collectible Memory Echo Shrines (12 across the 6 sectors).
	for (FEchoShrineRuntime& Shrine : EchoShrines)
	{
		if (Shrine.bCollected)
		{
			Shrine.Visual.SetVisible(false);
			continue;
		}

		const double Dist = std::hypot(Px - Shrine.ThreeX, Pz - Shrine.ThreeZ);
		const bool bVisible = Dist < 115.0;
		Shrine.Visual.SetVisible(bVisible);
		if (bVisible)
		{
			const float Pulse = FMath::Sin(static_cast<float>(TotalElapsedTime) * 3.8f + Shrine.PhaseOffset);
			if (Shrine.Visual.RingComp.IsValid())
			{
				const float S = 5.0f * (1.0f + Pulse * 0.07f);
				Shrine.Visual.RingComp->SetRelativeScale3D(FVector(S, S, 0.02f));
			}
			if (Shrine.Visual.SecondaryRingComp.IsValid())
			{
				const float S = 3.0f * (1.0f - Pulse * 0.06f);
				Shrine.Visual.SecondaryRingComp->SetRelativeScale3D(FVector(S, S, 0.02f));
			}
		}

		if (Dist < 3.2)
		{
			Shrine.bCollected = true;
			Shrine.Visual.SetVisible(false);
			CollectedEchoIds.Add(Shrine.Id);

			if (ActiveSaveGame)
			{
				ActiveSaveGame->AddCollectedEcho(Shrine.Id);
				FString SaveWarn;
				if (!UKomorebiSaveManager::SaveWithToastCheck(ActiveSaveGame, SaveWarn) && !SaveWarn.IsEmpty())
				{
					OnToast.Broadcast(SaveWarn);
				}
			}

			PlayAudioSfx(EKomorebiSfx::EchoCollect);
			OnRewardCash.Broadcast(75, FString::Printf(TEXT("Memory Echo recovered · %s"), *Shrine.Title));
			OnHealPlayer.Broadcast(20.0f);
			QueueDirectComms(Shrine.Speaker, Shrine.Quote, 4.8f);
		}
	}

	// 2. Active Story Sector Field Operation.
	const bool bRoam = (CurrentPlayMode != EKomorebiPlayMode::StorySectors);
	const int32 CurrentSeg = FMath::Clamp(ClearedSectors, 0, FMath::Max(0, SectorOps.Num() - 1));
	FSectorOpRuntime* ActiveOp = (!bRoam && !bWon && ClearedSectors < SectorOps.Num()) ? &SectorOps[CurrentSeg] : nullptr;

	for (FSectorOpRuntime& Sop : SectorOps)
	{
		const bool bShowSec = (&Sop == ActiveOp) && !Sop.bDone;
		for (FMissionTargetRuntime& Target : Sop.Targets)
		{
			if (Target.bDone)
			{
				continue;
			}
			const double Dist = std::hypot(Target.ThreeX - Px, Target.ThreeZ - Pz);
			if (Target.Kind == ETargetKind::Destructible || Target.Kind == ETargetKind::Terminal)
			{
				Target.Visual.SetVisible(bShowSec || Dist < 95.0);
				if (Target.Visual.BeamComp.IsValid())
				{
					Target.Visual.BeamComp->SetVisibility(bShowSec && Dist < 170.0);
				}
			}
		}
	}

	const std::vector<KomorebiCore::LevelSpec>& Levels = KomorebiCore::GetLevels();
	if (ActiveOp && !ActiveOp->bDone)
	{
		if (!ActiveOp->bAnnounced
			&& (CurrentSeg == 0 || (CurrentSeg < static_cast<int32>(Levels.size()) && std::abs(Pz - Levels[CurrentSeg].Checkpoint.Z) < 42.0)))
		{
			ActiveOp->bAnnounced = true;
			SpeakComms(CurrentSeg == 0 ? TEXT("intro") : FString::Printf(TEXT("sector%d"), CurrentSeg));
		}

		if (ActiveOp->Type == TEXT("intercept"))
		{
			FMissionTargetRuntime* CourierTarget = ActiveOp->Targets.FindByPredicate([](const FMissionTargetRuntime& T) { return T.Kind == ETargetKind::Courier; });
			if (CourierTarget && !CourierTarget->bDone)
			{
				const bool bCourierAlive = !bCourierDefeated && (!WorldContext.bValid || WorldContext.bCourierAlive);
				if (bCourierAlive)
				{
					if (WorldContext.bValid && !WorldContext.CourierLocation.IsZero())
					{
						const KomorebiCore::Vec3 CThree = KomorebiBridge::FVectorToThree(WorldContext.CourierLocation);
						CourierTarget->ThreeX = CThree.X;
						CourierTarget->ThreeZ = CThree.Z;
						CourierTarget->ThreeY = SampleTerrainHeight(CThree.X, CThree.Z);
					}
					CourierTarget->Visual.SetVisible(true);
					if (AActor* CourierActor = CourierTarget->Visual.Actor.Get())
					{
						CourierActor->SetActorLocation(ThreeToUE(CourierTarget->ThreeX, CourierTarget->ThreeY + 0.05, CourierTarget->ThreeZ));
					}
				}
				else
				{
					CourierTarget->bDone = true;
					CourierTarget->Visual.SetVisible(false);
					PlayAudioSfx(EKomorebiSfx::MissionStep);
					OnToast.Broadcast(TEXT("Cipher key recovered — sync at the Arcade Terminal"));
				}
			}

			FMissionTargetRuntime* TermTarget = ActiveOp->Targets.FindByPredicate([](const FMissionTargetRuntime& T) { return T.Kind == ETargetKind::Terminal; });
			if (TermTarget && !TermTarget->bDone && (!CourierTarget || CourierTarget->bDone))
			{
				const double Dist = std::hypot(Px - TermTarget->ThreeX, Pz - TermTarget->ThreeZ);
				if (Dist < 4.5)
				{
					TermTarget->SyncProgress = FMath::Min(1.0f, TermTarget->SyncProgress + Dt / 2.0f);
					SyncSoundTimer -= Dt;
					if (SyncSoundTimer <= 0.0f)
					{
						SyncSoundTimer = 0.28f;
						PlayAudioSfx(EKomorebiSfx::SyncTick, 0.4f + TermTarget->SyncProgress * 1.2f);
					}
					if (TermTarget->SyncProgress >= 1.0f)
					{
						TermTarget->bDone = true;
						TermTarget->Visual.SetVisible(false);
						TermTarget->Visual.DisableCollider();
						CheckSectorOpCompletion(CurrentSeg);
					}
				}
				else
				{
					TermTarget->SyncProgress = FMath::Max(0.0f, TermTarget->SyncProgress - Dt * 0.4f);
				}
			}
		}
		else if (ActiveOp->Type == TEXT("rings"))
		{
			FMissionTargetRuntime* NextRing = ActiveOp->Targets.FindByPredicate([](const FMissionTargetRuntime& T) { return !T.bDone; });
			for (FMissionTargetRuntime& Target : ActiveOp->Targets)
			{
				const bool bIsNext = (&Target == NextRing);
				Target.Visual.SetVisible(bIsNext);
				if (bIsNext)
				{
					if (Target.Visual.ModelComp.IsValid())
					{
						Target.Visual.ModelComp->AddLocalRotation(FRotator(0.0f, Dt * 80.0f, 0.0f));
					}
					if (std::hypot(Px - Target.ThreeX, Pz - Target.ThreeZ) < Target.Radius)
					{
						Target.bDone = true;
						Target.Visual.SetVisible(false);
						OnFlashText.Broadcast(FString::Printf(TEXT("Ring %d/%d"), Target.Index + 1, ActiveOp->Targets.Num()));
						CheckSectorOpCompletion(CurrentSeg);
					}
				}
			}
		}

		// Hit flash bounce and ring pulse on active sector targets.
		for (FMissionTargetRuntime& Target : ActiveOp->Targets)
		{
			if (Target.bDone)
			{
				continue;
			}
			Target.RamCooldown = FMath::Max(0.0f, Target.RamCooldown - Dt);
			if (Target.HitFlashTimer > 0.0f)
			{
				Target.HitFlashTimer = FMath::Max(0.0f, Target.HitFlashTimer - Dt);
				if (Target.Visual.ModelComp.IsValid())
				{
					const float S = 1.0f + Target.HitFlashTimer * 0.5f;
					Target.Visual.ModelComp->SetRelativeScale3D(FVector(S, S, FMath::Max(0.4f, 1.0f - Target.HitFlashTimer * 0.4f) * 1.7f));
				}
			}
		}
	}

	// 3. Repeatable Street Contract Kiosks (8 across the 720m city grid).
	for (int32 ki = 0; ki < Kiosks.Num(); ++ki)
	{
		FKioskRuntime& Kiosk = Kiosks[ki];
		Kiosk.Cooldown = FMath::Max(0.0f, Kiosk.Cooldown - Dt);
		const double Dist = std::hypot(Px - Kiosk.ThreeX, Pz - Kiosk.ThreeZ);
		const bool bVisible = Dist < 110.0;
		Kiosk.Visual.SetVisible(bVisible);
		if (bVisible && Kiosk.Visual.BeamComp.IsValid())
		{
			Kiosk.Visual.BeamComp->SetVisibility(Kiosk.Cooldown <= 0.0f && !bHasActiveContract);
		}
		if (!bHasActiveContract && Kiosk.Cooldown <= 0.0f && Dist < 3.6)
		{
			StartContract(ki);
		}
	}

	// 4. Active Timed Street Contract.
	if (bHasActiveContract)
	{
		ActiveContract.TimeLeft -= Dt;
		if (ActiveContract.TimeLeft <= 0.0f)
		{
			FinishContract(false);
		}
		else if (ActiveContract.Kind == TEXT("rings"))
		{
			FMissionTargetRuntime* NextRing = ActiveContract.Targets.FindByPredicate([](const FMissionTargetRuntime& T) { return !T.bDone; });
			for (FMissionTargetRuntime& Target : ActiveContract.Targets)
			{
				const bool bIsNext = (&Target == NextRing);
				Target.Visual.SetVisible(bIsNext);
				if (bIsNext)
				{
					if (Target.Visual.ModelComp.IsValid())
					{
						Target.Visual.ModelComp->AddLocalRotation(FRotator(0.0f, Dt * 92.0f, 0.0f));
					}
					if (std::hypot(Px - Target.ThreeX, Pz - Target.ThreeZ) < Target.Radius)
					{
						Target.bDone = true;
						Target.Visual.SetVisible(false);
						const bool bAllDone = !ActiveContract.Targets.ContainsByPredicate([](const FMissionTargetRuntime& T) { return !T.bDone; });
						if (bAllDone)
						{
							FinishContract(true);
							break;
						}
						PlayAudioSfx(EKomorebiSfx::MissionStep);
					}
				}
			}
		}
		else if (ActiveContract.Kind == TEXT("uplink"))
		{
			for (FMissionTargetRuntime& Target : ActiveContract.Targets)
			{
				if (Target.bDone)
				{
					continue;
				}
				if (std::hypot(Px - Target.ThreeX, Pz - Target.ThreeZ) < Target.Radius)
				{
					Target.SyncProgress += Dt / 1.8f;
					SyncSoundTimer -= Dt;
					if (SyncSoundTimer <= 0.0f)
					{
						SyncSoundTimer = 0.26f;
						PlayAudioSfx(EKomorebiSfx::SyncTick, 0.4f + Target.SyncProgress * 1.2f);
					}
					if (Target.SyncProgress >= 1.0f)
					{
						Target.bDone = true;
						Target.Visual.SetVisible(false);
						Target.Visual.DisableCollider();
						const bool bAllDone = !ActiveContract.Targets.ContainsByPredicate([](const FMissionTargetRuntime& T) { return !T.bDone; });
						if (bAllDone)
						{
							FinishContract(true);
							break;
						}
						PlayAudioSfx(EKomorebiSfx::MissionStep);
					}
				}
			}
		}
		else
		{
			for (FMissionTargetRuntime& Target : ActiveContract.Targets)
			{
				if (Target.bDone)
				{
					continue;
				}
				Target.RamCooldown = FMath::Max(0.0f, Target.RamCooldown - Dt);
				Target.HitFlashTimer = FMath::Max(0.0f, Target.HitFlashTimer - Dt);
			}
		}
	}

	// 5. Sector auto-progression if combat context reports sector crew & rival dead.
	if (!bRoam && !bWon && WorldContext.bValid && CurrentSeg < WorldContext.Sectors.Num())
	{
		const FKomorebiSectorCombatSnapshot& Snap = WorldContext.Sectors[CurrentSeg];
		const bool bCrewDead = (Snap.TotalCrewCount > 0) && (Snap.AliveCrewCount <= 0);
		const bool bRivalDead = !Snap.bRivalAlive;
		TryAdvanceSectorClear(CurrentSeg, bCrewDead, bRivalDead);
	}

	// 6. Relays, Gates, and Amber Sanctuary Terminal.
	UpdateRelaysAndGates(Dt, Px, Pz, PlayerHp);

	// 7. Refresh HUD snapshot.
	RebuildCachedHUDData(Px, Pz);
}

// --- Relays, Barrier Gates & Amber Sanctuary ---------------------------------------------------

void UMissionSubsystem::UpdateRelaysAndGates(float DeltaTime, double PlayerThreeX, double PlayerThreeZ, float PlayerHp)
{
	const bool bRoam = (CurrentPlayMode != EKomorebiPlayMode::StorySectors);
	const int32 TotalSectors = static_cast<int32>(KomorebiCore::GetLevels().size());

	// Animate opening gates lowering smoothly into the roadbed.
	for (FBarrierGateRuntime& Gate : Gates)
	{
		if (Gate.bOpen && Gate.OpenAnim < 1.0f)
		{
			Gate.OpenAnim = FMath::Min(1.0f, Gate.OpenAnim + DeltaTime * 1.1f);
			if (AActor* GateActor = Gate.Visual.Actor.Get())
			{
				const double DropMeters = static_cast<double>(Gate.OpenAnim) * 5.8;
				GateActor->SetActorLocation(ThreeToUE(Gate.ThreeX, Gate.ThreeY - DropMeters, Gate.ThreeZ));
			}
			if (Gate.OpenAnim >= 1.0f)
			{
				Gate.Visual.SetVisible(false);
			}
		}
		else if (!Gate.bOpen)
		{
			Gate.Visual.SetVisible(std::abs(PlayerThreeZ - Gate.ThreeZ) < 120.0);
		}
	}

	if (PlayerHp <= 0.0f)
	{
		return;
	}

	for (FRelayRuntime& Relay : Relays)
	{
		Relay.Cooldown = FMath::Max(0.0f, Relay.Cooldown - DeltaTime);
		const double Dist = std::hypot(PlayerThreeX - Relay.ThreeX, PlayerThreeZ - Relay.ThreeZ);
		if (Relay.Visual.ModelComp.IsValid())
		{
			Relay.Visual.ModelComp->SetVisibility(Dist < 75.0);
		}

		if (Relay.bUsed || Dist > 7.0)
		{
			continue;
		}

		if (!bRoam)
		{
			if (Relay.bIsGoal)
			{
				if (ClearedSectors >= TotalSectors)
				{
					Relay.bUsed = true;
					bWon = true;
					ActiveRespawnLocation = ThreeToUE(Relay.ThreeX, Relay.ThreeY, Relay.ThreeZ);
					OnCheckpointActivated.Broadcast(Relay.SectorIndex, ActiveRespawnLocation);
					OnHealPlayer.Broadcast(100.0f);
					PersistCampaignProgress();
					PlayAudioSfx(EKomorebiSfx::MissionDone);
					SpeakComms(TEXT("win"));
					OnToast.Broadcast(TEXT("Amber Sanctuary stabilized — you did it"));
					OnCampaignWon.Broadcast();
				}
				else if (GateToastCooldown <= 0.0f)
				{
					GateToastCooldown = 4.0f;
					OnToast.Broadcast(TEXT("The sanctuary is not safe yet — clear all 6 sectors first"));
				}
			}
			else if (Relay.SectorIndex <= ClearedSectors)
			{
				Relay.bUsed = true;
				ActiveRespawnLocation = ThreeToUE(Relay.ThreeX, Relay.ThreeY, Relay.ThreeZ);
				OnCheckpointActivated.Broadcast(Relay.SectorIndex, ActiveRespawnLocation);
				OnHealPlayer.Broadcast(100.0f);
				OnReduceHeat.Broadcast(35.0f);
				PersistCampaignProgress();
				PlayAudioSfx(EKomorebiSfx::MissionStep);
				OnToast.Broadcast(FString::Printf(TEXT("Mission Relay · %s"), *Relay.Name));
			}
		}
		else
		{
			// Free Roam / Online: relays heal & clear heat repeatedly on a 10s cooldown.
			if (Relay.Cooldown > 0.0f)
			{
				continue;
			}
			Relay.Cooldown = 10.0f;
			ActiveRespawnLocation = ThreeToUE(Relay.ThreeX, Relay.ThreeY, Relay.ThreeZ);
			OnCheckpointActivated.Broadcast(Relay.SectorIndex, ActiveRespawnLocation);
			OnHealPlayer.Broadcast(100.0f);
			OnReduceHeat.Broadcast(35.0f);
			PlayAudioSfx(EKomorebiSfx::MissionStep);
			OnToast.Broadcast(Relay.bIsGoal ? Relay.Name : FString::Printf(TEXT("Mission Relay · %s"), *Relay.Name));
		}
	}
}

bool UMissionSubsystem::TryAdvanceSectorClear(int32 SectorIndex, bool bCrewCleared, bool bRivalDefeated)
{
	if (CurrentPlayMode != EKomorebiPlayMode::StorySectors || bWon)
	{
		return false;
	}
	if (SectorIndex != ClearedSectors)
	{
		return false;
	}
	if (!IsSectorFieldOpDone(SectorIndex) || !bCrewCleared || !bRivalDefeated)
	{
		return false;
	}

	const std::vector<KomorebiCore::LevelSpec>& Levels = KomorebiCore::GetLevels();
	ClearedSectors = FMath::Min(static_cast<int32>(Levels.size()), ClearedSectors + 1);

	// Open the barrier gate for this cleared sector.
	for (FBarrierGateRuntime& Gate : Gates)
	{
		if (Gate.SectorIndex == SectorIndex && !Gate.bOpen)
		{
			Gate.bOpen = true;
			Gate.Visual.DisableCollider();
			PlayAudioSfx(EKomorebiSfx::GateOpen);
		}
	}

	FString PowerName;
	if (SectorIndex < static_cast<int32>(Levels.size()) && Levels[SectorIndex].Grant.has_value())
	{
		const KomorebiCore::PowerSpec& PSpec = KomorebiCore::GetPowerSpec(Levels[SectorIndex].Grant.value());
		PowerName = KomorebiBridge::ToFString(PSpec.Name);
		const FString KeyName = KomorebiBridge::ToFString(PSpec.Key);
		OnToast.Broadcast(FString::Printf(TEXT("Sector %d clear — %s earned (%s)"), SectorIndex + 1, *PowerName, *KeyName));
	}

	SpeakComms(FString::Printf(TEXT("clear%d"), SectorIndex));
	OnSectorGateOpened.Broadcast(SectorIndex, PowerName);

	if (ClearedSectors >= static_cast<int32>(Levels.size()) && !bRunTold)
	{
		bRunTold = true;
		OnToast.Broadcast(TEXT("Amber Sanctuary lies ahead — enter the golden terminal"));
	}

	PersistCampaignProgress();
	return true;
}

bool UMissionSubsystem::IsLocationPastClosedGate(FVector WorldLocation) const
{
	if (CurrentPlayMode != EKomorebiPlayMode::StorySectors || bWon)
	{
		return false;
	}
	const KomorebiCore::Vec3 Three = KomorebiBridge::FVectorToThree(WorldLocation);
	for (const FBarrierGateRuntime& Gate : Gates)
	{
		if (!Gate.bOpen && Three.Z < Gate.ThreeZ + 0.45)
		{
			return true;
		}
	}
	return false;
}

bool UMissionSubsystem::EnforceClosedGateBoundary(FVector& InOutWorldLocation)
{
	if (CurrentPlayMode != EKomorebiPlayMode::StorySectors || bWon)
	{
		return false;
	}
	const KomorebiCore::Vec3 Three = KomorebiBridge::FVectorToThree(InOutWorldLocation);
	for (const FBarrierGateRuntime& Gate : Gates)
	{
		if (!Gate.bOpen && Three.Z < Gate.ThreeZ + 0.45)
		{
			const double SafeZ = Gate.ThreeZ + 1.6;
			InOutWorldLocation = ThreeToUE(Three.X, SampleTerrainHeight(Three.X, SafeZ), SafeZ);
			if (GateToastCooldown <= 0.0f)
			{
				GateToastCooldown = 4.0f;
				const std::vector<KomorebiCore::LevelSpec>& Levels = KomorebiCore::GetLevels();
				const int32 Idx = FMath::Clamp(ClearedSectors, 0, static_cast<int32>(Levels.size()) - 1);
				const FString LevelName = KomorebiBridge::ToFString(Levels[Idx].Name);
				OnToast.Broadcast(!IsSectorFieldOpDone(ClearedSectors)
					? FString::Printf(TEXT("Complete %s mission first"), *LevelName)
					: FString::Printf(TEXT("Clear %s crew and Ren's Specter first"), *LevelName));
			}
			return true;
		}
	}
	return false;
}

// --- Combat & Vehicle Damage Against Mission Targets -------------------------------------------

bool UMissionSubsystem::HitAt(FVector WorldLocation, float RadiusMeters, float Damage, FName Source)
{
	const KomorebiCore::Vec3 Three = KomorebiBridge::FVectorToThree(WorldLocation);
	const bool bIsRam = (Source == TEXT("ram"));
	bool bHitAny = false;

	const bool bRoam = (CurrentPlayMode != EKomorebiPlayMode::StorySectors);
	if (!bRoam && ClearedSectors < SectorOps.Num() && !SectorOps[ClearedSectors].bDone)
	{
		for (FMissionTargetRuntime& Target : SectorOps[ClearedSectors].Targets)
		{
			if (Target.bDone || Target.Kind != ETargetKind::Destructible)
			{
				continue;
			}
			if (bIsRam && Target.RamCooldown > 0.0f)
			{
				continue;
			}
			if (std::hypot(Target.ThreeX - Three.X, Target.ThreeZ - Three.Z) > static_cast<double>(RadiusMeters + Target.Radius))
			{
				continue;
			}
			if (bIsRam)
			{
				Target.RamCooldown = 0.4f;
			}
			if (DamageTarget(Target, Damage, Source, ClearedSectors))
			{
				bHitAny = true;
			}
		}
	}

	if (bHasActiveContract)
	{
		for (FMissionTargetRuntime& Target : ActiveContract.Targets)
		{
			if (Target.bDone || Target.Kind != ETargetKind::Destructible)
			{
				continue;
			}
			if (bIsRam && Target.RamCooldown > 0.0f)
			{
				continue;
			}
			if (std::hypot(Target.ThreeX - Three.X, Target.ThreeZ - Three.Z) > static_cast<double>(RadiusMeters + Target.Radius))
			{
				continue;
			}
			if (bIsRam)
			{
				Target.RamCooldown = 0.4f;
			}
			if (DamageTarget(Target, Damage, Source, INDEX_NONE))
			{
				bHitAny = true;
				if (!bHasActiveContract)
				{
					break;
				}
			}
		}
	}

	return bHitAny;
}

bool UMissionSubsystem::HitBolt(FVector WorldLocation, float RadiusMeters, float Damage)
{
	const KomorebiCore::Vec3 Three = KomorebiBridge::FVectorToThree(WorldLocation);
	const bool bRoam = (CurrentPlayMode != EKomorebiPlayMode::StorySectors);

	if (!bRoam && ClearedSectors < SectorOps.Num() && !SectorOps[ClearedSectors].bDone)
	{
		for (FMissionTargetRuntime& Target : SectorOps[ClearedSectors].Targets)
		{
			if (Target.bDone || Target.Kind != ETargetKind::Destructible)
			{
				continue;
			}
			if (std::hypot(Target.ThreeX - Three.X, Target.ThreeZ - Three.Z) < static_cast<double>(RadiusMeters + Target.Radius)
				&& std::abs(Three.Y - (Target.ThreeY + 1.0)) < 2.2)
			{
				return DamageTarget(Target, Damage, TEXT("spark"), ClearedSectors);
			}
		}
	}

	if (bHasActiveContract)
	{
		for (FMissionTargetRuntime& Target : ActiveContract.Targets)
		{
			if (Target.bDone || Target.Kind != ETargetKind::Destructible)
			{
				continue;
			}
			if (std::hypot(Target.ThreeX - Three.X, Target.ThreeZ - Three.Z) < static_cast<double>(RadiusMeters + Target.Radius)
				&& std::abs(Three.Y - (Target.ThreeY + 1.0)) < 2.2)
			{
				return DamageTarget(Target, Damage, TEXT("spark"), INDEX_NONE);
			}
		}
	}

	return false;
}

bool UMissionSubsystem::DamageTarget(FMissionTargetRuntime& Target, float Damage, FName /*Source*/, int32 SectorIndexOrNegOne)
{
	if (Target.bDone)
	{
		return false;
	}

	Target.Hp -= Damage;
	Target.HitFlashTimer = 0.22f;

	if (Target.Hp <= 0.0f)
	{
		Target.bDone = true;
		Target.Hp = 0.0f;
		Target.Visual.SetVisible(false);
		Target.Visual.DisableCollider();
		PlayAudioSfx(EKomorebiSfx::ObjectBreak);
		OnFlashText.Broadcast(TEXT("Target Down"));

		if (SectorIndexOrNegOne != INDEX_NONE)
		{
			CheckSectorOpCompletion(SectorIndexOrNegOne);
		}
		else if (bHasActiveContract)
		{
			const bool bAllDone = !ActiveContract.Targets.ContainsByPredicate([](const FMissionTargetRuntime& T) { return !T.bDone; });
			if (bAllDone)
			{
				FinishContract(true);
			}
			else
			{
				PlayAudioSfx(EKomorebiSfx::MissionStep);
			}
		}
	}
	else
	{
		PlayAudioSfx(EKomorebiSfx::ObjectHit);
	}
	return true;
}

void UMissionSubsystem::CheckSectorOpCompletion(int32 Seg)
{
	if (!SectorOps.IsValidIndex(Seg) || SectorOps[Seg].bDone)
	{
		return;
	}

	FSectorOpRuntime& Op = SectorOps[Seg];
	const bool bAllDone = !Op.Targets.ContainsByPredicate([](const FMissionTargetRuntime& T) { return !T.bDone; });
	if (bAllDone)
	{
		Op.bDone = true;
		DoneSectorOps.Add(Seg);
		PersistCampaignProgress();
		PlayAudioSfx(EKomorebiSfx::MissionDone);

		if (!AnnouncedFieldClearSectors.Contains(Seg))
		{
			AnnouncedFieldClearSectors.Add(Seg);
			SpeakComms(FString::Printf(TEXT("field%d"), Seg));
		}
		OnToast.Broadcast(FString::Printf(TEXT("%s complete — clear crew & defeat Ren"), *Op.OpTitle));
	}
	else
	{
		PlayAudioSfx(EKomorebiSfx::MissionStep);
	}
}

void UMissionSubsystem::CompleteSectorFieldOp(int32 SectorIndex)
{
	if (!SectorOps.IsValidIndex(SectorIndex))
	{
		return;
	}
	DoneSectorOps.Add(SectorIndex);
	FSectorOpRuntime& Op = SectorOps[SectorIndex];
	Op.bDone = true;
	for (FMissionTargetRuntime& Target : Op.Targets)
	{
		Target.bDone = true;
		Target.Hp = 0.0f;
		Target.Visual.SetVisible(false);
		Target.Visual.DisableCollider();
	}
	if (SectorIndex == 1)
	{
		bCourierDefeated = true;
	}
	PersistCampaignProgress();
}

void UMissionSubsystem::UpdateWorldContext(const FKomorebiMissionWorldContext& Context)
{
	WorldContext = Context;
	WorldContext.bValid = true;
}

void UMissionSubsystem::NotifyCourierDefeated()
{
	bCourierDefeated = true;
}

void UMissionSubsystem::UpdateCourierLocation(FVector WorldLocation, bool bAlive)
{
	WorldContext.CourierLocation = WorldLocation;
	WorldContext.bCourierAlive = bAlive;
	if (!bAlive)
	{
		bCourierDefeated = true;
	}
}

// --- Repeatable Street Contracts ---------------------------------------------------------------

void UMissionSubsystem::StartContract(int32 KioskIndex)
{
	if (bHasActiveContract || !Kiosks.IsValidIndex(KioskIndex) || Kiosks[KioskIndex].Cooldown > 0.0f)
	{
		return;
	}

	FKioskRuntime& Kiosk = Kiosks[KioskIndex];
	const double Ax = SnapToRoadLine(Kiosk.ThreeX, KomorebiCore::Avenues());
	const double Sz = SnapToRoadLine(Kiosk.ThreeZ, KomorebiCore::Streets());
	const double Dx = static_cast<double>(Kiosk.DirX);
	const double Dz = static_cast<double>(Kiosk.DirZ);

	auto ClampGrid = [](double V) {
		return KomorebiCore::Clamp(V, KomorebiCore::kGridMin + 14.0, KomorebiCore::kGridMax - 14.0);
	};

	ActiveContract = FActiveContractRuntime{};
	ActiveContract.KioskIndex = KioskIndex;
	ActiveContract.Kind = Kiosk.Kind;
	ActiveContract.Name = Kiosk.Name;
	ActiveContract.TimeLeft = 75.0f;

	if (Kiosk.Kind == TEXT("rings"))
	{
		for (int32 i = 1; i <= 4; ++i)
		{
			const double Rx = ClampGrid(Ax + Dx * i * 34.0);
			const double Rz = ClampGrid(Sz + Dz * i * 34.0);
			FMissionTargetRuntime T;
			T.Kind = ETargetKind::Ring;
			T.Index = i - 1;
			T.Label = FString::Printf(TEXT("Sprint Ring %d/4"), i);
			T.ThreeX = Rx;
			T.ThreeZ = Rz;
			T.ThreeY = SampleTerrainHeight(Rx, Rz);
			T.Radius = 4.8f;
			T.Visual = SpawnRingGateVisual(Rx, Rz, (Dx != 0.0) ? (KomorebiCore::kPi * 0.5) : 0.0, kColorKiosk);
			T.Visual.SetVisible(i == 1);
			ActiveContract.Targets.Add(MoveTemp(T));
		}
	}
	else if (Kiosk.Kind == TEXT("jammers"))
	{
		for (int32 i = 1; i <= 3; ++i)
		{
			const double Offset = (i % 2 != 0) ? 5.8 : -5.8;
			const double Jx = ClampGrid(Ax + Dx * i * 32.0 + (Dz != 0.0 ? Offset : 0.0));
			const double Jz = ClampGrid(Sz + Dz * i * 32.0 + (Dx != 0.0 ? Offset : 0.0));
			FMissionTargetRuntime T;
			T.Kind = ETargetKind::Destructible;
			T.Index = i - 1;
			T.Label = FString::Printf(TEXT("Rogue Node %d/3"), i);
			T.ThreeX = Jx;
			T.ThreeZ = Jz;
			T.ThreeY = SampleTerrainHeight(Jx, Jz);
			T.Hp = 50.0f;
			T.MaxHp = 50.0f;
			T.Radius = 1.5f;
			T.Visual = SpawnPropVisual(TEXT("power-core"), Jx, Jz, kColorKiosk, 2.2f, 14.0f, true, 1.6f);
			ActiveContract.Targets.Add(MoveTemp(T));
		}
	}
	else
	{
		for (int32 i = 1; i <= 2; ++i)
		{
			const double Offset = (i % 2 != 0) ? 6.4 : -6.4;
			const double Ux = ClampGrid(Ax + Dx * i * 38.0 + (Dz != 0.0 ? 6.4 : 0.0));
			const double Uz = ClampGrid(Sz + Dz * i * 38.0 + (Dx != 0.0 ? Offset : 0.0));
			FMissionTargetRuntime T;
			T.Kind = ETargetKind::Terminal;
			T.Index = i - 1;
			T.Label = FString::Printf(TEXT("Relay Sync %d/2"), i);
			T.ThreeX = Ux;
			T.ThreeZ = Uz;
			T.ThreeY = SampleTerrainHeight(Ux, Uz);
			T.Radius = 4.2f;
			T.Visual = SpawnPropVisual(TEXT("terminal"), Ux, Uz, kColorKiosk, 3.6f, 14.0f, true, 1.6f);
			ActiveContract.Targets.Add(MoveTemp(T));
		}
	}

	bHasActiveContract = true;
	Kiosk.Cooldown = 25.0f;

	PlayAudioSfx(EKomorebiSfx::MissionStep);
	SpeakComms(TEXT("contractStart"));
	OnToast.Broadcast(FString::Printf(TEXT("Contract started · %s"), *Kiosk.Name));
}

void UMissionSubsystem::FinishContract(bool bSuccess)
{
	if (!bHasActiveContract)
	{
		return;
	}

	const FString ContractName = ActiveContract.Name;
	for (FMissionTargetRuntime& Target : ActiveContract.Targets)
	{
		Target.Visual.DestroyActor();
	}
	ActiveContract.Targets.Empty();
	bHasActiveContract = false;

	if (bSuccess)
	{
		PlayAudioSfx(EKomorebiSfx::MissionDone);
		SpeakComms(TEXT("contractDone"));
		OnRewardCash.Broadcast(150, FString::Printf(TEXT("Contract complete · %s (+$150)"), *ContractName));
		OnHealPlayer.Broadcast(35.0f);
		if (ActiveSaveGame)
		{
			ActiveSaveGame->Cash += 150;
			ActiveSaveGame->Contracts += 1;
			PersistCampaignProgress();
		}
	}
	else
	{
		OnToast.Broadcast(FString::Printf(TEXT("Contract expired · %s"), *ContractName));
	}
}

// --- Sequenced Multi-Speaker Radio Comms Queue -------------------------------------------------

void UMissionSubsystem::SpeakComms(const FString& CommKey)
{
	const std::string KeyUtf8(TCHAR_TO_UTF8(*CommKey));
	const KomorebiCore::CommsEntry* Primary = KomorebiCore::FindComms(KeyUtf8);
	if (!Primary)
	{
		return;
	}

	CommQueue.Add(FQueuedCommItem{
		KomorebiBridge::ToFString(Primary->Speaker),
		KomorebiBridge::ToFString(Primary->Line),
		4.8f,
	});

	if (const std::vector<KomorebiCore::CommsChainStep>* Chain = KomorebiCore::FindCommsChain(KeyUtf8))
	{
		for (const KomorebiCore::CommsChainStep& Step : *Chain)
		{
			CommQueue.Add(FQueuedCommItem{
				KomorebiBridge::ToFString(Step.Speaker),
				KomorebiBridge::ToFString(Step.Line),
				static_cast<float>(Step.Delay),
			});
		}
	}

	AdvanceCommsQueue(0.0f);
}

void UMissionSubsystem::QueueDirectComms(const FString& SpeakerId, const FString& Line, float MinDelaySeconds)
{
	if (Line.IsEmpty())
	{
		return;
	}
	CommQueue.Add(FQueuedCommItem{ SpeakerId, Line, MinDelaySeconds });
	AdvanceCommsQueue(0.0f);
}

void UMissionSubsystem::AdvanceCommsQueue(float DeltaTime)
{
	CommHoldTimer = FMath::Max(0.0f, CommHoldTimer - DeltaTime);
	if (CommHoldTimer <= 0.0f && CommQueue.IsEmpty())
	{
		bHasActiveComms = false;
		return;
	}
	if (CommHoldTimer > 0.0f || CommQueue.IsEmpty())
	{
		return;
	}

	const FQueuedCommItem Next = CommQueue[0];
	CommQueue.RemoveAt(0);
	DispatchCommsMessage(Next);
}

void UMissionSubsystem::DispatchCommsMessage(const FQueuedCommItem& Item)
{
	CurrentComms = BuildCommsMessage(Item.SpeakerId, Item.Line);
	bHasActiveComms = true;
	// Match missions.js line 312 pacing: hold at least delay, 6.2s, or reading length * 0.068s.
	CommHoldTimer = FMath::Max3(Item.Delay, 6.2f, static_cast<float>(Item.Line.Len()) * 0.068f);

	SpeakAudioLine(Item.Line, Item.SpeakerId);
	OnCommsMessageStarted.Broadcast(CurrentComms);
}

FKomorebiCommsMessage UMissionSubsystem::BuildCommsMessage(const FString& SpeakerId, const FString& Line) const
{
	FKomorebiCommsMessage Msg;
	Msg.SpeakerId = SpeakerId.ToLower();
	Msg.Line = Line;

	const std::string Utf8Id(TCHAR_TO_UTF8(*Msg.SpeakerId));
	const KomorebiCore::SpeakerProfile& Profile = KomorebiCore::FindSpeaker(Utf8Id);
	Msg.SpeakerName = KomorebiBridge::ToFString(Profile.Name);
	Msg.AccentColor = HexToLinear(Profile.ColorHex, kColorRing);

	if (const KomorebiCore::CharacterDossier* Dossier = KomorebiCore::FindCharacterDossier(Utf8Id))
	{
		Msg.SpeakerName = KomorebiBridge::ToFString(Dossier->Name);
		Msg.SpeakerRole = KomorebiBridge::ToFString(Dossier->Role);
		Msg.AccentColor = HexToLinear(Dossier->ColorHex, Msg.AccentColor);
	}
	else if (Msg.SpeakerId == TEXT("dispatch"))
	{
		Msg.SpeakerRole = TEXT("Aethelgard Broadcast");
	}

	if (Msg.SpeakerId == TEXT("sora"))         Msg.Frequency = TEXT("CH-07 // GHOST-LINK");
	else if (Msg.SpeakerId == TEXT("mira"))    Msg.Frequency = TEXT("CH-01 // ARCHIVE IMPRINT");
	else if (Msg.SpeakerId == TEXT("ren"))     Msg.Frequency = TEXT("CH-99 // MIRROR INTRUSION");
	else if (Msg.SpeakerId == TEXT("kael"))    Msg.Frequency = TEXT("CH-07 // COURIER-07");
	else                                       Msg.Frequency = TEXT("CH-00 // GRID DISPATCH");

	return Msg;
}

// --- HUD State Builder -------------------------------------------------------------------------

void UMissionSubsystem::RebuildCachedHUDData(double Px, double Pz)
{
	FKomorebiMissionHUDData Out;
	Out.EchoesCollected = CollectedEchoIds.Num();
	Out.EchoesTotal = static_cast<int32>(KomorebiCore::GetEchoShards().size());
	Out.bHasActiveComms = bHasActiveComms;
	Out.ActiveComms = CurrentComms;
	Out.bContractActive = bHasActiveContract;
	Out.ContractTimeRemaining = bHasActiveContract ? FMath::Max(0.0f, ActiveContract.TimeLeft) : 0.0f;

	for (const FKioskRuntime& K : Kiosks)
	{
		if (K.Cooldown <= 0.0f && !bHasActiveContract)
		{
			Out.MinimapMarkers.Add(FKomorebiMinimapMarker{ ThreeToUE(K.ThreeX, K.ThreeY, K.ThreeZ), TEXT("kiosk"), kColorKiosk });
		}
	}
	for (const FEchoShrineRuntime& S : EchoShrines)
	{
		if (!S.bCollected && std::hypot(S.ThreeX - Px, S.ThreeZ - Pz) < 70.0)
		{
			Out.MinimapMarkers.Add(FKomorebiMinimapMarker{ ThreeToUE(S.ThreeX, S.ThreeY, S.ThreeZ), TEXT("echo"), kColorEchoCyan });
		}
	}

	// Case A: Active timed street contract.
	if (bHasActiveContract)
	{
		int32 DoneCount = 0;
		const FMissionTargetRuntime* Next = nullptr;
		for (const FMissionTargetRuntime& T : ActiveContract.Targets)
		{
			if (T.bDone)
			{
				++DoneCount;
			}
			else
			{
				if (!Next) Next = &T;
				Out.MinimapMarkers.Add(FKomorebiMinimapMarker{ ThreeToUE(T.ThreeX, T.ThreeY, T.ThreeZ), TEXT("contract"), kColorKiosk });
			}
		}

		const FString SyncPct = (Next && Next->Kind == ETargetKind::Terminal && Next->SyncProgress > 0.0f)
			? FString::Printf(TEXT(" (%d%%)"), FMath::RoundToInt(Next->SyncProgress * 100.0f))
			: FString();

		Out.OpTitle = FString::Printf(TEXT("Contract // %s"), *ActiveContract.Name);
		Out.Steps.Add(FKomorebiMissionStepView{
			FString::Printf(TEXT("%s (%d/%d)%s"), Next ? *Next->Label : TEXT("Complete targets"), DoneCount, ActiveContract.Targets.Num(), *SyncPct),
			false,
		});
		Out.Steps.Add(FKomorebiMissionStepView{
			FString::Printf(TEXT("Timer: %ds"), FMath::Max(0, FMath::CeilToInt(ActiveContract.TimeLeft))),
			false,
		});
		Out.Tip = TEXT("Follow the green diamond · Complete before timer expires for +$150");
		if (Next)
		{
			Out.bHasWaypoint = true;
			Out.WaypointWorldLocation = ThreeToUE(Next->ThreeX, Next->ThreeY, Next->ThreeZ);
			Out.WaypointLabel = Next->Label + SyncPct;
			Out.WaypointColor = kColorKiosk;
		}
		CachedHUDData = MoveTemp(Out);
		return;
	}

	const bool bRoam = (CurrentPlayMode != EKomorebiPlayMode::StorySectors);

	// Case B: Free Roam, Online City, or Post-Victory.
	if (bRoam || bWon)
	{
		const FKioskRuntime* NearKiosk = nullptr;
		double BestDist = TNumericLimits<double>::Max();
		for (const FKioskRuntime& K : Kiosks)
		{
			if (K.Cooldown > 0.0f) continue;
			const double D = std::hypot(K.ThreeX - Px, K.ThreeZ - Pz);
			if (D < BestDist)
			{
				BestDist = D;
				NearKiosk = &K;
			}
		}

		Out.OpTitle = bWon ? TEXT("Protocol Zero-Seven // Complete") : TEXT("Free Roam // All Systems");
		Out.Steps.Add(FKomorebiMissionStepView{
			FString::Printf(TEXT("Memory Echoes (%d/%d) · Explore 196 blocks"), Out.EchoesCollected, Out.EchoesTotal),
			Out.EchoesCollected >= Out.EchoesTotal,
		});
		Out.Steps.Add(FKomorebiMissionStepView{
			NearKiosk ? FString::Printf(TEXT("Visit %s for +$150"), *NearKiosk->Name) : TEXT("Kiosks recharging..."),
			false,
		});
		Out.Tip = WorldContext.bDriving
			? TEXT("W/S drive · A/D steer · Space drift · G step out")
			: TEXT("Press G near any bike or car · Cyan/gold shrines hold Memory Echoes");
		if (NearKiosk)
		{
			Out.bHasWaypoint = true;
			Out.WaypointWorldLocation = ThreeToUE(NearKiosk->ThreeX, NearKiosk->ThreeY, NearKiosk->ThreeZ);
			Out.WaypointLabel = TEXT("Contract Kiosk");
			Out.WaypointColor = kColorKiosk;
		}
		CachedHUDData = MoveTemp(Out);
		return;
	}

	// Case C: All 6 sectors cleared -> Amber Sanctuary Finale.
	if (ClearedSectors >= SectorOps.Num())
	{
		const KomorebiCore::NamedPoint& Goal = KomorebiCore::GetGoal();
		const FVector GoalLoc = ThreeToUE(Goal.X, SampleTerrainHeight(Goal.X, Goal.Z), Goal.Z);
		Out.OpTitle = TEXT("Final Protocol // Amber Sanctuary");
		Out.Steps.Add(FKomorebiMissionStepView{ TEXT("All 6 sectors liberated"), true });
		Out.Steps.Add(FKomorebiMissionStepView{ TEXT("Deploy Seed at Amber Sanctuary Terminal"), false });
		Out.Tip = TEXT("Step into the golden Amber Sanctuary ring ahead to bloom the Komorebi Seed");
		Out.bHasWaypoint = true;
		Out.WaypointWorldLocation = GoalLoc;
		Out.WaypointLabel = TEXT("Amber Sanctuary");
		Out.WaypointColor = kColorGoal;
		Out.MinimapMarkers.Add(FKomorebiMinimapMarker{ GoalLoc, TEXT("goal"), kColorGoal });
		CachedHUDData = MoveTemp(Out);
		return;
	}

	// Case D: Active Story Sector (0..5).
	const int32 Seg = FMath::Clamp(ClearedSectors, 0, SectorOps.Num() - 1);
	const FSectorOpRuntime& Op = SectorOps[Seg];
	const bool bFieldDone = Op.bDone;

	int32 CrewAlive = 0;
	int32 CrewTotal = 0;
	bool bHasNearestCrew = false;
	FVector NearestCrewLoc = FVector::ZeroVector;
	bool bRivalAlive = true;
	FVector RivalLoc = FVector::ZeroVector;
	FString RivalName = TEXT("Ren's Specter");

	if (WorldContext.bValid && WorldContext.Sectors.IsValidIndex(Seg))
	{
		const FKomorebiSectorCombatSnapshot& Snap = WorldContext.Sectors[Seg];
		CrewAlive = Snap.AliveCrewCount;
		CrewTotal = Snap.TotalCrewCount;
		bHasNearestCrew = Snap.bHasNearestCrew;
		NearestCrewLoc = Snap.NearestCrewLocation;
		bRivalAlive = Snap.bRivalAlive;
		RivalLoc = Snap.RivalLocation;
		if (!Snap.RivalName.IsEmpty())
		{
			RivalName = Snap.RivalName;
		}
	}
	else
	{
		const std::vector<KomorebiCore::LevelSpec>& Levels = KomorebiCore::GetLevels();
		if (Seg < static_cast<int32>(Levels.size()))
		{
			CrewTotal = static_cast<int32>(Levels[Seg].Spots.size());
			CrewAlive = CrewTotal;
		}
		const std::vector<KomorebiCore::DuelConfig>& Duels = KomorebiCore::GetDuelConfigs();
		if (Seg < static_cast<int32>(Duels.size()))
		{
			RivalName = KomorebiBridge::ToFString(Duels[Seg].Name);
			RivalLoc = ThreeToUE(Duels[Seg].X, SampleTerrainHeight(Duels[Seg].X, Duels[Seg].Z), Duels[Seg].Z);
		}
	}

	const bool bCrewDone = (CrewTotal > 0 && CrewAlive <= 0);
	int32 FieldDoneCount = 0;
	const FMissionTargetRuntime* NextField = nullptr;
	double BestFieldDist = TNumericLimits<double>::Max();

	for (const FMissionTargetRuntime& T : Op.Targets)
	{
		if (T.bDone)
		{
			++FieldDoneCount;
			continue;
		}
		const FLinearColor Color = (T.Kind == ETargetKind::Ring) ? kColorRing
			: (T.Kind == ETargetKind::Terminal) ? kColorTerminal
			: kColorMission;
		Out.MinimapMarkers.Add(FKomorebiMinimapMarker{ ThreeToUE(T.ThreeX, T.ThreeY, T.ThreeZ), TEXT("mission"), Color });

		if (Op.Type == TEXT("intercept"))
		{
			if (T.Kind == ETargetKind::Courier || !NextField)
			{
				NextField = &T;
			}
		}
		else
		{
			const double D = std::hypot(T.ThreeX - Px, T.ThreeZ - Pz);
			if (D < BestFieldDist)
			{
				BestFieldDist = D;
				NextField = &T;
			}
		}
	}

	if (bFieldDone && bCrewDone && bRivalAlive)
	{
		Out.MinimapMarkers.Add(FKomorebiMinimapMarker{ RivalLoc, TEXT("rival"), kColorRival });
	}

	if (NextField)
	{
		const FString SyncPct = (NextField->Kind == ETargetKind::Terminal && NextField->SyncProgress > 0.0f)
			? FString::Printf(TEXT(" (%d%%)"), FMath::RoundToInt(NextField->SyncProgress * 100.0f))
			: FString();
		Out.bHasWaypoint = true;
		Out.WaypointWorldLocation = ThreeToUE(NextField->ThreeX, NextField->ThreeY, NextField->ThreeZ);
		Out.WaypointLabel = NextField->Label + SyncPct;
		Out.WaypointColor = (NextField->Kind == ETargetKind::Ring) ? kColorRing
			: (NextField->Kind == ETargetKind::Terminal) ? kColorTerminal
			: kColorMission;
	}
	else if (!bCrewDone && bHasNearestCrew)
	{
		Out.bHasWaypoint = true;
		Out.WaypointWorldLocation = NearestCrewLoc;
		Out.WaypointLabel = TEXT("Sector Crew");
		Out.WaypointColor = kColorMission;
	}
	else if (bRivalAlive)
	{
		FString ShortRival = RivalName;
		int32 CommaIdx = INDEX_NONE;
		if (ShortRival.FindChar(TEXT(','), CommaIdx))
		{
			ShortRival.LeftInline(CommaIdx);
		}
		Out.bHasWaypoint = true;
		Out.WaypointWorldLocation = RivalLoc;
		Out.WaypointLabel = ShortRival;
		Out.WaypointColor = kColorTerminal;
	}

	Out.OpTitle = Op.OpTitle;
	if (WorldContext.bBossEngaged)
	{
		Out.Tip = TEXT("Wait for the cyan Recovery glow after Ren swings, then strike!");
	}
	else if (WorldContext.Heat > 12.0f)
	{
		Out.Tip = TEXT("Wanted! Break line of sight or step into a cyan Mission Relay to clear heat");
	}
	else if (WorldContext.bDriving)
	{
		Out.Tip = TEXT("W/S throttle · A/D steer · Space handbrake · G step out");
	}
	else if (bFieldDone && bCrewDone && bRivalAlive)
	{
		Out.Tip = TEXT("Face Ren's Specter at the crimson beacon to open the Sector Gate!");
	}
	else if (bFieldDone && !bCrewDone)
	{
		Out.Tip = TEXT("Click/J Slash · Q Spark · Clear the remaining street crew");
	}
	else
	{
		Out.Tip = Op.Tip;
	}

	FString ShortRivalLabel = RivalName;
	int32 CommaIdx = INDEX_NONE;
	if (ShortRivalLabel.FindChar(TEXT(','), CommaIdx))
	{
		ShortRivalLabel.LeftInline(CommaIdx);
	}

	const FString TermSync = (NextField && NextField->Kind == ETargetKind::Terminal && NextField->SyncProgress > 0.0f)
		? FString::Printf(TEXT(" · %d%%"), FMath::RoundToInt(NextField->SyncProgress * 100.0f))
		: FString();

	Out.Steps.Add(FKomorebiMissionStepView{
		FString::Printf(TEXT("%s (%d/%d)%s"), *Op.StepLabel, FieldDoneCount, Op.Targets.Num(), *TermSync),
		bFieldDone,
	});
	Out.Steps.Add(FKomorebiMissionStepView{
		FString::Printf(TEXT("Defeat Sector Crew (%d/%d)"), FMath::Max(0, CrewTotal - CrewAlive), CrewTotal),
		bCrewDone,
	});
	Out.Steps.Add(FKomorebiMissionStepView{
		bRivalAlive ? FString::Printf(TEXT("Defeat %s"), *ShortRivalLabel) : TEXT("Specter defeated"),
		!bRivalAlive,
	});

	CachedHUDData = MoveTemp(Out);
}

FKomorebiMissionHUDData UMissionSubsystem::GetMissionHUDData() const
{
	return CachedHUDData;
}

void UMissionSubsystem::PopulateHUDState(FKomorebiHUDState& InOutHud) const
{
	InOutHud.ClearedSectors = ClearedSectors;
	InOutHud.bWon = bWon;
	InOutHud.bRoamMode = (CurrentPlayMode == EKomorebiPlayMode::FreeRoam);
	InOutHud.bOnlineMode = (CurrentPlayMode == EKomorebiPlayMode::OnlineCity);
	InOutHud.EchoesCollected = CachedHUDData.EchoesCollected;
	InOutHud.ObjectiveTitle = CachedHUDData.OpTitle;
	InOutHud.ObjectiveTip = CachedHUDData.Tip;
	InOutHud.Steps = CachedHUDData.Steps;
	InOutHud.bHasWaypoint = CachedHUDData.bHasWaypoint;
	InOutHud.WaypointWorldLocation = CachedHUDData.WaypointWorldLocation;
	InOutHud.WaypointLabel = CachedHUDData.WaypointLabel;
}

bool UMissionSubsystem::IsSectorFieldOpDone(int32 SectorIndex) const
{
	if (CurrentPlayMode != EKomorebiPlayMode::StorySectors)
	{
		return true;
	}
	return DoneSectorOps.Contains(SectorIndex) || (SectorOps.IsValidIndex(SectorIndex) && SectorOps[SectorIndex].bDone);
}

void UMissionSubsystem::RevealAroundShowcaseCamera(FVector FocusWorldLocation, FVector CameraWorldLocation)
{
	const KomorebiCore::Vec3 F = KomorebiBridge::FVectorToThree(FocusWorldLocation);
	const KomorebiCore::Vec3 C = KomorebiBridge::FVectorToThree(CameraWorldLocation);

	for (FSectorOpRuntime& Sop : SectorOps)
	{
		for (FMissionTargetRuntime& T : Sop.Targets)
		{
			if (T.bDone || T.Kind == ETargetKind::Courier)
			{
				continue;
			}
			const double D = FMath::Min(std::hypot(T.ThreeX - F.X, T.ThreeZ - F.Z), std::hypot(T.ThreeX - C.X, T.ThreeZ - C.Z));
			if (D < 95.0)
			{
				T.Visual.SetVisible(true);
				if (T.Visual.BeamComp.IsValid())
				{
					T.Visual.BeamComp->SetVisibility(true);
				}
			}
		}
	}

	for (FEchoShrineRuntime& S : EchoShrines)
	{
		if (S.bCollected)
		{
			continue;
		}
		const double D = FMath::Min(std::hypot(S.ThreeX - F.X, S.ThreeZ - F.Z), std::hypot(S.ThreeX - C.X, S.ThreeZ - C.Z));
		if (D < 95.0)
		{
			S.Visual.SetVisible(true);
		}
	}
}

void UMissionSubsystem::PersistCampaignProgress()
{
	// Free Roam and Online City never overwrite the story campaign save.
	if (CurrentPlayMode != EKomorebiPlayMode::StorySectors || !ActiveSaveGame)
	{
		return;
	}

	ActiveSaveGame->ClearedSectors = ClearedSectors;
	ActiveSaveGame->bWon = bWon;
	for (int32 Sec : DoneSectorOps)
	{
		ActiveSaveGame->MarkMissionSectorCompleted(Sec);
	}

	FString WarningToast;
	if (!UKomorebiSaveManager::SaveWithToastCheck(ActiveSaveGame, WarningToast) && !WarningToast.IsEmpty())
	{
		OnToast.Broadcast(WarningToast);
	}
}

void UMissionSubsystem::PlayAudioSfx(EKomorebiSfx Sfx, float Intensity) const
{
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UKomorebiAudioSubsystem* Audio = GI->GetSubsystem<UKomorebiAudioSubsystem>())
			{
				Audio->PlaySfx(Sfx, Intensity);
			}
		}
	}
}

void UMissionSubsystem::SpeakAudioLine(const FString& Line, const FString& SpeakerId) const
{
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UKomorebiAudioSubsystem* Audio = GI->GetSubsystem<UKomorebiAudioSubsystem>())
			{
				Audio->SpeakRadioLine(Line, SpeakerId, true, true);
			}
		}
	}
}
