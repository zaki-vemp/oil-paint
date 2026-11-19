// Copyright Komorebi Project. Ten-minute day/night cycle and stateful phase controller.
#pragma once

#include "KomorebiCoreAPI.h"

#include <functional>
#include <optional>
#include <string>

namespace KomorebiCore
{
	inline constexpr double kPhaseSeconds = 600.0;
	inline constexpr double kTransitionSeconds = 12.0;

	enum class EDayPhase : uint8_t
	{
		Day = 0,
		Night = 1,
	};

	inline const char* DayPhaseName(EDayPhase Phase) noexcept
	{
		return Phase == EDayPhase::Night ? "night" : "day";
	}

	struct DayNightState
	{
		EDayPhase Phase = EDayPhase::Day;
		double Night = 0.0;
		double Twilight = 0.0;
		double Remaining = kPhaseSeconds;
		std::string ClockMMSS = "10:00";
		int64_t Slot = 0;
	};

	/** Formats remaining seconds as MM:SS using ceil, matching legacy-web/src/game/daynight.js. */
	KOMOREBICORE_API std::string FormatClock(double Seconds);

	/** Evaluates the ten-minute day/night cycle at `ElapsedSeconds`, starting in day (or night if `bStartNight`). */
	KOMOREBICORE_API DayNightState DayNightAt(double ElapsedSeconds, bool bStartNight = false);

	/** Stateful day/night controller supporting solo simulation time, online wall clock, manual phase overrides, and tour previews. */
	class KOMOREBICORE_API DayNightController
	{
	public:
		using PhaseShiftCallback = std::function<void(EDayPhase, const DayNightState&)>;

		explicit DayNightController(
			EDayPhase StartPhase = EDayPhase::Day,
			bool bInOnline = false,
			PhaseShiftCallback InOnShift = nullptr);

		const DayNightState& Update(double WorldTime = 0.0);
		const DayNightState& SetPhase(EDayPhase Want, double WorldTime = 0.0);
		const DayNightState& PreviewPhase(std::optional<EDayPhase> Phase, double WorldTime = 0.0);
		const DayNightState& Advance(double Seconds, double WorldTime = 0.0);

		void SetOnPhaseShift(PhaseShiftCallback Callback);

		static constexpr double CycleSeconds() noexcept { return kPhaseSeconds; }
		const DayNightState& GetState() const noexcept { return State; }
		EDayPhase GetPhase() const noexcept { return State.Phase; }
		double GetNight() const noexcept { return State.Night; }
		double GetTwilight() const noexcept { return State.Twilight; }
		double GetRemaining() const noexcept { return State.Remaining; }
		const std::string& GetClockMMSS() const noexcept { return State.ClockMMSS; }

	private:
		double ClockSeconds(double WorldTime) const;

		bool bStartNight = false;
		bool bOnline = false;
		double Offset = 0.0;
		std::optional<double> ManualBase;
		std::optional<EDayPhase> Preview;
		int64_t LastSlot = 0;
		bool bInitialized = false;
		DayNightState State;
		PhaseShiftCallback OnShift;
	};
}
