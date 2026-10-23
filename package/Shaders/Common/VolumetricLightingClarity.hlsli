#ifndef __VOLUMETRIC_LIGHTING_CLARITY_HLSLI__
#define __VOLUMETRIC_LIGHTING_CLARITY_HLSLI__

#include "Common/VR.hlsli"
#include "Common/VolumetricLighting.hlsli"

namespace VolumetricLighting
{
	static const float kHazeRadiusInViewHeight = 0.04;

	struct SamplingRegion
	{
		float2 minimumUV;
		float2 maximumUV;
		float2 hazeRadius;
	};

	/** Keep the center and neighborhood samples inside the same active texel region. */
	SamplingRegion GetSamplingRegion(Texture2D<float4> source, float2 resolutionScale, uint eyeIndex)
	{
		uint width, height;
		source.GetDimensions(width, height);
		uint2 dimensions = uint2(width, height);
		uint2 active = clamp(uint2(float2(dimensions) * resolutionScale), uint2(1, 1), dimensions);
		float2 activeScale = float2(active) / dimensions;
		SamplingRegion region;
		region.minimumUV = Stereo::ClampToEyeUV(0.5 / float2(active), eyeIndex, active) * activeScale;
		region.maximumUV = Stereo::ClampToEyeUV((float2(active) - 0.5) / active, eyeIndex, active) * activeScale;
		region.hazeRadius = (kHazeRadiusInViewHeight * active.y) / float2(dimensions);
		return region;
	}

	/** Estimate the local haze floor inside one active eye; only the volumetric buffer is sampled. */
	float EstimateHaze(Texture2D<float4> source, SamplerState sourceSampler, float2 uv,
		SamplingRegion region, float centerValue)
	{
		static const float2 offsets[8] = {
			float2(-1, 0), float2(1, 0), float2(0, -1), float2(0, 1),
			float2(-0.707107, -0.707107), float2(0.707107, -0.707107),
			float2(-0.707107, 0.707107), float2(0.707107, 0.707107)
		};
		float haze = centerValue;
		[unroll] for (uint i = 0; i < 8; ++i)
		{
			float2 sampleUV = clamp(uv + offsets[i] * region.hazeRadius, region.minimumUV, region.maximumUV);
			float sampleValue = source.SampleLevel(sourceSampler, sampleUV, 0).x;
			// An invalid neighbor must not turn an otherwise smooth field into a bright shaft.
			if (IsFinite(sampleValue))
				haze = min(haze, max(sampleValue, 0.0));
		}
		return haze;
	}

	/** Suppress a nonnegative background floor after the same opacity and colour curve as the shafts. */
	float ApplyClarity(Texture2D<float4> source, SamplerState sourceSampler, float2 uv,
		SamplingRegion region, float rawValue, float processedValue, float rawClarity, float opacity)
	{
		float clarity = IsFinite(rawClarity) ? saturate(rawClarity) : 0.0;
		float result = processedValue;
		[branch] if (clarity > kTuningEpsilon && processedValue > 0.0)
		{
			float positiveValue = IsFinite(rawValue) ? max(rawValue, 0.0) : 0.0;
			float haze = EstimateHaze(source, sourceSampler, uv, region, positiveValue);
			float hazePower = Color::VolumetricLighting(ApplyOpacity(haze, opacity).xxx).x;
			result = max(processedValue - clarity * min(processedValue, hazePower), 0.0);
		}
		return result;
	}
}

#endif
