// Copyright Komorebi Project. Six gated level segments, spawns, powers, goal, story beats, and unlock rules.
#include "Game/LevelsData.h"

namespace KomorebiCore
{
	namespace
	{
		struct BaseLevelSpec
		{
			std::string_view Name;
			NamedPoint Checkpoint{};
			std::optional<Vec2> Gate;
			std::optional<EPowerKind> Grant;
			std::vector<EnemySpawnSpot> Spots;
		};

		constexpr std::array<EVehicleKind, 5> kGarageOrder = {
			EVehicleKind::Car,
			EVehicleKind::Bicycle,
			EVehicleKind::Bike,
			EVehicleKind::Sports,
			EVehicleKind::Pickup,
		};

		std::vector<LevelSpec> BuildLevels()
		{
			const std::vector<BaseLevelSpec> BaseLevels = {
				{
					"The strip",
					{0.0, 78.0, "Sunleaf Relay"},
					Vec2{-7.5, 54.0},
					EPowerKind::Spark,
					{
						{EEnemyKind::Punk, 3.0, 66.0},
						{EEnemyKind::Punk, -6.0, 62.0},
					},
				},
				{
					"The arcade",
					{-7.0, 51.0, "Arcade Relay"},
					Vec2{3.8, 30.0},
					EPowerKind::Dash,
					{
						{EEnemyKind::Punk, -9.0, 44.0},
						{EEnemyKind::Punk, 8.0, 42.0},
						{EEnemyKind::Enforcer, -1.0, 34.0},
					},
				},
				{
					"The heights",
					{3.6, 28.0, "Heights Relay"},
					Vec2{10.5, 4.0},
					EPowerKind::Heavy,
					{
						{EEnemyKind::Punk, 12.0, 18.0},
						{EEnemyKind::Enforcer, 4.0, 10.0},
						{EEnemyKind::Gunner, -4.0, 8.0},
					},
				},
				{
					"The stacks",
					{10.3, 2.0, "Stacks Relay"},
					Vec2{0.0, -30.0},
					EPowerKind::Flare,
					{
						{EEnemyKind::Enforcer, 2.0, -14.0},
						{EEnemyKind::Gunner, -4.0, -22.0},
						{EEnemyKind::Brute, 5.0, -26.0},
					},
				},
				{
					"The ashen hollow",
					{0.0, -32.0, "Hollow Relay"},
					Vec2{1.6, -62.0},
					EPowerKind::Whirl,
					{
						{EEnemyKind::Punk, -6.0, -36.0},
						{EEnemyKind::Blade, -8.0, -44.0},
						{EEnemyKind::Brute, -3.0, -48.0},
						{EEnemyKind::Gunner, 3.0, -53.0},
					},
				},
				{
					"The sanctuary run",
					{2.5, -64.0, "Sanctuary Relay"},
					std::nullopt,
					std::nullopt,
					{
						{EEnemyKind::Blade, 0.0, -69.0},
						{EEnemyKind::Brute, 8.0, -72.0},
						{EEnemyKind::Gunner, 3.0, -78.0},
						{EEnemyKind::Brute, 11.0, -82.0},
					},
				},
			};

			std::vector<LevelSpec> Out;
			Out.reserve(BaseLevels.size());
			for (const BaseLevelSpec& Base : BaseLevels)
			{
				LevelSpec Level;
				Level.Name = Base.Name;
				Level.Checkpoint = {Base.Checkpoint.X * kWorldScale, Base.Checkpoint.Z * kWorldScale, Base.Checkpoint.Name};
				if (Base.Gate.has_value())
				{
					Level.Gate = Vec2{Base.Gate->X * kWorldScale, Base.Gate->Z * kWorldScale};
				}
				Level.Grant = Base.Grant;

				std::vector<EnemySpawnSpot> ScaledSpots;
				ScaledSpots.reserve(Base.Spots.size());
				for (const EnemySpawnSpot& Spot : Base.Spots)
				{
					ScaledSpots.push_back({Spot.Kind, Spot.X * kWorldScale, Spot.Z * kWorldScale});
				}

				Level.Spots.reserve(ScaledSpots.size() * 2);
				for (const EnemySpawnSpot& Spot : ScaledSpots)
				{
					Level.Spots.push_back(Spot);
				}
				// Twin wave: same crew staggered back up the way and across it; a brute's twin is an enforcer.
				for (std::size_t Index = 0; Index < ScaledSpots.size(); ++Index)
				{
					const EnemySpawnSpot& Primary = ScaledSpots[Index];
					const EEnemyKind TwinKind = (Primary.Kind == EEnemyKind::Brute) ? EEnemyKind::Enforcer : Primary.Kind;
					const double OffsetX = (Index % 2 != 0) ? -7.0 : 7.0;
					Level.Spots.push_back({TwinKind, Primary.X + OffsetX, Primary.Z + 7.0});
				}

				Out.push_back(std::move(Level));
			}
			return Out;
		}
	}

	std::string_view EnemyKindToString(EEnemyKind Kind)
	{
		switch (Kind)
		{
			case EEnemyKind::Punk: return "punk";
			case EEnemyKind::Enforcer: return "enforcer";
			case EEnemyKind::Gunner: return "gunner";
			case EEnemyKind::Blade: return "blade";
			case EEnemyKind::Brute: return "brute";
			case EEnemyKind::Rival: return "rival";
			case EEnemyKind::Civ: return "civ";
			case EEnemyKind::Thief: return "thief";
		}
		return "punk";
	}

