// Copyright Komorebi Project. Deterministic 2D Perlin noise matching legacy-web/src/world/noise.js.
#include "World/KomorebiNoise.h"

#include <array>

namespace KomorebiCore
{
	namespace
	{
		constexpr std::array<uint8_t, 512> BuildPermutationTable() noexcept
		{
			std::array<uint8_t, 256> P{};
			for (int I = 0; I < 256; ++I)
			{
				P[static_cast<size_t>(I)] = static_cast<uint8_t>(I);
			}

			uint32_t S = kNoiseSeed;
			for (int I = 255; I > 0; --I)
			{
				S = S * 1664525u + 1013904223u;
				const uint32_t J = S % static_cast<uint32_t>(I + 1);
				const uint8_t Tmp = P[static_cast<size_t>(I)];
				P[static_cast<size_t>(I)] = P[static_cast<size_t>(J)];
				P[static_cast<size_t>(J)] = Tmp;
			}

			std::array<uint8_t, 512> Perm{};
			for (int I = 0; I < 512; ++I)
			{
				Perm[static_cast<size_t>(I)] = P[static_cast<size_t>(I & 255)];
			}
			return Perm;
		}

		constexpr std::array<uint8_t, 512> kPerm = BuildPermutationTable();

		constexpr double Fade(double T) noexcept
		{
			return T * T * T * (T * (T * 6.0 - 15.0) + 10.0);
		}

		constexpr double Grad(uint8_t H, double X, double Y) noexcept
		{
			return ((H & 1u) ? -X : X) + ((H & 2u) ? -Y : Y);
		}
	}

	double Noise(double X, double Y)
	{
		const double FloorX = std::floor(X);
		const double FloorY = std::floor(Y);
		const uint32_t Xi = static_cast<uint32_t>(static_cast<int64_t>(FloorX)) & 255u;
		const uint32_t Yi = static_cast<uint32_t>(static_cast<int64_t>(FloorY)) & 255u;
		const double Fx = X - FloorX;
		const double Fy = Y - FloorY;
		const double U = Fade(Fx);
		const double V = Fade(Fy);
		const uint32_t A = static_cast<uint32_t>(kPerm[Xi]) + Yi;
		const uint32_t B = static_cast<uint32_t>(kPerm[Xi + 1u]) + Yi;

		const double X0 = Lerp(Grad(kPerm[A], Fx, Fy), Grad(kPerm[B], Fx - 1.0, Fy), U);
		const double X1 = Lerp(Grad(kPerm[A + 1u], Fx, Fy - 1.0), Grad(kPerm[B + 1u], Fx - 1.0, Fy - 1.0), U);
		return Lerp(X0, X1, V) * 1.4;
	}
}
