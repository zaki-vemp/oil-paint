// Copyright Komorebi Project. Neo-Kyoto city street grid matching legacy-web/src/world/grid.js.
#include "World/WorldGrid.h"

#include <limits>

namespace KomorebiCore
{
	namespace
	{
		std::vector<double> BuildGridLines(double MinVal, double MaxVal, double Step)
		{
			std::vector<double> Out;
			for (double V = MinVal; V <= MaxVal + 1e-3; V += Step)
			{
				Out.push_back(V);
			}
			return Out;
		}

		double NearestLine(const std::vector<double>& Lines, double Value)
		{
			double Best = Lines[0];
			double BestDist = std::abs(Best - Value);
			for (size_t I = 1; I < Lines.size(); ++I)
			{
				const double Dist = std::abs(Lines[I] - Value);
				if (Dist < BestDist)
				{
					Best = Lines[I];
					BestDist = Dist;
				}
			}
			return Best;
		}
	}

	const std::vector<double>& Avenues()
	{
		static const std::vector<double> Lines = BuildGridLines(kGridMin, kGridMax, kSpacing);
		return Lines;
	}

	const std::vector<double>& Streets()
	{
		return Avenues();
	}

	int ZoneAt(double X, double Z)
	{
		if (Z > ZoneSplit.Downtown)
		{
			return 0;
		}
		if (Z > ZoneSplit.Market)
		{
			return 1;
		}
		if (X > ZoneSplit.East)
		{
			return 2;
		}
		return 3;
	}

	double GridDist(double X, double Z)
	{
		double Best = std::numeric_limits<double>::infinity();
		const bool bPastZ = Z < kGridMin || Z > kGridMax;
		const bool bPastX = X < kGridMin || X > kGridMax;
		const double ClampDz = Z < kGridMin ? (Z - kGridMin) : (Z - kGridMax);
		const double ClampDx = X < kGridMin ? (X - kGridMin) : (X - kGridMax);

		for (double Ax : Avenues())
		{
			const double Dist = bPastZ ? std::hypot(X - Ax, ClampDz) : std::abs(X - Ax);
			if (Dist < Best)
			{
				Best = Dist;
			}
		}
		for (double Sz : Streets())
		{
			const double Dist = bPastX ? std::hypot(Z - Sz, ClampDx) : std::abs(Z - Sz);
			if (Dist < Best)
			{
				Best = Dist;
			}
		}
		return Best;
	}

	std::vector<BlockLot> BlockLots()
	{
		static constexpr double kSlots[5][2] = {
			{ -8.0, -8.0 },
			{ 8.0, -8.0 },
			{ -8.0, 8.0 },
			{ 8.0, 8.0 },
			{ 0.0, 0.0 },
		};
		static constexpr int kLotsPerZone[4] = { 4, 3, 3, 2 };

		const auto& Aves = Avenues();
		const auto& Strs = Streets();
		std::vector<BlockLot> Out;
		Out.reserve(600);

		for (size_t I = 0; I + 1 < Aves.size(); ++I)
		{
			for (size_t J = 0; J + 1 < Strs.size(); ++J)
			{
				const double Cx = (Aves[I] + Aves[I + 1]) * 0.5;
				const double Cz = (Strs[J] + Strs[J + 1]) * 0.5;
				const int Zone = ZoneAt(Cx, Cz);
				for (int S = 0; S < kLotsPerZone[Zone]; ++S)
				{
					Out.push_back(BlockLot{
						Cx + kSlots[S][0],
						Cz + kSlots[S][1],
						Zone,
						S == 4,
					});
				}
			}
		}
		return Out;
	}

	PavementStep NextPavement(double X, double Z, double Dir, bool bCross)
	{
		const double Ax = NearestLine(Avenues(), X);
		const double Sz = NearestLine(Streets(), Z);
		const auto ClampCoord = [](double V)
		{
			return Clamp(V, kGridMin + kKerb, kGridMax - kKerb);
		};

		if (std::abs(X - Ax) <= std::abs(Z - Sz))
		{
			const double Side = (X - Ax) < 0.0 ? -1.0 : 1.0;
			if (bCross && std::abs(Z - Sz) < 7.0)
			{
				return PavementStep{ Ax - Side * kKerb, Sz, true };
			}
			return PavementStep{ Ax + Side * kKerb, ClampCoord(Z + Dir * 12.0), false };
		}

		const double Side = (Z - Sz) < 0.0 ? -1.0 : 1.0;
		if (bCross && std::abs(X - Ax) < 7.0)
		{
			return PavementStep{ Ax, Sz - Side * kKerb, true };
		}
		return PavementStep{ ClampCoord(X + Dir * 12.0), Sz + Side * kKerb, false };
	}
}
