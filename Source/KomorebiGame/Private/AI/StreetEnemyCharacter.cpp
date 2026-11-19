// Copyright Komorebi Project. Replicated street enemy, civilian, courier/thief, and police character implementation.
#include "AI/StreetEnemyCharacter.h"

#include "AI/EnemySquadSubsystem.h"
#include "Character/KaelCharacter.h"
#include "Combat/CombatComponent.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Game/StreetConfig.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "World/CoordBridge.h"
#include "World/NeoKyotoTerrain.h"
#include "World/WorldGrid.h"

namespace
{
	std::string_view SourceToCoreName(FName Source)
	{
		if (Source == TEXT("heavy")) return "heavy";
		if (Source == TEXT("whirl")) return "whirl";
		if (Source == TEXT("thrust")) return "thrust";
		if (Source == TEXT("spark")) return "spark";
		if (Source == TEXT("flare")) return "flare";
		if (Source == TEXT("ram")) return "ram";
		return "sword";
	}
}

AStreetEnemyCharacter::AStreetEnemyCharacter()
{
	PrimaryActorTick.bCanEverTick = false; // Driven centrally by UEnemySquadSubsystem
	bReplicates = true;
	SetReplicateMovement(true);

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->GravityScale = 0.0f; // We manage ground placement & airborne juggle deterministically matching hunt.js
		MoveComp->bOrientRotationToMovement = false;
	}

	LootBoxMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LootBoxMesh"));
	LootBoxMesh->SetupAttachment(RootComponent);
	LootBoxMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LootBoxMesh->SetRelativeLocation(FVector(-14.0f, -24.0f, -8.0f));
	LootBoxMesh->SetRelativeScale3D(FVector(0.20f, 0.12f, 0.24f));
	LootBoxMesh->SetVisibility(false);

	TelegraphRingMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TelegraphRingMesh"));
	TelegraphRingMesh->SetupAttachment(RootComponent);
	TelegraphRingMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TelegraphRingMesh->SetCastShadow(false);
	TelegraphRingMesh->SetUsingAbsoluteLocation(true);
	TelegraphRingMesh->SetUsingAbsoluteRotation(true);
	TelegraphRingMesh->SetUsingAbsoluteScale(true);
	TelegraphRingMesh->SetVisibility(false);

	GunnerAimBeamMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GunnerAimBeamMesh"));
	GunnerAimBeamMesh->SetupAttachment(RootComponent);
	GunnerAimBeamMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GunnerAimBeamMesh->SetCastShadow(false);
	GunnerAimBeamMesh->SetUsingAbsoluteLocation(true);
	GunnerAimBeamMesh->SetUsingAbsoluteRotation(true);
	GunnerAimBeamMesh->SetUsingAbsoluteScale(true);
	GunnerAimBeamMesh->SetVisibility(false);
}

void AStreetEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();

	const KomorebiCore::EEnemyKind CoreKind = KomorebiBridge::ToCoreEnemyKind(EnemyKind);
	Spec = KomorebiCore::GetEnemyKindSpec(CoreKind);
	Heft = KomorebiCore::GetHeft(CoreKind);

	if (HomeWorldLocation.IsNearlyZero())
	{
		HomeWorldLocation = GetFeetWorldLocation();
		WaypointWorld = HomeWorldLocation;
	}

	RefreshDynamicMaterials();

	if (UWorld* World = GetWorld())
	{
		if (UEnemySquadSubsystem* Squad = World->GetSubsystem<UEnemySquadSubsystem>())
		{
			Squad->RegisterEnemy(this);
		}
	}
}

void AStreetEnemyCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UEnemySquadSubsystem* Squad = World->GetSubsystem<UEnemySquadSubsystem>())
		{
			Squad->UnregisterEnemy(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AStreetEnemyCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AStreetEnemyCharacter, EnemyKind);
	DOREPLIFETIME(AStreetEnemyCharacter, CivArchetype);
	DOREPLIFETIME(AStreetEnemyCharacter, SegmentIndex);
	DOREPLIFETIME(AStreetEnemyCharacter, bIsPolice);
	DOREPLIFETIME(AStreetEnemyCharacter, bIsThief);
	DOREPLIFETIME(AStreetEnemyCharacter, Hp);
	DOREPLIFETIME(AStreetEnemyCharacter, MaxHp);
	DOREPLIFETIME(AStreetEnemyCharacter, bIsDead);
	DOREPLIFETIME(AStreetEnemyCharacter, bIsGone);
	DOREPLIFETIME(AStreetEnemyCharacter, WindupRemaining);
	DOREPLIFETIME(AStreetEnemyCharacter, AnimState);
}

void AStreetEnemyCharacter::InitializeEnemy(
	EKomorebiEnemyKind InKind,
	int32 InSegmentIndex,
	int32 InVariant,
	bool bInPolice,
	bool bInThief,
	bool bInSidewalkPool)
{
	EnemyKind = InKind;
	SegmentIndex = InSegmentIndex;
	bIsPolice = bInPolice;
	bIsThief = bInThief;
	bIsSidewalkPool = bInSidewalkPool;
	PoolSeed = InVariant;
	PoolDir = (InVariant % 2 != 0) ? 1.0f : -1.0f;

	if (EnemyKind == EKomorebiEnemyKind::Civ)
	{
		CivArchetype = static_cast<ECivArchetype>(FMath::Abs(InVariant) % 4);
	}

	const KomorebiCore::EEnemyKind CoreKind = KomorebiBridge::ToCoreEnemyKind(EnemyKind);
	Spec = KomorebiCore::GetEnemyKindSpec(CoreKind);
	Heft = KomorebiCore::GetHeft(CoreKind);

	MaxHp = static_cast<float>(KomorebiCore::GetHitHp(CoreKind));
	Hp = MaxHp;
	AggroRangeMeters = bIsPolice ? 36.0f : static_cast<float>(Spec.AggroRange);
	LeashMeters = bIsPolice ? 80.0f : static_cast<float>(Spec.Leash);
	AIMode = bIsPolice ? EStreetEnemyAIMode::Chase : EStreetEnemyAIMode::Roam;

	const float RadiusCm = static_cast<float>(Spec.CapsuleRadius * 100.0);
	const float HalfHeightCm = static_cast<float>((Spec.CapsuleHalfHeight + Spec.CapsuleRadius) * 100.0);
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCapsuleSize(RadiusCm, HalfHeightCm);
	}
	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		SkelMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -HalfHeightCm));
		SkelMesh->SetRelativeScale3D(FVector(static_cast<float>(Spec.Scale)));
	}
	if (LootBoxMesh)
	{
		LootBoxMesh->SetVisibility(bIsThief);
	}

	HomeWorldLocation = GetFeetWorldLocation();
	WaypointWorld = HomeWorldLocation;
	WaitTimer = FMath::FRandRange(0.0f, 1.5f);
	ChatCooldown = static_cast<float>((FMath::Abs(InVariant) % 7)) * 1.5f;

	if (bIsSidewalkPool)
	{
		const KomorebiCore::Vec3 ThreeHome = KomorebiBridge::FVectorToThree(HomeWorldLocation);
		const KomorebiCore::PavementStep Step = KomorebiCore::NextPavement(ThreeHome.X, ThreeHome.Z, PoolDir, false);
		const FVector NxtUE = KomorebiBridge::ThreeXZToFVector(Step.X, ThreeHome.Y, Step.Z);
		WaypointWorld = FVector(NxtUE.X, NxtUE.Y, QueryGroundHeightCm(NxtUE));
	}

	RefreshDynamicMaterials();
}

void AStreetEnemyCharacter::RefreshDynamicMaterials()
{
	DynamicMaterials.Reset();
	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		const int32 MatCount = SkelMesh->GetNumMaterials();
		for (int32 i = 0; i < MatCount; ++i)
		{
			if (UMaterialInstanceDynamic* MID = SkelMesh->CreateAndSetMaterialInstanceDynamic(i))
			{
				DynamicMaterials.Add(MID);
			}
		}
	}
}

