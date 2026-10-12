#pragma once

#include "ProfilerTiming.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>

namespace Util::VRFrameTiming
{
	inline constexpr double kDisplayWindowSeconds = 1.0;
	inline constexpr std::uint32_t kCompletionLagFrames = 2;
	inline constexpr std::uint32_t kMaxCatchUpFrames = 16;
	inline constexpr std::size_t kMaxWindowSamples = 512;
	inline constexpr std::uint32_t kDefinitionVersion = 2;

	/** Read bounded, oldest-first history through the single-frame API used by Skyrim's compositor. */
	template <class Timing, class Reader>
	std::size_t ReadRecentHistory(std::span<Timing> history, std::uint32_t lastCompletedFrame, Reader&& read)
	{
		if (history.size() <= kCompletionLagFrames)
			return 0;
		Timing latest{};
		if (!read(latest, 0))
			return 0;
		const auto available = std::min<std::size_t>(history.size(), kMaxCatchUpFrames + kCompletionLagFrames);
		// One extra older record keeps a completed sample available if the runtime advances during reads.
		const auto pending = lastCompletedFrame != 0 && latest.m_nFrameIndex > lastCompletedFrame ?
		                         static_cast<std::uint64_t>(latest.m_nFrameIndex) - lastCompletedFrame + 1 :
		                         kCompletionLagFrames + 2;
		const auto count = std::clamp<std::size_t>(pending, kCompletionLagFrames + 1, available);
		history[count - 1] = latest;
		for (std::uint32_t ago = 1; ago < count; ++ago) {
			if (!read(history[count - 1 - ago], ago))
				return 0;
		}
		return count;
	}

	struct Sample
	{
		std::uint32_t frameIndex = 0;
		double systemTime = 0.0;
		float frameMs = 0.0f;
		float gpuMs = 0.0f;
		float cpuMs = 0.0f;
		float sceneGpuMs = 0.0f;
		float poseWaitMs = 0.0f;
		float renderCpuMs = 0.0f;
		bool hasFrame = false;
		bool hasGpu = false;
		bool hasCpu = false;
		bool hasSceneGpu = false;
		bool hasPoseWait = false;
		bool hasRenderCpu = false;
	};

	/** Decode Valve's render/submission CPU and total GPU definitions without pose-wait inflation. */
	template <class Timing>
	Sample Decode(const Timing& timing)
	{
		Sample sample;
		sample.frameIndex = timing.m_nFrameIndex;
		sample.systemTime = timing.m_flSystemTimeInSeconds;
		sample.frameMs = timing.m_flClientFrameIntervalMs;
		sample.gpuMs = timing.m_flTotalRenderGpuMs;
		sample.sceneGpuMs = timing.m_flPreSubmitGpuMs;
		sample.poseWaitMs = timing.m_flNewPosesReadyMs - timing.m_flWaitGetPosesCalledMs;
		sample.renderCpuMs = timing.m_flNewFrameReadyMs - timing.m_flNewPosesReadyMs;
		sample.cpuMs = sample.renderCpuMs + timing.m_flCompositorRenderCpuMs;
		sample.hasFrame = ProfilerTiming::IsValidSample(sample.frameMs) && sample.frameMs > 0.0f;
		sample.hasGpu = ProfilerTiming::IsValidSample(sample.gpuMs) && sample.gpuMs > 0.0f;
		sample.hasSceneGpu = ProfilerTiming::IsValidSample(sample.sceneGpuMs) && sample.sceneGpuMs > 0.0f;
		sample.hasPoseWait = std::isfinite(sample.poseWaitMs) && sample.poseWaitMs >= 0.0f;
		sample.hasRenderCpu = std::isfinite(sample.renderCpuMs) && sample.renderCpuMs >= 0.0f;
		// Diagnostics retain long stalls; display and comparison consumers apply their own limits.
		sample.hasCpu = sample.hasRenderCpu && std::isfinite(timing.m_flCompositorRenderCpuMs) &&
		                timing.m_flCompositorRenderCpuMs >= 0.0f && std::isfinite(sample.cpuMs) && sample.cpuMs > 0.0f;
		return sample;
	}

