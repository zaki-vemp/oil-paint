// Copyright Komorebi Project. Pure Slate Investor Showcase letterbox overlay with clickable shot dots and progress bars.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;
class SBorder;
class SBox;

DECLARE_DELEGATE(FKomorebiOnShowcaseAction);
DECLARE_DELEGATE_OneParam(FKomorebiOnShowcaseSelectShot, int32 /*ShotIndex*/);

/**
 * Pure Slate Cinematic Investor Showcase overlay mirroring legacy-web/index.html #showcase-overlay.
 * Displays 01/06 counter, Day/Night phase badge, 6 clickable shot progress dots,
 * kicker/title/subtitle copy, Next Shot [Space], and Exit Tour [Esc].
 */
class KOMOREBIGAME_API SKomorebiShowcaseOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKomorebiShowcaseOverlay) {}
		SLATE_EVENT(FKomorebiOnShowcaseAction, OnNextShot)
		SLATE_EVENT(FKomorebiOnShowcaseAction, OnExitTour)
		SLATE_EVENT(FKomorebiOnShowcaseSelectShot, OnSelectShot)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SKomorebiShowcaseOverlay() override;

	/** Updates displayed shot metadata and progress fill bar [0..1] from KomorebiCore::GetShowcaseShots(). */
	void UpdateShowcase(int32 ShotIndex, float Progress01);

private:
	FKomorebiOnShowcaseAction OnNextShot;
	FKomorebiOnShowcaseAction OnExitTour;
	FKomorebiOnShowcaseSelectShot OnSelectShot;

	TSharedPtr<SBorder> BadgeBorder;
	TSharedPtr<STextBlock> CounterText;
	TSharedPtr<SBorder> PhaseBorder;
	TSharedPtr<STextBlock> PhaseText;
	TSharedPtr<SBorder> BottomAccentBorder;

	TSharedPtr<STextBlock> KickerText;
	TSharedPtr<STextBlock> TitleText;
	TSharedPtr<STextBlock> SubtitleText;

	struct FDotWidgets
	{
		TSharedPtr<SBox> SizeBox;
		TSharedPtr<SBorder> OuterBorder;
		TSharedPtr<SBox> FillBox;
		TSharedPtr<SBorder> FillBorder;
	};
	TArray<FDotWidgets> ShotDots;
};