FVector AStreetEnemyCharacter::GetFeetWorldLocation() const
{
	const float HalfH = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : GetBodyLiftCm();
	return GetActorLocation() - FVector(0.0f, 0.0f, HalfH);
}

bool AStreetEnemyCharacter::IsFiringRangedAttack() const
{
	const KomorebiCore::EnemyMovePattern* Pat = KomorebiCore::AttackFor(KomorebiBridge::ToCoreEnemyKind(EnemyKind), AttackIndex);
	return Pat ? Pat->Shot : (Spec.ShotSpeed > 0.0);
}

float AStreetEnemyCharacter::GetActiveMoveReachMeters() const
{
	const KomorebiCore::EnemyMovePattern* Pat = KomorebiCore::AttackFor(KomorebiBridge::ToCoreEnemyKind(EnemyKind), AttackIndex);
	return (Pat && Pat->Reach > 0.0) ? static_cast<float>(Pat->Reach) : static_cast<float>(Spec.Reach);
}

float AStreetEnemyCharacter::QueryGroundHeightCm(const FVector& WorldPos) const
{
	if (const UWorld* World = GetWorld())
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyGroundCheck), false, this);
		const FVector Start(WorldPos.X, WorldPos.Y, WorldPos.Z + 1200.0f);
		const FVector End(WorldPos.X, WorldPos.Y, WorldPos.Z - 2500.0f);
		if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
		{
			return Hit.ImpactPoint.Z;
		}
	}
	const KomorebiCore::Vec3 Three = KomorebiBridge::FVectorToThree(WorldPos);
	const double WayD = KomorebiCore::WayDist(Three.X, Three.Z);
	return static_cast<float>(KomorebiCore::NeoKyotoRawHeight(Three.X, Three.Z, WayD) * 100.0);
}

void AStreetEnemyCharacter::ApplySquadOrder(
	bool bInLead,
	int32 InSide,
	float InRadiusMeters,
	int32 InPreparedIndex,
	bool bInRanged,
	AActor* InTarget)
{
	bIsLead = bInLead;
	bPressuring = bInLead;
	FlankSide = InSide;
	FlankRadiusMeters = InRadiusMeters;
	PreparedAttackIndex = InPreparedIndex;
	bPreparedRanged = bInRanged;
	SquadTarget = InTarget;
}

void AStreetEnemyCharacter::ForceStagger(float DurationSeconds)
{
	StaggerRemaining = FMath::Max(StaggerRemaining, DurationSeconds);
	WindupRemaining = 0.0f;
}

void AStreetEnemyCharacter::ApplyFlareDisruption(const FVector& FlareOriginWorld, float DistanceMeters)
{
	const float PushMeters = FMath::Max(1.5f, 3.0f - DistanceMeters * 0.25f);
	const FVector Away = (GetFeetWorldLocation() - FlareOriginWorld).GetSafeNormal2D();
	SweptMoveHorizontal(Away * (PushMeters * 100.0f));
	AttackCooldown = FMath::Max(AttackCooldown, 2.0f);
	AIMode = EStreetEnemyAIMode::Roam;
	WaypointWorld = HomeWorldLocation;
	WaitTimer = 1.0f;
}

void AStreetEnemyCharacter::ProvokeWitness(float MobDurationSeconds)
{
	if (!Spec.IsCiv || bIsThief || bIsDead || bIsGone)
	{
		return;
	}
	AngryTimer = FMath::Max(AngryTimer, MobDurationSeconds);
	AggroRangeMeters = 18.0f;
	AIMode = EStreetEnemyAIMode::Chase;
	WaitTimer = 0.0f;
	FleeTimer = 0.0f;
	ChatTimer = 0.0f;
}

void AStreetEnemyCharacter::StartStreetChat(float ChatDuration, float Cooldown, float FaceYawDeg)
{
	WaitTimer = ChatDuration;
	ChatTimer = ChatDuration;
	ChatCooldown = Cooldown;
	ChatYawDeg = FaceYawDeg;
}

void AStreetEnemyCharacter::RecycleToSidewalk(const FVector& NewHomeWorld, const FVector& NextWaypointWorld)
{
	HomeWorldLocation = NewHomeWorld;
	WaypointWorld = NextWaypointWorld;
	KnockbackVelXY = FVector::ZeroVector;
	AirHeightCm = 0.0f;
	AirVelZCm = 0.0f;
	FleeTimer = 0.0f;
	ChatTimer = 0.0f;
	const float HalfH = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : GetBodyLiftCm();
	SetActorLocation(NewHomeWorld + FVector(0.0f, 0.0f, HalfH), false, nullptr, ETeleportType::TeleportPhysics);
}

void AStreetEnemyCharacter::ApplySeparationPush(const FVector& PushDeltaXY)
{
	if (bIsDead || bIsGone)
	{
		return;
	}
	SweptMoveHorizontal(PushDeltaXY);
}

void AStreetEnemyCharacter::ResetToHome(bool bRestoreFullHealth)
{
	if (bRestoreFullHealth)
	{
		Hp = MaxHp;
		bIsDead = false;
		bIsGone = false;
		DeadElapsed = 0.0f;
	}

	AIMode = bIsPolice ? EStreetEnemyAIMode::Chase : EStreetEnemyAIMode::Roam;
	WaypointWorld = HomeWorldLocation;
	WindupRemaining = 0.0f;
	RecoverRemaining = 0.0f;
	DefendRemaining = 0.0f;
	DefendCooldown = 0.0f;
	DefendingKind = KomorebiCore::EDefenseKind::None;
	StaggerRemaining = 0.0f;
	AttackCooldown = 0.0f;
	AirHeightCm = 0.0f;
	AirVelZCm = 0.0f;
	KnockbackVelXY = FVector::ZeroVector;
	bReprisal = false;

	if (Spec.IsCiv)
	{
		AngryTimer = 0.0f;
		AggroRangeMeters = 0.0f;
		FleeTimer = 0.0f;
		ChatTimer = 0.0f;
	}

	const float GroundZ = QueryGroundHeightCm(HomeWorldLocation);
	const float HalfH = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : GetBodyLiftCm();
	SetActorLocation(FVector(HomeWorldLocation.X, HomeWorldLocation.Y, GroundZ + HalfH), false, nullptr, ETeleportType::TeleportPhysics);
}

void AStreetEnemyCharacter::RespawnFromGone()
{
	ResetToHome(true);
	AttackCount = 0;
	AttackIndex = 0;
	WaitTimer = 1.0f;
	SetActorHiddenInGame(false);
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	if (LootBoxMesh)
	{
		LootBoxMesh->SetVisibility(bIsThief);
	}
}

void AStreetEnemyCharacter::BlameBystanderHit(bool bKilled, bool bProvoked, AActor* InstigatorActor)
{
	UWorld* World = GetWorld();
	UEnemySquadSubsystem* Squad = World ? World->GetSubsystem<UEnemySquadSubsystem>() : nullptr;
	UCombatComponent* PlayerCombat = InstigatorActor ? InstigatorActor->FindComponentByClass<UCombatComponent>() : nullptr;

	if (bIsThief)
	{
		if (bKilled && PlayerCombat)
		{
			const int32 RewardCash = KomorebiCore::kCashConfig.Thief;
			PlayerCombat->GrantReward(RewardCash, 1.0f, TEXT("Thief — no penalty"), FString::Printf(TEXT("+%d"), RewardCash));
		}
		return;
	}

	if (bProvoked)
	{
		return;
	}

	if (PlayerCombat)
	{
		PlayerCombat->ApplyGuiltAndCarjack(false, bKilled, false);
	}

	if (bKilled && Squad)
	{
		Squad->ReportBystanderCrime(GetFeetWorldLocation(), this, true, InstigatorActor);
	}
}

