// Copyright Komorebi Project. Pure Slate Investor Showcase letterbox overlay with clickable shot dots and progress bars.
#include "UI/SKomorebiShowcaseOverlay.h"

#include "KomorebiGameTypes.h"
#include "Game/ShowcaseData.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Styling/CoreStyle.h"

namespace ShowcasePalette
{
	inline FLinearColor Hex(const TCHAR* Code, float Alpha = 1.0f)
	{
		FLinearColor C = FLinearColor::FromSRGBColor(FColor::FromHex(Code));
		C.A = Alpha;
		return C;
	}

	inline const FSlateBrush* WhiteBrush()
	{
		return FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	}

	inline const FButtonStyle& FlatButtonStyle()
	{
		static FButtonStyle Style = []()
		{
			FButtonStyle S;
			const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
			S.SetNormal(*White).SetHovered(*White).SetPressed(*White).SetDisabled(*White);
			S.Normal.TintColor = FSlateColor(FLinearColor::Transparent);
			S.Hovered.TintColor = FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.10f));
			S.Pressed.TintColor = FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.18f));
			S.Disabled.TintColor = FSlateColor(FLinearColor::Transparent);
			S.NormalPadding = FMargin(0.0f);
			S.PressedPadding = FMargin(0.0f);
			return S;
		}();
		return Style;
	}
}

SKomorebiShowcaseOverlay::~SKomorebiShowcaseOverlay() = default;

