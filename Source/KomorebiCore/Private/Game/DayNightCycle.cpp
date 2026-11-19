// Copyright Komorebi Project. Ten-minute day/night cycle implementation matching legacy-web/src/game/daynight.js.
#include "Game/DayNightCycle.h"

#include <chrono>
#include <cstdio>
#include <utility>

namespace KomorebiCore
{
	std::string FormatClock(double Seconds)
	{
		const int64_t S = std::max<int64_t>(0, static_cast<int64_t>(std::ceil(Seconds)));
		const int64_t M = S / 60;
		const int64_t Rem = S % 60;
		char Buf[32];
		std::snprintf(Buf, sizeof(Buf), "%02lld:%02lld", static_cast<long long>(M), static_cast<long long>(Rem));
		return std::string(Buf);
	}

	DayNightState DayNightAt(double ElapsedSeconds, bool bStartNight)
	{
		const double Total = std::max(0.0, ElapsedSeconds);
		const int64_t Slot = static_cast<int64_t>(std::floor(Total / kPhaseSeconds));
		const double InSlot = Total - static_cast<double>(Slot) * kPhaseSeconds;
		const double Remaining = kPhaseSeconds - InSlot;

		const auto TargetOf = [bStartNight](int64_t K) -> double
		{
			return (((K % 2) == 1) != bStartNight) ? 1.0 : 0.0;
		};

		const bool bIsNight = TargetOf(Slot) == 1.0;
		const EDayPhase Phase = bIsNight ? EDayPhase::Night : EDayPhase::Day;

		const double Half = kTransitionSeconds * 0.5;
		double Night = TargetOf(Slot);
		if (InSlot > kPhaseSeconds - Half)
		{
			const double U = (InSlot - (kPhaseSeconds - Half)) / kTransitionSeconds;
			const double From = TargetOf(Slot);
			const double To = TargetOf(Slot + 1);
			Night = From + (To - From) * SmoothStep(0.0, 1.0, U);
		}
		else if (Slot > 0 && InSlot < Half)
		{
			const double U = 0.5 + InSlot / kTransitionSeconds;
			const double From = TargetOf(Slot - 1);
			const double To = TargetOf(Slot);
			Night = From + (To - From) * SmoothStep(0.0, 1.0, U);
		}

		const double Twilight = 4.0 * Night * (1.0 - Night);
		return DayNightState{ Phase, Night, Twilight, Remaining, FormatClock(Remaining), Slot };
	}

	DayNightController::DayNightController(EDayPhase StartPhase, bool bInOnline, PhaseShiftCallback InOnShift)
		: bStartNight(StartPhase == EDayPhase::Night)
		, bOnline(bInOnline)
		, State(DayNightAt(0.0, bStartNight))
		, OnShift(std::move(InOnShift))
	{
	}

	double DayNightController::ClockSeconds(double WorldTime) const
	{
		double Base = WorldTime;
		if (ManualBase.has_value())
		{
			Base = *ManualBase + WorldTime;
		}
		else if (bOnline)
		{
			const auto Now = std::chrono::system_clock::now().time_since_epoch();
			Base = std::chrono::duration<double>(Now).count();
		}
		return Base + Offset;
	}

	const DayNightState& DayNightController::Update(double WorldTime)
	{
		const DayNightState Live = DayNightAt(ClockSeconds(WorldTime), bStartNight);
		if (Preview.has_value())
		{
			State = Live;
			State.Phase = *Preview;
			State.Night = (*Preview == EDayPhase::Night) ? 1.0 : 0.0;
			State.Twilight = 0.0;
		}
		else
		{
			State = Live;
		}

		if (!bInitialized)
		{
			bInitialized = true;
			LastSlot = State.Slot;
		}
		else if (State.Slot != LastSlot)
		{
			LastSlot = State.Slot;
			if (!Preview.has_value() && OnShift)
			{
				OnShift(State.Phase, State);
			}
		}
		return State;
	}

	const DayNightState& DayNightController::SetPhase(EDayPhase Want, double WorldTime)
	{
		const bool bTargetNight = (Want == EDayPhase::Night);
		const int64_t Slot = (bTargetNight == bStartNight) ? 0 : 1;
		ManualBase = static_cast<double>(Slot) * kPhaseSeconds + kTransitionSeconds - WorldTime;
		Offset = 0.0;
		return Update(WorldTime);
	}

	const DayNightState& DayNightController::PreviewPhase(std::optional<EDayPhase> Phase, double WorldTime)
	{
		Preview = Phase;
		return Update(WorldTime);
	}

	const DayNightState& DayNightController::Advance(double Seconds, double WorldTime)
	{
		Offset += Seconds;
		return Update(WorldTime);
	}

	void DayNightController::SetOnPhaseShift(PhaseShiftCallback Callback)
	{
		OnShift = std::move(Callback);
	}
}
