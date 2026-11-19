// Copyright Komorebi Project. Standalone C++20 verification suite porting all 9 legacy JS invariant demos
// plus coordinate conversion, terrain heightfields, showcase curves, and audio syllable formants.
#include "KomorebiCoreAPI.h"
#include "World/CoordBridge.h"
#include "World/KomorebiNoise.h"
#include "World/WorldGrid.h"
#include "World/NeoKyotoTerrain.h"
#include "World/SnowboundTerrain.h"
#include "Game/ChaptersData.h"
#include "Game/StreetConfig.h"
#include "Game/LevelsData.h"
#include "Game/CombatRules.h"
#include "Game/TacticsRules.h"
#include "Game/DuelStateMachine.h"
#include "Game/VehiclePhysics.h"
#include "Game/DayNightCycle.h"
#include "Game/StoryDatabase.h"
#include "Game/OnlineMeta.h"
#include "Game/ShowcaseData.h"
#include "Game/AudioTheory.h"

#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace KomorebiCore;

namespace
{
	void Require(bool Cond, const std::string& Msg)
	{
		if (!Cond)
		{
			throw std::runtime_error("FAILED: " + Msg);
		}
	}

	bool Near(double A, double B, double Eps = 1e-6)
	{
		return std::abs(A - B) <= Eps;
	}

	void TestCoordBridge()
	{
		const Vec3 StartThree{ 0.0, 0.0, 234.0 };
		const Vec3 StartUE = ThreeToUE(StartThree);
		Require(Near(StartUE.X, -23400.0) && Near(StartUE.Y, 0.0) && Near(StartUE.Z, 0.0), "START Three->UE");
		const Vec3 RoundTrip = UEToThree(StartUE);
		Require(Near(RoundTrip.X, StartThree.X) && Near(RoundTrip.Y, StartThree.Y) && Near(RoundTrip.Z, StartThree.Z), "Round-trip START");

		const Vec3 EastNorthUp{ 12.5, 4.2, -30.0 };
		const Vec3 Ue = ThreeToUE(EastNorthUp);
		Require(Near(Ue.X, 3000.0) && Near(Ue.Y, 1250.0) && Near(Ue.Z, 420.0), "EastNorthUp mapping");

		// Three yaw PI = North (-Z) -> UE yaw 0 deg (+X)
		Require(Near(ThreeYawToUEYawDeg(kPi), 0.0, 1e-5), "North yaw -> 0 deg");
		// Three yaw +PI/2 = East (+X) -> UE yaw +90 deg (+Y)
		Require(Near(ThreeYawToUEYawDeg(kPi * 0.5), 90.0, 1e-5), "East yaw -> +90 deg");
		// Three yaw -PI/2 = West (-X) -> UE yaw -90 deg (-Y)
		Require(Near(ThreeYawToUEYawDeg(-kPi * 0.5), -90.0, 1e-5), "West yaw -> -90 deg");
		Require(Near(UEYawDegToThreeYaw(90.0), kPi * 0.5, 1e-5), "Inverse yaw +90 deg");
	}

	void TestNoiseAndGrid()
	{
		const double N1 = Noise(1.25, -3.5);
		const double N2 = Noise(1.25, -3.5);
		Require(std::isfinite(N1) && Near(N1, N2, 1e-12), "Noise deterministic");
		Require(N1 >= -1.5 && N1 <= 1.5, "Noise range");

		Rng R(4409u);
		const double V0 = R();
		const double V1 = R();
		Require(V0 >= 0.0 && V0 < 1.0 && V1 >= 0.0 && V1 < 1.0 && V0 != V1, "LCG RNG");

		const auto& Aves = Avenues();
		Require(Aves.size() == 15 && Near(Aves.front(), kGridMin) && Near(Aves.back(), kGridMax), "15 avenues");
		Require(GridDist(0.0, 100.0) <= 1e-6, "On avenue");
		Require(Near(GridDist(20.0, 20.0), 20.0), "Block centre");
		Require(ZoneAt(0.0, 180.0) == 0 && ZoneAt(0.0, 20.0) == 1 && ZoneAt(40.0, -60.0) == 2 && ZoneAt(-80.0, -60.0) == 3, "ZoneAt");
		const auto Lots = BlockLots();
		Require(Lots.size() >= 400, "City has 400+ lots");
		for (const auto& Lot : Lots)
		{
			Require(GridDist(Lot.X, Lot.Z) >= 12.0, "Lot clear of road");
		}
		const auto Step = NextPavement(5.8, 100.0, 1.0, false);
		Require(std::abs(Step.X - 5.8) <= 0.01 && Step.Z > 100.0 && !Step.bCrossed, "Sidewalk step");
		const auto Over = NextPavement(5.8, 6.0, 1.0, true);
		Require(Over.bCrossed && Over.X < 0.0, "Intersection crossing");
	}

