// Copyright Komorebi Project. Replicated street enemy, civilian, courier/thief, and police character porting legacy-web/src/game/hunt.js.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "KomorebiGameTypes.h"
#include "Game/CombatRules.h"
#include "Game/TacticsRules.h"
#include "StreetEnemyCharacter.generated.h"

class UStaticMeshComponent;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class AActor;
class AKaelCharacter;
class AKomorebiTerrainActor;
class UEnemySquadSubsystem;

UENUM(BlueprintType)
enum class EStreetEnemyAnimState : uint8
{
	Idle,
	Walk,
	Run,
	Attack,
	Alternate,
	Guard,
	EvadeLeft,
	EvadeRight,
	Hit,
	Death
};

UENUM(BlueprintType)
enum class EStreetEnemyAIMode : uint8
{
	Roam,
	Chase
};

UENUM(BlueprintType)
enum class ECivArchetype : uint8
{
	Resident,
	Mechanic,
	Executive,
	Vendor
};

/**
 * Replicated street body supporting all 5 armed crew kinds (Punk, Enforcer, Gunner, Blade, Brute),
 * 4 civilian archetypes, Thief/Courier runners (2/3 carrying a neon loot box), and Police enforcers.
 * Implements KIND/HIT/HEFT/PATTERNS/DEFENSE, Gunner AIM_LOCK + deflectable bolts, swept knockback &
 * airborne juggle, material FlashAmount hit-flash, distance-capped AI (36m) & visibility (85m),
 * vehicle ram impacts, and bystander Guilt/Witness/Mob/Heat rules.
 */
