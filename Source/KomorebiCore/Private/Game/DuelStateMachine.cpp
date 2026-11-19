// Copyright Komorebi Project. Six mirror-rival duels state machine, move table, hit detection, and visual state.
#include "Game/DuelStateMachine.h"

namespace KomorebiCore
{
	const std::vector<DuelConfig>& GetDuelConfigs()
	{
		static const std::vector<DuelConfig> Duels = {
			{
				"Ren, the Patient Blade",
				-7.5 * kWorldScale,
				58.0 * kWorldScale,
				120.0,
				"Jump or backstep the sweep. Strike when the blade falls.",
				{EDuelMove::Sweep},
			},
			{
				"Ren, the Piercing Wind",
				3.8 * kWorldScale,
				35.0 * kWorldScale,
				150.0,
				"Sidestep the thrust. Use your spark during the opening.",
				{EDuelMove::Thrust, EDuelMove::Reverse},
			},
			{
				"Ren, the Twin Current",
				10.5 * kWorldScale,
				9.0 * kWorldScale,
				200.0,
				"Wait for both cuts. Quickstep sideways when Ren circles.",
				{EDuelMove::Double, EDuelMove::Thrust, EDuelMove::Reverse},
			},
			{
				"Ren, the Iron Will",
				0.0 * kWorldScale,
				-25.0 * kWorldScale,
				240.0,
				"Two heavy strikes break his guard. Evade the overhead slam.",
				{EDuelMove::Slam, EDuelMove::Reply, EDuelMove::Double},
			},
			{
				"Ren, the Ember Spiral",
				-0.9 * kWorldScale,
				-57.0 * kWorldScale,
				270.0,
				"Jump or step beyond the spiral. Punish recovery with heavy strikes to earn Whirl.",
				{EDuelMove::Spiral, EDuelMove::Reverse, EDuelMove::Slam},
			},
			{
				"Ren, your Mirror",
				10.0 * kWorldScale,
				-88.0 * kWorldScale,
				320.0,
				"Wait out the feint. Whirl cracks the guard, and Flare forces an opening.",
				{EDuelMove::Feint, EDuelMove::Double, EDuelMove::Reply, EDuelMove::Spiral, EDuelMove::Slam},
			},
		};
		return Duels;
	}

	const DuelMoveSpec& GetDuelMoveSpec(EDuelMove Move)
	{
		static const DuelMoveSpec kSweep{
			1.0, 0.2, 1.35, 2.8, 1.5, 12.0, EAttackType::Cut, std::nullopt, 0.0, false, false,
			"Sweep — jump or step out of the ring",
		};
		static const DuelMoveSpec kThrust{
			0.95, 0.26, 1.3, 2.1, 0.42, 16.0, EAttackType::Thrust, std::nullopt, 10.0, false, false,
			"Thrust — sidestep the committed line",
		};
		static const DuelMoveSpec kDouble{
			0.9, 0.75, 1.4, 2.7, 1.3, 13.0, EAttackType::Backhand, EAttackType::Cut, 0.0, true, false,
			"Two cuts — wait for the second one",
		};
		static const DuelMoveSpec kSlam{
			1.15, 0.23, 1.6, 3.1, 0.65, 24.0, EAttackType::Heavy, std::nullopt, 0.0, false, true,
			"Overhead · unblockable — move sideways",
		};
		static const DuelMoveSpec kReverse{
			0.7, 0.23, 1.17, 2.65, 1.28, 14.0, EAttackType::Backhand, std::nullopt, 0.0, false, false,
			"Reverse cut — retreat past the arc",
		};
		static const DuelMoveSpec kReply{
			0.72, 0.25, 1.35, 2.25, 0.55, 17.0, EAttackType::Counter, std::nullopt, 3.4, false, false,
			"Riposte — step off his blade line",
		};
		static const DuelMoveSpec kSpiral{
			1.04, 0.48, 1.65, 3.25, kPi, 19.0, EAttackType::Spin, std::nullopt, 0.0, true, false,
			"Ember spiral — jump or leave the circle",
		};
		static const DuelMoveSpec kFeint{
			1.36, 0.31, 1.47, 2.65, 0.76, 21.0, EAttackType::Advance, std::nullopt, 5.5, false, false,
			"Feint — wait for the second windup",
		};

		switch (Move)
		{
			case EDuelMove::Sweep: return kSweep;
			case EDuelMove::Thrust: return kThrust;
			case EDuelMove::Double: return kDouble;
			case EDuelMove::Slam: return kSlam;
			case EDuelMove::Reverse: return kReverse;
			case EDuelMove::Reply: return kReply;
			case EDuelMove::Spiral: return kSpiral;
			case EDuelMove::Feint: return kFeint;
		}
		return kSweep;
	}

