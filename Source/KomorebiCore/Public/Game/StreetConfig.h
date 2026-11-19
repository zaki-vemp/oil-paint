// Copyright Komorebi Project. Street consequences tuning constants (guilt, cash, mob, witness, ram, heat).
#pragma once

#include "KomorebiCoreAPI.h"

#include <array>

namespace KomorebiCore
{
	/** Health a bruise on a citizen costs; killing one costs twice this. */
	inline constexpr int kGuilt = 15;

	struct CashConfig
	{
		int Thief = 25;
		int Enemy = 40;
	};

	struct MobConfig
	{
		double Radius = 34.0;
		double Time = 30.0;
	};

	struct WitnessConfig
	{
		double Radius = 34.0;
		double Time = 30.0;
		double Close = 12.0;
		double Pull = 2.4;
	};

	/** Below Stumble bystanders flinch, below Kill they are thrown and live, past Kill the bumper is fatal. */
	struct RamConfig
	{
		double Per = 5.0;
		double Max = 130.0;
		double Crew = 42.0;
		double Min = 4.0;
		double Stumble = 7.0;
		double Kill = 12.0;
	};

	/** Heat is 0..Max. Stars are thresholds for 1, 2, and 3; police arrive at 2, roadblock at 3. */
	struct HeatConfig
	{
		double Hurt = 8.0;
		double Kill = 22.0;
		double Carjack = 14.0;
		double Max = 100.0;
		std::array<double, 3> Stars = {12.0, 40.0, 70.0};
		double Decay = 3.5;
		double Safe = 10.0;
		int Cops = 2;
	};

	inline constexpr CashConfig kCashConfig{25, 40};
	inline constexpr MobConfig kMobConfig{34.0, 30.0};
	inline constexpr WitnessConfig kWitnessConfig{34.0, 30.0, 12.0, 2.4};
	inline constexpr RamConfig kRamConfig{5.0, 130.0, 42.0, 4.0, 7.0, 12.0};
	inline constexpr HeatConfig kHeatConfig{8.0, 22.0, 14.0, 100.0, {12.0, 40.0, 70.0}, 3.5, 10.0, 2};
}
