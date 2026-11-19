// Copyright Komorebi Project. Real-time C++20 procedural DSP synth running on Unreal's cross-platform AudioMixer thread.
#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "Containers/Queue.h"
#include "KomorebiGameTypes.h"
#include "Game/AudioTheory.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include "KomorebiSynthComponent.generated.h"

UENUM(BlueprintType)
enum class EKomorebiSfx : uint8
{
	SwordSwing,
	HeavySwing,
	ParryRing,
	HitImpact,
	Dodge,
	Deflect,
	SparkBolt,
	DashWhoosh,
	FlarePulse,
	WhirlSlash,
	PlungeImpact,
	EnemyBolt,
	AimLock,
	PlayerHurt,
	GuiltAlarm,
	Step,
	EnterVehicle,
	ExitVehicle,
	VehicleCrash,
	VehicleHorn,
	ObjectHit,
	ObjectBreak,
	SyncTick,
	MissionStep,
	MissionDone,
	GateOpen,
	EchoCollect,
	UiClick
};

UENUM(BlueprintType)
enum class EKomorebiBarkKind : uint8
{
	Swing,
	Heavy,
	Hurt,
	Parry,
	Clear
};

/**
 * Zero-asset procedural C++20 DSP engine built on USynthComponent (AudioMixer thread).
 * Synthesizes adaptive D-Dorian/E-minor music, continuous vehicle engine & tire screech,
 * polyphonic combat/world/UI SFX, and 3-band biquad formant radio voices with zero WAV files.
 */
UCLASS(ClassGroup = (Audio), meta = (BlueprintSpawnableComponent))
class KOMOREBIGAME_API UKomorebiSynthComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	UKomorebiSynthComponent(const FObjectInitializer& ObjectInitializer);

	// --- USynthComponent Overrides (AudioMixer Thread) -----------------------------------------

	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

	// --- Game-Thread Control API (Lock-Free) ---------------------------------------------------

	/** Updates master/music/voice/sfx mix levels and mute state. */
	void SetMixSettings(const KomorebiCore::AudioSettings& Settings);

	/** Updates adaptive music parameters (chapter 0..5, combat intensity 0..1, boss, scene, pause, driving). */
	void SetMusicState(
		int32 ChapterIndex,
		float CombatIntensity,
		bool bBoss,
		bool bSnowbound,
		bool bPaused,
		bool bDriving = false,
		float SpeedMps = 0.0f);

	/** Updates continuous RPM vehicle engine & tire-drift screech synthesizer state. */
	void SetRideAudioState(
		bool bDriving,
		EKomorebiVehicleKind Kind,
		float SpeedMps,
		float TopSpeedMps,
		float Throttle,
		bool bSliding,
		bool bAirborne,
		bool bPaused);

	/** Triggers a procedural sound effect on the polyphonic SFX voice pool. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Audio")
	void TriggerSfx(EKomorebiSfx Sfx, float Intensity = 1.0f);

	/** Triggers a short vocal exertion formant bark. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Audio")
	void TriggerBark(EKomorebiBarkKind Bark);

	/** Queues a formant-synthesized radio transmission driven by KomorebiCore::SyllablesOf(). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Audio")
	void SpeakFormantLine(const FString& Text, const FString& SpeakerId = TEXT("sora"), bool bInterrupt = true, bool bSquelch = true);

	/** Immediately stops active formant radio synthesis. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Audio")
	void StopFormantVoice();

	/** True while the formant radio synthesizer is actively speaking. */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Audio")
	bool IsVoiceActive() const;

	/** Smoothed voice envelope level in [0..1] for HUD radio waveform visualization. */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Audio")
	float GetVoiceLevel() const;

