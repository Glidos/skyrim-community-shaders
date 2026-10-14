#pragma once

#include "ShadowCullingPolicy.h"

namespace LightLimitFixDetail::ShadowCulling
{
	/** Install shadow-only native hooks after all plugins load, unless SPS owns them. */
	void Install();
	/** Publish immutable stereo demand and retained native shadow-camera identities. */
	void Update(const Settings& settings, bool featureEnabled);
	/** Draw shared native shadow-culling controls, disabled when unavailable. */
	void DrawSettings(Settings& settings, bool advanced);
	/** Report requested versus available controls; diagnostics compile out of production. */
	nlohmann::json Status(const Settings& settings);
}