	void TestTerrains()
	{
		Require(WaySamples().size() == 193, "WaySamples count");
		Require(Near(Start().X, 0.0) && Near(Start().Z, 234.0), "NeoKyoto Start");
		Require(Near(MountainHeight(0.0, 0.0), 0.0), "Downtown flat");
		Require(Near(MountainHeight(Start().X, Start().Z), 0.0), "Start flat");
		Require(MountainHeight(-340.0, 340.0) > 20.0, "NW corner mountain");
		Require(ForestAt(-100.0, 40.0) > 0.5, "West woodland park");

		NeoKyotoTerrainMap CityMap;
		CityMap.Prepare();
		Require(CityMap.IsPrepared(), "CityMap prepared");
		Require(Near(CityMap.HeightAt(0.0, 0.0), 0.0, 1e-3), "City centre height 0");
		Require(Near(CityMap.HeightAt(Start().X, Start().Z), 0.0, 1e-3), "Start height 0");
		Require(CityMap.HeightAt(-340.0, 340.0) > 20.0, "Baked mountain elevation");

		SnowboundTerrainMap SnowMap;
		SnowMap.Prepare();
		Require(SnowMap.IsPrepared(), "SnowMap prepared");
		const Vec3 Cab = SnowCabin();
		Require(std::abs(SnowMap.HeightAt(Cab.X, Cab.Z) - Cab.Y) < 0.5, "Snowbound cabin shelf level");
		Require(SnowMap.HeightAt(SnowPeak().X, SnowPeak().Z) > 300.0, "Snowbound main horn peak");
		const Vec3 Nrm = SnowMap.NormalAt(Cab.X, Cab.Z);
		Require(Nrm.Y > 0.9, "Cabin shelf normal upright");
	}