	DuelState CreateDuel(int Index)
	{
		const std::vector<DuelConfig>& Duels = GetDuelConfigs();
		const int Clamped = std::max(0, std::min(Index, static_cast<int>(Duels.size()) - 1));
		const DuelConfig& Config = Duels[static_cast<std::size_t>(Clamped)];

		DuelState State;
		State.Seg = Clamped;
		State.Name = Config.Name;
		State.Lesson = Config.Lesson;
		State.X = Config.X;
		State.Z = Config.Z;
		State.HomeX = Config.X;
		State.HomeZ = Config.Z;
		State.Hp = Config.Hp;
		State.MaxHp = Config.Hp;
		State.Phase = EDuelPhase::Waiting;
		State.Timer = 0.0;
		State.MoveIndex = 0;
		State.Move = EDuelMove::Sweep;
		State.AttackYaw = 0.0;
		State.Yaw = kPi;
		State.Windup = 0.0;
		State.Recovery = 0.0;
		State.Cool = 0.0;
		State.AttackU = -1.0;
		State.Going = 0.0;
		State.HitDone = false;
		State.Connected = false;
		State.Beat = 0;
		State.React = 1.7;
		State.ReactSide = 1;
		State.Evaded = false;
		State.Punishes = 0;
		State.Guard = 2;
		State.Ready = false;
		State.Dead = false;
		return State;
	}

