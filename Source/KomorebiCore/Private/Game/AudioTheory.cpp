// Copyright Komorebi Project. Audio theory tables and speech syllable parser matching legacy-web/src/game/audio.js.
#include "Game/AudioTheory.h"

#include <string>

namespace KomorebiCore
{
	namespace
	{
		double Clamp01OrDefault(double Value, double Fallback)
		{
			return std::isfinite(Value) ? Clamp(Value, 0.0, 1.0) : Fallback;
		}

		bool IsAsciiWhitespace(char Ch) noexcept
		{
			return Ch == ' ' || Ch == '\t' || Ch == '\n' || Ch == '\r' || Ch == '\f' || Ch == '\v';
		}

		bool IsVowelOrY(char Ch) noexcept
		{
			return Ch == 'a' || Ch == 'e' || Ch == 'i' || Ch == 'o' || Ch == 'u' || Ch == 'y';
		}

		bool IsUnvoicedInitial(char Ch) noexcept
		{
			return Ch == 't' || Ch == 'p' || Ch == 'k' || Ch == 'f' || Ch == 's' || Ch == 'x' || Ch == 'h';
		}

		bool EndsWith(std::string_view Str, std::string_view Suffix) noexcept
		{
			return Str.size() >= Suffix.size() && Str.substr(Str.size() - Suffix.size()) == Suffix;
		}

		double WordPauseAfter(std::string_view Raw) noexcept
		{
			if (Raw.empty())
			{
				return 0.025;
			}
			const char Last = Raw.back();
			if (Last == '.' || Last == '!' || Last == '?')
			{
				return 0.16;
			}
			if (Last == ',' || Last == ';' || Last == ':' || Last == '-'
				|| EndsWith(Raw, "\xE2\x80\x94") // em-dash U+2014
				|| EndsWith(Raw, "\xE2\x80\x93")) // en-dash U+2013
			{
				return 0.08;
			}
			return 0.025;
		}

		// Matches JS regex: /[^aeiouy]*[aeiouy]+(?:[^aeiouy]*(?=$|[^aeiouy]))?/g
		std::vector<std::string_view> SplitSyllableChunks(std::string_view Word)
		{
			std::vector<std::string_view> Chunks;
			const size_t N = Word.size();
			size_t Pos = 0;

			while (Pos < N)
			{
				const size_t Start = Pos;
				// [^aeiouy]*
				while (Pos < N && !IsVowelOrY(Word[Pos]))
				{
					++Pos;
				}
				// [aeiouy]+
				if (Pos >= N)
				{
					break;
				}
				while (Pos < N && IsVowelOrY(Word[Pos]))
				{
					++Pos;
				}
				// (?:[^aeiouy]*(?=$|[^aeiouy]))?
				size_t ConsStart = Pos;
				while (Pos < N && !IsVowelOrY(Word[Pos]))
				{
					++Pos;
				}
				const size_t ConsCount = Pos - ConsStart;
				if (Pos < N && ConsCount >= 1)
				{
					// Followed by a vowel: leave 1 consonant as onset of the next syllable.
					--Pos;
				}
				Chunks.push_back(Word.substr(Start, Pos - Start));
			}

			if (Chunks.empty())
			{
				Chunks.push_back(Word);
			}
			return Chunks;
		}
	}

	AudioSettings DefaultAudioSettings()
	{
		return AudioSettings{ 0.8, 0.55, 0.85, 0.75, false };
	}

	AudioSettings NormalizeAudioSettings(const AudioSettings& Raw)
	{
		const AudioSettings Def = DefaultAudioSettings();
		return AudioSettings{
			Clamp01OrDefault(Raw.Master, Def.Master),
			Clamp01OrDefault(Raw.Music, Def.Music),
			Clamp01OrDefault(Raw.Voice, Def.Voice),
			Clamp01OrDefault(Raw.Sfx, Def.Sfx),
			Raw.bMuted,
		};
	}

	const std::array<SpeakerProfile, 5>& GetSpeakers()
	{
		static const std::array<SpeakerProfile, 5> kSpeakers{{
			{ "sora", "Sora // Handler", 1.16, 1.04, 235.0, "#00f0ff", true },
			{ "mira", "Dr. Mira // Archive", 1.08, 0.96, 210.0, "#19ffc6", true },
			{ "ren", "Ren // Architect", 0.78, 0.93, 122.0, "#ff2a6d", false },
			{ "kael", "Kael // Courier 07", 0.98, 1.02, 155.0, "#fcee0a", false },
			{ "dispatch", "Grid Dispatch", 0.88, 1.08, 175.0, "#ffb000", false },
		}};
		return kSpeakers;
	}

