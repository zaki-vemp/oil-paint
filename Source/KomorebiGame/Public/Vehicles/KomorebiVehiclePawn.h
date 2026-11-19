// Copyright Komorebi Project. Replicated 5-class arcade vehicle pawn wrapping KomorebiCore::VehiclePhysics.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "KomorebiGameTypes.h"
#include "Game/CombatRules.h"
#include "Game/VehiclePhysics.h"
#include "World/NeoKyotoTerrain.h"

#include <functional>

#include "KomorebiVehiclePawn.generated.h"

class UBoxComponent;
class UCameraComponent;
class USceneComponent;
class USpotLightComponent;
class USpringArmComponent;
class UStaticMesh;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EKomorebiVehicleDamageState : uint8
{
	Healthy,
	Smoking,
	Burning,
	Wrecked
};

USTRUCT(BlueprintType)
struct KOMOREBIGAME_API FKomorebiVehicleTelemetry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Vehicle")
	bool bDriving = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Vehicle")
	EKomorebiVehicleKind Kind = EKomorebiVehicleKind::Car;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Vehicle")
	FString DisplayName = TEXT("Kestrel");

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Vehicle")
	float SpeedMps = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Vehicle")
	float SpeedKmh = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Vehicle")
	float TopSpeedMps = 25.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Vehicle")
	float Throttle = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Vehicle")
	bool bSliding = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Vehicle")
	bool bAirborne = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Vehicle")
	bool bTipped = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Vehicle")
	bool bHasTrafficDriver = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Vehicle")
	EKomorebiVehicleDamageState DamageState = EKomorebiVehicleDamageState::Healthy;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Vehicle")
	float HullHp = 140.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Vehicle")
	float MaxHullHp = 140.0f;
};

USTRUCT()
struct KOMOREBIGAME_API FKomorebiVehicleNetState
{
	GENERATED_BODY()

	UPROPERTY()
	float ThreeX = 0.0f;

	UPROPERTY()
	float ThreeY = 0.0f;

	UPROPERTY()
	float ThreeZ = 0.0f;

	UPROPERTY()
	float ThreeYaw = 0.0f;

	UPROPERTY()
	float Vx = 0.0f;

	UPROPERTY()
	float Vz = 0.0f;

	UPROPERTY()
	float Steer = 0.0f;

	UPROPERTY()
	float HullHp = 140.0f;

	UPROPERTY()
	bool bWrecked = false;

	UPROPERTY()
	bool bTipped = false;

	UPROPERTY()
	bool bOccupied = false;
};

USTRUCT()
struct FKomorebiWheelRig
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<USceneComponent> SteerPivot = nullptr;

	UPROPERTY()
	TObjectPtr<USceneComponent> SpinPivot = nullptr;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> WheelMesh = nullptr;

	bool bFront = false;
	double LocalThreeX = 0.0;
	double LocalThreeZ = 0.0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnVehicleEntered, AKomorebiVehiclePawn*, Vehicle, APawn*, Driver, bool, bWasCarjacked);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnVehicleExited, AKomorebiVehiclePawn*, Vehicle, APawn*, Driver);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnVehicleDriverCrashDamage, AKomorebiVehiclePawn*, Vehicle, float, DamageAmount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnVehicleCameraJolt, AKomorebiVehiclePawn*, Vehicle, float, Intensity);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FOnVehicleRamPawn, AKomorebiVehiclePawn*, Vehicle, APawn*, HitPawn, float, SpeedMps, FVector, ImpactDirectionUE, float, SuggestedDamage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVehicleWrecked, AKomorebiVehiclePawn*, Vehicle);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVehicleFeedbackText, const FString&, Message);

/**
 * Replicated arcade vehicle pawn supporting all 5 Neo-Kyoto vehicle classes:
 * Kestrel (Car), Kestrel GT (Sports), Parcel Van (Pickup), Razor Bike (Bike), and Courier Scooter (Bicycle).
 * Steps KomorebiCore::VehiclePhysics in small fixed substeps with static chassis sweeps, independent wheel pivots,
 * bike lean, hull damage/smoke/fire/wreck states, ramming impacts, and smooth remote peer glide.
 */
UCLASS(Blueprintable)
class KOMOREBIGAME_API AKomorebiVehiclePawn : public APawn
{
	GENERATED_BODY()

public:
	AKomorebiVehiclePawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// --- Initialization & Setup ----------------------------------------------------------------

	/** Initializes vehicle class, physics state, home transform, and 4-wheel or 2-wheel pivot rig. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Vehicle")
	void InitializeVehicle(EKomorebiVehicleKind InKind, FVector SpawnWorldLocation, float SpawnThreeYawRad = 0.0f);

	/** Assigns optional custom body/wheel/rider meshes from imported street assets. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Vehicle")
	void SetVehicleMeshAssets(UStaticMesh* InBodyMesh, UStaticMesh* InWheelMesh, UStaticMesh* InRiderMesh = nullptr);

	/** Supplies a custom terrain height provider in Three.js metres (defaults to NeoKyotoTerrainMap). */
	void SetTerrainHeightProvider(TFunction<double(double, double)> InHeightProvider);

