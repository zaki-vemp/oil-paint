// Copyright Komorebi Project. Procedural chunked terrain actor implementation.
#include "World/KomorebiTerrainActor.h"

#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "TextureResource.h"

namespace
{
	UTexture2D* CreateOrResizeDataTexture(UTexture2D* Existing, int32 Width, int32 Height)
	{
		if (Width <= 0 || Height <= 0)
		{
			return nullptr;
		}

		UTexture2D* Texture = Existing;
		if (!Texture || Texture->GetSizeX() != Width || Texture->GetSizeY() != Height)
		{
			Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
			if (!Texture)
			{
				return nullptr;
			}
			Texture->SRGB = false;
			Texture->Filter = TF_Bilinear;
			Texture->AddressX = TA_Clamp;
			Texture->AddressY = TA_Clamp;
			Texture->CompressionSettings = TC_VectorDisplacementmap;
#if WITH_EDITORONLY_DATA
			Texture->MipGenSettings = TMGS_NoMipmaps;
#endif
		}
		return Texture;
	}

	void UploadBGRA8Texture(UTexture2D* Texture, const TArray<FColor>& Pixels)
	{
		if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0)
		{
			return;
		}

		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		const int64 ExpectedBytes = static_cast<int64>(Mip.SizeX) * static_cast<int64>(Mip.SizeY) * sizeof(FColor);
		const int64 ProvidedBytes = static_cast<int64>(Pixels.Num()) * sizeof(FColor);
		if (ExpectedBytes <= 0 || ProvidedBytes < ExpectedBytes)
		{
			return;
		}

		void* Dest = Mip.BulkData.Lock(LOCK_READ_WRITE);
		if (Dest)
		{
			FMemory::Memcpy(Dest, Pixels.GetData(), ExpectedBytes);
			Mip.BulkData.Unlock();
			Texture->UpdateResource();
		}
	}
}

AKomorebiTerrainActor::AKomorebiTerrainActor()
{
	PrimaryActorTick.bCanEverTick = false;

	TerrainMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TerrainMesh"));
	SetRootComponent(TerrainMesh);

	// Build Chaos collision synchronously so characters and vehicles spawn cleanly on BeginPlay.
	TerrainMesh->bUseAsyncCooking = false;
	TerrainMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TerrainMesh->SetCollisionObjectType(ECC_WorldStatic);
	TerrainMesh->SetCollisionResponseToAllChannels(ECR_Block);
	TerrainMesh->SetCastShadow(true);
}

void AKomorebiTerrainActor::BeginPlay()
{
	Super::BeginPlay();

	EnsureTerrainBuilt();
}

void AKomorebiTerrainActor::EnsureTerrainBuilt()
{
	if (bTerrainBuilt)
	{
		return;
	}

	if (bIsSnowbound)
	{
		WorldScene = EKomorebiWorldScene::Snowbound;
	}
	else if (WorldScene == EKomorebiWorldScene::Snowbound)
	{
		bIsSnowbound = true;
	}

	RebuildTerrain();
}

void AKomorebiTerrainActor::SetSnowTrackTexture(UTexture* TrackTexture)
{
	SnowTrackTexture = TrackTexture;
	if (TerrainMID && SnowTrackTexture)
	{
		TerrainMID->SetTextureParameterValue(TEXT("SnowTracks"), SnowTrackTexture);
	}
}

#if WITH_EDITOR
void AKomorebiTerrainActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropName = PropertyChangedEvent.GetPropertyName();
	if (PropName == GET_MEMBER_NAME_CHECKED(AKomorebiTerrainActor, bIsSnowbound))
	{
		WorldScene = bIsSnowbound ? EKomorebiWorldScene::Snowbound : EKomorebiWorldScene::NeoKyoto;
	}
	else if (PropName == GET_MEMBER_NAME_CHECKED(AKomorebiTerrainActor, WorldScene))
	{
		bIsSnowbound = (WorldScene == EKomorebiWorldScene::Snowbound);
	}
}
#endif

