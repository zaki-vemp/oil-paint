// Copyright Komorebi Project. Pure Slate Cyberpunk HUD with live OnPaint Vector Minimap, cell bars, comms equalizer, and 3D markers.
#include "UI/SKomorebiHUD.h"

#include "World/WorldGrid.h"
#include "World/NeoKyotoTerrain.h"
#include "Game/LevelsData.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Styling/CoreStyle.h"
#include "Rendering/DrawElements.h"

namespace KomorebiHUDPalette
{
	inline FLinearColor Hex(const TCHAR* Code, float Alpha = 1.0f)
	{
		FLinearColor C = FLinearColor::FromSRGBColor(FColor::FromHex(Code));
		C.A = Alpha;
		return C;
	}

	inline const FLinearColor PanelBg() { return Hex(TEXT("080B10"), 0.90f); }
	inline const FLinearColor PlateBg() { return Hex(TEXT("0E141B"), 0.96f); }
	inline const FLinearColor MapBg() { return Hex(TEXT("070B10"), 0.95f); }
	inline const FLinearColor Ink() { return Hex(TEXT("E9F7FA"), 1.0f); }
	inline const FLinearColor Muted() { return Hex(TEXT("8AA0AB"), 1.0f); }
	inline const FLinearColor Edge() { return Hex(TEXT("22313B"), 1.0f); }
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
			S.Hovered.TintColor = FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.08f));
			S.Pressed.TintColor = FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.16f));
			S.Disabled.TintColor = FSlateColor(FLinearColor::Transparent);
			S.NormalPadding = FMargin(0.0f);
			S.PressedPadding = FMargin(0.0f);
			return S;
		}();
		return Style;
	}
}

// --- Custom OnPaint Leaf Widgets -------------------------------------------------------------

/** Cell-segmented cyberpunk bar (used for HP, Boss HP, Guard Meter, and 3 Spark charge pips). */
class SKomorebiCellBarWidget : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SKomorebiCellBarWidget)
		: _Segments(12)
		, _DesiredHeight(11.0f)
		, _FillColor(FLinearColor(1.0f, 0.165f, 0.427f, 1.0f))
		, _BgColor(FLinearColor(0.13f, 0.19f, 0.23f, 1.0f))
	{}
		SLATE_ARGUMENT(int32, Segments)
		SLATE_ARGUMENT(float, DesiredHeight)
		SLATE_ARGUMENT(FLinearColor, FillColor)
		SLATE_ARGUMENT(FLinearColor, BgColor)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Segments = FMath::Max(1, InArgs._Segments);
		DesiredHeight = InArgs._DesiredHeight;
		FillColor = InArgs._FillColor;
		BgColor = InArgs._BgColor;
	}

	void SetValue(float InRatio01, const FLinearColor& InFillColor)
	{
		FillRatio = FMath::Clamp(InRatio01, 0.0f, 1.0f);
		FillColor = InFillColor;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(120.0f, DesiredHeight);
	}

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override
	{
		const FVector2f Size = AllottedGeometry.GetLocalSize();
		if (Size.X <= 1.0f || Size.Y <= 1.0f)
		{
			return LayerId;
		}

		const float Gap = (Segments <= 3) ? 5.0f : 2.0f;
		const float CellW = FMath::Max(2.0f, (Size.X - Gap * (Segments - 1)) / static_cast<float>(Segments));
		const FSlateBrush* White = KomorebiHUDPalette::WhiteBrush();
		const float Scaled = FillRatio * static_cast<float>(Segments);

		for (int32 i = 0; i < Segments; ++i)
		{
			const float X = i * (CellW + Gap);
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(FVector2f(CellW, Size.Y), FSlateLayoutTransform(FVector2f(X, 0.0f))),
				White,
				ESlateDrawEffect::None,
				BgColor);

			const float CellFill = FMath::Clamp(Scaled - static_cast<float>(i), 0.0f, 1.0f);
			if (CellFill > 0.01f)
			{
				FSlateDrawElement::MakeBox(
					OutDrawElements,
					LayerId + 1,
					AllottedGeometry.ToPaintGeometry(FVector2f(CellW * CellFill, Size.Y), FSlateLayoutTransform(FVector2f(X, 0.0f))),
					White,
					ESlateDrawEffect::None,
					FillColor);
			}
		}

		return LayerId + 2;
	}

private:
	int32 Segments = 12;
	float DesiredHeight = 11.0f;
	float FillRatio = 1.0f;
	FLinearColor FillColor;
	FLinearColor BgColor;
};

/** Animated 7-bar radio voice equalizer matching legacy-web/src/game/hud.js .comms-bars. */
class SKomorebiCommsEqualizerWidget : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SKomorebiCommsEqualizerWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&) {}

	void UpdateAnim(float InAnimTime, float InVoiceLevel, bool bSpeaking, const FLinearColor& InColor)
	{
		AnimTime = InAnimTime;
		VoiceLevel = InVoiceLevel;
		bActiveSpeech = bSpeaking;
		BarColor = InColor;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(36.0f, 16.0f);
	}

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override
	{
		const FVector2f Size = AllottedGeometry.GetLocalSize();
		const FSlateBrush* White = KomorebiHUDPalette::WhiteBrush();
		constexpr int32 BarCount = 7;
		const float Gap = 2.5f;
		const float BarW = FMath::Max(2.0f, (Size.X - Gap * (BarCount - 1)) / BarCount);
		const float T = AnimTime * 15.0f;

		for (int32 i = 0; i < BarCount; ++i)
		{
			const float Wave = 0.38f + 0.62f * FMath::Abs(FMath::Sin(T + i * 1.15f) * FMath::Cos(T * 0.61f - i * 0.65f));
			const float Amp = (VoiceLevel > 0.02f)
				? FMath::Min(1.0f, 0.28f + VoiceLevel * 1.12f)
				: (bActiveSpeech ? 0.56f : 0.22f);
			const float H = Size.Y * FMath::Clamp(Amp * Wave, 0.18f, 1.0f);
			const float X = i * (BarW + Gap);

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(FVector2f(BarW, H), FSlateLayoutTransform(FVector2f(X, Size.Y - H))),
				White,
				ESlateDrawEffect::None,
				BarColor);
		}
		return LayerId + 1;
	}

private:
	float AnimTime = 0.0f;
	float VoiceLevel = 0.0f;
	bool bActiveSpeech = false;
	FLinearColor BarColor = FLinearColor(0.0f, 0.94f, 1.0f, 1.0f);
};