void SKomorebiShowcaseOverlay::Construct(const FArguments& InArgs)
{
	OnNextShot = InArgs._OnNextShot;
	OnExitTour = InArgs._OnExitTour;
	OnSelectShot = InArgs._OnSelectShot;

	const auto& Shots = KomorebiCore::GetShowcaseShots();
	TSharedRef<SHorizontalBox> DotsRow = SNew(SHorizontalBox);
	ShotDots.Reset();

	for (int32 i = 0; i < static_cast<int32>(Shots.size()); ++i)
	{
		FDotWidgets Dot;
		DotsRow->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(i == 0 ? 0.0f : 7.0f, 0.0f, 0.0f, 0.0f))
			[
				SNew(SButton)
				.ButtonStyle(&ShowcasePalette::FlatButtonStyle())
				.ToolTipText(FText::FromString(KomorebiBridge::ToFString(Shots[static_cast<size_t>(i)].Title)))
				.OnClicked_Lambda([this, i]() -> FReply
				{
					OnSelectShot.ExecuteIfBound(i);
					return FReply::Handled();
				})
				[
					SAssignNew(Dot.SizeBox, SBox)
					.WidthOverride(36.0f)
					.HeightOverride(9.0f)
					[
						SAssignNew(Dot.OuterBorder, SBorder)
						.BorderImage(ShowcasePalette::WhiteBrush())
						.BorderBackgroundColor(ShowcasePalette::Hex(TEXT("354957")))
						.Padding(1.0f)
						[
							SNew(SBorder)
							.BorderImage(ShowcasePalette::WhiteBrush())
							.BorderBackgroundColor(ShowcasePalette::Hex(TEXT("0D151D")))
							.Padding(0.0f)
							.HAlign(HAlign_Left)
							[
								SAssignNew(Dot.FillBox, SBox)
								.WidthOverride(0.0f)
								[
									SAssignNew(Dot.FillBorder, SBorder)
									.BorderImage(ShowcasePalette::WhiteBrush())
									.BorderBackgroundColor(ShowcasePalette::Hex(TEXT("00F0FF")))
								]
							]
						]
					]
				]
			];
		ShotDots.Add(Dot);
	}

	const FLinearColor DarkText = ShowcasePalette::Hex(TEXT("05070A"));
	const FLinearColor Yellow = ShowcasePalette::Hex(TEXT("FCEE0A"));
	const FLinearColor Ink = ShowcasePalette::Hex(TEXT("E9F7FA"));

	ChildSlot
	[
		SNew(SOverlay)

		// Top Letterbox Bar
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Top)
		[
			SNew(SBorder)
			.BorderImage(ShowcasePalette::WhiteBrush())
			.BorderBackgroundColor(ShowcasePalette::Hex(TEXT("04060A"), 0.88f))
			.Padding(FMargin(30.0f, 24.0f, 30.0f, 20.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.0f, 0.0f, 12.0f, 0.0f))
				[
					SAssignNew(BadgeBorder, SBorder)
					.BorderImage(ShowcasePalette::WhiteBrush())
					.BorderBackgroundColor(ShowcasePalette::Hex(TEXT("00F0FF")))
					.Padding(FMargin(10.0f, 4.0f))
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("◈ INVESTOR SHOWCASE")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						.ColorAndOpacity(DarkText)
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.0f, 0.0f, 12.0f, 0.0f))
				[
					SAssignNew(CounterText, STextBlock)
					.Text(FText::FromString(TEXT("01 / 06")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					.ColorAndOpacity(Ink)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SAssignNew(PhaseBorder, SBorder)
					.BorderImage(ShowcasePalette::WhiteBrush())
					.BorderBackgroundColor(ShowcasePalette::Hex(TEXT("00F0FF"), 0.55f))
					.Padding(1.0f)
					[
						SNew(SBorder)
						.BorderImage(ShowcasePalette::WhiteBrush())
						.BorderBackgroundColor(ShowcasePalette::Hex(TEXT("070B10"), 0.85f))
						.Padding(FMargin(8.0f, 3.0f))
						[
							SAssignNew(PhaseText, STextBlock)
							.Text(FText::FromString(TEXT("☾ NEON NIGHT")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
							.ColorAndOpacity(ShowcasePalette::Hex(TEXT("00F0FF")))
						]
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SSpacer)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					DotsRow
				]
			]
		]

		// Bottom Letterbox Bar
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Bottom)
		[
			SAssignNew(BottomAccentBorder, SBorder)
			.BorderImage(ShowcasePalette::WhiteBrush())
			.BorderBackgroundColor(ShowcasePalette::Hex(TEXT("00F0FF")))
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 3.0f))
			[
				SNew(SBorder)
				.BorderImage(ShowcasePalette::WhiteBrush())
				.BorderBackgroundColor(ShowcasePalette::Hex(TEXT("04060A"), 0.92f))
				.Padding(FMargin(30.0f, 22.0f, 30.0f, 24.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Bottom)
					.Padding(FMargin(0.0f, 0.0f, 24.0f, 0.0f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SAssignNew(KickerText, STextBlock)
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
							.ColorAndOpacity(ShowcasePalette::Hex(TEXT("00F0FF")))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0.0f, 5.0f, 0.0f, 6.0f))
						[
							SAssignNew(TitleText, STextBlock)
							.AutoWrapText(true)
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 24))
							.ColorAndOpacity(FLinearColor::White)
							.ShadowOffset(FVector2D(2.0f, 1.0f))
							.ShadowColorAndOpacity(ShowcasePalette::Hex(TEXT("FF2A6D"), 0.65f))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SAssignNew(SubtitleText, STextBlock)
							.AutoWrapText(true)
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
							.ColorAndOpacity(ShowcasePalette::Hex(TEXT("B7D2DC")))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Bottom)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(FMargin(0.0f, 0.0f, 10.0f, 0.0f))
						[
							SNew(SBorder)
							.BorderImage(ShowcasePalette::WhiteBrush())
							.BorderBackgroundColor(ShowcasePalette::Hex(TEXT("00F0FF"), 0.65f))
							.Padding(1.0f)
							[
								SNew(SBorder)
								.BorderImage(ShowcasePalette::WhiteBrush())
								.BorderBackgroundColor(ShowcasePalette::Hex(TEXT("0B1118"), 0.95f))
								.Padding(0.0f)
								[
									SNew(SButton)
									.ButtonStyle(&ShowcasePalette::FlatButtonStyle())
									.ContentPadding(FMargin(16.0f, 10.0f))
									.OnClicked_Lambda([this]() -> FReply
									{
										OnNextShot.ExecuteIfBound();
										return FReply::Handled();
									})
									[
										SNew(STextBlock)
										.Text(FText::FromString(TEXT("NEXT SHOT [SPACE]")))
										.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
										.ColorAndOpacity(Ink)
									]
								]
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SBorder)
							.BorderImage(ShowcasePalette::WhiteBrush())
							.BorderBackgroundColor(Yellow)
							.Padding(0.0f)
							[
								SNew(SButton)
								.ButtonStyle(&ShowcasePalette::FlatButtonStyle())
								.ContentPadding(FMargin(18.0f, 10.0f))
								.OnClicked_Lambda([this]() -> FReply
								{
									OnExitTour.ExecuteIfBound();
									return FReply::Handled();
								})
								[
									SNew(STextBlock)
									.Text(FText::FromString(TEXT("EXIT TOUR & PLAY [ESC]")))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
									.ColorAndOpacity(DarkText)
								]
							]
						]
					]
				]
			]
		]
	];

	UpdateShowcase(0, 0.0f);
}

