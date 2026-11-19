// Copyright Komorebi Project. GameInstance audio subsystem wrapping the procedural DSP synth and persistent mix settings.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Audio/KomorebiSynthComponent.h"
#include "KomorebiGameTypes.h"
#include "Game/AudioTheory.h"
#include "KomorebiAudioSubsystem.generated.h"

class AActor;
class UWorld;

USTRUCT(BlueprintType)
struct KOMOREBIGAME_API FKomorebiAudioSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Audio")
	float Master = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Audio")
	float Music = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Audio")
	float Voice = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Audio")
	float Sfx = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Audio")
	bool bMuted = false;

	KomorebiCore::AudioSettings ToCore() const
	{
		return KomorebiCore::AudioSettings{
			static_cast<double>(Master),
			static_cast<double>(Music),
			static_cast<double>(Voice),
			static_cast<double>(Sfx),
			bMuted,
		};
	}

	static FKomorebiAudioSettings FromCore(const KomorebiCore::AudioSettings& In)
	{
		const KomorebiCore::AudioSettings Clean = KomorebiCore::NormalizeAudioSettings(In);
		FKomorebiAudioSettings Out;
		Out.Master = static_cast<float>(Clean.Master);
		Out.Music = static_cast<float>(Clean.Music);
		Out.Voice = static_cast<float>(Clean.Voice);
		Out.Sfx = static_cast<float>(Clean.Sfx);
		Out.bMuted = Clean.bMuted;
		return Out;
	}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAudioSettingsChanged, const FKomorebiAudioSettings&, NewSettings);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAudioSaveWarning, const FString&, WarningMessage);

/**
 * Persistent game-instance audio controller bridging UI/gameplay systems with UKomorebiSynthComponent.
 * Automatically persists Master/Music/Voice/SFX/Mute mix settings into UKomorebiSaveGame.
 */
UCLASS()
class KOMOREBIGAME_API UKomorebiAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- Playback & Procedural Synth API -------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Audio")
	void PlaySfx(EKomorebiSfx Sfx, float Intensity = 1.0f);

	/** Convenience string-based SFX trigger accepting legacy-web names (slash, crash, horn, echoCollect, etc.). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Audio")
	void PlaySfxByName(FName SfxName, float Intensity = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Audio")
	void PlayBark(EKomorebiBarkKind Bark);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Audio")
	void SpeakRadioLine(
		const FString& Text,
		const FString& SpeakerId = TEXT("sora"),
		bool bInterrupt = true,
		bool bSquelch = true);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Audio")
	void StopRadioVoice();

	/** Updates adaptive music sequencer state (6-chapter Neo-Kyoto or Snowbound aurora). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Audio")
	void UpdateMusicState(int32 Chapter, bool bCombat, bool bBoss, bool bSnowbound, bool bPaused);

	/** Extended adaptive music updater allowing smooth combat intensity and driving energy layers. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Audio")
	void UpdateMusicStateDetailed(
		int32 Chapter,
		float CombatIntensity,
		bool bBoss,
		bool bSnowbound,
		bool bPaused,
		bool bDriving,
		float SpeedMps);

	/** Updates continuous RPM vehicle engine & tire-drift screech synth. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Audio")
	void UpdateRideAudio(
		bool bDriving,
		EKomorebiVehicleKind Kind,
		float SpeedMps,
		float TopSpeedMps,
		float Throttle,
		bool bSliding,
		bool bAirborne,
		bool bPaused);

	// --- Mix Settings & Persistence ------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Komorebi|Audio")
	FKomorebiAudioSettings GetAudioSettings() const { return CurrentSettings; }

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Audio")
	FKomorebiAudioSettings SetAudioSettings(const FKomorebiAudioSettings& NewSettings, bool bPersist = true);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Audio")
	void SetMasterVolume(float Value);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Audio")
	void SetMusicVolume(float Value);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Audio")
	void SetVoiceVolume(float Value);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Audio")
	void SetSfxVolume(float Value);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Audio")
	void SetMuted(bool bMute);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Audio")
	bool ToggleMute();

	UFUNCTION(BlueprintPure, Category = "Komorebi|Audio")
	bool IsVoiceActive() const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|Audio")
	float GetVoiceLevel() const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|Audio")
	UKomorebiSynthComponent* GetSynthComponent() const { return SynthComponent.Get(); }

	// --- Delegates -----------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Audio")
	FOnAudioSettingsChanged OnAudioSettingsChanged;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Audio")
	FOnAudioSaveWarning OnAudioSaveWarning;

private:
	void EnsureSynthReady(UWorld* TargetWorld = nullptr);
	void TeardownSynthHost();
	void PushAllStateToSynth();
	void PersistSettingsToSaveGame();

	void HandlePostWorldInit(UWorld* World, const UWorld::InitializationValues IVS);
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);

	FKomorebiAudioSettings CurrentSettings;

	int32 CachedChapter = 0;
	float CachedCombat = 0.0f;
	bool bCachedBoss = false;
	bool bCachedSnowbound = false;
	bool bCachedPaused = false;
	bool bCachedDriving = false;
	float CachedSpeedMps = 0.0f;

	TWeakObjectPtr<AActor> SynthHostActor;
	TWeakObjectPtr<UKomorebiSynthComponent> SynthComponent;

	FDelegateHandle PostWorldInitHandle;
	FDelegateHandle WorldCleanupHandle;
};
