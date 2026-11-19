// Copyright Komorebi Project. Six mirror-rival duels state machine, move table, hit detection, and visual state.
#pragma once

#include "KomorebiCoreAPI.h"
#include "Game/ChaptersData.h"
#include "Game/CombatRules.h"

#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace KomorebiCore
{
	inline constexpr double kArenaRadius = 8.6;

	enum class EDuelMove : std::uint8_t
	{
		Sweep,
		Thrust,
		Double,
		Slam,
		Reverse,
		Reply,
		Spiral,
		Feint
	};

	enum class EDuelPhase : std::uint8_t
	{
		Waiting,
		Intro,
		Approach,
		Guard,
		Sidestep,
		Windup,
		Active,
		Recovery
	};

	enum class EDuelEvent : std::uint8_t
	{
		None,
		Intro,
		Reset,
		Windup,
		Strike,
		Opening
	};

	enum class EDuelVisualMode : std::uint8_t
	{
		Stance,
		Danger,
		Opening
	};

	struct DuelConfig
	{
		std::string_view Name;
		double X = 0.0;
		double Z = 0.0;
		double Hp = 120.0;
		std::string_view Lesson;
		std::vector<EDuelMove> Moves;
	};

	struct DuelMoveSpec
	{
		double Windup = 1.0;
		double Active = 0.2;
		double Recover = 1.35;
		double Reach = 2.8;
		double Arc = 1.5;
		double Damage = 12.0;
		EAttackType Pose = EAttackType::Cut;
		std::optional<EAttackType> SecondPose;
		double Dash = 0.0;
		bool Low = false;
		bool Unblockable = false;
		std::string_view Cue;
	};

	struct DuelState
	{
		int Seg = 0;
		std::string_view Name;
		std::string_view Lesson;
		double X = 0.0;
		double Z = 0.0;
		double HomeX = 0.0;
		double HomeZ = 0.0;
		double Hp = 120.0;
		double MaxHp = 120.0;
		EDuelPhase Phase = EDuelPhase::Waiting;
		double Timer = 0.0;
		int MoveIndex = 0;
		EDuelMove Move = EDuelMove::Sweep;
		double AttackYaw = 0.0;
		double Yaw = kPi;
		double Windup = 0.0;
		double Recovery = 0.0;
		double Cool = 0.0;
		double AttackU = -1.0;
		double Going = 0.0;
		bool HitDone = false;
		bool Connected = false;
		int Beat = 0;
		double React = 1.7;
		int ReactSide = 1;
		bool Evaded = false;
		int Punishes = 0;
		int Guard = 2;
		bool Ready = false;
		bool Dead = false;
	};

	struct DuelStepInput
	{
		double X = 0.0;
		double Z = 0.0;
		bool Ready = false;
		std::function<void(double, double)> MoveFn;
		bool Threatening = false;
	};

	struct DuelVisualState
	{
		bool Engaged = false;
		EDuelVisualMode Mode = EDuelVisualMode::Stance;
		RGB Color{};
		RGB Accent{};
		double Progress = 0.0;
		double Reach = 2.8;
		double Arc = 1.5;
		double Spin = 1.1;
	};

	/** Returns all six mirror duels scaled by kWorldScale. */
	KOMOREBICORE_API const std::vector<DuelConfig>& GetDuelConfigs();

	/** Returns move timing, geometry, damage, and cue for a duel move. */
	KOMOREBICORE_API const DuelMoveSpec& GetDuelMoveSpec(EDuelMove Move);

	/** Initializes a fresh rival state for chapter index (0..5). */
	KOMOREBICORE_API DuelState CreateDuel(int Index);

	/** Steps the pure duel state machine by Dt seconds and returns any emitted transition event. */
	KOMOREBICORE_API EDuelEvent StepDuel(DuelState& State, double Dt, const DuelStepInput& Input);

	/** True when the rival's active blade window connects with a target at (X, Y, Z) over Floor. */
	KOMOREBICORE_API bool DuelHit(const DuelState& State, double X, double Y, double Z, double Floor);

	/** Applies guard/punish/guard-break rules and returns final integer damage dealt to the rival. */
	KOMOREBICORE_API int DamageRival(DuelState& State, double Amount, std::string_view Source);

	/** Computes telegraph ring/arc color, progress, and spin speed for the current duel state. */
	KOMOREBICORE_API DuelVisualState GetDuelVisual(const DuelState& State);
}