	EDuelEvent StepDuel(DuelState& State, double Dt, const DuelStepInput& Input)
	{
		State.Ready = Input.Ready;
		State.Going = 0.0;
		if (!Input.Ready)
		{
			State.Phase = EDuelPhase::Waiting;
			State.AttackU = -1.0;
			return EDuelEvent::None;
		}

		const double Dx = Input.X - State.X;
		const double Dz = Input.Z - State.Z;
		const double Distance = std::hypot(Dx, Dz);

		State.Timer = std::max(0.0, State.Timer - Dt);
		State.React = std::max(0.0, State.React - Dt);

		const DuelMoveSpec& Spec = GetDuelMoveSpec(State.Move);
		State.Windup = (State.Phase == EDuelPhase::Windup) ? State.Timer : 0.0;
		State.Recovery = (State.Phase == EDuelPhase::Recovery) ? State.Timer : 0.0;

		if (State.Phase == EDuelPhase::Waiting)
		{
			if (Distance < 9.0)
			{
				State.Phase = EDuelPhase::Intro;
				State.Timer = 1.4;
				return EDuelEvent::Intro;
			}
			return EDuelEvent::None;
		}

		// Leaving the duel resets health and lesson counters while keeping the cleared crew cleared.
		if (Distance > 17.0)
		{
			State = CreateDuel(State.Seg);
			State.Ready = true;
			return EDuelEvent::Reset;
		}

		if (State.Phase == EDuelPhase::Intro)
		{
			if (State.Timer == 0.0)
			{
				State.Phase = EDuelPhase::Approach;
			}
			return EDuelEvent::None;
		}

		auto Prepare = [&](EDuelMove MoveKind) -> EDuelEvent {
			State.Move = MoveKind;
			State.Phase = EDuelPhase::Windup;
			State.Timer = GetDuelMoveSpec(MoveKind).Windup;
			State.HitDone = false;
			State.Connected = false;
			State.Beat = 0;
			State.Guard = 2;
			State.AttackYaw = State.Yaw;
			return EDuelEvent::Windup;
		};

		if (State.Phase == EDuelPhase::Approach)
		{
			State.AttackU = -1.0;
			State.Yaw = std::atan2(Dx, Dz);
			if (State.Seg >= 2 && State.React == 0.0 && Input.Threatening && Distance < 3.45)
			{
				State.Phase = (State.Seg == 2 || State.Seg == 4) ? EDuelPhase::Sidestep : EDuelPhase::Guard;
				State.Timer = (State.Phase == EDuelPhase::Sidestep) ? 0.32 : 0.5;
				State.AttackYaw = State.Yaw;
				State.React = 4.2;
				State.ReactSide *= -1;
				return EDuelEvent::None;
			}
			if (Distance > 2.5)
			{
				const double Speed = State.Seg > 1 ? 3.5 : 2.5;
				const double StepDist = std::min(Speed * Dt, Distance - 2.5);
				if (Input.MoveFn)
				{
					Input.MoveFn((Dx / Distance) * StepDist, (Dz / Distance) * StepDist);
				}
				State.Going = Speed;
			}
			else
			{
				const std::vector<EDuelMove>& Moves = GetDuelConfigs()[static_cast<std::size_t>(State.Seg)].Moves;
				const EDuelMove Chosen = Moves[static_cast<std::size_t>(State.MoveIndex++ % static_cast<int>(Moves.size()))];
				return Prepare(Chosen);
			}
		}
		else if (State.Phase == EDuelPhase::Guard || State.Phase == EDuelPhase::Sidestep)
		{
			State.Yaw = std::atan2(Dx, Dz);
			if (State.Phase == EDuelPhase::Sidestep && Input.MoveFn)
			{
				const double Side = static_cast<double>(State.ReactSide);
				Input.MoveFn(
					(std::cos(State.AttackYaw) * Side - std::sin(State.AttackYaw) * 0.25) * Dt * 5.0,
					(-std::sin(State.AttackYaw) * Side - std::cos(State.AttackYaw) * 0.25) * Dt * 5.0);
			}
			if (State.Timer == 0.0)
			{
				return Prepare(State.Phase == EDuelPhase::Guard ? EDuelMove::Reply : EDuelMove::Reverse);
			}
		}
		else if (State.Phase == EDuelPhase::Windup)
		{
			const double P = 1.0 - State.Timer / Spec.Windup;
			if (P < (State.Move == EDuelMove::Feint ? 0.70 : 0.5))
			{
				State.AttackYaw = std::atan2(Dx, Dz);
			}
			State.Yaw = State.AttackYaw;
			State.AttackU = (State.Move == EDuelMove::Feint)
				? (0.24 * std::sin(kPi * std::min(P / 0.67, 1.0)) + 0.28 * std::max(0.0, (P - 0.67) / 0.33))
				: (0.28 * P);
			if (State.Timer == 0.0)
			{
				State.Phase = EDuelPhase::Active;
				State.Timer = Spec.Active;
				return EDuelEvent::Strike;
			}
		}
		else if (State.Phase == EDuelPhase::Active)
		{
			const double P = 1.0 - State.Timer / Spec.Active;
			const int Beat = (Spec.SecondPose.has_value() && P >= 0.51) ? 1 : 0;
			if (Beat != State.Beat)
			{
				State.Beat = Beat;
				State.HitDone = false;
			}
			State.AttackU = Spec.SecondPose.has_value()
				? (Beat != 0 ? (0.23 + (P - 0.51) * 0.98) : (0.3 + P * 0.84))
				: (0.3 + P * 0.43);
			if (Spec.Dash > 0.0 && Input.MoveFn)
			{
				Input.MoveFn(std::sin(State.AttackYaw) * Dt * Spec.Dash, std::cos(State.AttackYaw) * Dt * Spec.Dash);
			}
			if (State.Timer == 0.0)
			{
				if (!State.Connected && Distance < 8.0)
				{
					State.Evaded = true;
				}
				State.Phase = EDuelPhase::Recovery;
				State.Timer = Spec.Recover;
				return EDuelEvent::Opening;
			}
		}
		else if (State.Phase == EDuelPhase::Recovery)
		{
			State.AttackU = 0.76 + 0.24 * (1.0 - State.Timer / std::max(Spec.Recover, State.Timer));
			if (State.Timer == 0.0)
			{
				State.Phase = EDuelPhase::Approach;
				State.AttackU = -1.0;
			}
		}

		return EDuelEvent::None;
	}

