// Copyright Komorebi Project. World subsystem driving 6 sector ops, relays, gates, Amber Sanctuary, 12 Echo Shrines, 8 kiosks, and radio comms.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "KomorebiGameTypes.h"
#include "Game/LevelsData.h"
#include "Game/StoryDatabase.h"
#include "World/NeoKyotoTerrain.h"

#include <functional>

#include "MissionSubsystem.generated.h"

class AActor;
class UBoxComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UKomorebiSaveGame;

USTRUCT(BlueprintType)
struct KOMOREBIGAME_API FKomorebiMinimapMarker
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Mission")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Mission")
	FString Kind;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Mission")
	FLinearColor Color = FLinearColor::White;
};

USTRUCT(BlueprintType)
struct KOMOREBIGAME_API FKomorebiMissionHUDData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Mission")
	FString OpTitle;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Mission")
	TArray<FKomorebiMissionStepView> Steps;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Mission")
	FString Tip;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Mission")
	bool bHasWaypoint = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Mission")
	FVector WaypointWorldLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Mission")
	FString WaypointLabel;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Mission")
	FLinearColor WaypointColor = FLinearColor(1.0f, 0.165f, 0.427f, 1.0f);

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Mission")
	TArray<FKomorebiMinimapMarker> MinimapMarkers;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Mission")
	int32 EchoesCollected = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Mission")
	int32 EchoesTotal = 12;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Mission")
	bool bHasActiveComms = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Mission")
	FKomorebiCommsMessage ActiveComms;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Mission")
	bool bContractActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Mission")
	float ContractTimeRemaining = 0.0f;
};

USTRUCT(BlueprintType)
struct KOMOREBIGAME_API FKomorebiSectorCombatSnapshot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Mission")
	int32 AliveCrewCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Mission")
	int32 TotalCrewCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Mission")
	bool bHasNearestCrew = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Mission")
	FVector NearestCrewLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Mission")
	bool bRivalAlive = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Mission")
	FVector RivalLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Mission")
	FString RivalName;
};

USTRUCT(BlueprintType)
struct KOMOREBIGAME_API FKomorebiMissionWorldContext
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Mission")
	bool bValid = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Mission")
	FVector PlayerLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Mission")
	float PlayerHp = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Mission")
	bool bDriving = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Mission")
	float Heat = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Mission")
	bool bBossEngaged = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Mission")
	bool bCourierAlive = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Mission")
	FVector CourierLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Mission")
	TArray<FKomorebiSectorCombatSnapshot> Sectors;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMissionToast, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMissionFlashText, const FString&, Text);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCommsMessageStarted, const FKomorebiCommsMessage&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMissionRewardCash, int32, Amount, const FString&, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMissionHealPlayer, float, HealAmount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMissionReduceHeat, float, HeatDelta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCheckpointActivated, int32, SectorIndex, FVector, RespawnWorldLocation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSectorGateOpened, int32, SectorIndex, FString, UnlockedPowerName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCampaignWon);

/**
 * Manages the full Neo-Kyoto campaign structure ("Komorebi: Protocol Zero-Seven"):
 * 6 Sector Field Operations, 6 Checkpoint Relays, 5 Sector Barrier Gates, Amber Sanctuary Finale Terminal,
 * 12 Collectible Memory Echo Shrines, 8 Repeatable $150 Street Contract Kiosks, and sequenced COMMS_CHAINS.
 */
