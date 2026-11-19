// Copyright Komorebi Project. Dual-layer 20-sample procedural neon blade crescent trail implementation.
#include "Combat/BladeTrailComponent.h"

#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

UBladeTrailComponent::UBladeTrailComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	bUseAsyncCooking = false;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCastShadow(false);

	// Keep ribbon vertices pinned in world space independent of owner movement.
	SetUsingAbsoluteLocation(true);
	SetUsingAbsoluteRotation(true);
	SetUsingAbsoluteScale(true);
}

void UBladeTrailComponent::BeginPlay()
{
	Super::BeginPlay();
	SetWorldLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	SetWorldScale3D(FVector::OneVector);
	EnsureDynamicMaterials();
}

FLinearColor UBladeTrailComponent::GetThemeLinearColor(EBladeTrailTheme InTheme)
{
	switch (InTheme)
	{
		case EBladeTrailTheme::Gold:
			return FLinearColor(0xFC / 255.0f, 0xEE / 255.0f, 0x0A / 255.0f, 1.0f);
		case EBladeTrailTheme::Violet:
			return FLinearColor(0xD5 / 255.0f, 0x80 / 255.0f, 0xFF / 255.0f, 1.0f);
		case EBladeTrailTheme::Crimson:
			return FLinearColor(0xFF / 255.0f, 0x2A / 255.0f, 0x6D / 255.0f, 1.0f);
		case EBladeTrailTheme::Cyan:
		default:
			return FLinearColor(0x00 / 255.0f, 0xF0 / 255.0f, 0xFF / 255.0f, 1.0f);
	}
}

void UBladeTrailComponent::SetTheme(EBladeTrailTheme InTheme)
{
	Theme = InTheme;
	const FLinearColor Color = GetThemeLinearColor(Theme);
	if (CoreMID)
	{
		CoreMID->SetVectorParameterValue(TEXT("TrailColor"), Color);
	}
	if (GlowMID)
	{
		GlowMID->SetVectorParameterValue(TEXT("TrailColor"), Color);
	}
}

void UBladeTrailComponent::SetThemeForAttack(EKomorebiAttackType AttackType, bool bIsRival)
{
	if (bIsRival)
	{
		SetTheme(EBladeTrailTheme::Crimson);
	}
	else if (AttackType == EKomorebiAttackType::Heavy)
	{
		SetTheme(EBladeTrailTheme::Gold);
	}
	else if (AttackType == EKomorebiAttackType::Spin)
	{
		SetTheme(EBladeTrailTheme::Violet);
	}
	else
	{
		SetTheme(EBladeTrailTheme::Cyan);
	}
}

void UBladeTrailComponent::SetTrailMaterials(UMaterialInterface* InCoreMaterial, UMaterialInterface* InGlowMaterial)
{
	CoreTrailMaterial = InCoreMaterial;
	GlowTrailMaterial = InGlowMaterial;
	CoreMID = nullptr;
	GlowMID = nullptr;
	EnsureDynamicMaterials();
}

void UBladeTrailComponent::EnsureDynamicMaterials()
{
	const FLinearColor Color = GetThemeLinearColor(Theme);
	if (CoreTrailMaterial && !CoreMID)
	{
		CoreMID = UMaterialInstanceDynamic::Create(CoreTrailMaterial, this);
		if (CoreMID)
		{
			CoreMID->SetVectorParameterValue(TEXT("TrailColor"), Color);
			SetMaterial(0, CoreMID);
		}
	}
	if (GlowTrailMaterial && !GlowMID)
	{
		GlowMID = UMaterialInstanceDynamic::Create(GlowTrailMaterial, this);
		if (GlowMID)
		{
			GlowMID->SetVectorParameterValue(TEXT("TrailColor"), Color);
			SetMaterial(1, GlowMID);
		}
	}
}

void UBladeTrailComponent::ClearTrail()
{
	Samples.Reset();
	bWasActive = false;
	PrevAttackU = -1.0f;
	FadeRemaining = 0.0f;
	if (CachedCoreVertexCount > 0)
	{
		ClearMeshSection(0);
		CachedCoreVertexCount = 0;
	}
	if (CachedGlowVertexCount > 0)
	{
		ClearMeshSection(1);
		CachedGlowVertexCount = 0;
	}
}

