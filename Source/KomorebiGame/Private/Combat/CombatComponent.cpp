// Copyright Komorebi Project. Replicated player combat implementation porting legacy-web/src/game/hunt.js.
#include "Combat/CombatComponent.h"

#include "AI/EnemySquadSubsystem.h"
#include "AI/RenBossCharacter.h"
#include "AI/StreetEnemyCharacter.h"
#include "Character/KaelCharacter.h"
#include "Character/KaelMovementComponent.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Game/CombatRules.h"
#include "Game/DuelStateMachine.h"
#include "Game/LevelsData.h"
#include "Game/StreetConfig.h"
#include "Game/TacticsRules.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "World/CoordBridge.h"

namespace
{
	FVector YawDegToForwardXY(float YawDeg)
	{
		const float Rad = FMath::DegreesToRadians(YawDeg);
		return FVector(FMath::Cos(Rad), FMath::Sin(Rad), 0.0f);
	}

	FVector YawDegToRightXY(float YawDeg)
	{
		const float Rad = FMath::DegreesToRadians(YawDeg);
		return FVector(-FMath::Sin(Rad), FMath::Cos(Rad), 0.0f);
	}
}

UCombatComponent::UCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	MaxHp = static_cast<float>(KomorebiCore::kMaxPlayerHp);
	Hp = MaxHp;
	MaxSparkPower = static_cast<float>(KomorebiCore::kPowerMax);
	SparkPower = MaxSparkPower;
}

void UCombatComponent::BeginPlay()
{
	Super::BeginPlay();
	Hp = MaxHp;
	SparkPower = MaxSparkPower;
}

void UCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UCombatComponent, Hp);
	DOREPLIFETIME(UCombatComponent, SparkPower);
	DOREPLIFETIME(UCombatComponent, ClearedSectors);
	DOREPLIFETIME(UCombatComponent, Cash);
	DOREPLIFETIME(UCombatComponent, ComboCount);
	DOREPLIFETIME(UCombatComponent, bGuarding);
	DOREPLIFETIME(UCombatComponent, GuardMeter);
	DOREPLIFETIME(UCombatComponent, GuardBreakTimer);
	DOREPLIFETIME(UCombatComponent, CounterWindow);
	DOREPLIFETIME(UCombatComponent, bDowned);
	DOREPLIFETIME(UCombatComponent, DownedTimeRemaining);
	DOREPLIFETIME(UCombatComponent, ActiveAttackType);
	DOREPLIFETIME(UCombatComponent, SwingU);
}

bool UCombatComponent::HasPower(EKomorebiPower Power) const
{
	if (WorldScene == EKomorebiWorldScene::Snowbound || IsRoamMode())
	{
		return true;
	}
	const KomorebiCore::PowerSet Earned = KomorebiCore::PowersFor(ClearedSectors);
	return Earned.Has(KomorebiBridge::ToCorePowerKind(Power));
}

bool UCombatComponent::IsInAttackHitWindow() const
{
	if (SwingU < 0.0f)
	{
		return false;
	}
	const KomorebiCore::AttackSpec& Spec = KomorebiCore::GetAttackSpec(KomorebiBridge::ToCoreAttackType(ActiveAttackType));
	return SwingU >= static_cast<float>(Spec.HitStart) && SwingU <= static_cast<float>(Spec.HitEnd);
}

bool UCombatComponent::IsInAttackWindup() const
{
	if (SwingU <= 0.08f)
	{
		return false;
	}
	const KomorebiCore::AttackSpec& Spec = KomorebiCore::GetAttackSpec(KomorebiBridge::ToCoreAttackType(ActiveAttackType));
	return SwingU < static_cast<float>(Spec.HitStart);
}

float UCombatComponent::GetTimeScale() const
{
	if (IsOnlineMode())
	{
		return 1.0f;
	}
	if (HitPauseTimer > 0.0f)
	{
		return kHitStopTimeScale;
	}
	if (SlowMoTimer > 0.0f)
	{
		return kSlowMoTimeScale;
	}
	return 1.0f;
}

float UCombatComponent::GetCurrentSwingMultiplier() const
{
	if (bSwingHeavy) return 3.0f;
	if (ActiveAttackType == EKomorebiAttackType::Spin) return static_cast<float>(KomorebiCore::kWhirlMul);
	if (ActiveAttackType == EKomorebiAttackType::Counter) return 1.65f;
	if (ActiveAttackType == EKomorebiAttackType::Thrust) return 1.3f;
	return 1.0f;
}

FName UCombatComponent::GetCurrentSwingSource() const
{
	if (bSwingHeavy) return TEXT("heavy");
	if (ActiveAttackType == EKomorebiAttackType::Spin) return TEXT("whirl");
	if (ActiveAttackType == EKomorebiAttackType::Thrust) return TEXT("thrust");
	return TEXT("sword");
}

FVector UCombatComponent::GetOwnerFeetWorld() const
{
	if (const AKaelCharacter* Kael = Cast<AKaelCharacter>(GetOwner()))
	{
		return Kael->GetFeetWorldLocation();
	}
	return GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
}

float UCombatComponent::GetOwnerYawDeg() const
{
	return GetOwner() ? GetOwner()->GetActorRotation().Yaw : 0.0f;
}

float UCombatComponent::QueryGroundHeightCm(const FVector& WorldPos) const
{
	if (const UWorld* World = GetWorld())
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CombatGroundCheck), false, GetOwner());
		const FVector Start(WorldPos.X, WorldPos.Y, WorldPos.Z + 200.0f);
		const FVector End(WorldPos.X, WorldPos.Y, WorldPos.Z - 4000.0f);
		if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
		{
			return Hit.ImpactPoint.Z;
		}
	}
	return WorldPos.Z;
}

void UCombatComponent::EmitToast(const FString& Msg)
{
	if (!Msg.IsEmpty())
	{
		OnToast.Broadcast(Msg);
	}
}

void UCombatComponent::EmitFlash(const FString& Text)
{
	if (!Text.IsEmpty())
	{
		OnFlashText.Broadcast(Text);
	}
}

void UCombatComponent::EmitAudio(FName Cue, float Scale)
{
	OnAudioCue.Broadcast(Cue, Scale);
}

void UCombatComponent::ClearFocus()
{
	FocusTarget.Reset();
	FocusBlockedTimer = 0.0f;
}

void UCombatComponent::AddCameraKick(float Amount)
{
	CameraKick = FMath::Max(CameraKick, Amount);
}

void UCombatComponent::Heal(float Amount)
{
	if (Amount > 0.0f && !bDowned)
	{
		Hp = FMath::Min(MaxHp, Hp + Amount);
	}
}

void UCombatComponent::RefillFullResources()
{
	Hp = MaxHp;
	SparkPower = MaxSparkPower;
	GuardMeter = 1.0f;
	GuardBreakTimer = 0.0f;
}

void UCombatComponent::GrantReward(int32 CashDelta, float SparkDelta, const FString& FlashLabel, const FString& ToastLabel)
{
	Cash += CashDelta;
	if (SparkDelta > 0.0f)
	{
		SparkPower = FMath::Min(MaxSparkPower, SparkPower + SparkDelta);
	}
	EmitFlash(FlashLabel);
	EmitToast(ToastLabel);
}

bool UCombatComponent::TryRevive()
{
	if (!bDowned)
	{
		return false;
	}
	bDowned = false;
	DownedTimeRemaining = 0.0f;
	Hp = 45.0f;
	InvulnTimer = 1.5f;
	HurtPose = 0.0f;
	EmitToast(TEXT("Back on your feet"));
	return true;
}

