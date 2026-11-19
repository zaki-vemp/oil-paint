// Copyright Komorebi Project. Sword attack timings/poses, enemy combat stats, power metering, hit checks, and ram bands.
#include "Game/CombatRules.h"

#include <array>

namespace KomorebiCore
{
	namespace
	{
		constexpr std::array<double, 5> kPoseTimes = {0.0, 0.28, 0.53, 0.76, 1.0};

		constexpr std::array<AttackPoseFrame, 5> kCutPoses = {{
			{{0.34, 0.98, 0.12}, {0.1, -0.9, 0.2}, 0.0},
			{{0.52, 1.5, -0.16}, {0.5, 0.8, -0.5}, -0.65},
			{{-0.25, 1.13, 0.58}, {-0.8, 0.15, 0.6}, 0.6},
			{{-0.3, 0.85, 0.22}, {-0.5, -0.6, 0.5}, 0.35},
			{{0.34, 0.98, 0.12}, {0.1, -0.9, 0.2}, 0.0},
		}};

		constexpr std::array<AttackPoseFrame, 5> kBackhandPoses = {{
			{{0.34, 0.98, 0.12}, {0.1, -0.9, 0.2}, 0.0},
			{{-0.3, 1.1, 0.24}, {-0.8, 0.2, -0.3}, 0.5},
			{{0.47, 1.35, 0.53}, {0.85, 0.15, 0.4}, -0.55},
			{{0.51, 1.4, 0.0}, {0.5, 0.65, 0.1}, -0.3},
			{{0.34, 0.98, 0.12}, {0.1, -0.9, 0.2}, 0.0},
		}};

		constexpr std::array<AttackPoseFrame, 5> kThrustPoses = {{
			{{0.34, 0.98, 0.12}, {0.1, -0.9, 0.2}, 0.0},
			{{0.3, 1.22, -0.1}, {0.0, 0.1, 1.0}, -0.45},
			{{0.1, 1.18, 0.68}, {0.0, 0.05, 1.0}, 0.35},
			{{0.13, 1.15, 0.58}, {0.0, 0.05, 1.0}, 0.2},
			{{0.34, 0.98, 0.12}, {0.1, -0.9, 0.2}, 0.0},
		}};

		constexpr std::array<AttackPoseFrame, 5> kHeavyPoses = {{
			{{0.34, 0.98, 0.12}, {0.1, -0.9, 0.2}, 0.0},
			{{0.2, 1.76, -0.08}, {0.0, 0.5, -0.9}, -0.15},
			{{0.1, 1.1, 0.68}, {0.0, -0.8, 0.6}, 0.2},
			{{0.15, 0.85, 0.5}, {0.0, -0.9, 0.4}, 0.3},
			{{0.34, 0.98, 0.12}, {0.1, -0.9, 0.2}, 0.0},
		}};

		constexpr std::array<AttackPoseFrame, 5> kSpinPoses = {{
			{{0.34, 0.98, 0.12}, {0.1, -0.9, 0.2}, 0.0},
			{{0.62, 1.15, 0.22}, {1.0, 0.05, 0.25}, 0.3},
			{{0.64, 1.12, 0.26}, {1.0, 0.0, 0.3}, 0.2},
			{{0.6, 1.1, 0.3}, {0.9, -0.1, 0.4}, 0.1},
			{{0.34, 0.98, 0.12}, {0.1, -0.9, 0.2}, 0.0},
		}};

		constexpr std::array<AttackPoseFrame, 5> kCounterPoses = {{
			kGuardPose,
			{{0.19, 1.27, 0.62}, {-0.35, 0.18, 0.92}, -0.15},
			{{0.14, 1.11, 0.70}, {0.54, -0.22, 0.81}, 0.44},
			{{0.47, 1.08, 0.34}, {0.84, -0.37, 0.38}, 0.32},
			{{0.34, 0.98, 0.12}, {0.1, -0.9, 0.2}, 0.0},
		}};

		constexpr std::array<AttackPoseFrame, 5> kAdvancePoses = {{
			{{0.34, 0.98, 0.12}, {0.1, -0.9, 0.2}, 0.0},
			{{0.47, 0.79, -0.10}, {0.65, -0.35, -0.68}, -0.55},
			{{0.10, 1.24, 0.68}, {-0.25, 0.44, 0.86}, 0.28},
			{{-0.25, 1.53, 0.45}, {-0.4, 0.89, 0.21}, 0.40},
			{{0.34, 0.98, 0.12}, {0.1, -0.9, 0.2}, 0.0},
		}};

		const std::array<AttackPoseFrame, 5>& GetPoseKeys(EAttackType Type)
		{
			switch (Type)
			{
				case EAttackType::Cut: return kCutPoses;
				case EAttackType::Backhand: return kBackhandPoses;
				case EAttackType::Thrust: return kThrustPoses;
				case EAttackType::Heavy: return kHeavyPoses;
				case EAttackType::Spin: return kSpinPoses;
				case EAttackType::Counter: return kCounterPoses;
				case EAttackType::Advance: return kAdvancePoses;
			}
			return kCutPoses;
		}
	}

