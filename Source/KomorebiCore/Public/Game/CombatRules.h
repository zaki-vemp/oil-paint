// Copyright Komorebi Project. Sword attack timings/poses, enemy combat stats, power metering, hit checks, and ram bands.
#pragma once

#include "KomorebiCoreAPI.h"
#include "Game/LevelsData.h"
#include "Game/StreetConfig.h"
#include "Game/TacticsRules.h"

#include <optional>
#include <string_view>

namespace KomorebiCore
{
	enum class EAttackType : std::uint8_t
	{
		Cut,
		Backhand,
		Thrust,
		Heavy,
		Spin,
		Counter,
		Advance
	};

	struct AttackSpec
	{
		double Duration = 0.54;
		double HitStart = 0.32;
		double HitEnd = 0.69;
		double Reach = 2.6;
	};

	struct AttackPoseFrame
	{
		double Grip[3] = {0.0, 0.0, 0.0};
		double Blade[3] = {0.0, 0.0, 0.0};
		double TrunkTurn = 0.0;
	};

	inline constexpr AttackPoseFrame kGuardPose = {
		{0.22, 1.36, 0.45},
		{-0.84, 0.52, 0.15},
		-0.28,
	};

	inline constexpr double kPowerMax = 3.0;
	inline constexpr double kPowerFill = 42.0;
	inline constexpr double kDashFill = 26.0;
	inline constexpr double kWhirlMul = 1.8;
	inline constexpr double kPlungeMul = 1.4;
	inline constexpr double kParryMul = 1.5;
	inline constexpr double kPerfectWindow = 0.32;
	inline constexpr double kParryWindow = 0.28;
	inline constexpr double kAimLock = 0.3;
	inline constexpr double kLaunchSpeed = 6.5;
	inline constexpr double kMaxPlayerHp = 100.0;

	struct EnemyKindSpec
	{
		bool IsCiv = false;
		double Damage = 0.0;
		double RoamSpeed = 0.0;
		double ChaseSpeed = 0.0;
		double AggroRange = 0.0;
		double Reach = 0.0;
		double Leash = 0.0;
		double HomeRadius = 0.0;
		double Scale = 1.0;
		double BoltRadius = 0.8;
		double Windup = 0.5;
		double Push = 4.0;
		double CapsuleRadius = 0.32;
		double CapsuleHalfHeight = 0.5;
		double ShotSpeed = 0.0;
		double KeepDistance = 0.0;
		double Lunge = 0.0;
	};

	enum class ERamBand : std::uint8_t
	{
		None,
		Stumble,
		Knock,
		Kill
	};

	KOMOREBICORE_API const AttackSpec& GetAttackSpec(EAttackType Type);
	KOMOREBICORE_API AttackPoseFrame SampleAttack(EAttackType Type, double U);

	KOMOREBICORE_API const EnemyKindSpec& GetEnemyKindSpec(EEnemyKind Kind);
	KOMOREBICORE_API double GetHitHp(EEnemyKind Kind);
	KOMOREBICORE_API double GetSwordDamage(EEnemyKind Kind);
	KOMOREBICORE_API double GetBoltDamage(EEnemyKind Kind);
	KOMOREBICORE_API double GetKnockback(std::string_view Source);
	KOMOREBICORE_API double GetHeft(EEnemyKind Kind);

	/** Power remaining after spending one charge, or std::nullopt if less than 1.0. */
	KOMOREBICORE_API std::optional<double> SpendPower(double Power);

	/** Refills power toward MaxPower over Fill seconds from empty. */
	KOMOREBICORE_API double StepPower(double Power, double Dt, double Fill = kPowerFill, double MaxPower = kPowerMax);

	/** True when (Ex, Ez) lies inside a swing from (Px, Pz) facing Yaw within Reach and Arc (or within Close metres). */
	KOMOREBICORE_API bool InFront(
		double Px,
		double Pz,
		double Yaw,
		double Ex,
		double Ez,
		double Reach,
		double Arc,
		double Close = 0.65);

	/** True when a committed enemy strike connects with a target at (TargetX, TargetY, TargetZ). */
	KOMOREBICORE_API bool StrikeHits(
		double EnemyX,
		double EnemyZ,
		double AttackYaw,
		double FallbackReach,
		double TargetX,
		double TargetY,
		double TargetZ,
		double GroundY,
		const EnemyMovePattern* Spec = nullptr);

	/** Maps a vehicle ram force (m/s scaled by mass relative to sedan) to its bystander consequence band. */
	KOMOREBICORE_API ERamBand GetRamBand(double Force);
}