bool AStreetEnemyCharacter::ReceiveDamage(
	float Amount,
	FName Source,
	const FVector& AttackerLocation,
	AActor* AttackerActor,
	bool bIsParry)
{
	if (bIsDead || bIsGone)
	{
		return false;
	}

	UWorld* World = GetWorld();
	UCombatComponent* PlayerCombat = AttackerActor ? AttackerActor->FindComponentByClass<UCombatComponent>() : nullptr;
	UEnemySquadSubsystem* Squad = World ? World->GetSubsystem<UEnemySquadSubsystem>() : nullptr;

	if (World && PlayerCombat)
	{
		LastHitByPlayerTime = World->GetTimeSeconds();
	}

	const bool bProvoked = (AngryTimer > 0.0f);
	const bool bMelee = (Source == TEXT("sword") || Source == TEXT("thrust") || Source == TEXT("heavy") || Source == TEXT("whirl"));
	const bool bBreaking = (Source == TEXT("heavy") || Source == TEXT("whirl") || Source == TEXT("flare"));

	// Enemy Evade i-frames (hunt.js line 567)
	if (DefendingKind == KomorebiCore::EDefenseKind::Evade && DefendRemaining > 0.06f && bMelee && !bBreaking)
	{
		return false;
	}

	// Enemy frontal Guard block (hunt.js lines 568-576)
	const FVector MyFeet = GetFeetWorldLocation();
	const KomorebiCore::Vec3 MyThree = KomorebiBridge::FVectorToThree(MyFeet);
	const KomorebiCore::Vec3 AttThree = KomorebiBridge::FVectorToThree(AttackerLocation);
	const double MyThreeYaw = KomorebiCore::UEYawDegToThreeYaw(GetActorRotation().Yaw);

	const bool bBlocked = (DefendingKind == KomorebiCore::EDefenseKind::Guard)
		&& DefendRemaining > 0.0f
		&& bMelee
		&& !bBreaking
		&& KomorebiCore::InFront(MyThree.X, MyThree.Z, MyThreeYaw, AttThree.X, AttThree.Z, 5.0, 1.12, 0.0);

	if (bBlocked)
	{
		const float ChipDmg = FMath::Max(1.0f, static_cast<float>(FMath::RoundToInt(Amount * 0.2f)));
		Hp -= FMath::Clamp(ChipDmg, 0.0f, FMath::Max(0.0f, Hp - 1.0f));
		bReprisal = true;
		if (PlayerCombat)
		{
			PlayerCombat->OnPulseVFX.Broadcast(MyFeet + FVector(0.0f, 0.0f, 90.0f), FLinearColor(1.0f, 0.804f, 0.467f, 1.0f), false);
			PlayerCombat->OnFlashText.Broadcast(TEXT("Enemy blocked"));
			PlayerCombat->OnAudioCue.Broadcast(TEXT("parry"), 1.0f);
		}
		return false;
	}

	if (bBreaking && IsGuardingNow() && PlayerCombat)
	{
		PlayerCombat->OnFlashText.Broadcast(TEXT("Enemy guard broken"));
	}

	DefendRemaining = 0.0f;
	DefendingKind = KomorebiCore::EDefenseKind::None;
	RecoverRemaining = 0.0f;

	Hp -= Amount;
	FlashTimer = 0.16f;
	StaggerRemaining = (Amount > 40.0f) ? 0.55f : 0.22f;
	WindupRemaining = 0.0f;
	AttackCooldown = FMath::Max(AttackCooldown, 0.45f);

	// Swept knockback & heavy/whirl airborne juggle launch (hunt.js lines 587-590)
	const double KnockMps = KomorebiCore::GetKnockback(SourceToCoreName(Source)) * Heft;
	const FVector AwayXY = (MyFeet - AttackerLocation).GetSafeNormal2D();
	KnockbackVelXY = (AwayXY.IsNearlyZero() ? -GetActorForwardVector().GetSafeNormal2D() : AwayXY) * static_cast<float>(KnockMps * 100.0);

	if ((Source == TEXT("heavy") || Source == TEXT("whirl")) && AirHeightCm <= 0.0f)
	{
		AirVelZCm = static_cast<float>(KomorebiCore::kLaunchSpeed * Heft * 100.0);
	}

	if (PlayerCombat)
	{
		PlayerCombat->NotifyPlayerHitConnected(Source, false, EnemyKind == EKomorebiEnemyKind::Brute);
		PlayerCombat->OnPulseVFX.Broadcast(MyFeet + FVector(0.0f, 0.0f, 80.0f), FLinearColor(0.722f, 0.961f, 0.925f, 1.0f), false);
		if (Source == TEXT("heavy"))
		{
			PlayerCombat->OnPulseVFX.Broadcast(FVector(MyFeet.X, MyFeet.Y, QueryGroundHeightCm(MyFeet) + 7.0f), FLinearColor(0.965f, 0.831f, 0.576f, 1.0f), true);
		}
	}

	if (Spec.IsCiv)
	{
		BlameBystanderHit(Hp <= 0.0f, bProvoked, AttackerActor);
	}

	if (Hp <= 0.0f)
	{
		EnterDeathState();
	}

	(void)bIsParry;
	(void)Squad;
	return true;
}

bool AStreetEnemyCharacter::ReceiveRamImpact(float Force, FVector Dir, bool bPlayerLiable)
{
	if (bIsDead || bIsGone || RammedCooldown > 0.0f)
	{
		return false;
	}

	const KomorebiCore::ERamBand Band = Spec.IsCiv
		? KomorebiCore::GetRamBand(static_cast<double>(Force))
		: (Force < static_cast<float>(KomorebiCore::kRamConfig.Min) ? KomorebiCore::ERamBand::None : KomorebiCore::ERamBand::Kill);

	if (Band == KomorebiCore::ERamBand::None)
	{
		return false;
	}

	RammedCooldown = 0.5f;
	const bool bProvoked = (AngryTimer > 0.0f);

	UWorld* World = GetWorld();
	AActor* LiablePlayer = nullptr;
	if (bPlayerLiable && World)
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			LiablePlayer = PC->GetPawn();
		}
	}

	float RamDamage = 0.0f;
	if (Band == KomorebiCore::ERamBand::Kill)
	{
		const float Cap = static_cast<float>(Spec.IsCiv ? KomorebiCore::kRamConfig.Max : KomorebiCore::kRamConfig.Crew);
		RamDamage = FMath::Min(Cap, Force * static_cast<float>(KomorebiCore::kRamConfig.Per));
	}
	else
	{
		RamDamage = (Band == KomorebiCore::ERamBand::Stumble) ? 6.0f : FMath::Min(18.0f, FMath::Max(0.0f, Hp - 1.0f));
	}

	Hp -= RamDamage;
	FlashTimer = 0.16f;
	WindupRemaining = 0.0f;
	DefendRemaining = 0.0f;
	DefendingKind = KomorebiCore::EDefenseKind::None;

	if (Band != KomorebiCore::ERamBand::Kill)
	{
		if (Hp > 0.0f && Spec.IsCiv)
		{
			AngryTimer = FMath::Max(AngryTimer, 10.0f);
			AggroRangeMeters = 14.0f;
			AIMode = EStreetEnemyAIMode::Chase;
			WaitTimer = 0.0f;
			FleeTimer = 0.0f;
			ChatTimer = 0.0f;
		}
		StaggerRemaining = FMath::Max(StaggerRemaining, Band == KomorebiCore::ERamBand::Knock ? 1.15f : 0.4f);
	}
	else
	{
		StaggerRemaining = FMath::Max(StaggerRemaining, RamDamage > 40.0f ? 0.55f : 0.22f);
	}

	// Throw along bumper velocity scaled by Heft (hunt.js lines 549-551)
	const float Mul = (Band == KomorebiCore::ERamBand::Stumble) ? 0.28f : (Band == KomorebiCore::ERamBand::Knock ? 0.7f : 0.9f);
	const FVector FlatDir = Dir.GetSafeNormal2D();
	KnockbackVelXY = FlatDir * static_cast<float>(Force * Mul * Heft * 100.0);
	const float UpFactor = (Band == KomorebiCore::ERamBand::Stumble) ? 0.12f : 0.4f;
	AirVelZCm = FMath::Max(AirVelZCm, FMath::Min(750.0f, Force * UpFactor * 100.0f) * static_cast<float>(Heft));

	if (bPlayerLiable && Spec.IsCiv)
	{
		BlameBystanderHit(Hp <= 0.0f, bProvoked, LiablePlayer);
	}
	if (bPlayerLiable && World)
	{
		LastHitByPlayerTime = World->GetTimeSeconds();
	}

	if (Hp <= 0.0f)
	{
		EnterDeathState();
	}
	return true;
}

