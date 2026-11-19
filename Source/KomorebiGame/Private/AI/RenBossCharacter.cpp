// Copyright Komorebi Project. Mirror Rival boss character Ren driving all 6 duels via KomorebiCore::DuelState.
#include "AI/RenBossCharacter.h"

#include "AI/EnemySquadSubsystem.h"
#include "Character/KaelAnimInstance.h"
#include "Character/KaelCharacter.h"
#include "Combat/BladeTrailComponent.h"
#include "Combat/CombatComponent.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Game/CombatRules.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "World/CoordBridge.h"
#include "World/NeoKyotoTerrain.h"

namespace
{
	std::string_view FNameToDuelSource(FName Source)
	{
		if (Source == TEXT("heavy")) return "heavy";
		if (Source == TEXT("whirl")) return "whirl";
		if (Source == TEXT("flare")) return "flare";
		if (Source == TEXT("thrust")) return "thrust";
		if (Source == TEXT("spark")) return "spark";
		return "sword";
	}
}

ARenBossCharacter::ARenBossCharacter()
{
	PrimaryActorTick.bCanEverTick = false; // Driven centrally by UEnemySquadSubsystem
	bReplicates = true;
	SetReplicateMovement(true);

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->InitCapsuleSize(30.0f, 90.0f);
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision); // Enabled when duel becomes ready
	}
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->GravityScale = 0.0f;
	}

	RivalKatanaMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RivalKatanaMesh"));
	RivalKatanaMesh->SetupAttachment(GetMesh(), KatanaSocketName);
	RivalKatanaMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	BladeTrailComponent = CreateDefaultSubobject<UBladeTrailComponent>(TEXT("BladeTrailComponent"));
	BladeTrailComponent->SetupAttachment(RootComponent);
	BladeTrailComponent->SetTheme(EBladeTrailTheme::Crimson);

	auto SetupWorldProcMesh = [this](const TCHAR* Name) -> UProceduralMeshComponent*
	{
		UProceduralMeshComponent* Comp = CreateDefaultSubobject<UProceduralMeshComponent>(Name);
		Comp->SetupAttachment(RootComponent);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCastShadow(false);
		Comp->SetUsingAbsoluteLocation(true);
		Comp->SetUsingAbsoluteRotation(true);
		Comp->SetUsingAbsoluteScale(true);
		Comp->SetVisibility(false);
		return Comp;
	};

	ArenaRingMesh = SetupWorldProcMesh(TEXT("ArenaRingMesh"));
	TelegraphSectorMesh = SetupWorldProcMesh(TEXT("TelegraphSectorMesh"));
	AuraRingMesh = SetupWorldProcMesh(TEXT("AuraRingMesh"));
	BeaconMesh = SetupWorldProcMesh(TEXT("BeaconMesh"));
}

void ARenBossCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (!bInitialized)
	{
		InitializeDuel(DuelIndex);
	}

	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		for (int32 i = 0; i < SkelMesh->GetNumMaterials(); ++i)
		{
			if (UMaterialInstanceDynamic* MID = SkelMesh->CreateAndSetMaterialInstanceDynamic(i))
			{
				DynamicMaterials.Add(MID);
			}
		}
	}

	if (UWorld* World = GetWorld())
	{
		if (UEnemySquadSubsystem* Squad = World->GetSubsystem<UEnemySquadSubsystem>())
		{
			Squad->RegisterRival(this);
		}
	}
}

void ARenBossCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UEnemySquadSubsystem* Squad = World->GetSubsystem<UEnemySquadSubsystem>())
		{
			Squad->UnregisterRival(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ARenBossCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ARenBossCharacter, DuelIndex);
	DOREPLIFETIME(ARenBossCharacter, ReplicatedHp);
	DOREPLIFETIME(ARenBossCharacter, ReplicatedMaxHp);
	DOREPLIFETIME(ARenBossCharacter, bIsDead);
	DOREPLIFETIME(ARenBossCharacter, ReplicatedPhase);
}

