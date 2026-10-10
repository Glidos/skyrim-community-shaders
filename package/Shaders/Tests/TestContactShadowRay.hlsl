#define COMPUTESHADER
#include "/Shaders/LightLimitFix/ContactShadowRay.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

/// @tags contact-shadows, projection
/// A projected point must recover its actual view depth, including a biased ray origin.
[numthreads(1, 1, 1)] void TestContactShadowProjectedDepth() {
	float3 startVS = float3(8, 0, 64);
	float3 endVS = float3(96, 0, 192);
	float3 pointVS = (startVS + endVS) * 0.5;
	float startX = startVS.x / startVS.z;
	float endX = endVS.x / endVS.z;
	float t = (pointVS.x / pointVS.z - startX) / (endX - startX);
	float depth = LightLimitFix::GetContactShadowRayDepth(rcp(float2(startVS.z, endVS.z)), t);
	ASSERT(IsTrue, abs(depth - pointVS.z) < 0.0001);
	ASSERT(IsTrue, abs(lerp(startVS.z, endVS.z, t) - pointVS.z) > 1.0);
}

	/// @tags contact-shadows, projection
	/// Endpoints and a constant-depth ray retain their view depths.
	[numthreads(1, 1, 1)] void TestContactShadowDepthEndpoints()
{
	float2 inverseDepth = rcp(float2(64, 192));
	ASSERT(IsTrue, abs(LightLimitFix::GetContactShadowRayDepth(inverseDepth, 0) - 64.0) < 0.0001);
	ASSERT(IsTrue, abs(LightLimitFix::GetContactShadowRayDepth(inverseDepth, 1) - 192.0) < 0.0001);
	ASSERT(AreEqual, LightLimitFix::GetContactShadowRayDepth(float2(0.125, 0.125), 0.7), 8.0);
}

/// @tags contact-shadows, flat, render-scale
/// Fractional active extents exclude unused rows and columns while retaining interior pixels.
[numthreads(1, 1, 1)] void TestContactShadowFlatBounds() {
	uint2 dimensions = LightLimitFix::GetContactShadowRenderDimensions(float2(201, 101), float2(0.805, 0.605));
	ASSERT(AreEqual, dimensions, uint2(161, 61));
	ASSERT(AreEqual, LightLimitFix::ClampContactShadowDepthPixel(int2(201, 101), 0, dimensions), int2(160, 60));
	ASSERT(AreEqual, LightLimitFix::ClampContactShadowDepthPixel(int2(-1, -1), 0, dimensions), int2(0, 0));
	ASSERT(AreEqual, LightLimitFix::ClampContactShadowDepthPixel(int2(24, 30), 0, dimensions), int2(24, 30));
	ASSERT(AreEqual, LightLimitFix::GetContactShadowRenderDimensions(float2(201, 101), float2(0, 0)), uint2(1, 1));
}
