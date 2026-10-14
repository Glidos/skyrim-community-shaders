#pragma once

#include "PackedCropDiagnostics.h"
#include "PackedCropPolicy.h"
#include <nlohmann/json.hpp>

namespace NeuralRendering
{
#ifdef DEVBENCH_BRIDGE_ENABLED
	constexpr const char* PackedCropReasonName(PackedCropReason reason) noexcept
	{
		switch (reason) {
		case PackedCropReason::NotRequested:
			return "not_requested";
		case PackedCropReason::CropDisabled:
			return "cropping_disabled";
		case PackedCropReason::FullOutputMask:
			return "full_output_mask";
		case PackedCropReason::EmptySelection:
			return "empty_selection";
		case PackedCropReason::UncertainProjection:
			return "uncertain_projection";
		case PackedCropReason::FullEyeFallback:
			return "full_eye_eligibility_fallback";
		case PackedCropReason::NotExamined:
			return "not_examined";
		case PackedCropReason::TooFewRegions:
			return "fewer_than_two_regions";
		case PackedCropReason::InvalidSourceBounds:
			return "invalid_source_bounds";
		case PackedCropReason::ContextOutsideAvailable:
			return "required_context_outside_available_region";
		case PackedCropReason::MergedToSingle:
			return "context_overlap_merged_to_single_region";
		case PackedCropReason::SourcesReady:
			return "source_regions_ready";
		case PackedCropReason::RetainedSources:
			return "retained_previous_source_regions";
		case PackedCropReason::InvalidModelGeometry:
			return "invalid_model_geometry";
		case PackedCropReason::UnsupportedRoute:
			return "unsupported_route";
		case PackedCropReason::InvalidCropCount:
			return "invalid_crop_count";
		case PackedCropReason::OverlappingSources:
			return "overlapping_source_regions";
		case PackedCropReason::InvalidModelMapping:
			return "invalid_model_mapping";
		case PackedCropReason::NoBoundedLayout:
			return "no_layout_within_dimension_limit";
		case PackedCropReason::InsufficientAreaSaving:
			return "less_than_25_percent_area_saving";
		case PackedCropReason::ProviderTypeChanged:
			return "provider_feature_type_changed";
		case PackedCropReason::InvalidProjection:
			return "invalid_projection_contract";
		case PackedCropReason::CapacitySettling:
			return "atlas_capacity_settling";
		case PackedCropReason::ShadersUnavailable:
			return "packing_shaders_unavailable";
		case PackedCropReason::Admitted:
			return "admitted";
		}
		return "unknown";
	}

	inline nlohmann::json PackedCropSourcesJson(const PackedCropSources& sources)
	{
		auto regions = nlohmann::json::array();
		for (std::uint32_t i = 0; i < sources.count && i < kMaximumPackedCrops; ++i) {
			const auto& r = sources.regions[i];
			regions.push_back({ { "x", r.baseX }, { "y", r.baseY }, { "width", r.width }, { "height", r.height } });
		}
		return regions;
	}

	inline nlohmann::json PackedCropPlanningJson(const PackedCropPlanningDiagnostics& value)
	{
		using Json = nlohmann::json;
		return { { "reason", PackedCropReasonName(value.reason) }, { "candidateReason", PackedCropReasonName(value.candidateReason) },
			{ "inputRegionCount", value.inputRegions }, { "candidateExamined", value.candidateExamined },
			{ "tileSourceFrame", value.tileSourceFrame == std::numeric_limits<std::uint32_t>::max() ? Json(nullptr) : Json(value.tileSourceFrame) },
			{ "mergedRegionCount", value.candidateExamined ? Json(value.mergedContexts.count) : Json(nullptr) },
			{ "selectedRegionCount", value.selectedSources.count }, { "mergedContexts", PackedCropSourcesJson(value.mergedContexts) },
			{ "selectedContexts", PackedCropSourcesJson(value.selectedSources) },
			{ "contextPixels", kPackedCropContextPixels }, { "samplingGuardPixels", kCharacterComputeRoiGuardPixels },
			{ "alignmentPixels", kPackedCropAlignment },
			{ "availableRegion", { { "x", value.available.baseX }, { "y", value.available.baseY }, { "width", value.available.width }, { "height", value.available.height } } },
			{ "coordinateSpace", "eye_local_source_output_pixels" } };
	}