void ARenBossCharacter::InitializeDuel(int32 InDuelIndex)
{
	DuelIndex = FMath::Clamp(InDuelIndex, 0, 5);
	CoreState = KomorebiCore::CreateDuel(DuelIndex);
	bInitialized = true;
	bDeadBroadcasted = false;
	DuelSpinAccum = static_cast<float>(DuelIndex) * 1.1f;
	FlashTimer = 0.0f;
	DeadElapsed = 0.0f;

	ReplicatedHp = static_cast<float>(CoreState.Hp);
	ReplicatedMaxHp = static_cast<float>(CoreState.MaxHp);
	bIsDead = false;
	ReplicatedPhase = ERenDuelPhase::Waiting;

	if (BladeTrailComponent)
	{
		BladeTrailComponent->SetTheme(EBladeTrailTheme::Crimson);
		BladeTrailComponent->ClearTrail();
	}

	SyncActorTransformFromCoreState(0.0f);
	if (!bArenaBuilt)
	{
		BuildArenaRingMesh();
	}
	if (!bBeaconBuilt)
	{
		BuildBeaconMesh();
	}
}

void ARenBossCharacter::ResetDuel(bool bReviveIfDead)
{
	if (CoreState.Dead && !bReviveIfDead)
	{
		return;
	}
	CoreState = KomorebiCore::CreateDuel(DuelIndex);
	bDeadBroadcasted = false;
	FlashTimer = 0.0f;
	DeadElapsed = 0.0f;
	RespawnTimer = 0.0f;
	ReplicatedHp = static_cast<float>(CoreState.Hp);
	ReplicatedMaxHp = static_cast<float>(CoreState.MaxHp);
	bIsDead = false;
	ReplicatedPhase = ERenDuelPhase::Waiting;
	if (BladeTrailComponent)
	{
		BladeTrailComponent->ClearTrail();
	}
	SyncActorTransformFromCoreState(0.0f);
}

FString ARenBossCharacter::GetBossName() const
{
	return KomorebiBridge::ToFString(CoreState.Name);
}

FString ARenBossCharacter::GetBossLesson() const
{
	return KomorebiBridge::ToFString(CoreState.Lesson);
}

FString ARenBossCharacter::GetCurrentMoveCue() const
{
	const KomorebiCore::DuelMoveSpec& Spec = KomorebiCore::GetDuelMoveSpec(CoreState.Move);
	return KomorebiBridge::ToFString(Spec.Cue);
}

float ARenBossCharacter::GetCurrentMoveReachMeters() const
{
	return static_cast<float>(KomorebiCore::GetDuelMoveSpec(CoreState.Move).Reach);
}

FVector ARenBossCharacter::GetFeetWorldLocation() const
{
	const float HalfH = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0f;
	return GetActorLocation() - FVector(0.0f, 0.0f, HalfH);
}

float ARenBossCharacter::QueryGroundHeightCm(float WorldXCm, float WorldYCm, float FallbackZCm) const
{
	if (const UWorld* World = GetWorld())
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RenGroundCheck), false, this);
		const FVector Start(WorldXCm, WorldYCm, FallbackZCm + 2500.0f);
		const FVector End(WorldXCm, WorldYCm, FallbackZCm - 2500.0f);
		if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
		{
			return Hit.ImpactPoint.Z;
		}
	}
	const KomorebiCore::Vec3 Three = KomorebiBridge::FVectorToThree(FVector(WorldXCm, WorldYCm, FallbackZCm));
	const double WayD = KomorebiCore::WayDist(Three.X, Three.Z);
	return static_cast<float>(KomorebiCore::NeoKyotoRawHeight(Three.X, Three.Z, WayD) * 100.0);
}

void ARenBossCharacter::SyncActorTransformFromCoreState(float SpiralExtraYawDeg)
{
	const FVector HorizontalUE = KomorebiBridge::ThreeXZToFVector(CoreState.X, 0.0, CoreState.Z);
	const float GroundZ = QueryGroundHeightCm(HorizontalUE.X, HorizontalUE.Y, GetActorLocation().Z);
	const float HalfH = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0f;
	const float UEYawDeg = static_cast<float>(KomorebiCore::ThreeYawToUEYawDeg(CoreState.Yaw)) + SpiralExtraYawDeg;

	SetActorLocationAndRotation(
		FVector(HorizontalUE.X, HorizontalUE.Y, GroundZ + HalfH),
		FRotator(0.0f, FMath::UnwindDegrees(UEYawDeg), 0.0f));
}