void UBladeTrailComponent::SampleBlade(
	float DeltaTime,
	bool bActive,
	float AttackU,
	const FVector& WorldGrip,
	const FVector& WorldBase,
	const FVector& WorldTip,
	bool bIsSpinAttack)
{
	const int32 MaxPts = bIsSpinAttack ? kMaxTrailSamples : kStandardTrailSamples;

	if (bActive)
	{
		if (!bWasActive || AttackU < PrevAttackU - 0.12f)
		{
			Samples.Reset();
			Samples.Insert(FBladeSamplePair{ WorldBase, WorldTip }, 0);
		}
		else if (DeltaTime > 0.0f)
		{
			// Midpoint arc subdivision matching combat.js lines 192-206
			if (FVector::DistSquared(WorldTip, PrevTip) > kMidpointSubdivThresholdSqCm && Samples.Num() > 0)
			{
				const FVector MidGrip = 0.5f * (PrevGrip + WorldGrip);
				const FVector PrevBlade = PrevTip - PrevGrip;
				const FVector CurrBlade = WorldTip - WorldGrip;
				const float TipRadius = 0.5f * (PrevBlade.Size() + CurrBlade.Size());
				const float BaseRadius = (WorldBase - WorldGrip).Size();
				const FVector MidDir = (PrevBlade.GetSafeNormal() + CurrBlade.GetSafeNormal()).GetSafeNormal();

				if (!MidDir.IsNearlyZero())
				{
					Samples.Insert(
						FBladeSamplePair{ MidGrip + MidDir * BaseRadius, MidGrip + MidDir * TipRadius },
						0);
				}
			}

			Samples.Insert(FBladeSamplePair{ WorldBase, WorldTip }, 0);
			while (Samples.Num() > MaxPts)
			{
				Samples.Pop(EAllowShrinking::No);
			}
		}

		PrevGrip = WorldGrip;
		PrevTip = WorldTip;
		FadeRemaining = kTrailFadeSeconds;
	}
	else if (DeltaTime > 0.0f)
	{
		FadeRemaining = FMath::Max(0.0f, FadeRemaining - DeltaTime);
	}

	bWasActive = bActive;
	PrevAttackU = AttackU;

	if (FadeRemaining <= 0.0f)
	{
		Samples.Reset();
	}

	const float FadeAlpha = GetCurrentFadeAlpha();
	if (CoreMID)
	{
		CoreMID->SetScalarParameterValue(TEXT("Fade"), FadeAlpha);
	}
	if (GlowMID)
	{
		GlowMID->SetScalarParameterValue(TEXT("Fade"), FadeAlpha);
	}

	RebuildRibbonGeometry();
}