	/** Admission does not prove GPU mask coverage or a committed neural output. */
	inline nlohmann::json PackedCropAdmissionJson(const PackedCropAdmissionDiagnostics& value, std::uint32_t slot)
	{
		using Json = nlohmann::json;
		const auto stage = [](PackedCropStage s) {
			switch (s) {
			case PackedCropStage::SourcePlanning:
				return "source_planning";
			case PackedCropStage::ModelProjection:
				return "model_projection";
			case PackedCropStage::AtlasLayout:
				return "atlas_layout";
			case PackedCropStage::AreaAdmission:
				return "area_admission";
			case PackedCropStage::ProviderCompatibility:
				return "provider_compatibility";
			case PackedCropStage::Admitted:
				return "admitted";
			}
			return "unknown";
		};
		return { { "featureSlot", slot }, { "frame", value.frame == std::numeric_limits<std::uint32_t>::max() ? Json(nullptr) : Json(value.frame) },
			{ "sourceWorldFrame", value.sourceFrame == std::numeric_limits<std::uint32_t>::max() ? Json(nullptr) : Json(value.sourceFrame) },
			{ "generation", value.generation }, { "stage", stage(value.stage) }, { "reason", PackedCropReasonName(value.reason) },
			{ "modelResolutionPercent", value.modelResolutionPercent }, { "sourcePlanning", PackedCropPlanningJson(value.planning) },
			{ "baselineSourcePixels", value.baselineSourcePixels },
			{ "layoutBaselineModelPixels", value.modelAreasAvailable && value.layoutBaselineModelPixels ? Json(value.layoutBaselineModelPixels) : Json(nullptr) },
			{ "effectiveBaselineModelPixels", value.modelAreasAvailable ? Json(value.effectiveBaselineModelPixels) : Json(nullptr) },
			{ "candidateModelPixels", value.candidateAreaAvailable ? Json(value.candidateModelPixels) : Json(nullptr) },
			{ "candidateWidth", value.candidateAreaAvailable ? Json(value.candidateExtent.width) : Json(nullptr) },
			{ "candidateHeight", value.candidateAreaAvailable ? Json(value.candidateExtent.height) : Json(nullptr) },
			{ "capacity", { { "width", value.capacity.capacity.width }, { "height", value.capacity.capacity.height },
							  { "stableFrames", value.capacity.stableFrames }, { "requiredFrames", value.capacity.requiredFrames },
							  { "quantumPixels", kPackedCropCapacityQuantum }, { "reason", PackedCropReasonName(value.capacity.reason) } } },
			{ "minimumAreaSavingPercent", 25 }, { "minimumAtlasSidePixels", kMinimumExperimentalContextExtent }, { "scope", "cpu_atlas_admission_not_gpu_coverage_or_committed_output" } };
	}
	inline nlohmann::json PackedCropObservationJson(const PackedCropObservation& value)
	{
		using Json = nlohmann::json;
		const auto extent = [](auto e) { return Json::array({ e.width, e.height }); };
		return { { "frame", value.frame }, { "sourceWorldFrame", value.sourceFrame }, { "featureSlot", value.slot },
			{ "generation", std::to_string(value.generation) }, { "sourceTransactionId", value.sourceTransaction ? Json(std::to_string(value.sourceTransaction)) : Json(nullptr) },
			{ "sourceSize", extent(value.source) }, { "modelSize", extent(value.model) }, { "guideSize", extent(value.guide) },
			{ "baselineModelPixels", value.baselinePixels ? Json(*value.baselinePixels) : Json(nullptr) },
			{ "availableRegion", { { "x", value.available.baseX }, { "y", value.available.baseY }, { "width", value.available.width }, { "height", value.available.height } } },
			{ "sources", PackedCropSourcesJson(value.sources) }, { "planning", PackedCropPlanningJson(value.planning) },
			{ "capacity", extent(value.state.capacity) }, { "active", value.state.active },
			{ "stableFrames", value.state.stableFrames }, { "requiredFrames", value.state.requiredFrames },
			{ "reason", PackedCropReasonName(value.reason) }, { "geometryContractChanged", value.geometryChanged }, { "requested", value.requested } };
	}

