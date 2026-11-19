// Copyright Komorebi Project. Arcade tyre/chassis physics, suspension, rollover, and vehicle-to-vehicle collisions.
#include "Game/VehiclePhysics.h"

#include <array>
#include <limits>

namespace KomorebiCore
{
	const VehicleSpec& GetVehicleSpec(EVehicleKind Kind)
	{
		static constexpr VehicleSpec kCar{
			"Kestrel", 4.2, 1.9, 2.6, 0.36, 0.17, 25.0, 8.0, 12.0, 24.0, 7.4, 1.3, 6.0, 0.58, 9.0, false, 0.65, 1200.0, 10.5, 140.0, false,
		};
		static constexpr VehicleSpec kSports{
			"Kestrel GT", 4.3, 1.85, 2.45, 0.34, 0.12, 34.0, 9.0, 18.0, 30.0, 8.6, 0.95, 7.2, 0.72, 11.0, false, 0.58, 1000.0, 11.5, 90.0, false,
		};
		static constexpr VehicleSpec kPickup{
			"Parcel van", 5.3, 2.05, 3.25, 0.44, 0.24, 18.0, 6.0, 8.0, 16.0, 6.4, 1.5, 5.5, 0.46, 7.0, false, 0.84, 2400.0, 12.2, 240.0, false,
		};
		static constexpr VehicleSpec kBike{
			"Razor bike", 2.2, 0.7, 1.45, 0.36, 0.19, 31.0, 4.0, 17.0, 22.0, 9.0, 1.7, 7.5, 0.5, 8.0, true, 0.45, 240.0, 8.5, 70.0, true,
		};
		static constexpr VehicleSpec kBicycle{
			"Courier scooter", 1.7, 0.5, 1.05, 0.32, 0.14, 12.0, 3.0, 5.0, 10.0, 8.0, 2.2, 4.0, 0.7, 5.0, true, 0.28, 80.0, 7.6, 28.0, true,
		};

		switch (Kind)
		{
			case EVehicleKind::Car: return kCar;
			case EVehicleKind::Sports: return kSports;
			case EVehicleKind::Pickup: return kPickup;
			case EVehicleKind::Bike: return kBike;
			case EVehicleKind::Bicycle: return kBicycle;
		}
		return kCar;
	}

	std::vector<VehicleWheel> MakeDefaultWheels(const VehicleSpec& Spec)
	{
		std::vector<VehicleWheel> Wheels;
		const double HalfBase = Spec.Base * 0.5;
		if (Spec.Lean)
		{
			Wheels.reserve(2);
			Wheels.push_back({true, 0.0, HalfBase, 0.0});
			Wheels.push_back({false, 0.0, -HalfBase, 0.0});
		}
		else
		{
			const double HalfTrack = Spec.Wid * 0.5 - 0.12;
			Wheels.reserve(4);
			Wheels.push_back({true, HalfTrack, HalfBase, 0.0});
			Wheels.push_back({true, -HalfTrack, HalfBase, 0.0});
			Wheels.push_back({false, HalfTrack, -HalfBase, 0.0});
			Wheels.push_back({false, -HalfTrack, -HalfBase, 0.0});
		}
		return Wheels;
	}

	void DriveVehicle(
		VehicleState& V,
		const VehicleSpec& Spec,
		const VehicleDriveInput& Input,
		double Dt,
		bool Grounded,
		double GripMul)
	{
		const double Fx = std::sin(V.Yaw);
		const double Fz = std::cos(V.Yaw);
		double Vf = V.Vx * Fx + V.Vz * Fz;
		double Vs = V.Vx * Fz - V.Vz * Fx;
		V.YawRate = 0.0;
		V.Sliding = false;

		if (Grounded)
		{
			if (Input.Throttle * Vf < 0.0 && std::abs(Vf) > 0.5)
			{
				Vf = Toward(Vf, Spec.Brake * std::abs(Input.Throttle) * Dt);
			}
			else if (Input.Throttle > 0.0)
			{
				const double Ratio = Vf / Spec.Top;
				Vf += Spec.Accel * Input.Throttle * std::max(0.0, 1.0 - Ratio * Ratio) * Dt;
			}
			else if (Input.Throttle < 0.0)
			{
				const double Ratio = Vf / Spec.Rev;
				Vf += Spec.Accel * 0.6 * Input.Throttle * std::max(0.0, 1.0 - Ratio * Ratio) * Dt;
			}

			// Air drag takes a share of speed; rolling resistance and idle engine take a flat deduction.
			Vf = Toward(Vf - Vf * 0.1 * Dt, (Input.Throttle == 0.0 ? 1.8 : 0.3) * Dt);
			if (Input.Handbrake)
			{
				Vf = Toward(Vf, Spec.Brake * 0.2 * Dt);
			}

			const double Lock = Spec.Steer / (1.0 + std::abs(Vf) / Spec.Calm);
			V.Steer += (Input.Steer * Lock - V.Steer) * std::min(1.0, 9.0 * Dt);
			V.YawRate = -(Vf / Spec.Base) * std::tan(V.Steer) * (Input.Handbrake ? 1.35 : 1.0);
			V.Yaw += V.YawRate * Dt;

			const double Grip = Spec.Grip * GripMul;
			const double Slip = Spec.Slip * (0.35 + 0.65 * GripMul);
			const double Before = std::hypot(Vf, Vs);
			const double Slide = Input.Handbrake ? 1.0 : SmoothStep(Slip * 0.85, Slip * 1.15, std::abs(Vs));
			Vs *= std::exp(-Lerp(Grip, Spec.Drift * Lerp(0.5, 1.0, GripMul), Slide) * Dt);

			// Gripping tyres redirect velocity into the turn; sliding tyres stop returning that energy.
			const double After = std::hypot(Vf, Vs);
			if (Slide < 1.0 && After > 1e-3)
			{
				const double K = 1.0 + (Before / After - 1.0) * 0.75 * (1.0 - Slide);
				Vf *= K;
				Vs *= K;
			}
			V.Sliding = Slide > 0.5 && std::abs(Vs) > 2.0;
		}

		V.Vx = Fx * Vf + Fz * Vs;
		V.Vz = Fz * Vf - Fx * Vs;
		V.Vf = Vf;
	}

