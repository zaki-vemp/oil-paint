// Copyright Komorebi Project. Persistent UE5 GameInstance managing scene/mode selection, travel, local profile, and subsystem bridges.
#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "KomorebiGameTypes.h"
#include "Game/AudioTheory.h"
#include "Game/OnlineMeta.h"
#include "KomorebiGameInstance.generated.h"

class UOnlineSessionSubsystem;
class UMissionSubsystem;
class UKomorebiAudioSubsystem;
class UGraphicsSettingsSubsystem;
class UGameInstanceSubsystem;

DECLARE_MULTICAST_DELEGATE_OneParam(FKomorebiOnAudioMixChanged, const KomorebiCore::AudioSettings&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FKomorebiOnPlayVoiceRequested, const FString& /*SpeakerId*/, const FString& /*Line*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FKomorebiOnQualityTierChanged, EKomorebiQualityTier);
DECLARE_MULTICAST_DELEGATE_TwoParams(FKomorebiOnRenderStyleChanged, bool /*bComicInk*/, bool /*bOilPaintImpasto*/);
DECLARE_MULTICAST_DELEGATE(FKomorebiOnDayNightShiftRequested);
DECLARE_MULTICAST_DELEGATE_OneParam(FKomorebiOnShowcaseCommand, int32 /*Command: -1 stop, -2 toggle, -3 next, >=0 shot index*/);
DECLARE_MULTICAST_DELEGATE(FKomorebiOnProfileUpdated);

/**
 * Central persistent GameInstance for Komorebi: Protocol Zero-Seven.
 * Owns selected world scene (Neo-Kyoto / Snowbound), play mode (Story / Roam / Online),
 * pending online room/server travel parameters, local online identity & unlock stats,
 * audio/graphics preference cache, and cross-subsystem dispatch hooks.
 */
UCLASS()
class KOMOREBIGAME_API UKomorebiGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	UKomorebiGameInstance();

	virtual void Init() override;
	virtual void Shutdown() override;

	// --- Scene & Mode Selection ---

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Flow")
	EKomorebiWorldScene SelectedScene = EKomorebiWorldScene::NeoKyoto;

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Flow")
	EKomorebiPlayMode SelectedPlayMode = EKomorebiPlayMode::StorySectors;

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Network")
	FString PendingRoomCode;

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Network")
	FString PendingServerAddress;

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Network")
	bool bPendingPrivateRoom = false;

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Showcase")
	bool bStartWithShowcaseTour = false;

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Progress")
	bool bIgnoreSavedCheckpoint = false;

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Progress")
	int32 CachedClearedSectors = 0;

	// --- Local Online Profile & Unlock Persistence ---

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Online")
	FString LocalPlayerNickname;

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Online")
	FString LocalEquippedTitle;

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Online")
	int32 LocalEquippedColorIndex = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Graphics")
	EKomorebiQualityTier CachedQualityTier = EKomorebiQualityTier::Auto;

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Graphics")
	bool bComicInkEnabled = false;

	UPROPERTY(BlueprintReadWrite, Category = "Komorebi|Graphics")
	bool bOilPaintImpastoEnabled = false;

	/** Map asset path for a given scene ("/Game/Maps/L_NeoKyoto" or "/Game/Maps/L_Snowbound"). */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Flow")
	static FString GetMapPathForScene(EKomorebiWorldScene Scene);

	/** Online room pool identifier ("autumn" for Neo-Kyoto, "aurora" for Snowbound). */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Flow")
	static FString GetPoolNameForScene(EKomorebiWorldScene Scene);

	/**
	 * Travels to the requested scene and play mode.
	 * For OnlineCity, either connects directly to ServerAddress, joins/hosts via UOnlineSessionSubsystem,
	 * or opens a listen server with AKomorebiOnlineGameMode.
	 */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Flow")
	void TravelToSceneAndMode(
		EKomorebiWorldScene Scene,
		EKomorebiPlayMode Mode,
		const FString& RoomCode = TEXT(""),
		const FString& ServerAddress = TEXT(""),
		bool bPrivateRoom = false);

	/** Returns the native UE5 Online Session subsystem. */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Subsystems")
	UOnlineSessionSubsystem* GetOnlineSessionSubsystem() const;

	/** Resolves sibling subsystems cleanly by class name without hard header dependencies. */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Subsystems")
	UGameInstanceSubsystem* FindSiblingSubsystem(FName ClassName) const;

	/** Forward-declared typed accessors for sibling subsystems (null if not yet registered). */
	UKomorebiAudioSubsystem* GetKomorebiAudioSubsystem() const;
	UGraphicsSettingsSubsystem* GetGraphicsSettingsSubsystem() const;
	UMissionSubsystem* GetMissionSubsystem() const;

	// --- Local Profile, Online Stats, Echoes, Audio & Graphics ---

	const KomorebiCore::OnlineStats& GetLocalOnlineStats() const { return LocalOnlineStats; }
	void SetLocalOnlineStats(const KomorebiCore::OnlineStats& InStats);

	/**
	 * Increments a stat ("contracts", "mechs", "races", "duels", "revives"), saves locally,
	 * and returns any newly unlocked title or color notification strings.
	 */
	TArray<FString> EarnOnlineStat(const FString& StatName);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Online")
	void UpdateLocalOnlineProfile(const FString& InNickname, const FString& InTitle, int32 InColorIndex);

	const KomorebiCore::AudioSettings& GetAudioSettings() const { return CachedAudioSettings; }
	void ApplyAudioSettings(const KomorebiCore::AudioSettings& NewSettings);
	void RequestVoiceReplay(const FString& SpeakerId, const FString& Line);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Graphics")
	void ApplyQualityTier(EKomorebiQualityTier Tier);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Graphics")
	void ApplyRenderStyleToggles(bool bInComicInk, bool bInOilPaintImpasto);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Atmosphere")
	void RequestDayNightShift();

	/** Command: -1 stop, -2 toggle, -3 next shot, >=0 jump to shot index. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Showcase")
	void RequestShowcaseCommand(int32 Command);

	const TSet<FString>& GetUnlockedEchoIds() const { return UnlockedEchoIds; }
	void MarkEchoUnlocked(const FString& EchoId);
	void SetUnlockedEchoIds(const TSet<FString>& InEchoIds);

	void LoadLocalProfile();
	void SaveLocalProfile() const;

	// --- Cross-Subsystem Delegates ---
	FKomorebiOnAudioMixChanged OnAudioMixChanged;
	FKomorebiOnPlayVoiceRequested OnPlayVoiceRequested;
	FKomorebiOnQualityTierChanged OnQualityTierChanged;
	FKomorebiOnRenderStyleChanged OnRenderStyleChanged;
	FKomorebiOnDayNightShiftRequested OnDayNightShiftRequested;
	FKomorebiOnShowcaseCommand OnShowcaseCommand;
	FKomorebiOnProfileUpdated OnProfileUpdated;

private:
	void ParseLaunchCommandLine();
	void DispatchAudioSettingsToSibling();
	void DispatchGraphicsSettingsToSibling();

	KomorebiCore::OnlineStats LocalOnlineStats;
	KomorebiCore::AudioSettings CachedAudioSettings;
	TSet<FString> UnlockedEchoIds;
};