void UBladeTrailComponent::RebuildRibbonGeometry()
{
	if (Samples.Num() < 2 || FadeRemaining <= 0.0f)
	{
		if (CachedCoreVertexCount > 0)
		{
			ClearMeshSection(0);
			CachedCoreVertexCount = 0;
		}
		if (CachedGlowVertexCount > 0)
		{
			ClearMeshSection(1);
			CachedGlowVertexCount = 0;
		}
		return;
	}

	const int32 NumPts = Samples.Num();
	const int32 NumQuads = NumPts - 1;
	const float Span = static_cast<float>(FMath::Max(1, NumQuads));
	const float FadeAlpha = GetCurrentFadeAlpha();
	const FLinearColor Neon = GetThemeLinearColor(Theme);

	TArray<FVector> CoreVerts;
	TArray<int32> CoreTris;
	TArray<FVector> CoreNormals;
	TArray<FVector2D> CoreUVs;
	TArray<FLinearColor> CoreColors;
	TArray<FProcMeshTangent> CoreTangents;

	TArray<FVector> GlowVerts;
	TArray<int32> GlowTris;
	TArray<FVector> GlowNormals;
	TArray<FVector2D> GlowUVs;
	TArray<FLinearColor> GlowColors;
	TArray<FProcMeshTangent> GlowTangents;

	const int32 VertCount = NumPts * 2;
	const int32 IndexCount = NumQuads * 12; // double-sided (front + back)

	CoreVerts.Reserve(VertCount);
	CoreNormals.Reserve(VertCount);
	CoreUVs.Reserve(VertCount);
	CoreColors.Reserve(VertCount);
	CoreTris.Reserve(IndexCount);

	GlowVerts.Reserve(VertCount);
	GlowNormals.Reserve(VertCount);
	GlowUVs.Reserve(VertCount);
	GlowColors.Reserve(VertCount);
	GlowTris.Reserve(IndexCount);

	for (int32 j = 0; j < NumPts; ++j)
	{
		const float Age = static_cast<float>(j) / Span;
		const FVector& BasePt = Samples[j].Base;
		const FVector& TipPt = Samples[j].Tip;
		const FVector SpanVec = TipPt - BasePt;

		// Quadratic base-edge width taper matching combat.js line 225
		const float BaseTaper = Age * Age * 0.68f;
		const FVector TaperedBase = BasePt + SpanVec * BaseTaper;

		// White-hot leading edge & age brightness falloff (combat.js lines 229-233)
		const float WhiteHot = (j == 0) ? 1.0f : FMath::Square(FMath::Max(0.0f, 1.0f - Age * 4.2f));
		const float BaseBright = (0.52f + 0.48f * (1.0f - Age)) * 0.85f;
		const float TipBright = (0.52f + 0.48f * (1.0f - Age));

		const float CoreTailAlpha = FadeAlpha * FMath::Pow(1.0f - Age, 1.55f);
		const float GlowTailAlpha = FadeAlpha * FMath::Pow(1.0f - Age, 1.85f);

		const FLinearColor CoreBaseColor(
			FMath::Lerp(Neon.R, 1.75f, WhiteHot) * BaseBright,
			FMath::Lerp(Neon.G, 1.95f, WhiteHot) * BaseBright,
			FMath::Lerp(Neon.B, 2.25f, WhiteHot) * BaseBright,
			CoreTailAlpha * 0.30f);

		const FLinearColor CoreTipColor(
			FMath::Lerp(Neon.R, 1.75f, WhiteHot) * TipBright,
			FMath::Lerp(Neon.G, 1.95f, WhiteHot) * TipBright,
			FMath::Lerp(Neon.B, 2.25f, WhiteHot) * TipBright,
			CoreTailAlpha * 0.88f);

		// Compute local ribbon normal from neighbor motion
		const FVector& NeighborTip = (j + 1 < NumPts) ? Samples[j + 1].Tip : Samples[j - 1].Tip;
		const FVector MotionDir = (j + 1 < NumPts) ? (TipPt - NeighborTip) : (NeighborTip - TipPt);
		const FVector RibbonNormal = FVector::CrossProduct(SpanVec, MotionDir).GetSafeNormal();
		const FVector SafeNormal = RibbonNormal.IsNearlyZero() ? FVector::UpVector : RibbonNormal;

		// Core section vertices (even = inner/base, odd = outer/tip)
		CoreVerts.Add(TaperedBase);
		CoreVerts.Add(TipPt);
		CoreNormals.Add(SafeNormal);
		CoreNormals.Add(SafeNormal);
		CoreUVs.Add(FVector2D(Age, 0.0f));
		CoreUVs.Add(FVector2D(Age, 1.0f));
		CoreColors.Add(CoreBaseColor);
		CoreColors.Add(CoreTipColor);

		// Outer glow halo layer (slightly expanded along tip for luminous neon aura)
		const FVector GlowBase = BasePt + SpanVec * (BaseTaper * 0.85f);
		const FVector GlowTip = TipPt + SpanVec.GetSafeNormal() * 14.0f;
		GlowVerts.Add(GlowBase);
		GlowVerts.Add(GlowTip);
		GlowNormals.Add(SafeNormal);
		GlowNormals.Add(SafeNormal);
		GlowUVs.Add(FVector2D(Age, 0.0f));
		GlowUVs.Add(FVector2D(Age, 1.0f));
		GlowColors.Add(FLinearColor(Neon.R, Neon.G, Neon.B, GlowTailAlpha * 0.12f));
		GlowColors.Add(FLinearColor(Neon.R, Neon.G, Neon.B, GlowTailAlpha * 0.30f));
	}

	for (int32 i = 0; i < NumQuads; ++i)
	{
		const int32 B0 = i * 2;
		const int32 T0 = i * 2 + 1;
		const int32 B1 = (i + 1) * 2;
		const int32 T1 = (i + 1) * 2 + 1;

		// Front face
		CoreTris.Add(B0); CoreTris.Add(T0); CoreTris.Add(B1);
		CoreTris.Add(B1); CoreTris.Add(T0); CoreTris.Add(T1);
		// Back face (double-sided)
		CoreTris.Add(B0); CoreTris.Add(B1); CoreTris.Add(T0);
		CoreTris.Add(B1); CoreTris.Add(T1); CoreTris.Add(T0);

		GlowTris.Add(B0); GlowTris.Add(T0); GlowTris.Add(B1);
		GlowTris.Add(B1); GlowTris.Add(T0); GlowTris.Add(T1);
		GlowTris.Add(B0); GlowTris.Add(B1); GlowTris.Add(T0);
		GlowTris.Add(B1); GlowTris.Add(T1); GlowTris.Add(T0);
	}

	if (VertCount == CachedCoreVertexCount)
	{
		UpdateMeshSection_LinearColor(0, CoreVerts, CoreNormals, CoreUVs, CoreColors, CoreTangents);
	}
	else
	{
		CreateMeshSection_LinearColor(0, CoreVerts, CoreTris, CoreNormals, CoreUVs, CoreColors, CoreTangents, false);
		CachedCoreVertexCount = VertCount;
		if (CoreMID)
		{
			SetMaterial(0, CoreMID);
		}
	}

	if (VertCount == CachedGlowVertexCount)
	{
		UpdateMeshSection_LinearColor(1, GlowVerts, GlowNormals, GlowUVs, GlowColors, GlowTangents);
	}
	else
	{
		CreateMeshSection_LinearColor(1, GlowVerts, GlowTris, GlowNormals, GlowUVs, GlowColors, GlowTangents, false);
		CachedGlowVertexCount = VertCount;
		if (GlowMID)
		{
			SetMaterial(1, GlowMID);
		}
	}
}