void UCombatComponent::TriggerDeath(bool bForceRespawn)
{
	ClearFocus();
	bGuarding = false;
	GuardMeter = 1.0f;
	GuardBreakTimer = 0.0f;
	CounterWindow = 0.0f;

	if (IsOnlineMode() && !bForceRespawn)
	{
		if (bDowned)
		{
			return;
		}
		bDowned = true;
		DownedTimeRemaining = kOnlineDownedDuration;
		Hp = 0.0f;
		InvulnTimer = kOnlineDownedDuration + 1.0f;
		SwingU = -1.0f;
		bSwingHeavy = false;
		AttackBufferTimer = 0.0f;
		EmitToast(TEXT("Down · a teammate can revive you by standing close"));
		OnPlayerDied.Broadcast(true);
		return;
	}

	bDowned = false;
	DownedTimeRemaining = 0.0f;
	Hp = MaxHp;
	InvulnTimer = 2.6f;
	SwingU = -1.0f;
	bSwingHeavy = false;
	AttackBufferTimer = 0.0f;
	bPlungeActive = false;

	// Clear hostile bolts
	ActiveBolts.RemoveAll([](const FKomorebiActiveBolt& Bolt) { return Bolt.HostileDamage > 0.0f; });

	if (UWorld* World = GetWorld())
	{
		if (UEnemySquadSubsystem* Squad = World->GetSubsystem<UEnemySquadSubsystem>())
		{
			Squad->ResetAfterPlayerDeath(GetOwnerFeetWorld(), 500.0f);
		}
	}

	EmitToast(WorldScene == EKomorebiWorldScene::NeoKyoto
		? TEXT("Back to Mission Relay")
		: TEXT("Back to the last checkpoint"));
	OnPlayerDied.Broadcast(false);
}

void UCombatComponent::ApplyVehicleCrashDamage(float Damage, float CameraJolt)
{
	AddCameraKick(CameraJolt);
	if (Damage <= 0.0f || bDowned)
	{
		return;
	}
	Hp -= Damage;
	HurtPose = 1.0f;
	EmitAudio(TEXT("playerHurt"));
	if (Hp <= 0.0f)
	{
		TriggerDeath(false);
	}
}

void UCombatComponent::ApplyGuiltAndCarjack(bool bIsCarjack, bool bWasKilled, bool bWasProvoked)
{
	if (!bIsCarjack && bWasProvoked)
	{
		return;
	}

	const float PenaltyHp = static_cast<float>(KomorebiCore::kGuilt * ((!bIsCarjack && bWasKilled) ? 2 : 1));
	Hp = FMath::Max(1.0f, Hp - PenaltyHp);

	const float HeatDelta = bIsCarjack
		? static_cast<float>(KomorebiCore::kHeatConfig.Carjack)
		: static_cast<float>(bWasKilled ? KomorebiCore::kHeatConfig.Kill : KomorebiCore::kHeatConfig.Hurt);

	if (UWorld* World = GetWorld())
	{
		if (UEnemySquadSubsystem* Squad = World->GetSubsystem<UEnemySquadSubsystem>())
		{
			Squad->AddHeat(HeatDelta);
		}
	}

	AddCameraKick(bIsCarjack ? 0.12f : (bWasKilled ? 0.22f : 0.09f));
	EmitAudio(TEXT("guilt"));

	if (!bIsCarjack && ComboCount > 0)
	{
		ComboCount = 0;
		OnComboChanged.Broadcast(0);
	}

	EmitFlash(TEXT("Innocent!"));
	if (bIsCarjack)
	{
		EmitToast(TEXT("That driver did not hand you the keys."));
	}
}

void UCombatComponent::SpawnHostileBolt(const FVector& Origin, const FVector& VelocityCmPerSec, float Damage, AActor* Shooter)
{
	FKomorebiActiveBolt Bolt;
	Bolt.Location = Origin;
	Bolt.VelocityCm = VelocityCmPerSec;
	Bolt.GravityCm = 0.0f; // Gunner shots fly flat (hunt.js line 882: g: 0)
	Bolt.LifeRemaining = 1.3f;
	Bolt.HostileDamage = Damage;
	Bolt.Shooter = Shooter;
	ActiveBolts.Add(Bolt);
	EmitAudio(TEXT("bolt"));
}

bool UCombatComponent::ReceiveEnemyHit(float Damage, AActor* Attacker, float ShoveMps, bool bUnblockable)
{
	if (InvulnTimer > 0.0f || bDowned)
	{
		return false;
	}

	const FVector PlayerFeet = GetOwnerFeetWorld();
	const KomorebiCore::Vec3 PlayerThree = KomorebiBridge::FVectorToThree(PlayerFeet);
	const double PlayerThreeYaw = KomorebiCore::UEYawDegToThreeYaw(GetOwnerYawDeg());

	// Guard resolution matching hunt.js lines 643-660
	if (bGuarding && Attacker && !bUnblockable)
	{
		const KomorebiCore::Vec3 AttackerThree = KomorebiBridge::FVectorToThree(Attacker->GetActorLocation());
		if (KomorebiCore::InFront(PlayerThree.X, PlayerThree.Z, PlayerThreeYaw, AttackerThree.X, AttackerThree.Z, 1e6, 1.15, 0.0))
		{
			const bool bTimedGuard = GuardAge <= 0.17f;
			const float Cost = bTimedGuard ? 0.12f : FMath::Max(0.29f, Damage / 70.0f);
			GuardImpact = 1.0f;

			if (GuardMeter >= Cost)
			{
				GuardMeter -= Cost;
				InvulnTimer = 0.25f;
				if (bTimedGuard)
				{
					CounterWindow = 1.05f;
					SparkPower = FMath::Min(MaxSparkPower, SparkPower + 0.35f);
				}
				else
				{
					Hp -= Damage * 0.2f;
				}
				EmitAudio(TEXT("parry"));
				EmitFlash(bTimedGuard ? TEXT("Perfect guard") : TEXT("Guarded"));
				if (Hp <= 0.0f)
				{
					TriggerDeath(false);
				}
				return true;
			}

			bGuarding = false;
			GuardMeter = 0.0f;
			GuardBreakTimer = 0.9f;
			EmitFlash(TEXT("Guard broken"));
		}
	}

	Hp -= Damage;
	InvulnTimer = 0.7f;
	HurtPose = 1.0f;
	HitPauseTimer = FMath::Max(HitPauseTimer, 0.07f);
	AddCameraKick(0.16f);
	EmitAudio(TEXT("playerHurt"));
	EmitAudio(TEXT("bark_hurt"));

	if (ComboCount > 0)
	{
		ComboCount = 0;
		OnComboChanged.Broadcast(0);
	}

	if (Attacker)
	{
		if (const AKaelCharacter* Kael = Cast<AKaelCharacter>(GetOwner()))
		{
			if (UKaelMovementComponent* KaelMove = Kael->GetKaelMovement())
			{
				const FVector PushDir = (PlayerFeet - Attacker->GetActorLocation()).GetSafeNormal2D();
				KaelMove->ApplyKnockbackImpulse(PushDir, ShoveMps * 100.0f);
			}
		}
	}

	if (Hp <= 0.0f)
	{
		TriggerDeath(false);
	}
	return true;
}

void UCombatComponent::NotifyPlayerHitConnected(FName Source, bool bGuardedTarget, bool bIsBrute)
{
	++ComboCount;
	ComboDecayTimer = 2.2f;
	OnComboChanged.Broadcast(ComboCount);

	if (!bGuardedTarget || Source == TEXT("flare"))
	{
		const bool bBigHit = (Source == TEXT("heavy") || Source == TEXT("whirl") || Source == TEXT("thrust"));
		HitPauseTimer = FMath::Max(HitPauseTimer, (bBigHit ? 0.075f : 0.045f) + FMath::Min(ComboCount, 8) * 0.004f);
		CameraKick = bBigHit ? 0.12f : 0.055f;
		EmitAudio(Source == TEXT("heavy") ? TEXT("heavy") : TEXT("hit"), bIsBrute ? 0.72f : 1.0f);
	}
}