UCLASS(Blueprintable)
class KOMOREBIGAME_API AStreetEnemyCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AStreetEnemyCharacter();

	static constexpr float kCrewAIRangeMeters = 36.0f;
	static constexpr float kCivAIRangeMeters = 46.0f;
	static constexpr float kMaxVisibleMeters = 85.0f;
	static constexpr float kAlwaysVisibleMeters = 9.0f;

	// --- Replicated Identity & Combat State ---

	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "Komorebi|AI")
	EKomorebiEnemyKind EnemyKind = EKomorebiEnemyKind::Punk;

	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "Komorebi|AI")
	ECivArchetype CivArchetype = ECivArchetype::Resident;

	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "Komorebi|AI")
	int32 SegmentIndex = -1;

	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "Komorebi|AI")
	bool bIsPolice = false;

	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "Komorebi|AI")
	bool bIsThief = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|AI")
	float Hp = 60.0f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|AI")
	float MaxHp = 60.0f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|AI")
	bool bIsDead = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|AI")
	bool bIsGone = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|AI")
	float WindupRemaining = 0.0f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Komorebi|AI")
	EStreetEnemyAnimState AnimState = EStreetEnemyAnimState::Idle;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|AI")
	EStreetEnemyAIMode AIMode = EStreetEnemyAIMode::Roam;

	// --- Visual Components ---

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|AI")
	TObjectPtr<UStaticMeshComponent> LootBoxMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|AI")
	TObjectPtr<UProceduralMeshComponent> TelegraphRingMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|AI")
	TObjectPtr<UProceduralMeshComponent> GunnerAimBeamMesh;

	// --- Initialization & Configuration ---

	UFUNCTION(BlueprintCallable, Category = "Komorebi|AI")
	void InitializeEnemy(
		EKomorebiEnemyKind InKind,
		int32 InSegmentIndex,
		int32 InVariant = 0,
		bool bInPolice = false,
		bool bInThief = false,
		bool bInSidewalkPool = false);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|AI")
	void RefreshDynamicMaterials();

	// --- Combat & Consequences ---

	/** Applies damage from blade, heavy, whirl, thrust, spark, flare, or ram. Returns true if hit connected. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|AI")
	bool ReceiveDamage(float Amount, FName Source, const FVector& AttackerLocation, AActor* AttackerActor, bool bIsParry = false);

	/** Vehicle collision impact (Force = SpeedMps * MassScale). Only bPlayerLiable triggers Guilt/Heat. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|AI")
	bool ReceiveRamImpact(float Force, FVector Dir, bool bPlayerLiable);

	/** Executes Guilt/Witness/Mob/Heat/Police consequences when a bystander is struck or killed. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|AI")
	void BlameBystanderHit(bool bKilled, bool bProvoked, AActor* InstigatorActor);

	/** Turns this civilian hostile for MobDuration seconds (witness aggro). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|AI")
	void ProvokeWitness(float MobDurationSeconds);

	/** Forces stagger duration (e.g. 1.2s on melee Parry) and clears any active windup. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|AI")
	void ForceStagger(float DurationSeconds);

	/** Applies lantern Flare pushback, cooldown extension (>=2s), and returns crew AI to roam. */
	void ApplyFlareDisruption(const FVector& FlareOriginWorld, float DistanceMeters);

	/** Returns enemy to its home position in calm state (on player respawn or roam/civ respawn). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|AI")
	void ResetToHome(bool bRestoreFullHealth);

	/** Respawns a gone enemy cleanly with collision & visibility restored. */
	void RespawnFromGone();

	// --- Squad Tactics & Frame Tick (driven by UEnemySquadSubsystem) ---

	void ApplySquadOrder(bool bInLead, int32 InSide, float InRadiusMeters, int32 InPreparedIndex, bool bInRanged, AActor* InTarget);

	void StepEnemyAI(float DeltaTime, AActor* LocalPlayer, bool bFightingNearby, bool bGateBlocked, bool bFreeRoam);

	// --- Queries ---

	UFUNCTION(BlueprintPure, Category = "Komorebi|AI")
	bool IsCiv() const { return Spec.IsCiv; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|AI")
	bool IsAngry() const { return AngryTimer > 0.0f; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|AI")
	bool IsDead() const { return bIsDead; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|AI")
	bool IsGone() const { return bIsGone; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|AI")
	bool IsEventBoss() const { return bIsEventMech; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|AI")
	bool IsSidewalkPool() const { return bIsSidewalkPool; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|AI")
	bool IsGuardingNow() const { return DefendRemaining > 0.0f && DefendingKind == KomorebiCore::EDefenseKind::Guard; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|AI")
	bool IsFiringRangedAttack() const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|AI")
	float GetActiveMoveReachMeters() const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|AI")
	float GetBoltHitRadiusCm() const { return static_cast<float>(Spec.BoltRadius * 100.0); }

	UFUNCTION(BlueprintPure, Category = "Komorebi|AI")
	float GetBodyLiftCm() const { return static_cast<float>((Spec.CapsuleRadius + Spec.CapsuleHalfHeight) * 100.0); }

	UFUNCTION(BlueprintPure, Category = "Komorebi|AI")
	float GetVisualScale() const { return static_cast<float>(Spec.Scale); }

	UFUNCTION(BlueprintPure, Category = "Komorebi|AI")
	FVector GetHomeWorldLocation() const { return HomeWorldLocation; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|AI")
	FVector GetFeetWorldLocation() const;

	float GetAttackCooldown() const { return AttackCooldown; }
	float GetDefendRemaining() const { return DefendRemaining; }
	float GetStaggerRemaining() const { return StaggerRemaining; }
	bool IsPressuring() const { return bPressuring; }
	bool HasReprisal() const { return bReprisal; }
	int32 GetAttackCount() const { return AttackCount; }
	int32 GetAttackIndex() const { return AttackIndex; }
	float GetAggroRangeMeters() const { return AggroRangeMeters; }
	float GetLeashMeters() const { return LeashMeters; }
	float GetWaitTimer() const { return WaitTimer; }
	int32 GetPoolSeed() const { return PoolSeed; }
	float GetPoolDir() const { return PoolDir; }
	float GetChatCooldown() const { return ChatCooldown; }
	float GetFleeTimer() const { return FleeTimer; }

	void StartStreetChat(float ChatDuration, float Cooldown, float FaceYawDeg);
	void RecycleToSidewalk(const FVector& NewHomeWorld, const FVector& NextWaypointWorld);
	void ApplySeparationPush(const FVector& PushDeltaXY);

	// --- ACharacter Overrides ---

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	void EnterDeathState();
	void StepKnockbackAndAirborne(float DeltaTime);
	void SweptMoveHorizontal(const FVector& DeltaXY);
	bool HasClearFiringLane(const FVector& TargetFeet, float OptionalLockedThreeYaw, bool bUseLockedYaw) const;
	void FireGunnerBolt(const FVector& TargetFeet);
	void UpdateVisualsAndMaterials(float DeltaTime, AActor* LocalPlayer);
	void RebuildTelegraphRingMesh(float RadiusCm, const FLinearColor& Color);
	void RebuildAimBeamMesh(float LengthCm, const FLinearColor& Color);
	float QueryGroundHeightCm(const FVector& WorldPos) const;

	KomorebiCore::EnemyKindSpec Spec{};
	double Heft = 1.0;

	FVector HomeWorldLocation = FVector::ZeroVector;
	FVector WaypointWorld = FVector::ZeroVector;

	float AggroRangeMeters = 9.0f;
	float LeashMeters = 18.0f;

	// Combat & AI timers
	float AttackCooldown = 0.0f;
	float RecoverRemaining = 0.0f;
	float StaggerRemaining = 0.0f;
	float DefendRemaining = 0.0f;
	float DefendCooldown = 0.0f;
	KomorebiCore::EDefenseKind DefendingKind = KomorebiCore::EDefenseKind::None;
	int32 DefendSide = 1;

	int32 AttackIndex = 0;
	int32 PreparedAttackIndex = 0;
	int32 AttackCount = 0;
	bool bReprisal = false;
	bool bPressuring = false;
	bool bIsLead = true;
	bool bPreparedRanged = false;
	int32 FlankSide = 1;
	float FlankRadiusMeters = 3.6f;

	float AttackThreeYaw = 0.0f;
	bool bAimLockPlayed = false;
	float LaneStepTimer = 0.0f;
	int32 LaneSide = 1;

	// Knockback & airborne juggle (UE cm / s)
	FVector KnockbackVelXY = FVector::ZeroVector;
	float AirHeightCm = 0.0f;
	float AirVelZCm = 0.0f;
	float FlashTimer = 0.0f;
	float RammedCooldown = 0.0f;
	float DeadElapsed = 0.0f;
	float RespawnTimer = 0.0f;
	float LastHitByPlayerTime = -1000.0f;

	// Street / Civilian state
	bool bIsSidewalkPool = false;
	bool bIsEventMech = false;
	int32 PoolSeed = 0;
	float PoolDir = 1.0f;
	float WaitTimer = 0.0f;
	float AngryTimer = 0.0f;
	float FleeTimer = 0.0f;
	float SnatchTimer = 0.0f;
	float ChatTimer = 0.0f;
	float ChatCooldown = 0.0f;
	float ChatYawDeg = 0.0f;
	float GoingSpeedMps = 0.0f;

	TWeakObjectPtr<AActor> SquadTarget;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DynamicMaterials;

	bool bTelegraphBuilt = false;
	bool bAimBeamBuilt = false;
};