void SKomorebiShowcaseOverlay::UpdateShowcase(int32 ShotIndex, float Progress01)
{
	const auto& Shots = KomorebiCore::GetShowcaseShots();
	const int32 Total = static_cast<int32>(Shots.size());
	if (Total == 0)
	{
		return;
	}

	const int32 ClampedIdx = ((ShotIndex % Total) + Total) % Total;
	const KomorebiCore::ShowcaseShot& Shot = Shots[static_cast<size_t>(ClampedIdx)];
	const FLinearColor Accent = FLinearColor::FromSRGBColor(FColor::FromHex(KomorebiBridge::ToFString(Shot.AccentHex)));

	if (BadgeBorder.IsValid()) BadgeBorder->SetBorderBackgroundColor(Accent);
	if (PhaseBorder.IsValid()) PhaseBorder->SetBorderBackgroundColor(Accent.CopyWithNewOpacity(0.55f));
	if (BottomAccentBorder.IsValid()) BottomAccentBorder->SetBorderBackgroundColor(Accent);

	if (CounterText.IsValid())
	{
		CounterText->SetText(FText::FromString(FString::Printf(TEXT("%02d / %02d"), ClampedIdx + 1, Total)));
	}
	if (PhaseText.IsValid())
	{
		const bool bDay = (Shot.Phase == KomorebiCore::EDayPhase::Day);
		PhaseText->SetText(FText::FromString(bDay ? TEXT("☀ DAYLIGHT PREVIEW") : TEXT("☾ NEON NIGHT")));
		PhaseText->SetColorAndOpacity(Accent);
	}
	if (KickerText.IsValid())
	{
		KickerText->SetText(FText::FromString(KomorebiBridge::ToFString(Shot.Kicker).ToUpper()));
		KickerText->SetColorAndOpacity(Accent);
	}
	if (TitleText.IsValid())
	{
		TitleText->SetText(FText::FromString(KomorebiBridge::ToFString(Shot.Title).ToUpper()));
	}
	if (SubtitleText.IsValid())
	{
		SubtitleText->SetText(FText::FromString(KomorebiBridge::ToFString(Shot.Subtitle)));
	}

	const float ClampedProg = FMath::Clamp(Progress01, 0.0f, 1.0f);
	for (int32 i = 0; i < ShotDots.Num(); ++i)
	{
		const FDotWidgets& Dot = ShotDots[i];
		const bool bActive = (i == ClampedIdx);
		const bool bDone = (i < ClampedIdx);
		const float DotWidth = bActive ? 54.0f : 34.0f;

		if (Dot.SizeBox.IsValid()) Dot.SizeBox->SetWidthOverride(DotWidth);
		if (Dot.OuterBorder.IsValid())
		{
			Dot.OuterBorder->SetBorderBackgroundColor(bActive
				? Accent
				: (bDone ? Accent.CopyWithNewOpacity(0.70f) : ShowcasePalette::Hex(TEXT("354957"))));
		}
		if (Dot.FillBorder.IsValid()) Dot.FillBorder->SetBorderBackgroundColor(Accent);
		if (Dot.FillBox.IsValid())
		{
			const float FillFraction = bDone ? 1.0f : (bActive ? ClampedProg : 0.0f);
			Dot.FillBox->SetWidthOverride(FMath::Max(0.0f, (DotWidth - 2.0f) * FillFraction));
		}
	}
}
