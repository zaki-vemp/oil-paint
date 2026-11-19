// Copyright Komorebi Project. Real-time C++20 procedural DSP synth running on Unreal's cross-platform AudioMixer thread.
#include "Audio/KomorebiSynthComponent.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace
{
	constexpr float kTwoPi = 6.28318530717958647692f;
	constexpr float kPiF = 3.14159265358979323846f;

	inline float ClampF(float V, float Lo, float Hi)
	{
		return FMath::Clamp(V, Lo, Hi);
	}

	inline float LerpExp(float Start, float End, float T01)
	{
		const float SafeStart = FMath::Max(15.0f, Start);
		const float SafeEnd = FMath::Max(15.0f, End);
		const float Ratio = SafeEnd / SafeStart;
		return SafeStart * FMath::Pow(Ratio, ClampF(T01, 0.0f, 1.0f));
	}

	inline float FlushDenormal(float X)
	{
		return (FMath::Abs(X) < 1.0e-18f) ? 0.0f : X;
	}
}

// --- FBiquad -----------------------------------------------------------------------------------

void UKomorebiSynthComponent::FBiquad::Reset()
{
	Z1 = 0.0f;
	Z2 = 0.0f;
}

void UKomorebiSynthComponent::FBiquad::Configure(EFilterMode Mode, float FreqHz, float Q, float SampleRate)
{
	if (Mode == EFilterMode::None || SampleRate <= 0.0f)
	{
		B0 = 1.0f;
		B1 = 0.0f;
		B2 = 0.0f;
		A1 = 0.0f;
		A2 = 0.0f;
		return;
	}

	const float ClampedFreq = ClampF(FreqHz, 20.0f, SampleRate * 0.45f);
	const float ClampedQ = ClampF(Q, 0.25f, 18.0f);
	const float W0 = kTwoPi * ClampedFreq / SampleRate;
	const float CosW0 = std::cos(W0);
	const float SinW0 = std::sin(W0);
	const float Alpha = SinW0 / (2.0f * ClampedQ);

	float b0 = 1.0f;
	float b1 = 0.0f;
	float b2 = 0.0f;
	const float a0 = 1.0f + Alpha;
	const float a1 = -2.0f * CosW0;
	const float a2 = 1.0f - Alpha;

	switch (Mode)
	{
		case EFilterMode::LowPass:
			b0 = (1.0f - CosW0) * 0.5f;
			b1 = 1.0f - CosW0;
			b2 = (1.0f - CosW0) * 0.5f;
			break;
		case EFilterMode::HighPass:
			b0 = (1.0f + CosW0) * 0.5f;
			b1 = -(1.0f + CosW0);
			b2 = (1.0f + CosW0) * 0.5f;
			break;
		case EFilterMode::BandPass:
			// Constant-0 dB peak gain bandpass (matches Web Audio BiquadFilterNode bandpass).
			b0 = Alpha;
			b1 = 0.0f;
			b2 = -Alpha;
			break;
		default:
			break;
	}

	const float InvA0 = 1.0f / a0;
	B0 = b0 * InvA0;
	B1 = b1 * InvA0;
	B2 = b2 * InvA0;
	A1 = a1 * InvA0;
	A2 = a2 * InvA0;
}

float UKomorebiSynthComponent::FBiquad::Process(float In)
{
	const float Out = B0 * In + Z1;
	Z1 = FlushDenormal(B1 * In - A1 * Out + Z2);
	Z2 = FlushDenormal(B2 * In - A2 * Out);
	return Out;
}

// --- FCombFilter & FAllpassFilter --------------------------------------------------------------

void UKomorebiSynthComponent::FCombFilter::Init(int32 Size)
{
	Buffer.assign(FMath::Max(16, Size), 0.0f);
	Index = 0;
	FilterStore = 0.0f;
}

float UKomorebiSynthComponent::FCombFilter::Process(float Input)
{
	const float Output = Buffer[Index];
	FilterStore = FlushDenormal(Output * (1.0f - Damp) + FilterStore * Damp);
	Buffer[Index] = FlushDenormal(Input + FilterStore * Feedback);
	if (++Index >= static_cast<int32>(Buffer.size()))
	{
		Index = 0;
	}
	return Output;
}

void UKomorebiSynthComponent::FAllpassFilter::Init(int32 Size)
{
	Buffer.assign(FMath::Max(16, Size), 0.0f);
	Index = 0;
}

float UKomorebiSynthComponent::FAllpassFilter::Process(float Input)
{
	const float Buffered = Buffer[Index];
	const float Output = -Input + Buffered;
	Buffer[Index] = FlushDenormal(Input + Buffered * Feedback);
	if (++Index >= static_cast<int32>(Buffer.size()))
	{
		Index = 0;
	}
	return Output;
}

// --- UKomorebiSynthComponent -------------------------------------------------------------------

UKomorebiSynthComponent::UKomorebiSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAutoActivate = false;
	bAllowSpatialization = false;
	bIsUISound = true;
}

bool UKomorebiSynthComponent::Init(int32& SampleRate)
{
	NumChannels = 2;
	SampleRateHz = (SampleRate > 8000) ? static_cast<float>(SampleRate) : 48000.0f;

	MusicVoices.assign(MusicVoiceCapacity, FVoice{});
	SfxVoices.assign(SfxVoiceCapacity, FVoice{});

	// Pre-allocate 1.0s stereo delay ring buffer.
	DelayBufLen = FMath::Max(4096, static_cast<int32>(SampleRateHz));
	DelayBufL.assign(DelayBufLen, 0.0f);
	DelayBufR.assign(DelayBufLen, 0.0f);
	DelayWriteIdx = 0;
	DelayDampL.Configure(EFilterMode::LowPass, 2600.0f, 0.707f, SampleRateHz);
	DelayDampR.Configure(EFilterMode::LowPass, 2600.0f, 0.707f, SampleRateHz);

	// Pre-allocate Schroeder/Freeverb stereo reverb lines scaled to current sample rate.
	const float SrScale = SampleRateHz / 44100.0f;
	static constexpr int32 kCombLengthsL[4] = { 1116, 1188, 1277, 1356 };
	static constexpr int32 kCombLengthsR[4] = { 1139, 1211, 1300, 1379 };
	static constexpr int32 kAllpassLengthsL[2] = { 556, 441 };
	static constexpr int32 kAllpassLengthsR[2] = { 579, 464 };

	for (int32 i = 0; i < 4; ++i)
	{
		ReverbCombsL[i].Init(static_cast<int32>(kCombLengthsL[i] * SrScale));
		ReverbCombsR[i].Init(static_cast<int32>(kCombLengthsR[i] * SrScale));
		ReverbCombsL[i].Feedback = 0.80f;
		ReverbCombsR[i].Feedback = 0.80f;
		ReverbCombsL[i].Damp = 0.28f;
		ReverbCombsR[i].Damp = 0.28f;
	}
	for (int32 i = 0; i < 2; ++i)
	{
		ReverbAllpassL[i].Init(static_cast<int32>(kAllpassLengthsL[i] * SrScale));
		ReverbAllpassR[i].Init(static_cast<int32>(kAllpassLengthsR[i] * SrScale));
	}

	MusicMasterLpL.Configure(EFilterMode::LowPass, 9000.0f, 0.707f, SampleRateHz);
	MusicMasterLpR.Configure(EFilterMode::LowPass, 9000.0f, 0.707f, SampleRateHz);

	EngineLowpass.Configure(EFilterMode::LowPass, 320.0f, 0.9f, SampleRateHz);
	SkidBandpass.Configure(EFilterMode::BandPass, 1600.0f, 2.4f, SampleRateHz);
	WindBandpass.Configure(EFilterMode::BandPass, 340.0f, 1.4f, SampleRateHz);

	FormantRadioBand.Configure(EFilterMode::BandPass, 1450.0f, 0.7f, SampleRateHz);
	FormantConsonantHp.Configure(EFilterMode::HighPass, 3200.0f, 0.707f, SampleRateHz);
	for (int32 i = 0; i < 3; ++i)
	{
		FormantBp[i].Reset();
	}

	SequencerStep = 0;
	StepSampleAccum = 0.0;
	CompEnv = 0.0f;

	return true;
}