void AStreetEnemyCharacter::EnterDeathState()
{
	bIsDead = true;
	DeadElapsed = 0.001f;
	WindupRemaining = 0.0f;
	AnimState = EStreetEnemyAnimState::Death;

	if (TelegraphRingMesh) TelegraphRingMesh->SetVisibility(false);
	if (GunnerAimBeamMesh) GunnerAimBeamMesh->SetVisibility(false);

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// Fallen are thrown (hunt.js line 618)
	KnockbackVelXY *= 1.7f;
	AirVelZCm = FMath::Max(AirVelZCm, static_cast<float>(350.0 * Heft));

	// Reward player for armed crew kill/assist within 4s (hunt.js line 615)
	if (!Spec.IsCiv)
	{
		if (UWorld* World = GetWorld())
		{
			if ((World->GetTimeSeconds() - LastHitByPlayerTime) < 4.0f)
			{
				if (APlayerController* PC = World->GetFirstPlayerController())
				{
					if (APawn* Pawn = PC->GetPawn())
					{
						if (UCombatComponent* Combat = Pawn->FindComponentByClass<UCombatComponent>())
						{
							const int32 Bounty = KomorebiCore::kCashConfig.Enemy;
							Combat->GrantReward(Bounty, 0.35f, FString::Printf(TEXT("+%d"), Bounty), FString());
						}
					}
				}
			}
		}
	}
}

void AStreetEnemyCharacter::SweptMoveHorizontal(const FVector& DeltaXY)
{
	const float Dist = DeltaXY.Size2D();
	if (Dist <= KINDA_SMALL_NUMBER || !GetWorld())
	{
		return;
	}
	const FVector Dir = FVector(DeltaXY.X, DeltaXY.Y, 0.0f) / Dist;
	const FVector Feet = GetFeetWorldLocation();
	const FVector RayStart = Feet + FVector(0.0f, 0.0f, AirHeightCm + 60.0f);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemySweepMove), false, this);
	float AllowedDist = Dist;
	if (GetWorld()->LineTraceSingleByChannel(Hit, RayStart, RayStart + Dir * (Dist + 60.0f), ECC_WorldStatic, Params))
	{
		AllowedDist = FMath::Max(0.0f, Hit.Distance - 60.0f);
	}

	FVector Loc = GetActorLocation();
	Loc.X += Dir.X * AllowedDist;
	Loc.Y += Dir.Y * AllowedDist;
	SetActorLocation(Loc);
}

void AStreetEnemyCharacter::StepKnockbackAndAirborne(float DeltaTime)
{
	const float SpeedCm = KnockbackVelXY.Size2D();
	if (SpeedCm > 5.0f)
	{
		SweptMoveHorizontal(KnockbackVelXY * DeltaTime);
		const float Fade = FMath::Exp(-(AirHeightCm > 0.0f ? 2.5f : 7.0f) * DeltaTime);
		KnockbackVelXY *= Fade;
	}
	else
	{
		KnockbackVelXY = FVector::ZeroVector;
	}

	if (AirHeightCm > 0.0f || AirVelZCm > 0.0f)
	{
		AirVelZCm -= 2200.0f * DeltaTime;
		AirHeightCm += AirVelZCm * DeltaTime;
		if (AirHeightCm <= 0.0f)
		{
			AirHeightCm = 0.0f;
			AirVelZCm = 0.0f;
		}
		else if (!bIsDead)
		{
			StaggerRemaining = FMath::Max(StaggerRemaining, 0.15f);
			WindupRemaining = 0.0f;
		}
	}
}

bool AStreetEnemyCharacter::HasClearFiringLane(const FVector& TargetFeet, float OptionalLockedThreeYaw, bool bUseLockedYaw) const
{
	if (!GetWorld())
	{
		return true;
	}
	const FVector MyFeet = GetFeetWorldLocation();
	const FVector Muzzle = MyFeet + FVector(0.0f, 0.0f, 125.0f);
	FVector TargetChest = TargetFeet + FVector(0.0f, 0.0f, 105.0f);

	if (bUseLockedYaw)
	{
		const float HorizDist = FVector::Dist2D(MyFeet, TargetFeet);
		const double UEYawRad = FMath::DegreesToRadians(KomorebiCore::ThreeYawToUEYawDeg(OptionalLockedThreeYaw));
		TargetChest.X = MyFeet.X + FMath::Cos(UEYawRad) * HorizDist;
		TargetChest.Y = MyFeet.Y + FMath::Sin(UEYawRad) * HorizDist;
	}

	const float Dist = FVector::Dist(Muzzle, TargetChest);
	if (Dist < 80.0f)
	{
		return true;
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyFiringLane), false, this);
	const FVector End = Muzzle + (TargetChest - Muzzle).GetSafeNormal() * (Dist - 65.0f);
	return !GetWorld()->LineTraceSingleByChannel(Hit, Muzzle, End, ECC_WorldStatic, Params);
}

void AStreetEnemyCharacter::FireGunnerBolt(const FVector& TargetFeet)
{
	if (!GetWorld() || Spec.ShotSpeed <= 0.0)
	{
		return;
	}

	const FVector MyFeet = GetFeetWorldLocation();
	const float UEYawDeg = static_cast<float>(KomorebiCore::ThreeYawToUEYawDeg(AttackThreeYaw));
	const float UEYawRad = FMath::DegreesToRadians(UEYawDeg);
	const FVector Fwd(FMath::Cos(UEYawRad), FMath::Sin(UEYawRad), 0.0f);

	const FVector Origin = MyFeet + Fwd * 70.0f + FVector(0.0f, 0.0f, 125.0f);
	const float ShotSpeedCm = static_cast<float>(Spec.ShotSpeed * 100.0);
	const float HorizDistCm = FVector::Dist2D(MyFeet, TargetFeet);
	const float FlightTime = FMath::Max(0.2f, HorizDistCm / FMath::Max(1.0f, ShotSpeedCm));
	const float VelZCm = FMath::Clamp((TargetFeet.Z + 105.0f - Origin.Z) / FlightTime, -400.0f, 400.0f);
	const FVector BoltVel(Fwd.X * ShotSpeedCm, Fwd.Y * ShotSpeedCm, VelZCm);

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			if (UCombatComponent* Combat = Pawn->FindComponentByClass<UCombatComponent>())
			{
				Combat->SpawnHostileBolt(Origin, BoltVel, static_cast<float>(Spec.Damage), this);
			}
		}
	}
}

