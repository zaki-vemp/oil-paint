// Copyright Komorebi Project. Replicated 5-class arcade vehicle pawn wrapping KomorebiCore::VehiclePhysics.
#include "Vehicles/KomorebiVehiclePawn.h"

#include "Audio/KomorebiAudioSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Campaign/MissionSubsystem.h"
#include "CollisionQueryParams.h"
#include "Components/BoxComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/LevelsData.h"
#include "Game/OnlineMeta.h"
#include "Game/StreetConfig.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "World/CoordBridge.h"

#include <cmath>

bool AKomorebiVehiclePawn::bFirstRideCommsPlayed = false;

namespace
{
	FLinearColor DefaultVehicleBodyColor(EKomorebiVehicleKind Kind)
	{
		switch (Kind)
		{
			case EKomorebiVehicleKind::Car:     return FLinearColor(0.14f, 0.24f, 0.36f, 1.0f);
			case EKomorebiVehicleKind::Sports:  return FLinearColor(0.86f, 0.12f, 0.28f, 1.0f);
			case EKomorebiVehicleKind::Pickup:  return FLinearColor(0.22f, 0.26f, 0.28f, 1.0f);
			case EKomorebiVehicleKind::Bike:    return FLinearColor(0.04f, 0.72f, 0.85f, 1.0f);
			case EKomorebiVehicleKind::Bicycle: return FLinearColor(0.95f, 0.78f, 0.08f, 1.0f);
		}
		return FLinearColor(0.2f, 0.25f, 0.3f, 1.0f);
	}

	int32 RequiredSectorForVehicle(EKomorebiVehicleKind Kind)
	{
		switch (Kind)
		{
			case EKomorebiVehicleKind::Sports: return 2;
			case EKomorebiVehicleKind::Pickup: return 3;
			default:                           return 1;
		}
	}
}

AKomorebiVehiclePawn::AKomorebiVehiclePawn()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);

	VehicleRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VehicleRoot"));
	SetRootComponent(VehicleRoot);

	ChassisCollider = CreateDefaultSubobject<UBoxComponent>(TEXT("ChassisCollider"));
	ChassisCollider->SetupAttachment(VehicleRoot);
	ChassisCollider->SetCollisionObjectType(ECC_WorldDynamic);
	ChassisCollider->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ChassisCollider->SetCollisionResponseToAllChannels(ECR_Block);
	ChassisCollider->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	ChassisVisualPivot = CreateDefaultSubobject<USceneComponent>(TEXT("ChassisVisualPivot"));
	ChassisVisualPivot->SetupAttachment(VehicleRoot);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(ChassisVisualPivot);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	RiderMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RiderMesh"));
	RiderMesh->SetupAttachment(ChassisVisualPivot);
	RiderMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RiderMesh->SetVisibility(false);

	SmokeVfxMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SmokeVfxMesh"));
	SmokeVfxMesh->SetupAttachment(ChassisVisualPivot);
	SmokeVfxMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SmokeVfxMesh->SetCastShadow(false);
	SmokeVfxMesh->SetVisibility(false);

	FireVfxMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FireVfxMesh"));
	FireVfxMesh->SetupAttachment(ChassisVisualPivot);
	FireVfxMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FireVfxMesh->SetCastShadow(false);
	FireVfxMesh->SetVisibility(false);

	HeadlightLeft = CreateDefaultSubobject<USpotLightComponent>(TEXT("HeadlightLeft"));
	HeadlightLeft->SetupAttachment(ChassisVisualPivot);
	HeadlightLeft->SetLightColor(FLinearColor(0.88f, 0.94f, 1.0f, 1.0f));
	HeadlightLeft->SetIntensity(4500.0f);
	HeadlightLeft->SetAttenuationRadius(1800.0f);
	HeadlightLeft->SetOuterConeAngle(36.0f);
	HeadlightLeft->SetInnerConeAngle(16.0f);
	HeadlightLeft->SetCastShadows(false);

	HeadlightRight = CreateDefaultSubobject<USpotLightComponent>(TEXT("HeadlightRight"));
	HeadlightRight->SetupAttachment(ChassisVisualPivot);
	HeadlightRight->SetLightColor(FLinearColor(0.88f, 0.94f, 1.0f, 1.0f));
	HeadlightRight->SetIntensity(4500.0f);
	HeadlightRight->SetAttenuationRadius(1800.0f);
	HeadlightRight->SetOuterConeAngle(36.0f);
	HeadlightRight->SetInnerConeAngle(16.0f);
	HeadlightRight->SetCastShadows(false);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(VehicleRoot);
	CameraBoom->TargetArmLength = 1050.0f;
	CameraBoom->SocketOffset = FVector(0.0f, 0.0f, 220.0f);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->ProbeChannel = ECC_WorldStatic;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 10.0f;

	ChaseCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ChaseCamera"));
	ChaseCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	ChaseCamera->bUsePawnControlRotation = false;
}

void AKomorebiVehiclePawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AKomorebiVehiclePawn, VehicleKind);
	DOREPLIFETIME(AKomorebiVehiclePawn, ReplicatedNetState);
}

void AKomorebiVehiclePawn::BeginPlay()
{
	Super::BeginPlay();

	if (!bInitialized)
	{
		const float InitYaw = static_cast<float>(KomorebiCore::UEYawDegToThreeYaw(GetActorRotation().Yaw));
		InitializeVehicle(VehicleKind, GetActorLocation(), InitYaw);
	}
}

const KomorebiCore::VehicleSpec& AKomorebiVehiclePawn::GetVehicleSpec() const
{
	return KomorebiCore::GetVehicleSpec(KomorebiBridge::ToCoreVehicleKind(VehicleKind));
}

void AKomorebiVehiclePawn::InitializeVehicle(
	EKomorebiVehicleKind InKind,
	FVector SpawnWorldLocation,
	float SpawnThreeYawRad)
{
	VehicleKind = InKind;
	const KomorebiCore::EVehicleKind CoreKind = KomorebiBridge::ToCoreVehicleKind(InKind);
	const KomorebiCore::VehicleSpec& Spec = KomorebiCore::GetVehicleSpec(CoreKind);

	if (!CustomHeightProvider && !FallbackTerrain.IsPrepared())
	{
		FallbackTerrain.Prepare();
	}

	const KomorebiCore::Vec3 SpawnThree = KomorebiBridge::FVectorToThree(SpawnWorldLocation);

	SimState = KomorebiCore::VehicleState{};
	SimState.Kind = CoreKind;
	SimState.X = SpawnThree.X;
	SimState.Z = SpawnThree.Z;
	SimState.Y = SampleHeightAt(SpawnThree.X, SpawnThree.Z);
	SimState.Yaw = static_cast<double>(SpawnThreeYawRad);
	SimState.Wheels = KomorebiCore::MakeDefaultWheels(Spec);

	HomeThreeX = SimState.X;
	HomeThreeZ = SimState.Z;
	HomeThreeYaw = SimState.Yaw;

	HullHp = static_cast<float>(Spec.Hull);
	bWrecked = false;
	BoomTimer = 0.0f;
	LeanAngle = 0.0;
	WheelSpinRad = 0.0;
	bVisualDirty = true;

	ConfigureChassisVisualGeometry();
	RebuildWheelRig();

	KomorebiCore::SettleSuspension(SimState, Spec, [this](double X, double Z) { return SampleHeightAt(X, Z); });

	CameraBoom->TargetArmLength = static_cast<float>(Spec.Eye * 100.0);

	bInitialized = true;
	SyncVisualPose(0.0f, SpawnWorldLocation);
}

