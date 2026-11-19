// Copyright Komorebi Project. Six district chapters, palettes, scale factor, and progression lookup.
#pragma once

#include "KomorebiCoreAPI.h"

#include <array>
#include <string_view>
#include <vector>

namespace KomorebiCore
{
	/** Trail scale factor applied to the way, spawns, gates, duels, and chapter boundaries. */
	inline constexpr double kWorldScale = 3.0;
	inline constexpr int kChapterCount = 6;
	inline constexpr std::array<double, 5> kChapterBoundaries = {
		52.0 * kWorldScale,
		29.0 * kWorldScale,
		3.0 * kWorldScale,
		-31.0 * kWorldScale,
		-63.0 * kWorldScale,
	};

	struct ChapterDayPalette
	{
		RGB Grass{};
		RGB Path{};
		RGB Sky{};
		RGB Haze{};
	};

	struct ChapterSpec
	{
		std::string_view Name;
		std::string_view Subtitle;
		std::string_view Style; // "field" or "spirit"
		std::string_view ColorHex;
		RGB Grass{};
		RGB Foliage{};
		RGB LeafLight{};
		RGB Sky{};
		RGB Haze{};
		RGB Path{};
		ChapterDayPalette Day{};
	};

	/** Returns the six district chapter art directions in trail order (0..5). */
	KOMOREBICORE_API const std::vector<ChapterSpec>& GetChapters();

	/** Resolves the active chapter index (0..5) for world Z, capped by cleared count unless roaming. */
	KOMOREBICORE_API int ChapterAt(double Z, int Cleared = kChapterCount, bool Roam = false);
}
