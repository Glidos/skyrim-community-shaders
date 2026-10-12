#pragma once

#include <cstdint>
#include <openvr.h>

namespace TestRuntime
{
	enum class Mode
	{
		Live,
		NoCompositor,
		ResolveFault,
		Empty,
		ReadFault,
		Stale
	};
	Mode mode = Mode::Live;
	std::uint32_t compositorFrame = 200;
	std::uint32_t gameFrame = 1;
	std::uint32_t ticks = 0;
	std::uint32_t resolves = 0;
	std::uint32_t reads = 0;
	float gpuOffset = 0.0f;
	float cpuOffset = 0.0f;
	bool varyGpu = true;
	int compositor;
}

namespace Util::OpenVRFrameTiming
{
	int* TryResolveCompositor(bool* faulted)
	{
		using namespace TestRuntime;
		++resolves;
		*faulted = mode == Mode::ResolveFault;
		return mode == Mode::NoCompositor || *faulted ? nullptr : &compositor;
	}

	bool TryGetFrameTiming(int*, vr::Compositor_FrameTiming* timing, bool* faulted, std::uint32_t ago)
	{
		using namespace TestRuntime;
		++reads;
		*faulted = mode == Mode::ReadFault;
		if (mode == Mode::Empty || *faulted)
			return false;
		*timing = {};
		timing->m_nSize = sizeof(*timing);
		timing->m_nFrameIndex = compositorFrame - ago;
		timing->m_flSystemTimeInSeconds = 100.0 + timing->m_nFrameIndex * 0.01;
		timing->m_flClientFrameIntervalMs = 10.0f;
		timing->m_flTotalRenderGpuMs = 6.0f + gpuOffset + (varyGpu ? (timing->m_nFrameIndex % 17) * 0.1f : 0.0f);
		timing->m_flPreSubmitGpuMs = 5.0f;
		timing->m_flWaitGetPosesCalledMs = 1.0f;
		timing->m_flNewPosesReadyMs = 7.0f;
		timing->m_flNewFrameReadyMs = 9.0f + cpuOffset;
		timing->m_flCompositorRenderCpuMs = 0.3f;
		return true;
	}
}
