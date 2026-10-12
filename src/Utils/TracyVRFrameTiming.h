#pragma once

#include "Utils/OpenVRFrameTiming.h"
#include "Utils/VRFrameTiming.h"

#include <Tracy/Tracy.hpp>

#include <array>
#include <cmath>
#include <cstdint>

namespace Util::TracyVRFrameTiming
{
#if defined(TRACY_ENABLE) && defined(ENABLE_SKYRIM_VR)
	inline bool ValidMs(double a_value)
	{
		return std::isfinite(a_value) && a_value > 0.0;
	}

	inline bool ValidNonnegativeMs(double a_value)
	{
		return std::isfinite(a_value) && a_value >= 0.0;
	}

	inline void PlotFrame(const vr::Compositor_FrameTiming& a_timing, std::uint32_t& a_lastFrameIndex, double& a_lastSystemTime)
	{
		const auto sample = VRFrameTiming::Decode(a_timing);
		const double poseToSubmitMs = a_timing.m_flNewFrameReadyMs - a_timing.m_flWaitGetPosesCalledMs;
		if (ValidMs(poseToSubmitMs))
			TracyPlot("VR::PoseToSubmitMs", poseToSubmitMs);
		if (ValidMs(poseToSubmitMs) && sample.hasPoseWait && sample.hasRenderCpu) {
			TracyPlot("VR::PoseWaitElapsedMs", static_cast<double>(sample.poseWaitMs));
			TracyPlot("VR::RenderToSubmitElapsedMs", static_cast<double>(sample.renderCpuMs));
		}
		if (sample.hasCpu)
			TracyPlot("VR::OpenVRCpuFrameMs", static_cast<double>(sample.cpuMs));
		if (ValidMs(a_timing.m_flPreSubmitGpuMs))
			TracyPlot("VR::AppPreSubmitGpuMs", a_timing.m_flPreSubmitGpuMs);
		if (ValidNonnegativeMs(a_timing.m_flPostSubmitGpuMs))
			TracyPlot("VR::AppPostSubmitGpuMs", a_timing.m_flPostSubmitGpuMs);
		if (ValidMs(a_timing.m_flTotalRenderGpuMs))
			TracyPlot("VR::TotalRenderGpuMs", a_timing.m_flTotalRenderGpuMs);
		if (ValidNonnegativeMs(a_timing.m_flCompositorRenderGpuMs))
			TracyPlot("VR::CompositorRenderGpuMs", a_timing.m_flCompositorRenderGpuMs);
		if (ValidNonnegativeMs(a_timing.m_flCompositorRenderCpuMs))
			TracyPlot("VR::CompositorRenderCpuMs", a_timing.m_flCompositorRenderCpuMs);
		if (ValidNonnegativeMs(a_timing.m_flCompositorIdleCpuMs))
			TracyPlot("VR::CompositorIdleCpuMs", a_timing.m_flCompositorIdleCpuMs);
		if (ValidNonnegativeMs(a_timing.m_flPresentCallCpuMs))
			TracyPlot("VR::PresentCallCpuMs", a_timing.m_flPresentCallCpuMs);
		if (ValidNonnegativeMs(a_timing.m_flWaitForPresentCpuMs))
			TracyPlot("VR::WaitForPresentCpuMs", a_timing.m_flWaitForPresentCpuMs);
		if (ValidNonnegativeMs(a_timing.m_flSubmitFrameMs))
			TracyPlot("VR::SubmitFrameCpuMs", a_timing.m_flSubmitFrameMs);
		if (ValidMs(a_timing.m_flClientFrameIntervalMs))
			TracyPlot("VR::ClientFrameIntervalMs", a_timing.m_flClientFrameIntervalMs);

		TracyPlot("VR::FramePresents", static_cast<std::int64_t>(a_timing.m_nNumFramePresents));
		TracyPlot("VR::MisPresented", static_cast<std::int64_t>(a_timing.m_nNumMisPresented));
		TracyPlot("VR::DroppedFrames", static_cast<std::int64_t>(a_timing.m_nNumDroppedFrames));
		TracyPlot("VR::ReprojectionFlags", static_cast<std::int64_t>(a_timing.m_nReprojectionFlags));
		TracyPlot("VR::CompositorFrameIndex", static_cast<std::int64_t>(a_timing.m_nFrameIndex));
		if (a_lastFrameIndex != 0 && a_timing.m_nFrameIndex > a_lastFrameIndex) {
			const auto advance = a_timing.m_nFrameIndex - a_lastFrameIndex;
			TracyPlot("VR::FrameIndexAdvance", static_cast<std::int64_t>(advance));
			const double intervalMs = (a_timing.m_flSystemTimeInSeconds - a_lastSystemTime) * 1000.0;
			if (ValidMs(intervalMs)) {
				if (advance == 1)
					TracyPlot("VR::CompositorFrameIntervalMs", intervalMs);
				else
					TracyPlot("VR::ObservedTimingGapMs", intervalMs);
			}
		}
		a_lastFrameIndex = a_timing.m_nFrameIndex;
		a_lastSystemTime = a_timing.m_flSystemTimeInSeconds;
	}
#endif

	/** @brief Records bounded OpenVR history as Tracy plots without inventing elapsed zones. */
	inline void Record(std::uint32_t a_gameFrame)
	{
#if defined(TRACY_ENABLE) && defined(ENABLE_SKYRIM_VR)
		static constexpr std::uint32_t kRetryFrames = 120;
		static VRFrameTiming::Cursor cursor;
		static std::uint32_t lastFrameIndex = 0;
		static double lastSystemTime = 0.0;
		static std::uint32_t nextRetryFrame = 0;
		static bool disabled = false;

		if (!TracyIsConnected) {
			cursor = {};
			lastFrameIndex = 0;
			lastSystemTime = 0.0;
			nextRetryFrame = 0;
			disabled = false;
			return;
		}
		if (disabled || a_gameFrame == 0 || a_gameFrame < nextRetryFrame)
			return;

		bool faulted = false;
		auto* compositor = OpenVRFrameTiming::TryResolveCompositor(&faulted);
		if (faulted) {
			disabled = true;
			return;
		}
		if (!compositor) {
			nextRetryFrame = a_gameFrame + kRetryFrames;
			return;
		}

		std::array<vr::Compositor_FrameTiming, VRFrameTiming::kMaxCatchUpFrames + VRFrameTiming::kCompletionLagFrames> history{};
		const auto count = OpenVRFrameTiming::TryGetFrameTimings(
			compositor, history.data(), static_cast<std::uint32_t>(history.size()), &faulted);
		if (count == 0) {
			disabled = faulted;
			if (!faulted)
				nextRetryFrame = a_gameFrame + 1;
			return;
		}
		cursor.Consume(std::span<const vr::Compositor_FrameTiming>(history.data(), count), [&](const auto& timing) {
			PlotFrame(timing, lastFrameIndex, lastSystemTime);
		});
#else
		(void)a_gameFrame;
#endif
	}
}