void AStreetEnemyCharacter::StepEnemyAI(
	float DeltaTime,
	AActor* LocalPlayer,
	bool bFightingNearby,
	bool bGateBlocked,
	bool bFreeRoam)
{
	FlashTimer = FMath::Max(0.0f, FlashTimer - DeltaTime);
	RammedCooldown = FMath::Max(0.0f, RammedCooldown - DeltaTime);
	StaggerRemaining = FMath::Max(0.0f, StaggerRemaining - DeltaTime);

	// Respawn when gone: 45s for civilians/thieves, 20s for crew in free roam (hunt.js lines 1148-1156)
	if (bIsGone)
	{
		if ((bFreeRoam || Spec.IsCiv) && !bIsEventMech)
		{
			RespawnTimer -= DeltaTime;
			if (RespawnTimer <= 0.0f)
			{
				RespawnFromGone();
			}
		}
		return;
	}

	if (bIsDead)
	{
		DeadElapsed += DeltaTime;
		GoingSpeedMps = 0.0f;
		StepKnockbackAndAirborne(DeltaTime);
		UpdateVisualsAndMaterials(DeltaTime, LocalPlayer);
		if (DeadElapsed > 2.2f)
		{
			bIsGone = true;
			RespawnTimer = Spec.IsCiv ? UEnemySquadSubsystem::kCivRespawnSeconds : UEnemySquadSubsystem::kCrewRespawnSeconds;
			SetActorHiddenInGame(true);
		}
		return;
	}

	StepKnockbackAndAirborne(DeltaTime);

	const FVector MyFeet = GetFeetWorldLocation();
	const AActor* GoalActor = SquadTarget.IsValid() ? SquadTarget.Get() : LocalPlayer;
	const FVector GoalFeet = GoalActor
		? (Cast<AKaelCharacter>(GoalActor) ? Cast<AKaelCharacter>(GoalActor)->GetFeetWorldLocation() : GoalActor->GetActorLocation())
		: MyFeet;
	const FVector LocalFeet = LocalPlayer
		? (Cast<AKaelCharacter>(LocalPlayer) ? Cast<AKaelCharacter>(LocalPlayer)->GetFeetWorldLocation() : LocalPlayer->GetActorLocation())
		: MyFeet;

	const float DistToGoalM = FVector::Dist2D(MyFeet, GoalFeet) * 0.01f;
	const float DistHomeM = FVector::Dist2D(MyFeet, HomeWorldLocation) * 0.01f;

	// Distance-capped AI (36m crew, 46m civilians) matching hunt.js line 1181
	const float MaxAIRangeM = Spec.IsCiv ? kCivAIRangeMeters : kCrewAIRangeMeters;
	if (DistToGoalM > MaxAIRangeM)
	{
		GoingSpeedMps = 0.0f;
		AttackCooldown = FMath::Max(0.0f, AttackCooldown - DeltaTime);
		UpdateVisualsAndMaterials(0.0f, LocalPlayer);
		return;
	}

	if (ChatCooldown > 0.0f) ChatCooldown = FMath::Max(0.0f, ChatCooldown - DeltaTime);
	if (ChatTimer > 0.0f) ChatTimer = FMath::Max(0.0f, ChatTimer - DeltaTime);
	if (FleeTimer > 0.0f) FleeTimer = FMath::Max(0.0f, FleeTimer - DeltaTime);

	// Calm civilians flee nearby street fights (hunt.js lines 1186-1197)
	if (bFightingNearby && Spec.IsCiv && !bIsThief && AngryTimer <= 0.0f && WaitTimer < 50.0f && DistToGoalM > 3.2f && DistToGoalM < 15.0f)
	{
		if (bIsSidewalkPool)
		{
			const KomorebiCore::Vec3 MyThree = KomorebiBridge::FVectorToThree(MyFeet);
			const KomorebiCore::Vec3 PlThree = KomorebiBridge::FVectorToThree(LocalFeet);
			const KomorebiCore::PavementStep PosStep = KomorebiCore::NextPavement(MyThree.X, MyThree.Z, 1.0, false);
			const KomorebiCore::PavementStep NegStep = KomorebiCore::NextPavement(MyThree.X, MyThree.Z, -1.0, false);
			const double DPos = std::hypot(PosStep.X - PlThree.X, PosStep.Z - PlThree.Z);
			const double DNeg = std::hypot(NegStep.X - PlThree.X, NegStep.Z - PlThree.Z);
			const float AwayDir = (DPos >= DNeg) ? 1.0f : -1.0f;
			if (FleeTimer <= 0.0f || !FMath::IsNearlyEqual(PoolDir, AwayDir))
			{
				PoolDir = AwayDir;
				const KomorebiCore::PavementStep Chosen = (AwayDir > 0.0f) ? PosStep : NegStep;
				const FVector NxtUE = KomorebiBridge::ThreeXZToFVector(Chosen.X, MyThree.Y, Chosen.Z);
				WaypointWorld = FVector(NxtUE.X, NxtUE.Y, QueryGroundHeightCm(NxtUE));
			}
		}
		FleeTimer = 4.5f;
		WaitTimer = 0.0f;
		ChatTimer = 0.0f;
	}

	// Angry witness mob timer expiry (hunt.js lines 1203-1207)
	if (AngryTimer > 0.0f)
	{
		AngryTimer -= DeltaTime;
		if (AngryTimer <= 0.0f)
		{
			AngryTimer = 0.0f;
			AggroRangeMeters = 0.0f;
			AIMode = EStreetEnemyAIMode::Roam;
			if (bIsSidewalkPool)
			{
				const KomorebiCore::Vec3 MyThree = KomorebiBridge::FVectorToThree(MyFeet);
				const KomorebiCore::PavementStep Step = KomorebiCore::NextPavement(MyThree.X, MyThree.Z, PoolDir, false);
				const FVector NxtUE = KomorebiBridge::ThreeXZToFVector(Step.X, MyThree.Y, Step.Z);
				WaypointWorld = FVector(NxtUE.X, NxtUE.Y, QueryGroundHeightCm(NxtUE));
			}
		}
	}

	// Aggro & Leash transitions
	if (AIMode == EStreetEnemyAIMode::Roam && DistToGoalM < AggroRangeMeters && !bGateBlocked)
	{
		AIMode = EStreetEnemyAIMode::Chase;
	}
	if (AIMode == EStreetEnemyAIMode::Chase && AngryTimer <= 0.0f && (DistToGoalM > AggroRangeMeters + 5.0f || DistHomeM > LeashMeters))
	{
		AIMode = EStreetEnemyAIMode::Roam;
		WaypointWorld = HomeWorldLocation;
		WaitTimer = 0.0f;
	}

	DefendRemaining = FMath::Max(0.0f, DefendRemaining - DeltaTime);
	if (DefendRemaining <= 0.0f)
	{
		DefendingKind = KomorebiCore::EDefenseKind::None;
	}
	DefendCooldown = FMath::Max(0.0f, DefendCooldown - DeltaTime);
	RecoverRemaining = FMath::Max(0.0f, RecoverRemaining - DeltaTime);

	const KomorebiCore::EEnemyKind CoreKind = KomorebiBridge::ToCoreEnemyKind(EnemyKind);
	const std::optional<KomorebiCore::EnemyDefenseSpec> DefenseSpec = KomorebiCore::GetEnemyDefense(CoreKind);

	// Reactive Guard / Evade against player melee windup (hunt.js lines 1242-1252)
	const AKaelCharacter* GoalKael = Cast<AKaelCharacter>(GoalActor);
	const UCombatComponent* GoalCombat = GoalKael ? GoalKael->GetCombatComponent() : nullptr;
	if (DefenseSpec.has_value()
		&& AIMode == EStreetEnemyAIMode::Chase
		&& WindupRemaining <= 0.0f
		&& RecoverRemaining <= 0.0f
		&& StaggerRemaining <= 0.0f
		&& AirHeightCm <= 0.0f
		&& DefendCooldown <= 0.0f
		&& GoalCombat
		&& GoalCombat->IsInAttackWindup())
	{
		const KomorebiCore::Vec3 MyThree = KomorebiBridge::FVectorToThree(MyFeet);
		const KomorebiCore::Vec3 GoalThree = KomorebiBridge::FVectorToThree(GoalFeet);
		const double GoalThreeYaw = KomorebiCore::UEYawDegToThreeYaw(GoalKael->GetActorRotation().Yaw);
		const double MyThreeYaw = KomorebiCore::UEYawDegToThreeYaw(GetActorRotation().Yaw);

		if (KomorebiCore::InFront(GoalThree.X, GoalThree.Z, GoalThreeYaw, MyThree.X, MyThree.Z, DefenseSpec->Range, 0.88, 0.0)
			&& KomorebiCore::InFront(MyThree.X, MyThree.Z, MyThreeYaw, GoalThree.X, GoalThree.Z, DefenseSpec->Range, 1.55, 0.0)
			&& HasClearFiringLane(GoalFeet, 0.0f, false))
		{
			DefendingKind = DefenseSpec->Kind;
			DefendRemaining = static_cast<float>(DefenseSpec->Time);
			DefendCooldown = static_cast<float>(DefenseSpec->Cooldown);
			DefendSide = FlankSide;
			FRotator Rot = GetActorRotation();
			Rot.Yaw = (GoalFeet - MyFeet).Rotation().Yaw;
			SetActorRotation(Rot);
		}
	}

	const KomorebiCore::EnemyMovePattern* PreparedPat = KomorebiCore::AttackFor(CoreKind, PreparedAttackIndex);
	const KomorebiCore::EnemyMovePattern* ActivePat = KomorebiCore::AttackFor(CoreKind, AttackIndex);

	float MoveSpeedMps = 0.0f;
	const float RoamPaceMps = (FleeTimer > 0.0f)
		? (EnemyKind == EKomorebiEnemyKind::Civ ? static_cast<float>(Spec.RoamSpeed * 2.1) : FMath::Max(static_cast<float>(Spec.RoamSpeed), 4.8f))
		: (SnatchTimer > 0.0f ? static_cast<float>(Spec.RoamSpeed * 1.5) : static_cast<float>(Spec.RoamSpeed));

	if (AIMode == EStreetEnemyAIMode::Chase)
	{
		WaypointWorld = GoalFeet;
		const float ReachM = static_cast<float>((PreparedPat ? PreparedPat->Reach : Spec.Reach) + (PreparedPat ? PreparedPat->Rush : 0.0) * 0.65);
		MoveSpeedMps = (DistToGoalM > ReachM * 0.83f) ? static_cast<float>(Spec.ChaseSpeed) : 0.0f;

		// Gunner backs off inside KeepDistance (5m)
		if (PreparedPat && PreparedPat->Shot && DistToGoalM < static_cast<float>(Spec.KeepDistance))
		{
			WaypointWorld = MyFeet + (MyFeet - GoalFeet);
			MoveSpeedMps = static_cast<float>(Spec.ChaseSpeed);
		}

		const bool bEvading = (DefendRemaining > 0.0f && DefendingKind == KomorebiCore::EDefenseKind::Evade);
		if (!bIsLead || bEvading || LaneStepTimer > 0.0f)
		{
			const FVector ToGoal = (GoalFeet - MyFeet).GetSafeNormal2D();
			const FVector Tangent(-ToGoal.Y, ToGoal.X, 0.0f);
			const int32 Side = bEvading ? DefendSide : (LaneStepTimer > 0.0f ? LaneSide : FlankSide);
			const float TargetRadiusM = bPreparedRanged ? 7.0f : FlankRadiusMeters;
			const float RadialM = bEvading
				? (EnemyKind == EKomorebiEnemyKind::Gunner ? -1.45f : -0.5f)
				: FMath::Clamp(DistToGoalM - TargetRadiusM, -2.4f, 2.4f);

			WaypointWorld = MyFeet + Tangent * (static_cast<float>(Side) * 220.0f) + ToGoal * (RadialM * 100.0f);
			MoveSpeedMps = bEvading && DefenseSpec.has_value()
				? static_cast<float>(DefenseSpec->Speed)
				: static_cast<float>(Spec.ChaseSpeed * 0.82);
		}
	}
	else if (WaitTimer > 0.0f)
	{
		WaitTimer -= DeltaTime;
	}
	else if (FVector::Dist2D(MyFeet, WaypointWorld) < 45.0f)
	{
		WaitTimer = (FleeTimer > 0.0f) ? 0.0f : FMath::FRandRange(0.7f, 2.5f);
		if (bIsSidewalkPool)
		{
			const KomorebiCore::Vec3 MyThree = KomorebiBridge::FVectorToThree(MyFeet);
			const bool bCross = (FleeTimer <= 0.0f) && (bIsThief || FMath::FRand() < 0.4f);
			KomorebiCore::PavementStep Step = KomorebiCore::NextPavement(MyThree.X, MyThree.Z, PoolDir, bCross);
			if (std::hypot(Step.X - MyThree.X, Step.Z - MyThree.Z) < 0.8)
			{
				PoolDir *= -1.0f;
				Step = KomorebiCore::NextPavement(MyThree.X, MyThree.Z, PoolDir, false);
			}
			const FVector NxtUE = KomorebiBridge::ThreeXZToFVector(Step.X, MyThree.Y, Step.Z);
			WaypointWorld = FVector(NxtUE.X, NxtUE.Y, QueryGroundHeightCm(NxtUE));
			WaitTimer = (FleeTimer > 0.0f) ? 0.0f : (bIsThief ? 0.15f : FMath::FRandRange(0.0f, 1.4f));
		}
		else
		{
			const float Angle = FMath::FRandRange(0.0f, TWO_PI);
			const float RadiusCm = FMath::FRandRange(0.0f, static_cast<float>(Spec.HomeRadius * 100.0));
			WaypointWorld = HomeWorldLocation + FVector(FMath::Cos(Angle) * RadiusCm, FMath::Sin(Angle) * RadiusCm, 0.0f);
		}

		if (WaitTimer <= 0.0f)
		{
			MoveSpeedMps = RoamPaceMps;
		}
	}
	else
	{
		MoveSpeedMps = RoamPaceMps;
	}

	if (SnatchTimer > 0.0f) SnatchTimer -= DeltaTime;

	// Movement freeze during windup, stagger, early recovery, or Guard
	const float ActiveRecoverTotal = ActivePat ? static_cast<float>(ActivePat->Recover) : 0.0f;
	if (WindupRemaining > 0.0f || StaggerRemaining > 0.0f || RecoverRemaining > ActiveRecoverTotal * 0.62f || IsGuardingNow())
	{
		MoveSpeedMps = 0.0f;
	}

	const FVector ToWaypoint = FVector(WaypointWorld.X - MyFeet.X, WaypointWorld.Y - MyFeet.Y, 0.0f);
	const float WaypointDistCm = ToWaypoint.Size();
	const float StepCm = FMath::Min(MoveSpeedMps * 100.0f * DeltaTime, WaypointDistCm);

	const FVector PosBefore = GetActorLocation();
	if (StepCm > KINDA_SMALL_NUMBER && WaypointDistCm > KINDA_SMALL_NUMBER)
	{
		SweptMoveHorizontal((ToWaypoint / WaypointDistCm) * StepCm);
	}
	const FVector PosAfter = GetActorLocation();
	GoingSpeedMps = (FVector::Dist2D(PosBefore, PosAfter) * 0.01f) / FMath::Max(DeltaTime, 1e-4f);

	// Turning
	if ((StepCm > 0.01f || DefendRemaining > 0.0f) && WindupRemaining <= 0.0f)
	{
		const FVector FaceVec = (DefendRemaining > 0.0f || !bIsLead) ? (GoalFeet - MyFeet) : ToWaypoint;
		if (FaceVec.SizeSquared2D() > KINDA_SMALL_NUMBER)
		{
			const float WantYaw = FaceVec.Rotation().Yaw;
			const float CurYaw = GetActorRotation().Yaw;
			const float MaxTurnDeg = FMath::RadiansToDegrees(8.0f) * DeltaTime;
			FRotator Rot = GetActorRotation();
			Rot.Yaw = FMath::UnwindDegrees(CurYaw + FMath::Clamp(FMath::FindDeltaAngleDegrees(CurYaw, WantYaw), -MaxTurnDeg, MaxTurnDeg));
			SetActorRotation(Rot);
		}
	}
	else if (Spec.ShotSpeed > 0.0 && AIMode == EStreetEnemyAIMode::Chase && WindupRemaining <= 0.0f)
	{
		FRotator Rot = GetActorRotation();
		Rot.Yaw = (GoalFeet - MyFeet).Rotation().Yaw;
		SetActorRotation(Rot);
	}
	else if (ChatTimer > 0.0f && WindupRemaining <= 0.0f && AngryTimer <= 0.0f && FleeTimer <= 0.0f)
	{
		const float CurYaw = GetActorRotation().Yaw;
		const float MaxTurnDeg = FMath::RadiansToDegrees(6.0f) * DeltaTime;
		FRotator Rot = GetActorRotation();
		Rot.Yaw = FMath::UnwindDegrees(CurYaw + FMath::Clamp(FMath::FindDeltaAngleDegrees(CurYaw, ChatYawDeg), -MaxTurnDeg, MaxTurnDeg));
		SetActorRotation(Rot);
	}

	AttackCooldown = FMath::Max(0.0f, AttackCooldown - DeltaTime);
	if (LaneStepTimer > 0.0f) LaneStepTimer -= DeltaTime;

	// Windup & Strike execution (hunt.js lines 1317-1347)
	UEnemySquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<UEnemySquadSubsystem>() : nullptr;
	if (WindupRemaining > 0.0f)
	{
		const bool bShotAttack = ActivePat ? ActivePat->Shot : (Spec.ShotSpeed > 0.0);
		if (bShotAttack && WindupRemaining > static_cast<float>(KomorebiCore::kAimLock))
		{
			const float AimYawDeg = (GoalFeet - GetFeetWorldLocation()).Rotation().Yaw;
			FRotator Rot = GetActorRotation();
			Rot.Yaw = AimYawDeg;
			SetActorRotation(Rot);
			AttackThreeYaw = static_cast<float>(KomorebiCore::UEYawDegToThreeYaw(AimYawDeg));
		}
		if (bShotAttack && WindupRemaining < static_cast<float>(KomorebiCore::kAimLock) && !bAimLockPlayed)
		{
			bAimLockPlayed = true;
			if (Squad) Squad->EmitAudioCue(TEXT("lock"));
		}
		if (ActivePat && ActivePat->Rush > 0.0 && WindupRemaining <= 0.2f)
		{
			const double UEYawRad = FMath::DegreesToRadians(KomorebiCore::ThreeYawToUEYawDeg(AttackThreeYaw));
			const FVector RushDir(FMath::Cos(UEYawRad), FMath::Sin(UEYawRad), 0.0f);
			const float RushStepCm = static_cast<float>(ActivePat->Rush / 0.2 * FMath::Min(WindupRemaining, DeltaTime) * 100.0);
			SweptMoveHorizontal(RushDir * RushStepCm);
		}

		WindupRemaining = FMath::Max(0.0f, WindupRemaining - DeltaTime);
		if (WindupRemaining <= 0.0f)
		{
			const bool bPlayerAlive = !GoalCombat || (GoalCombat->Hp > 0.0f && !GoalCombat->bDowned);
			if (bShotAttack)
			{
				bAimLockPlayed = false;
				if (bPlayerAlive && !bGateBlocked && HasClearFiringLane(GoalFeet, AttackThreeYaw, true))
				{
					FireGunnerBolt(GoalFeet);
				}
			}
			else if (bPlayerAlive && !bGateBlocked)
			{
				const FVector StrikeFeet = GetFeetWorldLocation();
				const KomorebiCore::Vec3 MyThree = KomorebiBridge::FVectorToThree(StrikeFeet);
				const KomorebiCore::Vec3 LocalThree = KomorebiBridge::FVectorToThree(LocalFeet);
				const double GroundThreeY = static_cast<double>(QueryGroundHeightCm(StrikeFeet)) * 0.01;
				if (KomorebiCore::StrikeHits(MyThree.X, MyThree.Z, AttackThreeYaw, Spec.Reach, LocalThree.X, LocalThree.Y, LocalThree.Z, GroundThreeY, ActivePat))
				{
					if (AKaelCharacter* LocalKael = Cast<AKaelCharacter>(LocalPlayer))
					{
						if (UCombatComponent* LocalCombat = LocalKael->GetCombatComponent())
						{
							const float StrikeDmg = (ActivePat && ActivePat->Damage > 0.0)
								? static_cast<float>(ActivePat->Damage)
								: static_cast<float>(Spec.Damage);
							LocalCombat->ReceiveEnemyHit(StrikeDmg, this, static_cast<float>(Spec.Push), ActivePat ? ActivePat->Unblockable : false);
						}
					}
				}
			}

			RecoverRemaining = ActivePat ? static_cast<float>(ActivePat->Recover) : 0.0f;
			if (EnemyKind == EKomorebiEnemyKind::Brute)
			{
				if (AKaelCharacter* LocalKael = Cast<AKaelCharacter>(LocalPlayer))
				{
					if (UCombatComponent* LocalCombat = LocalKael->GetCombatComponent())
					{
						LocalCombat->AddCameraKick(0.06f);
						const FVector BruteFeet = GetFeetWorldLocation();
						LocalCombat->OnPulseVFX.Broadcast(FVector(BruteFeet.X, BruteFeet.Y, QueryGroundHeightCm(BruteFeet) + 6.0f), FLinearColor(1.0f, 0.624f, 0.478f, 1.0f), true);
					}
				}
			}
		}
	}
	else if (AIMode == EStreetEnemyAIMode::Chase
		&& bIsLead
		&& DistToGoalM < static_cast<float>((PreparedPat ? PreparedPat->Reach : Spec.Reach) + 0.25 + (PreparedPat ? PreparedPat->Rush : 0.0))
		&& AttackCooldown <= 0.0f
		&& StaggerRemaining <= 0.0f
		&& DefendRemaining <= 0.0f
		&& !bGateBlocked
		&& (!GoalCombat || GoalCombat->Hp > 0.0f))
	{
		if (PreparedPat && PreparedPat->Shot && !HasClearFiringLane(GoalFeet, 0.0f, false))
		{
			if (LaneSide == 0 || GoingSpeedMps < 0.2f)
			{
				LaneSide = (LaneSide == 1) ? -1 : 1;
			}
			LaneStepTimer = 0.56f;
			AttackCooldown = 0.56f;
		}
		else
		{
			AttackIndex = PreparedAttackIndex;
			++AttackCount;
			bReprisal = false;
			WindupRemaining = static_cast<float>(PreparedPat ? PreparedPat->Wind : Spec.Windup);
			bAimLockPlayed = false;

			const float AimYawDeg = (GoalFeet - GetFeetWorldLocation()).Rotation().Yaw;
			FRotator Rot = GetActorRotation();
			Rot.Yaw = AimYawDeg;
			SetActorRotation(Rot);
			AttackThreeYaw = static_cast<float>(KomorebiCore::UEYawDegToThreeYaw(AimYawDeg));
			AttackCooldown = WindupRemaining + static_cast<float>(PreparedPat ? PreparedPat->Recover : 1.0);
		}
	}

	UpdateVisualsAndMaterials(DeltaTime, LocalPlayer);
}