void AKomorebiVehiclePawn::SetVehicleMeshAssets(UStaticMesh* InBodyMesh, UStaticMesh* InWheelMesh, UStaticMesh* InRiderMesh)
{
	if (InBodyMesh && BodyMesh)
	{
		BodyMesh->SetStaticMesh(InBodyMesh);
		BodyMesh->SetRelativeLocation(FVector::ZeroVector);
		BodyMesh->SetRelativeScale3D(FVector::OneVector);
	}
	if (InRiderMesh && RiderMesh)
	{
		RiderMesh->SetStaticMesh(InRiderMesh);
		RiderMesh->SetRelativeLocation(FVector::ZeroVector);
		RiderMesh->SetRelativeScale3D(FVector::OneVector);
	}
	if (InWheelMesh)
	{
		CustomWheelMesh = InWheelMesh;
		for (FKomorebiWheelRig& Rig : WheelRigs)
		{
			if (Rig.WheelMesh)
			{
				Rig.WheelMesh->SetStaticMesh(InWheelMesh);
				Rig.WheelMesh->SetRelativeRotation(FRotator::ZeroRotator);
				Rig.WheelMesh->SetRelativeScale3D(FVector::OneVector);
			}
		}
	}
}

void AKomorebiVehiclePawn::SetTerrainHeightProvider(TFunction<double(double, double)> InHeightProvider)
{
	CustomHeightProvider = MoveTemp(InHeightProvider);
}

void AKomorebiVehiclePawn::ConfigureChassisVisualGeometry()
{
	const KomorebiCore::VehicleSpec& Spec = GetVehicleSpec();

	// Collider box matches ride.js cuboid(wid/2, 0.6, len/2) mapped to UE X=Len, Y=Wid, Z=Height.
	ChassisCollider->SetBoxExtent(FVector(
		static_cast<float>(Spec.Len * 50.0),
		static_cast<float>(Spec.Wid * 50.0),
		60.0f));
	ChassisCollider->SetRelativeLocation(FVector(0.0f, 0.0f, 70.0f));

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	if (BodyMesh && !BodyMesh->GetStaticMesh() && CubeMesh)
	{
		BodyMesh->SetStaticMesh(CubeMesh);
		const float BodyHeightMeters = Spec.Lean ? 0.72f : (VehicleKind == EKomorebiVehicleKind::Pickup ? 1.45f : 1.05f);
		BodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, static_cast<float>(Spec.Wheel * 100.0 + BodyHeightMeters * 45.0f)));
		BodyMesh->SetRelativeScale3D(FVector(
			static_cast<float>(Spec.Len),
			static_cast<float>(Spec.Wid),
			BodyHeightMeters));
		if (BaseMat)
		{
			UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(BaseMat, BodyMesh);
			Mat->SetVectorParameterValue(TEXT("Color"), DefaultVehicleBodyColor(VehicleKind));
			BodyMesh->SetMaterial(0, Mat);
		}
	}

	if (RiderMesh && !RiderMesh->GetStaticMesh() && CubeMesh)
	{
		RiderMesh->SetStaticMesh(CubeMesh);
		RiderMesh->SetRelativeLocation(FVector(-15.0f, 0.0f, 118.0f));
		RiderMesh->SetRelativeScale3D(FVector(0.45f, 0.42f, 0.95f));
	}
	if (RiderMesh)
	{
		RiderMesh->SetVisibility(Spec.Lean && (bPlayerDriven || bHasTrafficDriver || bRemoteNetDriven));
	}

	if (SmokeVfxMesh && SphereMesh)
	{
		SmokeVfxMesh->SetStaticMesh(SphereMesh);
		SmokeVfxMesh->SetRelativeLocation(FVector(static_cast<float>(-Spec.Len * 35.0), 0.0f, 130.0f));
		SmokeVfxMesh->SetRelativeScale3D(FVector(0.56f));
		if (BaseMat)
		{
			UMaterialInstanceDynamic* SmokeMat = UMaterialInstanceDynamic::Create(BaseMat, SmokeVfxMesh);
			SmokeMat->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.60f, 0.64f, 0.68f, 0.45f));
			SmokeVfxMesh->SetMaterial(0, SmokeMat);
		}
	}

	if (FireVfxMesh && SphereMesh)
	{
		FireVfxMesh->SetStaticMesh(SphereMesh);
		FireVfxMesh->SetRelativeLocation(FVector(static_cast<float>(-Spec.Len * 15.0), 0.0f, 115.0f));
		FireVfxMesh->SetRelativeScale3D(FVector(0.68f));
		if (BaseMat)
		{
			UMaterialInstanceDynamic* FireMat = UMaterialInstanceDynamic::Create(BaseMat, FireVfxMesh);
			FireMat->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 0.35f, 0.12f, 0.90f));
			FireVfxMesh->SetMaterial(0, FireMat);
		}
	}

	// Headlights: single center lamp on bikes, twin lamps on 4-wheelers (matching ride.js line 240).
	if (Spec.Lean)
	{
		HeadlightLeft->SetRelativeLocation(FVector(static_cast<float>(Spec.Len * 44.0), 0.0f, 72.0f));
		HeadlightLeft->SetRelativeRotation(FRotator(-6.0f, 0.0f, 0.0f));
		HeadlightLeft->SetVisibility(true);
		HeadlightRight->SetVisibility(false);
	}
	else
	{
		const float HalfLampY = static_cast<float>(Spec.Wid * 34.0);
		const float LampX = static_cast<float>(Spec.Len * 46.0);
		HeadlightLeft->SetRelativeLocation(FVector(LampX, -HalfLampY, 62.0f));
		HeadlightLeft->SetRelativeRotation(FRotator(-6.0f, 0.0f, 0.0f));
		HeadlightLeft->SetVisibility(true);
		HeadlightRight->SetRelativeLocation(FVector(LampX, HalfLampY, 62.0f));
		HeadlightRight->SetRelativeRotation(FRotator(-6.0f, 0.0f, 0.0f));
		HeadlightRight->SetVisibility(true);
	}
}

void AKomorebiVehiclePawn::RebuildWheelRig()
{
	for (FKomorebiWheelRig& Rig : WheelRigs)
	{
		if (Rig.WheelMesh) Rig.WheelMesh->DestroyComponent();
		if (Rig.SpinPivot) Rig.SpinPivot->DestroyComponent();
		if (Rig.SteerPivot) Rig.SteerPivot->DestroyComponent();
	}
	WheelRigs.Reset();

	const KomorebiCore::VehicleSpec& Spec = GetVehicleSpec();
	if (SimState.Wheels.empty())
	{
		SimState.Wheels = KomorebiCore::MakeDefaultWheels(Spec);
	}

	UStaticMesh* FallbackWheel = CustomWheelMesh ? CustomWheelMesh.Get() : LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	for (int32 i = 0; i < static_cast<int32>(SimState.Wheels.size()); ++i)
	{
		const KomorebiCore::VehicleWheel& W = SimState.Wheels[i];

		USceneComponent* SteerPivot = NewObject<USceneComponent>(this, *FString::Printf(TEXT("WheelSteerPivot_%d"), i));
		SteerPivot->SetupAttachment(VehicleRoot);
		// Map Three.js local (+Z forward, +X left, +Y up) -> UE local (+X forward, +Y right, +Z up).
		SteerPivot->SetRelativeLocation(FVector(
			static_cast<float>(W.LocalZ * 100.0),
			static_cast<float>(-W.LocalX * 100.0),
			static_cast<float>(Spec.Wheel * 100.0)));
		SteerPivot->RegisterComponent();

		USceneComponent* SpinPivot = NewObject<USceneComponent>(this, *FString::Printf(TEXT("WheelSpinPivot_%d"), i));
		SpinPivot->SetupAttachment(SteerPivot);
		SpinPivot->RegisterComponent();

		UStaticMeshComponent* WheelMesh = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("WheelMesh_%d"), i));
		WheelMesh->SetupAttachment(SpinPivot);
		WheelMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (FallbackWheel)
		{
			WheelMesh->SetStaticMesh(FallbackWheel);
			if (!CustomWheelMesh)
			{
				// Orient cylinder axle along UE Y (right-left) with wheel diameter = 2 * Spec.Wheel.
				WheelMesh->SetRelativeRotation(FRotator(0.0f, 0.0f, 90.0f));
				const float Diam = static_cast<float>(Spec.Wheel * 2.0);
				const float TireWidth = Spec.Lean ? 0.14f : 0.24f;
				WheelMesh->SetRelativeScale3D(FVector(Diam, Diam, TireWidth));
				if (BaseMat)
				{
					UMaterialInstanceDynamic* TireMat = UMaterialInstanceDynamic::Create(BaseMat, WheelMesh);
					TireMat->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.06f, 0.07f, 0.09f, 1.0f));
					WheelMesh->SetMaterial(0, TireMat);
				}
			}
		}
		WheelMesh->RegisterComponent();

		FKomorebiWheelRig Rig;
		Rig.SteerPivot = SteerPivot;
		Rig.SpinPivot = SpinPivot;
		Rig.WheelMesh = WheelMesh;
		Rig.bFront = W.Front;
		Rig.LocalThreeX = W.LocalX;
		Rig.LocalThreeZ = W.LocalZ;
		WheelRigs.Add(Rig);
	}
}

