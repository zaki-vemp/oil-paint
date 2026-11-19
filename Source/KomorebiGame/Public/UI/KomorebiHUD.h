// Copyright Komorebi Project. AHUD host mounting pure Slate HUD, Pause Menu + 3-Tab Lore Codex, and Showcase Overlay.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "KomorebiGameTypes.h"
#include "KomorebiHUD.generated.h"

class SKomorebiHUD;
class SKomorebiPauseMenu;
class SKomorebiShowcaseOverlay;
class SWidget;
class AKaelCharacter;
class AKomorebiVehiclePawn;
class UMissionSubsystem;
class UKomorebiAudioSubsystem;
class UGraphicsSettingsSubsystem;
class ADayNightAtmosphereActor;
class AShowcaseCameraDirector;

enum class EKomorebiMinimapBlipKind : uint8
{
	Enemy = 0,
	Brute,
	Citizen,
	MissionTarget,
	Kiosk,
	Checkpoint,
	CheckpointUsed,
	NextObjective
};

struct FKomorebiMinimapBlip
{
	FVector2D ThreeXZ = FVector2D::ZeroVector;
	FLinearColor Color = FLinearColor(1.0f, 0.165f, 0.427f, 1.0f);
	EKomorebiMinimapBlipKind Kind = EKomorebiMinimapBlipKind::Enemy;
	bool bWarning = false;
	bool bMarked = false;
};

struct FKomorebiProjectedMarker
{
	FVector2D ScreenPos = FVector2D::ZeroVector;
	bool bVisible = false;
	bool bClamped = false;
	float ClampAngleDeg = 0.0f;
	FString Label;
	FString SubLabel;
	FLinearColor Color = FLinearColor(0.0f, 0.94f, 1.0f, 1.0f);
	bool bIsDowned = false;
	bool bIsDriving = false;
};

struct FKomorebiHUDViewData
{
	FKomorebiHUDState Core;

	// Styling & District context
	FLinearColor ChapterAccent = FLinearColor(0.0f, 0.94f, 1.0f, 1.0f);
	bool bIsNeoKyoto = true;
	FString SectorLabel = TEXT("SECTOR 01 / 06");
	FString ChapterName = TEXT("SUNLEAF ROW");
	FString HostileCountLabel = TEXT("00 / 00 HOSTILES DOWN");
	FString DistanceLabel;
	FString MapZoneLabel = TEXT("SCAN · DOWNTOWN");

	// Powers unlock state (Spark, Dash, Heavy, Flare, Whirl)
	bool bHasPower[5] = { false, false, false, false, false };

	// Combat defense & target lock details
	bool bGuardBroken = false;
	bool bCounterReady = false;
	bool bTargetLocked = false;
	bool bTargetWarning = false;
	FString TargetSubText;
	FString TargetTell;

	// Boss duel protocol details
	bool bBossOpening = false;
	bool bBossReadDone = false;
	bool bBossEvaded = false;
	bool bBossPunished = false;

	// Vehicle telemetry
	FString VehicleStatusText = TEXT("G · STEP OUT");
	bool bVehicleLoose = false;

	// Online room badge
	int32 OnlinePlayerCount = 1;
	FString OnlineStatusPill;

	// Minimap telemetry (Three.js metres + radians for 1:1 grid/way rendering)
	FVector2D PlayerThreeXZ = FVector2D::ZeroVector;
	float PlayerThreeYawRad = 3.14159265f;
	TArray<FKomorebiMinimapBlip> MinimapBlips;

	// Screen-space projected markers
	FKomorebiProjectedMarker WaypointMarker;
	FKomorebiProjectedMarker FocusMarker;
	TArray<FKomorebiProjectedMarker> PeerNameplates;

	// Radio Comms
	bool bCommsVisible = false;
	FKomorebiCommsMessage Comms;
	float CommsTimeRemaining = 0.0f;
	float CommsVoiceLevel = 0.0f;
	float CommsAnimTime = 0.0f;

	// Toast, Chapter Banner, Flash Text, Damage Vignette
	FString ActiveToast;
	float ToastAlpha = 0.0f;

	bool bBannerVisible = false;
	FString BannerSectorLine;
	FString BannerTitle;
	FString BannerSubtitle;
	float BannerAlpha = 0.0f;

	FString FlashText;
	float FlashAlpha = 0.0f;

	float HitFeedbackAlpha = 0.0f;
	float CriticalPulseTime = 0.0f;
	bool bInvulnerable = false;
};

/**
 * Pure Slate HUD host mounted onto GEngine->GameViewport.
 * Owns SKomorebiHUD, SKomorebiPauseMenu (with 3-tab Lore Codex), and SKomorebiShowcaseOverlay,
 * projects 3D waypoints, focus reticles, and remote player nameplates every frame,
 * and manages pause/cursor state, toasts, chapter banners, radio comms, and flash callouts.
 */