bool UCombatComponent::IsTargetViable(AActor* Candidate, float MaxRangeMeters) const
{
	if (!Candidate || !GetWorld())
	{
		return false;
	}

	const FVector PlayerFeet = GetOwnerFeetWorld();
	FVector TargetFeet = Candidate->GetActorLocation();

	if (const AStreetEnemyCharacter* Enemy = Cast<AStreetEnemyCharacter>(Candidate))
	{
		if (Enemy->IsDead() || Enemy->IsGone())
		{
			return false;
		}
		if (Enemy->IsCiv() && !Enemy->IsAngry())
		{
			return false;
		}
		TargetFeet = Enemy->GetFeetWorldLocation();
	}
	else if (const ARenBossCharacter* Boss = Cast<ARenBossCharacter>(Candidate))
	{
		if (Boss->IsDead() || !Boss->IsReady())
		{
			return false;
		}
		TargetFeet = Boss->GetFeetWorldLocation();
	}
	else
	{
		return false;
	}

	if (const UEnemySquadSubsystem* Squad = GetWorld()->GetSubsystem<UEnemySquadSubsystem>())
	{
		if (Squad->IsGateBlockingBetween(PlayerFeet, TargetFeet))
		{
			return false;
		}
	}

	const float DistMeters = FVector::Dist2D(PlayerFeet, TargetFeet) * 0.01f;
	const float HeightDiffMeters = FMath::Abs(TargetFeet.Z - PlayerFeet.Z) * 0.01f;
	return DistMeters <= MaxRangeMeters && HeightDiffMeters < 5.0f;
}

bool UCombatComponent::HasLineOfSightTo(AActor* Candidate) const
{
	if (!Candidate || !GetWorld())
	{
		return false;
	}
	const FVector Chest = GetOwnerFeetWorld() + FVector(0.0f, 0.0f, 125.0f);
	const FVector TargetPt = Candidate->GetActorLocation() + FVector(0.0f, 0.0f, 20.0f);
	const float Dist = FVector::Dist(Chest, TargetPt);
	if (Dist < 40.0f)
	{
		return true;
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CombatFocusLOS), false, GetOwner());
	Params.AddIgnoredActor(Candidate);
	const FVector End = Chest + (TargetPt - Chest).GetSafeNormal() * (Dist - 20.0f);
	return !GetWorld()->LineTraceSingleByChannel(Hit, Chest, End, ECC_WorldStatic, Params);
}

void UCombatComponent::UpdateLockOn(float DeltaTime, const FKomorebiCombatFrameInput& Input, bool bAllowFocus)
{
	if (!bAllowFocus || (FocusTarget.IsValid() && !IsTargetViable(FocusTarget.Get(), 27.0f)))
	{
		ClearFocus();
	}

	if (FocusTarget.IsValid())
	{
		FocusBlockedTimer = HasLineOfSightTo(FocusTarget.Get()) ? 0.0f : (FocusBlockedTimer + DeltaTime);
		if (FocusBlockedTimer > 0.35f)
		{
			ClearFocus();
		}
	}

	if (!bAllowFocus || (!Input.bLockTargetPressed && !Input.bNextTargetPressed))
	{
		return;
	}

	if (Input.bLockTargetPressed && FocusTarget.IsValid())
	{
		ClearFocus();
		return;
	}

	const UWorld* World = GetWorld();
	const UEnemySquadSubsystem* Squad = World ? World->GetSubsystem<UEnemySquadSubsystem>() : nullptr;
	if (!Squad)
	{
		return;
	}

	struct FChoice
	{
		AActor* Actor = nullptr;
		float AngleRad = 0.0f;
		float DistanceM = 0.0f;
	};
	TArray<FChoice> Choices;

	const FVector PlayerFeet = GetOwnerFeetWorld();
	auto Consider = [&](AActor* Actor)
	{
		if (!IsTargetViable(Actor, 22.0f) || !HasLineOfSightTo(Actor))
		{
			return;
		}
		const FVector ToTarget = Actor->GetActorLocation() - PlayerFeet;
		const float TargetYawDeg = ToTarget.Rotation().Yaw;
		const float AngleRad = FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(Input.CameraYawDeg, TargetYawDeg));
		if (FMath::Abs(AngleRad) <= 1.3f || Actor == FocusTarget.Get())
		{
			Choices.Add(FChoice{ Actor, AngleRad, ToTarget.Size2D() * 0.01f });
		}
	};

	for (const TWeakObjectPtr<AStreetEnemyCharacter>& WeakEnemy : Squad->GetRegisteredEnemies())
	{
		if (AStreetEnemyCharacter* Enemy = WeakEnemy.Get())
		{
			Consider(Enemy);
		}
	}
	for (const TWeakObjectPtr<ARenBossCharacter>& WeakBoss : Squad->GetRegisteredRivals())
	{
		if (ARenBossCharacter* Boss = WeakBoss.Get())
		{
			Consider(Boss);
		}
	}

	if (FocusTarget.IsValid() && Input.bNextTargetPressed)
	{
		Choices.Sort([](const FChoice& A, const FChoice& B)
		{
			if (!FMath::IsNearlyEqual(A.AngleRad, B.AngleRad))
			{
				return A.AngleRad < B.AngleRad;
			}
			return A.DistanceM < B.DistanceM;
		});
		const int32 CurIdx = Choices.IndexOfByPredicate([&](const FChoice& C) { return C.Actor == FocusTarget.Get(); });
		if (Choices.Num() > 0)
		{
			const int32 NextIdx = (CurIdx >= 0) ? ((CurIdx + 1) % Choices.Num()) : 0;
			FocusTarget = Choices[NextIdx].Actor;
		}
	}
	else
	{
		Choices.Sort([](const FChoice& A, const FChoice& B)
		{
			const float ScoreA = FMath::Abs(A.AngleRad) * 12.0f + A.DistanceM;
			const float ScoreB = FMath::Abs(B.AngleRad) * 12.0f + B.DistanceM;
			return ScoreA < ScoreB;
		});
		if (Choices.Num() > 0)
		{
			FocusTarget = Choices[0].Actor;
		}
		else
		{
			ClearFocus();
			EmitFlash(TEXT("Aim toward a hostile"));
		}
	}
	FocusBlockedTimer = 0.0f;
}

