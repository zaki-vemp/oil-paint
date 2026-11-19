// Copyright Komorebi Project. Pure Slate modal Pause Menu mirroring index.html #menu + 3-Tab Lore Codex.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "KomorebiGameTypes.h"
#include "Game/AudioTheory.h"
#include "Game/OnlineMeta.h"

class STextBlock;
class SBorder;
class SVerticalBox;
class SWidgetSwitcher;
class SEditableTextBox;
class SSlider;

template <typename OptionType>
class SComboBox;

struct FKomorebiDropdownOption
{
	int32 Index = 0;
	FString Value;
	FString Label;
	bool bUnlocked = true;
	FLinearColor SwatchColor = FLinearColor(0.0f, 0.94f, 1.0f, 1.0f);
};

struct FKomorebiPauseMenuState
{
	EKomorebiWorldScene Scene = EKomorebiWorldScene::NeoKyoto;
	EKomorebiPlayMode Mode = EKomorebiPlayMode::StorySectors;
	int32 ClearedSectors = 0;
	bool bShowcaseActive = false;
	bool bIsNight = false;
	bool bFullscreen = false;

	// Online City panel
	FString Nickname = TEXT("Wanderer");
	FString EquippedTitle;
	int32 EquippedColorIndex = 0;
	KomorebiCore::OnlineStats OnlineStats;
	FString OnlineStatusText = TEXT("Offline · ready to connect");
	FString CurrentRoomCode;

	// Audio mix
	KomorebiCore::AudioSettings Audio = KomorebiCore::DefaultAudioSettings();

	// Graphics
	EKomorebiQualityTier QualityTier = EKomorebiQualityTier::Auto;
	bool bComicInk = false;
	bool bOilPaintImpasto = false;

	// Codex
	TSet<FString> UnlockedEchoIds;
};

DECLARE_DELEGATE(FKomorebiMenuVoidDelegate);
DECLARE_DELEGATE_OneParam(FKomorebiMenuModeDelegate, EKomorebiPlayMode);
DECLARE_DELEGATE_OneParam(FKomorebiMenuSceneDelegate, EKomorebiWorldScene);
DECLARE_DELEGATE_ThreeParams(FKomorebiMenuProfileDelegate, const FString& /*Nickname*/, const FString& /*Title*/, int32 /*ColorIndex*/);
DECLARE_DELEGATE_TwoParams(FKomorebiMenuHostDelegate, bool /*bPrivate*/, const FString& /*RoomCode*/);
DECLARE_DELEGATE_OneParam(FKomorebiMenuJoinIPDelegate, const FString& /*Address*/);
DECLARE_DELEGATE_OneParam(FKomorebiMenuAudioDelegate, const KomorebiCore::AudioSettings&);
DECLARE_DELEGATE_OneParam(FKomorebiMenuQualityDelegate, EKomorebiQualityTier);
DECLARE_DELEGATE_TwoParams(FKomorebiMenuRenderStyleDelegate, bool /*bComicInk*/, bool /*bImpasto*/);
DECLARE_DELEGATE_TwoParams(FKomorebiMenuEchoVoiceDelegate, const FString& /*SpeakerId*/, const FString& /*Quote*/);

/**
 * Pure Slate Cyberpunk Pause Menu mirroring legacy-web/index.html #menu:
 * - Resume, Fullscreen/Windowed toggle, Launch/Stop Investor Tour, Shift Day/Night
 * - Mode switcher (Story Sectors / Free Roam / Online City) & World Travel (Neo-Kyoto / Snowbound)
 * - Online City panel (Nickname, unlocked-gated Title & Color dropdowns, Host Listen Server / Quick Match / Join IP / Copy Private Room Code)
 * - Audio mixer sliders (Master, Music, Voice, SFX, Mute)
 * - Graphics Quality selector (Auto / High / Balanced / Low) + Comic Ink & Oil-Paint Impasto toggles
 * - Expandable 3-Tab Story & Memory Codex (6-Act Story, 5 Cast Dossiers, 12 Echo Shards with Play Voice replay)
 */
