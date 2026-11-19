// Copyright Komorebi Project. Sustained frame-time auto-scaling graphics & stylized finish subsystem.
#pragma once

#include "CoreMinimal.h"
#include "KomorebiGameTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GraphicsSettingsSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnKomorebiQualityChanged,
	EKomorebiQualityTier, EffectiveTier,
	const FString&, StatusLabel);

/**
 * Mirrors legacy-web/src/quality.js: adapts graphics detail to sustained frame performance
 * (never guessed GPU strings), steps down sooner than it steps up, ignores pauses/hitches,
 * configures UGameUserSettings + world actors (ANeoKyotoCityActor, AStreetPropsActor,
 * ASnowboundWorldActor), and controls optional comic ink + canvas impasto finish.
 */
UCLASS(BlueprintType)
class KOMOREBIGAME_API UGraphicsSettingsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Broadcast whenever the active quality tier changes (manually or via Auto stepping). */
	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Graphics")
	FOnKomorebiQualityChanged OnQualityChanged;

	/** Selects Auto, Low, Medium (Balanced), or High quality tier and applies immediately. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Graphics")
	void SetQualityTier(EKomorebiQualityTier InTier);

	/**
	 * Feeds live frame delta time into the Auto quality policy (exact quality.js thresholds:
	 * > 23 ms for 2.5 s steps down; < 17.8 ms for 10 s steps up; 6 s cooldown; 3 s warmup).
	 * Returns true if the effective tier changed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Graphics")
	bool SampleFrameTime(float DeltaSeconds, bool bActiveGameplay = true);

	/** Toggles optional post-process depth-edge comic ink outlines and canvas impasto strength. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Graphics")
	void SetStylizedFinish(bool bComicInk, float ImpastoAmount);

	/** Convenience boolean toggle overload invoked via reflection by UKomorebiGameInstance. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Graphics")
	void SetStylization(bool bComicInk, bool bImpasto);

	/** Applies current quality tier and stylized finish to all world actors in the active map. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Graphics")
	void ApplyToCurrentWorld();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Komorebi|Graphics")
	EKomorebiQualityTier GetRequestedTier() const { return RequestedTier; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Komorebi|Graphics")
	EKomorebiQualityTier GetEffectiveTier() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Komorebi|Graphics")
	int32 GetSmoothedFps() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Komorebi|Graphics")
	FString GetQualityStatusLabel() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Komorebi|Graphics")
	bool IsComicInkEnabled() const { return bComicInkEnabled; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Komorebi|Graphics")
	float GetImpastoAmount() const { return ImpastoStrength; }

private:
	struct FQualityProfile
	{
		EKomorebiQualityTier Tier;
		const TCHAR* Label;
		float ResolutionScaleNormalized;
		float DecorationDensity;
		int32 SnowDriftCount;
		float StreetPropDistanceMeters;
		int32 ScalabilityGroupLevel;
	};

	static const FQualityProfile& ProfileForTier(EKomorebiQualityTier Tier);
	static EKomorebiQualityTier TierFromAutoIndex(int32 Index);
	static int32 AutoIndexFromTier(EKomorebiQualityTier Tier);

	void ResetAutoPolicy(int32 InitialIndex);
	void ApplyActiveProfile();
	void HandlePostLoadMap(UWorld* LoadedWorld);

	EKomorebiQualityTier RequestedTier = EKomorebiQualityTier::Auto;
	int32 AutoLevelIndex = 1; // 0 = Low, 1 = Medium, 2 = High

	float AverageFrameMs = 0.0f;
	float WarmupSeconds = 3.0f;
	float CooldownSeconds = 0.0f;
	float SlowSeconds = 0.0f;
	float FastSeconds = 0.0f;

	bool bComicInkEnabled = false;
	float ImpastoStrength = 0.0f;

	FDelegateHandle PostLoadMapHandle;
};
