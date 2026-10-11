#pragma once

#include <atomic>
#include <cstdint>

namespace NeuralRendering
{
	/** Coalesces UI requests; consumption belongs to a completed render-frame boundary. */
	class ShaderInvalidation
	{
	public:
		void Request() noexcept { requested_.fetch_add(1, std::memory_order_release); }
		[[nodiscard]] bool Pending() const noexcept { return Requested() != applied_.load(std::memory_order_relaxed); }
		[[nodiscard]] std::uint64_t Requested() const noexcept { return requested_.load(std::memory_order_acquire); }
		[[nodiscard]] std::uint64_t Applied() const noexcept { return applied_.load(std::memory_order_acquire); }
		/** Acknowledge only the captured request; a concurrent newer request remains pending. */
		void Applied(std::uint64_t generation) noexcept { applied_.store(generation, std::memory_order_release); }

	private:
		std::atomic<std::uint64_t> requested_{ 0 }, applied_{ 0 };
	};
}
