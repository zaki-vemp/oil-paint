// Copyright Komorebi Project. Snowbound (Aurora) terrain implementation matching legacy-web/src/night/land.js.
#include "World/SnowboundTerrain.h"

#include "World/KomorebiNoise.h"

#include <array>

namespace KomorebiCore
{
	namespace
	{
		double SMin(double A, double B, double K)
		{
			const double H = Clamp(0.5 + 0.5 * (B - A) / K, 0.0, 1.0);
			return B + (A - B) * H - K * H * (1.0 - H);
		}

		const std::array<HornParams, 2>& GetHorns()
		{
			static const std::array<HornParams, 2> kHorns{{
				{ CrestX(-175.0) + 4.0, -175.0, 378.0, 1.52, 0.94, -0.34, 3.0, 0.2 },
				{ CrestX(-175.0) - 96.0, -226.0, 318.0, 1.5, -0.9, -0.44, 1.8, 1.9 },
			}};
			return kHorns;
		}

		double HornHeight(double X, double Z, const HornParams& O)
		{
			double Dx = X - O.X;
			double Dz = Z - O.Z;
			const double T = Dx * O.Sx + Dz * O.Sz;
			if (T > 0.0)
			{
				Dx += O.Sx * T * (O.Sheer - 1.0);
				Dz += O.Sz * T * (O.Sheer - 1.0);
			}
			const double R = std::hypot(Dx, Dz);
			const double Face = 1.0 - std::abs(std::cos(1.5 * (std::atan2(Dz, Dx) - O.Turn)));
			const double Rr = std::max(0.0, R * (1.0 + 0.5 * Face * SmoothStep(0.0, 30.0, R)
				+ 0.26 * Noise(X / 52.0 + O.X, Z / 52.0)
				+ 0.09 * Noise(X / 19.0, Z / 19.0 + O.Z)));
			return O.Top - O.Fall * 100.0 * std::pow(Rr / 100.0, 0.76);
		}

		double CoordAt(int Index)
		{
			return -kSnowExtent + (static_cast<double>(Index) / static_cast<double>(kSnowLandRes - 1)) * 2.0 * kSnowExtent;
		}

		uint8_t ToByte(double Normalized)
		{
			return static_cast<uint8_t>(std::lround(255.0 * Clamp(Normalized, 0.0, 1.0)));
		}
	}

	Vec3 MoonDir()
	{
		constexpr double RawX = -0.78;
		constexpr double RawY = 0.44;
		constexpr double RawZ = 0.3;
		const double Len = std::sqrt(RawX * RawX + RawY * RawY + RawZ * RawZ);
		return Vec3{ RawX / Len, RawY / Len, RawZ / Len };
	}

	double CrestX(double Z)
	{
		return 30.0 * std::sin(Z / 140.0) + 12.0 * std::sin(Z / 47.0 + 1.0);
	}

	double CrestH(double Z)
	{
		const double U = (Z - 150.0) / 80.0;
		return 222.0 + 28.0 * std::exp(-(U * U));
	}

	Vec2 SnowStart()
	{
		return Vec2{ CrestX(300.0), 300.0 };
	}

	Vec2 SnowKnoll()
	{
		return Vec2{ CrestX(150.0), 150.0 };
	}

	Vec3 SnowCabin()
	{
		return Vec3{ CrestX(-64.0) - 0.5, 226.6, -64.0 };
	}

	HornParams SnowPeak()
	{
		return GetHorns()[0];
	}