void UCombatComponent::ProcessPreMovementCombatInput(
	float DeltaTime,
	const FKomorebiCombatFrameInput& Input,
	bool bAllowCombat,
	bool& OutForceStrafe,
	bool& OutForceWalk,
	bool& OutHasCombatAimYaw,
	float& OutCombatAimYawDeg)
{
	OutForceStrafe = false;
	OutForceWalk = false;
	OutHasCombatAimYaw = false;
	OutCombatAimYawDeg = Input.CameraYawDeg;

	EvadeCooldown = FMath::Max(0.0f, EvadeCooldown - DeltaTime);
	GuardBreakTimer = FMath::Max(0.0f, GuardBreakTimer - DeltaTime);
	GuardImpact = FMath::Max(0.0f, GuardImpact - DeltaTime * 6.0f);
	CounterWindow = FMath::Max(0.0f, CounterWindow - DeltaTime);

	if (!bAllowCombat || Hp <= 0.0f || bDowned)
	{
		ClearFocus();
		bGuarding = false;
		return;
	}

	UpdateLockOn(DeltaTime, Input, bAllowCombat);

	const AKaelCharacter* Kael = Cast<AKaelCharacter>(GetOwner());
	UKaelMovementComponent* KaelMove = Kael ? Kael->GetKaelMovement() : nullptr;
	const FVector PlayerFeet = GetOwnerFeetWorld();

	float AimYawDeg = Input.CameraYawDeg;
	if (AActor* Focus = FocusTarget.Get())
	{
		AimYawDeg = (Focus->GetActorLocation() - PlayerFeet).Rotation().Yaw;
	}

	// Quickstep Evade (hunt.js lines 964-974)
	const KomorebiCore::AttackSpec& ActiveSpec = KomorebiCore::GetAttackSpec(KomorebiBridge::ToCoreAttackType(ActiveAttackType));
	const bool bRecoverable = (SwingU < 0.0f) || (SwingU > static_cast<float>(ActiveSpec.HitEnd));
	if (Input.bEvadePressed && bRecoverable && EvadeCooldown <= 0.0f && KaelMove)
	{
		FVector EvadeDir = YawDegToForwardXY(AimYawDeg) * Input.MoveInput.Y + YawDegToRightXY(AimYawDeg) * Input.MoveInput.X;
		if (Input.MoveInput.Size() <= 0.08f || EvadeDir.IsNearlyZero())
		{
			const float BackYaw = FocusTarget.IsValid() ? AimYawDeg : GetOwnerYawDeg();
			EvadeDir = -YawDegToForwardXY(BackYaw);
		}
		else
		{
			EvadeDir.Normalize();
		}

		if (KaelMove->StartEvade(EvadeDir))
		{
			SwingU = -1.0f;
			bSwingHeavy = false;
			AttackBufferTimer = 0.0f;
			EvadeCooldown = 0.82f;
			InvulnTimer = FMath::Max(InvulnTimer, 0.17f);
			EmitAudio(TEXT("dash"), 0.65f);
			CheckPerfectDodge(true);
		}
	}

	// Guard hold / meter regen (hunt.js lines 976-979)
	const bool bGrounded = KaelMove ? KaelMove->IsMovingOnGround() : true;
	const bool bMovingSpecial = KaelMove && (KaelMove->IsDashing() || KaelMove->IsEvading());
	const bool bHeld = Input.bGuardHeld
		&& !Input.bAttackPressed
		&& !Input.bHeavyPressed
		&& bGrounded
		&& !bMovingSpecial
		&& SwingU < 0.0f
		&& GuardBreakTimer <= 0.0f;

	GuardAge = (bHeld && bGuarding) ? (GuardAge + DeltaTime) : 0.0f;
	bGuarding = bHeld;

	if (!Input.bGuardHeld && GuardBreakTimer <= 0.0f)
	{
		GuardMeter = FMath::Min(1.0f, GuardMeter + DeltaTime * 0.55f);
	}

	OutHasCombatAimYaw = FocusTarget.IsValid();
	OutCombatAimYawDeg = AimYawDeg;
	OutForceStrafe = FocusTarget.IsValid() || bGuarding || (KaelMove && KaelMove->IsEvading());
	OutForceWalk = bGuarding;
}

void UCombatComponent::CheckPerfectDodge(bool bIsQuickstepEvade)
{
	const UWorld* World = GetWorld();
	const UEnemySquadSubsystem* Squad = World ? World->GetSubsystem<UEnemySquadSubsystem>() : nullptr;
	if (!Squad)
	{
		return;
	}

	const FVector PlayerFeet = GetOwnerFeetWorld();
	bool bImminent = false;

	for (const TWeakObjectPtr<AStreetEnemyCharacter>& WeakEnemy : Squad->GetRegisteredEnemies())
	{
		const AStreetEnemyCharacter* Enemy = WeakEnemy.Get();
		if (!Enemy || Enemy->IsDead() || Enemy->IsGone())
		{
			continue;
		}
		if (Squad->IsGateBlockingBetween(PlayerFeet, Enemy->GetFeetWorldLocation()))
		{
			continue;
		}
		const float DistM = FVector::Dist2D(PlayerFeet, Enemy->GetFeetWorldLocation()) * 0.01f;
		if (Enemy->WindupRemaining > 0.0f
			&& Enemy->WindupRemaining < static_cast<float>(KomorebiCore::kPerfectWindow)
			&& DistM < Enemy->GetActiveMoveReachMeters() + 1.0f)
		{
			bImminent = true;
			break;
		}
	}

	if (!bImminent)
	{
		for (const TWeakObjectPtr<ARenBossCharacter>& WeakBoss : Squad->GetRegisteredRivals())
		{
			const ARenBossCharacter* Boss = WeakBoss.Get();
			if (!Boss || Boss->IsDead() || !Boss->IsReady())
			{
				continue;
			}
			const KomorebiCore::DuelState& State = Boss->GetCoreState();
			const float DistM = FVector::Dist2D(PlayerFeet, Boss->GetFeetWorldLocation()) * 0.01f;
			const bool bDanger =
				(State.Phase == KomorebiCore::EDuelPhase::Windup && State.Timer < KomorebiCore::kPerfectWindow)
				|| (State.Phase == KomorebiCore::EDuelPhase::Active && !State.HitDone);
			if (bDanger && DistM < Boss->GetCurrentMoveReachMeters() + 1.0f)
			{
				bImminent = true;
				break;
			}
		}
	}

	if (!bImminent)
	{
		return;
	}

	SlowMoTimer = bIsQuickstepEvade ? 0.35f : 0.7f;
	if (!bIsQuickstepEvade)
	{
		InvulnTimer = FMath::Max(InvulnTimer, 0.6f);
	}
	SparkPower = FMath::Min(MaxSparkPower, SparkPower + (bIsQuickstepEvade ? 0.5f : 1.0f));
	EmitAudio(TEXT("dodge"));
	OnPulseVFX.Broadcast(PlayerFeet + FVector(0.0f, 0.0f, 100.0f), FLinearColor::White, false);
	EmitFlash(TEXT("Perfect dodge"));
}

void UCombatComponent::TryStartWhirl()
{
	if (!HasPower(EKomorebiPower::Whirl))
	{
		if (LockToastCooldown <= 0.0f)
		{
			LockToastCooldown = 4.0f;
			EmitToast(TEXT("Clear Sector 5 to learn the whirl"));
		}
		return;
	}

	if (SwingU >= 0.0f && SwingU <= 0.6f)
	{
		return;
	}

	const std::optional<double> NextPower = KomorebiCore::SpendPower(SparkPower);
	if (!NextPower.has_value())
	{
		if (LockToastCooldown <= 0.0f)
		{
			LockToastCooldown = 3.0f;
			EmitToast(TEXT("The whirl needs one spark"));
		}
		return;
	}

	SparkPower = static_cast<float>(*NextPower);
	ActiveAttackType = EKomorebiAttackType::Spin;
	SwingU = 0.0f;
	bSwingHeavy = false;
	StruckThisSwing.Reset();
	AttackBufferTimer = 0.0f;
	ComboWindowTimer = 0.0f;
	EmitAudio(TEXT("whirl"));

	const FVector Feet = GetOwnerFeetWorld();
	OnPulseVFX.Broadcast(FVector(Feet.X, Feet.Y, QueryGroundHeightCm(Feet) + 6.0f), FLinearColor(0.788f, 0.659f, 1.0f, 1.0f), true);
}

