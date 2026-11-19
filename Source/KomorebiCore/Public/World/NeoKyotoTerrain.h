// Copyright Komorebi Project. Neo-Kyoto terrain heightfield, road spine, woodland biome, and perimeter mountains.
#pragma once

#include "KomorebiCoreAPI.h"

#include <vector>

namespace KomorebiCore
{
	inline constexpr double kExtent = 360.0;
	inline constexpr int kLandRes = 1025;
	inline constexpr double kRoad = 3.6;
	inline constexpr double kScale = 3.0;

	struct ClearingArea
	{
		double X = 11.0 * kScale;
		double Z = -96.0 * kScale;
		double Radius = 26.0;
	};

	/** Player spawn origin along the winding trail: (0, 78 * kScale). */
	KOMOREBICORE_API Vec2 Start();

	/** Amber Sanctuary clearing at the northern end of the trail. */
	KOMOREBICORE_API ClearingArea Clearing();

	/** Catmull-Rom sampled waypoints along the Neo-Kyoto central trail (193 points). */
	KOMOREBICORE_API const std::vector<Vec2>& WaySamples();

	/** Metres from (X, Z) to the nearest road: the winding trail or a grid avenue/street. */
	KOMOREBICORE_API double WayDist(double X, double Z);

	/** Sanctuary clearing mask in [0..1] with organic noise border. */
	KOMOREBICORE_API double ClearingAt(double X, double Z);

	/** Perimeter and corner alpine mountain elevation in metres (>= 0), strictly 0 inside the city. */
	KOMOREBICORE_API double MountainHeight(double X, double Z);

	/** Woodland/park biome density in [0..1]. */
	KOMOREBICORE_API double ForestAt(double X, double Z);

	/** Analytic terrain elevation at (X, Z) given road distance `WayD`. */
	KOMOREBICORE_API double NeoKyotoRawHeight(double X, double Z, double WayD);

	/** Baked 1025x1025 height and RGBA land-mask textures for Neo-Kyoto, matching legacy-web/src/world/land.js. */
	class KOMOREBICORE_API NeoKyotoTerrainMap
	{
	public:
		std::vector<float> Heights;
		std::vector<uint8_t> LandMask;

		/** Builds the 1025x1025 heightfield and RGBA land mask (R=road, G=clearing, B=loam/moss, A=nature). */
		void Prepare();

		/** Bilinearly samples the prepared heightfield at world (X, Z) metres. */
		double HeightAt(double X, double Z) const;

		bool IsPrepared() const noexcept { return bPrepared; }
		static constexpr int Resolution() noexcept { return kLandRes; }

	private:
		bool bPrepared = false;
	};
}