UCLASS()
class KOMOREBIGAME_API AKomorebiHUD : public AHUD
{
	GENERATED_BODY()

public:
	AKomorebiHUD();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	// --- Primary Gameplay HUD Push APIs ---

	/** Updates the core HUD snapshot from Kael, combat, missions, and atmosphere systems. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|HUD")
	void UpdateHUDState(const FKomorebiHUDState& NewState);

	/** Updates Boss Duel Protocol status (Read / Evade / Punish steps and opening state). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|HUD")
	void SetBossDuelDetails(bool bOpening, bool bReadDone, bool bEvaded, bool bPunished);

	/** Updates target lock panel details (locked flag, windup warning, tier/HP label, combat tell). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|HUD")
	void SetTargetDetails(bool bLocked, bool bWarning, const FString& SubText, const FString& Tell);

	/** Updates guard-break / counter readiness and invulnerability visual indicator. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|HUD")
	void SetCombatDefenseDetails(bool bGuardBroken, bool bCounterReady, bool bInvulnerable);

	/** Updates vehicle extra status line and loose/sliding blink flag. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|HUD")
	void SetVehicleTelemetryDetails(const FString& StatusText, bool bLoose);

	/** Replaces the live minimap entity blips (hostiles, citizens, mission targets, checkpoints). */
	void SetMinimapBlips(const TArray<FKomorebiMinimapBlip>& InBlips);

	/** Updates which echoes are currently unlocked for HUD badge and Pause Menu Codex. */
	void SetUnlockedEchoIds(const TSet<FString>& InUnlockedIds);

	/** Sets live radio voice audio amplitude [0..1] to animate the 7-bar comms equalizer. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|HUD")
	void SetCommsVoiceLevel(float Level01);

	// --- Notifications, Banners, Comms & Flash Callouts ---

	UFUNCTION(BlueprintCallable, Category = "Komorebi|HUD")
	void EnqueueToast(const FString& Message, float DurationSeconds = 3.5f);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|HUD")
	void ShowChapterBanner(int32 ChapterIndex, bool bNightOps);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|HUD")
	void ShowCustomChapterBanner(const FString& SectorLine, const FString& Title, const FString& Subtitle);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|HUD")
	void ShowFlashText(const FString& Text, float DurationSeconds = 0.9f);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|HUD")
	void ShowCommsMessage(const FKomorebiCommsMessage& Message);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|HUD")
	void ShowCommsBySpeakerId(const FString& SpeakerId, const FString& Line);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|HUD")
	void DismissComms();

	// --- Pause Menu & Investor Showcase Controls ---

	UFUNCTION(BlueprintCallable, Category = "Komorebi|UI")
	void SetPauseMenuOpen(bool bOpen);

	UFUNCTION(BlueprintCallable, Category = "Komorebi|UI")
	void TogglePauseMenu();

	UFUNCTION(BlueprintPure, Category = "Komorebi|UI")
	bool IsPauseMenuOpen() const { return bPauseMenuOpen; }

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Showcase")
	void SetShowcaseActive(bool bActive);

	UFUNCTION(BlueprintPure, Category = "Komorebi|Showcase")
	bool IsShowcaseActive() const { return bShowcaseActive; }

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Showcase")
	void UpdateShowcaseState(int32 ShotIndex, float ShotProgress01);

	void AdvanceShowcaseShotLocal();
	void SelectShowcaseShotLocal(int32 ShotIndex);

	const FKomorebiHUDViewData& GetHUDViewData() const { return ViewData; }

private:
	void MountViewportWidgets();
	void UnmountViewportWidgets();
	void RefreshAutoTelemetry(float DeltaSeconds);
	void UpdateProjectedMarkers();
	void UpdateTimersAndQueues(float DeltaSeconds);
	void RefreshPauseMenuBindings();

	FKomorebiHUDViewData ViewData;

	TSharedPtr<SKomorebiHUD> HUDWidget;
	TSharedPtr<SKomorebiPauseMenu> PauseMenuWidget;
	TSharedPtr<SKomorebiShowcaseOverlay> ShowcaseOverlayWidget;

	TSharedPtr<SWidget> HUDViewportContainer;
	TSharedPtr<SWidget> PauseViewportContainer;
	TSharedPtr<SWidget> ShowcaseViewportContainer;

	struct FQueuedToast
	{
		FString Message;
		float Duration = 3.5f;
	};
	TArray<FQueuedToast> ToastQueue;
	float ActiveToastRemaining = 0.0f;
	float ActiveToastDuration = 3.5f;

	float BannerTimeRemaining = 0.0f;
	float FlashTimeRemaining = 0.0f;
	float FlashDuration = 0.9f;
	float LastKnownHp = 100.0f;
	int32 LastDisplayedChapter = -1;

	bool bPauseMenuOpen = false;
	bool bShowcaseActive = false;
	int32 ShowcaseShotIndex = 0;
	float ShowcaseElapsed = 0.0f;
};