void UCombatComponent::TryConsumeAttackBuffer(const FKomorebiCombatFrameInput& Input)
{
	if (SwingU >= 0.0f || AttackBufferTimer <= 0.0f || Hp <= 0.0f)
	{
		return;
	}

	ComboIndex = (ComboWindowTimer > 0.0f) ? ((ComboIndex + 1) % 3) : 0;
	static constexpr EKomorebiAttackType ComboChain[3] = {
		EKomorebiAttackType::Cut,
		EKomorebiAttackType::Backhand,
		EKomorebiAttackType::Thrust
	};
	ActiveAttackType = ComboChain[ComboIndex];

	if (bQueuedHeavy)
	{
		ActiveAttackType = EKomorebiAttackType::Heavy;
	}
	else if (CounterWindow > 0.0f)
	{
		ActiveAttackType = EKomorebiAttackType::Counter;
		CounterWindow = 0.0f;
	}
	else if (FocusTarget.IsValid() && FocusBlockedTimer <= 0.0f && ComboWindowTimer <= 0.0f && Input.MoveInput.Y > 0.62f)
	{
		ActiveAttackType = EKomorebiAttackType::Advance;
	}

	SwingU = 0.0f;
	bSwingHeavy = bQueuedHeavy;
	StruckThisSwing.Reset();
	AttackBufferTimer = 0.0f;

	AKaelCharacter* Kael = Cast<AKaelCharacter>(GetOwner());
	UKaelMovementComponent* KaelMove = Kael ? Kael->GetKaelMovement() : nullptr;
	const FVector PlayerFeet = GetOwnerFeetWorld();
	const float GroundZ = QueryGroundHeightCm(PlayerFeet);

	// Airborne plunge check (> 0.7 m above ground)
	bPlungeActive = KaelMove && !KaelMove->IsMovingOnGround() && (PlayerFeet.Z - GroundZ) > 70.0f;
	if (bPlungeActive)
	{
		ActiveAttackType = EKomorebiAttackType::Heavy;
		bSwingHeavy = false;
		KaelMove->StartPlungeSlam(2600.0f);
	}

	if (bQueuedHeavy || bPlungeActive)
	{
		EmitAudio(TEXT("heavy"));
		EmitAudio(TEXT("bark_heavy"));
	}
	else
	{
		EmitAudio(TEXT("slash"), ActiveAttackType == EKomorebiAttackType::Thrust ? 0.85f : 1.0f);
		EmitAudio(TEXT("bark_swing"));
	}

	if (bPlungeActive || !Kael)
	{
		return;
	}

	// Auto-aim + swept lunge gap close toward focus target or nearest enemy in front within 3.6m (hunt.js lines 1057-1066)
	AActor* BestTarget = (FocusBlockedTimer <= 0.0f) ? FocusTarget.Get() : nullptr;
	bool bTargetIsBrute = false;

	if (!BestTarget)
	{
		if (const UEnemySquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<UEnemySquadSubsystem>() : nullptr)
		{
			const KomorebiCore::Vec3 PlayerThree = KomorebiBridge::FVectorToThree(PlayerFeet);
			const double PlayerThreeYaw = KomorebiCore::UEYawDegToThreeYaw(GetOwnerYawDeg());
			float BestDistCm = 360.0f;

			for (const TWeakObjectPtr<AStreetEnemyCharacter>& WeakEnemy : Squad->GetRegisteredEnemies())
			{
				AStreetEnemyCharacter* Enemy = WeakEnemy.Get();
				if (!Enemy || Enemy->IsDead() || Enemy->IsGone() || (Enemy->IsCiv() && !Enemy->IsAngry()))
				{
					continue;
				}
				const FVector EnemyFeet = Enemy->GetFeetWorldLocation();
				if (Squad->IsGateBlockingBetween(PlayerFeet, EnemyFeet))
				{
					continue;
				}
				const KomorebiCore::Vec3 EnemyThree = KomorebiBridge::FVectorToThree(EnemyFeet);
				if (KomorebiCore::InFront(PlayerThree.X, PlayerThree.Z, PlayerThreeYaw, EnemyThree.X, EnemyThree.Z, 3.6, 1.8))
				{
					const float D = FVector::Dist2D(PlayerFeet, EnemyFeet);
					if (D < BestDistCm)
					{
						BestDistCm = D;
						BestTarget = Enemy;
					}
				}
			}

			for (const TWeakObjectPtr<ARenBossCharacter>& WeakBoss : Squad->GetRegisteredRivals())
			{
				ARenBossCharacter* Boss = WeakBoss.Get();
				if (!Boss || Boss->IsDead() || !Boss->IsReady())
				{
					continue;
				}
				const FVector BossFeet = Boss->GetFeetWorldLocation();
				const KomorebiCore::Vec3 BossThree = KomorebiBridge::FVectorToThree(BossFeet);
				if (KomorebiCore::InFront(PlayerThree.X, PlayerThree.Z, PlayerThreeYaw, BossThree.X, BossThree.Z, 3.6, 1.8))
				{
					const float D = FVector::Dist2D(PlayerFeet, BossFeet);
					if (D < BestDistCm)
					{
						BestDistCm = D;
						BestTarget = Boss;
					}
				}
			}
		}
	}

	if (BestTarget)
	{
		if (const AStreetEnemyCharacter* EnemyTarget = Cast<AStreetEnemyCharacter>(BestTarget))
		{
			bTargetIsBrute = (EnemyTarget->EnemyKind == EKomorebiEnemyKind::Brute);
		}

		const FVector ToTarget = BestTarget->GetActorLocation() - PlayerFeet;
		const float AimYawDeg = ToTarget.Rotation().Yaw;
		const float CurrentYawDeg = Kael->GetActorRotation().Yaw;
		const float MaxTurnDeg = FMath::RadiansToDegrees(0.7f);
		const float DeltaDeg = FMath::FindDeltaAngleDegrees(CurrentYawDeg, AimYawDeg);
		const float NewYawDeg = (BestTarget == FocusTarget.Get())
			? AimYawDeg
			: FMath::UnwindDegrees(CurrentYawDeg + FMath::Clamp(DeltaDeg, -MaxTurnDeg, MaxTurnDeg));

		FRotator Rot = Kael->GetActorRotation();
		Rot.Yaw = NewYawDeg;
		Kael->SetActorRotation(Rot);

		const float DistM = ToTarget.Size2D() * 0.01f;
		const float GapM = DistM - (bTargetIsBrute ? 2.1f : 1.5f);
		const float StrideM = (ActiveAttackType == EKomorebiAttackType::Advance) ? 2.6f : 1.6f;
		const KomorebiCore::AttackSpec& Spec = KomorebiCore::GetAttackSpec(KomorebiBridge::ToCoreAttackType(ActiveAttackType));
		if (KaelMove && GapM > 0.1f && DistM <= static_cast<float>(Spec.Reach) + StrideM - 0.5f)
		{
			const float ImpulseCmPerSec = FMath::Min(GapM, StrideM) * 8.4f * 100.0f;
			KaelMove->ApplyKnockbackImpulse(ToTarget.GetSafeNormal2D(), ImpulseCmPerSec);
		}
	}
}

