// Copyright Komorebi Project. Enemy attack patterns, defense specs, attack selection, and crew turn ordering.
#pragma once

#include "KomorebiCoreAPI.h"
#include "Game/LevelsData.h"

#include <optional>
#include <string_view>
#include <vector>

namespace KomorebiCore
{
	enum class EDefenseKind : std::uint8_t
	{
		None,
		Evade,
		Guard
	};

	struct EnemyMovePattern
	{
		std::string_view Name;
		double Wind = 0.0;
		double Reach = 0.0;
		double Arc = 0.0;    // 0.0 => unset (defaults to 0.85 in StrikeHits)
		double Recover = 0.0;
		double Rush = 0.0;
		double Damage = 0.0; // 0.0 => use EnemyKindSpec::Damage
		double Height = 0.0; // 0.0 => unset (defaults to 0.9 in StrikeHits)
		bool Shot = false;
		bool Unblockable = false;
	};

	struct EnemyDefenseSpec
	{
		EDefenseKind Kind = EDefenseKind::None;
		double Range = 0.0;
		double Time = 0.0;
		double Cooldown = 0.0;
		double Speed = 0.0;
	};

	struct CrewMemberOrder
	{
		double Windup = 0.0;
		double Score = 0.0;
		bool Ranged = false;
		bool Lead = false;
		int Side = 1;
		double Radius = 3.6;
	};

	/** Returns the authored move pattern list for an enemy kind (2 moves for crew kinds, empty otherwise). */
	KOMOREBICORE_API const std::vector<EnemyMovePattern>& GetEnemyPatterns(EEnemyKind Kind);

	/** Returns the defensive reaction spec for an enemy kind, if any. */
	KOMOREBICORE_API std::optional<EnemyDefenseSpec> GetEnemyDefense(EEnemyKind Kind);

	/** Picks attack pattern index (0 or 1) given enemy kind, distance to target, reprisal flag, and attack count. */
	KOMOREBICORE_API int ChooseAttack(EEnemyKind Kind, double Distance, bool Reprisal, int AttackCount);

	/** Looks up the pattern for `Kind` at `AttackIndex`, or nullptr if none exists. */
	KOMOREBICORE_API const EnemyMovePattern* AttackFor(EEnemyKind Kind, int AttackIndex);

	/** Assigns melee and ranged lead turns, flanking sides, and ring radii for a crew group sharing a target. */
	KOMOREBICORE_API void ComputeCrewOrder(std::vector<CrewMemberOrder>& Group);
}
