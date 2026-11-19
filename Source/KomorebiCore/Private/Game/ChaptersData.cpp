// Copyright Komorebi Project. Six district chapters, palettes, scale factor, and progression lookup.
#include "Game/ChaptersData.h"

namespace KomorebiCore
{
	const std::vector<ChapterSpec>& GetChapters()
	{
		static const std::vector<ChapterSpec> Chapters = {
			{
				"Sunleaf Row",
				"A small step into a wider grid",
				"field",
				"#00f0ff",
				{0.10, 0.13, 0.17},
				{0.0, 0.85, 0.95},
				{1.0, 0.86, 0.45},
				{0.03, 0.05, 0.12},
				{0.10, 0.22, 0.32},
				{0.20, 0.22, 0.27},
				{
					{0.60, 0.62, 0.65},
					{0.34, 0.36, 0.40},
					{0.18, 0.52, 0.88},
					{0.70, 0.81, 0.91},
				},
			},
			{
				"Petal Arcade",
				"Follow the falling signal",
				"field",
				"#ff4fa3",
				{0.13, 0.10, 0.16},
				{1.0, 0.25, 0.6},
				{1.0, 0.74, 0.86},
				{0.07, 0.03, 0.12},
				{0.30, 0.12, 0.28},
				{0.22, 0.20, 0.26},
				{
					{0.64, 0.60, 0.63},
					{0.36, 0.35, 0.40},
					{0.24, 0.50, 0.86},
					{0.76, 0.79, 0.89},
				},
			},
			{
				"Jade Heights",
				"The city learns your name",
				"spirit",
				"#19ffc6",
				{0.08, 0.14, 0.14},
				{0.1, 1.0, 0.75},
				{0.68, 1.0, 0.6},
				{0.02, 0.08, 0.10},
				{0.08, 0.26, 0.24},
				{0.18, 0.23, 0.24},
				{
					{0.56, 0.62, 0.61},
					{0.32, 0.36, 0.38},
					{0.16, 0.55, 0.84},
					{0.68, 0.82, 0.88},
				},
			},
			{
				"Moonveil Stacks",
				"Keep a little light with you",
				"spirit",
				"#6fb7ff",
				{0.08, 0.10, 0.17},
				{0.35, 0.6, 1.0},
				{0.78, 0.87, 1.0},
				{0.02, 0.03, 0.10},
				{0.10, 0.16, 0.34},
				{0.17, 0.19, 0.27},
				{
					{0.58, 0.60, 0.66},
					{0.33, 0.35, 0.41},
					{0.20, 0.52, 0.90},
					{0.72, 0.80, 0.92},
				},
			},
			{
				"Ashen Hollow",
				"Embers remember every blade",
				"spirit",
				"#ff5a4d",
				{0.14, 0.10, 0.11},
				{1.0, 0.3, 0.25},
				{1.0, 0.62, 0.36},
				{0.08, 0.03, 0.05},
				{0.30, 0.12, 0.12},
				{0.23, 0.20, 0.20},
				{
					{0.62, 0.58, 0.57},
					{0.37, 0.35, 0.37},
					{0.26, 0.48, 0.82},
					{0.78, 0.78, 0.85},
				},
			},
			{
				"Amber Sanctuary",
				"Every run leaves a light",
				"field",
				"#ffb000",
				{0.15, 0.13, 0.10},
				{1.0, 0.7, 0.05},
				{1.0, 0.9, 0.55},
				{0.09, 0.06, 0.03},
				{0.34, 0.22, 0.10},
				{0.25, 0.22, 0.18},
				{
					{0.64, 0.61, 0.56},
					{0.37, 0.36, 0.34},
					{0.22, 0.56, 0.92},
					{0.78, 0.84, 0.88},
				},
			},
		};
		return Chapters;
	}

	int ChapterAt(double Z, int Cleared, bool Roam)
	{
		int Index = 0;
		while (Index < static_cast<int>(kChapterBoundaries.size())
			&& Z < kChapterBoundaries[static_cast<std::size_t>(Index)])
		{
			++Index;
		}
		return Roam ? Index : std::min(Index, std::max(0, Cleared));
	}
}
