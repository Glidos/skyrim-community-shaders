#ifndef __LLF_CONTACT_SHADOW_RAY_HLSL__
#define __LLF_CONTACT_SHADOW_RAY_HLSL__

#include "Common/VR.hlsli"

namespace LightLimitFix
{
	/** Returns the active integer depth extent, with at least one texel per eye. */
	uint2 GetContactShadowRenderDimensions(float2 bufferDimensions, float2 resolutionScale)
	{
#if defined(VR)
		const uint2 minimumDimensions = uint2(2, 1);
#else
		const uint2 minimumDimensions = uint2(1, 1);
#endif
		return max(uint2(bufferDimensions * saturate(resolutionScale)), minimumDimensions);
	}

	/** Restricts point depth loads to active texels belonging to the explicit eye. */
	int2 ClampContactShadowDepthPixel(int2 pixel, uint eyeIndex, uint2 renderDimensions)
	{
#if defined(VR)
		return Stereo::ClampToEyeBounds(pixel, eyeIndex, float2(renderDimensions));
#else
		return clamp(pixel, int2(0, 0), int2(renderDimensions) - 1);
#endif
	}

	/** Evaluates view depth at a screen-linear ray position from positive endpoint inverse depths. */
	float GetContactShadowRayDepth(float2 inverseDepth, float t)
	{
		return rcp(lerp(inverseDepth.x, inverseDepth.y, t));
	}
}

#endif