void AStreetEnemyCharacter::RebuildTelegraphRingMesh(float RadiusCm, const FLinearColor& Color)
{
	if (!TelegraphRingMesh)
	{
		return;
	}
	constexpr int32 Segs = 32;
	const float InnerR = RadiusCm * 0.88f;
	const float OuterR = RadiusCm;

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
		const float Ang = U * TWO_PI;
		const float C = FMath::Cos(Ang);
		const float S = FMath::Sin(Ang);
		Verts.Add(FVector(C * InnerR, S * InnerR, 0.0f));
		Verts.Add(FVector(C * OuterR, S * OuterR, 0.0f));
		Normals.Add(FVector::UpVector);
		Normals.Add(FVector::UpVector);
		UVs.Add(FVector2D(U, 0.0f));
		UVs.Add(FVector2D(U, 1.0f));
		Colors.Add(Color);
		Colors.Add(Color);
	}
	for (int32 i = 0; i < Segs; ++i)
	{
		const int32 I0 = i * 2;
		Tris.Add(I0); Tris.Add(I0 + 2); Tris.Add(I0 + 1);
		Tris.Add(I0 + 1); Tris.Add(I0 + 2); Tris.Add(I0 + 3);
	}

	if (!bTelegraphBuilt)
	{
		TelegraphRingMesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, Colors, Tangents, false);
		bTelegraphBuilt = true;
	}
	else
	{
		TelegraphRingMesh->UpdateMeshSection_LinearColor(0, Verts, Normals, UVs, Colors, Tangents);
	}
}