void AKomorebiVehiclePawn::OnRep_VehicleKind()
{
	if (bInitialized)
	{
		InitializeVehicle(VehicleKind, GetActorLocation(), static_cast<float>(SimState.Yaw));
	}
}

void AKomorebiVehiclePawn::OnRep_NetState()
{
	if (bPlayerDriven)
	{
		return;
	}
	bRemoteNetDriven = ReplicatedNetState.bOccupied;
	HullHp = ReplicatedNetState.HullHp;
	bWrecked = ReplicatedNetState.bWrecked;
	SimState.Tipped = ReplicatedNetState.bTipped;
	SimState.Steer = static_cast<double>(ReplicatedNetState.Steer);
	if (const UWorld* World = GetWorld())
	{
		LastNetStateReceivedTime = World->GetTimeSeconds();
	}
}

void AKomorebiVehiclePawn::ServerSendNetState_Implementation(const FKomorebiVehicleNetState& NewState)
{
	ReplicatedNetState = NewState;
	if (!bPlayerDriven)
	{
		OnRep_NetState();
	}
}

void AKomorebiVehiclePawn::ResetToHome()
{
	TeleportThree(HomeThreeX, HomeThreeZ, HomeThreeYaw, true);
}

void AKomorebiVehiclePawn::TeleportThree(double ThreeX, double ThreeZ, double ThreeYawRad, bool bRestoreHull)
{
	const KomorebiCore::VehicleSpec& Spec = GetVehicleSpec();
	SimState.X = ThreeX;
	SimState.Z = ThreeZ;
	SimState.Yaw = ThreeYawRad;
	SimState.Vx = 0.0;
	SimState.Vz = 0.0;
	SimState.Vy = 0.0;
	SimState.Vf = 0.0;
	SimState.Steer = 0.0;
	SimState.YawRate = 0.0;
	SimState.Tilt = 0.0;
	SimState.RollRate = 0.0;
	SimState.SpinRate = 0.0;
	SimState.Tipped = false;
	SimState.Liable = 0.0;
	LeanAngle = 0.0;
	if (bRestoreHull)
	{
		HullHp = static_cast<float>(Spec.Hull);
		bWrecked = false;
		BoomTimer = 0.0f;
	}
	bVisualDirty = true;

	KomorebiCore::SettleSuspension(SimState, Spec, [this](double X, double Z) { return SampleHeightAt(X, Z); });
	SyncVisualPose(0.0f, GetActorLocation());
}

void AKomorebiVehiclePawn::SetDriveInput(float InThrottle, float InSteer, bool bInHandbrake)
{
	CurrentInputThrottle = FMath::Clamp(InThrottle, -1.0f, 1.0f);
	CurrentInputSteer = FMath::Clamp(InSteer, -1.0f, 1.0f);
	bCurrentInputHandbrake = bInHandbrake;
}

void AKomorebiVehiclePawn::SetHasTrafficDriver(bool bHasDriver)
{
	bHasTrafficDriver = bHasDriver;
	if (RiderMesh)
	{
		RiderMesh->SetVisibility(GetVehicleSpec().Lean && (bPlayerDriven || bHasTrafficDriver || bRemoteNetDriven));
	}
}

void AKomorebiVehiclePawn::SetNightPhase(float Night01)
{
	const float SmoothNight = static_cast<float>(KomorebiCore::SmoothStep(0.25, 0.80, static_cast<double>(Night01)));
	const float Intensity = SmoothNight * 4800.0f;
	const bool bOn = Intensity > 40.0f && !bWrecked;
	if (HeadlightLeft)
	{
		HeadlightLeft->SetIntensity(Intensity);
		HeadlightLeft->SetVisibility(bOn);
	}
	if (HeadlightRight)
	{
		HeadlightRight->SetIntensity(Intensity);
		HeadlightRight->SetVisibility(bOn && !GetVehicleSpec().Lean);
	}
}

// --- Driver Entry, Exit & Ejection -------------------------------------------------------------

bool AKomorebiVehiclePawn::TryEnterVehicle(
	APawn* InDriverPawn,
	bool bRoamMode,
	int32 ClearedSectors,
	FString& OutFeedbackMessage)
{
	OutFeedbackMessage.Reset();
	if (!InDriverPawn)
	{
		return false;
	}

	const double Speed = std::hypot(SimState.Vx, SimState.Vz);
	if (Speed > 2.0)
	{
		OutFeedbackMessage = TEXT("Wait for it to stop");
		OnFeedbackText.Broadcast(OutFeedbackMessage);
		return false;
	}

	if (!bRoamMode)
	{
		const std::vector<KomorebiCore::EVehicleKind> Unlocks = KomorebiCore::UnlocksFor(ClearedSectors);
		const KomorebiCore::EVehicleKind CoreKind = KomorebiBridge::ToCoreVehicleKind(VehicleKind);
		const bool bUnlocked = std::find(Unlocks.begin(), Unlocks.end(), CoreKind) != Unlocks.end();
		if (!bUnlocked)
		{
			OutFeedbackMessage = FString::Printf(TEXT("Unlocks in Sector %d"), RequiredSectorForVehicle(VehicleKind));
			OnFeedbackText.Broadcast(OutFeedbackMessage);
			return false;
		}
	}

	if (bRemoteNetDriven)
	{
		OutFeedbackMessage = TEXT("Someone is driving it");
		OnFeedbackText.Broadcast(OutFeedbackMessage);
		return false;
	}

	if (bWrecked)
	{
		OutFeedbackMessage = TEXT("It is finished");
		OnFeedbackText.Broadcast(OutFeedbackMessage);
		return false;
	}

	// G beside a stopped overturned vehicle rights it back onto its wheels (ride.js line 539).
	if (SimState.Tipped)
	{
		SimState.Tilt = 0.0;
		SimState.RollRate = 0.0;
		SimState.SpinRate = 0.0;
		SimState.Tipped = false;
		SimState.Vx = 0.0;
		SimState.Vz = 0.0;
		bVisualDirty = true;
		KomorebiCore::SettleSuspension(SimState, GetVehicleSpec(), [this](double X, double Z) { return SampleHeightAt(X, Z); });
		SyncVisualPose(0.0f, InDriverPawn->GetActorLocation());
		OutFeedbackMessage = TEXT("Back on its wheels · G to drive");
		OnFeedbackText.Broadcast(OutFeedbackMessage);
		return false;
	}

	const bool bStolen = bHasTrafficDriver;
	bHasTrafficDriver = false;
	bPlayerDriven = true;
	DriverPawn = InDriverPawn;
	SimState.Liable = 5.0;

	if (RiderMesh && GetVehicleSpec().Lean)
	{
		RiderMesh->SetVisibility(true);
	}

	// Hide player pawn while seated inside/on the vehicle and transfer controller possession.
	InDriverPawn->SetActorHiddenInGame(true);
	InDriverPawn->SetActorEnableCollision(false);
	if (AController* PrevController = InDriverPawn->GetController())
	{
		PrevController->Possess(this);
	}

	PlayAudioSfx(EKomorebiSfx::EnterVehicle);
	if (!bFirstRideCommsPlayed)
	{
		bFirstRideCommsPlayed = true;
		if (UWorld* World = GetWorld())
		{
			if (UMissionSubsystem* Missions = World->GetSubsystem<UMissionSubsystem>())
			{
				Missions->SpeakComms(TEXT("firstRide"));
			}
		}
	}

	const FString VehicleName = KomorebiBridge::ToFString(GetVehicleSpec().Name);
	OutFeedbackMessage = FString::Printf(TEXT("%s · W A S D drive · Space handbrake · G out"), *VehicleName);
	OnFeedbackText.Broadcast(OutFeedbackMessage);
	OnVehicleEntered.Broadcast(this, InDriverPawn, bStolen);
	return true;
}