	void TestChaptersAndLevels()
	{
		const auto& Chapters = GetChapters();
		Require(Chapters.size() == 6, "6 chapters");
		std::set<std::string_view> Styles;
		for (const auto& C : Chapters)
		{
			Styles.insert(C.Style);
			for (const RGB& Col : { C.Grass, C.Foliage, C.LeafLight, C.Sky, C.Haze, C.Path, C.Day.Grass, C.Day.Path, C.Day.Sky, C.Day.Haze })
			{
				Require(Col.R >= 0.0 && Col.R <= 1.0 && Col.G >= 0.0 && Col.G <= 1.0 && Col.B >= 0.0 && Col.B <= 1.0, "Palette in 0..1");
			}
		}
		Require(Styles.size() == 2, "2 HUD styles");
		auto At = [](double Z, int Cl = 6, bool Roam = false) { return ChapterAt(Z * kWorldScale, Cl, Roam); };
		Require(At(78) == 0 && At(-50) == 4 && At(-70) == 5 && At(-70, 1) == 1 && At(-70, 0, true) == 5, "Chapter progression");

		const auto& Levels = GetLevels();
		const auto& Duels = GetDuelConfigs();
		const auto& Goal = GetGoal();
		Require(Levels.size() >= 6, "At least 6 levels");
		for (size_t I = 0; I < Levels.size(); ++I)
		{
			const auto& L = Levels[I];
			Require(!L.Name.empty() && !L.Spots.empty(), "Valid level");
			const auto& Rival = Duels[I];
			const double GateZ = L.Gate.has_value() ? L.Gate->Z : Goal.Z;
			Require(Rival.Z < L.Checkpoint.Z && Rival.Z > GateZ, "Rival ends chapter");
			const double PrevGateZ = I > 0 ? Levels[I - 1].Gate->Z : 1e9;
			for (const auto& S : L.Spots)
			{
				Require(S.Z >= (L.Gate.has_value() ? L.Gate->Z + 3.0 : -1e9) && S.Z <= PrevGateZ - 3.0, "Spawn inside gates");
			}
			if (I > 0 && Levels[I - 1].Gate.has_value() && L.Gate.has_value())
			{
				Require(Levels[I - 1].Gate->Z > L.Gate->Z, "Gates strictly decreasing");
			}
		}
		for (EEnemyKind K : kCrewKinds)
		{
			bool Found = false;
			for (const auto& L : Levels)
			{
				for (const auto& S : L.Spots)
				{
					if (S.Kind == K) Found = true;
				}
			}
			Require(Found, "Every crew kind met");
		}
		const PowerSet P2 = PowersFor(2);
		Require(P2.Spark && P2.Dash && !P2.Heavy && !P2.Flare && !P2.Whirl, "Powers for 2 cleared");
		const PowerSet PAll = PowersFor(static_cast<int>(Levels.size()));
		for (EPowerKind PK : kPowerOrder)
		{
			Require(PAll.Has(PK), "All powers earned by clearing");
		}
		const auto U0 = UnlocksFor(0);
		Require(U0.size() == 3 && U0[0] == EVehicleKind::Car && U0[1] == EVehicleKind::Bicycle && U0[2] == EVehicleKind::Bike, "Starter garage unlocks");
		const auto U2 = UnlocksFor(2);
		Require(std::find(U2.begin(), U2.end(), EVehicleKind::Pickup) != U2.end(), "Pickup unlocks by cleared=2");
	}