UCLASS()
class KOMOREBIGAME_API UMissionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	// --- Setup & Configuration -----------------------------------------------------------------

	/** Initializes or rebuilds all world mission props, relays, gates, shrines, and kiosks. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Mission")
	void InitializeCampaign(EKomorebiPlayMode PlayMode = EKomorebiPlayMode::StorySectors, bool bFreshCampaign = false);

	/** Assigns optional custom imported meshes by asset key (terminal, beacon, power-core, supply-crate, barricade, echo-shrine, relay). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Mission")
	void SetPropMeshOverride(FName AssetKey, UStaticMesh* Mesh);

	/** Assigns optional custom additive glow material for rings and beacon beams. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Mission")
	void SetGlowMaterialOverride(UMaterialInterface* GlowMat);

	/** Injects custom terrain height query in Three.js metres (defaults to KomorebiCore::NeoKyotoTerrainMap). */
	void SetTerrainHeightProvider(TFunction<double(double, double)> InHeightProvider);

	// --- Gameplay Integration API --------------------------------------------------------------

	/** Supplies live combat/crew/rival/courier snapshot from the combat manager each frame. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Mission")
	void UpdateWorldContext(const FKomorebiMissionWorldContext& Context);

	/** Marks Sector 1's marked syndicate courier as intercepted/defeated. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Mission")
	void NotifyCourierDefeated();

	/** Updates Sector 1 courier world location while alive. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Mission")
	void UpdateCourierLocation(FVector WorldLocation, bool bAlive);

	/** Applies melee, heavy, or vehicle ram damage to destructible sector or contract targets. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Mission")
	bool HitAt(FVector WorldLocation, float RadiusMeters, float Damage, FName Source = TEXT("sword"));

	/** Applies Spark bolt damage to destructible targets (includes 2.2m vertical tolerance). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Mission")
	bool HitBolt(FVector WorldLocation, float RadiusMeters, float Damage);

	/** Marks a sector's field operation complete (e.g., via debug command or sync). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Mission")
	void CompleteSectorFieldOp(int32 SectorIndex);

	/**
	 * Checks if SectorIndex's field operation, crew, and Ren duel are all finished.
	 * If so, opens its barrier gate, unlocks its power, persists save, and triggers clear comms.
	 */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Mission")
	bool TryAdvanceSectorClear(int32 SectorIndex, bool bCrewCleared, bool bRivalDefeated);

	/** Checks whether a world location lies past the first still-closed Sector Barrier Gate. */
	UFUNCTION(BlueprintPure, Category = "Komorebi|Mission")
	bool IsLocationPastClosedGate(FVector WorldLocation) const;

	/** Clamps an attempted teleport back in front of the first closed gate (returns true if clamped). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Mission")
	bool EnforceClosedGateBoundary(FVector& InOutWorldLocation);

	/** Enqueues a primary radio comms key plus its entire COMMS_CHAINS multi-speaker sequence. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Mission")
	void SpeakComms(const FString& CommKey);

	/** Enqueues a direct single-speaker radio line (used for Echo Shards and custom beats). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Mission")
	void QueueDirectComms(const FString& SpeakerId, const FString& Line, float MinDelaySeconds = 4.8f);

	/** Keeps mission props and echo shrines visible around a remote showcase camera. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Mission")
	void RevealAroundShowcaseCamera(FVector FocusWorldLocation, FVector CameraWorldLocation);

	// --- Queries & HUD Export ------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Komorebi|Mission")
	FKomorebiMissionHUDData GetMissionHUDData() const;

	UFUNCTION(BlueprintCallable, Category = "Komorebi|Mission")
	void PopulateHUDState(UPARAM(ref) FKomorebiHUDState& InOutHud) const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|Mission")
	bool IsSectorFieldOpDone(int32 SectorIndex) const;

	UFUNCTION(BlueprintPure, Category = "Komorebi|Mission")
	int32 GetClearedSectors() const { return ClearedSectors; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Mission")
	bool IsCampaignWon() const { return bWon; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Mission")
	int32 GetCollectedEchoCount() const { return CollectedEchoIds.Num(); }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Mission")
	TArray<FString> GetCollectedEchoIds() const { return CollectedEchoIds.Array(); }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Mission")
	FVector GetActiveRespawnWorldLocation() const { return ActiveRespawnLocation; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Mission")
	EKomorebiPlayMode GetPlayMode() const { return CurrentPlayMode; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Mission")
	bool IsContractActive() const { return bHasActiveContract; }

	UFUNCTION(BlueprintPure, Category = "Komorebi|Mission")
	UKomorebiSaveGame* GetActiveSaveGame() const { return ActiveSaveGame; }

	// --- Delegates -----------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Mission")
	FOnMissionToast OnToast;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Mission")
	FOnMissionFlashText OnFlashText;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Mission")
	FOnCommsMessageStarted OnCommsMessageStarted;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Mission")
	FOnMissionRewardCash OnRewardCash;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Mission")
	FOnMissionHealPlayer OnHealPlayer;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Mission")
	FOnMissionReduceHeat OnReduceHeat;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Mission")
	FOnCheckpointActivated OnCheckpointActivated;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Mission")
	FOnSectorGateOpened OnSectorGateOpened;

	UPROPERTY(BlueprintAssignable, Category = "Komorebi|Mission")
	FOnCampaignWon OnCampaignWon;

private:
	enum class ETargetKind : uint8
	{
		Destructible,
		Ring,
		Courier,
		Terminal
	};

	struct FSpawnedVisual
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<UStaticMeshComponent> ModelComp;
		TWeakObjectPtr<UStaticMeshComponent> RingComp;
		TWeakObjectPtr<UStaticMeshComponent> SecondaryRingComp;
		TWeakObjectPtr<UStaticMeshComponent> BeamComp;
		TWeakObjectPtr<UStaticMeshComponent> SecondaryBeamComp;
		TWeakObjectPtr<UBoxComponent> ColliderComp;
		TWeakObjectPtr<UPointLightComponent> LightComp;

		void SetVisible(bool bVis) const;
		void DisableCollider() const;
		void DestroyActor();
	};

	struct FMissionTargetRuntime
	{
		ETargetKind Kind = ETargetKind::Destructible;
		FString Label;
		int32 Index = 0;
		double ThreeX = 0.0;
		double ThreeY = 0.0;
		double ThreeZ = 0.0;
		bool bDone = false;
		float Hp = 55.0f;
		float MaxHp = 55.0f;
		float SyncProgress = 0.0f;
		float Radius = 1.5f;
		float HitFlashTimer = 0.0f;
		float RamCooldown = 0.0f;
		FSpawnedVisual Visual;
	};

	struct FSectorOpRuntime
	{
		int32 Seg = 0;
		FString Type;
		FString OpTitle;
		FString StepLabel;
		FString Tip;
		bool bDone = false;
		bool bAnnounced = false;
		TArray<FMissionTargetRuntime> Targets;
	};

	struct FEchoShrineRuntime
	{
		FString Id;
		int32 Seg = 0;
		FString Title;
		FString Speaker;
		FString Quote;
		double ThreeX = 0.0;
		double ThreeY = 0.0;
		double ThreeZ = 0.0;
		bool bCollected = false;
		float PhaseOffset = 0.0f;
		FSpawnedVisual Visual;
	};

	struct FKioskRuntime
	{
		FString Id;
		FString Name;
		FString Kind;
		double ThreeX = 0.0;
		double ThreeY = 0.0;
		double ThreeZ = 0.0;
		int32 DirX = 0;
		int32 DirZ = 0;
		float Cooldown = 0.0f;
		FSpawnedVisual Visual;
	};

	struct FActiveContractRuntime
	{
		int32 KioskIndex = INDEX_NONE;
		FString Kind;
		FString Name;
		float TimeLeft = 75.0f;
		TArray<FMissionTargetRuntime> Targets;
	};

	struct FRelayRuntime
	{
		int32 SectorIndex = 0;
		FString Name;
		bool bIsGoal = false;
		double ThreeX = 0.0;
		double ThreeY = 0.0;
		double ThreeZ = 0.0;
		bool bUsed = false;
		float Cooldown = 0.0f;
		FSpawnedVisual Visual;
	};

	struct FBarrierGateRuntime
	{
		int32 SectorIndex = 0;
		double ThreeX = 0.0;
		double ThreeY = 0.0;
		double ThreeZ = 0.0;
		bool bOpen = false;
		float OpenAnim = 0.0f;
		FSpawnedVisual Visual;
	};

	struct FQueuedCommItem
	{
		FString SpeakerId;
		FString Line;
		float Delay = 4.8f;
	};

	// --- Helpers -------------------------------------------------------------------------------

	double SampleTerrainHeight(double ThreeX, double ThreeZ) const;
	FVector ThreeToUE(double ThreeX, double ThreeY, double ThreeZ) const;
	FVector GetLivePlayerLocation() const;

	void ClearSpawnedWorldActors();
	void BuildSectorOperations();
	void BuildEchoShrines();
	void BuildContractKiosks();
	void BuildRelaysAndSanctuary();
	void BuildBarrierGates();

	FSpawnedVisual SpawnPropVisual(
		FName AssetKey,
		double ThreeX,
		double ThreeZ,
		const FLinearColor& Accent,
		float RingRadiusMeters,
		float BeamHeightMeters,
		bool bWithCollider,
		float PropHeightMeters = 1.8f);

	FSpawnedVisual SpawnRingGateVisual(
		double ThreeX,
		double ThreeZ,
		double ThreeYawRad,
		const FLinearColor& Accent);

	FSpawnedVisual SpawnEchoShrineVisual(double ThreeX, double ThreeZ, bool bInitiallyVisible);
	FSpawnedVisual SpawnBarrierGateVisual(double ThreeX, double ThreeZ);
	FSpawnedVisual SpawnRelayVisual(double ThreeX, double ThreeZ, bool bIsGoal);

	UStaticMesh* ResolveMesh(FName AssetKey, const TCHAR* FallbackEngineMeshPath) const;
	UMaterialInstanceDynamic* CreateEmissiveMaterial(UObject* Outer, const FLinearColor& Color, float EmissiveBoost = 2.0f) const;

	void AdvanceCommsQueue(float DeltaTime);
	void DispatchCommsMessage(const FQueuedCommItem& Item);
	FKomorebiCommsMessage BuildCommsMessage(const FString& SpeakerId, const FString& Line) const;

	bool DamageTarget(FMissionTargetRuntime& Target, float Damage, FName Source, int32 SectorIndexOrNegOne);
	void CheckSectorOpCompletion(int32 Seg);
	void StartContract(int32 KioskIndex);
	void FinishContract(bool bSuccess);
	void UpdateRelaysAndGates(float DeltaTime, double PlayerThreeX, double PlayerThreeZ, float PlayerHp);
	void PersistCampaignProgress();
	void RebuildCachedHUDData(double PlayerThreeX, double PlayerThreeZ);

	void PlayAudioSfx(EKomorebiSfx Sfx, float Intensity = 1.0f) const;
	void SpeakAudioLine(const FString& Line, const FString& SpeakerId) const;

	// --- State ---------------------------------------------------------------------------------

	bool bCampaignInitialized = false;
	EKomorebiPlayMode CurrentPlayMode = EKomorebiPlayMode::StorySectors;
	int32 ClearedSectors = 0;
	bool bWon = false;
	bool bRunTold = false;
	bool bCourierDefeated = false;
	float SyncSoundTimer = 0.0f;
	float GateToastCooldown = 0.0f;
	double TotalElapsedTime = 0.0;

	FVector ActiveRespawnLocation = FVector::ZeroVector;
	FKomorebiMissionWorldContext WorldContext;
	FKomorebiMissionHUDData CachedHUDData;

	UPROPERTY(Transient)
	TObjectPtr<UKomorebiSaveGame> ActiveSaveGame = nullptr;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UStaticMesh>> PropMeshOverrides;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> GlowMaterialOverride = nullptr;

	TSet<int32> DoneSectorOps;
	TSet<int32> AnnouncedFieldClearSectors;
	TSet<FString> CollectedEchoIds;

	TArray<FSectorOpRuntime> SectorOps;
	TArray<FEchoShrineRuntime> EchoShrines;
	TArray<FKioskRuntime> Kiosks;
	TArray<FRelayRuntime> Relays;
	TArray<FBarrierGateRuntime> Gates;

	bool bHasActiveContract = false;
	FActiveContractRuntime ActiveContract;

	TArray<FQueuedCommItem> CommQueue;
	float CommHoldTimer = 0.0f;
	bool bHasActiveComms = false;
	FKomorebiCommsMessage CurrentComms;

	KomorebiCore::NeoKyotoTerrainMap FallbackTerrain;
	TFunction<double(double, double)> CustomHeightProvider;
};