	double SnowboundTerrainMap::RawHeight(double X, double Z, double* OutHornWeight)
	{
		const double D = X - CrestX(Z);
		const double Ad = std::abs(D);
		const double Steep = D < 0.0 ? 0.95 : 0.72;
		const double KnollU = (Z - 150.0) / 60.0;
		const double W = 15.0 + 16.0 * std::exp(-(KnollU * KnollU));
		double H = CrestH(Z) - Steep * (std::sqrt(D * D + W * W) - W);

		H += 16.0 * Noise(X / 190.0 + 2.3, Z / 190.0 - 1.1) * SmoothStep(15.0, 120.0, Ad);
		H += 5.0 * Noise(X / 55.0, Z / 55.0 + 7.0) * SmoothStep(8.0, 60.0, Ad);
		const double Gully = 1.0 - std::abs(Noise(Z / 44.0 + 0.5 * Noise(X / 90.0, Z / 90.0), X / 420.0 + 3.0));
		H -= 15.0 * Gully * Gully * Gully * SmoothStep(30.0, 150.0, Ad);
		H += 0.9 * Noise(X / 17.0 + 3.0, Z / 17.0) * SmoothStep(3.0, 25.0, Ad) + 0.3 * Noise(X / 9.0, Z / 9.0 + 2.0);

		const auto& HornsData = GetHorns();
		const double Horns = -SMin(-HornHeight(X, Z, HornsData[0]), -HornHeight(X, Z, HornsData[1]), 10.0);
		const double Hw = SmoothStep(-6.0, 30.0, Horns - H);

		const double Crag = std::max(0.0, Noise(X / 27.0 + 11.0, Z / 27.0 - 4.0) - 0.4);
		H += 24.0 * std::pow(Crag, 1.15) * SmoothStep(8.0, 26.0, Ad) * (1.0 - Hw);
		H = -SMin(-H, -Horns, 14.0);

		const double Beds = std::max({ Hw, 0.4 * SmoothStep(60.0, 150.0, Ad), SmoothStep(0.05, 0.3, Crag) * (1.0 - Hw) });
		const double Tilt = H + 0.42 * X + 0.2 * Z + 9.0 * Noise(X / 90.0, Z / 90.0);
		const double Broken = SmoothStep(-0.25, 0.3, Noise(X / 33.0 + 5.0, Z / 33.0 - 2.0));
		H += Beds * (1.6 * Broken * std::sin(Tilt * 0.571) + 2.6 * Hw * std::sin(Tilt * 0.17 + 1.3));

		const double X2 = X * X;
		const double Z2 = Z * Z;
		const double Edge = kSnowExtent - std::pow(X2 * X2 + Z2 * Z2, 0.25);
		H = SMin(H, 1.25 * (Edge - 18.0), 22.0);

		const Vec3 Cabin = SnowCabin();
		const double Shelf = std::hypot(X - Cabin.X, Z - Cabin.Z);
		if (Shelf < 27.0)
		{
			H += (Cabin.Y - H) * SmoothStep(27.0, 9.0, Shelf);
		}

		if (OutHornWeight != nullptr)
		{
			*OutHornWeight = Hw;
		}
		return H;
	}

	double SnowboundTerrainMap::RawHeight(double X, double Z, double& OutHornWeight)
	{
		return RawHeight(X, Z, &OutHornWeight);
	}

	void SnowboundTerrainMap::Prepare()
	{
		constexpr int N = kSnowLandRes;
		Heights.resize(static_cast<size_t>(N) * static_cast<size_t>(N));
		LandMask.resize(static_cast<size_t>(N) * static_cast<size_t>(N) * 4u);

		for (int J = 0; J < N; ++J)
		{
			const double Z = CoordAt(J);
			for (int I = 0; I < N; ++I)
			{
				double HornWeight = 0.0;
				const size_t Idx = static_cast<size_t>(I) + static_cast<size_t>(J) * static_cast<size_t>(N);
				Heights[Idx] = static_cast<float>(RawHeight(CoordAt(I), Z, &HornWeight));
				LandMask[Idx * 4u + 1u] = ToByte(HornWeight);
			}
		}

		for (int J = 0; J < N; ++J)
		{
			const int J0 = std::max(0, J - 1);
			const int J1 = std::min(N - 1, J + 1);
			const double Z = CoordAt(J);
			for (int I = 0; I < N; ++I)
			{
				const int I0 = std::max(0, I - 1);
				const int I1 = std::min(N - 1, I + 1);
				const double Hx = static_cast<double>(Heights[static_cast<size_t>(I1) + static_cast<size_t>(J) * static_cast<size_t>(N)]
					- Heights[static_cast<size_t>(I0) + static_cast<size_t>(J) * static_cast<size_t>(N)]) / static_cast<double>(I1 - I0);
				const double Hz = static_cast<double>(Heights[static_cast<size_t>(I) + static_cast<size_t>(J1) * static_cast<size_t>(N)]
					- Heights[static_cast<size_t>(I) + static_cast<size_t>(J0) * static_cast<size_t>(N)]) / static_cast<double>(J1 - J0);
				const double Ny = 1.0 / std::sqrt(1.0 + Hx * Hx + Hz * Hz);
				const size_t K = (static_cast<size_t>(I) + static_cast<size_t>(J) * static_cast<size_t>(N)) * 4u;
				const double X = CoordAt(I);
				const double Limit = 0.64 + 0.05 * (static_cast<double>(LandMask[K + 1u]) / 255.0);
				const double Rock = SmoothStep(
					Limit + 0.05,
					Limit - 0.05,
					Ny + 0.07 * Noise(X / 9.0, Z / 9.0) + 0.14 * std::max(0.0, Noise(X / 41.0 + 8.0, Z / 41.0)));
				LandMask[K + 0u] = ToByte(Rock);
				LandMask[K + 2u] = ToByte(Ny);
				LandMask[K + 3u] = 255u;
			}
		}

		bPrepared = true;
	}