void AStreetEnemyCharacter::RebuildAimBeamMesh(float LengthCm, const FLinearColor& Color)
{
	if (!GunnerAimBeamMesh)
	{
		return;
	}
	const float HalfW = 2.0f;
	TArray<FVector> Verts = {
		FVector(0.0f, -HalfW, 0.0f),
		FVector(0.0f, HalfW, 0.0f),
		FVector(LengthCm, -HalfW, 0.0f),
		FVector(LengthCm, HalfW, 0.0f)
	};
	TArray<int32> Tris = { 0, 2, 1, 1, 2, 3 };
	TArray<FVector> Normals = { FVector::UpVector, FVector::UpVector, FVector::UpVector, FVector::UpVector };
	TArray<FVector2D> UVs = { FVector2D(0, 0), FVector2D(0, 1), FVector2D(1, 0), FVector2D(1, 1) };
	TArray<FLinearColor> Colors = { Color, Color, Color, Color };
	TArray<FProcMeshTangent> Tangents;

	if (!bAimBeamBuilt)
	{
		GunnerAimBeamMesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, Colors, Tangents, false);
		bAimBeamBuilt = true;
	}
	else
	{
		GunnerAimBeamMesh->UpdateMeshSection_LinearColor(0, Verts, Normals, UVs, Colors, Tangents);
	}
}