void AKomorebiTerrainActor::SetWorldScene(EKomorebiWorldScene InScene)
{
	WorldScene = InScene;
	bIsSnowbound = (InScene == EKomorebiWorldScene::Snowbound);
	RebuildTerrain();
}

void AKomorebiTerrainActor::EnsureTerrainPrepared()
{
	if (WorldScene == EKomorebiWorldScene::Snowbound)
	{
		if (!SnowboundMap.IsPrepared())
		{
			SnowboundMap.Prepare();
		}
	}
	else
	{
		if (!NeoKyotoMap.IsPrepared())
		{
			NeoKyotoMap.Prepare();
		}
	}
}

bool AKomorebiTerrainActor::IsTerrainPrepared() const
{
	return (WorldScene == EKomorebiWorldScene::Snowbound)
		? SnowboundMap.IsPrepared()
		: NeoKyotoMap.IsPrepared();
}

void AKomorebiTerrainActor::RebuildTerrain()
{
	if (!TerrainMesh)
	{
		return;
	}

	EnsureTerrainPrepared();

	if (WorldScene == EKomorebiWorldScene::Snowbound)
	{
		UploadLandMaskTexture(SnowboundMap.LandMask, FSnowboundTerrainMap::Resolution());
		BuildChunkedMesh(
			SnowboundMap.Heights,
			SnowboundMap.LandMask,
			FSnowboundTerrainMap::Resolution(),
			KomorebiCore::kSnowExtent);
	}
	else
	{
		UploadLandMaskTexture(NeoKyotoMap.LandMask, FNeoKyotoTerrainMap::Resolution());
		BuildChunkedMesh(
			NeoKyotoMap.Heights,
			NeoKyotoMap.LandMask,
			FNeoKyotoTerrainMap::Resolution(),
			KomorebiCore::kExtent);
	}

	ResolveAndBindMaterial();
	bTerrainBuilt = true;
}

void AKomorebiTerrainActor::UploadLandMaskTexture(const std::vector<uint8_t>& LandMaskBytes, int32 Resolution)
{
	if (Resolution <= 0 || LandMaskBytes.size() < static_cast<size_t>(Resolution) * static_cast<size_t>(Resolution) * 4u)
	{
		return;
	}

	LandMaskTexture = CreateOrResizeDataTexture(LandMaskTexture, Resolution, Resolution);
	if (!LandMaskTexture)
	{
		return;
	}

	const int32 PixelCount = Resolution * Resolution;
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(PixelCount);

	for (int32 Idx = 0; Idx < PixelCount; ++Idx)
	{
		const size_t Base = static_cast<size_t>(Idx) * 4u;
		Pixels[Idx] = FColor(
			LandMaskBytes[Base + 0u],
			LandMaskBytes[Base + 1u],
			LandMaskBytes[Base + 2u],
			LandMaskBytes[Base + 3u]);
	}

	UploadBGRA8Texture(LandMaskTexture, Pixels);
}

void AKomorebiTerrainActor::UploadStreetLightsTexture(const TArray<FColor>& Data, int32 Size)
{
	if (Size <= 0 || Data.Num() < Size * Size)
	{
		return;
	}

	StreetLightsTexture = CreateOrResizeDataTexture(StreetLightsTexture, Size, Size);
	if (!StreetLightsTexture)
	{
		return;
	}

	UploadBGRA8Texture(StreetLightsTexture, Data);

	if (TerrainMID)
	{
		TerrainMID->SetTextureParameterValue(TEXT("StreetLights"), StreetLightsTexture);
	}
}

