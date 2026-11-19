// Copyright Komorebi Project. Pure Slate modal Pause Menu mirroring index.html #menu + 3-Tab Lore Codex.
#include "UI/SKomorebiPauseMenu.h"

#include "Game/ChaptersData.h"
#include "Game/StoryDatabase.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SComboBox.h"
#include "Styling/CoreStyle.h"

namespace PauseMenuStyle
{
	inline FLinearColor Hex(const TCHAR* Code, float Alpha = 1.0f)
	{
		FLinearColor C = FLinearColor::FromSRGBColor(FColor::FromHex(Code));
		C.A = Alpha;
		return C;
	}

	inline const FLinearColor Backdrop() { return Hex(TEXT("020406"), 0.85f); }
	inline const FLinearColor ModalBg() { return Hex(TEXT("080B10"), 0.96f); }
	inline const FLinearColor PlateBg() { return Hex(TEXT("0E141B"), 1.0f); }
	inline const FLinearColor CardBg() { return Hex(TEXT("0A1017"), 1.0f); }
	inline const FLinearColor Edge() { return Hex(TEXT("22313B"), 1.0f); }
	inline const FLinearColor Ink() { return Hex(TEXT("E9F7FA"), 1.0f); }
	inline const FLinearColor Muted() { return Hex(TEXT("8AA0AB"), 1.0f); }
	inline const FLinearColor Yellow() { return Hex(TEXT("FCEE0A"), 1.0f); }
	inline const FLinearColor Cyan() { return Hex(TEXT("00F0FF"), 1.0f); }
	inline const FLinearColor Red() { return Hex(TEXT("FF2A6D"), 1.0f); }
	inline const FLinearColor Mint() { return Hex(TEXT("19FFC6"), 1.0f); }
	inline const FLinearColor DarkText() { return Hex(TEXT("05070A"), 1.0f); }

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

	inline TSharedRef<SWidget> SectionHeading(const FString& Title)
	{
		return SNew(STextBlock)
			.Text(FText::FromString(Title.ToUpper()))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity(Cyan());
	}
}

SKomorebiPauseMenu::~SKomorebiPauseMenu() = default;

void SKomorebiPauseMenu::Construct(const FArguments& InArgs)
{
	OnResume = InArgs._OnResume;
	OnToggleFullscreen = InArgs._OnToggleFullscreen;
	OnToggleShowcase = InArgs._OnToggleShowcase;
	OnToggleDayNight = InArgs._OnToggleDayNight;
	OnSelectMode = InArgs._OnSelectMode;
	OnSelectScene = InArgs._OnSelectScene;
	OnProfileChanged = InArgs._OnProfileChanged;
	OnHostListenServer = InArgs._OnHostListenServer;
	OnQuickMatch = InArgs._OnQuickMatch;
	OnJoinByAddress = InArgs._OnJoinByAddress;
	OnCopyRoomCode = InArgs._OnCopyRoomCode;
	OnAudioSettingsChanged = InArgs._OnAudioSettingsChanged;
	OnQualityTierChanged = InArgs._OnQualityTierChanged;
	OnRenderStyleChanged = InArgs._OnRenderStyleChanged;
	OnPlayEchoVoice = InArgs._OnPlayEchoVoice;

	// Build static Quality options
	QualityOptions.Reset();
	{
		auto AddQ = [&](int32 Idx, const TCHAR* Label)
		{
			TSharedPtr<FKomorebiDropdownOption> Opt = MakeShared<FKomorebiDropdownOption>();
			Opt->Index = Idx;
			Opt->Label = Label;
			Opt->bUnlocked = true;
			QualityOptions.Add(Opt);
		};
		AddQ(static_cast<int32>(EKomorebiQualityTier::Auto), TEXT("Auto (recommended)"));
		AddQ(static_cast<int32>(EKomorebiQualityTier::High), TEXT("High"));
		AddQ(static_cast<int32>(EKomorebiQualityTier::Medium), TEXT("Balanced"));
		AddQ(static_cast<int32>(EKomorebiQualityTier::Low), TEXT("Low"));
	}

	RebuildOnlineDropdownOptions();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(PauseMenuStyle::WhiteBrush())
		.BorderBackgroundColor(PauseMenuStyle::Backdrop())
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Padding(FMargin(20.0f))
		[
			SNew(SBox)
			.WidthOverride(560.0f)
			.MaxDesiredHeight(780.0f)
			[
				SNew(SBorder)
				.BorderImage(PauseMenuStyle::WhiteBrush())
				.BorderBackgroundColor(PauseMenuStyle::Cyan())
				.Padding(1.5f)
				[
					SNew(SBorder)
					.BorderImage(PauseMenuStyle::WhiteBrush())
					.BorderBackgroundColor(PauseMenuStyle::ModalBg())
					.Padding(FMargin(24.0f, 20.0f))
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight() [ BuildHeaderSection() ]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 16.0f, 0.0f, 0.0f)) [ BuildShowcaseAndAtmosphereSection() ]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 14.0f, 0.0f, 0.0f)) [ BuildModeAndTravelSection() ]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 14.0f, 0.0f, 0.0f)) [ BuildOnlineCityPanel() ]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 14.0f, 0.0f, 0.0f)) [ BuildAudioMixerSection() ]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 14.0f, 0.0f, 0.0f)) [ BuildGraphicsSection() ]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 16.0f, 0.0f, 0.0f)) [ BuildCodexSection() ]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 14.0f, 0.0f, 0.0f)) [ BuildControlsGuideSection() ]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 14.0f, 0.0f, 0.0f))
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("SIX SECTORS. ONE WAY FORWARD.")))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
								.ColorAndOpacity(PauseMenuStyle::Muted())
							]
						]
					]
				]
			]
		]
	];

	RebuildCodexCastPane();
	RefreshState(FKomorebiPauseMenuState{});
}

TSharedRef<SWidget> SKomorebiPauseMenu::MakeSectionButton(
	const FString& Label,
	TSharedPtr<SBorder>& OutBg,
	TSharedPtr<STextBlock>& OutText,
	FOnClicked OnClicked)
{
	return SNew(SBorder)
		.BorderImage(PauseMenuStyle::WhiteBrush())
		.BorderBackgroundColor(PauseMenuStyle::Edge())
		.Padding(1.0f)
		[
			SAssignNew(OutBg, SBorder)
			.BorderImage(PauseMenuStyle::WhiteBrush())
			.BorderBackgroundColor(PauseMenuStyle::PlateBg())
			.Padding(0.0f)
			[
				SNew(SButton)
				.ButtonStyle(&PauseMenuStyle::FlatButtonStyle())
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.ContentPadding(FMargin(8.0f, 9.0f))
				.OnClicked(OnClicked)
				[
					SAssignNew(OutText, STextBlock)
					.Text(FText::FromString(Label.ToUpper()))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.ColorAndOpacity(PauseMenuStyle::Ink())
				]
			]
		];
}