// --- Game-Thread Lock-Free Setters -------------------------------------------------------------

void UKomorebiSynthComponent::SetMixSettings(const KomorebiCore::AudioSettings& Settings)
{
	const KomorebiCore::AudioSettings Clean = KomorebiCore::NormalizeAudioSettings(Settings);
	TargetMaster.store(static_cast<float>(Clean.Master), std::memory_order_relaxed);
	TargetMusic.store(static_cast<float>(Clean.Music), std::memory_order_relaxed);
	TargetVoice.store(static_cast<float>(Clean.Voice), std::memory_order_relaxed);
	TargetSfx.store(static_cast<float>(Clean.Sfx), std::memory_order_relaxed);
	bTargetMuted.store(Clean.bMuted, std::memory_order_relaxed);
}

void UKomorebiSynthComponent::SetMusicState(
	int32 ChapterIndex,
	float CombatIntensity,
	bool bBoss,
	bool bSnowbound,
	bool bPaused,
	bool bDriving,
	float SpeedMps)
{
	TargetChapter.store(FMath::Clamp(ChapterIndex, 0, 5), std::memory_order_relaxed);
	TargetCombat.store(FMath::Clamp(CombatIntensity, 0.0f, 1.0f), std::memory_order_relaxed);
	bTargetBoss.store(bBoss, std::memory_order_relaxed);
	bTargetSnowbound.store(bSnowbound, std::memory_order_relaxed);
	bTargetPaused.store(bPaused, std::memory_order_relaxed);
	bTargetDriving.store(bDriving, std::memory_order_relaxed);
	TargetSpeedMps.store(FMath::Abs(SpeedMps), std::memory_order_relaxed);
}

void UKomorebiSynthComponent::SetRideAudioState(
	bool bDriving,
	EKomorebiVehicleKind Kind,
	float SpeedMps,
	float TopSpeedMps,
	float Throttle,
	bool bSliding,
	bool bAirborne,
	bool bPaused)
{
	bRideDriving.store(bDriving, std::memory_order_relaxed);
	RideKind.store(static_cast<int32>(Kind), std::memory_order_relaxed);
	RideSpeedMps.store(FMath::Abs(SpeedMps), std::memory_order_relaxed);
	RideTopSpeedMps.store(FMath::Max(1.0f, TopSpeedMps), std::memory_order_relaxed);
	RideThrottle.store(FMath::Clamp(Throttle, -1.0f, 1.0f), std::memory_order_relaxed);
	bRideSliding.store(bSliding, std::memory_order_relaxed);
	bRideAirborne.store(bAirborne, std::memory_order_relaxed);
	bRidePaused.store(bPaused, std::memory_order_relaxed);
}

void UKomorebiSynthComponent::TriggerSfx(EKomorebiSfx Sfx, float Intensity)
{
	SfxQueue.Enqueue(FSfxCommand{ Sfx, FMath::Clamp(Intensity, 0.4f, 1.6f) });
}

void UKomorebiSynthComponent::TriggerBark(EKomorebiBarkKind Bark)
{
	BarkQueue.Enqueue(FBarkCommand{ Bark });
}

void UKomorebiSynthComponent::SpeakFormantLine(
	const FString& Text,
	const FString& SpeakerId,
	bool bInterrupt,
	bool bSquelch)
{
	const FString Trimmed = Text.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return;
	}

	const std::string Utf8Text(TCHAR_TO_UTF8(*Trimmed));
	const std::string Utf8Speaker(TCHAR_TO_UTF8(*SpeakerId.ToLower()));
	const KomorebiCore::SpeakerProfile& Profile = KomorebiCore::FindSpeaker(Utf8Speaker);
	const std::vector<KomorebiCore::Syllable> Parsed = KomorebiCore::SyllablesOf(Utf8Text);

	if (Parsed.empty())
	{
		return;
	}

	FVoiceUtteranceCommand Cmd;
	Cmd.bStopOnly = false;
	Cmd.bInterrupt = bInterrupt;
	Cmd.bSquelch = bSquelch;
	Cmd.F0 = static_cast<float>(Profile.F0);

	const std::string_view ProfileId(Profile.Id);
	Cmd.FormantScale = (ProfileId == "sora") ? 1.10f : (Profile.bPreferFemale ? 1.05f : 0.96f);

	const int32 Count = FMath::Min(MaxUtteranceSyllables, static_cast<int32>(Parsed.size()));
	Cmd.SyllableCount = Count;
	for (int32 i = 0; i < Count; ++i)
	{
		const KomorebiCore::Syllable& S = Parsed[i];
		FVoiceSyllableSlot& Slot = Cmd.Syllables[i];
		Slot.Formants[0] = static_cast<float>(S.Formants[0]);
		Slot.Formants[1] = static_cast<float>(S.Formants[1]);
		Slot.Formants[2] = static_cast<float>(S.Formants[2]);
		Slot.Duration = static_cast<float>(S.Duration);
		Slot.Pause = static_cast<float>(S.Pause);
		Slot.Stress = static_cast<float>(S.Stress);
		Slot.bUnvoiced = S.bUnvoiced;
	}

	VoiceQueue.Enqueue(Cmd);
}

void UKomorebiSynthComponent::StopFormantVoice()
{
	FVoiceUtteranceCommand Cmd;
	Cmd.bStopOnly = true;
	VoiceQueue.Enqueue(Cmd);
}

bool UKomorebiSynthComponent::IsVoiceActive() const
{
	return bAtomicVoiceActive.load(std::memory_order_relaxed);
}

float UKomorebiSynthComponent::GetVoiceLevel() const
{
	return AtomicVoiceLevel.load(std::memory_order_relaxed);
}

// --- Audio-Thread DSP Primitives ---------------------------------------------------------------

float UKomorebiSynthComponent::NextWhiteNoise()
{
	uint32 X = NoiseSeed;
	X ^= X << 13;
	X ^= X >> 17;
	X ^= X << 5;
	NoiseSeed = X;
	return static_cast<float>(static_cast<int32>(X)) * (1.0f / 2147483648.0f);
}

float UKomorebiSynthComponent::PolyBlep(float T, float Dt)
{
	if (Dt <= 0.0f)
	{
		return 0.0f;
	}
	if (T < Dt)
	{
		const float U = T / Dt;
		return U + U - U * U - 1.0f;
	}
	if (T > 1.0f - Dt)
	{
		const float U = (T - 1.0f) / Dt;
		return U * U + U + U + 1.0f;
	}
	return 0.0f;
}

float UKomorebiSynthComponent::RenderOscillator(EWaveType Wave, float Phase, float PhaseInc)
{
	switch (Wave)
	{
		case EWaveType::Sine:
			return std::sin(Phase * kTwoPi);
		case EWaveType::Triangle:
		{
			const float Raw = 2.0f * Phase - 1.0f;
			return 2.0f * (FMath::Abs(Raw) - 0.5f);
		}
		case EWaveType::Sawtooth:
		{
			const float Raw = 2.0f * Phase - 1.0f;
			return Raw - PolyBlep(Phase, PhaseInc);
		}
		case EWaveType::Square:
		{
			float Raw = (Phase < 0.5f) ? 1.0f : -1.0f;
			Raw += PolyBlep(Phase, PhaseInc);
			float Shifted = Phase + 0.5f;
			if (Shifted >= 1.0f)
			{
				Shifted -= 1.0f;
			}
			Raw -= PolyBlep(Shifted, PhaseInc);
			return Raw;
		}
		default:
			return 0.0f;
	}
}

UKomorebiSynthComponent::FVoice* UKomorebiSynthComponent::AllocateVoice(bool bMusicPool)
{
	std::vector<FVoice>& Pool = bMusicPool ? MusicVoices : SfxVoices;
	FVoice* Oldest = nullptr;
	float MaxAge = -1.0f;

	for (FVoice& V : Pool)
	{
		if (!V.bActive)
		{
			return &V;
		}
		if (V.Elapsed > MaxAge)
		{
			MaxAge = V.Elapsed;
			Oldest = &V;
		}
	}
	return Oldest;
}