private:
	enum class EWaveType : uint8
	{
		Sine,
		Triangle,
		Sawtooth,
		Square,
		Noise,
		FmBell
	};

	enum class EFilterMode : uint8
	{
		None,
		LowPass,
		HighPass,
		BandPass
	};

	struct FBiquad
	{
		float B0 = 1.0f;
		float B1 = 0.0f;
		float B2 = 0.0f;
		float A1 = 0.0f;
		float A2 = 0.0f;
		float Z1 = 0.0f;
		float Z2 = 0.0f;

		void Reset();
		void Configure(EFilterMode Mode, float FreqHz, float Q, float SampleRate);
		float Process(float In);
	};

	struct FVoice
	{
		bool bActive = false;
		bool bMusicBus = false;
		bool bVoiceBus = false;
		bool bSendDelay = false;
		bool bSendReverb = false;

		EWaveType Wave = EWaveType::Sine;
		EFilterMode FilterMode = EFilterMode::None;

		float DelaySeconds = 0.0f;
		float Elapsed = 0.0f;
		float Duration = 0.15f;
		float Attack = 0.006f;

		float StartFreq = 440.0f;
		float EndFreq = 440.0f;
		float DetuneCents = 0.0f;
		bool bDualDetune = false;

		float StartFilterFreq = 0.0f;
		float EndFilterFreq = 0.0f;
		float FilterQ = 1.0f;
		int32 FilterUpdateCountdown = 0;

		float Gain = 0.2f;
		float Pan = 0.0f;

		float Phase = 0.0f;
		float Phase2 = 0.0f;
		float ModPhase = 0.0f;
		float FmRatio = 2.0f;
		float FmIndexStart = 0.45f;
		float FmIndexEnd = 0.01f;

		FBiquad Filter;
	};

	struct FCombFilter
	{
		std::vector<float> Buffer;
		int32 Index = 0;
		float FilterStore = 0.0f;
		float Feedback = 0.78f;
		float Damp = 0.25f;

		void Init(int32 Size);
		float Process(float Input);
	};

	struct FAllpassFilter
	{
		std::vector<float> Buffer;
		int32 Index = 0;
		float Feedback = 0.5f;

		void Init(int32 Size);
		float Process(float Input);
	};

	struct FSfxCommand
	{
		EKomorebiSfx Sfx = EKomorebiSfx::UiClick;
		float Intensity = 1.0f;
	};

	struct FBarkCommand
	{
		EKomorebiBarkKind Bark = EKomorebiBarkKind::Swing;
	};

	static constexpr int32 MaxUtteranceSyllables = 160;

	struct FVoiceSyllableSlot
	{
		float Formants[3] = { 530.0f, 1840.0f, 2480.0f };
		float Duration = 0.08f;
		float Pause = 0.025f;
		float Stress = 1.0f;
		bool bUnvoiced = false;
	};

	struct FVoiceUtteranceCommand
	{
		bool bStopOnly = false;
		bool bInterrupt = true;
		bool bSquelch = true;
		float F0 = 175.0f;
		float FormantScale = 1.0f;
		int32 SyllableCount = 0;
		std::array<FVoiceSyllableSlot, MaxUtteranceSyllables> Syllables{};
	};

	// --- Audio-Thread Helpers ------------------------------------------------------------------

	float NextWhiteNoise();
	static float PolyBlep(float T, float Dt);
	static float RenderOscillator(EWaveType Wave, float Phase, float PhaseInc);

	FVoice* AllocateVoice(bool bMusicPool);
	void SpawnTone(
		bool bMusicPool,
		bool bVoiceBus,
		EWaveType Wave,
		float StartFreq,
		float EndFreq,
		float Duration,
		float Gain,
		float Attack = 0.006f,
		float DelaySec = 0.0f,
		EFilterMode FilterMode = EFilterMode::None,
		float StartFilterHz = 0.0f,
		float EndFilterHz = 0.0f,
		float FilterQ = 1.0f,
		bool bSendDelay = false,
		bool bSendReverb = false,
		float DetuneCents = 0.0f,
		bool bDualDetune = false,
		float Pan = 0.0f);

	void SpawnNoise(
		bool bMusicPool,
		bool bVoiceBus,
		float Duration,
		float Gain,
		EFilterMode FilterMode,
		float StartFilterHz,
		float EndFilterHz,
		float FilterQ = 1.2f,
		float DelaySec = 0.0f,
		float Pan = 0.0f);

	void DispatchSfxCommand(const FSfxCommand& Cmd);
	void DispatchBarkCommand(const FBarkCommand& Cmd);
	void SpawnRadioSquelch(bool bOpen, float DelaySec = 0.0f);

	void StepSequencer(int32 Step, bool bSnowbound, int32 Chapter, bool bBoss);
	void ScheduleAutumnStep(int32 Step, int32 Chapter, bool bBoss);
	void ScheduleAuroraStep(int32 Step);

	void RenderVoicePool(
		std::vector<FVoice>& Pool,
		float InvSr,
		float& OutMusicL,
		float& OutMusicR,
		float& OutDelayInL,
		float& OutDelayInR,
		float& OutReverbInL,
		float& OutReverbInR,
		float& OutSfxL,
		float& OutSfxR,
		float& OutVoice);

	float RenderFormantVoice(float InvSr);
	float RenderRideEngineAndSkid(float InvSr);
	float RenderAuroraWind(float InvSr, bool bSnowbound);

	// --- Lock-Free Shared Parameters (Game Thread -> Audio Thread) -----------------------------

	std::atomic<float> TargetMaster{ 0.8f };
	std::atomic<float> TargetMusic{ 0.55f };
	std::atomic<float> TargetVoice{ 0.85f };
	std::atomic<float> TargetSfx{ 0.75f };
	std::atomic<bool> bTargetMuted{ false };

	std::atomic<int32> TargetChapter{ 0 };
	std::atomic<float> TargetCombat{ 0.0f };
	std::atomic<bool> bTargetBoss{ false };
	std::atomic<bool> bTargetSnowbound{ false };
	std::atomic<bool> bTargetPaused{ false };
	std::atomic<bool> bTargetDriving{ false };
	std::atomic<float> TargetSpeedMps{ 0.0f };

	std::atomic<bool> bRideDriving{ false };
	std::atomic<int32> RideKind{ 0 };
	std::atomic<float> RideSpeedMps{ 0.0f };
	std::atomic<float> RideTopSpeedMps{ 25.0f };
	std::atomic<float> RideThrottle{ 0.0f };
	std::atomic<bool> bRideSliding{ false };
	std::atomic<bool> bRideAirborne{ false };
	std::atomic<bool> bRidePaused{ false };

	std::atomic<bool> bAtomicVoiceActive{ false };
	std::atomic<float> AtomicVoiceLevel{ 0.0f };

	TQueue<FSfxCommand, EQueueMode::Mpsc> SfxQueue;
	TQueue<FBarkCommand, EQueueMode::Mpsc> BarkQueue;
	TQueue<FVoiceUtteranceCommand, EQueueMode::Mpsc> VoiceQueue;

	// --- Audio-Thread-Only DSP State -----------------------------------------------------------

	float SampleRateHz = 48000.0f;
	uint32 NoiseSeed = 0x9E3779B9u;

	// Smoothed bus gains and filter cutoffs
	float SmoothedMaster = 0.8f;
	float SmoothedMusicGain = 0.55f;
	float SmoothedVoiceGain = 0.85f;
	float SmoothedSfxGain = 0.75f;
	float SmoothedMusicCutoff = 9000.0f;
	float CombatBlend = 0.0f;
	float DriveBlend = 0.0f;

	// Music master lowpass (950 Hz when paused, 9000 Hz active)
	FBiquad MusicMasterLpL;
	FBiquad MusicMasterLpR;
	int32 MusicFilterCountdown = 0;

	// Sequencer clock
	int32 SequencerStep = 0;
	double StepSampleAccum = 0.0;

	// Voice pools
	static constexpr int32 MusicVoiceCapacity = 64;
	static constexpr int32 SfxVoiceCapacity = 48;
	std::vector<FVoice> MusicVoices;
	std::vector<FVoice> SfxVoices;

	// Stereo feedback delay line
	std::vector<float> DelayBufL;
	std::vector<float> DelayBufR;
	int32 DelayWriteIdx = 0;
	int32 DelayBufLen = 48000;
	FBiquad DelayDampL;
	FBiquad DelayDampR;

	// Freeverb-style stereo reverb (4 combs + 2 allpass per channel)
	std::array<FCombFilter, 4> ReverbCombsL;
	std::array<FCombFilter, 4> ReverbCombsR;
	std::array<FAllpassFilter, 2> ReverbAllpassL;
	std::array<FAllpassFilter, 2> ReverbAllpassR;

	// Continuous engine & tire screech synth
	float EnginePhaseA = 0.0f;
	float EnginePhaseB = 0.0f;
	float EnginePhaseSub = 0.0f;
	float SmoothedEngineFreq = 54.0f;
	float SmoothedEngineFreqB = 108.0f;
	float SmoothedEngineCutoff = 320.0f;
	float SmoothedEngineGain = 0.0f;
	float SmoothedSkidGain = 0.0f;
	FBiquad EngineLowpass;
	FBiquad SkidBandpass;
	int32 EngineFilterCountdown = 0;

	// Snowbound ambient arctic wind pad
	float WindLfoPhase = 0.0f;
	float SmoothedWindGain = 0.0f;
	FBiquad WindBandpass;
	int32 WindFilterCountdown = 0;

	// Formant radio speech synthesizer
	bool bFormantActive = false;
	bool bFormantTailSquelch = false;
	float FormantLeadDelay = 0.0f;
	float FormantF0 = 175.0f;
	float FormantScale = 1.0f;
	int32 FormantSyllableCount = 0;
	int32 FormantSyllableIdx = 0;
	float FormantSyllableTime = 0.0f;
	float FormantPhase = 0.0f;
	float FormantEnvelopeSmooth = 0.0f;
	std::array<FVoiceSyllableSlot, MaxUtteranceSyllables> FormantSyllables{};
	FBiquad FormantBp[3];
	FBiquad FormantRadioBand;
	FBiquad FormantConsonantHp;

	// Master stereo dynamics compressor envelope
	float CompEnv = 0.0f;
};