int32 ARenBossCharacter::ReceivePlayerDamage(float RawAmount, FName Source)
{
	if (CoreState.Dead
		|| !CoreState.Ready
		|| CoreState.Phase == KomorebiCore::EDuelPhase::Waiting
		|| CoreState.Phase == KomorebiCore::EDuelPhase::Intro)
	{
		return 0;
	}

	const bool bWasGuarded = (CoreState.Phase != KomorebiCore::EDuelPhase::Recovery);
	const int32 FinalDamage = KomorebiCore::DamageRival(CoreState, static_cast<double>(RawAmount), FNameToDuelSource(Source));

	UEnemySquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<UEnemySquadSubsystem>() : nullptr;
	if (bWasGuarded && CoreState.Phase == KomorebiCore::EDuelPhase::Recovery && Squad)
	{
		Squad->EmitToast(TEXT("Guard broken — strike now!"));
	}

	CoreState.Hp = FMath::Max(0.0, CoreState.Hp - static_cast<double>(FinalDamage));
	ReplicatedHp = static_cast<float>(CoreState.Hp);
	ReplicatedPhase = static_cast<ERenDuelPhase>(CoreState.Phase);
	FlashTimer = 0.16f;

	if (CoreState.Hp <= 0.0 && !CoreState.Dead)
	{
		CoreState.Dead = true;
		bIsDead = true;
		DeadElapsed = 0.001f;

		if (UCapsuleComponent* Capsule = GetCapsuleComponent())
		{
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		if (ArenaRingMesh) ArenaRingMesh->SetVisibility(false);
		if (TelegraphSectorMesh) TelegraphSectorMesh->SetVisibility(false);
		if (AuraRingMesh) AuraRingMesh->SetVisibility(false);
		if (BeaconMesh) BeaconMesh->SetVisibility(false);

		if (!bDeadBroadcasted)
		{
			bDeadBroadcasted = true;
			const FString BossName = GetBossName();
			const FString Lesson = GetBossLesson();
			if (Squad)
			{
				Squad->EmitToast(FString::Printf(TEXT("%s — lesson mastered"), *BossName));
			}
			OnDuelCompleted.Broadcast(DuelIndex, BossName, Lesson);
		}
	}

	return FinalDamage;
}

void ARenBossCharacter::TickDuel(float DeltaTime, AKaelCharacter* PlayerCharacter, bool bInReady, bool bGateBlocked, bool bFreeRoam)
{
	FlashTimer = FMath::Max(0.0f, FlashTimer - DeltaTime);

	if (CoreState.Dead)
	{
		DeadElapsed += DeltaTime;
		if (DeadElapsed > 2.2f)
		{
			SetActorHiddenInGame(true);
			if (BladeTrailComponent)
			{
				BladeTrailComponent->SetVisibility(false);
			}
			if (bFreeRoam)
			{
				if (RespawnTimer <= 0.0f)
				{
					RespawnTimer = UEnemySquadSubsystem::kCrewRespawnSeconds;
				}
				RespawnTimer -= DeltaTime;
				if (RespawnTimer <= 0.0f)
				{
					ResetDuel(true);
					SetActorHiddenInGame(false);
				}
			}
		}
		else
		{
			UpdateVisualsAndBlade(DeltaTime, PlayerCharacter);
		}
		return;
	}

	// Enable/disable duel capsule based on readiness (hunt.js lines 1210-1211)
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		const ECollisionEnabled::Type WantCol = bInReady ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision;
		if (Capsule->GetCollisionEnabled() != WantCol)
		{
			Capsule->SetCollisionEnabled(WantCol);
		}
	}

	if (!PlayerCharacter)
	{
		return;
	}

	const FVector PlayerFeet = PlayerCharacter->GetFeetWorldLocation();
	const KomorebiCore::Vec3 PlayerThree = KomorebiBridge::FVectorToThree(PlayerFeet);
	const double PlayerThreeYaw = KomorebiCore::UEYawDegToThreeYaw(PlayerCharacter->GetActorRotation().Yaw);
	UCombatComponent* PlayerCombat = PlayerCharacter->GetCombatComponent();

	// Threatening check for Iron Will / Twin Current reactive Guard or Sidestep (hunt.js line 1212)
	const bool bThreatening = PlayerCombat
		&& PlayerCombat->IsInAttackWindup()
		&& KomorebiCore::InFront(PlayerThree.X, PlayerThree.Z, PlayerThreeYaw, CoreState.X, CoreState.Z, 3.45, 1.05);

	KomorebiCore::DuelStepInput Input;
	Input.X = PlayerThree.X;
	Input.Z = PlayerThree.Z;
	Input.Ready = bInReady;
	Input.Threatening = bThreatening;
	Input.MoveFn = [this](double DxThree, double DzThree)
	{
		const double DistM = std::hypot(DxThree, DzThree);
		if (DistM < 1e-6)
		{
			return;
		}
		double AllowedFraction = 1.0;
		if (UWorld* World = GetWorld())
		{
			const FVector StartUE = KomorebiBridge::ThreeXZToFVector(CoreState.X, 0.0, CoreState.Z);
			const float GroundZ = QueryGroundHeightCm(StartUE.X, StartUE.Y, GetActorLocation().Z);
			const FVector RayOrigin(StartUE.X, StartUE.Y, GroundZ + 90.0f);
			const FVector DirUE = KomorebiBridge::ThreeDirToFVector(KomorebiCore::Vec3{ DxThree / DistM, 0.0, DzThree / DistM }).GetSafeNormal();
			const float DistCm = static_cast<float>(DistM * 100.0);

			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(RenDuelMoveSweep), false, this);
			if (World->LineTraceSingleByChannel(Hit, RayOrigin, RayOrigin + DirUE * (DistCm + 50.0f), ECC_WorldStatic, Params))
			{
				const float AllowedCm = FMath::Max(0.0f, Hit.Distance - 50.0f);
				AllowedFraction = FMath::Min(1.0, static_cast<double>(AllowedCm) / FMath::Max(1.0, static_cast<double>(DistCm)));
			}
		}
		CoreState.X += DxThree * AllowedFraction;
		CoreState.Z += DzThree * AllowedFraction;
	};

	const KomorebiCore::EDuelEvent Event = KomorebiCore::StepDuel(CoreState, static_cast<double>(DeltaTime), Input);
	ReplicatedHp = static_cast<float>(CoreState.Hp);
	ReplicatedPhase = static_cast<ERenDuelPhase>(CoreState.Phase);

	UEnemySquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<UEnemySquadSubsystem>() : nullptr;
	if (Event != KomorebiCore::EDuelEvent::None)
	{
		const FString BossName = GetBossName();
		const FString Lesson = GetBossLesson();

		if (Event == KomorebiCore::EDuelEvent::Intro)
		{
			if (PlayerCombat)
			{
				PlayerCombat->RefillFullResources();
				const FVector Feet = GetFeetWorldLocation();
				PlayerCombat->OnPulseVFX.Broadcast(FVector(Feet.X, Feet.Y, QueryGroundHeightCm(Feet.X, Feet.Y, Feet.Z) + 5.0f), FLinearColor(0.659f, 0.937f, 0.863f, 1.0f), true);
			}
			if (Squad)
			{
				Squad->EmitToast(FString::Printf(TEXT("%s · %s"), *BossName, *Lesson));
			}
		}
		else if (Event == KomorebiCore::EDuelEvent::Reset && Squad)
		{
			Squad->EmitToast(TEXT("Duel reset. Approach the beacon when you are ready."));
		}
		else if (Event == KomorebiCore::EDuelEvent::Opening && Squad)
		{
			Squad->EmitToast(TEXT("Opening — strike now!"));
		}

		OnDuelEvent.Broadcast(DuelIndex, static_cast<ERenDuelEvent>(Event), BossName, Lesson);
	}

	// Active hit window resolution via KomorebiCore::DuelHit() (hunt.js lines 1227-1229)
	if (!bGateBlocked && PlayerCombat)
	{
		const FVector RenUE = KomorebiBridge::ThreeXZToFVector(CoreState.X, 0.0, CoreState.Z);
		const double RenGroundThreeY = static_cast<double>(QueryGroundHeightCm(RenUE.X, RenUE.Y, GetActorLocation().Z)) * 0.01;
		if (KomorebiCore::DuelHit(CoreState, PlayerThree.X, PlayerThree.Y, PlayerThree.Z, RenGroundThreeY))
		{
			const KomorebiCore::DuelMoveSpec& MoveSpec = KomorebiCore::GetDuelMoveSpec(CoreState.Move);
			const float ShoveMps = (CoreState.Move == KomorebiCore::EDuelMove::Slam) ? 8.0f : 5.5f;
			if (PlayerCombat->ReceiveEnemyHit(static_cast<float>(MoveSpec.Damage), this, ShoveMps, MoveSpec.Unblockable))
			{
				CoreState.HitDone = true;
				CoreState.Connected = true;
			}
		}
	}

	UpdateVisualsAndBlade(DeltaTime, PlayerCharacter);
}