	bool DuelHit(const DuelState& State, double X, double Y, double Z, double Floor)
	{
		const DuelMoveSpec& Move = GetDuelMoveSpec(State.Move);
		const double Dx = X - State.X;
		const double Dz = Z - State.Z;
		const double MaxHeight = (Move.Low || State.Move == EDuelMove::Sweep) ? 0.8 : 1.8;
		if (State.Phase != EDuelPhase::Active
			|| State.HitDone
			|| State.AttackU < 0.36
			|| State.AttackU > 0.67
			|| std::hypot(Dx, Dz) > Move.Reach
			|| (Y - Floor) > MaxHeight)
		{
			return false;
		}
		return std::abs(WrapAngle(std::atan2(Dx, Dz) - State.AttackYaw)) <= Move.Arc;
	}

	int DamageRival(DuelState& State, double Amount, std::string_view Source)
	{
		if (Source == "flare")
		{
			State.Phase = EDuelPhase::Recovery;
			State.Timer = 2.2;
		}
		if (State.Phase != EDuelPhase::Recovery)
		{
			if ((Source == "heavy" || Source == "whirl") && State.Seg >= 3 && --State.Guard <= 0)
			{
				State.Phase = EDuelPhase::Recovery;
				State.Timer = 1.8;
			}
			else
			{
				return std::max(1, static_cast<int>(std::floor(Amount * 0.08 + 0.5)));
			}
		}
		++State.Punishes;
		return static_cast<int>(std::floor(Amount * 1.25 + 0.5));
	}

	DuelVisualState GetDuelVisual(const DuelState& State)
	{
		const DuelMoveSpec& Spec = GetDuelMoveSpec(State.Move);
		const bool Engaged = State.Ready && !State.Dead && State.Phase != EDuelPhase::Waiting;

		if (State.Phase == EDuelPhase::Recovery)
		{
			const double Remain = Clamp(State.Timer / std::max(Spec.Recover, 0.001), 0.0, 1.0);
			return {
				Engaged,
				EDuelVisualMode::Opening,
				{0.0, 0.96, 0.82},
				{0.62, 1.0, 0.93},
				1.0 - Remain,
				Spec.Reach,
				kPi,
				2.4,
			};
		}
		if (State.Phase == EDuelPhase::Windup || State.Phase == EDuelPhase::Active)
		{
			const double Progress = (State.Phase == EDuelPhase::Active)
				? 1.0
				: Clamp(1.0 - State.Timer / std::max(Spec.Windup, 0.001), 0.0, 1.0);
			return {
				Engaged,
				EDuelVisualMode::Danger,
				Spec.Unblockable ? RGB{1.0, 0.41, 0.14} : RGB{1.0, 0.165, 0.427},
				Spec.Unblockable ? RGB{1.0, 0.82, 0.56} : RGB{1.0, 0.76, 0.84},
				Progress,
				Spec.Reach,
				Spec.Arc,
				State.Phase == EDuelPhase::Active ? 5.4 : (2.6 + Progress * 2.2),
			};
		}
		if (State.Phase == EDuelPhase::Guard)
		{
			return {
				Engaged,
				EDuelVisualMode::Stance,
				{1.0, 0.68, 0.22},
				{1.0, 0.88, 0.64},
				0.0,
				1.75,
				1.15,
				1.6,
			};
		}
		return {
			Engaged,
			EDuelVisualMode::Stance,
			{1.0, 0.22, 0.45},
			{1.0, 0.62, 0.42},
			0.0,
			Spec.Reach,
			Spec.Arc,
			1.1,
		};
	}
}