void UCombatComponent::EvaluateMeleeHitWindow()
{
	const UWorld* World = GetWorld();
	const UEnemySquadSubsystem* Squad = World ? World->GetSubsystem<UEnemySquadSubsystem>() : nullptr;
	const FVector PlayerFeet = GetOwnerFeetWorld();
	const float YawDeg = GetOwnerYawDeg();
	const KomorebiCore::Vec3 PlayerThree = KomorebiBridge::FVectorToThree(PlayerFeet);
	const double PlayerThreeYaw = KomorebiCore::UEYawDegToThreeYaw(YawDeg);
	const KomorebiCore::AttackSpec& Spec = KomorebiCore::GetAttackSpec(KomorebiBridge::ToCoreAttackType(ActiveAttackType));

	const float SwingMult = GetCurrentSwingMultiplier();
	const FName Source = GetCurrentSwingSource();

	// Notify MissionSubsystem listeners (destructible nodes / objectives)
	const FVector StrikeCenter = PlayerFeet + YawDegToForwardXY(YawDeg) * 140.0f;
	OnMissionStrike.Broadcast(StrikeCenter, static_cast<float>(Spec.Reach * 100.0), SwingMult * 34.0f, Source, ClearedSectors);

	if (!Squad)
	{
		return;
	}

	const double SwingArc = (ActiveAttackType == EKomorebiAttackType::Spin)
		? KomorebiCore::kPi
		: (bSwingHeavy ? 1.6 : 1.2);

	for (const TWeakObjectPtr<AStreetEnemyCharacter>& WeakEnemy : Squad->GetRegisteredEnemies())
	{
		AStreetEnemyCharacter* Enemy = WeakEnemy.Get();
		if (!Enemy || Enemy->IsDead() || Enemy->IsGone() || StruckThisSwing.Contains(Enemy))
		{
			continue;
		}
		if (Enemy == FocusTarget.Get() && FocusBlockedTimer > 0.0f)
		{
			continue;
		}
		const FVector EnemyFeet = Enemy->GetFeetWorldLocation();
		if (Squad->IsGateBlockingBetween(PlayerFeet, EnemyFeet))
		{
			continue;
		}
		if (FMath::Abs(PlayerFeet.Z - EnemyFeet.Z) >= 170.0f)
		{
			continue;
		}

		const KomorebiCore::Vec3 EnemyThree = KomorebiBridge::FVectorToThree(EnemyFeet);
		if (!KomorebiCore::InFront(PlayerThree.X, PlayerThree.Z, PlayerThreeYaw, EnemyThree.X, EnemyThree.Z, Spec.Reach, SwingArc))
		{
			continue;
		}

		StruckThisSwing.Add(Enemy);

		// Parry check: melee strike landing inside the final kParryWindow (0.28s) of enemy melee windup
		const bool bParry = !Enemy->IsFiringRangedAttack()
			&& Enemy->WindupRemaining > 0.0f
			&& Enemy->WindupRemaining < static_cast<float>(KomorebiCore::kParryWindow);

		const float BaseSwordDmg = static_cast<float>(KomorebiCore::GetSwordDamage(KomorebiBridge::ToCoreEnemyKind(Enemy->EnemyKind)));
		const float FinalDmg = SwingMult * BaseSwordDmg * (bParry ? static_cast<float>(KomorebiCore::kParryMul) : 1.0f);

		if (Enemy->ReceiveDamage(FinalDmg, Source, PlayerFeet, GetOwner(), bParry))
		{
			if (bParry && !Enemy->IsDead())
			{
				Enemy->ForceStagger(1.2f);
				SparkPower = FMath::Min(MaxSparkPower, SparkPower + 0.5f);
				SlowMoTimer = FMath::Max(SlowMoTimer, 0.25f);
				EmitAudio(TEXT("parry"));
				EmitAudio(TEXT("bark_parry"));
				EmitFlash(TEXT("Parry"));
			}
		}
	}

	// Mirror Rival hits
	const double RivalArc = (ActiveAttackType == EKomorebiAttackType::Spin) ? KomorebiCore::kPi : 1.3;
	for (const TWeakObjectPtr<ARenBossCharacter>& WeakBoss : Squad->GetRegisteredRivals())
	{
		ARenBossCharacter* Boss = WeakBoss.Get();
		if (!Boss || Boss->IsDead() || !Boss->IsReady() || StruckThisSwing.Contains(Boss))
		{
			continue;
		}
		if (Boss == FocusTarget.Get() && FocusBlockedTimer > 0.0f)
		{
			continue;
		}
		const FVector BossFeet = Boss->GetFeetWorldLocation();
		if (Squad->IsGateBlockingBetween(PlayerFeet, BossFeet))
		{
			continue;
		}

		const KomorebiCore::Vec3 BossThree = KomorebiBridge::FVectorToThree(BossFeet);
		if (!KomorebiCore::InFront(PlayerThree.X, PlayerThree.Z, PlayerThreeYaw, BossThree.X, BossThree.Z, Spec.Reach, RivalArc))
		{
			continue;
		}

		StruckThisSwing.Add(Boss);
		const float BaseRivalDmg = static_cast<float>(KomorebiCore::GetSwordDamage(KomorebiCore::EEnemyKind::Rival));
		const bool bWasGuarded = (Boss->GetDuelPhase() != ERenDuelPhase::Recovery);
		const int32 Dealt = Boss->ReceivePlayerDamage(SwingMult * BaseRivalDmg, Source);
		if (Dealt > 0)
		{
			const bool bNowOpening = (Boss->GetDuelPhase() == ERenDuelPhase::Recovery);
			NotifyPlayerHitConnected(Source, bWasGuarded && !bNowOpening, false);
			OnPulseVFX.Broadcast(
				BossFeet + FVector(0.0f, 0.0f, 110.0f),
				bWasGuarded ? FLinearColor(0.953f, 0.780f, 0.482f, 1.0f) : FLinearColor(0.722f, 0.961f, 0.925f, 1.0f),
				false);
		}
	}
}

void UCombatComponent::ExecutePlungeShockwave()
{
	bPlungeActive = false;
	AddCameraKick(0.14f);
	EmitAudio(TEXT("plunge"));

	const FVector PlayerFeet = GetOwnerFeetWorld();
	const float GroundZ = QueryGroundHeightCm(PlayerFeet);
	OnPulseVFX.Broadcast(FVector(PlayerFeet.X, PlayerFeet.Y, GroundZ + 7.0f), FLinearColor(0.988f, 0.933f, 0.039f, 1.0f), true);

	const float PlungeMult = static_cast<float>(KomorebiCore::kPlungeMul);
	OnMissionStrike.Broadcast(PlayerFeet, 360.0f, PlungeMult * 34.0f, TEXT("heavy"), ClearedSectors);

	const UEnemySquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<UEnemySquadSubsystem>() : nullptr;
	if (!Squad)
	{
		return;
	}

	for (const TWeakObjectPtr<AStreetEnemyCharacter>& WeakEnemy : Squad->GetRegisteredEnemies())
	{
		AStreetEnemyCharacter* Enemy = WeakEnemy.Get();
		if (!Enemy || Enemy->IsDead() || Enemy->IsGone())
		{
			continue;
		}
		const FVector EnemyFeet = Enemy->GetFeetWorldLocation();
		if (Squad->IsGateBlockingBetween(PlayerFeet, EnemyFeet) || FVector::Dist2D(PlayerFeet, EnemyFeet) > 340.0f)
		{
			continue;
		}
		StruckThisSwing.Add(Enemy);
		const float SwordDmg = static_cast<float>(KomorebiCore::GetSwordDamage(KomorebiBridge::ToCoreEnemyKind(Enemy->EnemyKind)));
		Enemy->ReceiveDamage(PlungeMult * SwordDmg, TEXT("heavy"), PlayerFeet, GetOwner(), false);
	}

	for (const TWeakObjectPtr<ARenBossCharacter>& WeakBoss : Squad->GetRegisteredRivals())
	{
		ARenBossCharacter* Boss = WeakBoss.Get();
		if (!Boss || Boss->IsDead() || !Boss->IsReady())
		{
			continue;
		}
		if (FVector::Dist2D(PlayerFeet, Boss->GetFeetWorldLocation()) <= 340.0f)
		{
			StruckThisSwing.Add(Boss);
			const float RivalDmg = static_cast<float>(KomorebiCore::GetSwordDamage(KomorebiCore::EEnemyKind::Rival));
			Boss->ReceivePlayerDamage(PlungeMult * RivalDmg, TEXT("heavy"));
		}
	}
}

void UCombatComponent::TryStartDash(const FKomorebiCombatFrameInput& Input)
{
	if (!HasPower(EKomorebiPower::Dash))
	{
		if (LockToastCooldown <= 0.0f)
		{
			LockToastCooldown = 4.0f;
			EmitToast(TEXT("Clear Sector 2 to learn the dash"));
		}
		return;
	}

	if (DashCooldown > 0.0f || Hp <= 0.0f)
	{
		return;
	}

	const AKaelCharacter* Kael = Cast<AKaelCharacter>(GetOwner());
	UKaelMovementComponent* KaelMove = Kael ? Kael->GetKaelMovement() : nullptr;
	if (!KaelMove)
	{
		return;
	}

	const float RefYawDeg = FocusTarget.IsValid()
		? (FocusTarget->GetActorLocation() - GetOwnerFeetWorld()).Rotation().Yaw
		: Input.CameraYawDeg;

	FVector DashDir = YawDegToForwardXY(RefYawDeg) * Input.MoveInput.Y + YawDegToRightXY(RefYawDeg) * Input.MoveInput.X;
	if (Input.MoveInput.Size() <= 0.08f || DashDir.IsNearlyZero())
	{
		DashDir = YawDegToForwardXY(GetOwnerYawDeg());
	}
	else
	{
		DashDir.Normalize();
	}

	if (KaelMove->StartDash(DashDir))
	{
		DashCooldown = 2.5f;
		InvulnTimer = FMath::Max(InvulnTimer, 0.3f);
		EmitAudio(TEXT("dash"));
		CheckPerfectDodge(false);
	}
}