	void TestConfigTacticsCombat()
	{
		// StreetConfig invariants
		Require(kGuilt > 0 && kGuilt * 2 < 100, "Guilt bounds");
		Require(kWitnessConfig.Radius >= 25.0 && kWitnessConfig.Radius <= 40.0 && kWitnessConfig.Close < kWitnessConfig.Radius, "Witness block radius");
		Require(kRamConfig.Min < kRamConfig.Stumble && kRamConfig.Stumble < kRamConfig.Kill && Near(kRamConfig.Crew, 42.0), "Ram bands");
		Require(kHeatConfig.Hurt < kHeatConfig.Stars[0] && kHeatConfig.Kill >= kHeatConfig.Stars[0] && kHeatConfig.Kill < kHeatConfig.Stars[1], "Heat star curve");
		Require(kHeatConfig.Stars[1] < kHeatConfig.Stars[2] && kHeatConfig.Stars[2] < kHeatConfig.Max, "Heat star monotonic");
		Require(kHeatConfig.Decay > 0.0 && kHeatConfig.Safe > kHeatConfig.Decay && kCashConfig.Thief > 0 && kCashConfig.Enemy > 0, "Heat decay & cash");

		// Tactics invariants
		std::vector<CrewMemberOrder> Group{
			CrewMemberOrder{ 0.0, 0.0, false, false, 1, 3.6 },
			CrewMemberOrder{ 0.3, 4.0, false, false, 1, 3.6 },
			CrewMemberOrder{ 0.0, 3.0, true, false, 1, 3.6 },
		};
		ComputeCrewOrder(Group);
		Require(!Group[0].Lead && Group[1].Lead && Group[2].Lead, "Committed melee keeps lead + ranged aims");
		for (EEnemyKind K : kCrewKinds)
		{
			const auto& Moves = GetEnemyPatterns(K);
			Require(Moves.size() == 2 && Moves[0].Name != Moves[1].Name, "2 distinct moves per kind");
			for (const auto& M : Moves)
			{
				Require(M.Wind > 0.28 && M.Recover >= 0.6 && M.Reach > 0.0, "Readable move timings");
			}
		}
		Require(ChooseAttack(EEnemyKind::Gunner, 1.5, false, 0) == 1 && ChooseAttack(EEnemyKind::Gunner, 6.0, false, 0) == 0, "Gunner stock strike vs pulse shot");

		// Combat poses
		for (EAttackType T : { EAttackType::Cut, EAttackType::Backhand, EAttackType::Thrust, EAttackType::Heavy, EAttackType::Spin, EAttackType::Counter, EAttackType::Advance })
		{
			const auto& A = GetAttackSpec(T);
			Require(A.HitStart < A.HitEnd && A.HitEnd < 1.0 && A.Duration > 0.4, "Attack timing");
			for (int I = 0; I <= 100; ++I)
			{
				const auto F = SampleAttack(T, I / 100.0);
				Require(std::isfinite(F.Grip[0]) && std::isfinite(F.Blade[2]) && std::isfinite(F.TrunkTurn), "Finite attack pose");
			}
		}

		// Hunt balance invariants
		auto P = SpendPower(kPowerMax);
		Require(P.has_value(), "Spend 1");
		P = SpendPower(*P);
		Require(P.has_value(), "Spend 2");
		P = SpendPower(*P);
		Require(P.has_value() && Near(*P, 0.0), "Spend 3 -> empty");
		Require(!SpendPower(*P).has_value(), "4th spend fails");
		Require(Near(StepPower(0.0, kPowerFill / kPowerMax), 1.0), "One charge refill");
		Require(Near(StepPower(0.0, kPowerFill), kPowerMax), "Full refill");
		Require(kPowerFill >= 30.0 && kPowerFill <= 50.0, "Power fill window");
		Require(InFront(0, 0, 0, 0.2, 1.2, 1.9, 1.2), "In front");
		Require(!InFront(0, 0, 0, 0, -1.2, 1.9, 1.2, 0.0), "Behind excluded");
		Require(InFront(0, 0, 0, 0, -0.4, 1.9, 1.2, 0.65), "Close behind included");
		Require(StrikeHits(0, 0, 0, 2.0, 0, 0, 1.5, 0), "Committed strike hits");
		Require(!StrikeHits(0, 0, 0, 2.0, 2, 0, 0, 0) && !StrikeHits(0, 0, 0, 2.0, 0, 1.1, 1.5, 0) && !StrikeHits(0, 0, 0, 2.0, 0, 0, 3, 0), "Strike dodgeable");
		Require(GetHitHp(EEnemyKind::Punk) - GetBoltDamage(EEnemyKind::Punk) <= 0.0, "Spark drops punk");
		Require(GetHitHp(EEnemyKind::Enforcer) - GetBoltDamage(EEnemyKind::Enforcer) > 0.0, "Spark does not drop enforcer");
		Require(GetHitHp(EEnemyKind::Punk) - 2.0 * GetSwordDamage(EEnemyKind::Punk) <= 0.0, "2 swings drop punk");
		Require(GetHitHp(EEnemyKind::Enforcer) - 3.0 * GetSwordDamage(EEnemyKind::Enforcer) > 0.0, "Enforcer survives 3 swings");
		Require(GetHitHp(EEnemyKind::Brute) - 5.0 * (3.0 * GetSwordDamage(EEnemyKind::Brute)) <= 0.0, "5 heavies drop brute");
		Require(GetHitHp(EEnemyKind::Punk) - kWhirlMul * GetSwordDamage(EEnemyKind::Punk) <= 0.0, "Whirl clears punk");
		Require(GetKnockback("heavy") * GetHeft(EEnemyKind::Brute) < GetKnockback("sword") * GetHeft(EEnemyKind::Punk), "Brute heavy knockback < punk sword");
		Require(kLaunchSpeed * GetHeft(EEnemyKind::Brute) < 2.2 && kLaunchSpeed * GetHeft(EEnemyKind::Punk) > 5.0, "Punks fly, mech stays down");
		Require(GetRamBand(kRamConfig.Stumble - 0.1) == ERamBand::Stumble && GetRamBand(kRamConfig.Kill - 0.1) == ERamBand::Knock && GetRamBand(kRamConfig.Kill) == ERamBand::Kill, "Ram bands");
	}