	void RockVehicle(VehicleState& V, double Dt, bool Stabilized)
	{
		const double Drag = std::cos(V.Tilt) < 0.0 ? -10.5 : -2.3;
		V.RollRate = (V.RollRate - 18.0 * std::sin(V.Tilt) * std::cos(V.Tilt) * Dt) * std::exp(Drag * Dt);
		if (Stabilized && !V.Tipped && std::abs(V.RollRate) < 5.0)
		{
			V.Tilt *= std::exp(-5.0 * Dt);
			V.RollRate *= std::exp(-3.0 * Dt);
		}
		V.Tilt = WrapAngle(V.Tilt + V.RollRate * Dt);
		V.Tipped = std::cos(V.Tilt) < 0.5;
	}

	double CollideVehicles(
		VehicleState& A,
		const VehicleSpec& SpecA,
		VehicleState& B,
		const VehicleSpec& SpecB)
	{
		if (std::abs(A.Y - B.Y) > 1.5)
		{
			return 0.0;
		}

		const double Dx = B.X - A.X;
		const double Dz = B.Z - A.Z;
		if (std::hypot(Dx, Dz) > (std::hypot(SpecA.Len, SpecA.Wid) + std::hypot(SpecB.Len, SpecB.Wid)) * 0.5)
		{
			return 0.0;
		}

		const std::array<std::array<double, 2>, 2> AxesA = {{
			{std::cos(A.Yaw), -std::sin(A.Yaw)},
			{std::sin(A.Yaw), std::cos(A.Yaw)},
		}};
		const std::array<std::array<double, 2>, 2> AxesB = {{
			{std::cos(B.Yaw), -std::sin(B.Yaw)},
			{std::sin(B.Yaw), std::cos(B.Yaw)},
		}};

		auto ProjectedRadius = [](
			const VehicleSpec& Spec,
			const std::array<std::array<double, 2>, 2>& Axes,
			double X,
			double Z) {
			return std::abs(Axes[0][0] * X + Axes[0][1] * Z) * Spec.Wid * 0.5
				+ std::abs(Axes[1][0] * X + Axes[1][1] * Z) * Spec.Len * 0.5;
		};

		const std::array<std::array<double, 2>, 4> AllAxes = {AxesA[0], AxesA[1], AxesB[0], AxesB[1]};
		double Depth = std::numeric_limits<double>::infinity();
		double Nx = 0.0;
		double Nz = 0.0;

		for (const auto& Axis : AllAxes)
		{
			const double Ax = Axis[0];
			const double Az = Axis[1];
			const double Dist = Dx * Ax + Dz * Az;
			const double Overlap = ProjectedRadius(SpecA, AxesA, Ax, Az)
				+ ProjectedRadius(SpecB, AxesB, Ax, Az)
				- std::abs(Dist);
			if (Overlap <= 0.0)
			{
				return 0.0;
			}
			if (Overlap < Depth)
			{
				Depth = Overlap;
				const double Sign = Dist < 0.0 ? -1.0 : 1.0;
				Nx = Ax * Sign;
				Nz = Az * Sign;
			}
		}

		const double InvA = 1.0 / SpecA.Mass;
		const double InvB = 1.0 / SpecB.Mass;
		const double Total = InvA + InvB;

		A.X -= Nx * (Depth + 0.002) * InvA / Total;
		A.Z -= Nz * (Depth + 0.002) * InvA / Total;
		B.X += Nx * (Depth + 0.002) * InvB / Total;
		B.Z += Nz * (Depth + 0.002) * InvB / Total;

		const double Closing = (A.Vx - B.Vx) * Nx + (A.Vz - B.Vz) * Nz;
		if (Closing <= 0.0)
		{
			return 0.0;
		}

		const double Impulse = Closing * 1.15 / Total;
		A.Vx -= Nx * Impulse * InvA;
		A.Vz -= Nz * Impulse * InvA;
		B.Vx += Nx * Impulse * InvB;
		B.Vz += Nz * Impulse * InvB;

		auto ApplyAngularImpulse = [&](VehicleState& V, const VehicleSpec& Spec, double Sign, double Inv) {
			const double Side = (Nx * std::cos(V.Yaw) - Nz * std::sin(V.Yaw)) * Sign;
			V.RollRate -= Side * Impulse * Inv * (Spec.Lean ? 0.9 : 0.55);
			const double Lever = Clamp((Dx * Nz - Dz * Nx) / Spec.Len, -1.0, 1.0);
			V.SpinRate += Clamp(Lever * Impulse * Inv / Spec.Base, -2.0, 2.0);
		};

		ApplyAngularImpulse(A, SpecA, -1.0, InvA);
		ApplyAngularImpulse(B, SpecB, 1.0, InvB);

		if (A.Liable > 0.0 || B.Liable > 0.0)
		{
			A.Liable = 5.0;
			B.Liable = 5.0;
		}

		return Closing;
	}