void ARenBossCharacter::BuildArenaRingMesh()
{
	if (!ArenaRingMesh)
	{
		return;
	}

	constexpr int32 Segments = 72;
	const FVector HomeUE = KomorebiBridge::ThreeXZToFVector(CoreState.HomeX, 0.0, CoreState.HomeZ);
	const float RefZ = QueryGroundHeightCm(HomeUE.X, HomeUE.Y, 0.0f);

	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;

	Verts.Reserve((Segments + 1) * 2);
	Normals.Reserve((Segments + 1) * 2);
	UVs.Reserve((Segments + 1) * 2);
	Colors.Reserve((Segments + 1) * 2);
	Tris.Reserve(Segments * 6);

	const FLinearColor DefaultCol(1.0f, 0.165f, 0.427f, 0.34f);
	for (int32 i = 0; i <= Segments; ++i)
	{
		const float U = static_cast<float>(i) / static_cast<float>(Segments);
		const float Ang = U * TWO_PI;
		const float C = FMath::Cos(Ang);
		const float S = FMath::Sin(Ang);

		const float InX = HomeUE.X + C * kArenaInnerRadiusCm;
		const float InY = HomeUE.Y + S * kArenaInnerRadiusCm;
		const float OutX = HomeUE.X + C * kArenaOuterRadiusCm;
		const float OutY = HomeUE.Y + S * kArenaOuterRadiusCm;

		Verts.Add(FVector(InX, InY, QueryGroundHeightCm(InX, InY, RefZ) + 5.0f));
		Verts.Add(FVector(OutX, OutY, QueryGroundHeightCm(OutX, OutY, RefZ) + 5.0f));
		Normals.Add(FVector::UpVector);
		Normals.Add(FVector::UpVector);
		UVs.Add(FVector2D(U, 0.0f));
		UVs.Add(FVector2D(U, 1.0f));
		Colors.Add(DefaultCol);
		Colors.Add(DefaultCol);
	}

	for (int32 i = 0; i < Segments; ++i)
	{
		const int32 I0 = i * 2;
		Tris.Add(I0); Tris.Add(I0 + 2); Tris.Add(I0 + 1);
		Tris.Add(I0 + 1); Tris.Add(I0 + 2); Tris.Add(I0 + 3);
	}

	ArenaRingMesh->SetWorldLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	ArenaRingMesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, Colors, Tangents, false);
	bArenaBuilt = true;
}