	void TestDuels()
	{
		const auto& Duels = GetDuelConfigs();
		Require(Duels.size() == 6, "6 duels");
		for (int I = 0; I < 6; ++I)
		{
			DuelState E = CreateDuel(I);
			DuelStepInput WaitIn{ E.X, E.Z + 2.0, false, [](double, double) {}, false };
			StepDuel(E, 1.0 / 60.0, WaitIn);
			Require(E.Phase == EDuelPhase::Waiting, "Crews before duel");

			DuelStepInput ReadyIn{ E.X, E.Z + 2.0, true, [](double, double) {}, false };
			Require(StepDuel(E, 1.0 / 60.0, ReadyIn) == EDuelEvent::Intro, "Duel intro");
			for (int K = 0; K < 300 && !DuelHit(E, E.X, 0.0, E.Z + 1.0, 0.0); ++K)
			{
				StepDuel(E, 1.0 / 60.0, ReadyIn);
			}
			Require(DuelHit(E, E.X, 0.0, E.Z + 1.0, 0.0) && !DuelHit(E, E.X + 5.0, 0.0, E.Z, 0.0), "Committed duel strike");
			Require(GetDuelVisual(E).Mode == EDuelVisualMode::Danger, "Danger visual");
			E.Phase = EDuelPhase::Windup;
			Require(DamageRival(E, 30.0, "sword") < 10, "Rival guards windup");
			E.Phase = EDuelPhase::Recovery;
			Require(DamageRival(E, 30.0, "sword") > 30 && E.Punishes > 0, "Punish recovery");
			Require(GetDuelVisual(E).Mode == EDuelVisualMode::Opening, "Opening visual");
		}
		DuelState Iron = CreateDuel(3);
		Iron.Phase = EDuelPhase::Windup;
		DamageRival(Iron, 66.0, "heavy");
		DamageRival(Iron, 66.0, "heavy");
		Require(Iron.Phase == EDuelPhase::Recovery, "2 heavies break Iron Will guard");
		Iron.Phase = EDuelPhase::Windup;
		DamageRival(Iron, 60.0, "flare");
		Require(Iron.Phase == EDuelPhase::Recovery, "Flare breaks guard");

		DuelState Twin = CreateDuel(2);
		DuelStepInput TwinIn{ Twin.X, Twin.Z + 2.0, true, [](double, double) {}, false };
		int Beats = 0;
		for (int K = 0; K < 280 && Twin.Phase != EDuelPhase::Recovery; ++K)
		{
			StepDuel(Twin, 1.0 / 60.0, TwinIn);
			if (DuelHit(Twin, TwinIn.X, 0.0, TwinIn.Z, 0.0))
			{
				Beats++;
				Twin.HitDone = Twin.Connected = true;
			}
		}
		Require(Beats == 2 && !Twin.Evaded, "Twin Current 2-beat window");
	}

