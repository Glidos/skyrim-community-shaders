#define VR
#define COMPUTESHADER
#include "/Shaders/LightLimitFix/ContactShadowRay.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

/// @tags contact-shadows, vr, render-scale
/// A left-eye sample exactly on the packed seam must remain in the left eye.
[numthreads(1, 1, 1)] void TestContactShadowScaledEyeSeam() {
	uint2 dimensions = LightLimitFix::GetContactShadowRenderDimensions(float2(2000, 1000), float2(0.75, 0.75));
	ASSERT(AreEqual, dimensions, uint2(1500, 750));
	ASSERT(AreEqual, LightLimitFix::ClampContactShadowDepthPixel(int2(750, 750), 0, dimensions), int2(749, 749));
	ASSERT(AreEqual, LightLimitFix::ClampContactShadowDepthPixel(int2(749, -1), 1, dimensions), int2(750, 0));
	ASSERT(AreEqual, LightLimitFix::ClampContactShadowDepthPixel(int2(1499, 24), 1, dimensions), int2(1499, 24));
}

	/// @tags contact-shadows, vr, render-scale
	/// Odd active widths divide at the integer eye boundary and never sample unused texels.
	[numthreads(1, 1, 1)] void TestContactShadowOddEyeBounds()
{
	uint2 dimensions = LightLimitFix::GetContactShadowRenderDimensions(float2(201, 101), float2(0.805, 0.605));
	ASSERT(AreEqual, dimensions, uint2(161, 61));
	ASSERT(AreEqual, LightLimitFix::ClampContactShadowDepthPixel(int2(80, 61), 0, dimensions), int2(79, 60));
	ASSERT(AreEqual, LightLimitFix::ClampContactShadowDepthPixel(int2(79, 0), 1, dimensions), int2(80, 0));
	ASSERT(AreEqual, LightLimitFix::ClampContactShadowDepthPixel(int2(161, 0), 1, dimensions), int2(160, 0));
}

/// @tags contact-shadows, vr, render-scale
/// The minimum packed extent still supplies one valid texel to each eye.
[numthreads(1, 1, 1)] void TestContactShadowMinimumEyeBounds() {
	uint2 dimensions = LightLimitFix::GetContactShadowRenderDimensions(float2(200, 100), float2(0, 0));
	ASSERT(AreEqual, dimensions, uint2(2, 1));
	ASSERT(AreEqual, LightLimitFix::ClampContactShadowDepthPixel(int2(2, 1), 0, dimensions), int2(0, 0));
	ASSERT(AreEqual, LightLimitFix::ClampContactShadowDepthPixel(int2(0, 0), 1, dimensions), int2(1, 0));
}
