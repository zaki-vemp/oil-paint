// Copyright Komorebi Project. Pure Slate Cyberpunk HUD with live OnPaint Vector Minimap, cell bars, comms equalizer, and 3D markers.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "UI/KomorebiHUD.h"

class STextBlock;
class SVerticalBox;
class SHorizontalBox;
class SBorder;
class SKomorebiMinimapWidget;
class SKomorebiCellBarWidget;
class SKomorebiCommsEqualizerWidget;
class SKomorebiScreenMarkersLayer;

DECLARE_DELEGATE(FKomorebiOnHUDDismissComms);

/**
 * Pure Slate Cyberpunk Heads-Up Display styled after legacy-web/src/ui.css.
 * Renders Quest Plate, live OnPaint Vector Minimap, Vitals + 3 Spark pips, Target Lock + Boss Duel bar,
 * 6-Slot Abilities bar, Voiced Radio Comms + 7-bar equalizer, Combo counter, Heat Stars,
 * Vehicle Speedometer + G prompt, Online player count pill, Chapter Banner, Flash Text, and Toast.
 */
class KOMOREBIGAME_API SKomorebiHUD : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKomorebiHUD) {}
		SLATE_EVENT(FKomorebiOnHUDDismissComms, OnDismissComms)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SKomorebiHUD() override;

	/** Pushes the latest HUD snapshot and updates dynamic labels, colors, bars, and visibilities. */
	void RefreshFromViewData(const FKomorebiHUDViewData& Data);

private:
	TSharedRef<SWidget> BuildHeaderBar();
	TSharedRef<SWidget> BuildQuestPlate();
	TSharedRef<SWidget> BuildMinimapColumn();
	TSharedRef<SWidget> BuildTargetAndBossStack();
	TSharedRef<SWidget> BuildRadioCommsPlate();
	TSharedRef<SWidget> BuildVitalsAndVehicleStack();
	TSharedRef<SWidget> BuildAbilitiesBar();
	TSharedRef<SWidget> BuildCenterOverlays();

	FKomorebiHUDViewData CachedData;
	FKomorebiOnHUDDismissComms OnDismissComms;

	// Dynamic widgets updated on RefreshFromViewData
	TSharedPtr<SKomorebiScreenMarkersLayer> MarkersLayer;
	TSharedPtr<SKomorebiMinimapWidget> MinimapWidget;
	TSharedPtr<SKomorebiCellBarWidget> PlayerHpBar;
	TSharedPtr<SKomorebiCellBarWidget> SparkPipsBar;
	TSharedPtr<SKomorebiCellBarWidget> TargetHpBar;
	TSharedPtr<SKomorebiCellBarWidget> BossHpBar;
	TSharedPtr<SKomorebiCommsEqualizerWidget> CommsEqualizer;

	// Accent borders that track chapter neon
	TSharedPtr<SBorder> QuestOuterBorder;
	TSharedPtr<SBorder> QuestStripeBorder;
	TSharedPtr<SBorder> MinimapOuterBorder;
	TSharedPtr<SBorder> VitalsOuterBorder;
	TSharedPtr<SBorder> CommsOuterBorder;
	TSharedPtr<SBorder> CommsPortraitBorder;
	TSharedPtr<SBorder> TargetOuterBorder;
	TSharedPtr<SBorder> BossOuterBorder;
	TSharedPtr<SBorder> DamageVignetteBorder;

	// Header & Online pill
	TSharedPtr<SBorder> OnlinePillBox;
	TSharedPtr<STextBlock> OnlinePillText;

	// Quest Plate
	TSharedPtr<STextBlock> SectorLabelText;
	TSharedPtr<STextBlock> ChapterTitleText;
	TSharedPtr<STextBlock> ObjectiveText;
	TSharedPtr<SVerticalBox> ChecklistBox;
	TSharedPtr<STextBlock> QuestTipText;
	TSharedPtr<STextBlock> HostileCountText;
	TSharedPtr<STextBlock> DistanceText;
	TSharedPtr<STextBlock> CashText;
	TArray<TSharedPtr<SBorder>> JourneyPips;
	FString LastChecklistKey;

	// Minimap labels
	TSharedPtr<STextBlock> MapZoneText;
	TSharedPtr<STextBlock> DayClockText;
	TSharedPtr<SBorder> EchoBadgeBox;
	TSharedPtr<STextBlock> EchoBadgeText;

	// Vitals
	TSharedPtr<STextBlock> HpNumberText;

	// Vehicle
	TSharedPtr<SWidget> VehicleSpeedBox;
	TSharedPtr<STextBlock> VehicleKmhText;
	TSharedPtr<STextBlock> VehicleNameText;
	TSharedPtr<STextBlock> VehicleStatusText;
	TSharedPtr<SWidget> VehiclePromptBox;
	TSharedPtr<STextBlock> VehiclePromptText;

	// Heat Stars
	TSharedPtr<SWidget> HeatBannerBox;
	TSharedPtr<STextBlock> HeatStarsText;

	// Target Lock
	TSharedPtr<SWidget> TargetPanelBox;
	TSharedPtr<STextBlock> TargetNameText;
	TSharedPtr<STextBlock> TargetSubText;
	TSharedPtr<STextBlock> TargetTellText;

	// Boss Duel Protocol
	TSharedPtr<SWidget> BossPanelBox;
	TSharedPtr<STextBlock> BossNameText;
	TSharedPtr<STextBlock> BossHpText;
	TSharedPtr<STextBlock> BossCueText;
	TSharedPtr<STextBlock> BossLessonText;
	TSharedPtr<STextBlock> BossStepReadText;
	TSharedPtr<STextBlock> BossStepEvadeText;
	TSharedPtr<STextBlock> BossStepPunishText;

	// Combat Guard/Counter Status
	TSharedPtr<SWidget> CombatStatusBox;
	TSharedPtr<STextBlock> CombatStatusText;
	TSharedPtr<SKomorebiCellBarWidget> GuardMeterBar;

	// Radio Comms
	TSharedPtr<SWidget> CommsPanelBox;
	TSharedPtr<STextBlock> CommsMonogramText;
	TSharedPtr<STextBlock> CommsSpeakerText;
	TSharedPtr<SBorder> CommsRoleBadge;
	TSharedPtr<STextBlock> CommsRoleText;
	TSharedPtr<STextBlock> CommsLineText;
	TSharedPtr<STextBlock> CommsFreqText;

	// Abilities Bar (6 slots: Strike, Spark, Dash, Heavy, Flare, Whirl)
	struct FAbilitySlotWidgets
	{
		TSharedPtr<SBorder> OuterBorder;
		TSharedPtr<SBorder> KeyBadge;
		TSharedPtr<STextBlock> KeyText;
		TSharedPtr<STextBlock> NameText;
		TSharedPtr<STextBlock> StatusText;
	};
	TSharedPtr<SWidget> AbilitiesContainer;
	TArray<FAbilitySlotWidgets> AbilitySlots;

	// Combo, Flash, Toast, Chapter Banner
	TSharedPtr<SWidget> ComboBox;
	TSharedPtr<STextBlock> ComboCountText;
	TSharedPtr<SWidget> FlashBox;
	TSharedPtr<STextBlock> FlashTextWidget;
	TSharedPtr<SWidget> ToastBox;
	TSharedPtr<STextBlock> ToastTextWidget;
	TSharedPtr<SWidget> ChapterBannerBox;
	TSharedPtr<STextBlock> BannerSectorText;
	TSharedPtr<STextBlock> BannerTitleText;
	TSharedPtr<STextBlock> BannerSubtitleText;
};