TSharedRef<SWidget> SKomorebiPauseMenu::BuildHeaderSection()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("LINK PAUSED")))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 26))
			.ColorAndOpacity(PauseMenuStyle::Yellow())
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 4.0f, 0.0f, 14.0f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("> the trail holds until you are back")))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
			.ColorAndOpacity(PauseMenuStyle::Muted())
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(PauseMenuStyle::WhiteBrush())
			.BorderBackgroundColor(PauseMenuStyle::Yellow())
			.Padding(0.0f)
			[
				SNew(SButton)
				.ButtonStyle(&PauseMenuStyle::FlatButtonStyle())
				.ContentPadding(FMargin(16.0f, 12.0f))
				.OnClicked_Lambda([this]() -> FReply
				{
					OnResume.ExecuteIfBound();
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("RESUME RUN")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
						.ColorAndOpacity(PauseMenuStyle::DarkText())
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("→")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
						.ColorAndOpacity(PauseMenuStyle::DarkText())
					]
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
		[
			SNew(SBorder)
			.BorderImage(PauseMenuStyle::WhiteBrush())
			.BorderBackgroundColor(PauseMenuStyle::Edge())
			.Padding(1.0f)
			[
				SNew(SBorder)
				.BorderImage(PauseMenuStyle::WhiteBrush())
				.BorderBackgroundColor(PauseMenuStyle::PlateBg())
				.Padding(0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&PauseMenuStyle::FlatButtonStyle())
					.HAlign(HAlign_Center)
					.ContentPadding(FMargin(14.0f, 9.0f))
					.OnClicked_Lambda([this]() -> FReply
					{
						OnToggleFullscreen.ExecuteIfBound();
						return FReply::Handled();
					})
					[
						SAssignNew(FullscreenBtnText, STextBlock)
						.Text(FText::FromString(TEXT("FULL SCREEN")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
						.ColorAndOpacity(PauseMenuStyle::Ink())
					]
				]
			]
		];
}

TSharedRef<SWidget> SKomorebiPauseMenu::BuildShowcaseAndAtmosphereSection()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
		[
			PauseMenuStyle::SectionHeading(TEXT("Showcase & Atmosphere"))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SBorder)
				.BorderImage(PauseMenuStyle::WhiteBrush())
				.BorderBackgroundColor(PauseMenuStyle::Yellow().CopyWithNewOpacity(0.60f))
				.Padding(1.0f)
				[
					SNew(SBorder)
					.BorderImage(PauseMenuStyle::WhiteBrush())
					.BorderBackgroundColor(PauseMenuStyle::PlateBg())
					.Padding(0.0f)
					[
						SNew(SButton)
						.ButtonStyle(&PauseMenuStyle::FlatButtonStyle())
						.HAlign(HAlign_Center)
						.ContentPadding(FMargin(10.0f, 9.0f))
						.OnClicked_Lambda([this]() -> FReply
						{
							OnToggleShowcase.ExecuteIfBound();
							return FReply::Handled();
						})
						[
							SAssignNew(TourBtnText, STextBlock)
							.Text(FText::FromString(TEXT("◈ LAUNCH INVESTOR TOUR")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
							.ColorAndOpacity(PauseMenuStyle::Yellow())
						]
					]
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
			[
				SAssignNew(DayNightBtnWrap, SBorder)
				.BorderImage(PauseMenuStyle::WhiteBrush())
				.BorderBackgroundColor(PauseMenuStyle::Cyan().CopyWithNewOpacity(0.50f))
				.Padding(1.0f)
				[
					SNew(SBorder)
					.BorderImage(PauseMenuStyle::WhiteBrush())
					.BorderBackgroundColor(PauseMenuStyle::PlateBg())
					.Padding(0.0f)
					[
						SNew(SButton)
						.ButtonStyle(&PauseMenuStyle::FlatButtonStyle())
						.HAlign(HAlign_Center)
						.ContentPadding(FMargin(10.0f, 9.0f))
						.OnClicked_Lambda([this]() -> FReply
						{
							OnToggleDayNight.ExecuteIfBound();
							return FReply::Handled();
						})
						[
							SAssignNew(DayNightBtnText, STextBlock)
							.Text(FText::FromString(TEXT("☾ SHIFT TO MIDNIGHT (DAY)")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
							.ColorAndOpacity(PauseMenuStyle::Ink())
						]
					]
				]
			]
		];
}

TSharedRef<SWidget> SKomorebiPauseMenu::BuildModeAndTravelSection()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
		[
			PauseMenuStyle::SectionHeading(TEXT("Mode"))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				MakeSectionButton(TEXT("Story Sectors"), ModeStoryBg, ModeStoryText,
					FOnClicked::CreateLambda([this]() -> FReply
					{
						OnSelectMode.ExecuteIfBound(EKomorebiPlayMode::StorySectors);
						return FReply::Handled();
					}))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
			[
				SAssignNew(ModeRoamWrap, SBox)
				[
					MakeSectionButton(TEXT("Free Roam"), ModeRoamBg, ModeRoamText,
						FOnClicked::CreateLambda([this]() -> FReply
						{
							OnSelectMode.ExecuteIfBound(EKomorebiPlayMode::FreeRoam);
							return FReply::Handled();
						}))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
			[
				MakeSectionButton(TEXT("Online City"), ModeOnlineBg, ModeOnlineText,
					FOnClicked::CreateLambda([this]() -> FReply
					{
						OnSelectMode.ExecuteIfBound(EKomorebiPlayMode::OnlineCity);
						return FReply::Handled();
					}))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 12.0f, 0.0f, 6.0f))
		[
			PauseMenuStyle::SectionHeading(TEXT("Travel To"))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				MakeSectionButton(TEXT("Neo-Kyoto"), SceneNeoKyotoBg, SceneNeoKyotoText,
					FOnClicked::CreateLambda([this]() -> FReply
					{
						OnSelectScene.ExecuteIfBound(EKomorebiWorldScene::NeoKyoto);
						return FReply::Handled();
					}))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
			[
				MakeSectionButton(TEXT("Snowbound"), SceneSnowboundBg, SceneSnowboundText,
					FOnClicked::CreateLambda([this]() -> FReply
					{
						OnSelectScene.ExecuteIfBound(EKomorebiWorldScene::Snowbound);
						return FReply::Handled();
					}))
			]
		];
}

TSharedRef<SWidget> SKomorebiPauseMenu::BuildOnlineCityPanel()
{
	TSharedPtr<SBorder> DummyBg;
	TSharedPtr<STextBlock> DummyTxt;

	return SNew(SBorder)
		.BorderImage(PauseMenuStyle::WhiteBrush())
		.BorderBackgroundColor(PauseMenuStyle::Edge())
		.Padding(1.0f)
		[
			SNew(SBorder)
			.BorderImage(PauseMenuStyle::WhiteBrush())
			.BorderBackgroundColor(PauseMenuStyle::CardBg())
			.Padding(FMargin(12.0f, 10.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						PauseMenuStyle::SectionHeading(TEXT("Online City (8-Player Native UE5)"))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SAssignNew(RoomCodeDisplayText, STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
						.ColorAndOpacity(PauseMenuStyle::Yellow())
					]
				]

				// Nickname input
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 10.0f, 0.0f))
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("NICKNAME")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
						.ColorAndOpacity(PauseMenuStyle::Muted())
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SAssignNew(NicknameInput, SEditableTextBox)
						.HintText(FText::FromString(TEXT("Wanderer-1234 (max 16 chars)")))
						.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
						{
							const FString Clean = NewText.ToString().TrimStartAndEnd().Left(16);
							if (!Clean.IsEmpty())
							{
								State.Nickname = Clean;
								OnProfileChanged.ExecuteIfBound(State.Nickname, State.EquippedTitle, State.EquippedColorIndex);
							}
						})
					]
				]

				// Title & Color dropdowns
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 3.0f))
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("TITLE")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
							.ColorAndOpacity(PauseMenuStyle::Muted())
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SAssignNew(TitleCombo, SComboBox<TSharedPtr<FKomorebiDropdownOption>>)
							.OptionsSource(&TitleOptions)
							.OnGenerateWidget_Lambda([](TSharedPtr<FKomorebiDropdownOption> Opt) -> TSharedRef<SWidget>
							{
								const bool bOpen = Opt.IsValid() && Opt->bUnlocked;
								return SNew(SBox)
									.Padding(FMargin(6.0f, 4.0f))
									[
										SNew(STextBlock)
										.Text(FText::FromString(Opt.IsValid() ? Opt->Label : TEXT("")))
										.Font(FCoreStyle::GetDefaultFontStyle(bOpen ? "Bold" : "Regular", 9))
										.ColorAndOpacity(bOpen ? PauseMenuStyle::Ink() : PauseMenuStyle::Muted().CopyWithNewOpacity(0.55f))
									];
							})
							.OnSelectionChanged_Lambda([this](TSharedPtr<FKomorebiDropdownOption> Opt, ESelectInfo::Type SelectInfo)
							{
								if (SelectInfo == ESelectInfo::Direct || !Opt.IsValid() || !Opt->bUnlocked)
								{
									return;
								}
								State.EquippedTitle = Opt->Value;
								if (SelectedTitleText.IsValid())
								{
									SelectedTitleText->SetText(FText::FromString(Opt->Label));
								}
								OnProfileChanged.ExecuteIfBound(State.Nickname, State.EquippedTitle, State.EquippedColorIndex);
							})
							[
								SAssignNew(SelectedTitleText, STextBlock)
								.Text(FText::FromString(TEXT("No title")))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
								.ColorAndOpacity(PauseMenuStyle::Ink())
							]
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 3.0f))
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("NAME COLOUR")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
							.ColorAndOpacity(PauseMenuStyle::Muted())
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SAssignNew(ColorCombo, SComboBox<TSharedPtr<FKomorebiDropdownOption>>)
							.OptionsSource(&ColorOptions)
							.OnGenerateWidget_Lambda([](TSharedPtr<FKomorebiDropdownOption> Opt) -> TSharedRef<SWidget>
							{
								const bool bOpen = Opt.IsValid() && Opt->bUnlocked;
								const FLinearColor Swatch = Opt.IsValid() ? Opt->SwatchColor : PauseMenuStyle::Cyan();
								return SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(6.0f, 4.0f, 6.0f, 4.0f))
									[
										SNew(SBox).WidthOverride(10.0f).HeightOverride(10.0f)
										[
											SNew(SBorder)
											.BorderImage(PauseMenuStyle::WhiteBrush())
											.BorderBackgroundColor(Swatch.CopyWithNewOpacity(bOpen ? 1.0f : 0.35f))
										]
									]
									+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(FMargin(0.0f, 4.0f, 6.0f, 4.0f))
									[
										SNew(STextBlock)
										.Text(FText::FromString(Opt.IsValid() ? Opt->Label : TEXT("")))
										.Font(FCoreStyle::GetDefaultFontStyle(bOpen ? "Bold" : "Regular", 9))
										.ColorAndOpacity(bOpen ? PauseMenuStyle::Ink() : PauseMenuStyle::Muted().CopyWithNewOpacity(0.55f))
									];
							})
							.OnSelectionChanged_Lambda([this](TSharedPtr<FKomorebiDropdownOption> Opt, ESelectInfo::Type SelectInfo)
							{
								if (SelectInfo == ESelectInfo::Direct || !Opt.IsValid() || !Opt->bUnlocked)
								{
									return;
								}
								State.EquippedColorIndex = Opt->Index;
								if (SelectedColorText.IsValid()) SelectedColorText->SetText(FText::FromString(Opt->Label));
								if (SelectedColorSwatch.IsValid()) SelectedColorSwatch->SetBorderBackgroundColor(Opt->SwatchColor);
								OnProfileChanged.ExecuteIfBound(State.Nickname, State.EquippedTitle, State.EquippedColorIndex);
							})
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
								[
									SNew(SBox).WidthOverride(10.0f).HeightOverride(10.0f)
									[
										SAssignNew(SelectedColorSwatch, SBorder)
										.BorderImage(PauseMenuStyle::WhiteBrush())
										.BorderBackgroundColor(PauseMenuStyle::Cyan())
									]
								]
								+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
								[
									SAssignNew(SelectedColorText, STextBlock)
									.Text(FText::FromString(TEXT("Cyan")))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
									.ColorAndOpacity(PauseMenuStyle::Ink())
								]
							]
						]
					]
				]

				// Direct IP / Private Room Code input
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 2.0f, 0.0f, 6.0f))
				[
					SAssignNew(AddressOrRoomInput, SEditableTextBox)
					.HintText(FText::FromString(TEXT("Server IP (127.0.0.1:7777) or private room code…")))
				]

				// Status readout
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
				[
					SAssignNew(OnlineStatusLabel, STextBlock)
					.AutoWrapText(true)
					.Text(FText::FromString(TEXT("Offline · ready to connect")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity(PauseMenuStyle::Mint())
				]

				// Action buttons row 1: Host Listen Server & Quick Match
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						MakeSectionButton(TEXT("Host Listen Server"), DummyBg, DummyTxt,
							FOnClicked::CreateLambda([this]() -> FReply
							{
								const FString Raw = AddressOrRoomInput.IsValid() ? AddressOrRoomInput->GetText().ToString().TrimStartAndEnd() : TEXT("");
								const bool bPrivate = !Raw.IsEmpty() && !Raw.Contains(TEXT(".")) && !Raw.Contains(TEXT(":"));
								OnHostListenServer.ExecuteIfBound(bPrivate, bPrivate ? Raw : TEXT(""));
								return FReply::Handled();
							}))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
					[
						MakeSectionButton(TEXT("Quick Match"), DummyBg, DummyTxt,
							FOnClicked::CreateLambda([this]() -> FReply
							{
								OnQuickMatch.ExecuteIfBound();
								return FReply::Handled();
							}))
					]
				]

				// Action buttons row 2: Join IP & Copy Private Room Code
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 6.0f, 0.0f, 0.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						MakeSectionButton(TEXT("Join IP"), DummyBg, DummyTxt,
							FOnClicked::CreateLambda([this]() -> FReply
							{
								const FString Addr = AddressOrRoomInput.IsValid() ? AddressOrRoomInput->GetText().ToString().TrimStartAndEnd() : TEXT("");
								OnJoinByAddress.ExecuteIfBound(Addr.IsEmpty() ? TEXT("127.0.0.1:7777") : Addr);
								return FReply::Handled();
							}))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
					[
						MakeSectionButton(TEXT("Copy Private Room Code"), DummyBg, DummyTxt,
							FOnClicked::CreateLambda([this]() -> FReply
							{
								OnCopyRoomCode.ExecuteIfBound();
								return FReply::Handled();
							}))
					]
				]
			]
		];
}