	const AttackSpec& GetAttackSpec(EAttackType Type)
	{
		static constexpr AttackSpec kCut{0.54, 0.32, 0.69, 2.6};
		static constexpr AttackSpec kBackhand{0.50, 0.30, 0.70, 2.6};
		static constexpr AttackSpec kThrust{0.66, 0.42, 0.72, 3.1};
		static constexpr AttackSpec kHeavy{0.88, 0.42, 0.73, 3.4};
		static constexpr AttackSpec kSpin{0.62, 0.16, 0.84, 3.3};
		static constexpr AttackSpec kCounter{0.47, 0.24, 0.65, 3.15};
		static constexpr AttackSpec kAdvance{0.64, 0.36, 0.66, 3.35};

		switch (Type)
		{
			case EAttackType::Cut: return kCut;
			case EAttackType::Backhand: return kBackhand;
			case EAttackType::Thrust: return kThrust;
			case EAttackType::Heavy: return kHeavy;
			case EAttackType::Spin: return kSpin;
			case EAttackType::Counter: return kCounter;
			case EAttackType::Advance: return kAdvance;
		}
		return kCut;
	}

	AttackPoseFrame SampleAttack(EAttackType Type, double U)
	{
		const std::array<AttackPoseFrame, 5>& Keys = GetPoseKeys(Type);
		const double ClampedU = Clamp(U, 0.0, 1.0);
		std::size_t Index = 0;
		while (Index < 3 && ClampedU > kPoseTimes[Index + 1])
		{
			++Index;
		}
		const double K = (ClampedU - kPoseTimes[Index]) / (kPoseTimes[Index + 1] - kPoseTimes[Index]);
		const double T = K * K * (3.0 - 2.0 * K);

		const AttackPoseFrame& A = Keys[Index];
		const AttackPoseFrame& B = Keys[Index + 1];
		AttackPoseFrame Out{};
		for (int i = 0; i < 3; ++i)
		{
			Out.Grip[i] = Lerp(A.Grip[i], B.Grip[i], T);
			Out.Blade[i] = Lerp(A.Blade[i], B.Blade[i], T);
		}
		Out.TrunkTurn = Lerp(A.TrunkTurn, B.TrunkTurn, T);
		return Out;
	}

	const EnemyKindSpec& GetEnemyKindSpec(EEnemyKind Kind)
	{
		static constexpr EnemyKindSpec kPunk{
			false, 9.0, 1.6, 3.6, 9.0, 1.55, 18.0, 3.2, 1.1, 0.8, 0.5, 4.0, 0.32, 0.5, 0.0, 0.0, 0.0,
		};
		static constexpr EnemyKindSpec kEnforcer{
			false, 16.0, 1.5, 4.6, 12.0, 1.95, 20.0, 4.0, 1.2, 0.9, 0.65, 6.0, 0.36, 0.52, 0.0, 0.0, 0.0,
		};
		static constexpr EnemyKindSpec kGunner{
			false, 12.0, 1.4, 3.4, 16.0, 11.0, 22.0, 4.0, 1.15, 0.8, 0.95, 3.0, 0.32, 0.5, 15.0, 5.0, 0.0,
		};
		static constexpr EnemyKindSpec kBlade{
			false, 20.0, 1.6, 5.2, 13.0, 2.1, 22.0, 4.0, 1.2, 0.9, 0.6, 6.0, 0.34, 0.52, 0.0, 0.0, 2.4,
		};
		static constexpr EnemyKindSpec kBrute{
			false, 26.0, 1.6, 5.2, 12.0, 2.6, 20.0, 4.5, 2.2, 1.9, 0.9, 9.0, 1.0, 0.7, 0.0, 0.0, 0.0,
		};
		static constexpr EnemyKindSpec kRival{
			false, 12.0, 1.6, 3.5, 9.0, 2.8, 17.0, 0.0, 1.0, 0.85, 1.0, 5.0, 0.34, 0.52, 0.0, 0.0, 0.0,
		};
		static constexpr EnemyKindSpec kCiv{
			true, 7.0, 1.3, 4.1, 0.0, 1.5, 45.0, 3.0, 1.08, 0.8, 0.55, 3.0, 0.3, 0.5, 0.0, 0.0, 0.0,
		};
		static constexpr EnemyKindSpec kThief{
			true, 7.0, 4.5, 4.5, 0.0, 1.5, 90.0, 3.0, 1.05, 0.8, 0.55, 3.0, 0.3, 0.5, 0.0, 0.0, 0.0,
		};

		switch (Kind)
		{
			case EEnemyKind::Punk: return kPunk;
			case EEnemyKind::Enforcer: return kEnforcer;
			case EEnemyKind::Gunner: return kGunner;
			case EEnemyKind::Blade: return kBlade;
			case EEnemyKind::Brute: return kBrute;
			case EEnemyKind::Rival: return kRival;
			case EEnemyKind::Civ: return kCiv;
			case EEnemyKind::Thief: return kThief;
		}
		return kPunk;
	}