class KOMOREBIGAME_API SKomorebiPauseMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKomorebiPauseMenu) {}
		SLATE_EVENT(FKomorebiMenuVoidDelegate, OnResume)
		SLATE_EVENT(FKomorebiMenuVoidDelegate, OnToggleFullscreen)
		SLATE_EVENT(FKomorebiMenuVoidDelegate, OnToggleShowcase)
		SLATE_EVENT(FKomorebiMenuVoidDelegate, OnToggleDayNight)
		SLATE_EVENT(FKomorebiMenuModeDelegate, OnSelectMode)
		SLATE_EVENT(FKomorebiMenuSceneDelegate, OnSelectScene)
		SLATE_EVENT(FKomorebiMenuProfileDelegate, OnProfileChanged)
		SLATE_EVENT(FKomorebiMenuHostDelegate, OnHostListenServer)
		SLATE_EVENT(FKomorebiMenuVoidDelegate, OnQuickMatch)
		SLATE_EVENT(FKomorebiMenuJoinIPDelegate, OnJoinByAddress)
		SLATE_EVENT(FKomorebiMenuVoidDelegate, OnCopyRoomCode)
		SLATE_EVENT(FKomorebiMenuAudioDelegate, OnAudioSettingsChanged)
		SLATE_EVENT(FKomorebiMenuQualityDelegate, OnQualityTierChanged)
		SLATE_EVENT(FKomorebiMenuRenderStyleDelegate, OnRenderStyleChanged)
		SLATE_EVENT(FKomorebiMenuEchoVoiceDelegate, OnPlayEchoVoice)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SKomorebiPauseMenu() override;

	/** Rebuilds dynamic status pills, unlocked dropdown options, audio sliders, and 3-Tab Lore Codex. */
	void RefreshState(const FKomorebiPauseMenuState& NewState);