TSharedRef<SWidget> SKomorebiPauseMenu::MakeAudioSliderRow(
	const FString& Label,
	TSharedPtr<SSlider>& OutSlider,
	TSharedPtr<STextBlock>& OutPctText,
	TFunction<void(float)> OnChanged)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(74.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Label.ToUpper()))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(PauseMenuStyle::Muted())
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 10.0f, 0.0f))
		[
			SNew(SBox).WidthOverride(38.0f)
			[
				SAssignNew(OutPctText, STextBlock)
				.Text(FText::FromString(TEXT("80%")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(PauseMenuStyle::Yellow())
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SAssignNew(OutSlider, SSlider)
			.MinValue(0.0f)
			.MaxValue(1.0f)
			.Value(0.8f)
			.SliderBarColor(PauseMenuStyle::Cyan())
			.SliderHandleColor(PauseMenuStyle::Yellow())
			.OnValueChanged_Lambda([this, OnChanged](float NewVal)
			{
				if (bSuppressSliderCallbacks)
				{
					return;
				}
				OnChanged(NewVal);
				EmitAudioChange();
			})
		];
}

TSharedRef<SWidget> SKomorebiPauseMenu::BuildAudioMixerSection()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				PauseMenuStyle::SectionHeading(TEXT("Audio Mixer"))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				MakeSectionButton(TEXT("Sound On"), MuteBtnBg, MuteBtnText,
					FOnClicked::CreateLambda([this]() -> FReply
					{
						State.Audio.bMuted = !State.Audio.bMuted;
						EmitAudioChange();
						return FReply::Handled();
					}))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 4.0f))
		[
			MakeAudioSliderRow(TEXT("Master"), MasterSlider, MasterPctText, [this](float V)
			{
				State.Audio.Master = V;
				if (V > 0.01f) State.Audio.bMuted = false;
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 4.0f))
		[
			MakeAudioSliderRow(TEXT("Music"), MusicSlider, MusicPctText, [this](float V) { State.Audio.Music = V; })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 4.0f))
		[
			MakeAudioSliderRow(TEXT("Voice"), VoiceSlider, VoicePctText, [this](float V) { State.Audio.Voice = V; })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 4.0f))
		[
			MakeAudioSliderRow(TEXT("SFX"), SfxSlider, SfxPctText, [this](float V) { State.Audio.Sfx = V; })
		];
}