void ARenBossCharacter::BuildBeaconMesh()
{
	if (!BeaconMesh)
	{
		return;
	}

	constexpr int32 Sides = 10;
	constexpr float TopRadiusCm = 12.0f;
	constexpr float BottomRadiusCm = 22.0f;
	constexpr float HeightCm = 2200.0f; // 22m tall beacon column (hunt.js line 333)

	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;

	const FLinearColor Col(1.0f, 0.165f, 0.427f, 0.18f);
	for (int32 i = 0; i <= Sides; ++i)
	{
		const float U = static_cast<float>(i) / static_cast<float>(Sides);
		const float Ang = U * TWO_PI;
		const FVector Dir(FMath::Cos(Ang), FMath::Sin(Ang), 0.0f);

		Verts.Add(FVector(Dir.X * BottomRadiusCm, Dir.Y * BottomRadiusCm, 0.0f));
		Verts.Add(FVector(Dir.X * TopRadiusCm, Dir.Y * TopRadiusCm, HeightCm));
		Normals.Add(Dir);
		Normals.Add(Dir);
		UVs.Add(FVector2D(U, 0.0f));
		UVs.Add(FVector2D(U, 1.0f));
		Colors.Add(Col);
		Colors.Add(FLinearColor(Col.R, Col.G, Col.B, 0.0f));
	}

	for (int32 i = 0; i < Sides; ++i)
	{
		const int32 I0 = i * 2;
		Tris.Add(I0); Tris.Add(I0 + 2); Tris.Add(I0 + 1);
		Tris.Add(I0 + 1); Tris.Add(I0 + 2); Tris.Add(I0 + 3);
	}

	BeaconMesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, Colors, Tangents, false);
	bBeaconBuilt = true;
}

