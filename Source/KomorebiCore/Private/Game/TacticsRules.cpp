// Copyright Komorebi Project. Enemy attack patterns, defense specs, attack selection, and crew turn ordering.
#include "Game/TacticsRules.h"

#include <algorithm>
#include <numeric>

namespace KomorebiCore
{
	const std::vector<EnemyMovePattern>& GetEnemyPatterns(EEnemyKind Kind)
	{
		static const std::vector<EnemyMovePattern> Empty;
		static const std::vector<EnemyMovePattern> Punk = {
			{"Quick cross", 0.50, 1.55, 0.76, 0.75, 0.0, 0.0, 0.0, false, false},
			{"Running hook", 0.68, 1.6, 1.05, 0.92, 0.95, 11.0, 0.0, false, false},
		};
		static const std::vector<EnemyMovePattern> Enforcer = {
			{"Heavy straight", 0.65, 1.95, 0.68, 1.1, 0.0, 0.0, 0.0, false, false},
			{"Shield bash", 0.79, 2.05, 0.88, 1.25, 0.7, 19.0, 0.0, false, false},
		};
		static const std::vector<EnemyMovePattern> Gunner = {
			{"Pulse shot", 0.95, 11.0, 0.0, 1.2, 0.0, 0.0, 0.0, true, false},
			{"Stock strike", 0.65, 1.8, 0.88, 1.05, 0.0, 10.0, 0.0, false, false},
		};
		static const std::vector<EnemyMovePattern> Blade = {
			{"Draw lunge", 0.60, 2.1, 0.54, 1.08, 2.4, 0.0, 0.0, false, false},
			{"Reverse cut", 0.83, 2.35, 1.18, 1.34, 0.55, 17.0, 0.0, false, false},
		};
		static const std::vector<EnemyMovePattern> Brute = {
			{"Pile-driver", 0.90, 2.6, 0.64, 1.52, 0.0, 0.0, 1.7, false, true},
			{"Seismic sweep", 1.22, 3.4, kPi, 1.68, 0.0, 20.0, 0.65, false, false},
		};

		switch (Kind)
		{
			case EEnemyKind::Punk: return Punk;
			case EEnemyKind::Enforcer: return Enforcer;
			case EEnemyKind::Gunner: return Gunner;
			case EEnemyKind::Blade: return Blade;
			case EEnemyKind::Brute: return Brute;
			default: return Empty;
		}
	}

	std::optional<EnemyDefenseSpec> GetEnemyDefense(EEnemyKind Kind)
	{
		switch (Kind)
		{
			case EEnemyKind::Punk:
				return EnemyDefenseSpec{EDefenseKind::Evade, 3.2, 0.28, 3.7, 5.8};
			case EEnemyKind::Enforcer:
				return EnemyDefenseSpec{EDefenseKind::Guard, 3.45, 0.55, 4.4, 0.0};
			case EEnemyKind::Gunner:
				return EnemyDefenseSpec{EDefenseKind::Evade, 3.7, 0.32, 4.2, 5.4};
			case EEnemyKind::Blade:
				return EnemyDefenseSpec{EDefenseKind::Evade, 3.65, 0.31, 3.45, 6.4};
			case EEnemyKind::Brute:
				return EnemyDefenseSpec{EDefenseKind::Guard, 3.85, 0.7, 5.2, 0.0};
			default:
				return std::nullopt;
		}
	}

	int ChooseAttack(EEnemyKind Kind, double Distance, bool Reprisal, int AttackCount)
	{
		if (Kind == EEnemyKind::Gunner)
		{
			return Distance < 2.05 ? 1 : 0;
		}
		if (Kind == EEnemyKind::Enforcer && Reprisal)
		{
			return 1;
		}
		return ((AttackCount % 2) + 2) % 2;
	}

	const EnemyMovePattern* AttackFor(EEnemyKind Kind, int AttackIndex)
	{
		const std::vector<EnemyMovePattern>& Patterns = GetEnemyPatterns(Kind);
		if (AttackIndex < 0 || static_cast<std::size_t>(AttackIndex) >= Patterns.size())
		{
			return nullptr;
		}
		return &Patterns[static_cast<std::size_t>(AttackIndex)];
	}

	void ComputeCrewOrder(std::vector<CrewMemberOrder>& Group)
	{
		std::vector<std::size_t> Indices;
		Indices.reserve(Group.size());

		for (bool bRanged : {false, true})
		{
			Indices.clear();
			for (std::size_t Idx = 0; Idx < Group.size(); ++Idx)
			{
				if (Group[Idx].Ranged == bRanged)
				{
					Indices.push_back(Idx);
				}
			}

			std::stable_sort(Indices.begin(), Indices.end(), [&](std::size_t A, std::size_t B) {
				const bool WindA = Group[A].Windup > 0.0;
				const bool WindB = Group[B].Windup > 0.0;
				if (WindA != WindB)
				{
					return WindA && !WindB;
				}
				return Group[A].Score < Group[B].Score;
			});

			for (std::size_t Rank = 0; Rank < Indices.size(); ++Rank)
			{
				CrewMemberOrder& Member = Group[Indices[Rank]];
				Member.Lead = (Rank == 0);
				Member.Side = (Rank % 2 != 0) ? -1 : 1;
				Member.Radius = bRanged
					? (7.0 + static_cast<double>(Rank % 2))
					: (3.6 + 0.65 * std::floor((static_cast<double>(Rank) - 1.0) / 2.0));
			}
		}
	}
}
