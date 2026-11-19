// Copyright Komorebi Project. 8-Player Online City GameMode for Listen Server and Dedicated Server (zero Node.js).
#pragma once

#include "CoreMinimal.h"
#include "Network/KomorebiGameMode.h"
#include "Game/DayNightCycle.h"
#include "Game/OnlineMeta.h"
#include "KomorebiOnlineGameMode.generated.h"

class AOnlineActivitiesActor;
class AKomorebiPlayerState;

DECLARE_MULTICAST_DELEGATE_TwoParams(FKomorebiOnServerSummonSiegeMech, const FVector& /*WorldLocation*/, const FString& /*DistrictName*/);

/**
 * Native UE5 8-player Online City GameMode supporting both Listen Server and KomorebiServer Dedicated Server.
 * - Enforces PreLogin cap (kMaxOnlinePlayers = 8) and separate Neo-Kyoto vs. Snowbound room pools.
 * - Keeps GlobalTimeDilation = 1.0f and disables pausing (online never freezes or slows time).
 * - Authoritatively manages District Contracts, Siege Mech world boss spawns (first 75s, every 180s, $200 assist pay),
 *   15s downed bleedout countdowns, and synchronized Day/Night clock replication.
 */
UCLASS()
class KOMOREBIGAME_API AKomorebiOnlineGameMode : public AKomorebiGameMode
{
	GENERATED_BODY()

public:
	AKomorebiOnlineGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual bool AllowPausing(APlayerController* PC = nullptr) override { return false; }
	virtual bool SetPause(APlayerController* PC, FCanUnpause CanUnpauseDelegate = FCanUnpause()) override { return false; }
	virtual void ToggleStoryAndRoamMode(AController* Requestor) override {}

	/** Called by server-authoritative crew AI whenever a shared crew member dies in SectorIndex (0..5). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Komorebi|Online")
	void NotifyCrewMemberKilled(int32 SectorIndex);

	/** Pays the $150 contract bonus and increments the `contracts` stat for every connected player in the room. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Komorebi|Online")
	void RewardRoomContract(int32 SectorIndex);

	/** Records that PlayerState damaged the active Siege Mech (players who hit it within 10s of death receive $200). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Komorebi|Online")
	void RegisterSiegeMechContributor(AKomorebiPlayerState* Contributor);

	/** Updates replicated Siege Mech health ratio [0..1] and location for all clients' HUDs. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Komorebi|Online")
	void UpdateSiegeMechTelemetry(const FVector& WorldLocation, float HealthRatio);

	/** Called when the active Siege Mech world boss falls; pays $200 + `mechs` stat to recent contributors. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Komorebi|Online")
	void NotifySiegeMechDefeated();

	/** Immediately summons the Siege Mech ahead of a random connected player along WaySamples(). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Komorebi|Online")
	void SummonSiegeMechWorldBoss();

	/** Returns the spawned Online Activities actor (Neo-Kyoto only). */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Online")
	AOnlineActivitiesActor* GetOnlineActivitiesActor() const { return OnlineActivitiesActor; }

	/** Broadcast on server when the Siege Mech spawns so the enemy spawner subsystem can instantiate the mech pawn. */
	FKomorebiOnServerSummonSiegeMech OnServerSummonSiegeMech;

private:
	void TickSyncedDayNight();
	void TickSiegeMechTimer(float DeltaSeconds);
	void TickDownedBleedouts(float DeltaSeconds);
	void InitializeDistrictContract(int32 SectorIndex);
	FString ResolveNearestDistrictName(double ThreeX, double ThreeZ) const;

	UPROPERTY()
	TObjectPtr<AOnlineActivitiesActor> OnlineActivitiesActor;

	KomorebiCore::DayNightController OnlineDayNight{ KomorebiCore::EDayPhase::Day, true };

	FString RoomCode;
	bool bPrivateRoom = false;

	float SiegeMechCooldown = static_cast<float>(KomorebiCore::kMechEventConfig.First);
	TMap<TWeakObjectPtr<AKomorebiPlayerState>, double> MechContributorHitTimes;
};
