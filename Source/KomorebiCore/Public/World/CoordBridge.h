// Copyright Komorebi Project. Coordinate bridge between Three.js (right-handed Y-up metres) and Unreal Engine (left-handed Z-up centimetres).
#pragma once

#include "KomorebiCoreAPI.h"

namespace KomorebiCore
{
	inline constexpr double kMetersToCm = 100.0;
	inline constexpr double kCmToMeters = 0.01;
	inline constexpr double kRadToDeg = 180.0 / kPi;
	inline constexpr double kDegToRad = kPi / 180.0;

	/** Converts a Three.js world position in metres (X right, Y up, Z south) to Unreal Engine centimetres (X north/-Z, Y east/+X, Z up/+Y). */
	inline Vec3 ThreeToUE(Vec3 ThreeMeters)
	{
		return Vec3{
			-ThreeMeters.Z * kMetersToCm,
			ThreeMeters.X * kMetersToCm,
			ThreeMeters.Y * kMetersToCm,
		};
	}

	/** Converts an Unreal Engine world position in centimetres back to Three.js metres. */
	inline Vec3 UEToThree(Vec3 UECentimeters)
	{
		return Vec3{
			UECentimeters.Y * kCmToMeters,
			UECentimeters.Z * kCmToMeters,
			-UECentimeters.X * kCmToMeters,
		};
	}

	/** Converts a unitless direction vector from Three.js axes to Unreal Engine axes without unit scaling. */
	inline Vec3 ThreeDirToUE(Vec3 ThreeDir)
	{
		return Vec3{
			-ThreeDir.Z,
			ThreeDir.X,
			ThreeDir.Y,
		};
	}

	/** Converts a unitless direction vector from Unreal Engine axes back to Three.js axes without unit scaling. */
	inline Vec3 UEDirToThree(Vec3 UEDir)
	{
		return Vec3{
			UEDir.Y,
			UEDir.Z,
			-UEDir.X,
		};
	}

	/** Converts Three.js yaw in radians (0 = +Z, +pi/2 = +X, pi = -Z) to Unreal Engine yaw in degrees (0 = +X/north, +90 = +Y/east). */
	inline double ThreeYawToUEYawDeg(double ThreeYawRad)
	{
		return WrapAngle(kPi - ThreeYawRad) * kRadToDeg;
	}

	/** Converts Unreal Engine yaw in degrees back to Three.js yaw in radians. */
	inline double UEYawDegToThreeYaw(double UEYawDeg)
	{
		return WrapAngle(kPi - UEYawDeg * kDegToRad);
	}

	/** Returns the horizontal forward direction (X, Z) in Three.js space for a Three.js yaw in radians. */
	inline Vec2 ThreeForwardFromYaw(double ThreeYawRad)
	{
		return Vec2{
			std::sin(ThreeYawRad),
			std::cos(ThreeYawRad),
		};
	}
}
