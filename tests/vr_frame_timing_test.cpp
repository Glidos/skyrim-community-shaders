#include "Utils/VRFrameTiming.h"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace
{
	using namespace Util::VRFrameTiming;

	struct Timing
	{
		std::uint32_t m_nFrameIndex = 1;
		double m_flSystemTimeInSeconds = 1.0;
		float m_flClientFrameIntervalMs = 22.22f;
		float m_flTotalRenderGpuMs = 15.5f;
		float m_flPreSubmitGpuMs = 12.86f;
		float m_flWaitGetPosesCalledMs = 1.0f;
		float m_flNewPosesReadyMs = 13.8f;
		float m_flNewFrameReadyMs = 16.4f;
		float m_flCompositorRenderCpuMs = 0.3f;
	};

	void Check(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void Near(double actual, double expected, const char* message)
	{
		Check(std::abs(actual - expected) < 0.0001, message);
	}

	void Definitions()
	{
		Timing timing;
		auto sample = Decode(timing);
		Check(sample.hasFrame && sample.hasCpu && sample.hasGpu, "valid timing was rejected");
		Near(sample.cpuMs, 2.9, "CPU must exclude pose wait and include compositor submission");
		Near(sample.poseWaitMs, 12.8, "pose wait must remain an independent detail");
		Near(sample.gpuMs, 15.5, "GPU must use total render time");
		Near(sample.sceneGpuMs, 12.86, "scene GPU must retain its narrower scope");
		timing.m_flTotalRenderGpuMs = 40.0f;
		timing.m_flNewFrameReadyMs = 50.0f;
		sample = Decode(timing);
		Near(sample.frameMs, 22.22, "work intervals must not clamp application cadence");
		timing.m_flNewFrameReadyMs = timing.m_flNewPosesReadyMs - 1.0f;
		timing.m_flCompositorRenderCpuMs = 5.0f;
		Check(!Decode(timing).hasCpu, "a negative subinterval must not become valid through addition");
		timing = {};
		timing.m_flCompositorRenderCpuMs = -1.0f;
		Check(!Decode(timing).hasCpu, "negative compositor CPU must be rejected");
		timing = {};
		timing.m_flNewPosesReadyMs = std::numeric_limits<float>::quiet_NaN();
		sample = Decode(timing);
		Check(!sample.hasCpu && !sample.hasPoseWait && sample.hasGpu && sample.hasFrame, "invalid CPU must remain isolated");
		timing = {};
		timing.m_flNewPosesReadyMs = timing.m_flWaitGetPosesCalledMs;
		Check(Decode(timing).hasPoseWait, "zero pose wait is valid");
		timing.m_flNewFrameReadyMs = 2000.0f;
		sample = Decode(timing);
		Check(sample.hasCpu && sample.cpuMs > 1000.0f, "shared decoder must preserve long stalls for tracing");
		timing = {};
		timing.m_flNewFrameReadyMs = timing.m_flNewPosesReadyMs;
		timing.m_flCompositorRenderCpuMs = 0.0f;
		Check(!Decode(timing).hasCpu, "unavailable zero CPU timestamps must not dilute live averages");
		timing.m_flClientFrameIntervalMs = 0.0f;
		timing.m_flTotalRenderGpuMs = std::numeric_limits<float>::infinity();
		sample = Decode(timing);
		Check(!sample.hasFrame && !sample.hasGpu, "unavailable cadence and nonfinite GPU must be rejected");
	}

	std::array<Timing, 5> History(std::uint32_t first)
	{
		std::array<Timing, 5> result;
		for (std::uint32_t i = 0; i < result.size(); ++i) {
			result[i].m_nFrameIndex = first + i;
			result[i].m_flSystemTimeInSeconds = 1.0 + (first + i) * 0.02;
		}
		return result;
	}

	void CompletedFrames()
	{
		Cursor cursor;
		std::vector<std::uint32_t> accepted;
		const auto consume = [&](const Timing& timing) { accepted.push_back(timing.m_nFrameIndex); };
		auto history = History(40);
		cursor.Consume(std::span<const Timing>(history), consume);
		Check(accepted == std::vector<std::uint32_t>{ 42 }, "first read must exclude old and in-flight frames");
		Check(cursor.latestFrameIndex == 44, "measurement barrier must include in-flight frames");
		cursor.Consume(std::span<const Timing>(history), consume);
		Check(accepted.size() == 1, "duplicate compositor read must not produce samples");
		history = History(42);
		cursor.Consume(std::span<const Timing>(history), consume);
		Check(accepted == std::vector<std::uint32_t>{ 42, 43, 44 }, "catch-up must preserve each completed frame exactly once");
		history = History(1);
		cursor.Consume(std::span<const Timing>(history), consume);
		Check(cursor.epoch == 1 && accepted.back() == 3, "compositor restart must begin a new epoch");
		history = History(2);
		history[2].m_flSystemTimeInSeconds = std::numeric_limits<double>::quiet_NaN();
		cursor.Consume(std::span<const Timing>(history), consume);
		Check(accepted.back() == 3, "invalid timestamp must not advance the completed cursor");
		history = History(3);
		cursor.Consume(std::span<const Timing>(history), consume);
		Check(accepted.back() == 5, "recovered completed frames must be accepted");
	}

	void FixedWindow()
	{
		Window window;
		Timing timing;
		timing.m_flSystemTimeInSeconds = 10.0;
		window.Push(Decode(timing));
		timing.m_flSystemTimeInSeconds = 10.5;
		timing.m_flTotalRenderGpuMs = 25.0f;
		window.Push(Decode(timing));
		auto stats = window.Get(10.75);
		Near(stats.gpu.Average(), 20.25, "display mean must include both samples");
		Near(stats.gpu.peak, 25.0, "display peak must retain the worst sample in the same window");
		Check(window.Get(11.0).gpu.count == 1, "time window must expire by timestamp, not sample count");
		Check(window.Get(11.5).gpu.count == 0, "stale display samples must expire without new frames");
		for (const auto rate : { 45, 90 }) {
			window = {};
			for (int i = 0; i < rate * 3; ++i) {
				timing.m_flSystemTimeInSeconds = 20.0 + static_cast<double>(i) / rate;
				timing.m_flClientFrameIntervalMs = 1000.0f / rate;
				window.Push(Decode(timing));
			}
			stats = window.Get(timing.m_flSystemTimeInSeconds);
			Check(stats.frame.count >= static_cast<unsigned>(rate - 1) && stats.frame.count <= static_cast<unsigned>(rate + 1),
				"window duration must not change with frame rate");
			Near(1000.0f / stats.frame.Average(), rate, "FPS must follow application cadence");
		}
	}
}

int main()
{
	try {
		Definitions();
		CompletedFrames();
		FixedWindow();
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
	std::cout << "VR frame timing tests passed\n";
}