	void TestVehiclePhysics()
	{
		for (EVehicleKind Kind : { EVehicleKind::Car, EVehicleKind::Sports, EVehicleKind::Pickup, EVehicleKind::Bike, EVehicleKind::Bicycle })
		{
			const auto& Spec = GetVehicleSpec(Kind);
			VehicleState V;
			V.Kind = Kind;
			double Fastest = 0.0;
			for (int I = 0; I < 60 * 14; ++I)
			{
				DriveVehicle(V, Spec, { 1.0, 0.0, false }, 1.0 / 60.0);
				Fastest = std::max(Fastest, V.Vf);
			}
			Require(Fastest > Spec.Top * 0.85 && Fastest <= Spec.Top * 1.001, "Top speed");

			double Dist = 0.0;
			for (int I = 0; I < 60 * 6 && V.Vf > 0.01; ++I)
			{
				DriveVehicle(V, Spec, { -1.0, 0.0, false }, 1.0 / 60.0);
				Dist += V.Vz / 60.0;
			}
			Require(Dist < 45.0 && std::abs(V.Vf) < 1.2, "Braking distance");

			auto Corner = [&](bool Hand)
			{
				VehicleState C;
				C.Kind = Kind;
				C.Vz = 12.0;
				double Slip = 0.0;
				for (int I = 0; I < 90; ++I)
				{
					DriveVehicle(C, Spec, { 0.4, 1.0, Hand }, 1.0 / 60.0);
					Slip = std::max(Slip, std::abs(C.Vx * std::cos(C.Yaw) - C.Vz * std::sin(C.Yaw)));
				}
				return std::pair<double, double>{ C.Yaw, Slip };
			};
			const auto [HeldYaw, HeldSlip] = Corner(false);
			const auto [SlidYaw, SlidSlip] = Corner(true);
			(void)SlidYaw;
			Require(HeldYaw < -0.8, "Turns right");
			Require(SlidSlip > HeldSlip * 1.5, "Handbrake increases slip");

			VehicleState Air;
			Air.Vz = 10.0;
			DriveVehicle(Air, Spec, { 1.0, 1.0, false }, 1.0 / 60.0, false);
			Require(Near(Air.Yaw, 0.0) && Near(Air.Vz, 10.0), "Airborne immunity");
		}

		// Suspension & flight
		VehicleState Sprung;
		Sprung.Kind = EVehicleKind::Car;
		const auto& CarSpec = GetVehicleSpec(EVehicleKind::Car);
		Sprung.Wheels = MakeDefaultWheels(CarSpec);
		SettleSuspension(Sprung, CarSpec, [](double X, double Z) { return Z * 0.2 + X * 0.1; });
		Require(Near(Sprung.Pitch, -std::atan(0.2)) && Near(Sprung.Roll, std::atan(0.1)), "Banked slope alignment");

		auto Flat = [](double, double) { return 0.0; };
		SettleSuspension(Sprung, CarSpec, Flat);
		Sprung.Y = 1.4;
		double Bottom = 0.0, Landing = 0.0;
		const double Dt = 1.0 / 120.0;
		SuspendVehicle(Sprung, CarSpec, Flat, Dt);
		Require(Sprung.Air > 0.0 && Near(Sprung.Vy, -kVehicleGravity * Dt), "Free flight above travel");
		for (int I = 0; I < 360; ++I)
		{
			Landing = std::max(Landing, SuspendVehicle(Sprung, CarSpec, Flat, Dt));
			Bottom = std::min(Bottom, Sprung.Y);
		}
		Require(Landing >= 5.0 && Bottom <= -0.02 && Bottom >= -CarSpec.Travel && std::abs(Sprung.Y) < 0.001 && Sprung.Air == 0.0, "Touchdown settle");

		// Momentum & energy in CollideVehicles
		VehicleState A, B;
		A.Kind = EVehicleKind::Car; A.Vz = 20.0;
		B.Kind = EVehicleKind::Car; B.Z = 4.0;
		auto Energy = [&](const VehicleState& X) { return GetVehicleSpec(X.Kind).Mass * (X.Vx * X.Vx + X.Vz * X.Vz) * 0.5; };
		const double BeforeE = Energy(A) + Energy(B);
		const double Mom = CarSpec.Mass * A.Vz;
		const double Closing = CollideVehicles(A, B);
		Require(Near(Closing, 20.0) && B.Vz >= 10.0 && A.Vz <= 10.0 && B.Z - A.Z >= CarSpec.Len, "Rear-end transfer");
		Require(Near(CarSpec.Mass * A.Vz + CarSpec.Mass * B.Vz, Mom) && Energy(A) + Energy(B) <= BeforeE, "Momentum conserved & energy lost");

		// Broadside tips bike
		VehicleState Bike, Car;
		Bike.Kind = EVehicleKind::Bike;
		Car.Kind = EVehicleKind::Car; Car.X = -1.2; Car.Vx = 25.0;
		Require(CollideVehicles(Car, Bike) > 20.0 && Bike.Vx > Car.Vx && std::abs(Bike.RollRate) > 7.0, "Broadside impulse on bike");
		for (int I = 0; I < 600; ++I) RockVehicle(Bike, 1.0 / 120.0);
		Require(Bike.Tipped && std::cos(Bike.Tilt) <= -0.9, "Tipped settles upside down");
	}

