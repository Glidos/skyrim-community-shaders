#include "Upscaling/NeuralRendering/ModelResolutionCommon.hlsli"

#ifndef PACKED_CROP_SUPPORT_PIXELS
#	error PACKED_CROP_SUPPORT_PIXELS must match the CPU crop policy
#endif

Texture2D<unorm float> CurrentSelection : register(t0);
RWStructuredBuffer<uint> PackedCoverage : register(u0);
groupshared uint GroupMissing;

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID, uint lane : SV_GroupIndex) {
	if (lane == 0u)
		GroupMissing = 0u;
	GroupMemoryBarrierWithGroupSync();
	if (all(id.xy < SourceRegionSize)) {
		const uint2 pixel = SourceRegionOffset + id.xy;
		if (CurrentSelection.Load(int3(pixel, 0)) > 0.0) {
			const uint2 lower = uint2(max(int2(SourceRegionOffset), int2(pixel) - PACKED_CROP_SUPPORT_PIXELS));
			const uint2 upper = min(SourceRegionOffset + SourceRegionSize, pixel + 1u + PACKED_CROP_SUPPORT_PIXELS);
			bool covered = false;
			[loop] for (uint i = 0u; i < PackedCount; ++i)
			{
				const uint4 crop = PackedTiles[i].source;
				covered = covered || (all(lower >= crop.xy) && all(upper <= crop.xy + crop.zw));
			}
			if (!covered)
				InterlockedOr(GroupMissing, 1u);
		}
	}
	GroupMemoryBarrierWithGroupSync();
	// Both eyes accumulate into the same flag before either reconstruction.
	if (lane == 0u && GroupMissing != 0u)
		InterlockedOr(PackedCoverage[0], 1u);
}
