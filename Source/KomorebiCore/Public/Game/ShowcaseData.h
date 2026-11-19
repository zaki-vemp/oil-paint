// Copyright Komorebi Project. Curated cinematic showcase camera shots and glide interpolation.
#pragma once

#include "KomorebiCoreAPI.h"
#include "Game/DayNightCycle.h"

#include <array>
#include <functional>

namespace KomorebiCore
{
	inline constexpr double kShotDuration = 7.0;

	struct ShowcaseShot
	{
		const char* Id = "";
		const char* Kicker = "";
		const char* Title = "";
		const char* Subtitle = "";
		EDayPhase Phase = EDayPhase::Night;
		const char* AccentHex = "#00f0ff";
		Vec3 FromPos;
		Vec3 ToPos;
		Vec3 FromTarget;
		Vec3 ToTarget;
	};

	/** Returns all 6 curated showcase shots in Three.js world metres, matching legacy-web/src/game/showcase.js. */
	KOMOREBICORE_API const std::array<ShowcaseShot, 6>& GetShowcaseShots();

	/** Hybrid linear/Hermite glide easing (0.32 * t + 0.68 * smoothstep(t)) matching showcase.js easeGlide(). */
	KOMOREBICORE_API double EaseGlide(double U);

	/** Evaluates camera position and look target along a showcase shot at normalized progress `T01` (assuming flat ground y = 0). */
	KOMOREBICORE_API void EvaluateShot(const ShowcaseShot& Shot, double T01, Vec3& OutPos, Vec3& OutTarget);

	/** Evaluates camera position and look target along a showcase shot using a ground height callback `HeightAt(x, z)`. */
	KOMOREBICORE_API void EvaluateShot(
		const ShowcaseShot& Shot,
		double T01,
		const std::function<double(double, double)>& HeightAt,
		Vec3& OutPos,
		Vec3& OutTarget);
}
