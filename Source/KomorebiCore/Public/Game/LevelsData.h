// Copyright Komorebi Project. Six gated level segments, spawns, powers, goal, story beats, and unlock rules.
#pragma once

#include "KomorebiCoreAPI.h"
#include "Game/ChaptersData.h"

#include <array>
#include <optional>
#include <string_view>
#include <vector>

namespace KomorebiCore
{
	enum class EEnemyKind : std::uint8_t
	{
		Punk,
		Enforcer,
		Gunner,
		Blade,
		Brute,
		Rival,
		Civ,
		Thief
	};

	enum class EPowerKind : std::uint8_t
	{
		Spark,
		Dash,
		Heavy,
		Flare,
		Whirl
	};

	enum class EVehicleKind : std::uint8_t
	{
		Car,
		Sports,
		Pickup,
		Bike,
		Bicycle
	};

	inline constexpr std::array<EEnemyKind, 5> kCrewKinds = {
		EEnemyKind::Punk,
		EEnemyKind::Enforcer,
		EEnemyKind::Gunner,
		EEnemyKind::Blade,
		EEnemyKind::Brute,
	};

	inline constexpr std::array<EPowerKind, 5> kPowerOrder = {
		EPowerKind::Spark,
		EPowerKind::Dash,
		EPowerKind::Heavy,
		EPowerKind::Flare,
		EPowerKind::Whirl,
	};

	inline constexpr std::string_view kLevelsSaveKey = "oilpaint.levels.v1";

	struct PowerSpec
	{
		EPowerKind Kind = EPowerKind::Spark;
		std::string_view Name;
		std::string_view Key;
		std::string_view Hint;
	};

	struct PowerSet
	{
		bool Spark = false;
		bool Dash = false;
		bool Heavy = false;
		bool Flare = false;
		bool Whirl = false;

		bool Has(EPowerKind Kind) const
		{
			switch (Kind)
			{
				case EPowerKind::Spark: return Spark;
				case EPowerKind::Dash: return Dash;
				case EPowerKind::Heavy: return Heavy;
				case EPowerKind::Flare: return Flare;
				case EPowerKind::Whirl: return Whirl;
			}
			return false;
		}
	};

	struct NamedPoint
	{
		double X = 0.0;
		double Z = 0.0;
		std::string_view Name;
	};

	struct EnemySpawnSpot
	{
		EEnemyKind Kind = EEnemyKind::Punk;
		double X = 0.0;
		double Z = 0.0;
	};

	struct LevelSpec
	{
		std::string_view Name;
		NamedPoint Checkpoint{};
		std::optional<Vec2> Gate;
		std::optional<EPowerKind> Grant;
		std::vector<EnemySpawnSpot> Spots;
	};

	struct StoryBeat
	{
		std::string_view Id;
		int At = 0;
		std::string_view On;
		std::string_view Mission;
		int Seconds = 0;
		std::string_view Line;
	};

	KOMOREBICORE_API std::string_view EnemyKindToString(EEnemyKind Kind);
	KOMOREBICORE_API std::string_view PowerKindToString(EPowerKind Kind);
	KOMOREBICORE_API std::string_view VehicleKindToString(EVehicleKind Kind);

	/** Returns all five unlockable powers in progression order. */
	KOMOREBICORE_API const std::vector<PowerSpec>& GetPowers();

	/** Returns the power metadata for a specific power kind. */
	KOMOREBICORE_API const PowerSpec& GetPowerSpec(EPowerKind Kind);

	/** Returns all six levels stretched by kWorldScale with twin-wave spawns generated. */
	KOMOREBICORE_API const std::vector<LevelSpec>& GetLevels();

	/** Returns the final clearing goal point scaled by kWorldScale. */
	KOMOREBICORE_API const NamedPoint& GetGoal();

	/** Returns the ten campaign relay story beats. */
	KOMOREBICORE_API const std::vector<StoryBeat>& GetStoryBeats();

	/** Powers derived from clearing `Cleared` segments (never stored directly). */
	KOMOREBICORE_API PowerSet PowersFor(int Cleared);

	/** Vehicles unlocked in the garage after `Cleared` segments. */
	KOMOREBICORE_API std::vector<EVehicleKind> UnlocksFor(int Cleared);
}
