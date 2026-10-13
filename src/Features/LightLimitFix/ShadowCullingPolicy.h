#pragma once

// Sun-angle and point-light magnification policy adapted from SPS, Copyright (c) 2026 dnai92.
// Modified for CSX on 2026-10-14; GPL-3.0-or-later with the packaged SPS exceptions.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace LightLimitFixDetail::ShadowCulling
{
	struct Settings
	{
		bool sun = true, torch = true, actors = true, interiorTorches = true;
		float minimumDistanceMeters = 50, actorDistanceMeters = 75;
		bool operator==(const Settings&) const = default;
		[[nodiscard]] bool Active() const noexcept { return sun || torch || actors; }
	};
	inline void Validate(const Settings& value)
	{
		if (!std::isfinite(value.minimumDistanceMeters) || value.minimumDistanceMeters < 30 || value.minimumDistanceMeters > 300 ||
			!std::isfinite(value.actorDistanceMeters) || value.actorDistanceMeters < 50 || value.actorDistanceMeters > 300)
			throw std::invalid_argument("Shadow-culling distances must be 30..300 m for objects and 50..300 m for NPCs");
	}
	inline Settings Parse(const nlohmann::json& value, Settings result = {})
	{
		if (!value.is_object())
			throw std::invalid_argument("Shadow culling requires an object");
		for (const auto& [name, field] : value.items()) {
			bool* flag = name == "sun" ? &result.sun : name == "torch"       ? &result.torch :
			                                       name == "actors"          ? &result.actors :
			                                       name == "interiorTorches" ? &result.interiorTorches :
			                                                                   nullptr;
			if (flag) {
				if (!field.is_boolean())
					throw std::invalid_argument(name + " must be boolean");
				*flag = field.get<bool>();
			} else if (name == "minimumDistanceMeters" || name == "actorDistanceMeters") {
				if (!field.is_number())
					throw std::invalid_argument(name + " must be numeric");
				const auto number = field.get<double>();
				if (!std::isfinite(number) || number < (name == "minimumDistanceMeters" ? 30 : 50) || number > 300)
					throw std::invalid_argument(name + " is outside its distance range");
				(name == "minimumDistanceMeters" ? result.minimumDistanceMeters : result.actorDistanceMeters) = static_cast<float>(number);
			} else {
				throw std::invalid_argument("Unknown shadow-culling setting: " + name);
			}
		}
		Validate(result);
		return result;
	}
	inline void to_json(nlohmann::json& value, const Settings& settings)
	{
		value = { { "sun", settings.sun }, { "torch", settings.torch }, { "actors", settings.actors }, { "interiorTorches", settings.interiorTorches },
			{ "minimumDistanceMeters", settings.minimumDistanceMeters }, { "actorDistanceMeters", settings.actorDistanceMeters } };
	}
	inline void from_json(const nlohmann::json& value, Settings& settings) { settings = Parse(value); }

	enum class Pass : std::uint8_t
	{
		Unknown,
		Sun,
		Point
	};
	struct Demand
	{
		Pass pass = Pass::Unknown;
		std::array<double, 2> eyeDistance{};
		unsigned eyeCount = 0;
		double radius = 0, sunElevationSine = 0, lightDistance = 0, lightRadius = 0;
		bool actor = false, dynamic = true, interior = false, protectedGeometry = false;
	};
	/** Keep invalid bounds and nearby actor roots conservative before applying shadow distance policy. */
	inline double SurfaceDistance(const std::array<double, 3>& center, double radius,
		const std::array<double, 3>& position, const std::array<double, 3>* actorRoot = nullptr) noexcept
	{
		const auto finite = [](const auto& point) { return std::all_of(point.begin(), point.end(), [](double v) { return std::isfinite(v); }); };
		if (!finite(center) || !finite(position) || !std::isfinite(radius) || radius <= 0 || (actorRoot && !finite(*actorRoot)))
			return std::numeric_limits<double>::quiet_NaN();
		const auto distance = [&](const auto& point) {
			return std::hypot(point[0] - position[0], point[1] - position[1], point[2] - position[2]) - radius;
		};
		const auto result = distance(center);
		return actorRoot ? std::min(result, distance(*actorRoot)) : result;
	}
	/** Admit only tiny distant shadow casters; unknown geometry, projections and close eyes retain their shadows. */
	inline bool ShouldCull(const Settings& settings, const Demand& demand) noexcept
	{
		if (!settings.Active() || demand.pass == Pass::Unknown || !demand.eyeCount || demand.eyeCount > demand.eyeDistance.size() ||
			!std::isfinite(demand.radius) || demand.radius <= 0 || demand.protectedGeometry)
			return false;
		if (demand.pass == Pass::Sun && (!std::isfinite(demand.sunElevationSine) || demand.sunElevationSine < 0.17364817766693033))
			return false;
		if (demand.pass == Pass::Point && demand.interior && !settings.interiorTorches)
			return false;
		if (demand.actor) {
			if (!settings.actors)
				return false;
		} else if (demand.dynamic || (demand.pass == Pass::Sun ? !settings.sun : !settings.torch)) {
			return false;
		}
		double shadowRadius = demand.radius;
		if (demand.pass == Pass::Point) {
			if (!std::isfinite(demand.lightDistance) || !std::isfinite(demand.lightRadius) || demand.lightDistance <= 1 ||
				demand.lightRadius <= 0 || demand.radius / demand.lightDistance >= 0.1)
				return false;
			shadowRadius *= std::max(1.0, demand.lightRadius / demand.lightDistance);
		} else {
			shadowRadius /= demand.sunElevationSine;
		}
		if (!std::isfinite(shadowRadius) || shadowRadius >= (demand.actor ? 256 : 64))
			return false;
		const auto minimum = (demand.actor ? settings.actorDistanceMeters : settings.minimumDistanceMeters) / 0.01428;
		for (unsigned eye = 0; eye < demand.eyeCount; ++eye) {
			const auto distance = demand.eyeDistance[eye];
			if (!std::isfinite(distance) || distance <= minimum || shadowRadius / distance >= (demand.actor ? 0.02 : 0.005))
				return false;
		}
		return true;
	}
}
