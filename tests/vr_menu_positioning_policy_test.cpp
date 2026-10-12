#include "Features/VR/MenuPositioningPolicy.h"
#include "Utils/UISmoothing.h"

#include <cmath>
#include <initializer_list>
#include <limits>

namespace
{
	using VRMenuPositioningPolicy::SelectEffectiveValue;
	using VRMenuPositioningPolicy::ShouldAllowOverlayDrag;
	using VRMenuPositioningPolicy::ShouldLockDesktopCanvas;
	using VRMenuPositioningPolicy::ShouldReanchorOnOpen;
	using VRMenuPositioningPolicy::ShouldTrackHMDYaw;
	using VRMenuPositioningPolicy::UseFixedWorld;

	constexpr bool CoversMenuLayoutSelection()
	{
		return UseFixedWorld(1) &&
		       !UseFixedWorld(0) &&
		       SelectEffectiveValue(false, 7, 1) == 1 &&
		       SelectEffectiveValue(true, 7, 1) == 7 &&
		       ShouldLockDesktopCanvas(true, false) &&
		       !ShouldLockDesktopCanvas(true, true) &&
		       !ShouldLockDesktopCanvas(false, false) &&
		       ShouldAllowOverlayDrag(true, true) &&
		       !ShouldAllowOverlayDrag(false, true) &&
		       !ShouldAllowOverlayDrag(true, false) &&
		       ShouldReanchorOnOpen(false) &&
		       !ShouldReanchorOnOpen(true) &&
		       ShouldTrackHMDYaw(false) &&
		       !ShouldTrackHMDYaw(true);
	}

	static_assert(CoversMenuLayoutSelection());
}

int main()
{
	if (!CoversMenuLayoutSelection())
		return 1;
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
