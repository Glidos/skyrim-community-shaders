#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "PackedCropPolicy.h"

#	include <algorithm>
#	include <array>
#	include <chrono>
#	include <cstdint>
#	include <optional>

namespace NeuralRendering
{
	struct PackedCropObservation
	{
		std::uint32_t frame = 0, sourceFrame = 0, slot = 0;
		std::uint64_t generation = 0, sourceTransaction = 0;
		UpscalingDLSS::Extent source{}, model{}, guide{};
		ComputeSubrect available{};
		std::optional<std::uint64_t> baselinePixels;
		PackedCropSources sources{};
		PackedCropPlanningDiagnostics planning{};
		PackedCropCapacityState state{};
		PackedCropReason reason = PackedCropReason::NotExamined;
		bool geometryChanged = false, requested = false;
	};

	struct PackedCropRebuildTimings
	{
		std::optional<std::uint64_t> retirementMicroseconds, gpuDrainMicroseconds, releaseMicroseconds;
		std::optional<std::uint64_t> sharedAllocationMicroseconds, colorAllocationMicroseconds, nativeCreateMicroseconds, nativeEvaluateMicroseconds;
		std::optional<bool> gpuDrainSucceeded, releaseSucceeded, sharedAllocationSucceeded, colorAllocationSucceeded, nativeCreateSucceeded;
	};

	struct PackedCropTransition
	{
		std::uint64_t sequence = 0, monotonicNanoseconds = 0;
		std::optional<PackedCropObservation> previous;
		PackedCropObservation current{};
		std::optional<PackedCropAdmissionDiagnostics> retainedRejection;
		PackedCropRebuildTimings timings{};
		bool rejected = false, allocationOnly = false;
	};

	struct PackedCropEvaluationCounts
	{
		std::uint64_t packed = 0, single = 0, failed = 0, packedPixels = 0, singlePixels = 0;
	};