TSharedRef<SWidget> SKomorebiPauseMenu::BuildGraphicsSection()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
		[
			PauseMenuStyle::SectionHeading(TEXT("Graphics & Stylization"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 12.0f, 0.0f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("QUALITY TIER")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(PauseMenuStyle::Muted())
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SAssignNew(QualityCombo, SComboBox<TSharedPtr<FKomorebiDropdownOption>>)
				.OptionsSource(&QualityOptions)
				.OnGenerateWidget_Lambda([](TSharedPtr<FKomorebiDropdownOption> Opt) -> TSharedRef<SWidget>
				{
					return SNew(SBox).Padding(FMargin(8.0f, 4.0f))
					[
						SNew(STextBlock)
						.Text(FText::FromString(Opt.IsValid() ? Opt->Label : TEXT("")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						.ColorAndOpacity(PauseMenuStyle::Ink())
					];
				})
				.OnSelectionChanged_Lambda([this](TSharedPtr<FKomorebiDropdownOption> Opt, ESelectInfo::Type SelectInfo)
				{
					if (SelectInfo == ESelectInfo::Direct || !Opt.IsValid())
					{
						return;
					}
					State.QualityTier = static_cast<EKomorebiQualityTier>(Opt->Index);
					if (SelectedQualityText.IsValid()) SelectedQualityText->SetText(FText::FromString(Opt->Label));
					OnQualityTierChanged.ExecuteIfBound(State.QualityTier);
				})
				[
					SAssignNew(SelectedQualityText, STextBlock)
					.Text(FText::FromString(TEXT("Auto (recommended)")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.ColorAndOpacity(PauseMenuStyle::Ink())
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				MakeSectionButton(TEXT("Comic Ink: OFF"), ComicInkBtnBg, ComicInkBtnText,
					FOnClicked::CreateLambda([this]() -> FReply
					{
						State.bComicInk = !State.bComicInk;
						OnRenderStyleChanged.ExecuteIfBound(State.bComicInk, State.bOilPaintImpasto);
						RefreshState(State);
						return FReply::Handled();
					}))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
			[
				MakeSectionButton(TEXT("Oil-Paint Impasto: OFF"), ImpastoBtnBg, ImpastoBtnText,
					FOnClicked::CreateLambda([this]() -> FReply
					{
						State.bOilPaintImpasto = !State.bOilPaintImpasto;
						OnRenderStyleChanged.ExecuteIfBound(State.bComicInk, State.bOilPaintImpasto);
						RefreshState(State);
						return FReply::Handled();
					}))
			]
		];
}

TSharedRef<SWidget> SKomorebiPauseMenu::BuildCodexSection()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(PauseMenuStyle::WhiteBrush())
			.BorderBackgroundColor(PauseMenuStyle::Mint().CopyWithNewOpacity(0.50f))
			.Padding(1.0f)
			[
				SNew(SBorder)
				.BorderImage(PauseMenuStyle::WhiteBrush())
				.BorderBackgroundColor(PauseMenuStyle::PlateBg())
				.Padding(0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&PauseMenuStyle::FlatButtonStyle())
					.ContentPadding(FMargin(12.0f, 10.0f))
					.OnClicked_Lambda([this]() -> FReply
					{
						bCodexExpanded = !bCodexExpanded;
						if (CodexBodyContainer.IsValid())
						{
							CodexBodyContainer->SetVisibility(bCodexExpanded ? EVisibility::Visible : EVisibility::Collapsed);
						}
						if (CodexToggleHeading.IsValid())
						{
							CodexToggleHeading->SetText(FText::FromString(bCodexExpanded
								? TEXT("▾ STORY & MEMORY CODEX (KOMOREBI PROTOCOL)")
								: TEXT("▸ STORY & MEMORY CODEX (KOMOREBI PROTOCOL)")));
						}
						if (bCodexExpanded)
						{
							RebuildCodexActsPane();
							RebuildCodexEchoesPane();
						}
						return FReply::Handled();
					})
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							SAssignNew(CodexToggleHeading, STextBlock)
							.Text(FText::FromString(TEXT("▸ STORY & MEMORY CODEX (KOMOREBI PROTOCOL)")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
							.ColorAndOpacity(PauseMenuStyle::Ink())
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SBorder)
							.BorderImage(PauseMenuStyle::WhiteBrush())
							.BorderBackgroundColor(PauseMenuStyle::Hex(TEXT("071917")))
							.Padding(FMargin(7.0f, 2.0f))
							[
								SAssignNew(CodexEchoBadgeText, STextBlock)
								.Text(FText::FromString(TEXT("◈ 0 / 12")))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
								.ColorAndOpacity(PauseMenuStyle::Mint())
							]
						]
					]
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 10.0f, 0.0f, 0.0f))
		[
			SAssignNew(CodexBodyContainer, SVerticalBox)
			.Visibility(EVisibility::Collapsed)
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					MakeSectionButton(TEXT("6-Act Story"), CodexTabBg[0], CodexTabText[0],
						FOnClicked::CreateLambda([this]() -> FReply
						{
							ActiveCodexTab = 0;
							UpdateCodexTabVisuals();
							return FReply::Handled();
						}))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(5.0f, 0.0f, 0.0f, 0.0f))
				[
					MakeSectionButton(TEXT("Cast Dossiers"), CodexTabBg[1], CodexTabText[1],
						FOnClicked::CreateLambda([this]() -> FReply
						{
							ActiveCodexTab = 1;
							UpdateCodexTabVisuals();
							return FReply::Handled();
						}))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(5.0f, 0.0f, 0.0f, 0.0f))
				[
					MakeSectionButton(TEXT("Echoes (0/12)"), CodexTabBg[2], CodexTabText[2],
						FOnClicked::CreateLambda([this]() -> FReply
						{
							ActiveCodexTab = 2;
							UpdateCodexTabVisuals();
							return FReply::Handled();
						}))
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.MaxDesiredHeight(320.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(CodexTabSwitcher, SWidgetSwitcher)
						+ SWidgetSwitcher::Slot() [ SAssignNew(CodexActsPane, SVerticalBox) ]
						+ SWidgetSwitcher::Slot() [ SAssignNew(CodexCastPane, SVerticalBox) ]
						+ SWidgetSwitcher::Slot() [ SAssignNew(CodexEchoesPane, SVerticalBox) ]
					]
				]
			]
		];
}

TSharedRef<SWidget> SKomorebiPauseMenu::BuildControlsGuideSection()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SButton)
			.ButtonStyle(&PauseMenuStyle::FlatButtonStyle())
			.ContentPadding(FMargin(0.0f, 6.0f))
			.OnClicked_Lambda([this]() -> FReply
			{
				bControlsExpanded = !bControlsExpanded;
				if (ControlsBodyContainer.IsValid())
				{
					ControlsBodyContainer->SetVisibility(bControlsExpanded ? EVisibility::Visible : EVisibility::Collapsed);
				}
				if (ControlsToggleText.IsValid())
				{
					ControlsToggleText->SetText(FText::FromString(bControlsExpanded
						? TEXT("▾ CONTROLS & MISSION GUIDE")
						: TEXT("▸ CONTROLS & MISSION GUIDE")));
				}
				return FReply::Handled();
			})
			[
				SAssignNew(ControlsToggleText, STextBlock)
				.Text(FText::FromString(TEXT("▸ CONTROLS & MISSION GUIDE")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.ColorAndOpacity(PauseMenuStyle::Ink())
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SAssignNew(ControlsBodyContainer, SBorder)
			.Visibility(EVisibility::Collapsed)
			.BorderImage(PauseMenuStyle::WhiteBrush())
			.BorderBackgroundColor(PauseMenuStyle::CardBg())
			.Padding(FMargin(10.0f, 8.0f))
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(FText::FromString(
					TEXT("WASD / Shift: Move / Sprint  |  Space: Jump / Handbrake\n")
					TEXT("J or LMB / F: Strike / Heavy  |  Q / E / R / C: Spark / Dash / Flare / Whirl\n")
					TEXT("T / Y: Lock / Next Target  |  X / Hold Z: Evade / Guard  |  G: Enter/Exit/Right Vehicle\n")
					TEXT("M: Toggle Story/Roam  |  K: Dismiss Comms  |  F8: Shift Day/Night  |  F9: Investor Tour")))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(PauseMenuStyle::Muted())
			]
		];
}