	void TestDayNightStoryOnlineAudio()
	{
		// Day/night
		const auto S0 = DayNightAt(0.0, false);
		Require(S0.Phase == EDayPhase::Day && Near(S0.Night, 0.0) && S0.ClockMMSS == "10:00", "Day start");
		const auto S600 = DayNightAt(600.0, false);
		Require(S600.Phase == EDayPhase::Night && S600.Twilight > 0.9, "Twilight shift at 600s");
		const auto S610 = DayNightAt(610.0, false);
		Require(S610.Phase == EDayPhase::Night && Near(S610.Night, 1.0), "Full night at 610s");

		std::vector<EDayPhase> Shifts;
		DayNightController Ctrl(EDayPhase::Day, false, [&](EDayPhase P, const DayNightState&) { Shifts.push_back(P); });
		Ctrl.Update(0.0);
		Ctrl.Update(600.0);
		Ctrl.Update(1200.0);
		Require(Shifts.size() == 2 && Shifts[0] == EDayPhase::Night && Shifts[1] == EDayPhase::Day, "DayNightController shift events");

		// Story
		Require(GetSectorMissions().size() == 6, "6 sector missions");
		Require(GetStoryActs().size() == 6, "6 story acts");
		Require(GetCharacterDossiers().size() == 5, "5 dossiers");
		Require(GetEchoShards().size() == 12, "12 echo shards");
		Require(GetKioskSpots().size() == 8, "8 kiosks");
		Require(FindComms("intro") != nullptr && FindCommsChain("intro") != nullptr, "Intro comms & chain");

		// Online meta
		Require(GetOnlineTitles().size() == 8 && GetOnlineColors().size() == 8, "Online titles & colors");
		Require(SanitizeRoomName("Neo_Kyoto!! 07") == "neokyoto07", "SanitizeRoomName");
		OnlineStats St;
		Require(IsOnlineUnlockEarned(St, GetOnlineTitles()[0]), "Free title unlocked");
		Require(!IsOnlineUnlockEarned(St, GetOnlineTitles()[1]), "Earned title locked at 0");
		St.Contracts = 1;
		Require(IsOnlineUnlockEarned(St, GetOnlineTitles()[1]), "Earned title unlocked at 1");

		// Showcase & audio
		Require(GetShowcaseShots().size() == 6, "6 showcase shots");
		Vec3 Pos, Target;
		EvaluateShot(GetShowcaseShots()[0], 0.5, Pos, Target);
		Require(std::isfinite(Pos.X) && std::isfinite(Target.Z), "Finite showcase evaluation");
		const auto Syls = SyllablesOf("Audio check, Courier Zero-Seven.");
		Require(Syls.size() >= 6, "Syllables parsed");
		Require(Near(MidiToHz(69.0), 440.0), "A4 = 440Hz");
		Require(GetAutumnChapterScales().size() == 6 && AuroraBells().size() == 8, "Music scales");
	}
}

int main(int argc, char** argv)
{
	try
	{
		const std::string Filter = argc > 1 ? argv[1] : "all";
		if (Filter == "all" || Filter == "coord")    { TestCoordBridge();           std::cout << "TestCoordBridge: OK\n"; }
		if (Filter == "all" || Filter == "grid")     { TestNoiseAndGrid();          std::cout << "TestNoiseAndGrid: OK\n"; }
		if (Filter == "all" || Filter == "terrain")  { TestTerrains();              std::cout << "TestTerrains: OK\n"; }
		if (Filter == "all" || Filter == "levels")   { TestChaptersAndLevels();     std::cout << "TestChaptersAndLevels: OK\n"; }
		if (Filter == "all" || Filter == "combat")   { TestConfigTacticsCombat();   std::cout << "TestConfigTacticsCombat: OK\n"; }
		if (Filter == "all" || Filter == "duels")    { TestDuels();                 std::cout << "TestDuels: OK\n"; }
		if (Filter == "all" || Filter == "vehicles") { TestVehiclePhysics();        std::cout << "TestVehiclePhysics: OK\n"; }
		if (Filter == "all" || Filter == "story")    { TestDayNightStoryOnlineAudio(); std::cout << "TestDayNightStoryOnlineAudio: OK\n"; }
		return 0;
	}
	catch (const std::exception& Ex)
	{
		std::cerr << Ex.what() << "\n";
		return 1;
	}
}
