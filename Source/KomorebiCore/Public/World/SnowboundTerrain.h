// Copyright Komorebi Project. Snowbound (Aurora) alpine ridge heightfield, horns, cabin shelf, and land mask.
#pragma once

#include "KomorebiCoreAPI.h"

#include <vector>

namespace KomorebiCore
{
	inline constexpr double kSnowExtent = 512.0;
	inline constexpr int kSnowLandRes = 1025;

	struct HornParams
	{
		double X = 0.0;
		double Z = 0.0;
		double Top = 0.0;
		double Fall = 0.0;
		double Sx = 0.0;
		double Sz = 0.0;
		double Sheer = 0.0;
		double Turn = 0.0;
	};

	/** Normalized moonlight direction vector matching THREE.Vector3(-0.78, 0.44, 0.3).normalize(). */
	KOMOREBICORE_API Vec3 MoonDir();

	/** Horizontal X offset of the snow ridge crest at world coordinate Z. */
	KOMOREBICORE_API double CrestX(double Z);

	/** Elevation of the snow ridge crest at world coordinate Z. */
	KOMOREBICORE_API double CrestH(double Z);

	/** Start position among the spruces at the south end of the ridge. */
	KOMOREBICORE_API Vec2 SnowStart();

	/** Knoll viewpoint along the ridge crest. */
	KOMOREBICORE_API Vec2 SnowKnoll();

	/** Cabin floor position (X, Y, Z) on the levelled shelf north of the knoll. */
	KOMOREBICORE_API Vec3 SnowCabin();

	/** Primary alpine horn parameters at the northern end of the ridge. */
	KOMOREBICORE_API HornParams SnowPeak();

	/** Baked 1025x1025 Snowbound terrain heightfield and RGBA land mask (R=rock, G=horn, B=level, A=255). */
	class KOMOREBICORE_API SnowboundTerrainMap
	{
	public:
		std::vector<float> Heights;
		std::vector<uint8_t> LandMask;

		/** Exact analytic Snowbound elevation at (X, Z), optionally writing the horn blend weight [0..1]. */
		static double RawHeight(double X, double Z, double* OutHornWeight = nullptr);
		static double RawHeight(double X, double Z, double& OutHornWeight);

		/** Builds the 1025x1025 heightfield and rock/horn/level mask matching legacy-web/src/night/land.js. */
		void Prepare();

		/** Bilinearly samples the prepared heightfield at world (X, Z) metres. */
		double HeightAt(double X, double Z) const;

		/** Nearest-texel query of [rock, horn] in [0..1]. */
		void LandAt(double X, double Z, double& OutRock, double& OutHorn) const;

		/** Finite-difference surface normal matching legacy-web/src/night/land.js normalAt(). */
		Vec3 NormalAt(double X, double Z) const;

		bool IsPrepared() const noexcept { return bPrepared; }
		static constexpr int Resolution() noexcept { return kSnowLandRes; }

	private:
		bool bPrepared = false;
	};
}