	/** CPU-only transitions retain the first rejection independently of ring overwrites and GPU resets. */
	class PackedCropDiagnostics
	{
	public:
		static constexpr std::size_t kCapacity = 32;
		[[nodiscard]] bool Enabled() const noexcept { return enabled_; }
		[[nodiscard]] std::uint64_t Total() const noexcept { return total_; }
		[[nodiscard]] std::uint64_t Rejections() const noexcept { return rejections_; }
		[[nodiscard]] const auto& FirstRejection() const noexcept { return firstRejection_; }
		[[nodiscard]] const auto& Records() const noexcept { return records_; }
		[[nodiscard]] const auto& Evaluations() const noexcept { return evaluations_; }
		void RecordEvaluation(std::uint32_t slot, bool packed, bool succeeded, std::uint64_t pixels) noexcept
		{
			if (!enabled_ || slot >= evaluations_.size())
				return;
			auto& counts = evaluations_[slot];
			if (!succeeded)
				++counts.failed;
			else if (packed) {
				++counts.packed;
				counts.packedPixels += pixels;
			} else {
				++counts.single;
				counts.singlePixels += pixels;
			}
		}
		void Configure(std::optional<bool> enabled, bool clear) noexcept
		{
			if (clear) {
				records_ = {};
				evaluations_ = {};
				firstRejection_.reset();
				total_ = rejections_ = 0;
				previous_ = {};
			}
			if (enabled && *enabled != enabled_) {
				previous_ = {};
				enabled_ = *enabled;
			}
		}
		void Observe(const PackedCropObservation& current,
			const std::optional<PackedCropAdmissionDiagnostics>& rejection, bool allocationOnly = false) noexcept
		{
			if (!enabled_ || current.slot >= previous_.size())
				return;
			auto& previous = previous_[current.slot];
			const bool active = current.state.active;
			const bool wasActive = previous && previous->state.active;
			const bool changed = !previous || allocationOnly || current.geometryChanged || active != wasActive ||
			                     (active && current.state.capacity != previous->state.capacity) ||
			                     (current.requested && (current.reason != previous->reason || !previous->requested));
			if (changed) {
				PackedCropTransition record;
				record.sequence = ++total_;
				record.monotonicNanoseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now().time_since_epoch())
						.count());
				record.previous = previous;
				record.current = current;
				record.allocationOnly = allocationOnly;
				record.retainedRejection = rejection;
				record.rejected = !active && (rejection.has_value() ||
												 (current.requested && current.reason != PackedCropReason::NotRequested &&
													 current.reason != PackedCropReason::CropDisabled && current.reason != PackedCropReason::EmptySelection &&
													 current.reason != PackedCropReason::UnsupportedRoute && current.reason != PackedCropReason::NotExamined &&
													 current.reason != PackedCropReason::CapacitySettling));
				records_[(record.sequence - 1u) % kCapacity] = record;
				if (record.rejected) {
					++rejections_;
					if (!firstRejection_)
						firstRejection_ = record;
				}
			}
			previous = current;
		}
		/** Match all ownership fields; an old event must never receive a later frame's costs. */
		[[nodiscard]] std::uint64_t Pending(std::uint32_t slot, std::uint32_t frame,
			std::uint32_t sourceFrame, std::uint64_t generation, std::uint64_t transaction = 0) const noexcept
		{
			if (!enabled_)
				return 0;
			for (std::uint64_t offset = 0; offset < std::min<std::uint64_t>(total_, kCapacity); ++offset) {
				const auto& record = records_[(total_ - offset - 1u) % kCapacity];
				const auto& current = record.current;
				if (current.slot == slot && current.frame == frame && current.sourceFrame == sourceFrame && current.generation == generation && current.sourceTransaction == transaction)
					return record.sequence;
			}
			return 0;
		}
		/** Allocation costs remain attributable even when packing stays inactive. */
		[[nodiscard]] std::uint64_t CostEvent(std::uint32_t slot, std::uint32_t frame,
			std::uint32_t sourceFrame, std::uint64_t generation, std::uint64_t transaction) noexcept
		{
			if (!enabled_ || slot >= previous_.size() || !previous_[slot])
				return 0;
			if (const auto pending = Pending(slot, frame, sourceFrame, generation, transaction))
				return pending;
			const auto current = *previous_[slot];
			if (current.frame != frame || current.sourceFrame != sourceFrame || current.generation != generation || current.sourceTransaction != transaction)
				return 0;
			Observe(current, std::nullopt, true);
			return total_;
		}
		template <class Callback>
		void Update(std::uint64_t sequence, Callback&& callback) noexcept
		{
			if (!enabled_ || !sequence)
				return;
			auto& record = records_[(sequence - 1u) % kCapacity];
			if (record.sequence != sequence)
				return;
			callback(record.timings);
			if (firstRejection_ && firstRejection_->sequence == sequence)
				firstRejection_->timings = record.timings;
		}

	private:
		bool enabled_ = false;
		std::uint64_t total_ = 0, rejections_ = 0;
		std::array<std::optional<PackedCropObservation>, kLogicalFeatureSlotCount> previous_{};
		std::array<PackedCropTransition, kCapacity> records_{};
		std::array<PackedCropEvaluationCounts, kLogicalFeatureSlotCount> evaluations_{};
		std::optional<PackedCropTransition> firstRejection_;
	};
	/** Capture all planning exits without depending on the game's scope helpers. */
	struct PackedCropObservationScope
	{
		PackedCropDiagnostics& diagnostics;
		std::optional<PackedCropObservation>& observation;
		const PackedCropCapacityState& state;
		const std::optional<PackedCropAdmissionDiagnostics>& rejection;
		~PackedCropObservationScope() noexcept
		{
			if (observation) {
				observation->state = state;
				diagnostics.Observe(*observation, rejection);
			}
		}
	};
}

#endif
