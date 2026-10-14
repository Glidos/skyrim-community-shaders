#ifdef EARLY_CATEGORY_BOUNDS
#	include "Common/CharacterCategoryMask.hlsli"
#endif

// Exact bounds of positive pixels, or enabled authored categories before resolve.
// One group owns one tile and always replaces its output record, including
// empty tiles. Bounds are global mask coordinates with exclusive maximum edges.
#ifndef CHARACTER_MASK_TILE_SIZE
#	define CHARACTER_MASK_TILE_SIZE 32
#endif
#if CHARACTER_MASK_TILE_SIZE != 16 && CHARACTER_MASK_TILE_SIZE != 32
#	error Unsupported character mask tile size
#endif
#define CHARACTER_MASK_LANE_SIZE (CHARACTER_MASK_TILE_SIZE / 8)

cbuffer CharacterMaskBoundsCB : register(b0)
{
	uint4 Size;  // mask width, mask height, tile count X, reserved
#ifdef EARLY_CATEGORY_BOUNDS
	uint4 ValidRegion[2];  // current capture rectangle per eye, offset.xy/extent.zw
#endif
};

#ifdef EARLY_CATEGORY_BOUNDS
Texture2D<unorm float2> AuthoredTuple : register(t0);
#else
Texture2D<unorm float> CharacterSelectionMask : register(t0);
#endif
RWStructuredBuffer<uint4> TileBounds : register(u0);
groupshared uint4 GroupBounds[64];

bool IsSelected(uint2 pixel, uint eye)
{
	if (any(pixel >= Size.xy))
		return false;
#ifdef EARLY_CATEGORY_BOUNDS
	if (any(pixel < ValidRegion[eye].xy) || any(pixel >= ValidRegion[eye].xy + ValidRegion[eye].zw))
		return false;
	const uint category = CharacterCategoryMask::DecodeCategory(
		AuthoredTuple.Load(int3(pixel + uint2(eye * Size.x, 0), 0)));
	return category >= 1u && category <= CharacterCategoryMask::CategoryCount && (Size.w & (1u << category)) != 0u;
#else
	return CharacterSelectionMask.Load(int3(pixel, 0)) > 0.0;
#endif
}

[numthreads(8, 8, 1)] void main(uint3 groupID : SV_GroupID, uint3 threadID : SV_GroupThreadID,
	uint lane : SV_GroupIndex) {
	uint2 minimum = uint2(0xffffffffu, 0xffffffffu);
	uint2 maximum = 0u;
	const uint2 base = groupID.xy * CHARACTER_MASK_TILE_SIZE + threadID.xy * CHARACTER_MASK_LANE_SIZE;
	[unroll] for (uint y = 0u; y < CHARACTER_MASK_LANE_SIZE; ++y)
	{
		[unroll] for (uint x = 0u; x < CHARACTER_MASK_LANE_SIZE; ++x)
		{
			const uint2 pixel = base + uint2(x, y);
			if (IsSelected(pixel, groupID.z)) {
				minimum = min(minimum, pixel);
				maximum = max(maximum, pixel + 1u);
			}
		}
	}

	GroupBounds[lane] = uint4(minimum, maximum);
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint stride = 32u; stride > 0u; stride >>= 1u)
	{
		if (lane < stride) {
			GroupBounds[lane].xy = min(GroupBounds[lane].xy, GroupBounds[lane + stride].xy);
			GroupBounds[lane].zw = max(GroupBounds[lane].zw, GroupBounds[lane + stride].zw);
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if (lane == 0u) {
		const uint eyeOffset = groupID.z * Size.z * ((Size.y + CHARACTER_MASK_TILE_SIZE - 1u) / CHARACTER_MASK_TILE_SIZE);
		TileBounds[eyeOffset + groupID.y * Size.z + groupID.x] =
			GroupBounds[0].x == 0xffffffffu ? uint4(0u, 0u, 0u, 0u) : GroupBounds[0];
	}
}
