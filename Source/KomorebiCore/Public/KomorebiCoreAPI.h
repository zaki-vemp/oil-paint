// Copyright Komorebi Project. Engine-agnostic export macro and lightweight math primitives for KomorebiCore.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#if defined(__UNREAL__)
	#include "CoreMinimal.h"
#else
	#ifndef KOMOREBICORE_API
		#define KOMOREBICORE_API
	#endif
#endif

namespace KomorebiCore
{
	inline constexpr double kPi = 3.14159265358979323846;
	inline constexpr double kTau = 6.28318530717958647692;

	struct Vec2
	{
		double X = 0.0;
		double Z = 0.0;
	};

	struct Vec3
	{
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
	};

	struct RGB
	{
		double R = 0.0;
		double G = 0.0;
		double B = 0.0;
	};

	inline double Clamp(double Value, double Lo, double Hi)
	{
		return std::max(Lo, std::min(Hi, Value));
	}

	inline double Lerp(double A, double B, double T)
	{
		return A + (B - A) * T;
	}

	inline double SmoothStep(double Edge0, double Edge1, double X)
	{
		if (Edge1 == Edge0)
		{
			return X < Edge0 ? 0.0 : 1.0;
		}
		const double T = Clamp((X - Edge0) / (Edge1 - Edge0), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	inline double WrapAngle(double AngleRad)
	{
		return std::atan2(std::sin(AngleRad), std::cos(AngleRad));
	}

	inline double Toward(double Value, double Step)
	{
		const double AbsV = std::abs(Value);
		const double Next = std::max(0.0, AbsV - Step);
		return Value < 0.0 ? -Next : (Value > 0.0 ? Next : 0.0);
	}
}
