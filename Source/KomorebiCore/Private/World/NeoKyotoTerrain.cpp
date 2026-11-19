// Copyright Komorebi Project. Neo-Kyoto terrain implementation matching legacy-web/src/world/land.js.
#include "World/NeoKyotoTerrain.h"

#include "World/KomorebiNoise.h"
#include "World/WorldGrid.h"

#include <array>
#include <limits>

namespace KomorebiCore
{
	namespace
	{
		double BoxMask(double X, double Z, double X0, double X1, double Z0, double Z1, double Feather)
		{
			const double Dx = std::max({ X0 - X, 0.0, X - X1 });
			const double Dz = std::max({ Z0 - Z, 0.0, Z - Z1 });
			return 1.0 - SmoothStep(0.0, Feather, std::hypot(Dx, Dz));
		}

		std::vector<Vec2> BuildWaySamples()
		{
			static constexpr std::array<Vec2, 9> kWayControl{{
				{ 2.0 * kScale, 96.0 * kScale },
				{ 0.0 * kScale, 80.0 * kScale },
				{ -7.0 * kScale, 56.0 * kScale },
				{ 3.0 * kScale, 32.0 * kScale },
				{ 10.0 * kScale, 6.0 * kScale },
				{ 3.0 * kScale, -20.0 * kScale },
				{ -5.0 * kScale, -44.0 * kScale },
				{ 4.0 * kScale, -68.0 * kScale },
				{ 10.0 * kScale, -90.0 * kScale },
			}};

			const auto At = [](int Idx) -> Vec2
			{
				const int Clamped = std::max(0, std::min(static_cast<int>(kWayControl.size()) - 1, Idx));
				return kWayControl[static_cast<size_t>(Clamped)];
			};

			std::vector<Vec2> Samples;
			Samples.reserve((kWayControl.size() - 1) * 24 + 1);

			for (int I = 0; I < static_cast<int>(kWayControl.size()) - 1; ++I)
			{
				const Vec2 P0 = At(I - 1);
				const Vec2 P1 = At(I);
				const Vec2 P2 = At(I + 1);
				const Vec2 P3 = At(I + 2);
				for (int K = 0; K < 24; ++K)
				{
					const double T = static_cast<double>(K) / 24.0;
					const double T2 = T * T;
					const double T3 = T2 * T;
					const auto Catmull = [T, T2, T3](double A, double B, double C, double D)
					{
						return 0.5 * (2.0 * B + (-A + C) * T + (2.0 * A - 5.0 * B + 4.0 * C - D) * T2 + (-A + 3.0 * B - 3.0 * C + D) * T3);
					};
					Samples.push_back(Vec2{
						Catmull(P0.X, P1.X, P2.X, P3.X),
						Catmull(P0.Z, P1.Z, P2.Z, P3.Z),
					});
				}
			}
			Samples.push_back(kWayControl.back());
			return Samples;
		}

		double SpineDist(double X, double Z)
		{
			const auto& Samples = WaySamples();
			double Best = std::numeric_limits<double>::infinity();
			for (size_t I = 0; I + 1 < Samples.size(); ++I)
			{
				const double Ax = Samples[I].X;
				const double Az = Samples[I].Z;
				const double Bx = Samples[I + 1].X;
				const double Bz = Samples[I + 1].Z;
				const double Dx = Bx - Ax;
				const double Dz = Bz - Az;
				const double Denom = Dx * Dx + Dz * Dz;
				const double T = Denom > 0.0 ? Clamp(((X - Ax) * Dx + (Z - Az) * Dz) / Denom, 0.0, 1.0) : 0.0;
				const double Dist = std::hypot(X - Ax - Dx * T, Z - Az - Dz * T);
				if (Dist < Best)
				{
					Best = Dist;
				}
			}
			return Best;
		}

		uint8_t ToByte(double Normalized)
		{
			return static_cast<uint8_t>(std::lround(255.0 * Clamp(Normalized, 0.0, 1.0)));
		}
	}