void SKomorebiPauseMenu::RebuildOnlineDropdownOptions()
{
	// Titles (8 entries from KomorebiCore::GetOnlineTitles())
	TitleOptions.Reset();
	const auto& Titles = KomorebiCore::GetOnlineTitles();
	for (int32 i = 0; i < static_cast<int32>(Titles.size()); ++i)
	{
		const KomorebiCore::OnlineTitle& Entry = Titles[static_cast<size_t>(i)];
		const bool bOpen = KomorebiCore::IsOnlineUnlockEarned(State.OnlineStats, Entry);
		const FString RawLabel = KomorebiBridge::ToFString(Entry.Label);
		const FString DisplayBase = RawLabel.IsEmpty() ? TEXT("No title") : RawLabel;

		TSharedPtr<FKomorebiDropdownOption> Opt = MakeShared<FKomorebiDropdownOption>();
		Opt->Index = i;
		Opt->Value = RawLabel;
		Opt->bUnlocked = bOpen;
		Opt->Label = bOpen
			? DisplayBase
			: FString::Printf(TEXT("%s · %d %s"), *DisplayBase, Entry.RequiredCount, *KomorebiBridge::ToFString(Entry.Stat));
		TitleOptions.Add(Opt);
	}
	if (TitleCombo.IsValid()) TitleCombo->RefreshOptions();

	// Colors (8 entries from KomorebiCore::GetOnlineColors())
	ColorOptions.Reset();
	const auto& Colors = KomorebiCore::GetOnlineColors();
	for (int32 i = 0; i < static_cast<int32>(Colors.size()); ++i)
	{
		const KomorebiCore::OnlineColor& Entry = Colors[static_cast<size_t>(i)];
		const bool bOpen = KomorebiCore::IsOnlineUnlockEarned(State.OnlineStats, Entry);
		const FString Name = KomorebiBridge::ToFString(Entry.DisplayName);

		TSharedPtr<FKomorebiDropdownOption> Opt = MakeShared<FKomorebiDropdownOption>();
		Opt->Index = i;
		Opt->Value = KomorebiBridge::ToFString(Entry.Hex);
		Opt->bUnlocked = bOpen;
		Opt->SwatchColor = FLinearColor::FromSRGBColor(FColor::FromHex(Opt->Value));
		Opt->Label = bOpen
			? Name
			: FString::Printf(TEXT("%s · %d %s"), *Name, Entry.RequiredCount, *KomorebiBridge::ToFString(Entry.Stat));
		ColorOptions.Add(Opt);
	}
	if (ColorCombo.IsValid()) ColorCombo->RefreshOptions();
}

void SKomorebiPauseMenu::RebuildCodexActsPane()
{
	if (!CodexActsPane.IsValid())
	{
		return;
	}
	CodexActsPane->ClearChildren();

	const auto& Acts = KomorebiCore::GetStoryActs();
	const auto& Chapters = KomorebiCore::GetChapters();
	const bool bRoamOrOnline = (State.Scene == EKomorebiWorldScene::NeoKyoto) && (State.Mode != EKomorebiPlayMode::StorySectors);

	for (const KomorebiCore::StoryAct& Act : Acts)
	{
		const FLinearColor Accent = (Act.Seg >= 0 && Act.Seg < static_cast<int32>(Chapters.size()))
			? FLinearColor::FromSRGBColor(FColor::FromHex(KomorebiBridge::ToFString(Chapters[static_cast<size_t>(Act.Seg)].ColorHex)))
			: PauseMenuStyle::Cyan();

		FString StatusLabel;
		FLinearColor StatusBg;
		FLinearColor StatusFg;
		if (bRoamOrOnline)
		{
			StatusLabel = TEXT("OPEN DISTRICT");
			StatusBg = PauseMenuStyle::Mint().CopyWithNewOpacity(0.18f);
			StatusFg = PauseMenuStyle::Mint();
		}
		else if (Act.Seg < State.ClearedSectors)
		{
			StatusLabel = TEXT("LIBERATED");
			StatusBg = PauseMenuStyle::Mint().CopyWithNewOpacity(0.18f);
			StatusFg = PauseMenuStyle::Mint();
		}
		else if (Act.Seg == State.ClearedSectors)
		{
			StatusLabel = TEXT("ACTIVE OP");
			StatusBg = PauseMenuStyle::Yellow().CopyWithNewOpacity(0.18f);
			StatusFg = PauseMenuStyle::Yellow();
		}
		else
		{
			StatusLabel = TEXT("QUARANTINED");
			StatusBg = PauseMenuStyle::Edge();
			StatusFg = PauseMenuStyle::Muted();
		}

		CodexActsPane->AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
			[
				SNew(SBorder)
				.BorderImage(PauseMenuStyle::WhiteBrush())
				.BorderBackgroundColor(Accent)
				.Padding(FMargin(3.0f, 0.0f, 0.0f, 0.0f))
				[
					SNew(SBorder)
					.BorderImage(PauseMenuStyle::WhiteBrush())
					.BorderBackgroundColor(PauseMenuStyle::CardBg())
					.Padding(FMargin(11.0f, 9.0f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::FromString(FString::Printf(TEXT("SECTOR %02d // %s"), Act.Act, *KomorebiBridge::ToFString(Act.District).ToUpper())))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
								.ColorAndOpacity(Accent)
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(SBorder)
								.BorderImage(PauseMenuStyle::WhiteBrush())
								.BorderBackgroundColor(StatusBg)
								.Padding(FMargin(6.0f, 2.0f))
								[
									SNew(STextBlock)
									.Text(FText::FromString(StatusLabel))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
									.ColorAndOpacity(StatusFg)
								]
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 4.0f))
						[
							SNew(STextBlock)
							.AutoWrapText(true)
							.Text(FText::FromString(KomorebiBridge::ToFString(Act.Title).ToUpper()))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
							.ColorAndOpacity(PauseMenuStyle::Ink())
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
						[
							SNew(STextBlock)
							.AutoWrapText(true)
							.Text(FText::FromString(KomorebiBridge::ToFString(Act.Epigraph)))
							.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
							.ColorAndOpacity(PauseMenuStyle::Hex(TEXT("CDE8EE")))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 5.0f))
						[
							SNew(STextBlock)
							.AutoWrapText(true)
							.Text(FText::FromString(KomorebiBridge::ToFString(Act.Synopsis)))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
							.ColorAndOpacity(PauseMenuStyle::Muted())
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.AutoWrapText(true)
							.Text(FText::FromString(FString::Printf(TEXT("◈ %s"), *KomorebiBridge::ToFString(Act.MemoryTheme))))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
							.ColorAndOpacity(PauseMenuStyle::Hex(TEXT("B9DCE4")))
						]
					]
				]
			];
	}
}