void AKomorebiTerrainActor::BuildChunkedMesh(
	const std::vector<float>& Heights,
	const std::vector<uint8_t>& LandMaskBytes,
	int32 Resolution,
	double ExtentMeters)
{
	TerrainMesh->ClearAllMeshSections();

	if (Resolution < 2 || Heights.size() < static_cast<size_t>(Resolution) * static_cast<size_t>(Resolution))
	{
		return;
	}

	const int32 Stride = FMath::Clamp(MeshGridStride, 1, 8);
	const int32 TotalCells = (Resolution - 1) / Stride;
	if (TotalCells <= 0)
	{
		return;
	}

	const int32 Chunks = FMath::Clamp(ChunkCountPerAxis, 1, FMath::Min(16, TotalCells));
	const int32 BaseCellsPerChunk = TotalCells / Chunks;
	const int32 ExtraCells = TotalCells % Chunks;

	const double InvResMinusOne = 1.0 / static_cast<double>(Resolution - 1);
	const double GridStepMeters = (2.0 * ExtentMeters) * InvResMinusOne;

	const auto SampleHeight = [&Heights, Resolution](int32 I, int32 J) -> double
	{
		const int32 CI = FMath::Clamp(I, 0, Resolution - 1);
		const int32 CJ = FMath::Clamp(J, 0, Resolution - 1);
		return static_cast<double>(Heights[static_cast<size_t>(CI) + static_cast<size_t>(CJ) * static_cast<size_t>(Resolution)]);
	};

	int32 SectionIndex = 0;
	int32 StartCellJ = 0;

	for (int32 ChunkJ = 0; ChunkJ < Chunks; ++ChunkJ)
	{
		const int32 CellsJ = BaseCellsPerChunk + (ChunkJ < ExtraCells ? 1 : 0);
		const int32 EndCellJ = StartCellJ + CellsJ;
		const int32 VertsJ = CellsJ + 1;

		int32 StartCellI = 0;
		for (int32 ChunkI = 0; ChunkI < Chunks; ++ChunkI)
		{
			const int32 CellsI = BaseCellsPerChunk + (ChunkI < ExtraCells ? 1 : 0);
			const int32 EndCellI = StartCellI + CellsI;
			const int32 VertsI = CellsI + 1;

			const int32 VertexCount = VertsI * VertsJ;
			TArray<FVector> Vertices;
			TArray<FVector> Normals;
			TArray<FVector2D> UVs;
			TArray<FLinearColor> VertexColors;
			TArray<FProcMeshTangent> Tangents;
			TArray<int32> Triangles;

			Vertices.Reserve(VertexCount);
			Normals.Reserve(VertexCount);
			UVs.Reserve(VertexCount);
			VertexColors.Reserve(VertexCount);
			Tangents.Reserve(VertexCount);
			Triangles.Reserve(CellsI * CellsJ * 6);

			for (int32 LocalJ = 0; LocalJ <= CellsJ; ++LocalJ)
			{
				const int32 GridJ = (StartCellJ + LocalJ == TotalCells)
					? (Resolution - 1)
					: FMath::Min(Resolution - 1, (StartCellJ + LocalJ) * Stride);
				const double VFrac = static_cast<double>(GridJ) * InvResMinusOne;
				const double ThreeZ = -ExtentMeters + VFrac * 2.0 * ExtentMeters;

				for (int32 LocalI = 0; LocalI <= CellsI; ++LocalI)
				{
					const int32 GridI = (StartCellI + LocalI == TotalCells)
						? (Resolution - 1)
						: FMath::Min(Resolution - 1, (StartCellI + LocalI) * Stride);
					const double UFrac = static_cast<double>(GridI) * InvResMinusOne;
					const double ThreeX = -ExtentMeters + UFrac * 2.0 * ExtentMeters;
					const double ThreeY = SampleHeight(GridI, GridJ);

					Vertices.Add(KomorebiBridge::ThreeXZToFVector(ThreeX, ThreeY, ThreeZ));

					// Seamless finite-difference normal from the full 1025x1025 heightfield.
					const int32 I0 = FMath::Max(0, GridI - 1);
					const int32 I1 = FMath::Min(Resolution - 1, GridI + 1);
					const int32 J0 = FMath::Max(0, GridJ - 1);
					const int32 J1 = FMath::Min(Resolution - 1, GridJ + 1);

					const double DhDx = (SampleHeight(I1, GridJ) - SampleHeight(I0, GridJ))
						/ (static_cast<double>(FMath::Max(1, I1 - I0)) * GridStepMeters);
					const double DhDz = (SampleHeight(GridI, J1) - SampleHeight(GridI, J0))
						/ (static_cast<double>(FMath::Max(1, J1 - J0)) * GridStepMeters);

					KomorebiCore::Vec3 ThreeNormal{ -DhDx, 1.0, -DhDz };
					const double NLen = std::sqrt(
						ThreeNormal.X * ThreeNormal.X +
						ThreeNormal.Y * ThreeNormal.Y +
						ThreeNormal.Z * ThreeNormal.Z);
					if (NLen > 1e-9)
					{
						ThreeNormal.X /= NLen;
						ThreeNormal.Y /= NLen;
						ThreeNormal.Z /= NLen;
					}
					else
					{
						ThreeNormal = KomorebiCore::Vec3{ 0.0, 1.0, 0.0 };
					}

					const FVector UENormal = KomorebiBridge::ThreeDirToFVector(ThreeNormal).GetSafeNormal();
					Normals.Add(UENormal);

					// Tangent along +UE.Y (increasing Three.X / U) orthogonalized against surface normal.
					const FVector RawTangent = FVector::VectorPlaneProject(FVector(0.0f, 1.0f, 0.0f), UENormal).GetSafeNormal();
					Tangents.Add(FProcMeshTangent(RawTangent.IsNearlyZero() ? FVector(0.0f, 1.0f, 0.0f) : RawTangent, false));

					UVs.Add(FVector2D(static_cast<float>(UFrac), static_cast<float>(VFrac)));

					const size_t MaskBase = (static_cast<size_t>(GridI) + static_cast<size_t>(GridJ) * static_cast<size_t>(Resolution)) * 4u;
					if (MaskBase + 3u < LandMaskBytes.size())
					{
						constexpr float Inv255 = 1.0f / 255.0f;
						VertexColors.Add(FLinearColor(
							static_cast<float>(LandMaskBytes[MaskBase + 0u]) * Inv255,
							static_cast<float>(LandMaskBytes[MaskBase + 1u]) * Inv255,
							static_cast<float>(LandMaskBytes[MaskBase + 2u]) * Inv255,
							static_cast<float>(LandMaskBytes[MaskBase + 3u]) * Inv255));
					}
					else
					{
						VertexColors.Add(FLinearColor(0.0f, 0.0f, 0.5f, 1.0f));
					}
				}
			}

			// Winding: UE front-facing triangles have outward normal along (C - A) x (B - A).
			// Here LocalI -> +UE.Y, LocalJ -> -UE.X, so (V00, V01, V10) and (V10, V01, V11) face +Z up.
			for (int32 LocalJ = 0; LocalJ < CellsJ; ++LocalJ)
			{
				for (int32 LocalI = 0; LocalI < CellsI; ++LocalI)
				{
					const int32 V00 = LocalI + LocalJ * VertsI;
					const int32 V10 = V00 + 1;
					const int32 V01 = V00 + VertsI;
					const int32 V11 = V01 + 1;

					Triangles.Add(V00);
					Triangles.Add(V01);
					Triangles.Add(V10);

					Triangles.Add(V10);
					Triangles.Add(V01);
					Triangles.Add(V11);
				}
			}

			TerrainMesh->CreateMeshSection_LinearColor(
				SectionIndex,
				Vertices,
				Triangles,
				Normals,
				UVs,
				VertexColors,
				Tangents,
				true);

			++SectionIndex;
			StartCellI = EndCellI;
		}
		StartCellJ = EndCellJ;
	}
}