	Vec2 Start()
	{
		return Vec2{ 0.0, 78.0 * kScale };
	}

	ClearingArea Clearing()
	{
		return ClearingArea{ 11.0 * kScale, -96.0 * kScale, 26.0 };
	}

	const std::vector<Vec2>& WaySamples()
	{
		static const std::vector<Vec2> Samples = BuildWaySamples();
		return Samples;
	}

	double WayDist(double X, double Z)
	{
		const double G = GridDist(X, Z);
		if (X < -42.0 || X > 62.0 || Z > 305.0 || Z < -318.0)
		{
			return G;
		}
		const double S = SpineDist(X, Z);
		return S < G ? S : G;
	}

	double ClearingAt(double X, double Z)
	{
		const ClearingArea C = Clearing();
		const double D = std::hypot(X - C.X, Z - C.Z) + Noise(X * 0.11, Z * 0.11) * 3.5;
		return 1.0 - Clamp((D - C.Radius * 0.72) / (C.Radius * 0.5), 0.0, 1.0);
	}

	double MountainHeight(double X, double Z)
	{
		const double MNW = BoxMask(X, Z, -360.0, -196.0, 238.0, 360.0, 22.0);
		const double MNE = BoxMask(X, Z, 196.0, 360.0, 238.0, 360.0, 22.0);
		const double MSW = BoxMask(X, Z, -360.0, -196.0, -360.0, -164.0, 22.0);
		const double ME = BoxMask(X, Z, 272.0, 360.0, -204.0, -76.0, 18.0);
		const double OuterX = SmoothStep(288.0, 342.0, std::abs(X));
		const double NorthZ = SmoothStep(328.0, 354.0, Z);
		const double SouthZ = SmoothStep(-318.0, -354.0, Z);
		const double MRim = std::max({ OuterX, NorthZ, SouthZ });

		const double WMax = std::max({ MNW, MNE, MSW, ME, MRim });
		if (WMax <= 0.0)
		{
			return 0.0;
		}

		const double Ridge1 = 1.0 - std::abs(Noise(X / 68.0 + 1.7, Z / 68.0 - 2.4));
		const double Ridge2 = 1.0 - std::abs(Noise(X / 31.0 - 4.1, Z / 31.0 + 1.9));
		const double Crag = 0.5 + 0.5 * Noise(X / 14.0 + 3.0, Z / 14.0 - 1.2);
		const double PeakProfile = 0.42 + 0.44 * Ridge1 * Ridge1 + 0.22 * Ridge2 + 0.08 * Crag;

		const auto Toe = [](double W)
		{
			return W <= 0.0 ? 0.0 : W * (0.35 + 0.65 * W);
		};
		const double LocalH = (48.0 * Toe(MNW) + 46.0 * Toe(MNE) + 54.0 * Toe(MSW) + 42.0 * Toe(ME)) * PeakProfile;
		const double RimH = Toe(MRim) * (34.0 + 42.0 * Ridge1 * Ridge1 + 14.0 * Ridge2);
		return std::max(LocalH, RimH);
	}

	double ForestAt(double X, double Z)
	{
		const double FWest = BoxMask(X, Z, -144.0, -52.0, -20.0, 98.0, 12.0);
		const double FNE = BoxMask(X, Z, 54.0, 104.0, 56.0, 104.0, 10.0);
		const double FSE = BoxMask(X, Z, 52.0, 144.0, -216.0, -92.0, 12.0);
		const double Span = std::max(std::abs(X), std::abs(Z));
		const double Outer = SmoothStep(254.0, 276.0, Span) * (1.0 - SmoothStep(302.0, 325.0, Span));
		const double MH = MountainHeight(X, Z);
		const double Foothill = (MH > 0.05 && MH < 18.0)
			? SmoothStep(0.05, 2.5, MH) * (1.0 - SmoothStep(10.0, 18.0, MH))
			: 0.0;

		double F = std::max({ FWest, FNE, FSE, Outer, Foothill });
		if (F <= 0.0)
		{
			return 0.0;
		}
		const Vec2 StartPos = Start();
		F *= 1.0 - ClearingAt(X, Z);
		F *= SmoothStep(14.0, 24.0, std::hypot(X - StartPos.X, Z - StartPos.Z));
		F *= SmoothStep(20.0, 30.0, std::hypot(X + 20.0, Z - 305.0));
		return Clamp(F, 0.0, 1.0);
	}