void UKomorebiSynthComponent::SpawnTone(
	bool bMusicPool,
	bool bVoiceBus,
	EWaveType Wave,
	float StartFreq,
	float EndFreq,
	float Duration,
	float Gain,
	float Attack,
	float DelaySec,
	EFilterMode FilterMode,
	float StartFilterHz,
	float EndFilterHz,
	float FilterQ,
	bool bSendDelay,
	bool bSendReverb,
	float DetuneCents,
	bool bDualDetune,
	float Pan)
{
	FVoice* V = AllocateVoice(bMusicPool);
	if (!V)
	{
		return;
	}

	*V = FVoice{};
	V->bActive = true;
	V->bMusicBus = bMusicPool;
	V->bVoiceBus = bVoiceBus;
	V->bSendDelay = bSendDelay;
	V->bSendReverb = bSendReverb;
	V->Wave = Wave;
	V->FilterMode = FilterMode;
	V->DelaySeconds = FMath::Max(0.0f, DelaySec);
	V->Elapsed = 0.0f;
	V->Duration = FMath::Max(0.01f, Duration);
	V->Attack = FMath::Clamp(Attack, 0.001f, V->Duration * 0.6f);
	V->StartFreq = FMath::Max(20.0f, StartFreq);
	V->EndFreq = FMath::Max(20.0f, EndFreq > 0.0f ? EndFreq : StartFreq);
	V->DetuneCents = DetuneCents;
	V->bDualDetune = bDualDetune;
	V->StartFilterFreq = StartFilterHz;
	V->EndFilterFreq = (EndFilterHz > 0.0f) ? EndFilterHz : StartFilterHz;
	V->FilterQ = FilterQ;
	V->Gain = Gain;
	V->Pan = ClampF(Pan, -0.85f, 0.85f);
	V->Filter.Reset();

	if (FilterMode != EFilterMode::None && StartFilterHz > 0.0f)
	{
		V->Filter.Configure(FilterMode, StartFilterHz, FilterQ, SampleRateHz);
	}
}

void UKomorebiSynthComponent::SpawnNoise(
	bool bMusicPool,
	bool bVoiceBus,
	float Duration,
	float Gain,
	EFilterMode FilterMode,
	float StartFilterHz,
	float EndFilterHz,
	float FilterQ,
	float DelaySec,
	float Pan)
{
	SpawnTone(
		bMusicPool,
		bVoiceBus,
		EWaveType::Noise,
		440.0f,
		440.0f,
		Duration,
		Gain,
		0.006f,
		DelaySec,
		FilterMode,
		StartFilterHz,
		EndFilterHz,
		FilterQ,
		false,
		false,
		0.0f,
		false,
		Pan);
}

void UKomorebiSynthComponent::SpawnRadioSquelch(bool bOpen, float DelaySec)
{
	SpawnNoise(
		false,
		true,
		bOpen ? 0.065f : 0.05f,
		0.14f,
		EFilterMode::BandPass,
		bOpen ? 2400.0f : 1650.0f,
		bOpen ? 3200.0f : 900.0f,
		2.2f,
		DelaySec);
	SpawnTone(
		false,
		true,
		EWaveType::Sine,
		bOpen ? 1180.0f : 880.0f,
		bOpen ? 1520.0f : 620.0f,
		0.055f,
		0.09f,
		0.004f,
		DelaySec);
}

// --- Procedural SFX & Bark Synthesis (1:1 port of legacy-web/src/game/audio.js) ---------------

