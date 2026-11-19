// Copyright Komorebi Project. Deterministic 2D Perlin noise and seeded LCG RNG shared by every scene's ground.
#pragma once

#include "KomorebiCoreAPI.h"

namespace KomorebiCore
{
	inline constexpr uint32_t kNoiseSeed = 20261110u;

	/** Deterministic 2D Perlin noise (scaled by 1.4, approximately -1..1), bit-exact to legacy-web/src/world/noise.js. */
	KOMOREBICORE_API double Noise(double X, double Y);

	/** Seeded 32-bit LCG functor matching JS `rng(seed)`: s = (s * 1664525 + 1013904223) >>> 0; return s / 2^32. */
	struct Rng
	{
		uint32_t State = 0u;

		explicit constexpr Rng(uint32_t Seed = 0u) noexcept
			: State(Seed)
		{
		}

		constexpr uint32_t NextU32() noexcept
		{
			State = State * 1664525u + 1013904223u;
			return State;
		}

		constexpr double operator()() noexcept
		{
			return static_cast<double>(NextU32()) / 4294967296.0;
		}
	};
}
