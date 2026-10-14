#include "Features/VR/MenuPositioningPolicy.h"
#include "Utils/UISmoothing.h"

#include <cmath>
#include <initializer_list>
#include <limits>

namespace
{
	using VRMenuPositioningPolicy::ShouldAllowOverlayDrag;
	using VRMenuPositioningPolicy::ShouldLockDesktopCanvas;
	using VRMenuPositioningPolicy::UseFixedWorld;

	constexpr bool CoversMenuLayoutSelection()
	{
		return UseFixedWorld(1) &&
		       !UseFixedWorld(0) &&
		       ShouldLockDesktopCanvas(true, false) &&
		       !ShouldLockDesktopCanvas(true, true) &&
		       !ShouldLockDesktopCanvas(false, false) &&
		       ShouldAllowOverlayDrag(true, true) &&
		       !ShouldAllowOverlayDrag(false, true) &&
		       !ShouldAllowOverlayDrag(true, false);
	}

	static_assert(CoversMenuLayoutSelection());
}

int main()
{
	if (!CoversMenuLayoutSelection())
		return 1;
	using namespace VRMenuPositioningPolicy;
	if (SanitizeDistance(-100) != MinDistance || SanitizeDistance(100) != MaxDistance ||
		SanitizeDistance(std::numeric_limits<float>::quiet_NaN()) != DefaultDistance ||
		SanitizeDistance(std::numeric_limits<float>::infinity()) != DefaultDistance ||
		SanitizeDistance(2.5f) != 2.5f || MaxDistance != 3.0f)
		return 1;
	for (const float scale : { 1.5f, 1.75f, 2.0f }) {
		const float referenceWidthPerMetre = scale * scale / 2.25f;
		for (const float distance : { 1.5f, 2.25f, 2.75f, 3.0f }) {
			const float actual = VRMenuPositioningPolicy::ScaleForDistance(scale, distance, 2.25f);
			if (std::abs(actual * actual / distance - referenceWidthPerMetre) > 1e-6f)
				return 1;
		}
	}
	for (const int rate : { 60, 90, 144 }) {
		float position = 0;
		const float amount = Util::UISmoothing::Amount(1.0f / rate);
		for (int frame = 0; frame < rate; ++frame) {
			const float next = std::lerp(position, 1.0f, amount);
			if (next < position || next > 1)
				return 1;
			position = next;
		}
		if (position < .999f)
			return 1;
	}
	const float once = Util::UISmoothing::Amount(1.0f / 60);
	const float twice = Util::UISmoothing::Amount(1.0f / 120);
	if (std::abs(once - (1 - (1 - twice) * (1 - twice))) > 1e-6f)
		return 1;
	return Util::UISmoothing::Amount(0) == 0 &&
	               Util::UISmoothing::Amount(-1) == 0 &&
	               Util::UISmoothing::Amount(std::numeric_limits<float>::quiet_NaN()) == 0 &&
	               Util::UISmoothing::Amount(10) == 1 ?
	           0 :
	           1;
}