bool AKomorebiVehiclePawn::TryExitVehicle(bool bQuiet, FString& OutFeedbackMessage)
{
	OutFeedbackMessage.Reset();
	if (!bPlayerDriven)
	{
		return false;
	}

	const double Speed = std::hypot(SimState.Vx, SimState.Vz);
	if (!bQuiet && Speed > KomorebiCore::kBailSpeed)
	{
		if (BailWarningCooldown <= 0.0f)
		{
			BailWarningCooldown = 2.0f;
			OutFeedbackMessage = TEXT("Too fast to bail");
			OnFeedbackText.Broadcast(OutFeedbackMessage);
		}
		return false;
	}

	APawn* ExitingDriver = DriverPawn.Get();
	const KomorebiCore::VehicleSpec& Spec = GetVehicleSpec();

	double ExitThreeX = SimState.X;
	double ExitThreeZ = SimState.Z;

	if (!bQuiet)
	{
		const double Lx = std::cos(SimState.Yaw);
		const double Lz = -std::sin(SimState.Yaw);
		const double SideD = Spec.Wid * 0.5 + 0.75;
		const double RearD = Spec.Len * 0.5 + 0.90;

		const double Candidates[3][2] = {
			{ Lx * SideD, Lz * SideD },
			{ -Lx * SideD, -Lz * SideD },
			{ -Lz * RearD, Lx * RearD },
		};

		bool bFoundClear = false;
		for (int32 i = 0; i < 3; ++i)
		{
			const double Cx = SimState.X + Candidates[i][0];
			const double Cz = SimState.Z + Candidates[i][1];
			if (IsExitSpotClear(Cx, Cz))
			{
				ExitThreeX = Cx;
				ExitThreeZ = Cz;
				bFoundClear = true;
				break;
			}
		}

		if (!bFoundClear)
		{
			OutFeedbackMessage = TEXT("No room to step out");
			OnFeedbackText.Broadcast(OutFeedbackMessage);
			return false;
		}
	}

	bPlayerDriven = false;
	CurrentInputThrottle = 0.0f;
	CurrentInputSteer = 0.0f;
	bCurrentInputHandbrake = false;

	if (RiderMesh && Spec.Lean)
	{
		RiderMesh->SetVisibility(false);
	}

	AController* ActiveController = GetController();
	DriverPawn.Reset();

	if (ExitingDriver)
	{
		if (!bQuiet)
		{
			const double GroundY = SampleHeightAt(ExitThreeX, ExitThreeZ);
			const FVector ExitUE = KomorebiBridge::ThreeXZToFVector(ExitThreeX, GroundY + 0.95, ExitThreeZ);
			const FRotator ExitRot(0.0f, static_cast<float>(KomorebiCore::ThreeYawToUEYawDeg(SimState.Yaw)), 0.0f);
			ExitingDriver->SetActorLocationAndRotation(ExitUE, ExitRot, false, nullptr, ETeleportType::TeleportPhysics);
		}
		ExitingDriver->SetActorHiddenInGame(false);
		ExitingDriver->SetActorEnableCollision(true);
		if (ActiveController)
		{
			ActiveController->Possess(ExitingDriver);
		}
	}

	if (!bQuiet)
	{
		PlayAudioSfx(EKomorebiSfx::ExitVehicle);
	}

	OnVehicleExited.Broadcast(this, ExitingDriver);
	return true;
}

void AKomorebiVehiclePawn::BailOut(const FString& Reason)
{
	if (!bPlayerDriven)
	{
		return;
	}

	APawn* ExitingDriver = DriverPawn.Get();
	const KomorebiCore::VehicleSpec& Spec = GetVehicleSpec();
	const double Lx = std::cos(SimState.Yaw);
	const double Lz = -std::sin(SimState.Yaw);
	const double EjectX = SimState.X + Lx * (Spec.Wid * 0.5 + 0.9);
	const double EjectZ = SimState.Z + Lz * (Spec.Wid * 0.5 + 0.9);

	FString Unused;
	TryExitVehicle(true, Unused);

	if (ExitingDriver)
	{
		const double GroundY = SampleHeightAt(EjectX, EjectZ);
		const FVector EjectUE = KomorebiBridge::ThreeXZToFVector(EjectX, GroundY + 0.95, EjectZ);
		const FRotator EjectRot(0.0f, static_cast<float>(KomorebiCore::ThreeYawToUEYawDeg(SimState.Yaw)), 0.0f);
		ExitingDriver->SetActorLocationAndRotation(EjectUE, EjectRot, false, nullptr, ETeleportType::TeleportPhysics);
	}

	if (!Reason.IsEmpty())
	{
		OnFeedbackText.Broadcast(Reason);
	}
}

// --- Physics Substep, Sweeps, Ramming & Hull Damage --------------------------------------------

void AKomorebiVehiclePawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const float Dt = FMath::Clamp(DeltaTime, 0.0f, 0.1f);
	BailWarningCooldown = FMath::Max(0.0f, BailWarningCooldown - Dt);

	// Decay per-pawn ram hit cooldowns.
	for (auto It = RamHitCooldowns.CreateIterator(); It; ++It)
	{
		It.Value() -= Dt;
		if (It.Value() <= 0.0f || !It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	if (bPlayerDriven)
	{
		PollLocalDriverInput(Dt);
		UpdateChaseCamera(Dt);
	}

	// If not managed by UTrafficSubsystem (e.g., standalone test map), step ourselves at 120Hz.
	if (!bManagedByTrafficSubsystem)
	{
		if (bRemoteNetDriven && !bPlayerDriven)
		{
			UpdateRemoteNetInterpolation(Dt);
		}
		else
		{
			const int32 SubCount = FMath::Max(1, FMath::CeilToInt(Dt * 120.0f));
			const double H = static_cast<double>(Dt) / static_cast<double>(SubCount);
			const KomorebiCore::VehicleDriveInput Input = bPlayerDriven
				? KomorebiCore::VehicleDriveInput{ CurrentInputThrottle, CurrentInputSteer, bCurrentInputHandbrake }
				: KomorebiCore::VehicleDriveInput{ 0.0, 0.0, true };

			for (int32 i = 0; i < SubCount; ++i)
			{
				StepPhysicsSubstep(H, Input, bPlayerDriven);
			}
		}

		const FVector ViewerLoc = GetActorLocation();
		SyncVisualPose(Dt, ViewerLoc);
	}

	if (bPlayerDriven && DriverPawn.IsValid())
	{
		DriverPawn->SetActorLocation(GetActorLocation(), false, nullptr, ETeleportType::TeleportPhysics);
	}

	PushNetworkState(Dt);
}

void AKomorebiVehiclePawn::PollLocalDriverInput(float /*DeltaTime*/)
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalPlayerController())
	{
		return;
	}

	float Throttle = 0.0f;
	if (PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::Up)) Throttle += 1.0f;
	if (PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::Down)) Throttle -= 1.0f;

	float Steer = 0.0f;
	if (PC->IsInputKeyDown(EKeys::D) || PC->IsInputKeyDown(EKeys::Right)) Steer += 1.0f;
	if (PC->IsInputKeyDown(EKeys::A) || PC->IsInputKeyDown(EKeys::Left)) Steer -= 1.0f;

	const bool bHandbrake = PC->IsInputKeyDown(EKeys::SpaceBar);
	SetDriveInput(Throttle, Steer, bHandbrake);

	if (PC->WasInputKeyJustPressed(EKeys::G))
	{
		FString Feedback;
		TryExitVehicle(false, Feedback);
	}
}

