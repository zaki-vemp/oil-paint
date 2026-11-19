// Copyright Komorebi Project. Online city constants, mech/race/ring rules, unlockable titles/colors, and room sanitization.
#pragma once

#include "KomorebiCoreAPI.h"
#include "Game/CombatRules.h"

#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace KomorebiCore
{
	inline constexpr int kMaxOnlinePlayers = 8;
	inline constexpr double kOnlineSendInterval = 0.1;
	inline constexpr double kReviveTime = 2.0;
	inline constexpr double kDownedTimeout = 15.0;
	inline constexpr double kReviveRadius = 2.4;
	inline constexpr std::string_view kOnlineSaveKey = "oilpaint.online.v1";

	struct MechEventConfig
	{
		double First = 75.0;
		double Every = 180.0;
		int Pay = 200;
	};

	struct RaceConfig
	{
		int Start = 30;
		std::array<int, 12> Gates = {42, 54, 66, 78, 90, 102, 114, 126, 138, 150, 162, 174};
		double Radius = 6.5;
		double LimitSeconds = 150.0;
	};

	struct DuelRingConfig
	{
		int At = 10;
		double Radius = 9.0;
		int CutDamage = 12;
		int BackhandDamage = 12;
		int ThrustDamage = 15;
		int HeavyDamage = 30;
		int SpinDamage = 18;
		int CounterDamage = 18;
		int AdvanceDamage = 14;

		int GetDamage(EAttackType Type) const
		{
			switch (Type)
			{
				case EAttackType::Cut: return CutDamage;
				case EAttackType::Backhand: return BackhandDamage;
				case EAttackType::Thrust: return ThrustDamage;
				case EAttackType::Heavy: return HeavyDamage;
				case EAttackType::Spin: return SpinDamage;
				case EAttackType::Counter: return CounterDamage;
				case EAttackType::Advance: return AdvanceDamage;
			}
			return CutDamage;
		}
	};

	inline constexpr MechEventConfig kMechEventConfig{75.0, 180.0, 200};
	inline constexpr RaceConfig kRaceConfig{};
	inline constexpr DuelRingConfig kDuelRingConfig{};

	struct OnlineTitle
	{
		std::string_view Label;
		std::string_view Stat; // Empty when free/unlocked by default
		int RequiredCount = 0;
	};

	struct OnlineColor
	{
		std::string_view Hex;
		std::string_view Stat; // Empty when free/unlocked by default
		int RequiredCount = 0;
		std::string_view DisplayName;
	};

	struct OnlineStats
	{
		int Contracts = 0;
		int Mechs = 0;
		int Races = 0;
		int Duels = 0;
		int Revives = 0;

		int Get(std::string_view StatName) const;
	};

	/** Returns all 8 online titles in unlock table order (index 0 is free). */
	KOMOREBICORE_API const std::vector<OnlineTitle>& GetOnlineTitles();

	/** Returns all 8 online name colors in unlock table order (index 0 is free). */
	KOMOREBICORE_API const std::vector<OnlineColor>& GetOnlineColors();

	/** Checks whether an unlock requirement (Stat, RequiredCount) is satisfied by OnlineStats. */
	KOMOREBICORE_API bool IsOnlineUnlockEarned(const OnlineStats& Stats, std::string_view Stat, int RequiredCount);

	/** Convenience overload for an OnlineTitle entry. */
	KOMOREBICORE_API bool IsOnlineUnlockEarned(const OnlineStats& Stats, const OnlineTitle& Title);

	/** Convenience overload for an OnlineColor entry. */
	KOMOREBICORE_API bool IsOnlineUnlockEarned(const OnlineStats& Stats, const OnlineColor& Color);

	/** Convenience overload accepting a dynamic stat map (e.g., deserialized JSON stats). */
	KOMOREBICORE_API bool IsOnlineUnlockEarned(
		const std::unordered_map<std::string, int>& Stats,
		std::string_view Stat,
		int RequiredCount);

	/** Normalizes a room name: lowercase ASCII [a-z0-9-] only, truncated to 24 characters. */
	KOMOREBICORE_API std::string SanitizeRoomName(std::string_view Raw);
}