void UKomorebiSynthComponent::DispatchSfxCommand(const FSfxCommand& Cmd)
{
	const float S = ClampF(Cmd.Intensity, 0.4f, 1.6f);
	const bool bSnowbound = bTargetSnowbound.load(std::memory_order_relaxed);

	switch (Cmd.Sfx)
	{
		case EKomorebiSfx::SwordSwing:
			SpawnNoise(false, false, 0.11f, 0.19f, EFilterMode::BandPass, 2400.0f * S, 750.0f, 1.5f);
			SpawnTone(false, false, EWaveType::Triangle, 520.0f * S, 190.0f, 0.10f, 0.09f);
			break;

		case EKomorebiSfx::HeavySwing:
			SpawnNoise(false, false, 0.22f, 0.26f, EFilterMode::BandPass, 1400.0f, 260.0f, 1.1f);
			SpawnTone(false, false, EWaveType::Sawtooth, 240.0f, 72.0f, 0.22f, 0.16f, 0.006f, 0.0f, EFilterMode::LowPass, 900.0f, 900.0f, 1.0f);
			break;

		case EKomorebiSfx::HitImpact:
			SpawnTone(false, false, EWaveType::Triangle, 195.0f * S, 55.0f, 0.12f, 0.28f);
			SpawnNoise(false, false, 0.08f, 0.22f, EFilterMode::LowPass, 1800.0f, 1800.0f, 1.0f);
			break;

		case EKomorebiSfx::ParryRing:
			SpawnTone(false, false, EWaveType::Sine, 1480.0f, 2150.0f, 0.26f, 0.25f);
			SpawnTone(false, false, EWaveType::Triangle, 2240.0f, 1880.0f, 0.22f, 0.16f, 0.006f, 0.015f);
			break;

		case EKomorebiSfx::Dodge:
			SpawnNoise(false, false, 0.16f, 0.18f, EFilterMode::HighPass, 1800.0f, 4200.0f, 1.0f);
			SpawnTone(false, false, EWaveType::Sine, 440.0f, 880.0f, 0.18f, 0.14f);
			break;

		case EKomorebiSfx::Deflect:
			SpawnTone(false, false, EWaveType::Triangle, 920.0f, 1760.0f, 0.18f, 0.22f);
			SpawnNoise(false, false, 0.09f, 0.18f, EFilterMode::BandPass, 3400.0f, 3400.0f, 1.2f);
			break;

		case EKomorebiSfx::SparkBolt:
			SpawnTone(false, false, EWaveType::Sine, 720.0f, 1320.0f, 0.14f, 0.18f);
			SpawnTone(false, false, EWaveType::Triangle, 1120.0f, 560.0f, 0.16f, 0.10f, 0.006f, 0.02f);
			break;

		case EKomorebiSfx::DashWhoosh:
			SpawnNoise(false, false, 0.15f, 0.20f, EFilterMode::BandPass, 950.0f, 2800.0f, 1.0f);
			break;

		case EKomorebiSfx::FlarePulse:
			SpawnTone(false, false, EWaveType::Sawtooth, 320.0f, 960.0f, 0.35f, 0.20f, 0.006f, 0.0f, EFilterMode::LowPass, 2400.0f, 2400.0f, 1.0f);
			SpawnNoise(false, false, 0.32f, 0.20f, EFilterMode::BandPass, 2200.0f, 600.0f, 1.2f);
			break;

		case EKomorebiSfx::WhirlSlash:
			SpawnNoise(false, false, 0.28f, 0.22f, EFilterMode::BandPass, 1200.0f, 2600.0f, 2.0f);
			SpawnTone(false, false, EWaveType::Triangle, 340.0f, 680.0f, 0.26f, 0.15f);
			break;

		case EKomorebiSfx::PlungeImpact:
			SpawnTone(false, false, EWaveType::Sine, 140.0f, 36.0f, 0.28f, 0.32f);
			SpawnNoise(false, false, 0.22f, 0.25f, EFilterMode::LowPass, 1100.0f, 240.0f, 1.0f);
			break;

		case EKomorebiSfx::EnemyBolt:
			SpawnTone(false, false, EWaveType::Sawtooth, 980.0f, 310.0f, 0.12f, 0.15f, 0.006f, 0.0f, EFilterMode::LowPass, 2600.0f, 2600.0f, 1.0f);
			break;

		case EKomorebiSfx::AimLock:
			SpawnTone(false, false, EWaveType::Sine, 1320.0f, 1320.0f, 0.07f, 0.11f);
			SpawnTone(false, false, EWaveType::Sine, 1760.0f, 1760.0f, 0.07f, 0.11f, 0.006f, 0.09f);
			break;

		case EKomorebiSfx::PlayerHurt:
			SpawnTone(false, false, EWaveType::Sawtooth, 165.0f, 62.0f, 0.18f, 0.26f, 0.006f, 0.0f, EFilterMode::LowPass, 700.0f, 700.0f, 1.0f);
			SpawnNoise(false, false, 0.12f, 0.20f, EFilterMode::LowPass, 900.0f, 900.0f, 1.0f);
			break;

		case EKomorebiSfx::GuiltAlarm:
			SpawnTone(false, false, EWaveType::Sawtooth, 220.0f, 146.0f, 0.26f, 0.20f, 0.006f, 0.0f, EFilterMode::LowPass, 850.0f, 850.0f, 1.0f);
			break;

		case EKomorebiSfx::Step:
			SpawnNoise(false, false, 0.045f, 0.055f * S, EFilterMode::LowPass, bSnowbound ? 680.0f : 950.0f, bSnowbound ? 680.0f : 950.0f, 1.0f);
			break;

		case EKomorebiSfx::EnterVehicle:
			SpawnTone(false, false, EWaveType::Sawtooth, 95.0f, 220.0f, 0.22f, 0.18f, 0.006f, 0.0f, EFilterMode::LowPass, 850.0f, 850.0f, 1.0f);
			break;

		case EKomorebiSfx::ExitVehicle:
			SpawnTone(false, false, EWaveType::Triangle, 180.0f, 90.0f, 0.12f, 0.14f);
			break;

		case EKomorebiSfx::VehicleCrash:
		{
			const float CrashGain = FMath::Min(1.4f, S);
			SpawnTone(false, false, EWaveType::Sawtooth, 110.0f, 34.0f, 0.24f, 0.28f * CrashGain, 0.006f, 0.0f, EFilterMode::LowPass, 950.0f, 950.0f, 1.0f);
			SpawnNoise(false, false, 0.20f, 0.26f * CrashGain, EFilterMode::LowPass, 1600.0f, 1600.0f, 1.0f);
			break;
		}

		case EKomorebiSfx::VehicleHorn:
			SpawnTone(false, false, EWaveType::Sawtooth, 392.0f, 392.0f, 0.18f, 0.14f, 0.006f, 0.0f, EFilterMode::LowPass, 1600.0f, 1600.0f, 1.0f);
			SpawnTone(false, false, EWaveType::Sawtooth, 493.88f, 493.88f, 0.18f, 0.14f, 0.006f, 0.0f, EFilterMode::LowPass, 1600.0f, 1600.0f, 1.0f);
			break;

		case EKomorebiSfx::ObjectHit:
			SpawnTone(false, false, EWaveType::Square, 420.0f, 220.0f, 0.09f, 0.16f, 0.006f, 0.0f, EFilterMode::LowPass, 1600.0f, 1600.0f, 1.0f);
			break;

		case EKomorebiSfx::ObjectBreak:
			SpawnTone(false, false, EWaveType::Sawtooth, 280.0f, 68.0f, 0.34f, 0.26f, 0.006f, 0.0f, EFilterMode::LowPass, 1800.0f, 1800.0f, 1.0f);
			SpawnNoise(false, false, 0.26f, 0.22f, EFilterMode::BandPass, 1200.0f, 320.0f, 1.2f);
			break;

		case EKomorebiSfx::SyncTick:
			SpawnTone(false, false, EWaveType::Sine, 620.0f + S * 420.0f, 620.0f + S * 420.0f, 0.06f, 0.09f);
			break;

		case EKomorebiSfx::MissionStep:
			SpawnTone(false, false, EWaveType::Triangle, 523.25f, 523.25f, 0.11f, 0.17f);
			SpawnTone(false, false, EWaveType::Triangle, 659.25f, 659.25f, 0.16f, 0.17f, 0.006f, 0.08f);
			break;

		case EKomorebiSfx::MissionDone:
		case EKomorebiSfx::GateOpen:
		{
			static constexpr float kChord[4] = { 523.25f, 659.25f, 783.99f, 1046.5f };
			for (int32 i = 0; i < 4; ++i)
			{
				SpawnTone(false, false, EWaveType::Triangle, kChord[i], kChord[i], 0.24f, 0.18f, 0.006f, i * 0.075f);
			}
			break;
		}

		case EKomorebiSfx::EchoCollect:
		{
			static constexpr float kArp[5] = { 587.33f, 880.0f, 1174.66f, 1318.51f, 1760.0f };
			for (int32 i = 0; i < 5; ++i)
			{
				const float Delay = i * 0.065f;
				SpawnTone(false, false, EWaveType::Sine, kArp[i], kArp[i], 0.42f - i * 0.03f, 0.16f, 0.01f, Delay);
				SpawnTone(false, false, EWaveType::Triangle, kArp[i] * 1.5f, kArp[i] * 1.5f, 0.28f, 0.055f, 0.012f, Delay + 0.015f, EFilterMode::None, 0.0f, 0.0f, 1.0f, false, false, 5.0f);
			}
			SpawnNoise(false, false, 0.24f, 0.06f, EFilterMode::HighPass, 4800.0f, 7600.0f, 1.4f, 0.04f);
			break;
		}

		case EKomorebiSfx::UiClick:
		default:
			SpawnTone(false, false, EWaveType::Sine, 880.0f, 660.0f, 0.045f, 0.10f);
			break;
	}
}

void UKomorebiSynthComponent::DispatchBarkCommand(const FBarkCommand& Cmd)
{
	float FStart = 185.0f;
	float FEnd = 210.0f;
	float Dur = 0.09f;
	char Vowel = 'e';

	switch (Cmd.Bark)
	{
		case EKomorebiBarkKind::Swing: FStart = 185.0f; FEnd = 210.0f; Dur = 0.09f; Vowel = 'e'; break;
		case EKomorebiBarkKind::Heavy: FStart = 130.0f; FEnd = 95.0f;  Dur = 0.15f; Vowel = 'o'; break;
		case EKomorebiBarkKind::Hurt:  FStart = 220.0f; FEnd = 145.0f; Dur = 0.14f; Vowel = 'a'; break;
		case EKomorebiBarkKind::Parry: FStart = 310.0f; FEnd = 420.0f; Dur = 0.12f; Vowel = 'i'; break;
		case EKomorebiBarkKind::Clear: FStart = 240.0f; FEnd = 320.0f; Dur = 0.14f; Vowel = 'a'; break;
	}

	const std::array<double, 3> F = KomorebiCore::VowelFormants(Vowel);
	SpawnTone(
		false,
		true,
		EWaveType::Sawtooth,
		FStart,
		FEnd,
		Dur,
		0.085f,
		0.015f,
		0.0f,
		EFilterMode::BandPass,
		static_cast<float>(F[1]) * 0.85f,
		static_cast<float>(F[1]) * 0.85f,
		3.2f);
}

// --- Adaptive Music Sequencer ------------------------------------------------------------------

void UKomorebiSynthComponent::StepSequencer(int32 Step, bool bSnowbound, int32 Chapter, bool bBoss)
{
	if (bSnowbound)
	{
		ScheduleAuroraStep(Step);
	}
	else
	{
		ScheduleAutumnStep(Step, Chapter, bBoss);
	}
}