void AKomorebiTerrainActor::ResolveAndBindMaterial()
{
	if (!TerrainMesh)
	{
		return;
	}

	if (!TerrainMaterial)
	{
		const TCHAR* DefaultMatPath = (WorldScene == EKomorebiWorldScene::Snowbound)
			? TEXT("/Game/Materials/M_TerrainSnowbound.M_TerrainSnowbound")
			: TEXT("/Game/Materials/M_TerrainNeoKyoto.M_TerrainNeoKyoto");
		TerrainMaterial = LoadObject<UMaterialInterface>(nullptr, DefaultMatPath);
	}

	UMaterialInterface* ActiveMaterial = TerrainMaterial;
	if (TerrainMaterial)
	{
		TerrainMID = UMaterialInstanceDynamic::Create(TerrainMaterial, this);
		if (TerrainMID)
		{
			if (LandMaskTexture)
			{
				TerrainMID->SetTextureParameterValue(TEXT("LandMask"), LandMaskTexture);
			}
			if (StreetLightsTexture)
			{
				TerrainMID->SetTextureParameterValue(TEXT("StreetLights"), StreetLightsTexture);
			}
			if (SnowTrackTexture)
			{
				TerrainMID->SetTextureParameterValue(TEXT("SnowTracks"), SnowTrackTexture);
			}
			ActiveMaterial = TerrainMID;
		}
	}

	if (ActiveMaterial)
	{
		const int32 NumSections = TerrainMesh->GetNumSections();
		for (int32 SectionIdx = 0; SectionIdx < NumSections; ++SectionIdx)
		{
			TerrainMesh->SetMaterial(SectionIdx, ActiveMaterial);
		}
	}
}