void ARenBossCharacter::UpdateTelegraphSectorMesh(const KomorebiCore::DuelVisualState& Vis, float GroundZ, float Alpha)
{
	if (!TelegraphSectorMesh)
	{
		return;
	}

	constexpr int32 Segs = 48;
	const float OuterR = static_cast<float>(Vis.Reach * 100.0);
	const float InnerR = OuterR * 0.72f;
	const bool bOpening = (Vis.Mode == KomorebiCore::EDuelVisualMode::Opening);
	const float HalfArc = bOpening ? PI : static_cast<float>(FMath::Clamp(Vis.Arc, 0.1, KomorebiCore::kPi));

	const FLinearColor BaseCol = KomorebiBridge::ToLinearColor(Vis.Color, Alpha);
	const FLinearColor AccentCol = KomorebiBridge::ToLinearColor(Vis.Accent, Alpha);

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
	Tris.Reserve(Segs * 6);

	for (int32 i = 0; i <= Segs; ++i)
	{
		const float U = static_cast<float>(i) / static_cast<float>(Segs);
		const float Ang = FMath::Lerp(-HalfArc, HalfArc, U);
		const float C = FMath::Cos(Ang);
		const float S = FMath::Sin(Ang);

		Verts.Add(FVector(C * InnerR, S * InnerR, 0.0f));
		Verts.Add(FVector(C * OuterR, S * OuterR, 0.0f));
		Normals.Add(FVector::UpVector);
		Normals.Add(FVector::UpVector);
		UVs.Add(FVector2D(U, 0.0f));
		UVs.Add(FVector2D(U, 1.0f));
		Colors.Add(BaseCol);
		Colors.Add(AccentCol);
	}

	for (int32 i = 0; i < Segs; ++i)
	{
		const int32 I0 = i * 2;
		Tris.Add(I0); Tris.Add(I0 + 2); Tris.Add(I0 + 1);
		Tris.Add(I0 + 1); Tris.Add(I0 + 2); Tris.Add(I0 + 3);
	}

	const FVector RenUE = KomorebiBridge::ThreeXZToFVector(CoreState.X, 0.0, CoreState.Z);
	const float AttackUEYaw = static_cast<float>(KomorebiCore::ThreeYawToUEYawDeg(CoreState.AttackYaw));
	TelegraphSectorMesh->SetWorldLocationAndRotation(
		FVector(RenUE.X, RenUE.Y, GroundZ + 4.5f),
		FRotator(0.0f, AttackUEYaw, 0.0f));
	TelegraphSectorMesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, Colors, Tangents, false);
}

void ARenBossCharacter::UpdateAuraRingMesh(const KomorebiCore::DuelVisualState& Vis, float RadiusCm, float GroundZ, float Alpha)
{
	if (!AuraRingMesh)
	{
		return;
	}

	constexpr int32 Segs = 36;
	const float OuterR = RadiusCm;
	const float InnerR = RadiusCm * 0.78f;
	const FLinearColor InnerCol = (FlashTimer > 0.0f)
		? FLinearColor(1.0f, 1.0f, 1.0f, Alpha)
		: KomorebiBridge::ToLinearColor(Vis.Color, Alpha);
	const FLinearColor OuterCol = KomorebiBridge::ToLinearColor(Vis.Accent, Alpha);

	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;

	for (int32 i = 0; i <= Segs; ++i)
	{
		const float U = static_cast<float>(i) / static_cast<float>(Segs);
		const float Ang = U * TWO_PI;
		const float C = FMath::Cos(Ang);
		const float S = FMath::Sin(Ang);
		Verts.Add(FVector(C * InnerR, S * InnerR, 0.0f));
		Verts.Add(FVector(C * OuterR, S * OuterR, 0.0f));
		Normals.Add(FVector::UpVector);
		Normals.Add(FVector::UpVector);
		UVs.Add(FVector2D(U, 0.0f));
		UVs.Add(FVector2D(U, 1.0f));
		Colors.Add(InnerCol);
		Colors.Add(OuterCol);
	}
	for (int32 i = 0; i < Segs; ++i)
	{
		const int32 I0 = i * 2;
		Tris.Add(I0); Tris.Add(I0 + 2); Tris.Add(I0 + 1);
		Tris.Add(I0 + 1); Tris.Add(I0 + 2); Tris.Add(I0 + 3);
	}

	const FVector RenUE = KomorebiBridge::ThreeXZToFVector(CoreState.X, 0.0, CoreState.Z);
	AuraRingMesh->SetWorldLocationAndRotation(FVector(RenUE.X, RenUE.Y, GroundZ + 5.8f), FRotator(0.0f, FMath::RadiansToDegrees(DuelSpinAccum), 0.0f));
	AuraRingMesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, Colors, Tangents, false);
}