void AKomorebiVehiclePawn::UpdateChaseCamera(float DeltaTime)
{
	const KomorebiCore::VehicleSpec& Spec = GetVehicleSpec();
	const float Speed = static_cast<float>(std::hypot(SimState.Vx, SimState.Vz));
	const float BoostMeters = FMath::Min(4.2f, Speed * 0.13f);
	const float TargetArmCm = static_cast<float>(Spec.Eye * 100.0) + BoostMeters * 100.0f;
	CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, TargetArmCm, DeltaTime, 6.0f);

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (PC && PC->IsLocalPlayerController())
	{
		float MouseDx = 0.0f;
		float MouseDy = 0.0f;
		PC->GetInputMouseDelta(MouseDx, MouseDy);
		if (FMath::Abs(MouseDx) + FMath::Abs(MouseDy) > 1e-3f)
		{
			CameraLookIdleTimer = 0.0f;
		}
		else
		{
			CameraLookIdleTimer += DeltaTime;
		}

		// Re-center follow camera smoothly behind vehicle once mouse has been still > 0.7s while moving forward.
		if (CameraLookIdleTimer > 0.7f && SimState.Vf > 2.0)
		{
			const float TargetYawDeg = static_cast<float>(KomorebiCore::ThreeYawToUEYawDeg(SimState.Yaw));
			FRotator ControlRot = PC->GetControlRotation();
			ControlRot.Yaw = FMath::RInterpTo(ControlRot, FRotator(ControlRot.Pitch, TargetYawDeg, 0.0f), DeltaTime, 2.4f).Yaw;
			PC->SetControlRotation(ControlRot);
		}
	}
}

void AKomorebiVehiclePawn::StepPhysicsSubstep(
	double SubDt,
	const KomorebiCore::VehicleDriveInput& InInput,
	bool bIsPlayerDriven)
{
	const KomorebiCore::VehicleSpec& Spec = GetVehicleSpec();
	const double BeforeVf = SimState.Vf;

	SimState.Liable = std::max(0.0, SimState.Liable - SubDt);
	if (bIsPlayerDriven)
	{
		SimState.Liable = 5.0;
	}

	const KomorebiCore::VehicleDriveInput EffectiveInput = bWrecked
		? KomorebiCore::VehicleDriveInput{ 0.0, 0.0, true }
		: InInput;

	// Grip fades off asphalt shoulder and when suspension unloads over a crest.
	const double Dirt = KomorebiCore::SmoothStep(KomorebiCore::kRoad, KomorebiCore::kRoad + 2.0, KomorebiCore::WayDist(SimState.X, SimState.Z));
	const double Unloaded = KomorebiCore::SmoothStep(0.0, Spec.Travel, SimState.Y - SimState.Floor);
	const double GripMul = (1.0 - 0.6 * Dirt) * (1.0 - 0.45 * Unloaded);

	KomorebiCore::DriveVehicle(SimState, Spec, EffectiveInput, SubDt, SimState.Air <= 0.0 && !SimState.Tipped, GripMul);

	if (SimState.Tipped)
	{
		const double Drag = std::exp(-2.5 * SubDt);
		SimState.Vx *= Drag;
		SimState.Vz *= Drag;
	}

	SimState.Yaw = KomorebiCore::WrapAngle(SimState.Yaw + SimState.SpinRate * SubDt);
	SimState.SpinRate *= std::exp(-3.0 * SubDt);

	KomorebiCore::RockVehicle(SimState, SubDt, bIsPlayerDriven);

	const double Fx = std::sin(SimState.Yaw);
	const double Fz = std::cos(SimState.Yaw);
	const double Sp = std::hypot(SimState.Vx, SimState.Vz);

	if (Sp > 0.02)
	{
		const double Ux = SimState.Vx / Sp;
		const double Uz = SimState.Vz / Sp;
		const double Dist = Sp * SubDt;

		double ImpactDist = Dist;
		double Nx = 0.0;
		double Nz = 0.0;
		const bool bHitStatic = SweepChassisStatic(SimState.X, SimState.Y, SimState.Z, Ux, Uz, Dist, ImpactDist, Nx, Nz);

		double Go = bHitStatic ? std::max(0.0, ImpactDist - 0.02) : Dist;
		const double NextX = SimState.X + Ux * Dist;
		const double NextZ = SimState.Z + Uz * Dist;
		constexpr double Bound = KomorebiCore::kExtent - 10.0;
		if (!bHitStatic && (std::abs(NextX) > Bound || std::abs(NextZ) > Bound))
		{
			Go = 0.0;
			Nx = -Ux;
			Nz = -Uz;
		}

		SimState.X += Ux * std::min(Go, Dist);
		SimState.Z += Uz * std::min(Go, Dist);

		const double Nl = std::hypot(Nx, Nz);
		if (Nl > 1e-3)
		{
			Nx /= Nl;
			Nz /= Nl;
			const double Into = SimState.Vx * Nx + SimState.Vz * Nz;
			if (Into < 0.0)
			{
				SimState.Vx = (SimState.Vx - 1.3 * Into * Nx) * 0.92;
				SimState.Vz = (SimState.Vz - 1.3 * Into * Nz) * 0.92;

				const double SideSpin = (Nx * Fz - Nz * Fx) * Into * (Spec.Lean ? 0.26 : 0.32);
				SimState.RollRate += (-Into < 16.0) ? KomorebiCore::Clamp(SideSpin, -3.2, 3.2) : SideSpin;

				if (bIsPlayerDriven && -Into > 5.0)
				{
					OnCameraJolt.Broadcast(this, static_cast<float>(std::min(0.22, -Into * 0.012)));
					PlayAudioSfx(EKomorebiSfx::VehicleCrash, static_cast<float>(-Into / 12.0));
				}
				if (bIsPlayerDriven && -Into > 11.0)
				{
					const float CrashDmg = static_cast<float>(std::min(18.0, (-Into - 11.0) * 1.4));
					OnDriverCrashDamage.Broadcast(this, CrashDmg);
					if (DriverPawn.IsValid())
					{
						UGameplayStatics::ApplyDamage(DriverPawn.Get(), CrashDmg, GetController(), this, nullptr);
					}
				}

				ApplyHullDamage(900.0f, static_cast<float>(-Into));

				if (bIsPlayerDriven && Spec.ThrowRider && -Into > 18.0 && !bWrecked)
				{
					BailOut(TEXT("Thrown"));
				}
			}
		}

		if (Sp > KomorebiCore::kRamConfig.Min && SimState.Liable > 0.0 && !bWrecked)
		{
			CheckRammingImpacts(Sp, Ux, Uz, Fx, Fz);
		}
	}

	const double Landing = KomorebiCore::SuspendVehicle(
		SimState,
		Spec,
		[this](double X, double Z) { return SampleHeightAt(X, Z); },
		SubDt);

	if (bIsPlayerDriven && Landing > 5.0)
	{
		OnCameraJolt.Broadcast(this, static_cast<float>(std::min(0.20, Landing * 0.014)));
	}

	SimState.Vf = SimState.Vx * std::sin(SimState.Yaw) + SimState.Vz * std::cos(SimState.Yaw);
	const double WantLean = KomorebiCore::Clamp(-SimState.YawRate * SimState.Vf / 9.8, -0.6, 0.6);
	LeanAngle = KomorebiCore::Lerp(LeanAngle, WantLean, 1.0 - std::exp(-8.0 * SubDt));

	const double StrokeAngle = 1.25 * Spec.Travel / Spec.Base;
	const double BrakeDive = (SubDt > 0.0 && SimState.Air <= 0.0)
		? KomorebiCore::Clamp((BeforeVf - SimState.Vf) / SubDt * 0.004, -StrokeAngle, StrokeAngle)
		: 0.0;
	SimState.Squat = KomorebiCore::Lerp(SimState.Squat, BrakeDive, 1.0 - std::exp(-12.0 * SubDt));

	if (bIsPlayerDriven && SimState.Tipped)
	{
		BailOut(TEXT("Thrown"));
	}
}

