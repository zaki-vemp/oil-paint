// Copyright Komorebi Project. Curated cinematic showcase shots matching legacy-web/src/game/showcase.js.
#include "Game/ShowcaseData.h"

namespace KomorebiCore
{
	const std::array<ShowcaseShot, 6>& GetShowcaseShots()
	{
		static const std::array<ShowcaseShot, 6> kShots{{
			{
				"sunleaf-torii",
				"Shot 01 // Sunleaf Row \xC2\xB7 Midnight Neon",
				"Sunleaf Row Cyber-Skyline & Neon Torii Gateways",
				"World-Pinned Paint \xC2\xB7 Neon Reflections on Wet Asphalt",
				EDayPhase::Night,
				"#00f0ff",
				{ -118.2, 3.6, 214.0 },
				{ -121.2, 5.8, 191.5 },
				{ -120.0, 6.8, 172.0 },
				{ -82.0, 11.5, 156.0 },
			},
			{
				"daybreak-parks",
				"Shot 02 // West Woodland & Skyline \xC2\xB7 Daybreak Preview",
				"Daybreak Over Neo-Kyoto & Lush Woodland Parks",
				"Dynamic 10-Minute Day / Night Cycle \xC2\xB7 Cel-Painted Canopies, Sky-Glass High-Rises & Alpine Peaks",
				EDayPhase::Day,
				"#fcee0a",
				{ -112.0, 13.5, 76.0 },
				{ -86.0, 10.8, 52.0 },
				{ -28.0, 9.0, 96.0 },
				{ -12.0, 12.0, 118.0 },
			},
			{
				"petal-arcade",
				"Shot 03 // Petal Arcade \xC2\xB7 Living Street Simulation",
				"Petal Arcade Bustling Street Life, Night-Market Stalls & Fleet Vehicles",
				"Market Stalls, Pedestrians & Five Vehicle Classes",
				EDayPhase::Night,
				"#ff4fa3",
				{ -26.5, 3.8, 164.0 },
				{ -16.5, 3.1, 157.5 },
				{ -11.0, 1.9, 146.5 },
				{ -18.5, 2.3, 136.0 },
			},
			{
				"moonveil-skybridges",
				"Shot 04 // Jade Heights & Moonveil Stacks \xC2\xB7 Vertical City",
				"Jade Heights & Moonveil Elevated Skybridges",
				"6 Distinct Cyberpunk Districts \xC2\xB7 Batched Instanced Architecture \xC2\xB7 GPU-Driven Spirit Petals",
				EDayPhase::Night,
				"#19ffc6",
				{ -38.5, 5.4, -22.0 },
				{ -41.2, 10.2, -45.5 },
				{ -40.0, 14.2, -62.0 },
				{ -12.0, 12.8, -68.0 },
			},
			{
				"ashen-mech",
				"Shot 05 // Ashen Hollow \xC2\xB7 Combat & Enemy Escalation",
				"Ashen Hollow Aethelgard Siege Mech & Syndicate Garrison",
				"Swordplay, Parries & Perfect Dodges \xC2\xB7 Aethelgard Infantry and Siege Mechs",
				EDayPhase::Night,
				"#ff5a4d",
				{ 4.5, 4.9, -127.5 },
				{ -1.8, 3.6, -132.5 },
				{ -9.0, 2.4, -144.0 },
				{ -12.0, 2.2, -145.5 },
			},
			{
				"amber-sanctuary",
				"Shot 06 // Amber Sanctuary \xC2\xB7 Protocol Zero-Seven Finale",
				"The Amber Sanctuary Spire \xE2\x80\x94 Finale Overlook",
				"Six-Sector Campaign \xC2\xB7 Eight-Player Online City",
				EDayPhase::Night,
				"#ffb000",
				{ 21.0, 5.4, -256.0 },
				{ 33.0, 10.6, -263.5 },
				{ 33.0, 8.6, -288.0 },
				{ 33.0, 12.8, -288.0 },
			},
		}};
		return kShots;
	}

	double EaseGlide(double U)
	{
		const double T = Clamp(U, 0.0, 1.0);
		const double Smooth = T * T * (3.0 - 2.0 * T);
		return 0.32 * T + 0.68 * Smooth;
	}

	void EvaluateShot(const ShowcaseShot& Shot, double T01, Vec3& OutPos, Vec3& OutTarget)
	{
		EvaluateShot(Shot, T01, nullptr, OutPos, OutTarget);
	}

	void EvaluateShot(
		const ShowcaseShot& Shot,
		double T01,
		const std::function<double(double, double)>& HeightAt,
		Vec3& OutPos,
		Vec3& OutTarget)
	{
		const double K = EaseGlide(T01);
		const double Px = Lerp(Shot.FromPos.X, Shot.ToPos.X, K);
		const double Pz = Lerp(Shot.FromPos.Z, Shot.ToPos.Z, K);
		const double PyOff = Lerp(Shot.FromPos.Y, Shot.ToPos.Y, K);
		const double GroundPos = HeightAt ? HeightAt(Px, Pz) : 0.0;
		const double Py = std::max(GroundPos + 1.4, GroundPos + PyOff);

		const double Tx = Lerp(Shot.FromTarget.X, Shot.ToTarget.X, K);
		const double Tz = Lerp(Shot.FromTarget.Z, Shot.ToTarget.Z, K);
		const double TyOff = Lerp(Shot.FromTarget.Y, Shot.ToTarget.Y, K);
		const double GroundTarget = HeightAt ? HeightAt(Tx, Tz) : 0.0;
		const double Ty = GroundTarget + TyOff;

		OutPos = Vec3{ Px, Py, Pz };
		OutTarget = Vec3{ Tx, Ty, Tz };
	}
}
