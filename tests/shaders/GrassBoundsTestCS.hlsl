#include "GrassOptimizations/GrassCullingBounds.hlsli"

struct TestCase
{
	row_major float4x4 projection;
	float4 sphere;
};
StructuredBuffer<TestCase> Cases : register(t0);
RWStructuredBuffer<float4> Results : register(u0);
[numthreads(1, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
	TestCase item = Cases[id.x];
	float2 lo, hi;
	float nearDepth;
	bool valid = GrassProjectSphere(item.projection, item.sphere.xyz, item.sphere.w, lo, hi, nearDepth);
	Results[id.x * 2] = float4(lo, hi);
	Results[id.x * 2 + 1] = float4(nearDepth, valid, GrassBasisNorm((float3x3)item.projection), 0);
}