	/** Select completed, unique frames; resets define a new timing epoch for measurement ownership. */
	struct Cursor
	{
		std::uint32_t lastFrameIndex = 0;
		std::uint32_t latestFrameIndex = 0;
		double lastSystemTime = 0.0;
		double latestSystemTime = 0.0;
		std::uint64_t epoch = 0;

		void Reset()
		{
			++epoch;
			lastFrameIndex = latestFrameIndex = 0;
			lastSystemTime = latestSystemTime = 0.0;
		}

		template <class Timing, class Consumer>
		void Consume(std::span<const Timing> history, Consumer&& consume)
		{
			if (history.empty())
				return;
			const auto& latest = history.back();
			if (!std::isfinite(latest.m_flSystemTimeInSeconds) || latest.m_flSystemTimeInSeconds <= 0.0)
				return;
			if (latestSystemTime > 0.0 &&
				(latest.m_nFrameIndex < latestFrameIndex || latest.m_flSystemTimeInSeconds < latestSystemTime))
				Reset();
			latestFrameIndex = latest.m_nFrameIndex;
			latestSystemTime = latest.m_flSystemTimeInSeconds;
			if (history.size() <= kCompletionLagFrames)
				return;
			const auto completed = history.size() - kCompletionLagFrames;
			const auto isCompleted = [&](const auto& timing) {
				return timing.m_nFrameIndex <= latestFrameIndex &&
				       latestFrameIndex - timing.m_nFrameIndex >= kCompletionLagFrames &&
				       std::isfinite(timing.m_flSystemTimeInSeconds) && timing.m_flSystemTimeInSeconds > 0.0 &&
				       timing.m_flSystemTimeInSeconds <= latestSystemTime;
			};
			// The first observation establishes ownership; older frames predate the observer.
			auto first = lastSystemTime == 0.0 ? completed - 1 : 0;
			while (first > 0 && !isCompleted(history[first]))
				--first;
			for (auto i = first; i < completed; ++i) {
				const auto& timing = history[i];
				if (!isCompleted(timing) ||
					(lastSystemTime > 0.0 && timing.m_nFrameIndex <= lastFrameIndex) ||
					timing.m_flSystemTimeInSeconds <= lastSystemTime)
					continue;
				consume(timing);
				lastFrameIndex = timing.m_nFrameIndex;
				lastSystemTime = timing.m_flSystemTimeInSeconds;
			}
		}
	};

	struct Metric
	{
		double sum = 0.0;
		float peak = 0.0f;
		std::uint32_t count = 0;

		void Add(float value, bool valid)
		{
			if (!valid || !ProfilerTiming::IsValidSample(value))
				return;
			sum += value;
			peak = std::max(peak, value);
			++count;
		}

		[[nodiscard]] float Average() const { return count ? static_cast<float>(sum / count) : 0.0f; }
	};

	struct Statistics
	{
		Metric frame, gpu, cpu, sceneGpu, poseWait;
	};

	/** Bounded display history with a fixed time window, independent of application frame rate. */
	struct Window
	{
		std::deque<Sample> samples;

		void Push(const Sample& sample)
		{
			if (!std::isfinite(sample.systemTime) || sample.systemTime <= 0.0)
				return;
			samples.push_back(sample);
			while (samples.size() > kMaxWindowSamples ||
				   sample.systemTime - samples.front().systemTime >= kDisplayWindowSeconds)
				samples.pop_front();
		}

		[[nodiscard]] Statistics Get(double now) const
		{
			Statistics result;
			for (const auto& sample : samples) {
				const double age = now - sample.systemTime;
				if (!std::isfinite(age) || age < 0.0 || age >= kDisplayWindowSeconds)
					continue;
				result.frame.Add(sample.frameMs, sample.hasFrame);
				result.gpu.Add(sample.gpuMs, sample.hasGpu);
				result.cpu.Add(sample.cpuMs, sample.hasCpu);
				result.sceneGpu.Add(sample.sceneGpuMs, sample.hasSceneGpu);
				result.poseWait.Add(sample.poseWaitMs, sample.hasPoseWait);
			}
			return result;
		}
	};
}