	/** Called by UTrafficSubsystem so all vehicles step together in synchronized 120Hz substeps. */
	void SetManagedByTrafficSubsystem(bool bManaged) { bManagedByTrafficSubsystem = bManaged; }

	/** Resets the vehicle back to its home parking transform with full hull health. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Vehicle")
	void ResetToHome();

	/** Teleports vehicle to a specific Three.js position/yaw and settles suspension cleanly. */
	void TeleportThree(double ThreeX, double ThreeZ, double ThreeYawRad, bool bRestoreHull = false);

	// --- Driver Entry / Exit / Input -----------------------------------------------------------

	/** Sets driving inputs (-1..1 throttle/brake, -1..1 steer, handbrake). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Vehicle")
	void SetDriveInput(float InThrottle, float InSteer, bool bInHandbrake);

	/**
	 * Attempts to enter (or right if overturned and stopped) this vehicle.
	 * Checks speed (<= 2 m/s), sector unlock rules, remote driver lock, wreck status, and carjack status.
	 */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Vehicle")
	bool TryEnterVehicle(
		APawn* InDriverPawn,
		bool bRoamMode,
		int32 ClearedSectors,
		FString& OutFeedbackMessage);

	/**
	 * Attempts to step out of this vehicle, enforcing kBailSpeed (9 m/s) unless bQuiet is true,
	 * and checking left/right/rear static collision clearance.
	 */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Vehicle")
	bool TryExitVehicle(bool bQuiet, FString& OutFeedbackMessage);

	/** Immediately ejects the driver to the left side (rollover, lethal bike impact, or wreck). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Vehicle")
	void BailOut(const FString& Reason);

	// --- Physics Substep & Collision (Called by Tick or UTrafficSubsystem) ---------------------

	/** Advances a single fixed physics substep (handling, rocking, wall sweep, ramming, suspension). */
	void StepPhysicsSubstep(double SubDt, const KomorebiCore::VehicleDriveInput& Input, bool bIsPlayerDriven);

	/** Sweeps back from (PrevThreeX, PrevThreeZ) after vehicle-to-vehicle OBB separation so walls are never penetrated. */
	void CorrectAfterCollision(double PrevThreeX, double PrevThreeZ);

	/** Applies collision hull damage scaled by colliding mass and closing speed; wrecks at 0 HP. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Vehicle")
	void ApplyHullDamage(float OtherMassKg, float ClosingSpeedMps);

	/** Immediately wrecks the vehicle, triggers explosion flare, drops traffic AI, and bails any driver. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Vehicle")
	void WreckVehicle();

	/** Synchronizes UE actor transform, wheel steer/spin/compression pivots, bike lean, rider, and smoke/fire VFX. */
	void SyncVisualPose(float DeltaTime, const FVector& ViewerWorldLocation);

	/** Applies smooth dead-reckoning interpolation when controlled by a remote networked peer. */
	void UpdateRemoteNetInterpolation(float DeltaTime);

	/** Modulates headlight intensity based on Day/Night phase (0 = day, 1 = night). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Vehicle")
	void SetNightPhase(float Night01);

	// --- Accessors -----------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Komorebi|Vehicle")
	FKomorebiVehicleTelemetry GetTelemetry() const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|Vehicle")
	EKomorebiVehicleKind GetVehicleKind() const { return VehicleKind; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Vehicle")
	EKomorebiVehicleDamageState GetDamageState() const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|Vehicle")
	bool IsPlayerDriven() const { return bPlayerDriven; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Vehicle")
	bool IsInitialized() const { return bInitialized; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Vehicle")
	bool IsRemoteDriven() const { return bRemoteNetDriven; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Vehicle")
	bool IsWrecked() const { return bWrecked; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Vehicle")
	bool IsTipped() const { return SimState.Tipped; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Vehicle")
	bool HasTrafficDriver() const { return bHasTrafficDriver; }

	void SetHasTrafficDriver(bool bHasDriver);

	UFUNCTION(BlueprintPure, Category = "Komorebi|Vehicle")
	APawn* GetDriverPawn() const { return DriverPawn.Get(); }

	KomorebiCore::VehicleState& GetSimState() { return SimState; }
	const KomorebiCore::VehicleState& GetSimState() const { return SimState; }
	const KomorebiCore::VehicleSpec& GetVehicleSpec() const;

	double GetHomeThreeX() const { return HomeThreeX; }
	double GetHomeThreeZ() const { return HomeThreeZ; }
	double GetHomeThreeYaw() const { return HomeThreeYaw; }
	float GetCurrentThrottle() const { return CurrentInputThrottle; }

	/** Returns the latest buffered driver input (keyboard/gamepad/EnhancedInput/AI). */
	KomorebiCore::VehicleDriveInput GetDriveInput() const
	{
		return KomorebiCore::VehicleDriveInput{
			static_cast<double>(CurrentInputThrottle),
			static_cast<double>(CurrentInputSteer),
			bCurrentInputHandbrake,
		};
	}