void UCombatComponent::TryFireSpark()
{
	if (!HasPower(EKomorebiPower::Spark))
	{
		if (WorldScene == EKomorebiWorldScene::NeoKyoto && !IsRoamMode() && LockToastCooldown <= 0.0f)
		{
			LockToastCooldown = 4.0f;
			EmitToast(TEXT("Clear Sector 1 to earn the spark"));
		}
		return;
	}

	const std::optional<double> NextPower = KomorebiCore::SpendPower(SparkPower);
	if (!NextPower.has_value())
	{
		return;
	}

	SparkPower = static_cast<float>(*NextPower);
	CastPose = 1.0f;
	EmitAudio(TEXT("spark"));

	const FVector PlayerFeet = GetOwnerFeetWorld();
	const FVector Fwd = YawDegToForwardXY(GetOwnerYawDeg());

	FKomorebiActiveBolt Bolt;
	Bolt.Location = PlayerFeet + Fwd * 70.0f + FVector(0.0f, 0.0f, 130.0f);
	Bolt.VelocityCm = Fwd * 1800.0f + FVector(0.0f, 0.0f, 240.0f);
	Bolt.GravityCm = 700.0f;
	Bolt.LifeRemaining = 1.05f;
	Bolt.HostileDamage = 0.0f;
	Bolt.Shooter = GetOwner();
	ActiveBolts.Add(Bolt);

	OnPulseVFX.Broadcast(Bolt.Location, FLinearColor(0.616f, 0.925f, 0.906f, 1.0f), false);
}

void UCombatComponent::TryCastFlare()
{
	if (!HasPower(EKomorebiPower::Flare) || FlareCooldown > 0.0f || Hp <= 0.0f)
	{
		return;
	}

	if (SparkPower < 2.0f)
	{
		if (LockToastCooldown <= 0.0f)
		{
			LockToastCooldown = 4.0f;
			EmitToast(TEXT("The flare needs two spark"));
		}
		return;
	}

	SparkPower -= 2.0f;
	FlareCooldown = 20.0f;
	EmitAudio(TEXT("flare"));

	const FVector PlayerFeet = GetOwnerFeetWorld();
	const float GroundZ = QueryGroundHeightCm(PlayerFeet);
	OnPulseVFX.Broadcast(FVector(PlayerFeet.X, PlayerFeet.Y, GroundZ + 8.0f), FLinearColor(0.961f, 0.808f, 0.518f, 1.0f), true);
	OnMissionStrike.Broadcast(PlayerFeet, 900.0f, 60.0f, TEXT("flare"), ClearedSectors);

	const UEnemySquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<UEnemySquadSubsystem>() : nullptr;
	if (!Squad)
	{
		return;
	}

	for (const TWeakObjectPtr<AStreetEnemyCharacter>& WeakEnemy : Squad->GetRegisteredEnemies())
	{
		AStreetEnemyCharacter* Enemy = WeakEnemy.Get();
		if (!Enemy || Enemy->IsDead() || Enemy->IsGone())
		{
			continue;
		}
		const FVector EnemyFeet = Enemy->GetFeetWorldLocation();
		const float DistM = FVector::Dist2D(PlayerFeet, EnemyFeet) * 0.01f;
		if (DistM > 9.0f || Squad->IsGateBlockingBetween(PlayerFeet, EnemyFeet))
		{
			continue;
		}
		Enemy->ReceiveDamage(60.0f, TEXT("flare"), PlayerFeet, GetOwner(), false);
		Enemy->ApplyFlareDisruption(PlayerFeet, DistM);
	}

	for (const TWeakObjectPtr<ARenBossCharacter>& WeakBoss : Squad->GetRegisteredRivals())
	{
		ARenBossCharacter* Boss = WeakBoss.Get();
		if (!Boss || Boss->IsDead() || !Boss->IsReady())
		{
			continue;
		}
		const float DistM = FVector::Dist2D(PlayerFeet, Boss->GetFeetWorldLocation()) * 0.01f;
		if (DistM <= 9.0f)
		{
			Boss->ReceivePlayerDamage(60.0f, TEXT("flare"));
		}
	}
}

void UCombatComponent::StepActiveBolts(float DeltaTime)
{
	if (ActiveBolts.IsEmpty() || !GetWorld())
	{
		return;
	}

	const UWorld* World = GetWorld();
	const UEnemySquadSubsystem* Squad = World->GetSubsystem<UEnemySquadSubsystem>();
	const FVector PlayerFeet = GetOwnerFeetWorld();
	const KomorebiCore::Vec3 PlayerThree = KomorebiBridge::FVectorToThree(PlayerFeet);
	const double PlayerThreeYaw = KomorebiCore::UEYawDegToThreeYaw(GetOwnerYawDeg());
	const bool bHittingNow = IsInAttackHitWindow();

	for (int32 i = ActiveBolts.Num() - 1; i >= 0; --i)
	{
		FKomorebiActiveBolt& Bolt = ActiveBolts[i];
		Bolt.LifeRemaining -= DeltaTime;
		Bolt.VelocityCm.Z -= Bolt.GravityCm * DeltaTime;

		const FVector StepVec = Bolt.VelocityCm * DeltaTime;
		const float StepLen = StepVec.Size();

		bool bHitWall = false;
		if (StepLen > KINDA_SMALL_NUMBER)
		{
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(KomorebiBoltTrace), false, GetOwner());
			if (const AActor* ShooterActor = Bolt.Shooter.Get())
			{
				Params.AddIgnoredActor(ShooterActor);
			}
			if (World->LineTraceSingleByChannel(Hit, Bolt.Location, Bolt.Location + StepVec, ECC_WorldStatic, Params))
			{
				Bolt.Location = Hit.ImpactPoint;
				bHitWall = true;
			}
			else
			{
				Bolt.Location += StepVec;
			}
		}

		const float GroundZ = QueryGroundHeightCm(Bolt.Location);
		bool bGone = (Bolt.LifeRemaining <= 0.0f) || (Bolt.Location.Z < GroundZ + 25.0f);

		if (!bGone && Bolt.HostileDamage > 0.0f)
		{
			const float DistToPlayerM = FVector::Dist2D(Bolt.Location, PlayerFeet) * 0.01f;
			const KomorebiCore::Vec3 BoltThree = KomorebiBridge::FVectorToThree(Bolt.Location);

			// Deflect hostile gunner shot with timed sword swing (hunt.js lines 1458-1463)
			if (bHittingNow
				&& DistToPlayerM < 2.4f
				&& KomorebiCore::InFront(PlayerThree.X, PlayerThree.Z, PlayerThreeYaw, BoltThree.X, BoltThree.Z, 2.4, 1.6, 0.9))
			{
				Bolt.VelocityCm.X *= -1.5f;
				Bolt.VelocityCm.Y *= -1.5f;
				Bolt.VelocityCm.Z = 0.0f;
				Bolt.HostileDamage = 0.0f;
				Bolt.LifeRemaining = 1.2f;
				Bolt.Shooter = GetOwner();
				SparkPower = FMath::Min(MaxSparkPower, SparkPower + 0.25f);
				HitPauseTimer = FMath::Max(HitPauseTimer, 0.05f);
				EmitAudio(TEXT("deflect"));
				OnPulseVFX.Broadcast(Bolt.Location, FLinearColor(0.0f, 0.941f, 1.0f, 1.0f), false);
				EmitFlash(TEXT("Deflected"));
			}
			else if (DistToPlayerM < 0.55f
				&& FMath::Abs((Bolt.Location.Z - PlayerFeet.Z) - 100.0f) < 110.0f
				&& Hp > 0.0f)
			{
				bGone = ReceiveEnemyHit(Bolt.HostileDamage, Bolt.Shooter.Get(), 3.0f, false);
			}
		}
		else if (!bGone && Squad)
		{
			// Friendly Spark or deflected bolt hitting enemies / rivals
			OnMissionStrike.Broadcast(Bolt.Location, 120.0f, 75.0f, TEXT("spark"), ClearedSectors);

			for (const TWeakObjectPtr<AStreetEnemyCharacter>& WeakEnemy : Squad->GetRegisteredEnemies())
			{
				AStreetEnemyCharacter* Enemy = WeakEnemy.Get();
				if (!Enemy || Enemy->IsDead() || Enemy->IsGone())
				{
					continue;
				}
				const FVector EnemyFeet = Enemy->GetFeetWorldLocation();
				if (Squad->IsGateBlockingBetween(PlayerFeet, EnemyFeet))
				{
					continue;
				}
				const float LiftCm = Enemy->GetBodyLiftCm();
				if (FMath::Abs(Bolt.Location.Z - (EnemyFeet.Z + LiftCm)) < LiftCm + 30.0f
					&& FVector::Dist2D(Bolt.Location, EnemyFeet) < Enemy->GetBoltHitRadiusCm())
				{
					const float BoltDmg = static_cast<float>(KomorebiCore::GetBoltDamage(KomorebiBridge::ToCoreEnemyKind(Enemy->EnemyKind)));
					Enemy->ReceiveDamage(BoltDmg, TEXT("spark"), Bolt.Location, GetOwner(), false);
					bGone = true;
					break;
				}
			}

			if (!bGone)
			{
				for (const TWeakObjectPtr<ARenBossCharacter>& WeakBoss : Squad->GetRegisteredRivals())
				{
					ARenBossCharacter* Boss = WeakBoss.Get();
					if (!Boss || Boss->IsDead() || !Boss->IsReady())
					{
						continue;
					}
					const FVector BossFeet = Boss->GetFeetWorldLocation();
					if (FMath::Abs(Bolt.Location.Z - (BossFeet.Z + 90.0f)) < 120.0f
						&& FVector::Dist2D(Bolt.Location, BossFeet) < 65.0f)
					{
						const float RivalBoltDmg = static_cast<float>(KomorebiCore::GetBoltDamage(KomorebiCore::EEnemyKind::Rival));
						Boss->ReceivePlayerDamage(RivalBoltDmg, TEXT("spark"));
						bGone = true;
						break;
					}
				}
			}
		}

		if (bHitWall || bGone)
		{
			ActiveBolts.RemoveAtSwap(i, 1, EAllowShrinking::No);
		}
	}
}

