#include "Common/Math.hlsli"

struct VS_INPUT
{
	float4 Color;
	float4 InstanceData1;
};
cbuffer NativeWorld : register(b0)
{
	row_major float4x4 World[1];
};
#include "GrassCollision/GrassCollision.hlsli"

struct Case
{
	float4 color;
	float4 root;
	float4 position;
	float4 distance;
};
struct Result
{
	float4 current;
	float4 previous;
};
StructuredBuffer<Case> Cases : register(t0);
RWStructuredBuffer<Result> Results : register(u0);

[numthreads(1, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
	VS_INPUT input;
	input.Color = Cases[id.x].color;
	input.InstanceData1 = Cases[id.x].root;
	float3 current, previous;
	GrassCollision::GetDisplacedPosition(input, Cases[id.x].position.xyz, current, previous, Cases[id.x].distance.x);
	Result output;
	output.current = float4(current, 0);
	output.previous = float4(previous, 0);
	Results[id.x] = output;
}