private:
	TSharedRef<SWidget> BuildHeaderSection();
	TSharedRef<SWidget> BuildShowcaseAndAtmosphereSection();
	TSharedRef<SWidget> BuildModeAndTravelSection();
	TSharedRef<SWidget> BuildOnlineCityPanel();
	TSharedRef<SWidget> BuildAudioMixerSection();
	TSharedRef<SWidget> BuildGraphicsSection();
	TSharedRef<SWidget> BuildCodexSection();
	TSharedRef<SWidget> BuildControlsGuideSection();

	TSharedRef<SWidget> MakeSectionButton(
		const FString& Label,
		TSharedPtr<SBorder>& OutBg,
		TSharedPtr<STextBlock>& OutText,
		FOnClicked OnClicked);

	TSharedRef<SWidget> MakeAudioSliderRow(
		const FString& Label,
		TSharedPtr<SSlider>& OutSlider,
		TSharedPtr<STextBlock>& OutPctText,
		TFunction<void(float)> OnChanged);

	void RebuildOnlineDropdownOptions();
	void RebuildCodexActsPane();
	void RebuildCodexCastPane();
	void RebuildCodexEchoesPane();
	void UpdateCodexTabVisuals();
	void EmitAudioChange();

	FKomorebiPauseMenuState State;
	bool bSuppressSliderCallbacks = false;
	bool bCodexExpanded = false;
	bool bControlsExpanded = false;
	int32 ActiveCodexTab = 0;

	// Delegates
	FKomorebiMenuVoidDelegate OnResume;
	FKomorebiMenuVoidDelegate OnToggleFullscreen;
	FKomorebiMenuVoidDelegate OnToggleShowcase;
	FKomorebiMenuVoidDelegate OnToggleDayNight;
	FKomorebiMenuModeDelegate OnSelectMode;
	FKomorebiMenuSceneDelegate OnSelectScene;
	FKomorebiMenuProfileDelegate OnProfileChanged;
	FKomorebiMenuHostDelegate OnHostListenServer;
	FKomorebiMenuVoidDelegate OnQuickMatch;
	FKomorebiMenuJoinIPDelegate OnJoinByAddress;
	FKomorebiMenuVoidDelegate OnCopyRoomCode;
	FKomorebiMenuAudioDelegate OnAudioSettingsChanged;
	FKomorebiMenuQualityDelegate OnQualityTierChanged;
	FKomorebiMenuRenderStyleDelegate OnRenderStyleChanged;
	FKomorebiMenuEchoVoiceDelegate OnPlayEchoVoice;

	// Top & Mode widgets
	TSharedPtr<STextBlock> FullscreenBtnText;
	TSharedPtr<STextBlock> TourBtnText;
	TSharedPtr<SWidget> DayNightBtnWrap;
	TSharedPtr<STextBlock> DayNightBtnText;

	TSharedPtr<SBorder> ModeStoryBg;
	TSharedPtr<STextBlock> ModeStoryText;
	TSharedPtr<SWidget> ModeRoamWrap;
	TSharedPtr<SBorder> ModeRoamBg;
	TSharedPtr<STextBlock> ModeRoamText;
	TSharedPtr<SBorder> ModeOnlineBg;
	TSharedPtr<STextBlock> ModeOnlineText;

	TSharedPtr<SBorder> SceneNeoKyotoBg;
	TSharedPtr<STextBlock> SceneNeoKyotoText;
	TSharedPtr<SBorder> SceneSnowboundBg;
	TSharedPtr<STextBlock> SceneSnowboundText;

	// Online panel widgets
	TSharedPtr<SEditableTextBox> NicknameInput;
	TSharedPtr<SEditableTextBox> AddressOrRoomInput;
	TSharedPtr<STextBlock> OnlineStatusLabel;
	TSharedPtr<STextBlock> RoomCodeDisplayText;

	TArray<TSharedPtr<FKomorebiDropdownOption>> TitleOptions;
	TSharedPtr<SComboBox<TSharedPtr<FKomorebiDropdownOption>>> TitleCombo;
	TSharedPtr<STextBlock> SelectedTitleText;

	TArray<TSharedPtr<FKomorebiDropdownOption>> ColorOptions;
	TSharedPtr<SComboBox<TSharedPtr<FKomorebiDropdownOption>>> ColorCombo;
	TSharedPtr<SBorder> SelectedColorSwatch;
	TSharedPtr<STextBlock> SelectedColorText;

	// Audio widgets
	TSharedPtr<SBorder> MuteBtnBg;
	TSharedPtr<STextBlock> MuteBtnText;
	TSharedPtr<SSlider> MasterSlider;
	TSharedPtr<STextBlock> MasterPctText;
	TSharedPtr<SSlider> MusicSlider;
	TSharedPtr<STextBlock> MusicPctText;
	TSharedPtr<SSlider> VoiceSlider;
	TSharedPtr<STextBlock> VoicePctText;
	TSharedPtr<SSlider> SfxSlider;
	TSharedPtr<STextBlock> SfxPctText;

	// Graphics widgets
	TArray<TSharedPtr<FKomorebiDropdownOption>> QualityOptions;
	TSharedPtr<SComboBox<TSharedPtr<FKomorebiDropdownOption>>> QualityCombo;
	TSharedPtr<STextBlock> SelectedQualityText;
	TSharedPtr<SBorder> ComicInkBtnBg;
	TSharedPtr<STextBlock> ComicInkBtnText;
	TSharedPtr<SBorder> ImpastoBtnBg;
	TSharedPtr<STextBlock> ImpastoBtnText;

	// 3-Tab Lore Codex widgets
	TSharedPtr<STextBlock> CodexToggleHeading;
	TSharedPtr<STextBlock> CodexEchoBadgeText;
	TSharedPtr<SWidget> CodexBodyContainer;
	TSharedPtr<SWidgetSwitcher> CodexTabSwitcher;
	TSharedPtr<SBorder> CodexTabBg[3];
	TSharedPtr<STextBlock> CodexTabText[3];
	TSharedPtr<SVerticalBox> CodexActsPane;
	TSharedPtr<SVerticalBox> CodexCastPane;
	TSharedPtr<SVerticalBox> CodexEchoesPane;

	// Controls Guide
	TSharedPtr<STextBlock> ControlsToggleText;
	TSharedPtr<SWidget> ControlsBodyContainer;
};
