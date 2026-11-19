// Copyright Komorebi Project. Ten-minute day/night atmosphere, directional light, fog, and MPC_KomorebiShared driver.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Game/DayNightCycle.h"
#include "KomorebiGameTypes.h"
#include "DayNightAtmosphereActor.generated.h"

class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UMaterialParameterCollection;
class UMaterialParameterCollectionInstance;
class USkyAtmosphereComponent;
class USkyLightComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDayNightPhaseShifted, bool, bIsNight, const FString&, PhaseTitle);

/**
 * Drives the ten-minute day/night cycle (KomorebiCore::DayNightController),
 * directional sun/moon lighting, sky atmosphere, exponential height fog,
 * and every shared scalar/vector parameter in /Game/Materials/MPC_KomorebiShared.
 */
UCLASS(BlueprintType, Blueprintable)
class KOMOREBIGAME_API ADayNightAtmosphereActor : public AActor
{
	GENERATED_BODY()

public:
	ADayNightAtmosphereActor();

	/** Scene atmosphere profile (Neo-Kyoto city starts in day; Snowbound ridge starts in polar night). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Atmosphere")
	EKomorebiWorldScene WorldScene = EKomorebiWorldScene::NeoKyoto;

	/** Convenience flag synced with WorldScene for automated map builders. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Atmosphere")
	bool bIsSnowbound = false;

	/** Primary directional sunlight / moonlight source. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Atmosphere")
	TObjectPtr<UDirectionalLightComponent> SunMoonLight;

	/** Real-time atmospheric sky light for hemisphere fill. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Atmosphere")
	TObjectPtr<USkyLightComponent> SkyLight;

	/** Physically-based Rayleigh/Mie sky atmosphere. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Atmosphere")
	TObjectPtr<USkyAtmosphereComponent> SkyAtmosphere;

	/** Exponential height fog tinted by district haze, polar night, and twilight. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Komorebi|Atmosphere")
	TObjectPtr<UExponentialHeightFogComponent> HeightFog;

	/** Shared Material Parameter Collection (/Game/Materials/MPC_KomorebiShared). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Atmosphere")
	TObjectPtr<UMaterialParameterCollection> SharedMPC;

	/** Broadcast whenever the live day/night cycle crosses into a new phase. */
	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Atmosphere")
	FOnDayNightPhaseShifted OnPhaseShifted;

	/** Manually flips between Day and Night phases immediately. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Atmosphere")
	void ShiftPhase();

	/** Temporarily locks visuals to a preview phase (e.g. during Showcase tours); pass unset TOptional to resume live clock. */
	void SetPreviewPhase(TOptional<KomorebiCore::EDayPhase> PreviewPhase);

	/** Configures optional post-process depth-edge comic ink and canvas impasto modulation. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Atmosphere")
	void SetInkAndImpasto(float Ink, float Impasto);

	/** Switches between solo simulation time and synchronized UTC wall-clock phase (online city). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Atmosphere")
	void SetOnlineClock(bool bOnline);

	/** Returns the latest evaluated KomorebiCore day/night state. */
	const KomorebiCore::DayNightState& GetCurrentState() const { return Controller.GetState(); }

	/** Updates district palette vectors pushed to MPC_KomorebiShared (called by ANeoKyotoCityActor). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Atmosphere")
	void SetDistrictPaletteColors(
		const FLinearColor& InGrass,
		const FLinearColor& InPath,
		const FLinearColor& InFoliage,
		const FLinearColor& InLeafLight,
		const FLinearColor& InSkyHigh,
		const FLinearColor& InHaze);

	/** Updates the focus world position (cm) pushed to MPC_KomorebiShared. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Atmosphere")
	void SetFocusWorldLocation(const FVector& InFocusUE);

	/** Updates the carried lantern / warm dynamic light vector pushed to MPC_KomorebiShared. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Atmosphere")
	void SetLampState(const FVector& LampPosUE, float Strength, const FLinearColor& Color);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Komorebi|Atmosphere")
	FString GetClockLabel() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Komorebi|Atmosphere")
	bool IsNightPhase() const { return Controller.GetPhase() == KomorebiCore::EDayPhase::Night; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Komorebi|Atmosphere")
	float GetNightFactor() const { return static_cast<float>(Controller.GetNight()); }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Komorebi|Atmosphere")
	float GetTwilightFactor() const { return static_cast<float>(Controller.GetTwilight()); }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Komorebi|Atmosphere")
	float GetAuroraBreathing() const { return AuroraIntensity; }

	virtual void Tick(float DeltaTime) override;

protected:
	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void InitializeController();
	void ApplyAtmosphereAndMPC(const KomorebiCore::DayNightState& State);
	void EnsureMPCInstance();
	void HandlePhaseShift(KomorebiCore::EDayPhase NewPhase, const KomorebiCore::DayNightState& NewState);

	KomorebiCore::DayNightController Controller;
	double ElapsedWorldTime = 0.0;
	bool bOnlineClock = false;

	float InkStrength = 0.0f;
	float ImpastoStrength = 0.0f;
	float AuroraIntensity = 0.0f;
	bool bHasExternalFocus = false;

	FLinearColor ChapterGrass = FLinearColor(0.60f, 0.62f, 0.65f, 1.0f);
	FLinearColor ChapterPath = FLinearColor(0.34f, 0.36f, 0.40f, 1.0f);
	FLinearColor ChapterFoliage = FLinearColor(0.0f, 0.85f, 0.95f, 1.0f);
	FLinearColor ChapterLeafLight = FLinearColor(1.0f, 0.86f, 0.45f, 1.0f);
	FLinearColor SkyHighColor = FLinearColor(0.18f, 0.52f, 0.88f, 1.0f);
	FLinearColor HazeColor = FLinearColor(0.70f, 0.81f, 0.91f, 1.0f);
	FLinearColor LampPosStrength = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
	FLinearColor LampColor = FLinearColor(1.0f, 0.62f, 0.25f, 1.0f);
	FVector FocusWorldLocation = FVector::ZeroVector;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollectionInstance> CachedMPCInstance;
};
