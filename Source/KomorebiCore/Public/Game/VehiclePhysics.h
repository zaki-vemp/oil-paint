// Copyright Komorebi Project. Arcade tyre/chassis physics, suspension, rollover, and vehicle-to-vehicle collisions.
#pragma once

#include "KomorebiCoreAPI.h"
#include "Game/LevelsData.h"

#include <functional>
#include <string_view>
#include <vector>

namespace KomorebiCore
{
	inline constexpr double kVehicleGravity = 20.0;
	inline constexpr double kBailSpeed = 9.0; // m/s: faster than this the rider stays in the seat

	struct VehicleSpec
	{
		std::string_view Name;
		double Len = 4.2;
		double Wid = 1.9;
		double Base = 2.6;
		double Wheel = 0.36;
		double Travel = 0.17;
		double Top = 25.0;
		double Rev = 8.0;
		double Accel = 12.0;
		double Brake = 24.0;
		double Grip = 7.4;
		double Drift = 1.3;
		double Slip = 6.0;
		double Steer = 0.58;
		double Calm = 9.0;
		bool Lean = false;
		double Heft = 0.65;
		double Mass = 1200.0;
		double Eye = 10.5;
		double Hull = 140.0;
		bool ThrowRider = false;
	};

	struct VehicleWheel
	{
		bool Front = false;
		double LocalX = 0.0;
		double LocalZ = 0.0;
		double Ground = 0.0;
	};

	struct VehicleState
	{
		EVehicleKind Kind = EVehicleKind::Car;
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
		double Yaw = 0.0;
		double Vx = 0.0;
		double Vy = 0.0;
		double Vz = 0.0;
		double Vf = 0.0;
		double Steer = 0.0;
		double YawRate = 0.0;
		bool Sliding = false;
		double Tilt = 0.0;
		double RollRate = 0.0;
		double SpinRate = 0.0;
		bool Tipped = false;
		double Floor = 0.0;
		double FloorPitch = 0.0;
		double FloorRoll = 0.0;
		double Pitch = 0.0;
		double Roll = 0.0;
		double Air = 0.0;
		double Squat = 0.0;
		double Liable = 0.0;
		std::vector<VehicleWheel> Wheels;
	};

	struct VehicleDriveInput
	{
		double Throttle = 0.0;
		double Steer = 0.0;
		bool Handbrake = false;
	};

	/** Returns the handling, geometry, mass, and hull spec for one of the five vehicle kinds. */
	KOMOREBICORE_API const VehicleSpec& GetVehicleSpec(EVehicleKind Kind);

	/** Builds default wheel patch offsets (4 for cars/vans, 2 for bikes/scooters) from a VehicleSpec. */
	KOMOREBICORE_API std::vector<VehicleWheel> MakeDefaultWheels(const VehicleSpec& Spec);

	/** Steps handling, steering lock, lateral grip/drift, and velocity projection in place. */
	KOMOREBICORE_API void DriveVehicle(
		VehicleState& V,
		const VehicleSpec& Spec,
		const VehicleDriveInput& Input,
		double Dt,
		bool Grounded = true,
		double GripMul = 1.0);

	/** Steps chassis suspension rocking and rollover energy transfer; sets V.Tipped when overturned. */
	KOMOREBICORE_API void RockVehicle(VehicleState& V, double Dt, bool Stabilized = false);

	/** Oriented-box SAT collision and impulse/spin/roll exchange between two chassis; returns closing speed. */
	KOMOREBICORE_API double CollideVehicles(
		VehicleState& A,
		const VehicleSpec& SpecA,
		VehicleState& B,
		const VehicleSpec& SpecB);

	/** Convenience overload looking up VehicleSpec from A.Kind and B.Kind. */
	KOMOREBICORE_API double CollideVehicles(VehicleState& A, VehicleState& B);

	/** Samples heightfield at each wheel contact patch to compute rest floor height, pitch, and roll. */
	KOMOREBICORE_API void WheelTerrain(
		VehicleState& V,
		const VehicleSpec& Spec,
		const std::function<double(double, double)>& HeightAt);

	/** Immediately settles the sprung chassis onto the terrain rest plane with zero vertical speed. */
	KOMOREBICORE_API void SettleSuspension(
		VehicleState& V,
		const VehicleSpec& Spec,
		const std::function<double(double, double)>& HeightAt);

	/** Steps spring-damper suspension and ballistic free flight; returns touchdown impact speed (>0 on landing). */
	KOMOREBICORE_API double SuspendVehicle(
		VehicleState& V,
		const VehicleSpec& Spec,
		const std::function<double(double, double)>& HeightAt,
		double Dt);
}