void UKomorebiSynthComponent::ScheduleAutumnStep(int32 Step, int32 Chapter, bool bBoss)
{
	const auto& Scales = KomorebiCore::GetAutumnChapterScales();
	const KomorebiCore::ChapterMusicScale& Pal = Scales[FMath::Clamp(Chapter, 0, static_cast<int32>(Scales.size()) - 1)];

	const int32 Step16 = Step % 16;
	const int32 BarStep = Step % 32;
	const int32 BassIdx = (Step % 64) / 8;
	const int32 ChordNote = Pal.Bass[BassIdx % 8];

	// 1. Detuned sawtooth pad chord every 2 bars.
	if (BarStep == 0)
	{
		const int32 Notes[3] = { ChordNote + 12, ChordNote + 15, ChordNote + 19 };
		const float Pans[3] = { -0.25f, 0.0f, 0.25f };
		for (int32 i = 0; i < 3; ++i)
		{
			const float Hz = static_cast<float>(KomorebiCore::MidiToHz(Notes[i]));
			SpawnTone(
				true,
				false,
				EWaveType::Sawtooth,
				Hz,
				Hz,
				4.0f,
				0.036f,
				0.45f,
				0.0f,
				EFilterMode::LowPass,
				480.0f + CombatBlend * 420.0f,
				950.0f + CombatBlend * 650.0f,
				0.9f,
				false,
				true,
				7.0f,
				true,
				Pans[i]);
		}
	}

	// 2. Four-on-the-floor kick on beats 0, 4, 8, 12.
	if (Step16 % 4 == 0)
	{
		const float KickGain = 0.22f + CombatBlend * 0.10f + DriveBlend * 0.08f;
		SpawnTone(true, false, EWaveType::Sine, 130.0f, 38.0f, 0.18f, KickGain, 0.003f);
	}

	// 3. Rolling 8th/16th resonant sub bass.
	if (Step16 % 2 == 0 || (DriveBlend > 0.35f && Step16 % 2 == 1))
	{
		const int32 Oct = (Step16 % 4 == 2) ? 12 : 0;
		const float BGain = (Step16 % 2 == 0 ? 0.11f : 0.06f * DriveBlend) + CombatBlend * 0.04f;
		const float BassHz = static_cast<float>(KomorebiCore::MidiToHz(ChordNote + Oct));
		const float Cutoff = 340.0f + CombatBlend * 420.0f + DriveBlend * 320.0f;
		SpawnTone(true, false, EWaveType::Sawtooth, BassHz, BassHz, 0.12f, BGain, 0.005f, 0.0f, EFilterMode::LowPass, Cutoff, Cutoff * 0.85f, 3.2f);
	}

	// 4. D-Dorian triangle arpeggiator routed through stereo delay.
	if (Step16 % 2 == 0 || DriveBlend > 0.25f)
	{
		const int32 ArpNote = Pal.Arp[Step16 % 8] + ((DriveBlend > 0.5f && Step16 % 4 == 3) ? 12 : 0);
		const float AGain = 0.038f + DriveBlend * 0.035f;
		const float ArpHz = static_cast<float>(KomorebiCore::MidiToHz(ArpNote));
		const float Pan = ((Step16 % 4) - 1.5f) * 0.18f;
		SpawnTone(
			true,
			false,
			EWaveType::Triangle,
			ArpHz,
			ArpHz,
			0.16f,
			AGain,
			0.006f,
			0.0f,
			EFilterMode::LowPass,
			1600.0f + DriveBlend * 1400.0f,
			1600.0f + DriveBlend * 1400.0f,
			0.9f,
			true,
			false,
			0.0f,
			false,
			Pan);
	}

	// 5. Combat & driving percussion (snare on 4/12, hi-hats on odd 16ths).
	if (CombatBlend > 0.08f || DriveBlend > 0.25f)
	{
		if (Step16 == 4 || Step16 == 12)
		{
			const float SLevel = FMath::Max(CombatBlend, DriveBlend * 0.7f);
			SpawnNoise(true, false, 0.15f, 0.12f * SLevel, EFilterMode::BandPass, 1800.0f, 1800.0f, 0.8f);
			SpawnTone(true, false, EWaveType::Triangle, 210.0f, 110.0f, 0.08f, 0.09f * SLevel);
		}
		if (Step16 % 2 == 1)
		{
			const float HLevel = FMath::Max(CombatBlend, DriveBlend * 0.8f);
			SpawnNoise(true, false, 0.045f, 0.05f * HLevel, EFilterMode::HighPass, 6200.0f, 6200.0f, 0.9f, 0.0f, (Step16 % 4 == 1) ? -0.2f : 0.2f);
		}
	}

	// 6. Combat & boss sawtooth lead line.
	if (CombatBlend > 0.2f && Step16 % 2 == 0)
	{
		const int32 LeadNote = Pal.Lead[(Step16 / 2) % 8];
		if (LeadNote > 0)
		{
			const float LGain = (bBoss ? 0.075f : 0.05f) * CombatBlend;
			const float LeadHz = static_cast<float>(KomorebiCore::MidiToHz(LeadNote));
			SpawnTone(
				true,
				false,
				EWaveType::Sawtooth,
				LeadHz,
				LeadHz,
				0.22f,
				LGain,
				0.012f,
				0.0f,
				EFilterMode::LowPass,
				2100.0f,
				1900.0f,
				1.5f,
				true,
				true,
				0.0f,
				false,
				0.12f);
		}
	}
}

void UKomorebiSynthComponent::ScheduleAuroraStep(int32 Step)
{
	const int32 Step16 = Step % 16;

	// E-minor warm arctic pad every 2 bars.
	if (Step % 32 == 0)
	{
		static constexpr int32 kPadNotes[4] = { 40, 47, 52, 55 };
		static constexpr float kPans[4] = { -0.35f, -0.12f, 0.12f, 0.35f };
		for (int32 i = 0; i < 4; ++i)
		{
			const float Hz = static_cast<float>(KomorebiCore::MidiToHz(kPadNotes[i]));
			SpawnTone(
				true,
				false,
				EWaveType::Sine,
				Hz,
				Hz,
				7.0f,
				0.045f,
				1.2f,
				0.0f,
				EFilterMode::LowPass,
				620.0f + CombatBlend * 380.0f,
				620.0f + CombatBlend * 380.0f,
				0.707f,
				false,
				true,
				0.0f,
				false,
				kPans[i]);
		}
	}

	// Sparse FM arctic bell chimes in E minor.
	if (Step16 % 4 == 0 && (((Step * 7) % 5) < 3 || CombatBlend > 0.2f))
	{
		const auto& Bells = KomorebiCore::AuroraBells();
		const int32 Note = Bells[(Step >> 2) % static_cast<int32>(Bells.size())] + (CombatBlend > 0.4f ? 12 : 0);
		const float Hz = static_cast<float>(KomorebiCore::MidiToHz(Note));
		FVoice* V = AllocateVoice(true);
		if (V)
		{
			*V = FVoice{};
			V->bActive = true;
			V->bMusicBus = true;
			V->bSendDelay = true;
			V->bSendReverb = true;
			V->Wave = EWaveType::FmBell;
			V->StartFreq = Hz;
			V->EndFreq = Hz;
			V->Duration = 1.6f;
			V->Attack = 0.02f;
			V->Gain = 0.045f + CombatBlend * 0.03f;
			V->FmRatio = 2.0f;
			V->FmIndexStart = 0.45f;
			V->FmIndexEnd = 0.005f;
			V->Pan = (((Step >> 2) % 2) == 0) ? -0.32f : 0.32f;
		}
	}
}

// --- Per-Sample Renderers ----------------------------------------------------------------------