	std::string_view PowerKindToString(EPowerKind Kind)
	{
		switch (Kind)
		{
			case EPowerKind::Spark: return "spark";
			case EPowerKind::Dash: return "dash";
			case EPowerKind::Heavy: return "heavy";
			case EPowerKind::Flare: return "flare";
			case EPowerKind::Whirl: return "whirl";
		}
		return "spark";
	}

	std::string_view VehicleKindToString(EVehicleKind Kind)
	{
		switch (Kind)
		{
			case EVehicleKind::Car: return "car";
			case EVehicleKind::Sports: return "sports";
			case EVehicleKind::Pickup: return "pickup";
			case EVehicleKind::Bike: return "bike";
			case EVehicleKind::Bicycle: return "bicycle";
		}
		return "car";
	}

	const std::vector<PowerSpec>& GetPowers()
	{
		static const std::vector<PowerSpec> Powers = {
			{EPowerKind::Spark, "Spark", "Q", "Q throws a spark"},
			{EPowerKind::Dash, "Dash", "E", "E dashes through danger"},
			{EPowerKind::Heavy, "Heavy swing", "F", "F: a heavy swing"},
			{EPowerKind::Flare, "Lantern flare", "R", "R: a flare that staggers the pack"},
			{EPowerKind::Whirl, "Whirl", "C", "C: a spinning slash that hits all around (one spark)"},
		};
		return Powers;
	}

	const PowerSpec& GetPowerSpec(EPowerKind Kind)
	{
		const std::vector<PowerSpec>& Powers = GetPowers();
		const std::size_t Index = static_cast<std::size_t>(Kind);
		return Index < Powers.size() ? Powers[Index] : Powers[0];
	}

	const std::vector<LevelSpec>& GetLevels()
	{
		static const std::vector<LevelSpec> Levels = BuildLevels();
		return Levels;
	}

	const NamedPoint& GetGoal()
	{
		static const NamedPoint Goal = {11.0 * kWorldScale, -96.0 * kWorldScale, "Amber Sanctuary"};
		return Goal;
	}

	const std::vector<StoryBeat>& GetStoryBeats()
	{
		static const std::vector<StoryBeat> Story = {
			{
				"late",
				0,
				"",
				"time",
				180,
				"Courier Zero-Seven, ghost-link online. Aethelgard sealed the six gates under the Eternity Grid.",
			},
			{
				"waiting",
				0,
				"clear",
				"",
				0,
				"They were not waiting for your blade, Kael. They were waiting for Dr. Mira’s Komorebi Seed.",
			},
			{
				"runner",
				1,
				"",
				"chase",
				0,
				"An Aethelgard Syndicate courier lifted your transit cipher. Intercept them and sync the Arcade Uplink.",
			},
			{
				"heights",
				2,
				"",
				"chase",
				0,
				"Jade Heights is drowning our link in cortical white noise. Shatter all three Aethelgard Beacons.",
			},
			{
				"seen",
				2,
				"heat",
				"",
				0,
				"Sleepwalking citizens spotted the strike—break line of sight or step into a Mission Relay.",
			},
			{
				"tail",
				3,
				"",
				"heat",
				0,
				"Scrub your trace first. Moonveil Stacks will not open while Aethelgard patrols lock your tag.",
			},
			{
				"whole",
				4,
				"",
				"deliver",
				0,
				"Smash the memory-purge caches in Ashen Hollow before Ren erases Ward Four a second time.",
			},
			{
				"convoy",
				3,
				"clear",
				"convoy",
				0,
				"Thread the Moonveil Relay Rings at full throttle and break the Stacks siege garrison.",
			},
			{
				"name",
				5,
				"",
				"boss",
				0,
				"Protocol Zero-Seven leads straight to Ren. Overload the Shield Pylons and face his true Mirror.",
			},
			{
				"end",
				6,
				"",
				"boss",
				0,
				"The Komorebi Seed has bloomed across Neo-Kyoto. Every district is awake and yours to roam, Kael.",
			},
		};
		return Story;
	}

	PowerSet PowersFor(int Cleared)
	{
		PowerSet Out;
		const std::vector<LevelSpec>& Levels = GetLevels();
		const int Limit = std::min(std::max(0, Cleared), static_cast<int>(Levels.size()));
		for (int i = 0; i < Limit; ++i)
		{
			const std::optional<EPowerKind>& Grant = Levels[static_cast<std::size_t>(i)].Grant;
			if (!Grant.has_value())
			{
				continue;
			}
			switch (*Grant)
			{
				case EPowerKind::Spark: Out.Spark = true; break;
				case EPowerKind::Dash: Out.Dash = true; break;
				case EPowerKind::Heavy: Out.Heavy = true; break;
				case EPowerKind::Flare: Out.Flare = true; break;
				case EPowerKind::Whirl: Out.Whirl = true; break;
			}
		}
		return Out;
	}

	std::vector<EVehicleKind> UnlocksFor(int Cleared)
	{
		const int Count = std::min(static_cast<int>(kGarageOrder.size()), 3 + std::max(0, Cleared));
		std::vector<EVehicleKind> Out;
		Out.reserve(static_cast<std::size_t>(Count));
		for (int i = 0; i < Count; ++i)
		{
			Out.push_back(kGarageOrder[static_cast<std::size_t>(i)]);
		}
		return Out;
	}
}