void SKomorebiPauseMenu::RebuildCodexCastPane()
{
	if (!CodexCastPane.IsValid())
	{
		return;
	}
	CodexCastPane->ClearChildren();

	for (const KomorebiCore::CharacterDossier& Dossier : KomorebiCore::GetCharacterDossiers())
	{
		const FLinearColor Accent = FLinearColor::FromSRGBColor(FColor::FromHex(KomorebiBridge::ToFString(Dossier.ColorHex)));
		const FString NameStr = KomorebiBridge::ToFString(Dossier.Name);
		const FString Monogram = NameStr.Left(2).ToUpper();

		CodexCastPane->AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
			[
				SNew(SBorder)
				.BorderImage(PauseMenuStyle::WhiteBrush())
				.BorderBackgroundColor(Accent)
				.Padding(FMargin(3.0f, 0.0f, 0.0f, 0.0f))
				[
					SNew(SBorder)
					.BorderImage(PauseMenuStyle::WhiteBrush())
					.BorderBackgroundColor(PauseMenuStyle::CardBg())
					.Padding(FMargin(11.0f, 9.0f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(FMargin(0.0f, 0.0f, 11.0f, 0.0f))
						[
							SNew(SBorder)
							.BorderImage(PauseMenuStyle::WhiteBrush())
							.BorderBackgroundColor(Accent)
							.Padding(1.0f)
							[
								SNew(SBox).WidthOverride(46.0f).HeightOverride(54.0f)
								[
									SNew(SBorder)
									.BorderImage(PauseMenuStyle::WhiteBrush())
									.BorderBackgroundColor(PauseMenuStyle::PlateBg())
									.HAlign(HAlign_Center)
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.Text(FText::FromString(Monogram))
										.Font(FCoreStyle::GetDefaultFontStyle("Bold", 15))
										.ColorAndOpacity(Accent)
									]
								]
							]
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(KomorebiBridge::ToFString(Dossier.Affiliation).ToUpper()))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
								.ColorAndOpacity(Accent)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 2.0f))
							[
								SNew(STextBlock)
								.Text(FText::FromString(NameStr.ToUpper()))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
								.ColorAndOpacity(PauseMenuStyle::Ink())
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
							[
								SNew(STextBlock)
								.Text(FText::FromString(KomorebiBridge::ToFString(Dossier.Role).ToUpper()))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
								.ColorAndOpacity(Accent)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
							[
								SNew(STextBlock)
								.AutoWrapText(true)
								.Text(FText::FromString(KomorebiBridge::ToFString(Dossier.Quote)))
								.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
								.ColorAndOpacity(PauseMenuStyle::Hex(TEXT("CDE8EE")))
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock)
								.AutoWrapText(true)
								.Text(FText::FromString(KomorebiBridge::ToFString(Dossier.Bio)))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
								.ColorAndOpacity(PauseMenuStyle::Muted())
							]
						]
					]
				]
			];
	}
}