	inline nlohmann::json PackedCropTransitionJson(const PackedCropTransition& value)
	{
		using Json = nlohmann::json;
		const auto optional = [](const auto& v) { return v ? Json(*v) : Json(nullptr); };
		const auto& t = value.timings;
		return { { "sequence", value.sequence }, { "monotonicNanoseconds", std::to_string(value.monotonicNanoseconds) },
			{ "rejected", value.rejected }, { "allocationOnly", value.allocationOnly }, { "previous", value.previous ? PackedCropObservationJson(*value.previous) : Json(nullptr) },
			{ "current", PackedCropObservationJson(value.current) },
			{ "retainedCapacityTest", value.retainedRejection ? PackedCropAdmissionJson(*value.retainedRejection, value.current.slot) : Json(nullptr) },
			{ "cpuMicroseconds", { { "retirement", optional(t.retirementMicroseconds) }, { "gpuDrain", optional(t.gpuDrainMicroseconds) },
									 { "nativeRelease", optional(t.releaseMicroseconds) }, { "sharedAllocation", optional(t.sharedAllocationMicroseconds) },
									 { "colorAllocation", optional(t.colorAllocationMicroseconds) }, { "nativeCreate", optional(t.nativeCreateMicroseconds) }, { "nativeEvaluate", optional(t.nativeEvaluateMicroseconds) } } },
			{ "outcomes", { { "gpuDrainSucceeded", optional(t.gpuDrainSucceeded) }, { "nativeReleaseSucceeded", optional(t.releaseSucceeded) },
							  { "sharedAllocationSucceeded", optional(t.sharedAllocationSucceeded) }, { "colorAllocationSucceeded", optional(t.colorAllocationSucceeded) }, { "nativeCreateSucceeded", optional(t.nativeCreateSucceeded) } } } };
	}

	/** Reading retained CPU evidence adds no GPU queries, waits, or sampling requests. */
	inline nlohmann::json PackedCropDiagnosticsJson(const PackedCropDiagnostics& value)
	{
		using Json = nlohmann::json;
		auto records = Json::array();
		auto evaluations = Json::array();
		for (std::size_t slot = 0; slot < value.Evaluations().size(); ++slot) {
			const auto& counts = value.Evaluations()[slot];
			evaluations.push_back({ { "featureSlot", slot }, { "packed", counts.packed }, { "single", counts.single },
				{ "failed", counts.failed }, { "packedPixels", counts.packedPixels }, { "singlePixels", counts.singlePixels } });
		}
		const auto overwritten = value.Total() > value.kCapacity ? value.Total() - value.kCapacity : 0;
		for (auto sequence = overwritten + 1u; sequence <= value.Total(); ++sequence)
			records.push_back(PackedCropTransitionJson(value.Records()[(sequence - 1u) % value.kCapacity]));
		return { { "schemaVersion", 1 }, { "enabled", value.Enabled() }, { "capacity", value.kCapacity },
			{ "totalRecords", value.Total() }, { "overwrittenRecords", overwritten }, { "rejections", value.Rejections() },
			{ "clock", "steady_clock_nanoseconds_not_UTC_or_SteamVR_Time" },
			{ "timingScope", "CPU durations for the matching slot/frame/source/generation; retirement includes drain and release; null means unmeasured" },
			{ "records", std::move(records) }, { "evaluations", std::move(evaluations) },
			{ "evaluationScope", "successful_native_calls_per_eye_not_GPU_coverage_or_frame_commit" },
			{ "firstRejection", value.FirstRejection() ? PackedCropTransitionJson(*value.FirstRejection()) : Json(nullptr) } };
	}
#endif

	/** Read-only diagnostic serialization; callers are compiled only into DevBench builds. */
	inline nlohmann::json PackedCropLayoutJson(const PackedCropLayout& layout)
	{
		using Json = nlohmann::json;
		const auto rect = [](const ComputeSubrect& value) {
			return Json{ { "x", value.baseX }, { "y", value.baseY }, { "width", value.width }, { "height", value.height } };
		};
		auto tiles = Json::array();
		for (std::uint32_t i = 0; i < layout.count && i < kMaximumPackedCrops; ++i)
			tiles.push_back({ { "source", rect(layout.tiles[i].source) }, { "model", rect(layout.tiles[i].model) }, { "atlas", rect(layout.tiles[i].packed) } });
		return { { "active", layout.count != 0 }, { "cropCount", layout.count },
			{ "width", layout.extent.width }, { "height", layout.extent.height },
			{ "unpackedWidth", layout.unpackedExtent.width }, { "unpackedHeight", layout.unpackedExtent.height },
			{ "tiles", std::move(tiles) } };
	}
}
