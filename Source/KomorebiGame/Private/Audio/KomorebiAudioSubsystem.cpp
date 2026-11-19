// Copyright Komorebi Project. GameInstance audio subsystem wrapping the procedural DSP synth and persistent mix settings.
#include "Audio/KomorebiAudioSubsystem.h"

#include "Campaign/KomorebiSaveGame.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogKomorebiAudio, Log, All);

void UKomorebiAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Load persisted audio mix from SaveGame (falls back to KomorebiCore::DefaultAudioSettings).
	if (UKomorebiSaveGame* Save = UKomorebiSaveManager::LoadOrCreate())
	{
		CurrentSettings = FKomorebiAudioSettings::FromCore(Save->ToCoreAudioSettings());
	}
	else
	{
		CurrentSettings = FKomorebiAudioSettings::FromCore(KomorebiCore::DefaultAudioSettings());
	}

	PostWorldInitHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(
		this, &UKomorebiAudioSubsystem::HandlePostWorldInit);
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(
		this, &UKomorebiAudioSubsystem::HandleWorldCleanup);

	if (UWorld* World = GetWorld())
	{
		EnsureSynthReady(World);
	}
}

void UKomorebiAudioSubsystem::Deinitialize()
{
	if (PostWorldInitHandle.IsValid())
	{
		FWorldDelegates::OnPostWorldInitialization.Remove(PostWorldInitHandle);
		PostWorldInitHandle.Reset();
	}
	if (WorldCleanupHandle.IsValid())
	{
		FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
		WorldCleanupHandle.Reset();
	}

	TeardownSynthHost();
	Super::Deinitialize();
}

void UKomorebiAudioSubsystem::HandlePostWorldInit(UWorld* World, const UWorld::InitializationValues /*IVS*/)
{
	if (World && World->IsGameWorld() && World->GetGameInstance() == GetGameInstance())
	{
		EnsureSynthReady(World);
	}
}

void UKomorebiAudioSubsystem::HandleWorldCleanup(UWorld* World, bool /*bSessionEnded*/, bool /*bCleanupResources*/)
{
	if (SynthHostActor.IsValid() && SynthHostActor->GetWorld() == World)
	{
		TeardownSynthHost();
	}
}

void UKomorebiAudioSubsystem::EnsureSynthReady(UWorld* TargetWorld)
{
	if (IsRunningDedicatedServer())
	{
		return;
	}

	UWorld* World = TargetWorld ? TargetWorld : GetWorld();
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (SynthComponent.IsValid() && SynthHostActor.IsValid() && SynthHostActor->GetWorld() == World)
	{
		if (!SynthComponent->IsPlaying())
		{
			SynthComponent->Start();
		}
		return;
	}

	TeardownSynthHost();

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags = RF_Transient;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* Host = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!Host)
	{
		UE_LOG(LogKomorebiAudio, Warning, TEXT("Could not spawn transient audio host actor in world '%s'."), *World->GetName());
		return;
	}
	Host->SetActorTickEnabled(false);

	UKomorebiSynthComponent* Synth = NewObject<UKomorebiSynthComponent>(Host, TEXT("KomorebiProceduralSynth"));
	if (!Synth)
	{
		Host->Destroy();
		return;
	}

	Host->SetRootComponent(Synth);
	Synth->RegisterComponent();

	SynthHostActor = Host;
	SynthComponent = Synth;

	PushAllStateToSynth();
	Synth->Start();
}

void UKomorebiAudioSubsystem::TeardownSynthHost()
{
	if (SynthComponent.IsValid())
	{
		SynthComponent->StopFormantVoice();
		SynthComponent->Stop();
		SynthComponent.Reset();
	}
	if (SynthHostActor.IsValid())
	{
		SynthHostActor->Destroy();
		SynthHostActor.Reset();
	}
}

void UKomorebiAudioSubsystem::PushAllStateToSynth()
{
	if (!SynthComponent.IsValid())
	{
		return;
	}

	SynthComponent->SetMixSettings(CurrentSettings.ToCore());
	SynthComponent->SetMusicState(
		CachedChapter,
		CachedCombat,
		bCachedBoss,
		bCachedSnowbound,
		bCachedPaused,
		bCachedDriving,
		CachedSpeedMps);
}

void UKomorebiAudioSubsystem::PersistSettingsToSaveGame()
{
	UKomorebiSaveGame* Save = UKomorebiSaveManager::LoadOrCreate();
	if (!Save)
	{
		return;
	}

	Save->ApplyCoreAudioSettings(CurrentSettings.ToCore());
	FString WarningToast;
	if (!UKomorebiSaveManager::SaveWithToastCheck(Save, WarningToast) && !WarningToast.IsEmpty())
	{
		OnAudioSaveWarning.Broadcast(WarningToast);
	}
}

// --- Public Playback API -----------------------------------------------------------------------

