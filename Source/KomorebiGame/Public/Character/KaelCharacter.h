// Copyright Komorebi Project. Player character Kael integrating custom movement, follow camera, combat, katana/lantern sockets, Enhanced Input, vehicle driving, and online pose replication.
#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "KomorebiGameTypes.h"
#include "Combat/CombatComponent.h"
#include "KaelCharacter.generated.h"

class UCameraComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UInputAction;
class UInputMappingContext;
class UKaelMovementComponent;
class UFollowCameraComponent;
class UBladeTrailComponent;
class UKaelAnimInstance;
class APawn;
class AKomorebiVehiclePawn;
class AKomorebiTerrainActor;
class UMissionSubsystem;
class UKomorebiAudioSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnKaelRideToggleRequested, AKaelCharacter*, Kael);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnKaelPauseToggleRequested);

/** Compact replicated pose packet sent for remote wanderers in Online City mode. */
USTRUCT(BlueprintType)
struct FKaelOnlinePoseState
{
	GENERATED_BODY()

	UPROPERTY()
	FVector_NetQuantize Location = FVector::ZeroVector;

	UPROPERTY()
	float YawDeg = 0.0f;

	UPROPERTY()
	float SpeedCm = 0.0f;

	UPROPERTY()
	float MoveAngleRad = 0.0f;

	UPROPERTY()
	uint8 Flags = 0; // bit0=Grounded, bit1=Strafing, bit2=Guarding, bit3=Driving, bit4=Downed, bit5=RightHandLantern

	UPROPERTY()
	uint8 AttackTypeByte = 0;

	UPROPERTY()
	float AttackU = -1.0f;

	UPROPERTY()
	float HurtPose = 0.0f;

	UPROPERTY()
	float CastPose = 0.0f;
};

/**
 * Playable hero character Kael using UKaelMovementComponent, UFollowCameraComponent, UCombatComponent,
 * UBladeTrailComponent, Katana on `hand_l`, Spirit Lantern on `pelvis` (or `hand_r` in Snowbound),
 * and full Enhanced Input bindings (keyboard, mouse, gamepad) matching legacy-web/src/figure/input.js.
 */