void AKomorebiVehiclePawn::CheckRammingImpacts(double SpeedMps, double Ux, double Uz, double Fx, double Fz)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const KomorebiCore::VehicleSpec& Spec = GetVehicleSpec();

	// 1. Destructible mission props at the front bumper.
	if (UMissionSubsystem* Missions = World->GetSubsystem<UMissionSubsystem>())
	{
		const FVector BumperUE = KomorebiBridge::ThreeXZToFVector(
			SimState.X + Fx * Spec.Len * 0.4,
			SimState.Y + 0.7,
			SimState.Z + Fz * Spec.Len * 0.4);
		if (Missions->HitAt(BumperUE, static_cast<float>(Spec.Wid * 0.7 + 0.9), static_cast<float>(SpeedMps * 5.0), TEXT("ram")))
		{
			SimState.Vx *= 0.85;
			SimState.Vz *= 0.85;
			OnCameraJolt.Broadcast(this, 0.08f);
		}
	}

	// 2. Enemies, civilians, and thieves in chassis footprint.
	const double MassFactor = Spec.Mass / 1200.0;
	const double Force = SpeedMps * MassFactor;
	const float RawDmg = static_cast<float>(std::min(KomorebiCore::kRamConfig.Max, SpeedMps * KomorebiCore::kRamConfig.Per * MassFactor));
	const FVector MoveDirUE = KomorebiBridge::ThreeDirToFVector(KomorebiCore::Vec3{ Ux, 0.0, Uz }).GetSafeNormal();

	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Candidate = *It;
		if (!Candidate || Candidate == this || Candidate == DriverPawn.Get() || Candidate->IsHidden() || Candidate->IsA<AKomorebiVehiclePawn>())
		{
			continue;
		}
		if (RamHitCooldowns.Contains(Candidate))
		{
			continue;
		}

		const KomorebiCore::Vec3 TargetThree = KomorebiBridge::FVectorToThree(Candidate->GetActorLocation());
		const double Dx = TargetThree.X - SimState.X;
		const double Dz = TargetThree.Z - SimState.Z;
		constexpr double PawnRadius = 0.35;

		if (std::abs(Dx * Fz - Dz * Fx) > Spec.Wid * 0.5 + PawnRadius
			|| std::abs(Dx * Fx + Dz * Fz) > Spec.Len * 0.5 + PawnRadius)
		{
			continue;
		}
		if (Dx * Ux + Dz * Uz < -0.4 || std::abs(TargetThree.Y - SimState.Y) > 2.2)
		{
			continue;
		}

		RamHitCooldowns.Add(Candidate, 0.35f);

		const bool bIsBrute = Candidate->ActorHasTag(TEXT("Brute"));
		const bool bIsCrew = Candidate->ActorHasTag(TEXT("Crew")) || bIsBrute;
		const float AppliedDamage = bIsCrew ? FMath::Min(RawDmg, static_cast<float>(KomorebiCore::kRamConfig.Crew)) : RawDmg;

		OnVehicleRamPawn.Broadcast(this, Candidate, static_cast<float>(SpeedMps), MoveDirUE, AppliedDamage);

		FHitResult DummyHit;
		DummyHit.ImpactPoint = Candidate->GetActorLocation();
		DummyHit.ImpactNormal = -MoveDirUE;
		UGameplayStatics::ApplyPointDamage(Candidate, AppliedDamage, MoveDirUE, DummyHit, GetController(), this, nullptr);

		const double Keep = bIsBrute ? 0.25 : Spec.Heft;
		SimState.Vx *= Keep;
		SimState.Vz *= Keep;
		OnCameraJolt.Broadcast(this, 0.07f);
		PlayAudioSfx(EKomorebiSfx::HitImpact, static_cast<float>(FMath::Clamp(Force / 14.0, 0.5, 1.5)));
	}
}

void AKomorebiVehiclePawn::CorrectAfterCollision(double PrevThreeX, double PrevThreeZ)
{
	const double Dx = SimState.X - PrevThreeX;
	const double Dz = SimState.Z - PrevThreeZ;
	const double Dist = std::hypot(Dx, Dz);
	if (Dist < 1e-6)
	{
		return;
	}

	SimState.X = PrevThreeX;
	SimState.Z = PrevThreeZ;

	double ImpactDist = Dist;
	double Nx = 0.0;
	double Nz = 0.0;
	const bool bHit = SweepChassisStatic(PrevThreeX, SimState.Y, PrevThreeZ, Dx / Dist, Dz / Dist, Dist, ImpactDist, Nx, Nz);
	const double Go = bHit ? std::max(0.0, ImpactDist - 0.02) : Dist;

	SimState.X += (Dx / Dist) * std::min(Dist, Go);
	SimState.Z += (Dz / Dist) * std::min(Dist, Go);

	KomorebiCore::WheelTerrain(SimState, GetVehicleSpec(), [this](double X, double Z) { return SampleHeightAt(X, Z); });
}

void AKomorebiVehiclePawn::ApplyHullDamage(float OtherMassKg, float ClosingSpeedMps)
{
	if (ClosingSpeedMps < 5.0f || bWrecked)
	{
		return;
	}

	HullHp = FMath::Max(0.0f, HullHp - ClosingSpeedMps * OtherMassKg / 1400.0f);
	if (HullHp <= 0.0f)
	{
		WreckVehicle();
	}
}

void AKomorebiVehiclePawn::WreckVehicle()
{
	if (bWrecked)
	{
		return;
	}

	bWrecked = true;
	HullHp = 0.0f;
	BoomTimer = 0.45f;
	bHasTrafficDriver = false;
	PlayAudioSfx(EKomorebiSfx::VehicleCrash, 1.5f);

	if (bPlayerDriven)
	{
		BailOut(TEXT("Wrecked"));
	}
	else
	{
		OnCameraJolt.Broadcast(this, 0.08f);
	}
	OnVehicleWrecked.Broadcast(this);
}

// --- Static World Sweeps & Ground Sampling -----------------------------------------------------

double AKomorebiVehiclePawn::SampleHeightAt(double ThreeX, double ThreeZ) const
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