void AStreetEnemyCharacter::UpdateVisualsAndMaterials(float DeltaTime, AActor* LocalPlayer)
{
	const FVector Loc = GetActorLocation();
	const float GroundZ = QueryGroundHeightCm(Loc);
	const float HalfH = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : GetBodyLiftCm();
	const float DeadSinkCm = bIsDead ? FMath::Min(40.0f, FMath::Max(0.0f, DeadElapsed - 1.2f) * 45.0f) : 0.0f;

	SetActorLocation(FVector(Loc.X, Loc.Y, GroundZ + HalfH + AirHeightCm - DeadSinkCm));

	// Distance-capped visibility (85m max, <9m always visible) matching seen() in hunt.js lines 850-851
	bool bVisible = !bIsGone;
	if (bVisible && LocalPlayer)
	{
		const float PlayerDistM = FVector::Dist2D(Loc, LocalPlayer->GetActorLocation()) * 0.01f;
		if (PlayerDistM >= kMaxVisibleMeters)
		{
			bVisible = false;
		}
		else if (PlayerDistM >= kAlwaysVisibleMeters)
		{
			if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
			{
				FVector CamLoc;
				FRotator CamRot;
				PC->GetPlayerViewPoint(CamLoc, CamRot);
				const FVector ToMe = (Loc - CamLoc).GetSafeNormal();
				if (FVector::DotProduct(CamRot.Vector(), ToMe) < 0.15f)
				{
					bVisible = false;
				}
			}
		}
	}
	SetActorHiddenInGame(!bVisible);

	// Hit-flash dynamic material update & squash/stretch (hunt.js lines 802-804)
	const float FlashIntensity = FlashTimer > 0.0f ? FMath::Clamp(FlashTimer / 0.16f, 0.0f, 1.0f) : 0.0f;
	for (UMaterialInstanceDynamic* MID : DynamicMaterials)
	{
		if (MID)
		{
			MID->SetScalarParameterValue(TEXT("FlashAmount"), FlashIntensity);
		}
	}

	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		const float Sc = static_cast<float>(Spec.Scale) * (FlashTimer > 0.0f ? 1.06f : 1.0f);
		SkelMesh->SetRelativeScale3D(FVector(Sc, Sc, Sc * (FlashTimer > 0.0f ? 0.86f : 1.0f)));
	}

	const KomorebiCore::EnemyMovePattern* ActivePat = KomorebiCore::AttackFor(KomorebiBridge::ToCoreEnemyKind(EnemyKind), AttackIndex);
	const bool bFiring = IsFiringRangedAttack();

	// Gunner laser sight (flickers once WindupRemaining < kAimLock = 0.3s)
	if (GunnerAimBeamMesh)
	{
		const bool bShowAim = !bIsDead && bVisible && bFiring && WindupRemaining > 0.0f;
		GunnerAimBeamMesh->SetVisibility(bShowAim);
		if (bShowAim)
		{
			const float AimAlpha = (WindupRemaining < static_cast<float>(KomorebiCore::kAimLock))
				? (0.55f + 0.45f * FMath::Sin(WindupRemaining * 70.0f))
				: 0.35f;
			const float UEYawDeg = static_cast<float>(KomorebiCore::ThreeYawToUEYawDeg(AttackThreeYaw));
			GunnerAimBeamMesh->SetWorldLocationAndRotation(
				FVector(Loc.X, Loc.Y, GroundZ + 125.0f),
				FRotator(0.0f, UEYawDeg, 0.0f));
			RebuildAimBeamMesh(static_cast<float>((Spec.Reach + 2.0) * 100.0), FLinearColor(1.0f, 0.165f, 0.427f, AimAlpha));
		}
	}

	// Ground telegraph ring for melee windups or Guard
	if (TelegraphRingMesh)
	{
		const bool bShowTell = !bIsDead && bVisible && ((WindupRemaining > 0.0f && !bFiring) || IsGuardingNow());
		TelegraphRingMesh->SetVisibility(bShowTell);
		if (bShowTell)
		{
			TelegraphRingMesh->SetWorldLocation(FVector(Loc.X, Loc.Y, GroundZ + 4.0f));
			const float RingRadiusCm = IsGuardingNow()
				? static_cast<float>(86.0 * Spec.Scale)
				: (GetActiveMoveReachMeters() * 100.0f);
			const float WindTotal = ActivePat ? static_cast<float>(ActivePat->Wind) : static_cast<float>(Spec.Windup);
			const float Alpha = IsGuardingNow()
				? 0.80f
				: (0.38f + 0.50f * (1.0f - WindupRemaining / FMath::Max(0.05f, WindTotal)));
			const FLinearColor Color = IsGuardingNow()
				? FLinearColor(1.0f, 0.804f, 0.467f, Alpha)
				: ((ActivePat && ActivePat->Unblockable)
					? FLinearColor(1.0f, 0.412f, 0.176f, Alpha)
					: FLinearColor(1.0f, 0.165f, 0.427f, Alpha));
			RebuildTelegraphRingMesh(RingRadiusCm, Color);
		}
	}

	// Animation state selection (hunt.js line 825)
	if (bIsDead)
	{
		AnimState = EStreetEnemyAnimState::Death;
	}
	else if (FlashTimer > 0.0f)
	{
		AnimState = EStreetEnemyAnimState::Hit;
	}
	else if (DefendRemaining > 0.0f)
	{
		AnimState = (DefendingKind == KomorebiCore::EDefenseKind::Guard)
			? EStreetEnemyAnimState::Guard
			: (DefendSide > 0 ? EStreetEnemyAnimState::EvadeLeft : EStreetEnemyAnimState::EvadeRight);
	}
	else if (WindupRemaining > 0.0f || RecoverRemaining > 0.0f)
	{
		AnimState = (AttackIndex == 1) ? EStreetEnemyAnimState::Alternate : EStreetEnemyAnimState::Attack;
	}
	else if (GoingSpeedMps > 2.4f)
	{
		AnimState = EStreetEnemyAnimState::Run;
	}
	else if (GoingSpeedMps > 0.25f)
	{
		AnimState = EStreetEnemyAnimState::Walk;
	}
	else
	{
		AnimState = EStreetEnemyAnimState::Idle;
	}

	(void)DeltaTime;
}