void UKomorebiSynthComponent::RenderVoicePool(
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
	float& OutVoice)
{
	for (FVoice& V : Pool)
	{
		if (!V.bActive)
		{
			continue;
		}

		if (V.DelaySeconds > 0.0f)
		{
			V.DelaySeconds -= InvSr;
			continue;
		}

		if (V.Elapsed >= V.Duration)
		{
			V.bActive = false;
			continue;
		}

		const float U = V.Elapsed / V.Duration;
		float Env = 1.0f;
		if (V.Elapsed < V.Attack)
		{
			Env = V.Elapsed / V.Attack;
		}
		else
		{
			const float DecayFrac = (V.Elapsed - V.Attack) / FMath::Max(0.001f, V.Duration - V.Attack);
			// Exponential decay to -80 dB (0.0001) matching Web Audio exponentialRampToValueAtTime.
			Env = std::exp(-9.21034f * DecayFrac);
		}

		float Sample = 0.0f;
		if (V.Wave == EWaveType::Noise)
		{
			Sample = NextWhiteNoise();
		}
		else if (V.Wave == EWaveType::FmBell)
		{
			const float CarrierHz = V.StartFreq;
			const float ModHz = CarrierHz * V.FmRatio;
			const float ModDecay = std::exp(-4.6f * FMath::Min(1.0f, V.Elapsed / 0.8f));
			const float ModIndex = V.FmIndexStart * ModDecay + V.FmIndexEnd;
			const float ModVal = std::sin(V.ModPhase * kTwoPi);
			const float InstHz = FMath::Max(20.0f, CarrierHz + ModVal * CarrierHz * ModIndex);

			Sample = std::sin(V.Phase * kTwoPi);
			V.Phase += InstHz * InvSr;
			V.ModPhase += ModHz * InvSr;
			if (V.Phase >= 1.0f) V.Phase -= std::floor(V.Phase);
			if (V.ModPhase >= 1.0f) V.ModPhase -= std::floor(V.ModPhase);
		}
		else
		{
			const float Freq = (V.StartFreq == V.EndFreq)
				? V.StartFreq
				: LerpExp(V.StartFreq, V.EndFreq, U);

			if (V.bDualDetune)
			{
				const float Ratio = std::pow(2.0f, V.DetuneCents / 1200.0f);
				const float Inc1 = (Freq / Ratio) * InvSr;
				const float Inc2 = (Freq * Ratio) * InvSr;
				Sample = 0.5f * (RenderOscillator(V.Wave, V.Phase, Inc1) + RenderOscillator(V.Wave, V.Phase2, Inc2));
				V.Phase += Inc1;
				V.Phase2 += Inc2;
				if (V.Phase >= 1.0f) V.Phase -= 1.0f;
				if (V.Phase2 >= 1.0f) V.Phase2 -= 1.0f;
			}
			else
			{
				const float DetunedFreq = (V.DetuneCents != 0.0f)
					? Freq * std::pow(2.0f, V.DetuneCents / 1200.0f)
					: Freq;
				const float Inc = DetunedFreq * InvSr;
				Sample = RenderOscillator(V.Wave, V.Phase, Inc);
				V.Phase += Inc;
				if (V.Phase >= 1.0f) V.Phase -= 1.0f;
			}
		}

		if (V.FilterMode != EFilterMode::None)
		{
			if (V.StartFilterFreq != V.EndFilterFreq && (--V.FilterUpdateCountdown <= 0))
			{
				V.FilterUpdateCountdown = 32;
				const float CurFilterHz = LerpExp(V.StartFilterFreq, V.EndFilterFreq, U);
				V.Filter.Configure(V.FilterMode, CurFilterHz, V.FilterQ, SampleRateHz);
			}
			Sample = V.Filter.Process(Sample);
		}

		const float Out = Sample * Env * V.Gain;
		const float PanL = std::cos((V.Pan + 1.0f) * 0.25f * kPiF);
		const float PanR = std::sin((V.Pan + 1.0f) * 0.25f * kPiF);

		if (V.bVoiceBus)
		{
			OutVoice += Out;
		}
		else if (V.bMusicBus)
		{
			const float L = Out * PanL;
			const float R = Out * PanR;
			OutMusicL += L;
			OutMusicR += R;
			if (V.bSendDelay)
			{
				OutDelayInL += L;
				OutDelayInR += R;
			}
			if (V.bSendReverb)
			{
				OutReverbInL += L;
				OutReverbInR += R;
			}
		}
		else
		{
			OutSfxL += Out * PanL;
			OutSfxR += Out * PanR;
		}

		V.Elapsed += InvSr;
	}
}

float UKomorebiSynthComponent::RenderFormantVoice(float InvSr)
{
	if (!bFormantActive)
	{
		FormantEnvelopeSmooth *= 0.996f;
		return 0.0f;
	}

	if (FormantLeadDelay > 0.0f)
	{
		FormantLeadDelay -= InvSr;
		return 0.0f;
	}

	if (FormantSyllableIdx >= FormantSyllableCount)
	{
		bFormantActive = false;
		if (bFormantTailSquelch)
		{
			SpawnRadioSquelch(false, 0.0f);
		}
		return 0.0f;
	}

	const FVoiceSyllableSlot& Syl = FormantSyllables[FormantSyllableIdx];
	const float TotalSlotDur = Syl.Duration + Syl.Pause;

	// Configure the 3 parallel resonant vowel formant biquads at syllable onset.
	if (FormantSyllableTime == 0.0f)
	{
		for (int32 fi = 0; fi < 3; ++fi)
		{
			const float TargetHz = Syl.Formants[fi] * FormantScale;
			const float Q = (fi == 0) ? 5.5f : 8.0f;
			FormantBp[fi].Configure(EFilterMode::BandPass, TargetHz, Q, SampleRateHz);
		}
	}

	float VoiceOut = 0.0f;
	if (FormantSyllableTime < Syl.Duration)
	{
		const float U = FormantSyllableTime / FMath::Max(0.01f, Syl.Duration);
		const float Denom = static_cast<float>(FMath::Max(1, FormantSyllableCount - 1));
		const float ProgressAll = static_cast<float>(FormantSyllableIdx) / Denom;
		const float Contour = 1.0f + 0.08f * std::sin(ProgressAll * kPiF) - 0.08f * ProgressAll;
		const float StartF0 = FormantF0 * Syl.Stress * Contour;
		const float EndF0 = FormantF0 * Contour * ((FormantSyllableIdx == FormantSyllableCount - 1) ? 0.86f : 1.02f);
		const float CurF0 = FMath::Lerp(StartF0, EndF0, U);

		// Glottal sawtooth source through 3 parallel resonant vowel formants.
		const float PhaseInc = CurF0 * InvSr;
		const float Glottal = RenderOscillator(EWaveType::Sawtooth, FormantPhase, PhaseInc);
		FormantPhase += PhaseInc;
		if (FormantPhase >= 1.0f)
		{
			FormantPhase -= 1.0f;
		}

		const float F1 = FormantBp[0].Process(Glottal) * 0.70f;
		const float F2 = FormantBp[1].Process(Glottal) * 0.50f;
		const float F3 = FormantBp[2].Process(Glottal) * 0.25f;
		const float Banded = FormantRadioBand.Process(F1 + F2 + F3);

		// Syllable amplitude envelope: 15ms attack, sustain to 72%, exponential release.
		float Env = 0.0f;
		if (FormantSyllableTime < 0.015f)
		{
			Env = (FormantSyllableTime / 0.015f) * 0.22f;
		}
		else if (U < 0.72f)
		{
			Env = FMath::Lerp(0.22f, 0.19f, (U - 0.15f) / 0.57f);
		}
		else
		{
			const float Rel = (U - 0.72f) / 0.28f;
			Env = 0.19f * std::exp(-6.5f * Rel);
		}

		VoiceOut = std::tanh(Banded * 1.45f) * Env;

		// High-passed unvoiced consonant burst at syllable start (35ms).
		if (Syl.bUnvoiced && FormantSyllableTime < 0.035f)
		{
			const float ConsEnv = 0.06f * std::exp(-6.0f * (FormantSyllableTime / 0.035f));
			VoiceOut += FormantConsonantHp.Process(NextWhiteNoise()) * ConsEnv;
		}
	}

	FormantSyllableTime += InvSr;
	if (FormantSyllableTime >= TotalSlotDur)
	{
		FormantSyllableTime = 0.0f;
		++FormantSyllableIdx;
	}

	const float AbsLvl = FMath::Abs(VoiceOut) * 6.5f;
	FormantEnvelopeSmooth += (ClampF(AbsLvl, 0.0f, 1.0f) - FormantEnvelopeSmooth) * (60.0f * InvSr);
	return VoiceOut;
}

