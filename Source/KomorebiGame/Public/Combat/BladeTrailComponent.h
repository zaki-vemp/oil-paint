// Copyright Komorebi Project. Dual-layer 20-sample procedural neon blade crescent trail porting legacy-web/src/game/combat.js.
#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "KomorebiGameTypes.h"
#include "BladeTrailComponent.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;

UENUM(BlueprintType)
enum class EBladeTrailTheme : uint8
{
	Cyan UMETA(DisplayName = "Cyan"),
	Gold UMETA(DisplayName = "Gold"),
	Violet UMETA(DisplayName = "Violet"),
	Crimson UMETA(DisplayName = "Crimson")
};

/**
 * World-space dual-layer procedural mesh ribbon for katana slashes.
 * Section 0: Core crescent wake with midpoint subdivision, quadratic inner taper, and white-hot leading edge.
 * Section 1: Outer additive neon glow halo.
 */
UCLASS(ClassGroup = (Komorebi), meta = (BlueprintSpawnableComponent))
class KOMOREBIGAME_API UBladeTrailComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	UBladeTrailComponent(const FObjectInitializer& ObjectInitializer);

	static constexpr int32 kMaxTrailSamples = 20;
	static constexpr int32 kStandardTrailSamples = 18;
	static constexpr float kTrailFadeSeconds = 0.18f;
	static constexpr float kBladeBaseOffsetCm = 28.0f;
	static constexpr float kBladeTipOffsetCm = 118.0f;
	static constexpr float kMidpointSubdivThresholdSqCm = 9.0f; // (3 cm)^2 == 0.0009 m^2

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Trail")
	EBladeTrailTheme Theme = EBladeTrailTheme::Cyan;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Trail")
	TObjectPtr<UMaterialInterface> CoreTrailMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Komorebi|Trail")
	TObjectPtr<UMaterialInterface> GlowTrailMaterial = nullptr;

	/** Sets the trail color theme directly. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Trail")
	void SetTheme(EBladeTrailTheme InTheme);

	/** Selects Crimson for rivals, Gold for Heavy, Violet for Spin/Whirl, Cyan otherwise (combat.js line 219). */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Trail")
	void SetThemeForAttack(EKomorebiAttackType AttackType, bool bIsRival);

	/** Assigns custom core and additive glow materials and updates their dynamic instances. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Trail")
	void SetTrailMaterials(UMaterialInterface* InCoreMaterial, UMaterialInterface* InGlowMaterial);

	/** Samples current blade world coordinates, runs midpoint subdivision, and rebuilds both ribbon sections. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Trail")
	void SampleBlade(
		float DeltaTime,
		bool bActive,
		float AttackU,
		const FVector& WorldGrip,
		const FVector& WorldBase,
		const FVector& WorldTip,
		bool bIsSpinAttack = false);

	/** Clears all trail samples immediately and hides both procedural sections. */
	UFUNCTION(BlueprintCallable, Category = "Komorebi|Trail")
	void ClearTrail();

	UFUNCTION(BlueprintPure, Category = "Komorebi|Trail")
	float GetCurrentFadeAlpha() const { return FadeRemaining > 0.0f ? FMath::Clamp(FadeRemaining / kTrailFadeSeconds, 0.0f, 1.0f) : 0.0f; }

	static FLinearColor GetThemeLinearColor(EBladeTrailTheme InTheme);

protected:
	virtual void BeginPlay() override;

	void EnsureDynamicMaterials();
	void RebuildRibbonGeometry();

	struct FBladeSamplePair
	{
		FVector Base = FVector::ZeroVector;
		FVector Tip = FVector::ZeroVector;
	};

	TArray<FBladeSamplePair> Samples;
	FVector PrevGrip = FVector::ZeroVector;
	FVector PrevTip = FVector::ZeroVector;

	bool bWasActive = false;
	float PrevAttackU = -1.0f;
	float FadeRemaining = 0.0f;

	int32 CachedCoreVertexCount = 0;
	int32 CachedGlowVertexCount = 0;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CoreMID = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GlowMID = nullptr;
};