	double CollideVehicles(VehicleState& A, VehicleState& B)
	{
		return CollideVehicles(A, GetVehicleSpec(A.Kind), B, GetVehicleSpec(B.Kind));
	}

	void WheelTerrain(
		VehicleState& V,
		const VehicleSpec& Spec,
		const std::function<double(double, double)>& HeightAt)
	{
		if (V.Wheels.empty() || !HeightAt)
		{
			return;
		}

		const double Fx = std::sin(V.Yaw);
		const double Fz = std::cos(V.Yaw);
		const std::size_t Count = V.Wheels.size();
		const double CountF = static_cast<double>(Count);
		double Front = 0.0;
		double Rear = 0.0;
		double Left = 0.0;
		double Right = 0.0;

		for (VehicleWheel& W : V.Wheels)
		{
			W.Ground = HeightAt(V.X + Fx * W.LocalZ + Fz * W.LocalX, V.Z + Fz * W.LocalZ - Fx * W.LocalX);
			if (W.Front)
			{
				Front += W.Ground;
			}
			else
			{
				Rear += W.Ground;
			}
			if (W.LocalX > 0.0)
			{
				Left += W.Ground;
			}
			else
			{
				Right += W.Ground;
			}
		}

		V.Floor = (Front + Rear) / CountF;
		V.FloorPitch = -std::atan2(2.0 * (Front - Rear) / CountF, Spec.Base);
		V.FloorRoll = (Count == 4) ? std::atan2((Left - Right) * 0.5, Spec.Wid - 0.24) : 0.0;
	}

	void SettleSuspension(
		VehicleState& V,
		const VehicleSpec& Spec,
		const std::function<double(double, double)>& HeightAt)
	{
		WheelTerrain(V, Spec, HeightAt);
		V.Y = V.Floor;
		V.Pitch = V.FloorPitch;
		V.Roll = V.FloorRoll;
		V.Vy = 0.0;
		V.Air = 0.0;
		V.Squat = 0.0;
	}

	double SuspendVehicle(
		VehicleState& V,
		const VehicleSpec& Spec,
		const std::function<double(double, double)>& HeightAt,
		double Dt)
	{
		WheelTerrain(V, Spec, HeightAt);
		const double Travel = Spec.Travel;
		const double Rate = std::sqrt(kVehicleGravity / Travel);
		const double Flight = V.Y + V.Vy * Dt - kVehicleGravity * Dt * Dt * 0.5;
		double Landing = 0.0;

		if (Flight > V.Floor + Travel)
		{
			V.Y = Flight;
			V.Vy -= kVehicleGravity * Dt;
			V.Air += Dt;
			V.Pitch = Lerp(V.Pitch, 0.12, 1.0 - std::exp(-1.2 * Dt));
		}
		else
		{
			if (V.Air > 0.2)
			{
				Landing = std::max(0.0, -V.Vy);
			}
			V.Air = 0.0;
			// Gravity is balanced at static ride height; the spring pushes up and never pulls onto the road.
			const double Acceleration = std::max(-kVehicleGravity, (V.Floor - V.Y) * Rate * Rate - V.Vy * 1.5 * Rate);
			V.Vy += Acceleration * Dt;
			V.Y += V.Vy * Dt;
			if (V.Y < V.Floor - Travel)
			{
				V.Y = V.Floor - Travel;
				V.Vy = std::max(0.0, V.Vy);
			}
			const double Ease = 1.0 - std::exp(-Rate * Dt);
			V.Pitch = Lerp(V.Pitch, V.FloorPitch, Ease);
			V.Roll = Lerp(V.Roll, V.FloorRoll, Ease);
		}

		return Landing;
	}
}