void UKomorebiAudioSubsystem::PlaySfx(EKomorebiSfx Sfx, float Intensity)
{
	if (CurrentSettings.bMuted)
	{
		return;
	}
	EnsureSynthReady();
	if (SynthComponent.IsValid())
	{
		SynthComponent->TriggerSfx(Sfx, Intensity);
	}
}

void UKomorebiAudioSubsystem::PlaySfxByName(FName SfxName, float Intensity)
{
	const FString Lower = SfxName.ToString().ToLower();
	EKomorebiSfx Resolved = EKomorebiSfx::UiClick;

	if (Lower == TEXT("slash") || Lower == TEXT("swordswing"))          Resolved = EKomorebiSfx::SwordSwing;
	else if (Lower == TEXT("heavy") || Lower == TEXT("heavyswing"))     Resolved = EKomorebiSfx::HeavySwing;
	else if (Lower == TEXT("hit") || Lower == TEXT("hitimpact"))        Resolved = EKomorebiSfx::HitImpact;
	else if (Lower == TEXT("parry") || Lower == TEXT("parryring"))      Resolved = EKomorebiSfx::ParryRing;
	else if (Lower == TEXT("dodge"))                                    Resolved = EKomorebiSfx::Dodge;
	else if (Lower == TEXT("deflect"))                                  Resolved = EKomorebiSfx::Deflect;
	else if (Lower == TEXT("spark") || Lower == TEXT("sparkbolt"))      Resolved = EKomorebiSfx::SparkBolt;
	else if (Lower == TEXT("dash") || Lower == TEXT("dashwhoosh"))      Resolved = EKomorebiSfx::DashWhoosh;
	else if (Lower == TEXT("flare") || Lower == TEXT("flarepulse"))     Resolved = EKomorebiSfx::FlarePulse;
	else if (Lower == TEXT("whirl") || Lower == TEXT("whirlslash"))     Resolved = EKomorebiSfx::WhirlSlash;
	else if (Lower == TEXT("plunge") || Lower == TEXT("plungeimpact"))  Resolved = EKomorebiSfx::PlungeImpact;
	else if (Lower == TEXT("bolt") || Lower == TEXT("enemybolt"))       Resolved = EKomorebiSfx::EnemyBolt;
	else if (Lower == TEXT("lock") || Lower == TEXT("aimlock"))         Resolved = EKomorebiSfx::AimLock;
	else if (Lower == TEXT("playerhurt") || Lower == TEXT("hurt"))      Resolved = EKomorebiSfx::PlayerHurt;
	else if (Lower == TEXT("guilt") || Lower == TEXT("guiltalarm"))     Resolved = EKomorebiSfx::GuiltAlarm;
	else if (Lower == TEXT("step"))                                     Resolved = EKomorebiSfx::Step;
	else if (Lower == TEXT("enterride") || Lower == TEXT("entervehicle")) Resolved = EKomorebiSfx::EnterVehicle;
	else if (Lower == TEXT("exitride") || Lower == TEXT("exitvehicle")) Resolved = EKomorebiSfx::ExitVehicle;
	else if (Lower == TEXT("crash") || Lower == TEXT("vehiclecrash"))   Resolved = EKomorebiSfx::VehicleCrash;
	else if (Lower == TEXT("horn") || Lower == TEXT("vehiclehorn"))     Resolved = EKomorebiSfx::VehicleHorn;
	else if (Lower == TEXT("objhit") || Lower == TEXT("objecthit"))     Resolved = EKomorebiSfx::ObjectHit;
	else if (Lower == TEXT("objbreak") || Lower == TEXT("objectbreak")) Resolved = EKomorebiSfx::ObjectBreak;
	else if (Lower == TEXT("synctick"))                                 Resolved = EKomorebiSfx::SyncTick;
	else if (Lower == TEXT("missionstep"))                              Resolved = EKomorebiSfx::MissionStep;
	else if (Lower == TEXT("missiondone"))                              Resolved = EKomorebiSfx::MissionDone;
	else if (Lower == TEXT("gateopen"))                                 Resolved = EKomorebiSfx::GateOpen;
	else if (Lower == TEXT("echocollect"))                              Resolved = EKomorebiSfx::EchoCollect;
	else if (Lower == TEXT("uiclick"))                                  Resolved = EKomorebiSfx::UiClick;
	else
	{
		return;
	}

	PlaySfx(Resolved, Intensity);
}

void UKomorebiAudioSubsystem::PlayBark(EKomorebiBarkKind Bark)
{
	if (CurrentSettings.bMuted)
	{
		return;
	}
	EnsureSynthReady();
	if (SynthComponent.IsValid())
	{
		SynthComponent->TriggerBark(Bark);
	}
}

void UKomorebiAudioSubsystem::SpeakRadioLine(
	const FString& Text,
	const FString& SpeakerId,
	bool bInterrupt,
	bool bSquelch)
{
	if (Text.IsEmpty())
	{
		return;
	}
	EnsureSynthReady();
	if (SynthComponent.IsValid())
	{
		SynthComponent->SpeakFormantLine(Text, SpeakerId, bInterrupt, bSquelch);
	}
}