UCLASS(Blueprintable)
class KOMOREBIGAME_API AKaelCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AKaelCharacter(const FObjectInitializer& ObjectInitializer);

	// --- Core Components ---

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Components")
	TObjectPtr<UFollowCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Components")
	TObjectPtr<UCameraComponent> ViewCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Components")
	TObjectPtr<UCombatComponent> CombatComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Components")
	TObjectPtr<UBladeTrailComponent> BladeTrailComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Equipment")
	TObjectPtr<UStaticMeshComponent> KatanaMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Equipment")
	TObjectPtr<UStaticMeshComponent> SpiritLanternMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Equipment")
	TObjectPtr<UPointLightComponent> LanternLight;

	// --- Enhanced Input Mapping & Actions ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|Input")
	TObjectPtr<UInputAction> IA_Move = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|Input")
	TObjectPtr<UInputAction> IA_Look = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|Input")
	TObjectPtr<UInputAction> IA_Zoom = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|Input")
	TObjectPtr<UInputAction> IA_Jump = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|Input")
	TObjectPtr<UInputAction> IA_Sprint = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|Input")
	TObjectPtr<UInputAction> IA_WalkToggle = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|Input")
	TObjectPtr<UInputAction> IA_Attack = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|Input")
	TObjectPtr<UInputAction> IA_Heavy = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|Input")
	TObjectPtr<UInputAction> IA_Spark = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|Input")
	TObjectPtr<UInputAction> IA_Dash = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|Input")
	TObjectPtr<UInputAction> IA_Flare = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|Input")
	TObjectPtr<UInputAction> IA_Whirl = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|Input")
	TObjectPtr<UInputAction> IA_Guard = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|Input")
	TObjectPtr<UInputAction> IA_Evade = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|Input")
	TObjectPtr<UInputAction> IA_LockTarget = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|Input")
	TObjectPtr<UInputAction> IA_NextTarget = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|Input")
	TObjectPtr<UInputAction> IA_Ride = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Komorebi|Input")
	TObjectPtr<UInputAction> IA_Pause = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Input")
	float MouseLookSensitivityDeg = 0.126f; // ~0.0022 rad/px converted to deg

	// --- Equipment & Scene Options ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Equipment")
	FName KatanaSocketName = TEXT("hand_l");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Equipment")
	FName HipLanternSocketName = TEXT("pelvis");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Equipment")
	FName RightHandLanternSocketName = TEXT("hand_r");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Equipment")
	bool bRightHandLanternMode = false;

	// --- Delegates ---

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Events")
	FOnKaelRideToggleRequested OnRideRequested;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Events")
	FOnKaelPauseToggleRequested OnPauseRequested;

	// --- Public API ---

	/** Teleports Kael safely to Location (feet world pos) facing YawDeg, resetting physics, pivot, focus, and foot IK. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Character")
	void TeleportAndSettle(FVector Location, float YawDeg);

	/** Toggles vehicle driving state: hides Kael & equipment, masks combat input, disables capsule, and aligns chase camera. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Character")
	void SetDrivingState(bool bInDriving, APawn* VehiclePawn);

	/** Switches Spirit Lantern between hip belt mount (Neo-Kyoto) and right-hand hold (Snowbound). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Equipment")
	void SetRightHandLanternMode(bool bInRightHandMode);

	UFUNCTION(BlueprintPure, Category = "Komorebi|Character")
	bool IsDriving() const { return bIsDriving; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Character")
	APawn* GetDrivenVehicle() const { return DrivenVehiclePawn.Get(); }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Character")
	FVector GetFeetWorldLocation() const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|Character")
	UKaelMovementComponent* GetKaelMovement() const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|Character")
	UFollowCameraComponent* GetFollowCamera() const { return FollowCamera; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Character")
	UCombatComponent* GetCombatComponent() const { return CombatComponent; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Character")
	UBladeTrailComponent* GetBladeTrail() const { return BladeTrailComponent; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Character")
	FVector2D GetLatestMoveInput() const { return CurrentMoveInput; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Character")
	bool IsJumpHeld() const { return bJumpInputHeld; }

	// --- ACharacter Overrides ---

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	void EnsureDefaultEnhancedInputAssets();
	void RegisterInputMappingContext();
	void UpdateEquipmentAttachments();
	void UpdateAnimAndBladeTrail(float DeltaTime);
	void UpdateOnlinePoseReplication(float RealDeltaTime);
	void ApplyRemoteOnlinePose(float DeltaTime);

	// Enhanced Input callbacks
	void OnMoveTriggered(const FInputActionValue& Value);
	void OnMoveCompleted(const FInputActionValue& Value);
	void OnLookTriggered(const FInputActionValue& Value);
	void OnZoomTriggered(const FInputActionValue& Value);
	void OnJumpStarted(const FInputActionValue& Value);
	void OnJumpCompleted(const FInputActionValue& Value);
	void OnSprintStarted(const FInputActionValue& Value);
	void OnSprintCompleted(const FInputActionValue& Value);
	void OnWalkToggleStarted(const FInputActionValue& Value);
	void OnAttackStarted(const FInputActionValue& Value);
	void OnHeavyStarted(const FInputActionValue& Value);
	void OnSparkStarted(const FInputActionValue& Value);
	void OnDashStarted(const FInputActionValue& Value);
	void OnFlareStarted(const FInputActionValue& Value);
	void OnWhirlStarted(const FInputActionValue& Value);
	void OnGuardStarted(const FInputActionValue& Value);
	void OnGuardCompleted(const FInputActionValue& Value);
	void OnEvadeStarted(const FInputActionValue& Value);
	void OnLockTargetStarted(const FInputActionValue& Value);
	void OnNextTargetStarted(const FInputActionValue& Value);
	void OnRideStarted(const FInputActionValue& Value);
	void OnPauseStarted(const FInputActionValue& Value);

	// Replicated Online City pose
	UPROPERTY(ReplicatedUsing = OnRep_OnlinePose)
	FKaelOnlinePoseState ReplicatedOnlinePose;

	UFUNCTION()
	void OnRep_OnlinePose();

	UFUNCTION(Server, Unreliable)
	void ServerSyncOnlinePose(const FKaelOnlinePoseState& NewPose);

	// Transient per-frame input accumulators
	FVector2D CurrentMoveInput = FVector2D::ZeroVector;
	FVector2D PendingLookDeltaDeg = FVector2D::ZeroVector;
	float PendingZoomSteps = 0.0f;
	bool bJumpInputHeld = false;
	bool bSprintInputHeld = false;
	bool bWalkToggled = false;
	bool bGuardInputHeld = false;

	FKomorebiCombatFrameInput PendingCombatInput;

	// Vehicle state
	bool bIsDriving = false;
	TWeakObjectPtr<APawn> DrivenVehiclePawn;

	// Footstep audio cadence & online pose sync throttle
	float StepDistanceAccumCm = 0.0f;
	float OnlinePoseSyncAccum = 0.0f;
};