	double SnowboundTerrainMap::HeightAt(double X, double Z) const
	{
		constexpr int N = kSnowLandRes;
		if (Heights.size() != static_cast<size_t>(N) * static_cast<size_t>(N))
		{
			return 0.0;
		}
		const double Fx = Clamp(((X + kSnowExtent) / (2.0 * kSnowExtent)) * static_cast<double>(N - 1), 0.0, static_cast<double>(N) - 1.001);
		const double Fz = Clamp(((Z + kSnowExtent) / (2.0 * kSnowExtent)) * static_cast<double>(N - 1), 0.0, static_cast<double>(N) - 1.001);
		const int I = static_cast<int>(std::floor(Fx));
		const int J = static_cast<int>(std::floor(Fz));
		const double Tx = Fx - static_cast<double>(I);
		const double Tz = Fz - static_cast<double>(J);

		const size_t Row0 = static_cast<size_t>(J) * static_cast<size_t>(N);
		const size_t Row1 = static_cast<size_t>(J + 1) * static_cast<size_t>(N);
		const double A = static_cast<double>(Heights[static_cast<size_t>(I) + Row0]);
		const double B = static_cast<double>(Heights[static_cast<size_t>(I + 1) + Row0]);
		const double C = static_cast<double>(Heights[static_cast<size_t>(I) + Row1]);
		const double D = static_cast<double>(Heights[static_cast<size_t>(I + 1) + Row1]);
		return (A + (B - A) * Tx) * (1.0 - Tz) + (C + (D - C) * Tx) * Tz;
	}

	void SnowboundTerrainMap::LandAt(double X, double Z, double& OutRock, double& OutHorn) const
	{
		constexpr int N = kSnowLandRes;
		if (LandMask.size() != static_cast<size_t>(N) * static_cast<size_t>(N) * 4u)
		{
			OutRock = 0.0;
			OutHorn = 0.0;
			return;
		}
		const int I = static_cast<int>(Clamp(std::round(((X + kSnowExtent) / (2.0 * kSnowExtent)) * static_cast<double>(N - 1)), 0.0, static_cast<double>(N - 1)));
		const int J = static_cast<int>(Clamp(std::round(((Z + kSnowExtent) / (2.0 * kSnowExtent)) * static_cast<double>(N - 1)), 0.0, static_cast<double>(N - 1)));
		const size_t K = (static_cast<size_t>(I) + static_cast<size_t>(J) * static_cast<size_t>(N)) * 4u;
		OutRock = static_cast<double>(LandMask[K + 0u]) / 255.0;
		OutHorn = static_cast<double>(LandMask[K + 1u]) / 255.0;
	}

	Vec3 SnowboundTerrainMap::NormalAt(double X, double Z) const
	{
		constexpr double E = 1.2;
		const double Nx = HeightAt(X - E, Z) - HeightAt(X + E, Z);
		const double Ny = 2.0 * E;
		const double Nz = HeightAt(X, Z - E) - HeightAt(X, Z + E);
		const double Len = std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
		if (Len <= 1e-12)
		{
			return Vec3{ 0.0, 1.0, 0.0 };
		}
		return Vec3{ Nx / Len, Ny / Len, Nz / Len };
	}
}
