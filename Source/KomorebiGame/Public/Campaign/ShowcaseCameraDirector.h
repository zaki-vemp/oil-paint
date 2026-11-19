// Copyright Komorebi Project. Curated 6-shot cinematic investor showcase camera director.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "Game/ShowcaseData.h"
#include "World/NeoKyotoTerrain.h"

#include <functional>

#include "ShowcaseCameraDirector.generated.h"

class UCameraComponent;
class USceneComponent;

USTRUCT(BlueprintType)
struct KOMOREBIGAME_API FKomorebiShowcaseShotMeta
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Showcase")
	int32 ShotIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Showcase")
	int32 TotalShots = 6;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Showcase")
	FString CounterText = TEXT("01 / 06");

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Showcase")
	FString ShotId;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Showcase")
	FString Kicker;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Showcase")
	FString Title;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Showcase")
	FString Subtitle;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Showcase")
	FString PhaseLabel = TEXT("Neon Night");

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Showcase")
	bool bIsNight = true;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Showcase")
	FLinearColor AccentColor = FLinearColor(0.0f, 0.941f, 1.0f, 1.0f);

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Showcase")
	float Progress01 = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Showcase")
	bool bActive = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnShowcaseStateChanged, bool, bTourActive);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnShowcaseShotChanged, const FKomorebiShowcaseShotMeta&, ShotMeta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnShowcaseProgressUpdated, int32, ShotIndex, float, Progress01);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnShowcaseDayPhasePreview, bool, bOverrideActive, bool, bPreviewNight);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnShowcaseToast, const FString&, Message);

/**
 * Cinematic Investor Showcase & Camera Tour actor porting legacy-web/src/game/showcase.js.
 * Smoothly glides through all 6 curated 7-second Neo-Kyoto shots, previews Daylight/Night phases
 * per shot, broadcasts metadata to the Showcase Overlay HUD, and exits cleanly on WASD / Esc.
 */
UCLASS(Blueprintable)
class KOMOREBIGAME_API AShowcaseCameraDirector : public AActor
{
	GENERATED_BODY()

public:
	AShowcaseCameraDirector();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	// --- Tour Controls -------------------------------------------------------------------------

	/** Begins the 6-shot investor showcase tour starting at StartShotIndex (0..5). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Showcase")
	void StartTour(int32 StartShotIndex = 0);

	/** Exits the tour, restores live Day/Night phase, and returns camera control to the player. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Showcase")
	void StopTour(bool bSilent = false);

	/** Toggles the showcase tour on/off; returns the resulting active state. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Showcase")
	bool ToggleTour();

	/** Advances to the next curated shot (wrapping around to 0 after shot 5). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Showcase")
	void NextShot();

	/** Steps backward to the previous curated shot. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Showcase")
	void PreviousShot();

	/** Jumps directly to a specific shot index (wrapped into 0..5) and resets the 7s timer. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Showcase")
	void JumpToShot(int32 ShotIndex);

	/** Handles keyboard input (Space = next shot, WASD/Arrows/Esc = exit tour). Returns true if consumed. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Showcase")
	bool HandleInputKey(FKey PressedKey);

	/** Injects a custom terrain height callback in Three.js metres (defaults to NeoKyotoTerrainMap). */
	void SetTerrainHeightProvider(TFunction<double(double, double)> InHeightProvider);

	// --- Queries -------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Komorebi|Showcase")
	bool IsTourActive() const { return bTourActive; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Showcase")
	int32 GetCurrentShotIndex() const { return CurrentShotIndex; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Showcase")
	float GetCurrentShotProgress() const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|Showcase")
	FKomorebiShowcaseShotMeta GetCurrentShotMeta() const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|Showcase")
	FVector GetCurrentCameraWorldLocation() const { return CurrentEyeLocationUE; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Showcase")
	FVector GetCurrentFocusWorldLocation() const { return CurrentTargetLocationUE; }

	// --- Components & Config -------------------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Showcase")
	TObjectPtr<USceneComponent> DirectorRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Showcase")
	TObjectPtr<UCameraComponent> ShowcaseCamera;

	/** Optional blend duration when entering or exiting the tour camera (0 = crisp instant cut). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Showcase")
	float ViewTargetBlendSeconds = 0.25f;

	/** Duration of each showcase shot in seconds (canonical: 7.0s from KomorebiCore::kShotDuration). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Showcase")
	float ShotDurationSeconds = static_cast<float>(KomorebiCore::kShotDuration);

	// --- Delegates -----------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Showcase")
	FOnShowcaseStateChanged OnShowcaseStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Showcase")
	FOnShowcaseShotChanged OnShowcaseShotChanged;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Showcase")
	FOnShowcaseProgressUpdated OnShowcaseProgressUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Showcase")
	FOnShowcaseDayPhasePreview OnDayPhasePreviewChanged;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Showcase")
	FOnShowcaseToast OnShowcaseToast;

private:
	double SampleTerrainHeight(double ThreeX, double ThreeZ) const;
	void ApplyCurrentShotMetadata();
	void SampleShotPose(float Progress01);
	void PollPlayerControllerExitKeys();
	void RevealWorldAroundCamera() const;

	bool bTourActive = false;
	int32 CurrentShotIndex = 0;
	float ShotElapsed = 0.0f;
	float TourStartedWorldTime = 0.0f;

	FVector CurrentEyeLocationUE = FVector::ZeroVector;
	FVector CurrentTargetLocationUE = FVector::ZeroVector;

	TWeakObjectPtr<AActor> PreviousViewTarget;

	KomorebiCore::NeoKyotoTerrainMap FallbackTerrain;
	TFunction<double(double, double)> CustomHeightProvider;
};
