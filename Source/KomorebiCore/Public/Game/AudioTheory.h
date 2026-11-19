// Copyright Komorebi Project. Audio settings, radio speaker profiles, syllable formant parser, and adaptive music scales.
#pragma once

#include "KomorebiCoreAPI.h"

#include <array>
#include <string_view>
#include <vector>

namespace KomorebiCore
{
	inline constexpr const char* kAudioStorageKey = "oilpaint.audio.v1";

	struct AudioSettings
	{
		double Master = 0.8;
		double Music = 0.55;
		double Voice = 0.85;
		double Sfx = 0.75;
		bool bMuted = false;
	};

	struct SpeakerProfile
	{
		const char* Id = "";
		const char* Name = "";
		double Pitch = 1.0;
		double Rate = 1.0;
		double F0 = 175.0;
		const char* ColorHex = "#ffffff";
		bool bPreferFemale = false;
	};

	struct Syllable
	{
		char Vowel = 'e';
		std::array<double, 3> Formants = { 530.0, 1840.0, 2480.0 };
		double Duration = 0.08;
		double Pause = 0.025;
		double Stress = 1.0;
		bool bUnvoiced = false;
	};

	struct ChapterMusicScale
	{
		int Root = 38;
		std::array<int, 8> Arp{};
		std::array<int, 8> Bass{};
		std::array<int, 8> Lead{};
	};

	/** Default persisted audio mix volumes and mute state. */
	KOMOREBICORE_API AudioSettings DefaultAudioSettings();

	/** Clamps finite volumes to [0..1] and replaces non-finite entries with defaults. */
	KOMOREBICORE_API AudioSettings NormalizeAudioSettings(const AudioSettings& Raw);

	/** All 5 voiced radio speaker profiles (sora, mira, ren, kael, dispatch). */
	KOMOREBICORE_API const std::array<SpeakerProfile, 5>& GetSpeakers();

	/** Looks up a speaker profile by lowercase id, defaulting to sora if unknown. */
	KOMOREBICORE_API const SpeakerProfile& FindSpeaker(std::string_view SpeakerId);

	/** Formant frequency targets [F1, F2, F3] in Hz for vowels a/e/i/o/u. */
	KOMOREBICORE_API std::array<double, 3> VowelFormants(char Vowel);

	/** Breaks speech text into timed syllables with vowel formants and punctuation pauses. */
	KOMOREBICORE_API std::vector<Syllable> SyllablesOf(std::string_view Text);

	/** Converts a MIDI note number to frequency in Hz (A4 = MIDI 69 = 440 Hz). */
	KOMOREBICORE_API double MidiToHz(double MidiNote);

	/** D Dorian synthwave palettes for all 6 Neo-Kyoto city chapters. */
	KOMOREBICORE_API const std::array<ChapterMusicScale, 6>& GetAutumnChapterScales();

	/** E minor arctic bell motif for the Snowbound (Aurora) prologue. */
	KOMOREBICORE_API const std::array<int, 8>& AuroraBells();
}