/** Live OnPaint Vector Minimap drawing Neo-Kyoto avenues, streets, trail spine, blips, checkpoints, and player chevron. */
class SKomorebiMinimapWidget : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SKomorebiMinimapWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&) {}

	void SetSnapshot(const FKomorebiHUDViewData& InData)
	{
		PlayerXZ = InData.PlayerThreeXZ;
		PlayerYawRad = InData.PlayerThreeYawRad;
		AccentColor = InData.ChapterAccent;
		bIsNeoKyoto = InData.bIsNeoKyoto;
		Blips = InData.MinimapBlips;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(156.0f, 156.0f);
	}

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override
	{
		const FVector2f Size = AllottedGeometry.GetLocalSize();
		const float Half = Size.X * 0.5f;
		const float Scale = Size.X / 160.0f; // Matches 288px canvas with 1.8 px/m -> 160m span across minimap
		const FSlateBrush* White = KomorebiHUDPalette::WhiteBrush();

		auto ToMap = [&](double ThreeX, double ThreeZ) -> FVector2D
		{
			return FVector2D(
				Half + (ThreeX - PlayerXZ.X) * Scale,
				Half + (ThreeZ - PlayerXZ.Y) * Scale);
		};

		// Dark scan backdrop
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform()),
			White,
			ESlateDrawEffect::None,
			KomorebiHUDPalette::MapBg());
		++LayerId;

		// World-fixed 10m grid lines (#16232c)
		const FLinearColor GridCol = KomorebiHUDPalette::Hex(TEXT("16232C"));
		const double BaseX = FMath::RoundToDouble(PlayerXZ.X / 10.0) * 10.0;
		const double BaseZ = FMath::RoundToDouble(PlayerXZ.Y / 10.0) * 10.0;
		for (int32 g = -8; g <= 8; ++g)
		{
			const FVector2D PX = ToMap(BaseX + g * 10.0, PlayerXZ.Y);
			if (PX.X >= 0.0 && PX.X <= Size.X)
			{
				TArray<FVector2D> Line{ FVector2D(PX.X, 0.0), FVector2D(PX.X, Size.Y) };
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Line, ESlateDrawEffect::None, GridCol, true, 1.0f);
			}
			const FVector2D PZ = ToMap(PlayerXZ.X, BaseZ + g * 10.0);
			if (PZ.Y >= 0.0 && PZ.Y <= Size.Y)
			{
				TArray<FVector2D> Line{ FVector2D(0.0, PZ.Y), FVector2D(Size.X, PZ.Y) };
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Line, ESlateDrawEffect::None, GridCol, true, 1.0f);
			}
		}
		++LayerId;

		// City Avenues & Streets (#1a3344) + Trail Spine
		if (bIsNeoKyoto)
		{
			const FLinearColor RoadCol = KomorebiHUDPalette::Hex(TEXT("1A3344"));
			for (double Ax : KomorebiCore::Avenues())
			{
				const FVector2D A = ToMap(Ax, KomorebiCore::kGridMin);
				const FVector2D B = ToMap(Ax, KomorebiCore::kGridMax);
				if (A.X < -6.0 || A.X > Size.X + 6.0)
				{
					continue;
				}
				TArray<FVector2D> Line{
					FVector2D(A.X, FMath::Clamp(A.Y, 0.0, static_cast<double>(Size.Y))),
					FVector2D(B.X, FMath::Clamp(B.Y, 0.0, static_cast<double>(Size.Y)))
				};
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Line, ESlateDrawEffect::None, RoadCol, true, 2.5f);
			}
			for (double Sz : KomorebiCore::Streets())
			{
				const FVector2D A = ToMap(KomorebiCore::kGridMin, Sz);
				const FVector2D B = ToMap(KomorebiCore::kGridMax, Sz);
				if (A.Y < -6.0 || A.Y > Size.Y + 6.0)
				{
					continue;
				}
				TArray<FVector2D> Line{
					FVector2D(FMath::Clamp(A.X, 0.0, static_cast<double>(Size.X)), A.Y),
					FVector2D(FMath::Clamp(B.X, 0.0, static_cast<double>(Size.X)), B.Y)
				};
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Line, ESlateDrawEffect::None, RoadCol, true, 2.5f);
			}
			++LayerId;

			// Trail spine from KomorebiCore::WaySamples()
			const std::vector<KomorebiCore::Vec2>& Way = KomorebiCore::WaySamples();
			if (Way.size() >= 2)
			{
				TArray<FVector2D> SpinePts;
				SpinePts.Reserve(static_cast<int32>(Way.size()));
				for (const KomorebiCore::Vec2& W : Way)
				{
					SpinePts.Add(ToMap(W.X, W.Z));
				}
				FLinearColor GlowCol = AccentColor;
				GlowCol.A = 0.22f;
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), SpinePts, ESlateDrawEffect::None, GlowCol, true, 6.5f);
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), SpinePts, ESlateDrawEffect::None, AccentColor, true, 2.0f);
				LayerId += 2;
			}
		}

		// Entity Blips (checkpoints, mission targets, hostiles, next objective)
		const float EdgeRadius = Half - 8.0f;
		for (const FKomorebiMinimapBlip& Blip : Blips)
		{
			const FVector2D Pt = ToMap(Blip.ThreeXZ.X, Blip.ThreeXZ.Y);
			const FVector2D Delta = Pt - FVector2D(Half, Half);
			const double Dist = Delta.Size();

			const bool bOutside = (Pt.X < 4.0 || Pt.X > Size.X - 4.0 || Pt.Y < 4.0 || Pt.Y > Size.Y - 4.0);
			if (Blip.Kind == EKomorebiMinimapBlipKind::NextObjective || Blip.Kind == EKomorebiMinimapBlipKind::MissionTarget)
			{
				FVector2D DrawPt = Pt;
				if (Dist > EdgeRadius && Dist > 0.01)
				{
					DrawPt = FVector2D(Half, Half) + (Delta / Dist) * EdgeRadius;
				}
				const float R = (Blip.Kind == EKomorebiMinimapBlipKind::NextObjective) ? 4.5f : 4.0f;
				TArray<FVector2D> Diamond{
					FVector2D(DrawPt.X, DrawPt.Y - R),
					FVector2D(DrawPt.X + R, DrawPt.Y),
					FVector2D(DrawPt.X, DrawPt.Y + R),
					FVector2D(DrawPt.X - R, DrawPt.Y),
					FVector2D(DrawPt.X, DrawPt.Y - R)
				};
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Diamond, ESlateDrawEffect::None, Blip.Color, true, 2.0f);
				FSlateDrawElement::MakeBox(
					OutDrawElements,
					LayerId,
					AllottedGeometry.ToPaintGeometry(FVector2f(R, R), FSlateLayoutTransform(FVector2f(static_cast<float>(DrawPt.X - R * 0.5f), static_cast<float>(DrawPt.Y - R * 0.5f)))),
					White,
					ESlateDrawEffect::None,
					Blip.Color);
				continue;
			}

			if (bOutside)
			{
				continue;
			}

			if (Blip.Kind == EKomorebiMinimapBlipKind::Checkpoint || Blip.Kind == EKomorebiMinimapBlipKind::CheckpointUsed)
			{
				const FLinearColor CpCol = KomorebiHUDPalette::Hex(TEXT("42D9EF"));
				if (Blip.Kind == EKomorebiMinimapBlipKind::Checkpoint)
				{
					FSlateDrawElement::MakeBox(
						OutDrawElements,
						LayerId,
						AllottedGeometry.ToPaintGeometry(FVector2f(6.0f, 6.0f), FSlateLayoutTransform(FVector2f(static_cast<float>(Pt.X - 3.0), static_cast<float>(Pt.Y - 3.0)))),
						White,
						ESlateDrawEffect::None,
						CpCol);
				}
				else
				{
					TArray<FVector2D> Sq{
						FVector2D(Pt.X - 3.0, Pt.Y - 3.0),
						FVector2D(Pt.X + 3.0, Pt.Y - 3.0),
						FVector2D(Pt.X + 3.0, Pt.Y + 3.0),
						FVector2D(Pt.X - 3.0, Pt.Y + 3.0),
						FVector2D(Pt.X - 3.0, Pt.Y - 3.0)
					};
					FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Sq, ESlateDrawEffect::None, CpCol, true, 1.5f);
				}
			}
			else if (Blip.Kind == EKomorebiMinimapBlipKind::Citizen)
			{
				const float BoxS = Blip.bMarked ? 5.0f : 3.0f;
				const FLinearColor Col = Blip.bMarked ? KomorebiHUDPalette::Red() : KomorebiHUDPalette::Muted();
				FSlateDrawElement::MakeBox(
					OutDrawElements,
					LayerId,
					AllottedGeometry.ToPaintGeometry(FVector2f(BoxS, BoxS), FSlateLayoutTransform(FVector2f(static_cast<float>(Pt.X - BoxS * 0.5f), static_cast<float>(Pt.Y - BoxS * 0.5f)))),
					White,
					ESlateDrawEffect::None,
					Col);
			}
			else
			{
				const float R = (Blip.Kind == EKomorebiMinimapBlipKind::Brute) ? 5.0f : 3.2f;
				const FLinearColor Col = Blip.bWarning ? KomorebiHUDPalette::Yellow() : Blip.Color;
				TArray<FVector2D> Diamond{
					FVector2D(Pt.X, Pt.Y - R),
					FVector2D(Pt.X + R, Pt.Y),
					FVector2D(Pt.X, Pt.Y + R),
					FVector2D(Pt.X - R, Pt.Y),
					FVector2D(Pt.X, Pt.Y - R)
				};
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Diamond, ESlateDrawEffect::None, Col, true, 2.0f);
			}
		}
		++LayerId;

		// White rotated player chevron at minimap center (matching hud.js lines 200-202)
		const float CosA = FMath::Cos(-PlayerYawRad);
		const float SinA = FMath::Sin(-PlayerYawRad);
		auto RotPt = [&](float LocalX, float LocalY) -> FVector2D
		{
			return FVector2D(
				Half + LocalX * CosA - LocalY * SinA,
				Half + LocalX * SinA + LocalY * CosA);
		};
		TArray<FVector2D> Chevron{
			RotPt(0.0f, 6.5f),
			RotPt(-4.8f, -4.2f),
			RotPt(0.0f, -1.5f),
			RotPt(4.8f, -4.2f),
			RotPt(0.0f, 6.5f)
		};
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(),
			Chevron,
			ESlateDrawEffect::None,
			FLinearColor::White,
			true,
			2.2f);

		return LayerId + 1;
	}

private:
	FVector2D PlayerXZ = FVector2D::ZeroVector;
	float PlayerYawRad = 3.14159265f;
	FLinearColor AccentColor = FLinearColor(0.0f, 0.94f, 1.0f, 1.0f);
	bool bIsNeoKyoto = true;
	TArray<FKomorebiMinimapBlip> Blips;
};