bool AKomorebiVehiclePawn::SweepChassisStatic(
	double StartThreeX,
	double StartThreeY,
	double StartThreeZ,
	double DirX,
	double DirZ,
	double DistMeters,
	double& OutImpactDist,
	double& OutNormalThreeX,
	double& OutNormalThreeZ) const
{
	OutImpactDist = DistMeters;
	OutNormalThreeX = 0.0;
	OutNormalThreeZ = 0.0;

	const UWorld* World = GetWorld();
	if (!World || DistMeters <= 1e-5)
	{
		return false;
	}

	const KomorebiCore::VehicleSpec& Spec = GetVehicleSpec();
	const FVector StartUE = KomorebiBridge::ThreeXZToFVector(StartThreeX, StartThreeY + 0.8, StartThreeZ);
	const FVector EndUE = KomorebiBridge::ThreeXZToFVector(StartThreeX + DirX * DistMeters, StartThreeY + 0.8, StartThreeZ + DirZ * DistMeters);
	const FQuat BoxRot(FRotator(0.0f, static_cast<float>(KomorebiCore::ThreeYawToUEYawDeg(SimState.Yaw)), 0.0f));
	const FCollisionShape BoxShape = FCollisionShape::MakeBox(FVector(
		static_cast<float>(Spec.Len * 50.0),
		static_cast<float>(Spec.Wid * 50.0),
		40.0f));

	FCollisionQueryParams Params(SCENE_QUERY_STAT(KomorebiVehicleChassisSweep), false, this);
	if (DriverPawn.IsValid())
	{
		Params.AddIgnoredActor(DriverPawn.Get());
	}

	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);

	TArray<FHitResult> Hits;
	if (!World->SweepMultiByObjectType(Hits, StartUE, EndUE, BoxRot, ObjParams, BoxShape, Params))
	{
		return false;
	}

	for (const FHitResult& Hit : Hits)
	{
		const AActor* HitActor = Hit.GetActor();
		if (HitActor && (HitActor == this || HitActor->IsA<AKomorebiVehiclePawn>() || HitActor->ActorHasTag(TEXT("KomorebiTerrain"))))
		{
			continue;
		}
		// Skip flat/gentle ground triangles: heightAt handles terrain support so bumpers never snag.
		if (Hit.ImpactNormal.Z > 0.72f)
		{
			continue;
		}

		OutImpactDist = static_cast<double>(Hit.Distance) * KomorebiCore::kCmToMeters;
		const KomorebiCore::Vec3 NormalThree = KomorebiCore::UEDirToThree(KomorebiCore::Vec3{
			Hit.ImpactNormal.X,
			Hit.ImpactNormal.Y,
			Hit.ImpactNormal.Z,
		});
		OutNormalThreeX = NormalThree.X;
		OutNormalThreeZ = NormalThree.Z;
		return true;
	}

	return false;
}

bool AKomorebiVehiclePawn::IsExitSpotClear(double TargetThreeX, double TargetThreeZ) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}

	const FVector FromUE = KomorebiBridge::ThreeXZToFVector(SimState.X, SimState.Y + 0.9, SimState.Z);
	const double ExitGroundY = SampleHeightAt(TargetThreeX, TargetThreeZ);
	const FVector ToUE = KomorebiBridge::ThreeXZToFVector(TargetThreeX, ExitGroundY + 0.95, TargetThreeZ);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(KomorebiVehicleExitCheck), false, this);
	if (DriverPawn.IsValid())
	{
		Params.AddIgnoredActor(DriverPawn.Get());
	}

	FHitResult RayHit;
	if (World->LineTraceSingleByChannel(RayHit, FromUE, ToUE, ECC_WorldStatic, Params))
	{
		if (RayHit.ImpactNormal.Z <= 0.72f)
		{
			return false;
		}
	}

	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(32.0f, 82.0f);
	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
	TArray<FOverlapResult> Overlaps;
	if (World->OverlapMultiByObjectType(Overlaps, ToUE, FQuat::Identity, ObjParams, Capsule, Params))
	{
		for (const FOverlapResult& Ov : Overlaps)
		{
			const AActor* Actor = Ov.GetActor();
			if (Actor && Actor != this && !Actor->ActorHasTag(TEXT("KomorebiTerrain")))
			{
				return false;
			}
		}
	}

	return true;
}

// --- Visual Transform, Wheel Pivots & Smoke/Fire VFX -------------------------------------------

void AKomorebiVehiclePawn::SyncVisualPose(float DeltaTime, const FVector& ViewerWorldLocation)
{
	const KomorebiCore::VehicleSpec& Spec = GetVehicleSpec();
	const KomorebiCore::Vec3 ViewerThree = KomorebiBridge::FVectorToThree(ViewerWorldLocation);
	const double ViewerDist = std::hypot(SimState.X - ViewerThree.X, SimState.Z - ViewerThree.Z);
	const bool bVis = bPlayerDriven || ViewerDist < 110.0;

	SetActorHiddenInGame(!bVis);

	const bool bMoving = (DeltaTime <= 0.0f)
		|| bVisualDirty
		|| bPlayerDriven
		|| bHasTrafficDriver
		|| bRemoteNetDriven
		|| std::hypot(SimState.Vx, SimState.Vz) > 0.01
		|| SimState.Air > 0.0
		|| std::abs(SimState.RollRate) > 0.002;
	bVisualDirty = false;
	if (!bVis && !bMoving)
	{
		return;
	}

	const double PitchRad = SimState.Pitch + SimState.Squat;
	const double RollRad = SimState.Roll + SimState.Tilt + LeanAngle * (Spec.Lean ? 1.0 : -Spec.Travel);

	// Lift roof/side clear of the ground during a rollover (ride.js line 354).
	const double Lift = std::abs(std::sin(SimState.Tilt)) * Spec.Wid * 0.5
		+ std::abs(std::cos(SimState.Tilt)) * 0.7 - 0.7;

	// A bike leans about its tyres during a turn and about the chassis as it rolls over.
	const double PivotY = Spec.Lean
		? 0.7 * KomorebiCore::SmoothStep(0.0, 1.0, std::abs(SimState.Tilt))
		: 0.7;

	const FRotator ActorRot(
		static_cast<float>(-PitchRad * KomorebiCore::kRadToDeg),
		static_cast<float>(KomorebiCore::ThreeYawToUEYawDeg(SimState.Yaw)),
		static_cast<float>(RollRad * KomorebiCore::kRadToDeg));

	const FVector LocalPivotOffsetUE = ActorRot.RotateVector(FVector(0.0f, 0.0f, static_cast<float>(-PivotY * 100.0)));
	const FVector BasePosUE = KomorebiBridge::ThreeXZToFVector(SimState.X, SimState.Y + PivotY + Lift, SimState.Z);
	SetActorLocationAndRotation(BasePosUE + LocalPivotOffsetUE, ActorRot);

	// Wheel contact height compression, steering lock, and spin rotation.
	const double UpX = std::cos(PitchRad) * std::sin(RollRad);
	const double UpY = std::cos(PitchRad) * std::cos(RollRad);
	const double UpZ = -std::sin(PitchRad);
	const double RollingRadius = Spec.Wheel * std::hypot(UpY, UpZ);
	const double RootThreeY = (BasePosUE.Z + LocalPivotOffsetUE.Z) * KomorebiCore::kCmToMeters;

	WheelSpinRad += SimState.Vf * static_cast<double>(DeltaTime) / std::max(0.05, Spec.Wheel);
	const float SpinPitchDeg = static_cast<float>(-WheelSpinRad * KomorebiCore::kRadToDeg);
	const float SteerYawDeg = static_cast<float>(SimState.Steer * KomorebiCore::kRadToDeg);

	const int32 WheelCount = FMath::Min(WheelRigs.Num(), static_cast<int32>(SimState.Wheels.size()));
	for (int32 i = 0; i < WheelCount; ++i)
	{
		const KomorebiCore::VehicleWheel& W = SimState.Wheels[i];
		FKomorebiWheelRig& Rig = WheelRigs[i];
		const double ContactHeight = (SimState.Air <= 0.0 && !SimState.Tipped && UpY > 0.1)
			? (W.Ground + RollingRadius - RootThreeY - Rig.LocalThreeX * UpX - Rig.LocalThreeZ * UpZ) / UpY
			: (Spec.Wheel - Spec.Travel);
		const double ClampedY = KomorebiCore::Clamp(ContactHeight, Spec.Wheel - Spec.Travel, Spec.Wheel + Spec.Travel);

		if (Rig.SteerPivot)
		{
			Rig.SteerPivot->SetRelativeLocation(FVector(
				static_cast<float>(Rig.LocalThreeZ * 100.0),
				static_cast<float>(-Rig.LocalThreeX * 100.0),
				static_cast<float>(ClampedY * 100.0)));
			if (Rig.bFront)
			{
				Rig.SteerPivot->SetRelativeRotation(FRotator(0.0f, SteerYawDeg, 0.0f));
			}
		}
		if (Rig.SpinPivot)
		{
			Rig.SpinPivot->SetRelativeRotation(FRotator(SpinPitchDeg, 0.0f, 0.0f));
		}
	}

	if (RiderMesh && Spec.Lean)
	{
		RiderMesh->SetVisibility(bPlayerDriven || bHasTrafficDriver || bRemoteNetDriven);
	}

	// Smoke (< 66% hull), Fire (< 34% hull or explosion flash), Wrecked (0% hull).
	const bool bShowSmoke = HullHp < static_cast<float>(Spec.Hull) * 0.66f;
	const bool bShowFire = HullHp < static_cast<float>(Spec.Hull) * 0.34f || BoomTimer > 0.0f;
	if (SmokeVfxMesh)
	{
		SmokeVfxMesh->SetVisibility(bShowSmoke);
	}
	if (FireVfxMesh)
	{
		FireVfxMesh->SetVisibility(bShowFire);
	}
	if (bShowSmoke || bShowFire)
	{
		const float PhaseInput = (BoomTimer > 0.0f ? BoomTimer : HullHp) * 18.0f + (bShowSmoke ? 1.0f : 0.0f);
		const float Bob = 1.0f + FMath::Sin(PhaseInput) * 0.15f;
		if (SmokeVfxMesh)
		{
			SmokeVfxMesh->SetRelativeLocation(FVector(static_cast<float>(-Spec.Len * 35.0), 0.0f, (1.3f + Bob * 0.35f) * 100.0f));
		}
		if (FireVfxMesh)
		{
			FireVfxMesh->SetRelativeLocation(FVector(static_cast<float>(-Spec.Len * 15.0), 0.0f, (1.05f + Bob * 0.2f) * 100.0f));
			if (BoomTimer > 0.0f)
			{
				BoomTimer = FMath::Max(0.0f, BoomTimer - DeltaTime);
				FireVfxMesh->SetRelativeScale3D(FVector(0.68f * (1.0f + (0.45f - BoomTimer) * 6.0f)));
			}
			else
			{
				FireVfxMesh->SetRelativeScale3D(FVector(0.68f));
			}
		}
	}
}