float UKomorebiSynthComponent::RenderRideEngineAndSkid(float InvSr)
{
	const bool bMuted = bTargetMuted.load(std::memory_order_relaxed);
	const bool bActive = bRideDriving.load(std::memory_order_relaxed)
		&& !bRidePaused.load(std::memory_order_relaxed)
		&& !bMuted;

	float WantFreqA = 54.0f;
	float WantFreqB = 108.0f;
	float WantCutoff = 320.0f;
	float WantEngineGain = 0.0f;
	float WantSkidGain = 0.0f;

	if (bActive)
	{
		const EKomorebiVehicleKind Kind = static_cast<EKomorebiVehicleKind>(RideKind.load(std::memory_order_relaxed));
		const float Speed = RideSpeedMps.load(std::memory_order_relaxed);
		const float Top = FMath::Max(1.0f, RideTopSpeedMps.load(std::memory_order_relaxed));
		const float AbsThrottle = FMath::Abs(RideThrottle.load(std::memory_order_relaxed));
		const bool bSliding = bRideSliding.load(std::memory_order_relaxed);
		const bool bAir = bRideAirborne.load(std::memory_order_relaxed);

		const float Norm = ClampF(Speed / Top, 0.0f, 1.2f);
		const float BaseHz = (Kind == EKomorebiVehicleKind::Bike) ? 68.0f
			: (Kind == EKomorebiVehicleKind::Bicycle) ? 42.0f
			: (Kind == EKomorebiVehicleKind::Pickup) ? 46.0f
			: 54.0f;
		const float SpanHz = (Kind == EKomorebiVehicleKind::Bike) ? 165.0f
			: (Kind == EKomorebiVehicleKind::Bicycle) ? 65.0f
			: 120.0f;

		WantFreqA = BaseHz + Norm * SpanHz + (bAir ? 22.0f : 0.0f) + AbsThrottle * 14.0f;
		WantFreqB = WantFreqA * ((Kind == EKomorebiVehicleKind::Bike) ? 1.5f : 2.0f);
		WantCutoff = 240.0f + Norm * 1100.0f + AbsThrottle * 260.0f;
		WantEngineGain = (Kind == EKomorebiVehicleKind::Bicycle)
			? (0.03f + Norm * 0.04f)
			: (0.055f + Norm * 0.09f + AbsThrottle * 0.025f);
		WantSkidGain = (bSliding && !bAir) ? 0.08f : 0.0f;
	}

	const float SmoothCoeff = 20.0f * InvSr;
	SmoothedEngineFreq += (WantFreqA - SmoothedEngineFreq) * SmoothCoeff;
	SmoothedEngineFreqB += (WantFreqB - SmoothedEngineFreqB) * SmoothCoeff;
	SmoothedEngineCutoff += (WantCutoff - SmoothedEngineCutoff) * SmoothCoeff;
	SmoothedEngineGain += (WantEngineGain - SmoothedEngineGain) * SmoothCoeff;
	SmoothedSkidGain += (WantSkidGain - SmoothedSkidGain) * (25.0f * InvSr);

	if (SmoothedEngineGain < 1.0e-5f && SmoothedSkidGain < 1.0e-5f)
	{
		return 0.0f;
	}

	if (--EngineFilterCountdown <= 0)
	{
		EngineFilterCountdown = 64;
		EngineLowpass.Configure(EFilterMode::LowPass, SmoothedEngineCutoff, 1.0f, SampleRateHz);
	}

	const float IncA = SmoothedEngineFreq * InvSr;
	const float IncB = SmoothedEngineFreqB * InvSr;
	const float IncSub = (SmoothedEngineFreq * 0.5f) * InvSr;

	const float Saw = RenderOscillator(EWaveType::Sawtooth, EnginePhaseA, IncA);
	const float Tri = RenderOscillator(EWaveType::Triangle, EnginePhaseB, IncB);
	const float Sub = std::sin(EnginePhaseSub * kTwoPi);

	EnginePhaseA += IncA; if (EnginePhaseA >= 1.0f) EnginePhaseA -= 1.0f;
	EnginePhaseB += IncB; if (EnginePhaseB >= 1.0f) EnginePhaseB -= 1.0f;
	EnginePhaseSub += IncSub; if (EnginePhaseSub >= 1.0f) EnginePhaseSub -= 1.0f;

	const float EngineSignal = EngineLowpass.Process((Saw * 0.55f + Tri * 0.35f + Sub * 0.25f)) * SmoothedEngineGain;
	const float SkidSignal = (SmoothedSkidGain > 1.0e-5f)
		? SkidBandpass.Process(NextWhiteNoise()) * SmoothedSkidGain
		: 0.0f;

	return EngineSignal + SkidSignal;
}

float UKomorebiSynthComponent::RenderAuroraWind(float InvSr, bool bSnowbound)
{
	const float WantGain = bSnowbound ? 0.028f : 0.0f;
	SmoothedWindGain += (WantGain - SmoothedWindGain) * (2.5f * InvSr);
	if (SmoothedWindGain < 1.0e-5f)
	{
		return 0.0f;
	}

	WindLfoPhase += 0.14f * InvSr;
	if (WindLfoPhase >= 1.0f)
	{
		WindLfoPhase -= 1.0f;
	}

	if (--WindFilterCountdown <= 0)
	{
		WindFilterCountdown = 128;
		const float Swell = 0.5f + 0.5f * std::sin(WindLfoPhase * kTwoPi);
		WindBandpass.Configure(EFilterMode::BandPass, 260.0f + Swell * 280.0f, 1.8f, SampleRateHz);
	}

	return WindBandpass.Process(NextWhiteNoise()) * SmoothedWindGain;
}

// --- Main AudioMixer Render Callback -----------------------------------------------------------

