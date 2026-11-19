// Copyright Komorebi Project. Third-person follow camera porting legacy-web/src/figure/camera.js to UE5.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "FollowCameraComponent.generated.h"

class UCameraComponent;
class APawn;
class AKomorebiTerrainActor;
class AKomorebiVehiclePawn;

/**
 * Damped third-person follow camera with shoulder offset, near-plane sphere sweep against ECC_WorldStatic,
 * terrain floor clamp with Sink, combat lock-on yaw tracking, hit-kick shake, vehicle chase auto-align &
 * speed boost, and free-camera override for the Showcase Tour.
 */
UCLASS(ClassGroup = (Komorebi), meta = (BlueprintSpawnableComponent))
class KOMOREBIGAME_API UFollowCameraComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UFollowCameraComponent();

	// --- Tuning parameters (centimetres & degrees, matching camera.js) ---

	/** Positive pitch looks down on Kael (0.22 rad ~= 12.605 deg in Neo-Kyoto, -4.01 deg in Snowbound). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Camera")
	float PitchDeg = 12.605f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Camera")
	float YawDeg = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Camera")
	float MinPitchDeg = -42.9718f; // -0.75 rad

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Camera")
	float MaxPitchDeg = 68.7549f; // +1.2 rad

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Camera")
	float DesiredDistanceCm = 720.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Camera")
	float MinDistanceCm = 280.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Camera")
	float MaxDistanceCm = 1300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Camera")
	float MaxBoostedDistanceCm = 1800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Camera")
	float ZoomStepCm = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Camera")
	float ShoulderOffsetCm = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Camera")
	float TargetHeightOffsetCm = 135.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Camera")
	float LookLiftCm = 25.0f;

	/** When terrain lifts the camera, look target drops by Raised * Sink (0 in Neo-Kyoto, 0.7 in Snowbound). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Camera", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Sink = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Camera")
	float PivotDampRateXY = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Camera")
	float PivotDampRateZ = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Camera")
	float DistanceRecoverRate = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Camera")
	float FloorClearanceCm = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Camera")
	float LookAheadCm = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Camera")
	float CameraFovDeg = 52.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Camera")
	bool bReducedMotion = false;

	// --- Setup & Presets ---

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Camera")
	void SetCameraComponent(UCameraComponent* InCamera);

	UFUNCTION(BlueprintPure, Category = "Komorebi|Camera")
	UCameraComponent* GetCameraComponent() const { return DrivenCamera; }

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Camera")
	void ApplyNeoKyotoPreset();

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Camera")
	void ApplySnowboundPreset();

	/** Optional terrain height callback (WorldXCm, WorldYCm) -> GroundZCm. Falls back to ECC_WorldStatic trace. */
	void SetGroundHeightProvider(TFunction<bool(float, float, float&)> InProvider);

	void SetTerrainActor(AKomorebiTerrainActor* InTerrainActor) { TerrainActorPtr = InTerrainActor; }

	AKomorebiTerrainActor* GetTerrainActor() const { return TerrainActorPtr; }

	// --- Frame Update & Controls ---

	/** Applies look deltas (degrees) and zoom steps, then sweeps and positions the driven camera. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Camera")
	void UpdateFollowCamera(float RealDeltaTime, const FVector& TargetFeetWorld, FVector2D LookDeltaDeg, float ZoomSteps);

	/** Exponentially tracks YawDeg toward TargetYawDeg for combat lock-on (rate 11). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Camera")
	void TrackYaw(float TargetYawDeg, float DeltaTime);

	/** Updates vehicle chase camera auto-align and speed-based distance boost. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Camera")
	void UpdateVehicleChase(float DeltaTime, APawn* VehiclePawn, float LookInputMagnitude);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Camera")
	void OnEnterVehicle(APawn* VehiclePawn);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Camera")
	void OnExitVehicle();

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Camera")
	void SetShake(float InShake);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Camera")
	void AddShake(float InShake);

	/** Locks the camera to an authored eye/target position (Showcase Tour / capture stills). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Camera")
	void SetFreeView(const FVector& EyeWorldLocation, const FVector& TargetWorldLocation);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Camera")
	void ClearFreeView();

	UFUNCTION(BlueprintPure, Category = "Komorebi|Camera")
	bool IsFreeViewActive() const { return bFreeViewActive; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Camera")
	FVector GetFreeViewTarget() const { return FreeViewTarget; }

	/** Resets pivot history on teleports/respawns so the camera snaps cleanly instead of streaking. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Camera")
	void ResetPivot();

	UFUNCTION(BlueprintPure, Category = "Komorebi|Camera")
	float GetCurrentDistanceCm() const { return CurrentDistanceCm; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Camera")
	FVector GetCameraWorldLocation() const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|Camera")
	FVector GetCameraForwardVector() const;

protected:
	virtual void BeginPlay() override;

	float QueryGroundHeightCm(float WorldXCm, float WorldYCm, float ReferenceZCm) const;

	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> DrivenCamera = nullptr;

	AKomorebiTerrainActor* TerrainActorPtr = nullptr;

	TWeakObjectPtr<APawn> IgnoredVehiclePawn;

	TFunction<bool(float, float, float&)> GroundHeightProvider;

	FVector PivotWorld = FVector::ZeroVector;
	bool bPivotInitialized = false;

	float CurrentDistanceCm = 720.0f;
	float SpeedDistanceBoostCm = 0.0f;
	float PreDriveDesiredDistanceCm = 720.0f;
	float VehicleLookIdleTime = 0.0f;
	bool bDrivingVehicle = false;

	float ShakeAmount = 0.0f;
	float ShakeTime = 0.0f;

	bool bFreeViewActive = false;
	FVector FreeViewEye = FVector::ZeroVector;
	FVector FreeViewTarget = FVector::ZeroVector;
};