	// --- Components ----------------------------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Vehicle")
	TObjectPtr<USceneComponent> VehicleRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Vehicle")
	TObjectPtr<UBoxComponent> ChassisCollider;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Vehicle")
	TObjectPtr<USceneComponent> ChassisVisualPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Vehicle")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Vehicle")
	TObjectPtr<UStaticMeshComponent> RiderMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Vehicle")
	TObjectPtr<UStaticMeshComponent> SmokeVfxMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Vehicle")
	TObjectPtr<UStaticMeshComponent> FireVfxMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Vehicle")
	TObjectPtr<USpotLightComponent> HeadlightLeft;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Vehicle")
	TObjectPtr<USpotLightComponent> HeadlightRight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Vehicle")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Vehicle")
	TObjectPtr<UCameraComponent> ChaseCamera;

	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_VehicleKind, BlueprintReadOnly, Category = "Komorebi|Vehicle")
	EKomorebiVehicleKind VehicleKind = EKomorebiVehicleKind::Car;

	// --- Delegates -----------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Vehicle")
	FOnVehicleEntered OnVehicleEntered;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Vehicle")
	FOnVehicleExited OnVehicleExited;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Vehicle")
	FOnVehicleDriverCrashDamage OnDriverCrashDamage;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Vehicle")
	FOnVehicleCameraJolt OnCameraJolt;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Vehicle")
	FOnVehicleRamPawn OnVehicleRamPawn;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Vehicle")
	FOnVehicleWrecked OnVehicleWrecked;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Vehicle")
	FOnVehicleFeedbackText OnFeedbackText;

protected:
	UFUNCTION()
	void OnRep_VehicleKind();

	UFUNCTION()
	void OnRep_NetState();

	UFUNCTION(Server, Unreliable)
	void ServerSendNetState(const FKomorebiVehicleNetState& NewState);

private:
	void RebuildWheelRig();
	void ConfigureChassisVisualGeometry();
	double SampleHeightAt(double ThreeX, double ThreeZ) const;
	bool SweepChassisStatic(double StartThreeX, double StartThreeY, double StartThreeZ, double DirX, double DirZ, double DistMeters, double& OutImpactDist, double& OutNormalThreeX, double& OutNormalThreeZ) const;
	bool IsExitSpotClear(double TargetThreeX, double TargetThreeZ) const;
	void CheckRammingImpacts(double SpeedMps, double Ux, double Uz, double Fx, double Fz);
	void PollLocalDriverInput(float DeltaTime);
	void UpdateChaseCamera(float DeltaTime);
	void PushNetworkState(float DeltaTime);
	void PlayAudioSfx(EKomorebiSfx Sfx, float Intensity = 1.0f) const;

	KomorebiCore::VehicleState SimState;

	UPROPERTY(Transient)
	TArray<FKomorebiWheelRig> WheelRigs;

	double HomeThreeX = 0.0;
	double HomeThreeZ = 0.0;
	double HomeThreeYaw = 0.0;

	double LeanAngle = 0.0;
	double WheelSpinRad = 0.0;

	float HullHp = 140.0f;
	bool bWrecked = false;
	float BoomTimer = 0.0f;
	bool bVisualDirty = true;
	bool bInitialized = false;
	bool bManagedByTrafficSubsystem = false;
	bool bPlayerDriven = false;
	bool bHasTrafficDriver = false;
	bool bRemoteNetDriven = false;

	float CurrentInputThrottle = 0.0f;
	float CurrentInputSteer = 0.0f;
	bool bCurrentInputHandbrake = false;
	float BailWarningCooldown = 0.0f;
	float CameraLookIdleTimer = 0.0f;
	float NetSendAccum = 0.0f;
	float LastNetStateReceivedTime = 0.0f;

	static bool bFirstRideCommsPlayed;

	TWeakObjectPtr<APawn> DriverPawn;
	TMap<TWeakObjectPtr<APawn>, float> RamHitCooldowns;

	UPROPERTY(ReplicatedUsing = OnRep_NetState)
	FKomorebiVehicleNetState ReplicatedNetState;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CustomWheelMesh = nullptr;

	KomorebiCore::NeoKyotoTerrainMap FallbackTerrain;
	TFunction<double(double, double)> CustomHeightProvider;
};