void SKomorebiPauseMenu::RebuildCodexEchoesPane()
{
	if (!CodexEchoesPane.IsValid())
	{
		return;
	}
	CodexEchoesPane->ClearChildren();

	const auto& Shards = KomorebiCore::GetEchoShards();
	const auto& Chapters = KomorebiCore::GetChapters();
	const int32 Total = static_cast<int32>(Shards.size());
	int32 UnlockedCount = 0;
	for (const KomorebiCore::EchoShard& Shard : Shards)
	{
		if (State.UnlockedEchoIds.Contains(KomorebiBridge::ToFString(Shard.Id)))
		{
			++UnlockedCount;
		}
	}

	// Top summary meter (12 pips)
	TSharedRef<SHorizontalBox> MeterRow = SNew(SHorizontalBox);
	for (int32 i = 0; i < Total; ++i)
	{
		const bool bHas = State.UnlockedEchoIds.Contains(KomorebiBridge::ToFString(Shards[static_cast<size_t>(i)].Id));
		MeterRow->AddSlot()
			.FillWidth(1.0f)
			.Padding(FMargin(i == 0 ? 0.0f : 3.0f, 0.0f, 0.0f, 0.0f))
			[
				SNew(SBox).HeightOverride(5.0f)
				[
					SNew(SBorder)
					.BorderImage(PauseMenuStyle::WhiteBrush())
					.BorderBackgroundColor(bHas ? PauseMenuStyle::Mint() : PauseMenuStyle::Edge())
				]
			];
	}

	CodexEchoesPane->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
		[
			SNew(SBorder)
			.BorderImage(PauseMenuStyle::WhiteBrush())
			.BorderBackgroundColor(PauseMenuStyle::Mint().CopyWithNewOpacity(0.35f))
			.Padding(1.0f)
			[
				SNew(SBorder)
				.BorderImage(PauseMenuStyle::WhiteBrush())
				.BorderBackgroundColor(PauseMenuStyle::Hex(TEXT("071316")))
				.Padding(FMargin(10.0f, 8.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("RECOVERED MEMORY ECHOES")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
							.ColorAndOpacity(PauseMenuStyle::Mint())
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(STextBlock)
							.Text(FText::FromString(FString::Printf(TEXT("%d / %d"), UnlockedCount, Total)))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
							.ColorAndOpacity(PauseMenuStyle::Mint())
						]
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						MeterRow
					]
				]
			]
		];

	// 12 Echo Shard Cards
	for (int32 i = 0; i < Total; ++i)
	{
		const KomorebiCore::EchoShard& Shard = Shards[static_cast<size_t>(i)];
		const FString ShardId = KomorebiBridge::ToFString(Shard.Id);
		const FString District = (Shard.Seg >= 0 && Shard.Seg < static_cast<int32>(Chapters.size()))
			? KomorebiBridge::ToFString(Chapters[static_cast<size_t>(Shard.Seg)].Name)
			: FString::Printf(TEXT("Sector %d"), Shard.Seg + 1);
		const bool bOpen = State.UnlockedEchoIds.Contains(ShardId);

		if (!bOpen)
		{
			CodexEchoesPane->AddSlot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
				[
					SNew(SBorder)
					.BorderImage(PauseMenuStyle::WhiteBrush())
					.BorderBackgroundColor(PauseMenuStyle::Hex(TEXT("31424E")))
					.Padding(FMargin(3.0f, 0.0f, 0.0f, 0.0f))
					[
						SNew(SBorder)
						.BorderImage(PauseMenuStyle::WhiteBrush())
						.BorderBackgroundColor(PauseMenuStyle::CardBg().CopyWithNewOpacity(0.75f))
						.Padding(FMargin(11.0f, 9.0f))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f)
								[
									SNew(STextBlock)
									.Text(FText::FromString(FString::Printf(TEXT("ECHO %02d // %s"), i + 1, *District.ToUpper())))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
									.ColorAndOpacity(PauseMenuStyle::Muted())
								]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									SNew(STextBlock)
									.Text(FText::FromString(TEXT("ENCRYPTED")))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
									.ColorAndOpacity(PauseMenuStyle::Muted())
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 4.0f, 0.0f, 2.0f))
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("UNRECOVERED MEMORY SHARD")))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
								.ColorAndOpacity(PauseMenuStyle::Muted())
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock)
								.AutoWrapText(true)
								.Text(FText::FromString(FString::Printf(TEXT("Approach the cyan-and-gold Echo Shrine in %s to decrypt this Komorebi archive fragment."), *District)))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
								.ColorAndOpacity(PauseMenuStyle::Muted())
							]
						]
					]
				];
			continue;
		}

		const KomorebiCore::SpeakerProfile& Speaker = KomorebiCore::FindSpeaker(Shard.Speaker);
		const FLinearColor SpCol = FLinearColor::FromSRGBColor(FColor::FromHex(KomorebiBridge::ToFString(Speaker.ColorHex)));
		const FString SpeakerId = KomorebiBridge::ToFString(Shard.Speaker);
		const FString Quote = KomorebiBridge::ToFString(Shard.Quote);

		CodexEchoesPane->AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
			[
				SNew(SBorder)
				.BorderImage(PauseMenuStyle::WhiteBrush())
				.BorderBackgroundColor(SpCol)
				.Padding(FMargin(3.0f, 0.0f, 0.0f, 0.0f))
				[
					SNew(SBorder)
					.BorderImage(PauseMenuStyle::WhiteBrush())
					.BorderBackgroundColor(PauseMenuStyle::CardBg())
					.Padding(FMargin(11.0f, 9.0f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f)
							[
								SNew(STextBlock)
								.Text(FText::FromString(FString::Printf(TEXT("ECHO %02d // %s"), i + 1, *District.ToUpper())))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
								.ColorAndOpacity(SpCol)
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(SBorder)
								.BorderImage(PauseMenuStyle::WhiteBrush())
								.BorderBackgroundColor(PauseMenuStyle::Mint().CopyWithNewOpacity(0.18f))
								.Padding(FMargin(6.0f, 2.0f))
								[
									SNew(STextBlock)
									.Text(FText::FromString(TEXT("RECOVERED")))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
									.ColorAndOpacity(PauseMenuStyle::Mint())
								]
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 4.0f))
						[
							SNew(STextBlock)
							.AutoWrapText(true)
							.Text(FText::FromString(KomorebiBridge::ToFString(Shard.Title).ToUpper()))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
							.ColorAndOpacity(PauseMenuStyle::Ink())
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
						[
							SNew(STextBlock)
							.AutoWrapText(true)
							.Text(FText::FromString(FString::Printf(TEXT("“%s”"), *Quote)))
							.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
							.ColorAndOpacity(PauseMenuStyle::Hex(TEXT("CDE8EE")))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
						[
							SNew(STextBlock)
							.AutoWrapText(true)
							.Text(FText::FromString(KomorebiBridge::ToFString(Shard.Lore)))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
							.ColorAndOpacity(PauseMenuStyle::Muted())
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::FromString(KomorebiBridge::ToFString(Speaker.Name).ToUpper()))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
								.ColorAndOpacity(SpCol)
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(SBorder)
								.BorderImage(PauseMenuStyle::WhiteBrush())
								.BorderBackgroundColor(SpCol)
								.Padding(1.0f)
								[
									SNew(SBorder)
									.BorderImage(PauseMenuStyle::WhiteBrush())
									.BorderBackgroundColor(PauseMenuStyle::PlateBg())
									.Padding(0.0f)
									[
										SNew(SButton)
										.ButtonStyle(&PauseMenuStyle::FlatButtonStyle())
										.ContentPadding(FMargin(8.0f, 3.0f))
										.OnClicked_Lambda([this, SpeakerId, Quote]() -> FReply
										{
											OnPlayEchoVoice.ExecuteIfBound(SpeakerId, Quote);
											return FReply::Handled();
										})
										[
											SNew(STextBlock)
											.Text(FText::FromString(TEXT("▶ PLAY VOICE")))
											.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
											.ColorAndOpacity(SpCol)
										]
									]
								]
							]
						]
					]
				]
			];
	}
}

void SKomorebiPauseMenu::UpdateCodexTabVisuals()
{
	if (CodexTabSwitcher.IsValid())
	{
		CodexTabSwitcher->SetActiveWidgetIndex(ActiveCodexTab);
	}
	for (int32 i = 0; i < 3; ++i)
	{
		const bool bOn = (i == ActiveCodexTab);
		if (CodexTabBg[i].IsValid())
		{
			CodexTabBg[i]->SetBorderBackgroundColor(bOn ? PauseMenuStyle::Mint() : PauseMenuStyle::PlateBg());
		}
		if (CodexTabText[i].IsValid())
		{
			CodexTabText[i]->SetColorAndOpacity(bOn ? PauseMenuStyle::DarkText() : PauseMenuStyle::Muted());
		}
	}
}

void SKomorebiPauseMenu::EmitAudioChange()
{
	State.Audio = KomorebiCore::NormalizeAudioSettings(State.Audio);
	if (MasterPctText.IsValid()) MasterPctText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(State.Audio.Master * 100.0))));
	if (MusicPctText.IsValid()) MusicPctText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(State.Audio.Music * 100.0))));
	if (VoicePctText.IsValid()) VoicePctText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(State.Audio.Voice * 100.0))));
	if (SfxPctText.IsValid()) SfxPctText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(State.Audio.Sfx * 100.0))));
	if (MuteBtnBg.IsValid()) MuteBtnBg->SetBorderBackgroundColor(State.Audio.bMuted ? PauseMenuStyle::Red() : PauseMenuStyle::Cyan());
	if (MuteBtnText.IsValid())
	{
		MuteBtnText->SetText(FText::FromString(State.Audio.bMuted ? TEXT("MUTED (UNMUTE)") : TEXT("SOUND ON")));
		MuteBtnText->SetColorAndOpacity(PauseMenuStyle::DarkText());
	}
	OnAudioSettingsChanged.ExecuteIfBound(State.Audio);
}