/** Full-screen OnPaint layer rendering projected 3D World Waypoints, Focus Reticles, and Remote Peer Nameplates. */
class SKomorebiScreenMarkersLayer : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SKomorebiScreenMarkersLayer) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&) {}

	void UpdateMarkers(
		const FKomorebiProjectedMarker& InWaypoint,
		const FKomorebiProjectedMarker& InFocus,
		const TArray<FKomorebiProjectedMarker>& InPeers)
	{
		Waypoint = InWaypoint;
		Focus = InFocus;
		Peers = InPeers;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D::ZeroVector;
	}

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override
	{
		const float InvScale = (AllottedGeometry.Scale > KINDA_SMALL_NUMBER) ? (1.0f / AllottedGeometry.Scale) : 1.0f;
		const FSlateBrush* White = KomorebiHUDPalette::WhiteBrush();
		const FSlateFontInfo SmallBold = FCoreStyle::GetDefaultFontStyle("Bold", 9);
		const FSlateFontInfo PeerFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);

		// 1. 3D Projected Focus Reticle (rotated cyan diamond)
		if (Focus.bVisible)
		{
			const FVector2D P = Focus.ScreenPos * InvScale;
			const float R = 12.0f;
			TArray<FVector2D> Diamond{
				FVector2D(P.X, P.Y - R),
				FVector2D(P.X + R, P.Y),
				FVector2D(P.X, P.Y + R),
				FVector2D(P.X - R, P.Y),
				FVector2D(P.X, P.Y - R)
			};
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(),
				Diamond,
				ESlateDrawEffect::None,
				KomorebiHUDPalette::Cyan(),
				true,
				2.0f);
		}

		// 2. 3D Projected World Waypoint Diamond + Clamped Arrow + Label/Dist
		if (Waypoint.bVisible)
		{
			const FVector2D P = Waypoint.ScreenPos * InvScale;
			const float R = 9.0f;
			TArray<FVector2D> Diamond{
				FVector2D(P.X, P.Y - R),
				FVector2D(P.X + R, P.Y),
				FVector2D(P.X, P.Y + R),
				FVector2D(P.X - R, P.Y),
				FVector2D(P.X, P.Y - R)
			};
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(),
				Diamond,
				ESlateDrawEffect::None,
				Waypoint.Color,
				true,
				2.2f);

			if (Waypoint.bClamped)
			{
				const float Rad = FMath::DegreesToRadians(Waypoint.ClampAngleDeg);
				const FVector2D Tip(P.X + FMath::Sin(Rad) * 18.0f, P.Y - FMath::Cos(Rad) * 18.0f);
				const FVector2D Base(P.X + FMath::Sin(Rad) * 11.0f, P.Y - FMath::Cos(Rad) * 11.0f);
				const FVector2D Perp(FMath::Cos(Rad) * 5.0f, FMath::Sin(Rad) * 5.0f);
				TArray<FVector2D> Arrow{ Base - Perp, Tip, Base + Perp, Base - Perp };
				FSlateDrawElement::MakeLines(
					OutDrawElements,
					LayerId,
					AllottedGeometry.ToPaintGeometry(),
					Arrow,
					ESlateDrawEffect::None,
					KomorebiHUDPalette::Yellow(),
					true,
					2.0f);
			}

			if (!Waypoint.Label.IsEmpty())
			{
				const float LabelW = FMath::Max(72.0f, static_cast<float>(Waypoint.Label.Len()) * 6.8f + 16.0f);
				const float BoxX = static_cast<float>(P.X) - LabelW * 0.5f;
				const float BoxY = static_cast<float>(P.Y) + 12.0f;

				FSlateDrawElement::MakeBox(
					OutDrawElements,
					LayerId,
					AllottedGeometry.ToPaintGeometry(FVector2f(LabelW, 18.0f), FSlateLayoutTransform(FVector2f(BoxX, BoxY))),
					White,
					ESlateDrawEffect::None,
					KomorebiHUDPalette::PanelBg());

				FSlateDrawElement::MakeText(
					OutDrawElements,
					LayerId + 1,
					AllottedGeometry.ToPaintGeometry(FVector2f(LabelW, 18.0f), FSlateLayoutTransform(FVector2f(BoxX + 6.0f, BoxY + 3.0f))),
					FText::FromString(Waypoint.Label.ToUpper()),
					SmallBold,
					ESlateDrawEffect::None,
					FLinearColor::White);

				if (!Waypoint.SubLabel.IsEmpty())
				{
					FSlateDrawElement::MakeText(
						OutDrawElements,
						LayerId + 1,
						AllottedGeometry.ToPaintGeometry(FVector2f(LabelW, 14.0f), FSlateLayoutTransform(FVector2f(static_cast<float>(P.X) - 18.0f, BoxY + 20.0f))),
						FText::FromString(Waypoint.SubLabel),
						SmallBold,
						ESlateDrawEffect::None,
						Waypoint.Color);
				}
			}
		}

		// 3. Remote Peer Nameplates ("Name · Title" with unlocked neon badge)
		for (const FKomorebiProjectedMarker& Peer : Peers)
		{
			if (!Peer.bVisible || Peer.Label.IsEmpty())
			{
				continue;
			}
			const FVector2D P = Peer.ScreenPos * InvScale;
			FString FullTag = Peer.Label;
			if (Peer.bIsDowned)
			{
				FullTag += TEXT(" · DOWN");
			}
			else if (Peer.bIsDriving)
			{
				FullTag += TEXT(" · DRIVING");
			}

			const float TagW = FMath::Max(64.0f, static_cast<float>(FullTag.Len()) * 7.2f + 16.0f);
			const float TagH = 20.0f;
			const float X = static_cast<float>(P.X) - TagW * 0.5f;
			const float Y = static_cast<float>(P.Y) - TagH - 6.0f;

			const FLinearColor BgCol = Peer.bIsDowned ? KomorebiHUDPalette::Red() : Peer.Color;
			const FLinearColor TextCol = Peer.bIsDowned ? FLinearColor::White : KomorebiHUDPalette::DarkText();

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(FVector2f(TagW, TagH), FSlateLayoutTransform(FVector2f(X, Y))),
				White,
				ESlateDrawEffect::None,
				BgCol);

			FSlateDrawElement::MakeText(
				OutDrawElements,
				LayerId + 1,
				AllottedGeometry.ToPaintGeometry(FVector2f(TagW, TagH), FSlateLayoutTransform(FVector2f(X + 7.0f, Y + 3.0f))),
				FText::FromString(FullTag),
				PeerFont,
				ESlateDrawEffect::None,
				TextCol);
		}

		return LayerId + 2;
	}

private:
	FKomorebiProjectedMarker Waypoint;
	FKomorebiProjectedMarker Focus;
	TArray<FKomorebiProjectedMarker> Peers;
};

// --- SKomorebiHUD Main Layout ----------------------------------------------------------------

SKomorebiHUD::~SKomorebiHUD() = default;

void SKomorebiHUD::Construct(const FArguments& InArgs)
{
	OnDismissComms = InArgs._OnDismissComms;

	ChildSlot
	[
		SNew(SOverlay)

		// Layer 0: Damage Red Edge Vignette
		+ SOverlay::Slot()
		[
			SAssignNew(DamageVignetteBorder, SBorder)
			.Visibility(EVisibility::HitTestInvisible)
			.BorderImage(KomorebiHUDPalette::WhiteBrush())
			.BorderBackgroundColor(FLinearColor(1.0f, 0.165f, 0.427f, 0.0f))
		]

		// Layer 1: Projected 3D Waypoints, Focus Reticle & Peer Nameplates
		+ SOverlay::Slot()
		[
			SAssignNew(MarkersLayer, SKomorebiScreenMarkersLayer)
			.Visibility(EVisibility::HitTestInvisible)
		]

		// Layer 2: Top Header Wordmark & Online Count Pill
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Top)
		.Padding(FMargin(28.0f, 20.0f, 28.0f, 0.0f))
		[
			BuildHeaderBar()
		]

		// Layer 3: Top-Left Quest Plate
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(FMargin(28.0f, 82.0f, 0.0f, 0.0f))
		[
			BuildQuestPlate()
		]

		// Layer 4: Top-Right Vector Minimap + Clock + Echo Counter
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(FMargin(0.0f, 82.0f, 28.0f, 0.0f))
		[
			BuildMinimapColumn()
		]

		// Layer 5: Top-Center Target Lock, Boss Duel Protocol & Heat Banner
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Top)
		.Padding(FMargin(0.0f, 24.0f, 0.0f, 0.0f))
		[
			BuildTargetAndBossStack()
		]

		// Layer 6: Upper-Center Voiced Radio Comms Plate
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Top)
		.Padding(FMargin(0.0f, 94.0f, 0.0f, 0.0f))
		[
			BuildRadioCommsPlate()
		]

		// Layer 7: Bottom-Left Vitals + Speedometer
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(28.0f, 0.0f, 0.0f, 28.0f))
		[
			BuildVitalsAndVehicleStack()
		]

		// Layer 8: Bottom-Center Abilities Bar
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 24.0f))
		[
			BuildAbilitiesBar()
		]

		// Layer 9: Center Overlays (Combo, Flash, Toast, Chapter Banner, Vehicle Prompt, Guard Status)
		+ SOverlay::Slot()
		[
			BuildCenterOverlays()
		]
	];

	RefreshFromViewData(FKomorebiHUDViewData{});
}