	const SpeakerProfile& FindSpeaker(std::string_view SpeakerId)
	{
		const auto& Speakers = GetSpeakers();
		for (const SpeakerProfile& Profile : Speakers)
		{
			if (SpeakerId == Profile.Id)
			{
				return Profile;
			}
		}
		return Speakers[0];
	}

	std::array<double, 3> VowelFormants(char Vowel)
	{
		switch (Vowel)
		{
			case 'a': return { 730.0, 1090.0, 2440.0 };
			case 'e': return { 530.0, 1840.0, 2480.0 };
			case 'i': return { 290.0, 2290.0, 3010.0 };
			case 'o': return { 570.0, 840.0, 2410.0 };
			case 'u': return { 320.0, 870.0, 2240.0 };
			default:  return { 530.0, 1840.0, 2480.0 };
		}
	}

	std::vector<Syllable> SyllablesOf(std::string_view Text)
	{
		std::vector<Syllable> Out;
		size_t Idx = 0;
		const size_t Len = Text.size();

		while (Idx < Len)
		{
			while (Idx < Len && IsAsciiWhitespace(Text[Idx]))
			{
				++Idx;
			}
			if (Idx >= Len)
			{
				break;
			}
			const size_t WordStart = Idx;
			while (Idx < Len && !IsAsciiWhitespace(Text[Idx]))
			{
				++Idx;
			}
			const std::string_view Raw = Text.substr(WordStart, Idx - WordStart);
			const double Pause = WordPauseAfter(Raw);

			std::string CleanWord;
			CleanWord.reserve(Raw.size());
			for (char C : Raw)
			{
				const unsigned char Uc = static_cast<unsigned char>(C);
				if (Uc >= 'A' && Uc <= 'Z')
				{
					CleanWord.push_back(static_cast<char>(Uc - 'A' + 'a'));
				}
				else if ((Uc >= 'a' && Uc <= 'z') || (Uc >= '0' && Uc <= '9'))
				{
					CleanWord.push_back(static_cast<char>(Uc));
				}
			}
			if (CleanWord.empty())
			{
				continue;
			}

			const std::vector<std::string_view> Chunks = SplitSyllableChunks(CleanWord);
			for (size_t Ci = 0; Ci < Chunks.size(); ++Ci)
			{
				const std::string_view Chunk = Chunks[Ci];
				char VKey = 'e';
				for (char C : Chunk)
				{
					if (IsVowelOrY(C))
					{
						VKey = (C == 'y') ? 'i' : C;
						break;
					}
				}
				const bool bUnvoiced = !Chunk.empty() && IsUnvoicedInitial(Chunk.front());
				const double Duration = Clamp(0.055 + static_cast<double>(Chunk.size()) * 0.015, 0.065, 0.16);
				const double Stress = (Ci == 0) ? 1.08 : 0.96;

				Out.push_back(Syllable{
					VKey,
					VowelFormants(VKey),
					Duration,
					(Ci + 1 == Chunks.size()) ? Pause : 0.01,
					Stress,
					bUnvoiced,
				});
			}
		}

		return Out;
	}

	double MidiToHz(double MidiNote)
	{
		return 440.0 * std::pow(2.0, (MidiNote - 69.0) / 12.0);
	}

	const std::array<ChapterMusicScale, 6>& GetAutumnChapterScales()
	{
		static const std::array<ChapterMusicScale, 6> kScales{{
			{ 38, { 62, 65, 69, 72, 74, 72, 69, 65 }, { 38, 38, 41, 43, 38, 38, 45, 43 }, { 74, 0, 77, 79, 81, 0, 79, 77 } },
			{ 41, { 65, 69, 72, 74, 77, 74, 72, 69 }, { 41, 41, 45, 48, 41, 41, 43, 45 }, { 77, 0, 81, 79, 77, 0, 74, 72 } },
			{ 40, { 64, 67, 71, 74, 76, 74, 71, 67 }, { 40, 40, 43, 45, 40, 40, 47, 45 }, { 76, 0, 79, 83, 81, 0, 79, 76 } },
			{ 43, { 67, 71, 74, 77, 79, 77, 74, 71 }, { 43, 43, 45, 48, 43, 43, 41, 45 }, { 79, 0, 81, 84, 83, 0, 79, 77 } },
			{ 36, { 60, 64, 67, 71, 72, 71, 67, 64 }, { 36, 36, 40, 43, 36, 36, 41, 40 }, { 72, 0, 76, 79, 77, 0, 74, 72 } },
			{ 38, { 62, 66, 69, 74, 76, 74, 69, 66 }, { 38, 38, 45, 43, 38, 38, 47, 45 }, { 74, 78, 81, 0, 86, 81, 78, 74 } },
		}};
		return kScales;
	}

	const std::array<int, 8>& AuroraBells()
	{
		static const std::array<int, 8> kBells{ 64, 67, 71, 74, 76, 79, 71, 67 };
		return kBells;
	}
}