	double NeoKyotoRawHeight(double X, double Z, double WayD)
	{
		const double MH = MountainHeight(X, Z);
		if (MH > 0.0)
		{
			return MH;
		}
		const double F = ForestAt(X, Z);
		if (F > 0.4 && WayD >= kRoad + 7.0)
		{
			return F * SmoothStep(kRoad + 7.0, kRoad + 16.0, WayD) * 0.28 * std::max(0.0, Noise(X / 22.0 + 5.0, Z / 22.0 - 3.0));
		}
		return 0.0;
	}

	void NeoKyotoTerrainMap::Prepare()
	{
		constexpr int N = kLandRes;
		Heights.resize(static_cast<size_t>(N) * static_cast<size_t>(N));
		LandMask.resize(static_cast<size_t>(N) * static_cast<size_t>(N) * 4u);

		for (int J = 0; J < N; ++J)
		{
			const double Z = -kExtent + (static_cast<double>(J) / static_cast<double>(N - 1)) * 2.0 * kExtent;
			for (int I = 0; I < N; ++I)
			{
				const double X = -kExtent + (static_cast<double>(I) / static_cast<double>(N - 1)) * 2.0 * kExtent;
				const bool bCovered = std::abs(X) < kExtent - 18.0 && std::abs(Z) < kExtent - 14.0;
				const double MH = MountainHeight(X, Z);
				const double Wd = (bCovered && MH <= 0.8) ? WayDist(X, Z) : 99.0;
				const size_t Idx = static_cast<size_t>(I) + static_cast<size_t>(J) * static_cast<size_t>(N);
				Heights[Idx] = static_cast<float>(NeoKyotoRawHeight(X, Z, Wd));

				const size_t K = Idx * 4u;
				const double Ragged = GridDist(X, Z) < kRoad + 1.4
					? 0.0
					: Noise(X * 0.9, Z * 0.9) * 0.35 + Noise(X * 0.23, Z * 0.23) * 0.5;
				const double RoadMask = 1.0 - SmoothStep(kRoad - 0.5, kRoad + 0.7, Wd + Ragged * 0.45);
				const double ClearMask = ClearingAt(X, Z);
				const double MossMask = 0.5 + 0.6 * Noise(X / 17.0 + 9.0, Z / 17.0) + 0.3 * Noise(X / 5.0, Z / 5.0 + 4.0);
				const double NatureMask = std::max(ForestAt(X, Z), MH > 0.15 ? 1.0 : 0.0);

				LandMask[K + 0] = ToByte(RoadMask);
				LandMask[K + 1] = ToByte(ClearMask);
				LandMask[K + 2] = ToByte(MossMask);
				LandMask[K + 3] = ToByte(NatureMask);
			}
		}

		bPrepared = true;
	}

	double NeoKyotoTerrainMap::HeightAt(double X, double Z) const
	{
		constexpr int N = kLandRes;
		if (Heights.size() != static_cast<size_t>(N) * static_cast<size_t>(N))
		{
			return 0.0;
		}
		const double Fx = Clamp(((X + kExtent) / (2.0 * kExtent)) * static_cast<double>(N - 1), 0.0, static_cast<double>(N) - 1.001);
		const double Fz = Clamp(((Z + kExtent) / (2.0 * kExtent)) * static_cast<double>(N - 1), 0.0, static_cast<double>(N) - 1.001);
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
}
