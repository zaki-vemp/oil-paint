// Copyright Komorebi Project. Complete story database for "Komorebi: Protocol Zero-Seven".
// Contains all voiced radio comms, multi-speaker comms chains, 6 story acts, 5 character dossiers,
// 12 collectible Memory Echo Shards, 6 sector field missions, 8 repeatable contract kiosks, and EXTRA_KEEP points.
#pragma once

#include "KomorebiCoreAPI.h"

#include <string_view>
#include <vector>

namespace KomorebiCore
{
	inline constexpr std::string_view kLoreSaveKey = "oilpaint.lore.v1";

	struct CommsEntry
	{
		std::string_view Key;
		std::string_view Speaker;
		std::string_view Line;
	};

	struct CommsChainStep
	{
		std::string_view Speaker;
		std::string_view Line;
		double Delay = 4.8;
	};

	struct CommsChain
	{
		std::string_view Key;
		std::vector<CommsChainStep> Steps;
	};

	struct StoryAct
	{
		int Act = 1;
		int Seg = 0;
		std::string_view District;
		std::string_view Title;
		std::string_view Epigraph;
		std::string_view Synopsis;
		std::string_view MemoryTheme;
	};

	struct CharacterDossier
	{
		std::string_view Id;
		std::string_view Name;
		std::string_view Role;
		std::string_view Affiliation;
		std::string_view ColorHex;
		std::string_view Quote;
		std::string_view Bio;
	};

	struct EchoShard
	{
		std::string_view Id;
		int Seg = 0;
		std::string_view Title;
		std::string_view Speaker;
		double X = 0.0;
		double Z = 0.0;
		std::string_view Quote;
		std::string_view Lore;
	};

	struct MissionTarget
	{
		double X = 0.0;
		double Z = 0.0;
		std::string_view Label;
		std::string_view Role;
	};

	struct SectorMission
	{
		int Seg = 0;
		std::string_view Type;
		std::string_view OpTitle;
		std::string_view StepLabel;
		std::string_view Tip;
		std::vector<MissionTarget> Targets;
	};

	struct KioskSpot
	{
		std::string_view Id;
		std::string_view Name;
		double X = 0.0;
		double Z = 0.0;
		std::string_view Kind;
		int DirX = 0;
		int DirZ = 0;
	};

	KOMOREBICORE_API const std::vector<CommsEntry>& GetComms();
	KOMOREBICORE_API const CommsEntry* FindComms(std::string_view Key);

	KOMOREBICORE_API const std::vector<CommsChain>& GetCommsChains();
	KOMOREBICORE_API const std::vector<CommsChainStep>* FindCommsChain(std::string_view Key);

	KOMOREBICORE_API const std::vector<StoryAct>& GetStoryActs();

	KOMOREBICORE_API const std::vector<CharacterDossier>& GetCharacterDossiers();
	KOMOREBICORE_API const CharacterDossier* FindCharacterDossier(std::string_view Id);

	KOMOREBICORE_API const std::vector<EchoShard>& GetEchoShards();
	KOMOREBICORE_API const EchoShard* FindEchoShard(std::string_view Id);

	KOMOREBICORE_API const std::vector<SectorMission>& GetSectorMissions();

	KOMOREBICORE_API const std::vector<KioskSpot>& GetKioskSpots();

	/** Returns all mission target, kiosk, and echo shard coordinates that scenery placement must keep clear of. */
	KOMOREBICORE_API const std::vector<Vec2>& GetExtraKeep();
}
