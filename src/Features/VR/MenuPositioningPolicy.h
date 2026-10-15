#pragma once

#include <algorithm>
#include <cmath>

namespace VRMenuPositioningPolicy
{
	inline constexpr float MinDistance = 1.5f;
	inline constexpr float MaxDistance = 3.0f;
	inline constexpr float DefaultDistance = 2.4f;
	// Keep the angular size of saved scales independent of the default placement.
	inline constexpr float ScaleReferenceDistance = 2.25f;

	/** Keep menu depth usable for saved settings, live edits and controller drag. */
	inline float SanitizeDistance(float a_metres)
	{
		return std::isfinite(a_metres) ? std::clamp(a_metres, MinDistance, MaxDistance) : DefaultDistance;
	}

	/** Native overlay dimensions contain scale twice; preserve width / distance. */
	inline float ScaleForDistance(float a_scale, float a_distance, float a_referenceDistance)
	{
		return a_scale * std::sqrt(a_distance / a_referenceDistance);
	}

	constexpr bool UseFixedWorld(int a_positioningMethod)
	{
		return a_positioningMethod == 1;
	}

	constexpr bool ShouldLockDesktopCanvas(bool a_isVR, bool a_layoutUnlocked)
	{
		return a_isVR && !a_layoutUnlocked;
	}

	constexpr bool ShouldAllowOverlayDrag(bool a_layoutUnlocked, bool a_dragEnabled)
	{
		return a_layoutUnlocked && a_dragEnabled;
	}

}