	double GetHitHp(EEnemyKind Kind)
	{
		switch (Kind)
		{
			case EEnemyKind::Punk: return 60.0;
			case EEnemyKind::Enforcer: return 120.0;
			case EEnemyKind::Gunner: return 70.0;
			case EEnemyKind::Blade: return 150.0;
			case EEnemyKind::Brute: return 330.0;
			case EEnemyKind::Rival: return 120.0;
			case EEnemyKind::Civ: return 50.0;
			case EEnemyKind::Thief: return 50.0;
		}
		return 60.0;
	}

	double GetSwordDamage(EEnemyKind Kind)
	{
		switch (Kind)
		{
			case EEnemyKind::Punk: return 34.0;
			case EEnemyKind::Enforcer: return 30.0;
			case EEnemyKind::Gunner: return 34.0;
			case EEnemyKind::Blade: return 28.0;
			case EEnemyKind::Brute: return 22.0;
			case EEnemyKind::Rival: return 30.0;
			case EEnemyKind::Civ: return 30.0;
			case EEnemyKind::Thief: return 30.0;
		}
		return 30.0;
	}

	double GetBoltDamage(EEnemyKind Kind)
	{
		switch (Kind)
		{
			case EEnemyKind::Punk: return 80.0;
			case EEnemyKind::Enforcer: return 50.0;
			case EEnemyKind::Gunner: return 80.0;
			case EEnemyKind::Blade: return 45.0;
			case EEnemyKind::Brute: return 40.0;
			case EEnemyKind::Rival: return 32.0;
			case EEnemyKind::Civ: return 60.0;
			case EEnemyKind::Thief: return 60.0;
		}
		return 60.0;
	}

	double GetKnockback(std::string_view Source)
	{
		if (Source == "sword") return 3.2;
		if (Source == "thrust") return 6.5;
		if (Source == "heavy") return 9.0;
		if (Source == "whirl") return 7.5;
		if (Source == "spark") return 4.0;
		if (Source == "flare") return 0.0;
		return 3.0;
	}

	double GetHeft(EEnemyKind Kind)
	{
		switch (Kind)
		{
			case EEnemyKind::Punk: return 1.0;
			case EEnemyKind::Enforcer: return 0.75;
			case EEnemyKind::Gunner: return 1.0;
			case EEnemyKind::Blade: return 0.7;
			case EEnemyKind::Brute: return 0.32;
			case EEnemyKind::Rival: return 1.0;
			case EEnemyKind::Civ: return 1.1;
			case EEnemyKind::Thief: return 1.1;
		}
		return 1.0;
	}

	std::optional<double> SpendPower(double Power)
	{
		if (Power >= 1.0)
		{
			return Power - 1.0;
		}
		return std::nullopt;
	}

	double StepPower(double Power, double Dt, double Fill, double MaxPower)
	{
		if (Power >= MaxPower)
		{
			return MaxPower;
		}
		return std::min(MaxPower, Power + (Dt * MaxPower) / Fill);
	}

	bool InFront(
		double Px,
		double Pz,
		double Yaw,
		double Ex,
		double Ez,
		double Reach,
		double Arc,
		double Close)
	{
		const double Dx = Ex - Px;
		const double Dz = Ez - Pz;
		const double Dist = std::hypot(Dx, Dz);
		if (Close > 0.0 && Dist <= Close)
		{
			return true;
		}
		if (Dist > Reach)
		{
			return false;
		}
		const double Diff = WrapAngle(std::atan2(Dx, Dz) - Yaw);
		return std::abs(Diff) <= Arc;
	}

	bool StrikeHits(
		double EnemyX,
		double EnemyZ,
		double AttackYaw,
		double FallbackReach,
		double TargetX,
		double TargetY,
		double TargetZ,
		double GroundY,
		const EnemyMovePattern* Spec)
	{
		const double Height = (Spec != nullptr && Spec->Height > 0.0) ? Spec->Height : 0.9;
		const double Reach = (Spec != nullptr && Spec->Reach > 0.0) ? Spec->Reach : FallbackReach;
		const double Arc = (Spec != nullptr && Spec->Arc > 0.0) ? Spec->Arc : 0.85;
		return std::abs(TargetY - GroundY) < Height
			&& InFront(EnemyX, EnemyZ, AttackYaw, TargetX, TargetZ, Reach + 0.2, Arc, 0.35);
	}

	ERamBand GetRamBand(double Force)
	{
		if (Force < kRamConfig.Min)
		{
			return ERamBand::None;
		}
		if (Force < kRamConfig.Stumble)
		{
			return ERamBand::Stumble;
		}
		if (Force < kRamConfig.Kill)
		{
			return ERamBand::Knock;
		}
		return ERamBand::Kill;
	}
}