void ARenBossCharacter::UpdateVisualsAndBlade(float DeltaTime, AKaelCharacter* PlayerCharacter)
{
	const KomorebiCore::DuelVisualState Vis = KomorebiCore::GetDuelVisual(CoreState);
	if (DeltaTime > 0.0f)
	{
		DuelSpinAccum += DeltaTime * static_cast<float>(Vis.Spin);
	}

	const FVector RenUE = KomorebiBridge::ThreeXZToFVector(CoreState.X, 0.0, CoreState.Z);
	const float GroundZ = QueryGroundHeightCm(RenUE.X, RenUE.Y, GetActorLocation().Z);
	const float PlayerDistM = PlayerCharacter
		? (FVector::Dist2D(RenUE, PlayerCharacter->GetFeetWorldLocation()) * 0.01f)
		: 0.0f;

	// Beacon visibility (hunt.js line 734)
	if (BeaconMesh)
	{
		const bool bShowBeacon = !CoreState.Dead && CoreState.Ready && (!Vis.Engaged || PlayerDistM > 16.0f);
		BeaconMesh->SetVisibility(bShowBeacon);
		if (bShowBeacon)
		{
			BeaconMesh->SetWorldLocation(FVector(RenUE.X, RenUE.Y, GroundZ));
		}
	}

	const bool bShowRen = (CoreState.Ready || CoreState.Dead) && PlayerDistM < 85.0f;
	SetActorHiddenInGame(!bShowRen);
	if (BladeTrailComponent)
	{
		BladeTrailComponent->SetVisibility(bShowRen);
	}

	if (!bShowRen)
	{
		if (ArenaRingMesh) ArenaRingMesh->SetVisibility(false);
		if (TelegraphSectorMesh) TelegraphSectorMesh->SetVisibility(false);
		if (AuraRingMesh) AuraRingMesh->SetVisibility(false);
		return;
	}

	// Spiral 360 rotation during active Ember Spiral move (hunt.js line 750)
	float SpiralExtraYawDeg = 0.0f;
	if (CoreState.Move == KomorebiCore::EDuelMove::Spiral && CoreState.Phase == KomorebiCore::EDuelPhase::Active)
	{
		const double SpiralActive = KomorebiCore::GetDuelMoveSpec(KomorebiCore::EDuelMove::Spiral).Active;
		const float Progress = static_cast<float>(1.0 - CoreState.Timer / FMath::Max(0.01, SpiralActive));
		SpiralExtraYawDeg = Progress * 360.0f;
	}
	SyncActorTransformFromCoreState(SpiralExtraYawDeg);

	// Flash material parameter
	const float FlashAlpha = FlashTimer > 0.0f ? FMath::Clamp(FlashTimer / 0.16f, 0.0f, 1.0f) : 0.0f;
	for (UMaterialInstanceDynamic* MID : DynamicMaterials)
	{
		if (MID)
		{
			MID->SetScalarParameterValue(TEXT("FlashAmount"), FlashAlpha);
		}
	}

	// Drive UKaelAnimInstance & Crimson UBladeTrailComponent
	const KomorebiCore::DuelMoveSpec& MoveSpec = KomorebiCore::GetDuelMoveSpec(CoreState.Move);
	const KomorebiCore::EAttackType CorePose = (CoreState.Beat != 0 && MoveSpec.SecondPose.has_value())
		? *MoveSpec.SecondPose
		: MoveSpec.Pose;
	const EKomorebiAttackType UEPose = static_cast<EKomorebiAttackType>(CorePose);
	const float AttackU = CoreState.Dead ? -1.0f : static_cast<float>(CoreState.AttackU);

	UKaelAnimInstance* RenAnim = GetMesh() ? Cast<UKaelAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	if (RenAnim)
	{
		RenAnim->SetExternalLocomotion(static_cast<float>(CoreState.Going * 100.0), 0.0f, true, false, false, CoreState.Dead);
		RenAnim->UpdateCombatPose(
			DeltaTime,
			AttackU,
			UEPose,
			FlashTimer * 3.0f,
			0.0f,
			!CoreState.Dead && CoreState.Phase == KomorebiCore::EDuelPhase::Guard,
			FlashTimer * 4.0f,
			CoreState.Phase == KomorebiCore::EDuelPhase::Sidestep ? FMath::Min(0.28f, static_cast<float>(CoreState.Timer)) : 0.0f,
			static_cast<float>(CoreState.ReactSide) * HALF_PI,
			false);
	}

	if (BladeTrailComponent)
	{
		const KomorebiCore::AttackSpec& PoseSpec = KomorebiCore::GetAttackSpec(CorePose);
		const bool bTrailActive = !CoreState.Dead
			&& AttackU >= static_cast<float>(PoseSpec.HitStart)
			&& AttackU <= static_cast<float>(PoseSpec.HitEnd);

		BladeTrailComponent->SetTheme(EBladeTrailTheme::Crimson);

		FVector WorldGrip = GetActorLocation() + FVector(0.0f, 0.0f, 10.0f);
		FVector WorldBase = WorldGrip + GetActorForwardVector() * UBladeTrailComponent::kBladeBaseOffsetCm;
		FVector WorldTip = WorldGrip + GetActorForwardVector() * UBladeTrailComponent::kBladeTipOffsetCm;

		if (RivalKatanaMesh && RivalKatanaMesh->GetAttachParent())
		{
			const FTransform SwordXform = RivalKatanaMesh->GetComponentTransform();
			WorldGrip = SwordXform.GetLocation();
			WorldBase = SwordXform.TransformPosition(FVector(0.0f, 0.0f, UBladeTrailComponent::kBladeBaseOffsetCm));
			WorldTip = SwordXform.TransformPosition(FVector(4.2f, 0.0f, UBladeTrailComponent::kBladeTipOffsetCm));
		}
		else if (RenAnim)
		{
			const FTransform ActorXform = GetActorTransform();
			WorldGrip = ActorXform.TransformPosition(RenAnim->SwordGripLocal - FVector(0.0f, 0.0f, 90.0f));
			const FVector BladeWorldDir = ActorXform.TransformVectorNoScale(RenAnim->SwordBladeDir).GetSafeNormal();
			WorldBase = WorldGrip + BladeWorldDir * UBladeTrailComponent::kBladeBaseOffsetCm;
			WorldTip = WorldGrip + BladeWorldDir * UBladeTrailComponent::kBladeTipOffsetCm;
		}

		BladeTrailComponent->SampleBlade(
			DeltaTime,
			bTrailActive,
			AttackU,
			WorldGrip,
			WorldBase,
			WorldTip,
			CorePose == KomorebiCore::EAttackType::Spin);
	}

	// Arena ring visibility
	if (ArenaRingMesh)
	{
		ArenaRingMesh->SetVisibility(!CoreState.Dead && CoreState.Ready);
	}

	// Telegraph sector visibility & geometry (hunt.js lines 770-782)
	if (TelegraphSectorMesh)
	{
		TelegraphSectorMesh->SetVisibility(Vis.Engaged);
		if (Vis.Engaged)
		{
			const float TellAlpha = (CoreState.Phase == KomorebiCore::EDuelPhase::Active) ? 0.92f
				: (CoreState.Phase == KomorebiCore::EDuelPhase::Windup) ? static_cast<float>(0.42 + 0.50 * Vis.Progress)
				: (Vis.Mode == KomorebiCore::EDuelVisualMode::Opening) ? 0.78f
				: 0.24f;
			UpdateTelegraphSectorMesh(Vis, GroundZ, TellAlpha);
		}
	}

	// Expanding charge aura ring (hunt.js lines 783-798)
	if (AuraRingMesh)
	{
		AuraRingMesh->SetVisibility(Vis.Engaged);
		if (Vis.Engaged)
		{
			const double AuraRadiusM = (CoreState.Phase == KomorebiCore::EDuelPhase::Windup)
				? (0.48 + Vis.Progress * (Vis.Reach * 0.98 - 0.48))
				: (CoreState.Phase == KomorebiCore::EDuelPhase::Active) ? Vis.Reach
				: (Vis.Mode == KomorebiCore::EDuelVisualMode::Opening) ? (1.22 + 0.14 * FMath::Sin(DuelSpinAccum * 4.2f))
				: (1.06 + 0.06 * FMath::Sin(DuelSpinAccum * 2.5f));
			const float AuraAlpha = (CoreState.Phase == KomorebiCore::EDuelPhase::Active) ? 0.88f
				: (CoreState.Phase == KomorebiCore::EDuelPhase::Windup) ? static_cast<float>(0.45 + 0.42 * Vis.Progress)
				: (Vis.Mode == KomorebiCore::EDuelVisualMode::Opening) ? 0.76f
				: 0.34f;
			UpdateAuraRingMesh(Vis, static_cast<float>(AuraRadiusM * 100.0), GroundZ, AuraAlpha);
		}
	}
}