void UCombatComponent::ApplyGlobalTimeDilation()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float DesiredScale = GetTimeScale();
	if (!FMath::IsNearlyEqual(DesiredScale, LastAppliedTimeScale, 0.005f))
	{
		UGameplayStatics::SetGlobalTimeDilation(World, DesiredScale);
		LastAppliedTimeScale = DesiredScale;
	}
}

void UCombatComponent::StepCombat(
	float ScaledDeltaTime,
	float RealDeltaTime,
	const FKomorebiCombatFrameInput& Input,
	bool bAllowCombat)
{
	// Online downed countdown runs in real time (hunt.js line 989)
	if (bDowned)
	{
		DownedTimeRemaining -= RealDeltaTime;
		if (DownedTimeRemaining <= 0.0f)
		{
			TriggerDeath(true);
		}
	}

	// Real-time timers (hit-stop, slow-mo, combo decay, camera kick)
	HitPauseTimer = FMath::Max(0.0f, HitPauseTimer - RealDeltaTime);
	SlowMoTimer = FMath::Max(0.0f, SlowMoTimer - RealDeltaTime);
	CameraKick = FMath::Max(0.0f, CameraKick - RealDeltaTime * 0.7f);

	if (ComboCount > 0)
	{
		ComboDecayTimer -= RealDeltaTime;
		if (ComboDecayTimer <= 0.0f)
		{
			ComboCount = 0;
			OnComboChanged.Broadcast(0);
		}
	}

	// Scaled-time timers
	HurtPose = FMath::Max(0.0f, HurtPose - ScaledDeltaTime * 5.0f);
	CastPose = FMath::Max(0.0f, CastPose - ScaledDeltaTime * 3.0f);
	ComboWindowTimer = FMath::Max(0.0f, ComboWindowTimer - ScaledDeltaTime);
	InvulnTimer = FMath::Max(0.0f, InvulnTimer - ScaledDeltaTime);
	DashCooldown = FMath::Max(0.0f, DashCooldown - ScaledDeltaTime);
	FlareCooldown = FMath::Max(0.0f, FlareCooldown - ScaledDeltaTime);
	LockToastCooldown = FMath::Max(0.0f, LockToastCooldown - ScaledDeltaTime);
	AttackBufferTimer = FMath::Max(0.0f, AttackBufferTimer - ScaledDeltaTime);

	// Spark power regeneration (kDashFill=26s once Dash unlocked in Neo-Kyoto, else kPowerFill=42s)
	const double FillSeconds = (WorldScene == EKomorebiWorldScene::NeoKyoto && HasPower(EKomorebiPower::Dash))
		? KomorebiCore::kDashFill
		: KomorebiCore::kPowerFill;
	SparkPower = static_cast<float>(KomorebiCore::StepPower(SparkPower, ScaledDeltaTime, FillSeconds, MaxSparkPower));

	const AKaelCharacter* Kael = Cast<AKaelCharacter>(GetOwner());
	const UKaelMovementComponent* KaelMove = Kael ? Kael->GetKaelMovement() : nullptr;
	const bool bBusy = bGuarding || (KaelMove && KaelMove->IsEvading());

	if (bAllowCombat && Hp > 0.0f && !bDowned && !bBusy)
	{
		if (Input.bHeavyPressed && !HasPower(EKomorebiPower::Heavy) && LockToastCooldown <= 0.0f)
		{
			LockToastCooldown = 4.0f;
			EmitToast(TEXT("Clear Sector 3 to learn the heavy swing"));
		}

		if (Input.bAttackPressed || (Input.bHeavyPressed && HasPower(EKomorebiPower::Heavy)))
		{
			AttackBufferTimer = 0.55f;
			bQueuedHeavy = Input.bHeavyPressed && HasPower(EKomorebiPower::Heavy);
		}

		if (Input.bWhirlPressed)
		{
			TryStartWhirl();
		}

		TryConsumeAttackBuffer(Input);

		if (Input.bDashPressed)
		{
			TryStartDash(Input);
		}

		if (Input.bSparkPressed)
		{
			TryFireSpark();
		}

		if (Input.bFlarePressed)
		{
			TryCastFlare();
		}
	}

	// Advance swing progress
	if (SwingU >= 0.0f)
	{
		const KomorebiCore::AttackSpec& Spec = KomorebiCore::GetAttackSpec(KomorebiBridge::ToCoreAttackType(ActiveAttackType));
		SwingU += ScaledDeltaTime / FMath::Max(0.05f, static_cast<float>(Spec.Duration));
		if (SwingU >= 1.0f)
		{
			SwingU = -1.0f;
			bSwingHeavy = false;
			ComboWindowTimer = 0.7f;
		}
	}

	if (IsInAttackHitWindow())
	{
		EvaluateMeleeHitWindow();
	}

	if (bPlungeActive && KaelMove && KaelMove->IsMovingOnGround())
	{
		ExecutePlungeShockwave();
	}

	StepActiveBolts(ScaledDeltaTime);
	ApplyGlobalTimeDilation();
}