double AKomorebiTerrainActor::GetHeightAtThree(double X, double Z) const
{
	if (WorldScene == EKomorebiWorldScene::Snowbound)
	{
		if (SnowboundMap.IsPrepared())
		{
			return SnowboundMap.HeightAt(X, Z);
		}
		return FSnowboundTerrainMap::RawHeight(X, Z);
	}

	if (NeoKyotoMap.IsPrepared())
	{
		return NeoKyotoMap.HeightAt(X, Z);
	}
	return KomorebiCore::NeoKyotoRawHeight(X, Z, KomorebiCore::WayDist(X, Z));
}

float AKomorebiTerrainActor::GetHeightAtUE(const FVector& UELocation) const
{
	const KomorebiCore::Vec3 ThreePos = KomorebiBridge::FVectorToThree(UELocation);
	const double HeightMeters = GetHeightAtThree(ThreePos.X, ThreePos.Z);
	return static_cast<float>(HeightMeters * KomorebiCore::kMetersToCm);
}

void AKomorebiTerrainActor::GetSnowboundLandAtThree(double X, double Z, double& OutRock, double& OutHorn) const
{
	if (SnowboundMap.IsPrepared())
	{
		SnowboundMap.LandAt(X, Z, OutRock, OutHorn);
	}
	else
	{
		OutRock = 0.0;
		OutHorn = 0.0;
	}
}

KomorebiCore::Vec3 AKomorebiTerrainActor::GetNormalAtThree(double X, double Z) const
{
	if (WorldScene == EKomorebiWorldScene::Snowbound && SnowboundMap.IsPrepared())
	{
		return SnowboundMap.NormalAt(X, Z);
	}

	constexpr double Eps = 0.75;
	const double Nx = GetHeightAtThree(X - Eps, Z) - GetHeightAtThree(X + Eps, Z);
	const double Ny = 2.0 * Eps;
	const double Nz = GetHeightAtThree(X, Z - Eps) - GetHeightAtThree(X, Z + Eps);
	const double Len = std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
	if (Len <= 1e-9)
	{
		return KomorebiCore::Vec3{ 0.0, 1.0, 0.0 };
	}
	return KomorebiCore::Vec3{ Nx / Len, Ny / Len, Nz / Len };
}