void SKomorebiPauseMenu::RefreshState(const FKomorebiPauseMenuState& NewState)
{
	State = NewState;

	if (FullscreenBtnText.IsValid())
	{
		FullscreenBtnText->SetText(FText::FromString(State.bFullscreen ? TEXT("WINDOWED MODE") : TEXT("FULL SCREEN")));
	}
	if (TourBtnText.IsValid())
	{
		TourBtnText->SetText(FText::FromString(State.bShowcaseActive ? TEXT("◈ STOP INVESTOR TOUR") : TEXT("◈ LAUNCH INVESTOR TOUR")));
	}
	const bool bIsNeoKyoto = (State.Scene == EKomorebiWorldScene::NeoKyoto);
	if (DayNightBtnWrap.IsValid())
	{
		DayNightBtnWrap->SetVisibility(bIsNeoKyoto ? EVisibility::Visible : EVisibility::Collapsed);
	}
	if (DayNightBtnText.IsValid())
	{
		DayNightBtnText->SetText(FText::FromString(State.bIsNight
			? TEXT("☀ SHIFT TO DAYLIGHT (NIGHT)")
			: TEXT("☾ SHIFT TO MIDNIGHT (DAY)")));
	}
	if (ModeRoamWrap.IsValid())
	{
		ModeRoamWrap->SetVisibility(bIsNeoKyoto ? EVisibility::Visible : EVisibility::Collapsed);
	}

	auto PaintToggleBtn = [](TSharedPtr<SBorder>& Bg, TSharedPtr<STextBlock>& Txt, bool bActive)
	{
		if (Bg.IsValid()) Bg->SetBorderBackgroundColor(bActive ? PauseMenuStyle::Cyan() : PauseMenuStyle::PlateBg());
		if (Txt.IsValid()) Txt->SetColorAndOpacity(bActive ? PauseMenuStyle::DarkText() : PauseMenuStyle::Ink());
	};

	PaintToggleBtn(ModeStoryBg, ModeStoryText, State.Mode == EKomorebiPlayMode::StorySectors);
	PaintToggleBtn(ModeRoamBg, ModeRoamText, State.Mode == EKomorebiPlayMode::FreeRoam);
	PaintToggleBtn(ModeOnlineBg, ModeOnlineText, State.Mode == EKomorebiPlayMode::OnlineCity);
	if (ModeStoryText.IsValid()) ModeStoryText->SetText(FText::FromString(bIsNeoKyoto ? TEXT("STORY SECTORS") : TEXT("SOLO")));

	PaintToggleBtn(SceneNeoKyotoBg, SceneNeoKyotoText, bIsNeoKyoto);
	PaintToggleBtn(SceneSnowboundBg, SceneSnowboundText, !bIsNeoKyoto);

	// Online profile & dropdowns
	if (NicknameInput.IsValid())
	{
		NicknameInput->SetText(FText::FromString(State.Nickname));
	}
	if (OnlineStatusLabel.IsValid())
	{
		OnlineStatusLabel->SetText(FText::FromString(State.OnlineStatusText));
	}
	if (RoomCodeDisplayText.IsValid())
	{
		RoomCodeDisplayText->SetText(FText::FromString(State.CurrentRoomCode.IsEmpty()
			? TEXT("")
			: FString::Printf(TEXT("ROOM: %s"), *State.CurrentRoomCode.ToUpper())));
	}

	RebuildOnlineDropdownOptions();
	if (SelectedTitleText.IsValid())
	{
		SelectedTitleText->SetText(FText::FromString(State.EquippedTitle.IsEmpty() ? TEXT("No title") : State.EquippedTitle));
	}
	if (ColorOptions.IsValidIndex(State.EquippedColorIndex))
	{
		const TSharedPtr<FKomorebiDropdownOption>& ChosenColor = ColorOptions[State.EquippedColorIndex];
		if (SelectedColorText.IsValid()) SelectedColorText->SetText(FText::FromString(ChosenColor->Label));
		if (SelectedColorSwatch.IsValid()) SelectedColorSwatch->SetBorderBackgroundColor(ChosenColor->SwatchColor);
	}

	// Audio sliders
	bSuppressSliderCallbacks = true;
	if (MasterSlider.IsValid()) MasterSlider->SetValue(static_cast<float>(State.Audio.Master));
	if (MusicSlider.IsValid()) MusicSlider->SetValue(static_cast<float>(State.Audio.Music));
	if (VoiceSlider.IsValid()) VoiceSlider->SetValue(static_cast<float>(State.Audio.Voice));
	if (SfxSlider.IsValid()) SfxSlider->SetValue(static_cast<float>(State.Audio.Sfx));
	bSuppressSliderCallbacks = false;

	if (MasterPctText.IsValid()) MasterPctText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(State.Audio.Master * 100.0))));
	if (MusicPctText.IsValid()) MusicPctText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(State.Audio.Music * 100.0))));
	if (VoicePctText.IsValid()) VoicePctText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(State.Audio.Voice * 100.0))));
	if (SfxPctText.IsValid()) SfxPctText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(State.Audio.Sfx * 100.0))));
	if (MuteBtnBg.IsValid()) MuteBtnBg->SetBorderBackgroundColor(State.Audio.bMuted ? PauseMenuStyle::Red() : PauseMenuStyle::Cyan());
	if (MuteBtnText.IsValid())
	{
		MuteBtnText->SetText(FText::FromString(State.Audio.bMuted ? TEXT("MUTED (UNMUTE)") : TEXT("SOUND ON")));
		MuteBtnText->SetColorAndOpacity(PauseMenuStyle::DarkText());
	}

	// Graphics
	for (const TSharedPtr<FKomorebiDropdownOption>& Opt : QualityOptions)
	{
		if (Opt.IsValid() && Opt->Index == static_cast<int32>(State.QualityTier))
		{
			if (SelectedQualityText.IsValid()) SelectedQualityText->SetText(FText::FromString(Opt->Label));
			break;
		}
	}
	PaintToggleBtn(ComicInkBtnBg, ComicInkBtnText, State.bComicInk);
	if (ComicInkBtnText.IsValid()) ComicInkBtnText->SetText(FText::FromString(State.bComicInk ? TEXT("COMIC INK: ON") : TEXT("COMIC INK: OFF")));
	PaintToggleBtn(ImpastoBtnBg, ImpastoBtnText, State.bOilPaintImpasto);
	if (ImpastoBtnText.IsValid()) ImpastoBtnText->SetText(FText::FromString(State.bOilPaintImpasto ? TEXT("OIL-PAINT IMPASTO: ON") : TEXT("OIL-PAINT IMPASTO: OFF")));

	// Codex badges & panes
	const int32 TotalEchoes = static_cast<int32>(KomorebiCore::GetEchoShards().size());
	const int32 UnlockedEchoes = State.UnlockedEchoIds.Num();
	if (CodexEchoBadgeText.IsValid())
	{
		CodexEchoBadgeText->SetText(FText::FromString(FString::Printf(TEXT("◈ %d / %d"), UnlockedEchoes, TotalEchoes)));
	}
	if (CodexTabText[2].IsValid())
	{
		CodexTabText[2]->SetText(FText::FromString(FString::Printf(TEXT("ECHOES (%d/%d)"), UnlockedEchoes, TotalEchoes)));
	}
	UpdateCodexTabVisuals();
	if (bCodexExpanded)
	{
		RebuildCodexActsPane();
		RebuildCodexEchoesPane();
	}
}
