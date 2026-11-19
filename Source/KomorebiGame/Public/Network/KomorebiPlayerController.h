// Copyright Komorebi Project. PlayerController bridging input focus, pause/showcase/day-night hotkeys, and Online City RPCs.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "KomorebiGameTypes.h"
#include "KomorebiPlayerController.generated.h"

class AKomorebiPlayerState;
class AKomorebiHUD;
class AKaelCharacter;
class AKomorebiVehiclePawn;
class ADayNightAtmosphereActor;
class AShowcaseCameraDirector;

/**
 * Primary PlayerController for solo, free-roam, and 8-player online play.
 * Manages cursor/input focus transitions, Pause/Showcase/DayNight/Mode hotkeys,
 * local-to-server profile synchronization, and authoritative Online City RPCs.
 */
UCLASS()
class KOMOREBIGAME_API AKomorebiPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AKomorebiPlayerController();

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void OnRep_PlayerState() override;

	// --- Local Flow Controls ---

	UFUNCTION(BlueprintCallable, Category = "Komorebi|UI")
	void TogglePauseMenu();

	UFUNCTION(BlueprintCallable, Category = "Komorebi|UI")
	void SetPauseMenuOpen(bool bOpen);

	UFUNCTION(BlueprintPure, Category = "Komorebi|UI")
	bool IsPauseMenuOpen() const;

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Showcase")
	void ToggleShowcaseTour();

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Showcase")
	void AdvanceShowcaseShot();

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Showcase")
	void ExitShowcaseTour();

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Atmosphere")
	void ToggleDayNightPhase();

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Flow")
	void ToggleSoloRoamMode();

	UFUNCTION(BlueprintCallable, Category = "Komorebi|UI")
	void DismissActiveComms();

	/** True if this world is currently running in 8-player Online City mode (never pauses or slows time). */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Online")
	bool IsOnlineCityMode() const;

	/** Synchronizes local nickname, title, color, and earned stats from GameInstance to the server PlayerState. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Online")
	void SyncLocalProfileToServer();

	// --- Server RPCs (Online City) ---

	/** Requests server-validated proximity revive of a downed teammate within 2.4 m. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Komorebi|Online")
	void ServerRequestRevive(AKomorebiPlayerState* DownedTarget);

	/** Submits a completed 12-gate street race time to the server for validation and room-best tracking. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Komorebi|Online")
	void ServerSubmitRaceTime(float ElapsedSeconds);

	/** Reports a blade strike landed on another player; server validates Duel Ring containment and applies damage. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Komorebi|Online")
	void ServerReportPvPHit(AKomorebiPlayerState* TargetPlayer, EKomorebiAttackType AttackType, FVector AttackerLocation);

	/** Requests entering or exiting a replicated vehicle pawn. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Komorebi|Online")
	void ServerRequestVehiclePossess(APawn* VehiclePawn, bool bEnterVehicle);

	/** Registers that this player damaged the active Siege Mech world boss (qualifies for $200 payout). */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Komorebi|Online")
	void ServerRegisterSiegeMechHit();

	// --- Client RPCs ---

	UFUNCTION(Client, Reliable, BlueprintCallable, Category = "Komorebi|UI")
	void ClientShowToast(const FString& Message);

	UFUNCTION(Client, Reliable, BlueprintCallable, Category = "Komorebi|UI")
	void ClientShowFlashText(const FString& Text);

	UFUNCTION(Client, Reliable, BlueprintCallable, Category = "Komorebi|UI")
	void ClientShowComms(const FKomorebiCommsMessage& Message);

	/** Returns the saved on-foot character pawn while this controller is possessing a vehicle. */
	APawn* GetCachedOnFootPawn() const { return CachedOnFootPawn.Get(); }

protected:
	void HandleEscapeKeyPressed();
	void HandlePauseHotkeyPressed();
	void HandleSpaceKeyPressed();
	void HandleDayNightKeyPressed();
	void HandleModeToggleKeyPressed();
	void HandleDismissCommsKeyPressed();

	bool IsEditingTextWidget() const;
	AKomorebiHUD* GetKomorebiHUD() const;

private:
	UPROPERTY()
	TWeakObjectPtr<APawn> CachedOnFootPawn;

	bool bProfileSyncedOnce = false;
};
