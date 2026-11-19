// Copyright Komorebi Project. Shared UE5 Blueprint/C++ bridge types and KomorebiCore coordinate helpers.
#pragma once

#include "CoreMinimal.h"
#include <string>
#include "KomorebiCoreAPI.h"
#include "World/CoordBridge.h"
#include "Game/LevelsData.h"
#include "Game/CombatRules.h"
#include "KomorebiGameTypes.generated.h"

UENUM(BlueprintType)
enum class EKomorebiWorldScene : uint8
{
	NeoKyoto UMETA(DisplayName = "Neo-Kyoto"),
	Snowbound UMETA(DisplayName = "Snowbound")
};

UENUM(BlueprintType)
enum class EKomorebiPlayMode : uint8
{
	StorySectors UMETA(DisplayName = "Story Sectors"),
	FreeRoam UMETA(DisplayName = "Free Roam"),
	OnlineCity UMETA(DisplayName = "Online City")
};

UENUM(BlueprintType)
enum class EKomorebiQualityTier : uint8
{
	Auto UMETA(DisplayName = "Auto"),
	Low UMETA(DisplayName = "Low"),
	Medium UMETA(DisplayName = "Balanced"),
	High UMETA(DisplayName = "High")
};

UENUM(BlueprintType)
enum class EKomorebiEnemyKind : uint8
{
	Punk,
	Enforcer,
	Gunner,
	Blade,
	Brute,
	Rival,
	Civ,
	Thief
};

UENUM(BlueprintType)
enum class EKomorebiVehicleKind : uint8
{
	Car,
	Sports,
	Pickup,
	Bike,
	Bicycle
};

UENUM(BlueprintType)
enum class EKomorebiPower : uint8
{
	Spark,
	Dash,
	Heavy,
	Flare,
	Whirl
};

UENUM(BlueprintType)
enum class EKomorebiAttackType : uint8
{
	Cut,
	Backhand,
	Thrust,
	Heavy,
	Spin,
	Counter,
	Advance
};

USTRUCT(BlueprintType)
struct FKomorebiCommsMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Comms")
	FString SpeakerId;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Comms")
	FString SpeakerName;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Comms")
	FString SpeakerRole;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Comms")
	FString Frequency;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Comms")
	FString Line;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Comms")
	FLinearColor AccentColor = FLinearColor(0.0f, 0.94f, 1.0f, 1.0f);
};

USTRUCT(BlueprintType)
struct FKomorebiMissionStepView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Mission")
	FString Label;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|Mission")
	bool bDone = false;
};

USTRUCT(BlueprintType)
struct FKomorebiHUDState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	float Hp = 100.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	float MaxHp = 100.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	float SparkPower = 3.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	int32 ClearedSectors = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	int32 ActiveChapter = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	bool bRoamMode = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	bool bOnlineMode = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	bool bWon = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	int32 Cash = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	float Heat = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	int32 HeatStars = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	int32 ComboCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	float DashCooldown = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	float FlareCooldown = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	bool bGuarding = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	float GuardMeter = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	int32 EchoesCollected = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	FString DayClockLabel = TEXT("Day · 10:00");

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	bool bIsNight = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	FString ObjectiveTitle;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	FString ObjectiveTip;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	TArray<FKomorebiMissionStepView> Steps;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	bool bHasWaypoint = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	FVector WaypointWorldLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	FString WaypointLabel;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	bool bDriving = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	FString VehicleName;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	float VehicleKmh = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	bool bVehiclePromptVisible = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	FString VehiclePromptText;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	bool bBossActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	FString BossName;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	float BossHp = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	float BossMaxHp = 120.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	FString BossCue;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	FString BossLesson;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	bool bHasFocusTarget = false;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	FString FocusTargetName;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	float FocusTargetHp = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	float FocusTargetMaxHp = 100.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Komorebi|HUD")
	FVector FocusTargetLocation = FVector::ZeroVector;
};

namespace KomorebiBridge
{
	inline FVector ThreeToFVector(const KomorebiCore::Vec3& V)
	{
		const KomorebiCore::Vec3 UE = KomorebiCore::ThreeToUE(V);
		return FVector(UE.X, UE.Y, UE.Z);
	}

	inline FVector ThreeXZToFVector(double X, double Y, double Z)
	{
		return ThreeToFVector(KomorebiCore::Vec3{ X, Y, Z });
	}

	inline KomorebiCore::Vec3 FVectorToThree(const FVector& UE)
	{
		return KomorebiCore::UEToThree(KomorebiCore::Vec3{ UE.X, UE.Y, UE.Z });
	}

	inline FVector ThreeDirToFVector(const KomorebiCore::Vec3& Dir)
	{
		const KomorebiCore::Vec3 UE = KomorebiCore::ThreeDirToUE(Dir);
		return FVector(UE.X, UE.Y, UE.Z);
	}

	inline FLinearColor ToLinearColor(const KomorebiCore::RGB& C, float Alpha = 1.0f)
	{
		return FLinearColor(static_cast<float>(C.R), static_cast<float>(C.G), static_cast<float>(C.B), Alpha);
	}

	inline KomorebiCore::EEnemyKind ToCoreEnemyKind(EKomorebiEnemyKind Kind)
	{
		return static_cast<KomorebiCore::EEnemyKind>(Kind);
	}

	inline KomorebiCore::EVehicleKind ToCoreVehicleKind(EKomorebiVehicleKind Kind)
	{
		return static_cast<KomorebiCore::EVehicleKind>(Kind);
	}

	inline KomorebiCore::EAttackType ToCoreAttackType(EKomorebiAttackType Type)
	{
		return static_cast<KomorebiCore::EAttackType>(Type);
	}

	inline KomorebiCore::EPowerKind ToCorePowerKind(EKomorebiPower Power)
	{
		return static_cast<KomorebiCore::EPowerKind>(Power);
	}

	inline FString ToFString(std::string_view View)
	{
		return FString(UTF8_TO_TCHAR(std::string(View).c_str()));
	}
}