void AKomorebiVehiclePawn::UpdateRemoteNetInterpolation(float DeltaTime)
{
	const UWorld* World = GetWorld();
	const double Age = World ? FMath::Clamp(static_cast<double>(World->GetTimeSeconds() - LastNetStateReceivedTime), 0.0, 0.5) : 0.0;
	const double K = std::min(1.0, static_cast<double>(DeltaTime) * 8.0);

	const double TargetX = static_cast<double>(ReplicatedNetState.ThreeX) + static_cast<double>(ReplicatedNetState.Vx) * Age;
	const double TargetZ = static_cast<double>(ReplicatedNetState.ThreeZ) + static_cast<double>(ReplicatedNetState.Vz) * Age;

	SimState.X += (TargetX - SimState.X) * K;
	SimState.Z += (TargetZ - SimState.Z) * K;
	SimState.Yaw = KomorebiCore::WrapAngle(SimState.Yaw + KomorebiCore::WrapAngle(static_cast<double>(ReplicatedNetState.ThreeYaw) - SimState.Yaw) * K);
	SimState.Vx = static_cast<double>(ReplicatedNetState.Vx);
	SimState.Vz = static_cast<double>(ReplicatedNetState.Vz);
	SimState.Vf = SimState.Vx * std::sin(SimState.Yaw) + SimState.Vz * std::cos(SimState.Yaw);

	KomorebiCore::SettleSuspension(SimState, GetVehicleSpec(), [this](double X, double Z) { return SampleHeightAt(X, Z); });
}

void AKomorebiVehiclePawn::PushNetworkState(float DeltaTime)
{
	if (!bPlayerDriven && !HasAuthority())
	{
		return;
	}

	NetSendAccum += DeltaTime;
	if (NetSendAccum < static_cast<float>(KomorebiCore::kOnlineSendInterval))
	{
		return;
	}
	NetSendAccum = 0.0f;

	FKomorebiVehicleNetState Snapshot;
	Snapshot.ThreeX = static_cast<float>(SimState.X);
	Snapshot.ThreeY = static_cast<float>(SimState.Y);
	Snapshot.ThreeZ = static_cast<float>(SimState.Z);
	Snapshot.ThreeYaw = static_cast<float>(SimState.Yaw);
	Snapshot.Vx = static_cast<float>(SimState.Vx);
	Snapshot.Vz = static_cast<float>(SimState.Vz);
	Snapshot.Steer = static_cast<float>(SimState.Steer);
	Snapshot.HullHp = HullHp;
	Snapshot.bWrecked = bWrecked;
	Snapshot.bTipped = SimState.Tipped;
	Snapshot.bOccupied = bPlayerDriven || bHasTrafficDriver;

	if (HasAuthority())
	{
		ReplicatedNetState = Snapshot;
	}
	else if (bPlayerDriven)
	{
		ServerSendNetState(Snapshot);
	}
}

// --- Telemetry & Audio Helper ------------------------------------------------------------------

EKomorebiVehicleDamageState AKomorebiVehiclePawn::GetDamageState() const
{
	if (bWrecked || HullHp <= 0.0f)
	{
		return EKomorebiVehicleDamageState::Wrecked;
	}
	const float MaxHull = static_cast<float>(GetVehicleSpec().Hull);
	if (HullHp < MaxHull * 0.34f)
	{
		return EKomorebiVehicleDamageState::Burning;
	}
	if (HullHp < MaxHull * 0.66f)
	{
		return EKomorebiVehicleDamageState::Smoking;
	}
	return EKomorebiVehicleDamageState::Healthy;
}

FKomorebiVehicleTelemetry AKomorebiVehiclePawn::GetTelemetry() const
{
	const KomorebiCore::VehicleSpec& Spec = GetVehicleSpec();
	const float SpeedMps = static_cast<float>(std::hypot(SimState.Vx, SimState.Vz));

	FKomorebiVehicleTelemetry Out;
	Out.bDriving = bPlayerDriven;
	Out.Kind = VehicleKind;
	Out.DisplayName = KomorebiBridge::ToFString(Spec.Name);
	Out.SpeedMps = SpeedMps;
	Out.SpeedKmh = SpeedMps * 3.6f;
	Out.TopSpeedMps = static_cast<float>(Spec.Top);
	Out.Throttle = CurrentInputThrottle;
	Out.bSliding = SimState.Sliding;
	Out.bAirborne = SimState.Air > 0.15;
	Out.bTipped = SimState.Tipped;
	Out.bHasTrafficDriver = bHasTrafficDriver;
	Out.DamageState = GetDamageState();
	Out.HullHp = HullHp;
	Out.MaxHullHp = static_cast<float>(Spec.Hull);
	return Out;
}

void AKomorebiVehiclePawn::PlayAudioSfx(EKomorebiSfx Sfx, float Intensity) const
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