TSharedRef<SWidget> SKomorebiHUD::BuildHeaderBar()
{
	return SNew(SHorizontalBox)
		.Visibility(EVisibility::HitTestInvisible)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("KOMOREBI")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 20))
				.ColorAndOpacity(KomorebiHUDPalette::Yellow())
				.ShadowOffset(FVector2D(1.5f, 1.5f))
				.ShadowColorAndOpacity(FLinearColor::Black)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("PROTOCOL ZERO-SEVEN")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				.ColorAndOpacity(KomorebiHUDPalette::Cyan())
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
		.Padding(FMargin(0.0f, 0.0f, 10.0f, 0.0f))
		[
			SAssignNew(OnlinePillBox, SBorder)
			.Visibility(EVisibility::Collapsed)
			.BorderImage(KomorebiHUDPalette::WhiteBrush())
			.BorderBackgroundColor(KomorebiHUDPalette::Cyan().CopyWithNewOpacity(0.45f))
			.Padding(1.0f)
			[
				SNew(SBorder)
				.BorderImage(KomorebiHUDPalette::WhiteBrush())
				.BorderBackgroundColor(KomorebiHUDPalette::PanelBg())
				.Padding(FMargin(10.0f, 5.0f))
				[
					SAssignNew(OnlinePillText, STextBlock)
					.Text(FText::FromString(TEXT("ONLINE · 1")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.ColorAndOpacity(KomorebiHUDPalette::Cyan())
				]
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(KomorebiHUDPalette::WhiteBrush())
			.BorderBackgroundColor(KomorebiHUDPalette::Edge())
			.Padding(1.0f)
			[
				SNew(SBorder)
				.BorderImage(KomorebiHUDPalette::WhiteBrush())
				.BorderBackgroundColor(KomorebiHUDPalette::PanelBg())
				.Padding(FMargin(10.0f, 5.0f))
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("MENU [ESC]  ·  TOUR [F9]")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.ColorAndOpacity(KomorebiHUDPalette::Ink())
				]
			]
		];
}

TSharedRef<SWidget> SKomorebiHUD::BuildQuestPlate()
{
	TSharedRef<SHorizontalBox> JourneyRow = SNew(SHorizontalBox);
	JourneyPips.Reset();
	for (int32 i = 0; i < 6; ++i)
	{
		TSharedPtr<SBorder> Pip;
		JourneyRow->AddSlot()
			.FillWidth(1.0f)
			.Padding(FMargin(i == 0 ? 0.0f : 4.0f, 0.0f, 0.0f, 0.0f))
			[
				SNew(SBox)
				.HeightOverride(5.0f)
				[
					SAssignNew(Pip, SBorder)
					.BorderImage(KomorebiHUDPalette::WhiteBrush())
					.BorderBackgroundColor(KomorebiHUDPalette::Edge())
				]
			];
		JourneyPips.Add(Pip);
	}

	return SNew(SBox)
		.WidthOverride(300.0f)
		.Visibility(EVisibility::HitTestInvisible)
		[
			SAssignNew(QuestOuterBorder, SBorder)
			.BorderImage(KomorebiHUDPalette::WhiteBrush())
			.BorderBackgroundColor(KomorebiHUDPalette::Cyan().CopyWithNewOpacity(0.45f))
			.Padding(1.0f)
			[
				SNew(SBorder)
				.BorderImage(KomorebiHUDPalette::WhiteBrush())
				.BorderBackgroundColor(KomorebiHUDPalette::PanelBg())
				.Padding(0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SBox)
						.WidthOverride(4.0f)
						[
							SAssignNew(QuestStripeBorder, SBorder)
							.BorderImage(KomorebiHUDPalette::WhiteBrush())
							.BorderBackgroundColor(KomorebiHUDPalette::Cyan())
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(FMargin(14.0f, 12.0f, 14.0f, 12.0f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SAssignNew(SectorLabelText, STextBlock)
							.Text(FText::FromString(TEXT("SECTOR 01 / 06")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
							.ColorAndOpacity(KomorebiHUDPalette::Cyan())
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0.0f, 3.0f, 0.0f, 6.0f))
						[
							SAssignNew(ChapterTitleText, STextBlock)
							.Text(FText::FromString(TEXT("SUNLEAF ROW")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
							.ColorAndOpacity(KomorebiHUDPalette::Ink())
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
						[
							SAssignNew(ObjectiveText, STextBlock)
							.AutoWrapText(true)
							.Text(FText::FromString(TEXT("▸ PUSH THROUGH THE DISTRICT.")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
							.ColorAndOpacity(KomorebiHUDPalette::Ink())
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
						[
							SAssignNew(ChecklistBox, SVerticalBox)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
						[
							SAssignNew(QuestTipText, STextBlock)
							.AutoWrapText(true)
							.Visibility(EVisibility::Collapsed)
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
							.ColorAndOpacity(KomorebiHUDPalette::Cyan())
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SAssignNew(HostileCountText, STextBlock)
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
								.ColorAndOpacity(KomorebiHUDPalette::Yellow())
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.HAlign(HAlign_Center)
							[
								SAssignNew(DistanceText, STextBlock)
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
								.ColorAndOpacity(KomorebiHUDPalette::Muted())
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SAssignNew(CashText, STextBlock)
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
								.ColorAndOpacity(KomorebiHUDPalette::Yellow())
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0.0f, 10.0f, 0.0f, 0.0f))
						[
							JourneyRow
						]
					]
				]
			]
		];
}

TSharedRef<SWidget> SKomorebiHUD::BuildMinimapColumn()
{
	return SNew(SVerticalBox)
		.Visibility(EVisibility::HitTestInvisible)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		[
			SAssignNew(MinimapOuterBorder, SBorder)
			.BorderImage(KomorebiHUDPalette::WhiteBrush())
			.BorderBackgroundColor(KomorebiHUDPalette::Cyan().CopyWithNewOpacity(0.60f))
			.Padding(1.0f)
			[
				SNew(SBox)
				.WidthOverride(156.0f)
				.HeightOverride(156.0f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SAssignNew(MinimapWidget, SKomorebiMinimapWidget)
					]
					+ SOverlay::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Top)
					.Padding(FMargin(6.0f, 4.0f, 0.0f, 0.0f))
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("N")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						.ColorAndOpacity(KomorebiHUDPalette::Yellow())
					]
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(FMargin(0.0f, 5.0f, 0.0f, 0.0f))
		[
			SNew(SBorder)
			.BorderImage(KomorebiHUDPalette::WhiteBrush())
			.BorderBackgroundColor(KomorebiHUDPalette::PanelBg())
			.Padding(FMargin(8.0f, 3.0f))
			[
				SAssignNew(MapZoneText, STextBlock)
				.Text(FText::FromString(TEXT("SCAN · DOWNTOWN")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				.ColorAndOpacity(KomorebiHUDPalette::Cyan())
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
		[
			SNew(SBorder)
			.BorderImage(KomorebiHUDPalette::WhiteBrush())
			.BorderBackgroundColor(KomorebiHUDPalette::PanelBg())
			.Padding(FMargin(8.0f, 3.0f))
			[
				SAssignNew(DayClockText, STextBlock)
				.Text(FText::FromString(TEXT("DAY · 10:00")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				.ColorAndOpacity(KomorebiHUDPalette::Yellow())
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
		[
			SAssignNew(EchoBadgeBox, SBorder)
			.BorderImage(KomorebiHUDPalette::WhiteBrush())
			.BorderBackgroundColor(KomorebiHUDPalette::Mint().CopyWithNewOpacity(0.50f))
			.Padding(1.0f)
			[
				SNew(SBorder)
				.BorderImage(KomorebiHUDPalette::WhiteBrush())
				.BorderBackgroundColor(KomorebiHUDPalette::PanelBg())
				.Padding(FMargin(8.0f, 3.0f))
				[
					SAssignNew(EchoBadgeText, STextBlock)
					.Text(FText::FromString(TEXT("◈ 0/12 ECHOES")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
					.ColorAndOpacity(KomorebiHUDPalette::Mint())
				]
			]
		];
}

TSharedRef<SWidget> SKomorebiHUD::BuildTargetAndBossStack()
{
	return SNew(SVerticalBox)
		.Visibility(EVisibility::HitTestInvisible)

		// Target Lock Plate
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		[
			SAssignNew(TargetPanelBox, SBox)
			.WidthOverride(280.0f)
			.Visibility(EVisibility::Collapsed)
			[
				SAssignNew(TargetOuterBorder, SBorder)
				.BorderImage(KomorebiHUDPalette::WhiteBrush())
				.BorderBackgroundColor(KomorebiHUDPalette::Red().CopyWithNewOpacity(0.65f))
				.Padding(1.0f)
				[
					SNew(SBorder)
					.BorderImage(KomorebiHUDPalette::WhiteBrush())
					.BorderBackgroundColor(KomorebiHUDPalette::PanelBg())
					.Padding(FMargin(14.0f, 9.0f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							[
								SAssignNew(TargetNameText, STextBlock)
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
								.ColorAndOpacity(KomorebiHUDPalette::Ink())
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SAssignNew(TargetSubText, STextBlock)
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
								.ColorAndOpacity(KomorebiHUDPalette::Red())
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0.0f, 6.0f))
						[
							SAssignNew(TargetHpBar, SKomorebiCellBarWidget)
							.Segments(14)
							.DesiredHeight(6.0f)
							.FillColor(KomorebiHUDPalette::Red())
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SAssignNew(TargetTellText, STextBlock)
							.AutoWrapText(true)
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
							.ColorAndOpacity(KomorebiHUDPalette::Muted())
						]
					]
				]
			]
		]

		// Boss Duel Protocol Bar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		[
			SAssignNew(BossPanelBox, SBox)
			.WidthOverride(480.0f)
			.Visibility(EVisibility::Collapsed)
			[
				SAssignNew(BossOuterBorder, SBorder)
				.BorderImage(KomorebiHUDPalette::WhiteBrush())
				.BorderBackgroundColor(KomorebiHUDPalette::Red().CopyWithNewOpacity(0.75f))
				.Padding(1.0f)
				[
					SNew(SBorder)
					.BorderImage(KomorebiHUDPalette::WhiteBrush())
					.BorderBackgroundColor(KomorebiHUDPalette::PanelBg())
					.Padding(FMargin(18.0f, 12.0f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(0.0f, 0.0f, 10.0f, 0.0f))
							[
								SNew(SBorder)
								.BorderImage(KomorebiHUDPalette::WhiteBrush())
								.BorderBackgroundColor(KomorebiHUDPalette::Red())
								.Padding(FMargin(6.0f, 2.0f))
								[
									SNew(STextBlock)
									.Text(FText::FromString(TEXT("DUEL PROTOCOL")))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
									.ColorAndOpacity(KomorebiHUDPalette::DarkText())
								]
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							[
								SAssignNew(BossNameText, STextBlock)
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
								.ColorAndOpacity(KomorebiHUDPalette::Ink())
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SAssignNew(BossHpText, STextBlock)
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
								.ColorAndOpacity(KomorebiHUDPalette::Red())
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0.0f, 8.0f))
						[
							SAssignNew(BossHpBar, SKomorebiCellBarWidget)
							.Segments(20)
							.DesiredHeight(9.0f)
							.FillColor(KomorebiHUDPalette::Red())
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SAssignNew(BossCueText, STextBlock)
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
							.ColorAndOpacity(KomorebiHUDPalette::Yellow())
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0.0f, 3.0f, 0.0f, 8.0f))
						[
							SAssignNew(BossLessonText, STextBlock)
							.AutoWrapText(true)
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
							.ColorAndOpacity(KomorebiHUDPalette::Muted())
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 16.0f, 0.0f))
							[
								SAssignNew(BossStepReadText, STextBlock)
								.Text(FText::FromString(TEXT("□ READ")))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
								.ColorAndOpacity(KomorebiHUDPalette::Muted())
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 16.0f, 0.0f))
							[
								SAssignNew(BossStepEvadeText, STextBlock)
								.Text(FText::FromString(TEXT("□ EVADE")))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
								.ColorAndOpacity(KomorebiHUDPalette::Muted())
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SAssignNew(BossStepPunishText, STextBlock)
								.Text(FText::FromString(TEXT("□ PUNISH")))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
								.ColorAndOpacity(KomorebiHUDPalette::Muted())
							]
						]
					]
				]
			]
		]

		// Heat Stars Warning Banner
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
		[
			SAssignNew(HeatBannerBox, SBorder)
			.Visibility(EVisibility::Collapsed)
			.BorderImage(KomorebiHUDPalette::WhiteBrush())
			.BorderBackgroundColor(KomorebiHUDPalette::Red())
			.Padding(FMargin(14.0f, 5.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(0.0f, 0.0f, 10.0f, 0.0f))
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("HEAT")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
					.ColorAndOpacity(KomorebiHUDPalette::DarkText())
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SAssignNew(HeatStarsText, STextBlock)
					.Text(FText::FromString(TEXT("●")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
					.ColorAndOpacity(KomorebiHUDPalette::DarkText())
				]
			]
		];
}

TSharedRef<SWidget> SKomorebiHUD::BuildRadioCommsPlate()
{
	return SAssignNew(CommsPanelBox, SBox)
		.WidthOverride(500.0f)
		.Visibility(EVisibility::Collapsed)
		[
			SAssignNew(CommsOuterBorder, SBorder)
			.BorderImage(KomorebiHUDPalette::WhiteBrush())
			.BorderBackgroundColor(KomorebiHUDPalette::Cyan().CopyWithNewOpacity(0.75f))
			.Padding(1.0f)
			[
				SNew(SBorder)
				.BorderImage(KomorebiHUDPalette::WhiteBrush())
				.BorderBackgroundColor(KomorebiHUDPalette::Hex(TEXT("0A1017"), 0.95f))
				.Padding(FMargin(12.0f, 10.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.0f, 0.0f, 12.0f, 0.0f))
					[
						SAssignNew(CommsPortraitBorder, SBorder)
						.BorderImage(KomorebiHUDPalette::WhiteBrush())
						.BorderBackgroundColor(KomorebiHUDPalette::Cyan())
						.Padding(1.5f)
						[
							SNew(SBox)
							.WidthOverride(52.0f)
							.HeightOverride(60.0f)
							[
								SNew(SBorder)
								.BorderImage(KomorebiHUDPalette::WhiteBrush())
								.BorderBackgroundColor(KomorebiHUDPalette::PlateBg())
								.HAlign(HAlign_Center)
								.VAlign(VAlign_Center)
								[
									SAssignNew(CommsMonogramText, STextBlock)
									.Text(FText::FromString(TEXT("SR")))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
									.ColorAndOpacity(KomorebiHUDPalette::Cyan())
								]
							]
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))
							[
								SAssignNew(CommsSpeakerText, STextBlock)
								.Text(FText::FromString(TEXT("SORA")))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
								.ColorAndOpacity(KomorebiHUDPalette::Cyan())
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SAssignNew(CommsRoleBadge, SBorder)
								.BorderImage(KomorebiHUDPalette::WhiteBrush())
								.BorderBackgroundColor(KomorebiHUDPalette::Cyan())
								.Padding(FMargin(6.0f, 2.0f))
								[
									SAssignNew(CommsRoleText, STextBlock)
									.Text(FText::FromString(TEXT("GHOST-LINE HANDLER")))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
									.ColorAndOpacity(KomorebiHUDPalette::DarkText())
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
								SAssignNew(CommsEqualizer, SKomorebiCommsEqualizerWidget)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0.0f, 6.0f))
						[
							SAssignNew(CommsLineText, STextBlock)
							.AutoWrapText(true)
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
							.ColorAndOpacity(KomorebiHUDPalette::Ink())
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							[
								SAssignNew(CommsFreqText, STextBlock)
								.Text(FText::FromString(TEXT("CH-07 // RESISTANCE LINK")))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
								.ColorAndOpacity(KomorebiHUDPalette::Muted())
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SButton)
								.ButtonStyle(&KomorebiHUDPalette::FlatButtonStyle())
								.OnClicked_Lambda([this]() -> FReply
								{
									OnDismissComms.ExecuteIfBound();
									return FReply::Handled();
								})
								[
									SNew(STextBlock)
									.Text(FText::FromString(TEXT("DISMISS [K]")))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
									.ColorAndOpacity(KomorebiHUDPalette::Yellow())
								]
							]
						]
					]
				]
			]
		];
}

TSharedRef<SWidget> SKomorebiHUD::BuildVitalsAndVehicleStack()
{
	return SNew(SVerticalBox)
		.Visibility(EVisibility::HitTestInvisible)

		// Vehicle Speedometer (above vitals while driving)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 14.0f))
		[
			SAssignNew(VehicleSpeedBox, SVerticalBox)
			.Visibility(EVisibility::Collapsed)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Bottom)
				[
					SAssignNew(VehicleKmhText, STextBlock)
					.Text(FText::FromString(TEXT("0")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 42))
					.ColorAndOpacity(KomorebiHUDPalette::Yellow())
					.ShadowOffset(FVector2D(2.0f, 2.0f))
					.ShadowColorAndOpacity(KomorebiHUDPalette::Red())
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Bottom)
				.Padding(FMargin(6.0f, 0.0f, 0.0f, 8.0f))
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("KM/H")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
					.ColorAndOpacity(KomorebiHUDPalette::Cyan())
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(VehicleNameText, STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.ColorAndOpacity(KomorebiHUDPalette::Ink())
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(VehicleStatusText, STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(KomorebiHUDPalette::Muted())
			]
		]

		// Vitals Plate (Kael portrait, HP bar, 3 Spark pips)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.WidthOverride(318.0f)
			[
				SAssignNew(VitalsOuterBorder, SBorder)
				.BorderImage(KomorebiHUDPalette::WhiteBrush())
				.BorderBackgroundColor(KomorebiHUDPalette::Cyan().CopyWithNewOpacity(0.45f))
				.Padding(1.0f)
				[
					SNew(SBorder)
					.BorderImage(KomorebiHUDPalette::WhiteBrush())
					.BorderBackgroundColor(KomorebiHUDPalette::PanelBg())
					.Padding(FMargin(12.0f, 10.0f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(FMargin(0.0f, 0.0f, 12.0f, 0.0f))
						[
							SNew(SBorder)
							.BorderImage(KomorebiHUDPalette::WhiteBrush())
							.BorderBackgroundColor(KomorebiHUDPalette::Cyan())
							.Padding(1.0f)
							[
								SNew(SBox)
								.WidthOverride(48.0f)
								.HeightOverride(54.0f)
								[
									SNew(SBorder)
									.BorderImage(KomorebiHUDPalette::WhiteBrush())
									.BorderBackgroundColor(KomorebiHUDPalette::PlateBg())
									.HAlign(HAlign_Center)
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.Text(FText::FromString(TEXT("07")))
										.Font(FCoreStyle::GetDefaultFontStyle("Bold", 20))
										.ColorAndOpacity(KomorebiHUDPalette::Yellow())
									]
								]
							]
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								[
									SNew(STextBlock)
									.Text(FText::FromString(TEXT("KAEL // 07")))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
									.ColorAndOpacity(KomorebiHUDPalette::Ink())
								]
								+ SHorizontalBox::Slot()
								.AutoWidth()
								[
									SAssignNew(HpNumberText, STextBlock)
									.Text(FText::FromString(TEXT("100 / 100")))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
									.ColorAndOpacity(KomorebiHUDPalette::Red())
								]
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SAssignNew(PlayerHpBar, SKomorebiCellBarWidget)
								.Segments(16)
								.DesiredHeight(11.0f)
								.FillColor(KomorebiHUDPalette::Red())
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.AutoWidth()
								[
									SNew(SBox)
									.WidthOverride(104.0f)
									[
										SAssignNew(SparkPipsBar, SKomorebiCellBarWidget)
										.Segments(3)
										.DesiredHeight(6.0f)
										.FillColor(KomorebiHUDPalette::Cyan())
									]
								]
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								.Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
								[
									SNew(STextBlock)
									.Text(FText::FromString(TEXT("CHARGE")))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
									.ColorAndOpacity(KomorebiHUDPalette::Cyan())
								]
							]
						]
					]
				]
			]
		];
}

TSharedRef<SWidget> SKomorebiHUD::BuildAbilitiesBar()
{
	struct FSlotDef { const TCHAR* Key; const TCHAR* Name; };
	static const FSlotDef Defs[6] = {
		{ TEXT("J"), TEXT("STRIKE") },
		{ TEXT("Q"), TEXT("SPARK") },
		{ TEXT("E"), TEXT("DASH") },
		{ TEXT("F"), TEXT("HEAVY") },
		{ TEXT("R"), TEXT("FLARE") },
		{ TEXT("C"), TEXT("WHIRL") },
	};

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	AbilitySlots.Reset();

	for (int32 i = 0; i < 6; ++i)
	{
		FAbilitySlotWidgets SlotW;
		Row->AddSlot()
			.AutoWidth()
			.Padding(FMargin(i == 0 ? 0.0f : 8.0f, 0.0f, 0.0f, 0.0f))
			[
				SNew(SBox)
				.WidthOverride(68.0f)
				.HeightOverride(76.0f)
				[
					SAssignNew(SlotW.OuterBorder, SBorder)
					.BorderImage(KomorebiHUDPalette::WhiteBrush())
					.BorderBackgroundColor(KomorebiHUDPalette::Cyan().CopyWithNewOpacity(0.45f))
					.Padding(1.0f)
					[
						SNew(SBorder)
						.BorderImage(KomorebiHUDPalette::WhiteBrush())
						.BorderBackgroundColor(KomorebiHUDPalette::PanelBg())
						.Padding(0.0f)
						[
							SNew(SOverlay)
							+ SOverlay::Slot()
							.HAlign(HAlign_Left)
							.VAlign(VAlign_Top)
							[
								SAssignNew(SlotW.KeyBadge, SBorder)
								.BorderImage(KomorebiHUDPalette::WhiteBrush())
								.BorderBackgroundColor(KomorebiHUDPalette::Yellow())
								.Padding(FMargin(5.0f, 1.5f))
								[
									SAssignNew(SlotW.KeyText, STextBlock)
									.Text(FText::FromString(Defs[i].Key))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
									.ColorAndOpacity(KomorebiHUDPalette::DarkText())
								]
							]
							+ SOverlay::Slot()
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							.Padding(FMargin(2.0f, 16.0f, 2.0f, 2.0f))
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.AutoHeight()
								.HAlign(HAlign_Center)
								[
									SAssignNew(SlotW.NameText, STextBlock)
									.Text(FText::FromString(Defs[i].Name))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
									.ColorAndOpacity(KomorebiHUDPalette::Ink())
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.HAlign(HAlign_Center)
								.Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
								[
									SAssignNew(SlotW.StatusText, STextBlock)
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
									.ColorAndOpacity(KomorebiHUDPalette::Yellow())
								]
							]
						]
					]
				]
			];
		AbilitySlots.Add(SlotW);
	}

	AbilitiesContainer = Row;
	return Row;
}

TSharedRef<SWidget> SKomorebiHUD::BuildCenterOverlays()
{
	return SNew(SOverlay)
		.Visibility(EVisibility::HitTestInvisible)

		// Chapter Banner (upper center)
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Top)
		.Padding(FMargin(0.0f, 190.0f, 0.0f, 0.0f))
		[
			SAssignNew(ChapterBannerBox, SVerticalBox)
			.Visibility(EVisibility::Collapsed)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SAssignNew(BannerSectorText, STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.ColorAndOpacity(KomorebiHUDPalette::Cyan())
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(FMargin(0.0f, 6.0f))
			[
				SAssignNew(BannerTitleText, STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 38))
				.ColorAndOpacity(FLinearColor::White)
				.ShadowOffset(FVector2D(2.5f, 2.5f))
				.ShadowColorAndOpacity(KomorebiHUDPalette::Red())
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(SBorder)
				.BorderImage(KomorebiHUDPalette::WhiteBrush())
				.BorderBackgroundColor(KomorebiHUDPalette::Yellow())
				.Padding(FMargin(12.0f, 4.0f))
				[
					SAssignNew(BannerSubtitleText, STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.ColorAndOpacity(KomorebiHUDPalette::DarkText())
				]
			]
		]

		// Center Flash Text (Perfect Dodge, Parry, City Limits, etc.)
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SAssignNew(FlashBox, SBox)
			.Visibility(EVisibility::Collapsed)
			[
				SAssignNew(FlashTextWidget, STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 30))
				.ColorAndOpacity(FLinearColor::White)
				.ShadowOffset(FVector2D(2.0f, 0.0f))
				.ShadowColorAndOpacity(KomorebiHUDPalette::Cyan())
			]
		]

		// Right-Mid Combo Counter
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.Padding(FMargin(0.0f, 0.0f, 38.0f, 0.0f))
		[
			SAssignNew(ComboBox, SVerticalBox)
			.Visibility(EVisibility::Collapsed)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			[
				SAssignNew(ComboCountText, STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 44))
				.ColorAndOpacity(KomorebiHUDPalette::Yellow())
				.ShadowOffset(FVector2D(3.0f, 3.0f))
				.ShadowColorAndOpacity(KomorebiHUDPalette::Red())
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("HITS")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.ColorAndOpacity(KomorebiHUDPalette::Cyan())
			]
		]

		// Lower-Center Stack: Vehicle G Prompt, Toast Plate, Guard/Counter Status
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 118.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
			[
				SAssignNew(VehiclePromptBox, SBorder)
				.Visibility(EVisibility::Collapsed)
				.BorderImage(KomorebiHUDPalette::WhiteBrush())
				.BorderBackgroundColor(KomorebiHUDPalette::Yellow())
				.Padding(1.0f)
				[
					SNew(SBorder)
					.BorderImage(KomorebiHUDPalette::WhiteBrush())
					.BorderBackgroundColor(KomorebiHUDPalette::PanelBg())
					.Padding(FMargin(10.0f, 6.0f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))
						[
							SNew(SBorder)
							.BorderImage(KomorebiHUDPalette::WhiteBrush())
							.BorderBackgroundColor(KomorebiHUDPalette::Yellow())
							.Padding(FMargin(6.0f, 2.0f))
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("G")))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
								.ColorAndOpacity(KomorebiHUDPalette::DarkText())
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SAssignNew(VehiclePromptText, STextBlock)
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
							.ColorAndOpacity(KomorebiHUDPalette::Ink())
						]
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
			[
				SAssignNew(ToastBox, SBorder)
				.Visibility(EVisibility::Collapsed)
				.BorderImage(KomorebiHUDPalette::WhiteBrush())
				.BorderBackgroundColor(KomorebiHUDPalette::Yellow())
				.Padding(FMargin(3.0f, 1.0f, 1.0f, 1.0f))
				[
					SNew(SBorder)
					.BorderImage(KomorebiHUDPalette::WhiteBrush())
					.BorderBackgroundColor(KomorebiHUDPalette::PanelBg())
					.Padding(FMargin(14.0f, 8.0f))
					[
						SAssignNew(ToastTextWidget, STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
						.ColorAndOpacity(KomorebiHUDPalette::Ink())
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SAssignNew(CombatStatusBox, SBorder)
				.Visibility(EVisibility::Collapsed)
				.BorderImage(KomorebiHUDPalette::WhiteBrush())
				.BorderBackgroundColor(KomorebiHUDPalette::Edge())
				.Padding(1.0f)
				[
					SNew(SBorder)
					.BorderImage(KomorebiHUDPalette::WhiteBrush())
					.BorderBackgroundColor(KomorebiHUDPalette::PanelBg())
					.Padding(FMargin(10.0f, 5.0f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						[
							SAssignNew(CombatStatusText, STextBlock)
							.Text(FText::FromString(TEXT("GUARD")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
							.ColorAndOpacity(KomorebiHUDPalette::Cyan())
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
						[
							SNew(SBox)
							.WidthOverride(120.0f)
							[
								SAssignNew(GuardMeterBar, SKomorebiCellBarWidget)
								.Segments(10)
								.DesiredHeight(4.0f)
								.FillColor(KomorebiHUDPalette::Cyan())
							]
						]
					]
				]
			]
		];
}

void SKomorebiHUD::RefreshFromViewData(const FKomorebiHUDViewData& Data)
{
	CachedData = Data;

	const FLinearColor Accent = Data.ChapterAccent;
	const FLinearColor BorderAccent = Accent.CopyWithNewOpacity(0.45f);

	// 1. Damage Vignette
	if (DamageVignetteBorder.IsValid())
	{
		DamageVignetteBorder->SetBorderBackgroundColor(FLinearColor(1.0f, 0.165f, 0.427f, FMath::Clamp(Data.HitFeedbackAlpha * 0.35f, 0.0f, 0.35f)));
	}

	// 2. Projected 3D Markers
	if (MarkersLayer.IsValid())
	{
		MarkersLayer->UpdateMarkers(Data.WaypointMarker, Data.FocusMarker, Data.PeerNameplates);
	}

	// 3. Online Pill
	if (OnlinePillBox.IsValid() && OnlinePillText.IsValid())
	{
		OnlinePillBox->SetVisibility(Data.Core.bOnlineMode ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
		const FString PillStr = Data.OnlineStatusPill.IsEmpty()
			? FString::Printf(TEXT("ONLINE · %d"), FMath::Max(1, Data.OnlinePlayerCount))
			: Data.OnlineStatusPill.ToUpper();
		OnlinePillText->SetText(FText::FromString(PillStr));
	}

	// 4. Quest Plate
	if (QuestOuterBorder.IsValid()) QuestOuterBorder->SetBorderBackgroundColor(BorderAccent);
	if (QuestStripeBorder.IsValid()) QuestStripeBorder->SetBorderBackgroundColor(Accent);
	if (SectorLabelText.IsValid())
	{
		SectorLabelText->SetText(FText::FromString(Data.SectorLabel.ToUpper()));
		SectorLabelText->SetColorAndOpacity(Accent);
	}
	if (ChapterTitleText.IsValid())
	{
		ChapterTitleText->SetText(FText::FromString(Data.ChapterName.ToUpper()));
	}
	if (ObjectiveText.IsValid())
	{
		const FString Obj = Data.Core.ObjectiveTitle.IsEmpty()
			? TEXT("▸ PUSH THROUGH THE DISTRICT.")
			: FString::Printf(TEXT("▸ %s"), *Data.Core.ObjectiveTitle.ToUpper());
		ObjectiveText->SetText(FText::FromString(Obj));
	}

	// Mission Checklist rebuild only when steps change
	if (ChecklistBox.IsValid())
	{
		FString StepsKey;
		for (const FKomorebiMissionStepView& Step : Data.Core.Steps)
		{
			StepsKey += Step.bDone ? TEXT("1") : TEXT("0");
			StepsKey += Step.Label;
			StepsKey += TEXT("|");
		}
		if (StepsKey != LastChecklistKey)
		{
			LastChecklistKey = StepsKey;
			ChecklistBox->ClearChildren();
			bool bFoundActive = false;
			for (const FKomorebiMissionStepView& Step : Data.Core.Steps)
			{
				const bool bActive = !Step.bDone && !bFoundActive;
				if (bActive) bFoundActive = true;
				const TCHAR* Prefix = Step.bDone ? TEXT("■ ") : (bActive ? TEXT("▸ ") : TEXT("□ "));
				const FLinearColor Col = Step.bDone
					? Accent.CopyWithNewOpacity(0.80f)
					: (bActive ? KomorebiHUDPalette::Ink() : KomorebiHUDPalette::Muted());

				ChecklistBox->AddSlot()
					.AutoHeight()
					.Padding(FMargin(0.0f, 1.0f))
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.Text(FText::FromString(FString::Printf(TEXT("%s%s"), Prefix, *Step.Label)))
						.Font(FCoreStyle::GetDefaultFontStyle(bActive ? "Bold" : "Regular", 9))
						.ColorAndOpacity(Col)
					];
			}
		}
	}

	if (QuestTipText.IsValid())
	{
		const bool bHasTip = !Data.Core.ObjectiveTip.IsEmpty();
		QuestTipText->SetVisibility(bHasTip ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
		if (bHasTip)
		{
			QuestTipText->SetText(FText::FromString(Data.Core.ObjectiveTip));
		}
	}
	if (HostileCountText.IsValid()) HostileCountText->SetText(FText::FromString(Data.HostileCountLabel.ToUpper()));
	if (DistanceText.IsValid()) DistanceText->SetText(FText::FromString(Data.DistanceLabel.ToUpper()));
	if (CashText.IsValid()) CashText->SetText(FText::FromString(Data.Core.Cash > 0 ? FString::Printf(TEXT("$%d"), Data.Core.Cash) : TEXT("")));

	for (int32 i = 0; i < JourneyPips.Num(); ++i)
	{
		if (!JourneyPips[i].IsValid()) continue;
		if (i < Data.Core.ClearedSectors)
		{
			JourneyPips[i]->SetBorderBackgroundColor(Accent);
		}
		else if (i == Data.Core.ClearedSectors)
		{
			const float Blink = (FMath::Sin(Data.CriticalPulseTime * 5.0f) > 0.0f) ? 1.0f : 0.35f;
			JourneyPips[i]->SetBorderBackgroundColor(Accent.CopyWithNewOpacity(Blink));
		}
		else
		{
			JourneyPips[i]->SetBorderBackgroundColor(KomorebiHUDPalette::Edge());
		}
	}

	// 5. Minimap & Badges
	if (MinimapOuterBorder.IsValid()) MinimapOuterBorder->SetBorderBackgroundColor(Accent.CopyWithNewOpacity(0.65f));
	if (MinimapWidget.IsValid()) MinimapWidget->SetSnapshot(Data);
	if (MapZoneText.IsValid())
	{
		MapZoneText->SetText(FText::FromString(Data.MapZoneLabel.ToUpper()));
		MapZoneText->SetColorAndOpacity(Accent);
	}
	if (DayClockText.IsValid())
	{
		DayClockText->SetText(FText::FromString(Data.Core.DayClockLabel.ToUpper()));
		DayClockText->SetColorAndOpacity(Data.Core.bIsNight ? KomorebiHUDPalette::Cyan() : KomorebiHUDPalette::Yellow());
	}
	if (EchoBadgeBox.IsValid() && EchoBadgeText.IsValid())
	{
		EchoBadgeBox->SetVisibility(Data.bIsNeoKyoto ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
		const bool bComplete = Data.Core.EchoesCollected >= 12;
		const FLinearColor EchoCol = bComplete ? KomorebiHUDPalette::Yellow() : KomorebiHUDPalette::Mint();
		EchoBadgeBox->SetBorderBackgroundColor(EchoCol.CopyWithNewOpacity(0.55f));
		EchoBadgeText->SetColorAndOpacity(EchoCol);
		EchoBadgeText->SetText(FText::FromString(FString::Printf(TEXT("◈ %d/12 ECHOES"), Data.Core.EchoesCollected)));
	}

	// 6. Vitals
	const bool bCriticalHp = (Data.Core.Hp > 0.0f && Data.Core.Hp <= 30.0f);
	if (VitalsOuterBorder.IsValid())
	{
		VitalsOuterBorder->SetBorderBackgroundColor(bCriticalHp ? KomorebiHUDPalette::Red() : BorderAccent);
	}
	if (HpNumberText.IsValid())
	{
		HpNumberText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), FMath::Max(0, FMath::CeilToInt(Data.Core.Hp)), FMath::Max(1, FMath::RoundToInt(Data.Core.MaxHp)))));
	}
	if (PlayerHpBar.IsValid())
	{
		FLinearColor HpCol = KomorebiHUDPalette::Red();
		if (Data.bInvulnerable)
		{
			HpCol.A = 0.65f;
		}
		else if (bCriticalHp && FMath::Sin(Data.CriticalPulseTime * 12.0f) < 0.0f)
		{
			HpCol.A = 0.35f;
		}
		PlayerHpBar->SetValue(Data.Core.Hp / FMath::Max(1.0f, Data.Core.MaxHp), HpCol);
	}
	if (SparkPipsBar.IsValid())
	{
		SparkPipsBar->SetValue(Data.Core.SparkPower / 3.0f, KomorebiHUDPalette::Cyan());
	}

	// 7. Vehicle Speedometer & Prompt
	if (VehicleSpeedBox.IsValid())
	{
		VehicleSpeedBox->SetVisibility(Data.Core.bDriving ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
		if (Data.Core.bDriving)
		{
			if (VehicleKmhText.IsValid()) VehicleKmhText->SetText(FText::FromString(FString::Printf(TEXT("%d"), FMath::RoundToInt(FMath::Abs(Data.Core.VehicleKmh)))));
			if (VehicleNameText.IsValid()) VehicleNameText->SetText(FText::FromString(Data.Core.VehicleName.ToUpper()));
			if (VehicleStatusText.IsValid())
			{
				VehicleStatusText->SetText(FText::FromString(Data.VehicleStatusText.ToUpper()));
				VehicleStatusText->SetColorAndOpacity(Data.bVehicleLoose ? KomorebiHUDPalette::Red() : KomorebiHUDPalette::Muted());
			}
		}
	}
	if (VehiclePromptBox.IsValid() && VehiclePromptText.IsValid())
	{
		VehiclePromptBox->SetVisibility(Data.Core.bVehiclePromptVisible ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
		if (Data.Core.bVehiclePromptVisible)
		{
			VehiclePromptText->SetText(FText::FromString(Data.Core.VehiclePromptText.ToUpper()));
		}
	}

	// 8. Heat Stars
	if (HeatBannerBox.IsValid() && HeatStarsText.IsValid())
	{
		const bool bShowHeat = Data.Core.Heat > 0.0f || Data.Core.HeatStars > 0;
		HeatBannerBox->SetVisibility(bShowHeat ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
		if (bShowHeat)
		{
			const int32 Stars = FMath::Clamp(FMath::Max(1, Data.Core.HeatStars), 1, 5);
			FString Dots;
			for (int32 s = 0; s < Stars; ++s) Dots += TEXT("● ");
			HeatStarsText->SetText(FText::FromString(Dots.TrimEnd()));
		}
	}

	// 9. Target Lock Plate
	if (TargetPanelBox.IsValid())
	{
		const bool bShowTarget = Data.Core.bHasFocusTarget && (!Data.Core.bBossActive || Data.bTargetLocked);
		TargetPanelBox->SetVisibility(bShowTarget ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
		if (bShowTarget)
		{
			if (TargetOuterBorder.IsValid())
			{
				TargetOuterBorder->SetBorderBackgroundColor(Data.bTargetWarning ? KomorebiHUDPalette::Yellow() : KomorebiHUDPalette::Red().CopyWithNewOpacity(0.65f));
			}
			if (TargetNameText.IsValid())
			{
				TargetNameText->SetText(FText::FromString(Data.bTargetLocked
					? FString::Printf(TEXT("LOCKED · %s"), *Data.Core.FocusTargetName.ToUpper())
					: Data.Core.FocusTargetName.ToUpper()));
			}
			if (TargetSubText.IsValid())
			{
				const FString Sub = Data.TargetSubText.IsEmpty()
					? FString::Printf(TEXT("%d HP"), FMath::CeilToInt(Data.Core.FocusTargetHp))
					: Data.TargetSubText;
				TargetSubText->SetText(FText::FromString(Sub.ToUpper()));
			}
			if (TargetHpBar.IsValid())
			{
				TargetHpBar->SetValue(Data.Core.FocusTargetHp / FMath::Max(1.0f, Data.Core.FocusTargetMaxHp), KomorebiHUDPalette::Red());
			}
			if (TargetTellText.IsValid())
			{
				TargetTellText->SetText(FText::FromString(Data.TargetTell.ToUpper()));
				TargetTellText->SetColorAndOpacity(Data.bTargetWarning ? KomorebiHUDPalette::Yellow() : KomorebiHUDPalette::Muted());
			}
		}
	}

	// 10. Boss Duel Protocol Bar
	if (BossPanelBox.IsValid())
	{
		BossPanelBox->SetVisibility(Data.Core.bBossActive ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
		if (Data.Core.bBossActive)
		{
			const FLinearColor BossCol = Data.bBossOpening ? KomorebiHUDPalette::Cyan() : KomorebiHUDPalette::Red();
			if (BossOuterBorder.IsValid()) BossOuterBorder->SetBorderBackgroundColor(BossCol);
			if (BossNameText.IsValid()) BossNameText->SetText(FText::FromString(Data.Core.BossName.ToUpper()));
			if (BossHpText.IsValid()) BossHpText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), FMath::Max(0, FMath::CeilToInt(Data.Core.BossHp)), FMath::Max(1, FMath::RoundToInt(Data.Core.BossMaxHp)))));
			if (BossHpBar.IsValid()) BossHpBar->SetValue(Data.Core.BossHp / FMath::Max(1.0f, Data.Core.BossMaxHp), BossCol);
			if (BossCueText.IsValid())
			{
				BossCueText->SetText(FText::FromString(Data.Core.BossCue.ToUpper()));
				BossCueText->SetColorAndOpacity(Data.bBossOpening ? KomorebiHUDPalette::Cyan() : KomorebiHUDPalette::Yellow());
			}
			if (BossLessonText.IsValid()) BossLessonText->SetText(FText::FromString(Data.Core.BossLesson));
			if (BossStepReadText.IsValid())
			{
				BossStepReadText->SetText(FText::FromString(Data.bBossReadDone ? TEXT("■ READ") : TEXT("□ READ")));
				BossStepReadText->SetColorAndOpacity(Data.bBossReadDone ? KomorebiHUDPalette::Cyan() : KomorebiHUDPalette::Muted());
			}
			if (BossStepEvadeText.IsValid())
			{
				BossStepEvadeText->SetText(FText::FromString(Data.bBossEvaded ? TEXT("■ EVADE") : TEXT("□ EVADE")));
				BossStepEvadeText->SetColorAndOpacity(Data.bBossEvaded ? KomorebiHUDPalette::Cyan() : KomorebiHUDPalette::Muted());
			}
			if (BossStepPunishText.IsValid())
			{
				BossStepPunishText->SetText(FText::FromString(Data.bBossPunished ? TEXT("■ PUNISH") : TEXT("□ PUNISH")));
				BossStepPunishText->SetColorAndOpacity(Data.bBossPunished ? KomorebiHUDPalette::Cyan() : KomorebiHUDPalette::Muted());
			}
		}
	}

	// 11. Guard / Counter Combat Status
	if (CombatStatusBox.IsValid())
	{
		const bool bShowCombat = Data.Core.bGuarding || Data.Core.GuardMeter < 0.99f || Data.bCounterReady || Data.bGuardBroken;
		CombatStatusBox->SetVisibility(bShowCombat ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
		if (bShowCombat)
		{
			if (CombatStatusText.IsValid())
			{
				const TCHAR* Label = Data.bGuardBroken ? TEXT("GUARD BROKEN") : (Data.bCounterReady ? TEXT("COUNTER · STRIKE") : TEXT("GUARD"));
				CombatStatusText->SetText(FText::FromString(Label));
			}
			if (GuardMeterBar.IsValid())
			{
				GuardMeterBar->SetValue(Data.Core.GuardMeter, Data.bGuardBroken ? KomorebiHUDPalette::Red() : KomorebiHUDPalette::Cyan());
			}
		}
	}

	// 12. Voiced Radio Comms Plate
	if (CommsPanelBox.IsValid())
	{
		CommsPanelBox->SetVisibility(Data.bCommsVisible ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
		if (Data.bCommsVisible)
		{
			const FLinearColor SpCol = Data.Comms.AccentColor;
			if (CommsOuterBorder.IsValid()) CommsOuterBorder->SetBorderBackgroundColor(SpCol.CopyWithNewOpacity(0.75f));
			if (CommsPortraitBorder.IsValid()) CommsPortraitBorder->SetBorderBackgroundColor(SpCol);
			if (CommsMonogramText.IsValid())
			{
				const FString Mono = Data.Comms.SpeakerName.Left(2).ToUpper();
				CommsMonogramText->SetText(FText::FromString(Mono.IsEmpty() ? TEXT("07") : Mono));
				CommsMonogramText->SetColorAndOpacity(SpCol);
			}
			if (CommsSpeakerText.IsValid())
			{
				CommsSpeakerText->SetText(FText::FromString(Data.Comms.SpeakerName.ToUpper()));
				CommsSpeakerText->SetColorAndOpacity(SpCol);
			}
			if (CommsRoleBadge.IsValid()) CommsRoleBadge->SetBorderBackgroundColor(SpCol);
			if (CommsRoleText.IsValid()) CommsRoleText->SetText(FText::FromString(Data.Comms.SpeakerRole.ToUpper()));
			if (CommsLineText.IsValid()) CommsLineText->SetText(FText::FromString(Data.Comms.Line));
			if (CommsFreqText.IsValid()) CommsFreqText->SetText(FText::FromString(Data.Comms.Frequency.ToUpper()));
			if (CommsEqualizer.IsValid())
			{
				CommsEqualizer->UpdateAnim(Data.CommsAnimTime, Data.CommsVoiceLevel, Data.CommsTimeRemaining > 0.5f, SpCol);
			}
		}
	}

	// 13. Abilities Bar (0=Strike, 1=Spark, 2=Dash, 3=Heavy, 4=Flare, 5=Whirl)
	if (AbilitiesContainer.IsValid())
	{
		AbilitiesContainer->SetRenderOpacity(Data.Core.bDriving ? 0.30f : 1.0f);
	}
	for (int32 i = 0; i < AbilitySlots.Num(); ++i)
	{
		const bool bUnlocked = (i == 0) || Data.Core.bRoamMode || Data.Core.bOnlineMode || Data.bHasPower[i - 1];
		float Cool = 0.0f;
		if (i == 2) Cool = Data.Core.DashCooldown;
		else if (i == 4) Cool = Data.Core.FlareCooldown;

		const bool bEmptyCharge = ((i == 1 || i == 5) && Data.Core.SparkPower < 1.0f) || (i == 4 && Data.Core.SparkPower < 2.0f);
		const bool bCooling = bUnlocked && (Cool > 0.0f || bEmptyCharge);

		const FAbilitySlotWidgets& SlotW = AbilitySlots[i];
		if (SlotW.OuterBorder.IsValid())
		{
			const FLinearColor SlotBorder = !bUnlocked
				? KomorebiHUDPalette::Hex(TEXT("2B3A44"))
				: (bCooling ? KomorebiHUDPalette::Edge() : BorderAccent);
			SlotW.OuterBorder->SetBorderBackgroundColor(SlotBorder);
		}
		if (SlotW.KeyBadge.IsValid())
		{
			SlotW.KeyBadge->SetBorderBackgroundColor(bUnlocked ? KomorebiHUDPalette::Yellow() : KomorebiHUDPalette::Hex(TEXT("2B3A44")));
		}
		if (SlotW.KeyText.IsValid())
		{
			SlotW.KeyText->SetColorAndOpacity(bUnlocked ? KomorebiHUDPalette::DarkText() : KomorebiHUDPalette::Muted());
		}
		if (SlotW.NameText.IsValid())
		{
			SlotW.NameText->SetColorAndOpacity(!bUnlocked ? KomorebiHUDPalette::Hex(TEXT("51626C")) : (bCooling ? KomorebiHUDPalette::Muted() : KomorebiHUDPalette::Ink()));
		}
		if (SlotW.StatusText.IsValid())
		{
			FString Status;
			if (!bUnlocked)
			{
				Status = FString::Printf(TEXT("SEC 0%d"), i);
			}
			else if (Cool > 0.0f)
			{
				Status = FString::Printf(TEXT("%dS"), FMath::CeilToInt(Cool));
			}
			else if (bEmptyCharge)
			{
				Status = TEXT("NO CHARGE");
			}
			SlotW.StatusText->SetText(FText::FromString(Status));
		}
	}

	// 14. Combo Counter
	if (ComboBox.IsValid() && ComboCountText.IsValid())
	{
		const bool bShowCombo = Data.Core.ComboCount >= 2;
		ComboBox->SetVisibility(bShowCombo ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
		if (bShowCombo)
		{
			ComboCountText->SetText(FText::FromString(FString::Printf(TEXT("%d"), Data.Core.ComboCount)));
		}
	}

	// 15. Flash Text
	if (FlashBox.IsValid() && FlashTextWidget.IsValid())
	{
		const bool bShowFlash = Data.FlashAlpha > 0.01f && !Data.FlashText.IsEmpty();
		FlashBox->SetVisibility(bShowFlash ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
		if (bShowFlash)
		{
			FlashBox->SetRenderOpacity(FMath::Clamp(Data.FlashAlpha, 0.0f, 1.0f));
			FlashTextWidget->SetText(FText::FromString(Data.FlashText.ToUpper()));
		}
	}

	// 16. Toast Plate
	if (ToastBox.IsValid() && ToastTextWidget.IsValid())
	{
		const bool bShowToast = Data.ToastAlpha > 0.01f && !Data.ActiveToast.IsEmpty();
		ToastBox->SetVisibility(bShowToast ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
		if (bShowToast)
		{
			ToastBox->SetRenderOpacity(FMath::Clamp(Data.ToastAlpha, 0.0f, 1.0f));
			ToastTextWidget->SetText(FText::FromString(FString::Printf(TEXT("// %s"), *Data.ActiveToast)));
		}
	}

	// 17. Chapter Banner
	if (ChapterBannerBox.IsValid())
	{
		const bool bShowBanner = Data.bBannerVisible && Data.BannerAlpha > 0.01f && !Data.Core.bBossActive;
		ChapterBannerBox->SetVisibility(bShowBanner ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
		if (bShowBanner)
		{
			ChapterBannerBox->SetRenderOpacity(FMath::Clamp(Data.BannerAlpha, 0.0f, 1.0f));
			if (BannerSectorText.IsValid())
			{
				BannerSectorText->SetText(FText::FromString(Data.BannerSectorLine.ToUpper()));
				BannerSectorText->SetColorAndOpacity(Accent);
			}
			if (BannerTitleText.IsValid()) BannerTitleText->SetText(FText::FromString(Data.BannerTitle.ToUpper()));
			if (BannerSubtitleText.IsValid()) BannerSubtitleText->SetText(FText::FromString(Data.BannerSubtitle.ToUpper()));
		}
	}
}
