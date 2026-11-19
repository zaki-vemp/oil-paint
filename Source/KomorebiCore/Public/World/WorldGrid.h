// Copyright Komorebi Project. Neo-Kyoto city street grid, zones, building lots, and sidewalk routing.
#pragma once

#include "KomorebiCoreAPI.h"

#include <array>
#include <vector>

namespace KomorebiCore
{
	inline constexpr double kSpacing = 40.0;
	inline constexpr double kGridMin = -280.0;
	inline constexpr double kGridMax = 280.0;
	inline constexpr double kKerb = 5.8;

	struct ZoneSplitConfig
	{
		double Downtown = 120.0;
		double Market = -15.0;
		double East = -35.0;
	};

	inline constexpr ZoneSplitConfig ZoneSplit{ 120.0, -15.0, -35.0 };

	struct ZoneInfo
	{
		const char* Name = "";
		int Chapter = 0;
	};

	inline constexpr std::array<ZoneInfo, 4> Zones{{
		{ "Downtown", 0 },
		{ "Market", 1 },
		{ "Docks", 4 },
		{ "Suburbs", 2 },
	}};

	struct BlockLot
	{
		double X = 0.0;
		double Z = 0.0;
		int Zone = 0;
		bool bHeart = false;
	};

	struct PavementStep
	{
		double X = 0.0;
		double Z = 0.0;
		bool bCrossed = false;
	};

	/** North-south avenue X coordinates (-280..280 step 40). */
	KOMOREBICORE_API const std::vector<double>& Avenues();

	/** East-west street Z coordinates (-280..280 step 40). */
	KOMOREBICORE_API const std::vector<double>& Streets();

	/** District zone index (0 = Downtown, 1 = Market, 2 = Docks, 3 = Suburbs). */
	KOMOREBICORE_API int ZoneAt(double X, double Z);

	/** Metres from (X, Z) to the nearest avenue or street, or road endpoint outside the grid. */
	KOMOREBICORE_API double GridDist(double X, double Z);

	/** All building lot coordinates across every city block. */
	KOMOREBICORE_API std::vector<BlockLot> BlockLots();

	/** Next sidewalk waypoint along the kerb, crossing at intersections when `bCross` is true. */
	KOMOREBICORE_API PavementStep NextPavement(double X, double Z, double Dir, bool bCross);
}