void UKomorebiAudioSubsystem::StopRadioVoice()
{
	if (SynthComponent.IsValid())
	{
		SynthComponent->StopFormantVoice();
	}
}

void UKomorebiAudioSubsystem::UpdateMusicState(
	int32 Chapter,
	bool bCombat,
	bool bBoss,
	bool bSnowbound,
	bool bPaused)
{
	UpdateMusicStateDetailed(
		Chapter,
		bCombat ? 1.0f : 0.0f,
		bBoss,
		bSnowbound,
		bPaused,
		bCachedDriving,
		CachedSpeedMps);
}

void UKomorebiAudioSubsystem::UpdateMusicStateDetailed(
	int32 Chapter,
	float CombatIntensity,
	bool bBoss,
	bool bSnowbound,
	bool bPaused,
	bool bDriving,
	float SpeedMps)
{
	CachedChapter = FMath::Clamp(Chapter, 0, 5);
	CachedCombat = FMath::Clamp(CombatIntensity, 0.0f, 1.0f);
	bCachedBoss = bBoss;
	bCachedSnowbound = bSnowbound;
	bCachedPaused = bPaused;
	bCachedDriving = bDriving;
	CachedSpeedMps = FMath::Abs(SpeedMps);

	EnsureSynthReady();
	if (SynthComponent.IsValid())
	{
		SynthComponent->SetMusicState(
			CachedChapter,
			CachedCombat,
			bCachedBoss,
			bCachedSnowbound,
			bCachedPaused,
			bCachedDriving,
			CachedSpeedMps);
	}
}

void UKomorebiAudioSubsystem::UpdateRideAudio(
	bool bDriving,
	EKomorebiVehicleKind Kind,
	float SpeedMps,
	float TopSpeedMps,
	float Throttle,
	bool bSliding,
	bool bAirborne,
	bool bPaused)
{
	bCachedDriving = bDriving;
	CachedSpeedMps = FMath::Abs(SpeedMps);

	EnsureSynthReady();
	if (SynthComponent.IsValid())
	{
		SynthComponent->SetRideAudioState(bDriving, Kind, SpeedMps, TopSpeedMps, Throttle, bSliding, bAirborne, bPaused);
		SynthComponent->SetMusicState(
			CachedChapter,
			CachedCombat,
			bCachedBoss,
			bCachedSnowbound,
			bCachedPaused || bPaused,
			bCachedDriving,
			CachedSpeedMps);
	}
}

// --- Settings Management -----------------------------------------------------------------------

FKomorebiAudioSettings UKomorebiAudioSubsystem::SetAudioSettings(const FKomorebiAudioSettings& NewSettings, bool bPersist)
{
	CurrentSettings = FKomorebiAudioSettings::FromCore(NewSettings.ToCore());
	if (SynthComponent.IsValid())
	{
		SynthComponent->SetMixSettings(CurrentSettings.ToCore());
	}
	if (bPersist)
	{
		PersistSettingsToSaveGame();
	}
	OnAudioSettingsChanged.Broadcast(CurrentSettings);
	return CurrentSettings;
}

void UKomorebiAudioSubsystem::SetMasterVolume(float Value)
{
	FKomorebiAudioSettings Next = CurrentSettings;
	Next.Master = Value;
	SetAudioSettings(Next, true);
}

void UKomorebiAudioSubsystem::SetMusicVolume(float Value)
{
	FKomorebiAudioSettings Next = CurrentSettings;
	Next.Music = Value;
	SetAudioSettings(Next, true);
}

void UKomorebiAudioSubsystem::SetVoiceVolume(float Value)
{
	FKomorebiAudioSettings Next = CurrentSettings;
	Next.Voice = Value;
	SetAudioSettings(Next, true);
}

void UKomorebiAudioSubsystem::SetSfxVolume(float Value)
{
	FKomorebiAudioSettings Next = CurrentSettings;
	Next.Sfx = Value;
	SetAudioSettings(Next, true);
}

void UKomorebiAudioSubsystem::SetMuted(bool bMute)
{
	FKomorebiAudioSettings Next = CurrentSettings;
	Next.bMuted = bMute;
	SetAudioSettings(Next, true);
}

bool UKomorebiAudioSubsystem::ToggleMute()
{
	EnsureSynthReady();
	SetMuted(!CurrentSettings.bMuted);
	return CurrentSettings.bMuted;
}

bool UKomorebiAudioSubsystem::IsVoiceActive() const
{
	return SynthComponent.IsValid() ? SynthComponent->IsVoiceActive() : false;
}

float UKomorebiAudioSubsystem::GetVoiceLevel() const
{
	return SynthComponent.IsValid() ? SynthComponent->GetVoiceLevel() : 0.0f;
}
