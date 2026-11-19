// Copyright Komorebi Project. Online city constants, mech/race/ring rules, unlockable titles/colors, and room sanitization.
#include "Game/OnlineMeta.h"

namespace KomorebiCore
{
	int OnlineStats::Get(std::string_view StatName) const
	{
		if (StatName == "contracts") return Contracts;
		if (StatName == "mechs") return Mechs;
		if (StatName == "races") return Races;
		if (StatName == "duels") return Duels;
		if (StatName == "revives") return Revives;
		return 0;
	}

	const std::vector<OnlineTitle>& GetOnlineTitles()
	{
		static const std::vector<OnlineTitle> Titles = {
			{"", "", 0},
			{"Street Breaker", "contracts", 1},
			{"Mech Hunter", "mechs", 1},
			{"Road Ghost", "races", 1},
			{"Duelist", "duels", 1},
			{"Medic", "revives", 1},
			{"Night Runner", "races", 5},
			{"Ring Champion", "duels", 5},
		};
		return Titles;
	}

	const std::vector<OnlineColor>& GetOnlineColors()
	{
		static const std::vector<OnlineColor> Colors = {
			{"#00f0ff", "", 0, "Cyan"},
			{"#ff2a6d", "contracts", 3, "Pink"},
			{"#fcee0a", "mechs", 2, "Yellow"},
			{"#9dff6a", "races", 3, "Lime"},
			{"#c9a8ff", "duels", 3, "Violet"},
			{"#ffffff", "revives", 3, "White"},
			{"#ff9f43", "contracts", 8, "Orange"},
			{"#5ef2e0", "mechs", 5, "Teal"},
		};
		return Colors;
	}

	bool IsOnlineUnlockEarned(const OnlineStats& Stats, std::string_view Stat, int RequiredCount)
	{
		if (Stat.empty())
		{
			return true;
		}
		return Stats.Get(Stat) >= RequiredCount;
	}

	bool IsOnlineUnlockEarned(const OnlineStats& Stats, const OnlineTitle& Title)
	{
		return IsOnlineUnlockEarned(Stats, Title.Stat, Title.RequiredCount);
	}

	bool IsOnlineUnlockEarned(const OnlineStats& Stats, const OnlineColor& Color)
	{
		return IsOnlineUnlockEarned(Stats, Color.Stat, Color.RequiredCount);
	}

	bool IsOnlineUnlockEarned(
		const std::unordered_map<std::string, int>& Stats,
		std::string_view Stat,
		int RequiredCount)
	{
		if (Stat.empty())
		{
			return true;
		}
		const auto It = Stats.find(std::string(Stat));
		const int Value = (It != Stats.end()) ? It->second : 0;
		return Value >= RequiredCount;
	}

	std::string SanitizeRoomName(std::string_view Raw)
	{
		std::string Out;
		Out.reserve(std::min<std::size_t>(Raw.size(), 24));
		for (char Ch : Raw)
		{
			if (Out.size() >= 24)
			{
				break;
			}
			if (Ch >= 'A' && Ch <= 'Z')
			{
				Ch = static_cast<char>(Ch - 'A' + 'a');
			}
			if ((Ch >= 'a' && Ch <= 'z') || (Ch >= '0' && Ch <= '9') || Ch == '-')
			{
				Out.push_back(Ch);
			}
		}
		return Out;
	}
}
