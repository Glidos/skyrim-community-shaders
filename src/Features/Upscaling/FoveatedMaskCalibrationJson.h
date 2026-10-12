#pragma once

#include "FoveatedMaskCalibration.h"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace FoveatedMaskCalibration
{
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Geometry, scale, horizontalScale, centers)
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SetupConfirmation, finished, geometry, feather)

	inline void to_json(nlohmann::json& json, const Reference& value)
	{
		nlohmann::json outlines = nlohmann::json::array();
		for (const auto& outline : value.headsetOutlines) {
			auto points = nlohmann::json::array();
			for (auto point : outline)
				points.push_back({ point.x, point.y });
			outlines.push_back(std::move(points));
		}
		json = { { "version", value.version }, { "outer", value.outer }, { "leftToRight", value.leftToRight },
			{ "innerLimits", value.innerLimits }, { "headsetOutlines", std::move(outlines) }, { "peripheryTaa", value.peripheryTaa }, { "fullImage", value.fullImage },
			{ "centerScale", value.centerScale }, { "feather", value.feather }, { "coveragePercent", value.coveragePercent } };
	}

	/** Reject incomplete or corrupt references before they can alter live geometry. */
	inline void from_json(const nlohmann::json& json, Reference& value)
	{
		Reference parsed;
		const auto& version = json.at("version");
		if (!version.is_number_integer() || (version != 0 && version != 1 && version != 2 && version != 3 && version != 4 && version != kReferenceVersion))
			throw std::invalid_argument("Unsupported FOV calibration version");
		// Legacy references use scene-camera projection and do not prove nasal coverage.
		parsed.version = version == 2 || version == 3 || version == 4 || version == kReferenceVersion ? kReferenceVersion : 0;
		if (parsed.version != 0) {
			const auto& centers = json.at("outer").at("centers");
			const auto& projection = json.at("leftToRight");
			const auto& innerLimits = json.at("innerLimits");
			if (!centers.is_array() || centers.size() != 4 || !projection.is_array() || projection.size() != 9 || !innerLimits.is_array() || innerLimits.size() != 2)
				throw std::invalid_argument("Invalid FOV calibration dimensions");
			const auto requireNumber = [](const nlohmann::json& number) {
				if (!number.is_number())
					throw std::invalid_argument("FOV calibration geometry requires numeric values");
			};
			for (const auto* number : { &json.at("outer").at("scale"), &json.at("outer").at("horizontalScale"),
					 &json.at("centerScale"), &json.at("feather") })
				requireNumber(*number);
			for (const auto& number : centers)
				requireNumber(number);
			for (const auto& number : projection)
				requireNumber(number);
			for (const auto& number : innerLimits)
				requireNumber(number);
			if (version == 4 || version == kReferenceVersion) {
				requireNumber(json.at("coveragePercent"));
				json.at("coveragePercent").get_to(parsed.coveragePercent);
			}
			if (version == 3 || version == 4 || version == kReferenceVersion) {
				const auto& outlines = json.at("headsetOutlines");
				if (!outlines.is_array() || outlines.size() != 2)
					throw std::invalid_argument("Invalid headset outline dimensions");
				for (unsigned eye = 0; eye < 2; ++eye) {
					const auto& outline = outlines[eye];
					if (!outline.is_array() || outline.size() > kMaximumHeadsetOutlineVertices)
						throw std::invalid_argument("Invalid headset outline vertex count");
					for (const auto& point : outline) {
						if (!point.is_array() || point.size() != 2)
							throw std::invalid_argument("Invalid headset outline vertex");
						requireNumber(point[0]);
						requireNumber(point[1]);
						parsed.headsetOutlines[eye].push_back({ point[0].get<double>(), point[1].get<double>() });
					}
				}
			}
			json.at("outer").get_to(parsed.outer);
			json.at("leftToRight").get_to(parsed.leftToRight);
			innerLimits.get_to(parsed.innerLimits);
			json.at("peripheryTaa").get_to(parsed.peripheryTaa);
			json.at("fullImage").get_to(parsed.fullImage);
			json.at("centerScale").get_to(parsed.centerScale);
			json.at("feather").get_to(parsed.feather);
			if (!IsValid(parsed))
				throw std::invalid_argument("Invalid FOV calibration reference");
			// Old headset fits distorted the contour; retain slider geometry but require a fresh reference.
			if (version != kReferenceVersion && UsesHeadsetOutline(parsed))
				parsed = {};
		}
		value = parsed;
	}
}