int32 UKomorebiSynthComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	const int32 Channels = FMath::Max(1, NumChannels);
	const int32 NumFrames = NumSamples / Channels;
	if (NumFrames <= 0)
	{
		return 0;
	}

	// 1. Drain lock-free command queues from game thread.
	FSfxCommand SfxCmd;
	while (SfxQueue.Dequeue(SfxCmd))
	{
		DispatchSfxCommand(SfxCmd);
	}

	FBarkCommand BarkCmd;
	while (BarkQueue.Dequeue(BarkCmd))
	{
		DispatchBarkCommand(BarkCmd);
	}

	FVoiceUtteranceCommand VoiceCmd;
	while (VoiceQueue.Dequeue(VoiceCmd))
	{
		if (VoiceCmd.bStopOnly)
		{
			bFormantActive = false;
			bFormantTailSquelch = false;
			FormantSyllableCount = 0;
			FormantSyllableIdx = 0;
			FormantSyllableTime = 0.0f;
		}
		else if (VoiceCmd.bInterrupt || !bFormantActive)
		{
			bFormantActive = true;
			bFormantTailSquelch = VoiceCmd.bSquelch;
			FormantLeadDelay = VoiceCmd.bSquelch ? 0.07f : 0.0f;
			FormantF0 = VoiceCmd.F0;
			FormantScale = VoiceCmd.FormantScale;
			FormantSyllableCount = FMath::Clamp(VoiceCmd.SyllableCount, 0, MaxUtteranceSyllables);
			FormantSyllableIdx = 0;
			FormantSyllableTime = 0.0f;
			FormantPhase = 0.0f;
			for (int32 i = 0; i < FormantSyllableCount; ++i)
			{
				FormantSyllables[i] = VoiceCmd.Syllables[i];
			}
			if (VoiceCmd.bSquelch)
			{
				SpawnRadioSquelch(true, 0.0f);
			}
		}
	}

	// 2. Snapshot atomic parameters for this block.
	const bool bMuted = bTargetMuted.load(std::memory_order_relaxed);
	const float WantMaster = bMuted ? 0.0f : TargetMaster.load(std::memory_order_relaxed);
	const float RawMusic = TargetMusic.load(std::memory_order_relaxed);
	const float WantVoice = TargetVoice.load(std::memory_order_relaxed);
	const float WantSfx = TargetSfx.load(std::memory_order_relaxed);

	const int32 Chapter = TargetChapter.load(std::memory_order_relaxed);
	const bool bBoss = bTargetBoss.load(std::memory_order_relaxed);
	const bool bSnowbound = bTargetSnowbound.load(std::memory_order_relaxed);
	const bool bPaused = bTargetPaused.load(std::memory_order_relaxed);
	const float GoalCombat = bBoss ? 1.0f : TargetCombat.load(std::memory_order_relaxed);
	const float GoalDrive = (bTargetDriving.load(std::memory_order_relaxed) && TargetSpeedMps.load(std::memory_order_relaxed) > 4.0f) ? 1.0f : 0.0f;

	const float InvSr = 1.0f / SampleRateHz;
	const float BlockDt = static_cast<float>(NumFrames) * InvSr;
	const float BlendK = 1.0f - std::exp(-BlockDt * 2.4f);
	CombatBlend += (GoalCombat - CombatBlend) * BlendK;
	DriveBlend += (GoalDrive - DriveBlend) * BlendK;

	const double StepSec = bSnowbound ? (60.0 / 68.0 / 4.0) : (60.0 / 116.0 / 4.0);
	const double SamplesPerStep = StepSec * static_cast<double>(SampleRateHz);
	const bool bMusicAudible = !bMuted && WantMaster > 0.001f && RawMusic > 0.001f;

	const int32 DelaySamplesL = FMath::Clamp(static_cast<int32>((bSnowbound ? 0.44f : 0.258f) * SampleRateHz), 128, DelayBufLen - 2);
	const int32 DelaySamplesR = FMath::Clamp(static_cast<int32>(DelaySamplesL * 1.04f), 128, DelayBufLen - 2);
	const float DelayFb = bSnowbound ? 0.38f : 0.26f;
	const float ReverbMix = bSnowbound ? 0.45f : 0.14f;

	const float CompAttackCoeff = 1.0f - std::exp(-1.0f / (0.005f * SampleRateHz));
	const float CompReleaseCoeff = 1.0f - std::exp(-1.0f / (0.160f * SampleRateHz));
	constexpr float CompThreshold = 0.3162f; // -10 dB

	// 3. Render stereo frames sample-by-sample.
	for (int32 Frame = 0; Frame < NumFrames; ++Frame)
	{
		// Step adaptive music sequencer at exact sample accuracy.
		if (bMusicAudible)
		{
			StepSampleAccum += 1.0;
			if (StepSampleAccum >= SamplesPerStep)
			{
				StepSampleAccum -= SamplesPerStep;
				StepSequencer(SequencerStep, bSnowbound, Chapter, bBoss);
				++SequencerStep;
			}
		}

		// Smoothly update bus gains and music ducking during radio comms.
		const float Duck = (bFormantActive || FormantEnvelopeSmooth > 0.02f) ? 0.42f : 1.0f;
		const float PauseMul = bPaused ? 0.55f : 1.0f;
		const float WantMusicGain = RawMusic * Duck * PauseMul;
		const float WantCutoff = bPaused ? 950.0f : 9000.0f;

		SmoothedMaster += (WantMaster - SmoothedMaster) * (35.0f * InvSr);
		SmoothedMusicGain += (WantMusicGain - SmoothedMusicGain) * (14.0f * InvSr);
		SmoothedVoiceGain += (WantVoice - SmoothedVoiceGain) * (45.0f * InvSr);
		SmoothedSfxGain += (WantSfx - SmoothedSfxGain) * (45.0f * InvSr);
		SmoothedMusicCutoff += (WantCutoff - SmoothedMusicCutoff) * (10.0f * InvSr);

		if (--MusicFilterCountdown <= 0)
		{
			MusicFilterCountdown = 64;
			MusicMasterLpL.Configure(EFilterMode::LowPass, SmoothedMusicCutoff, 0.707f, SampleRateHz);
			MusicMasterLpR.Configure(EFilterMode::LowPass, SmoothedMusicCutoff, 0.707f, SampleRateHz);
		}

		float MusicL = 0.0f;
		float MusicR = 0.0f;
		float DelayInL = 0.0f;
		float DelayInR = 0.0f;
		float ReverbInL = 0.0f;
		float ReverbInR = 0.0f;
		float SfxL = 0.0f;
		float SfxR = 0.0f;
		float VoiceSample = 0.0f;

		RenderVoicePool(MusicVoices, InvSr, MusicL, MusicR, DelayInL, DelayInR, ReverbInL, ReverbInR, SfxL, SfxR, VoiceSample);
		RenderVoicePool(SfxVoices, InvSr, MusicL, MusicR, DelayInL, DelayInR, ReverbInL, ReverbInR, SfxL, SfxR, VoiceSample);

		// Arctic wind pad in Snowbound mode.
		const float Wind = RenderAuroraWind(InvSr, bSnowbound);
		MusicL += Wind;
		MusicR += Wind;
		ReverbInL += Wind * 0.5f;
		ReverbInR += Wind * 0.5f;

		// Stereo damped feedback delay.
		int32 ReadIdxL = DelayWriteIdx - DelaySamplesL;
		if (ReadIdxL < 0) ReadIdxL += DelayBufLen;
		int32 ReadIdxR = DelayWriteIdx - DelaySamplesR;
		if (ReadIdxR < 0) ReadIdxR += DelayBufLen;

		const float TapL = DelayDampL.Process(DelayBufL[ReadIdxL]);
		const float TapR = DelayDampR.Process(DelayBufR[ReadIdxR]);
		DelayBufL[DelayWriteIdx] = FlushDenormal(DelayInL + TapR * DelayFb);
		DelayBufR[DelayWriteIdx] = FlushDenormal(DelayInR + TapL * DelayFb);
		if (++DelayWriteIdx >= DelayBufLen)
		{
			DelayWriteIdx = 0;
		}
		MusicL += TapL * 0.65f;
		MusicR += TapR * 0.65f;

		// Stereo Schroeder/Freeverb reverb.
		const float RevMonoL = ReverbInL * 0.25f;
		const float RevMonoR = ReverbInR * 0.25f;
		float RevSumL = 0.0f;
		float RevSumR = 0.0f;
		for (int32 c = 0; c < 4; ++c)
		{
			RevSumL += ReverbCombsL[c].Process(RevMonoL);
			RevSumR += ReverbCombsR[c].Process(RevMonoR);
		}
		for (int32 a = 0; a < 2; ++a)
		{
			RevSumL = ReverbAllpassL[a].Process(RevSumL);
			RevSumR = ReverbAllpassR[a].Process(RevSumR);
		}
		MusicL += RevSumL * ReverbMix;
		MusicR += RevSumR * ReverbMix;

		// Master music lowpass & bus gain.
		MusicL = MusicMasterLpL.Process(MusicL) * SmoothedMusicGain;
		MusicR = MusicMasterLpR.Process(MusicR) * SmoothedMusicGain;

		// Continuous vehicle engine + skid synth routed to SFX bus.
		const float RideMono = RenderRideEngineAndSkid(InvSr);
		SfxL = (SfxL + RideMono) * SmoothedSfxGain;
		SfxR = (SfxR + RideMono) * SmoothedSfxGain;

		// Formant radio speech synth routed to Voice bus.
		VoiceSample = (VoiceSample + RenderFormantVoice(InvSr)) * SmoothedVoiceGain;

		// Sum master bus.
		float MixL = (MusicL + SfxL + VoiceSample) * SmoothedMaster;
		float MixR = (MusicR + SfxR + VoiceSample) * SmoothedMaster;

		// Master soft-knee dynamics compressor (-10 dB, 4:1) + gentle limiter.
		const float Peak = FMath::Max(FMath::Abs(MixL), FMath::Abs(MixR));
		const float EnvCoeff = (Peak > CompEnv) ? CompAttackCoeff : CompReleaseCoeff;
		CompEnv = FlushDenormal(CompEnv + (Peak - CompEnv) * EnvCoeff);
		if (CompEnv > CompThreshold)
		{
			const float Over = CompEnv / CompThreshold;
			const float GainReduction = std::pow(Over, -0.75f); // 4:1 ratio
			MixL *= GainReduction;
			MixR *= GainReduction;
		}

		MixL = ClampF(std::tanh(MixL), -0.98f, 0.98f);
		MixR = ClampF(std::tanh(MixR), -0.98f, 0.98f);

		if (Channels == 2)
		{
			OutAudio[Frame * 2] = MixL;
			OutAudio[Frame * 2 + 1] = MixR;
		}
		else
		{
			OutAudio[Frame] = 0.5f * (MixL + MixR);
		}
	}

	bAtomicVoiceActive.store(bFormantActive, std::memory_order_relaxed);
	AtomicVoiceLevel.store(ClampF(FormantEnvelopeSmooth, 0.0f, 1.0f), std::memory_order_relaxed);

	return NumSamples;
}
