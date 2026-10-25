#pragma once

#include "Utils/Finite.h"

#include <cmath>

/** Atmospheric clarity and retained legacy preset validation. */
namespace AdaptiveBalanceGodray
{
	inline constexpr float kMin = 0.0f;
	inline constexpr float kMax = 5.0f;
	inline constexpr float kDefault = 1.0f;
	inline constexpr float kClarityMin = 0.0f;
	inline constexpr float kClarityMax = 1.0f;
	inline constexpr float kClarityDefault = 0.0f;

	/** Validate external writes without silently replacing an unsupported value. */
	[[nodiscard]] inline bool IsValidFinalBrightness(double a_value) noexcept
	{
		return std::isfinite(a_value) && a_value >= kMin && a_value <= kMax;
	}

	/** Keep the inert legacy preset/API value finite and compatible with older exports. */
	[[nodiscard]] inline float SanitizeFinalBrightness(float a_value) noexcept
	{
		return Util::ClampFinite(a_value, kMin, kMax, kDefault);
	}

	/** Bound smooth volumetric haze suppression; invalid values preserve the original image. */
	[[nodiscard]] inline float SanitizeClarity(float a_value) noexcept
	{
		return Util::ClampFinite(a_value, kClarityMin, kClarityMax, kClarityDefault);
	}
}
