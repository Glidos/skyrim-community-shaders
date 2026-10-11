#include "Features/Upscaling/NeuralRendering/NativeEvaluationLayout.h"
#include "Features/Upscaling/NeuralRendering/PrewarmPolicy.h"
#include "Features/Upscaling/NeuralRendering/ShaderInvalidation.h"

#include <atomic>
#include <bit>
#include <chrono>
#include <format>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

namespace NeuralRendering
{
	struct ID3D12GraphicsCommandList
	{};
	struct ID3D12Resource
	{};
	using HMODULE = void*;
	constexpr std::int32_t E_INVALIDARG = -1, E_PENDING = -2;
	constexpr unsigned kFeatureDlssNr = 18;
	constexpr bool NVSDK_NGX_FAILED(int result) { return result != 0; }
	struct NVSDK_NGX_Handle
	{};
	struct NVSDK_NGX_Parameter
	{
		std::map<std::string, std::uint64_t> values;
		void Reset() { values.clear(); }
		void Set(const char* key, std::uint32_t value) { values[key] = value; }
		void Set(const char* key, float value) { values[key] = std::bit_cast<std::uint32_t>(value); }
		void Set(const char* key, ID3D12Resource* value) { values[key] = reinterpret_cast<std::uintptr_t>(value); }
	};
	struct RuntimeCallerPathCall
	{
		static inline bool installed = true;
		explicit RuntimeCallerPathCall(HMODULE) {}
		bool IsInstalled() const { return installed; }
		unsigned Hits() const { return 1; }
	};
	struct D3D12Interop
	{
		void BeginEvaluationTiming(ID3D12GraphicsCommandList*, unsigned) {}
		void EndEvaluationTiming(ID3D12GraphicsCommandList*, unsigned) {}
	};
	enum class RuntimeStatus
	{
		Initialized,
		FeatureConfigurationChanged,
		FeatureCreateFailed,
		FeatureEvaluateFailed,
		MissingExport
	};
	enum class RuntimeFailureStage
	{
		None,
		FeatureConfiguration,
		FeatureCreate,
		FeatureEvaluate,
		Load
	};
	const char* ToString(int) { return "test"; }
	auto DiagnosticNow() { return std::chrono::steady_clock::now(); }
	struct RuntimeExecutionEvidence
	{
		bool createAttempted = false, createSucceeded = false, evaluateAttempted = false, evaluateSucceeded = false;
		const char* createReason = nullptr;
		std::optional<std::uint32_t> createResult, evaluateResult;
		std::optional<std::uint64_t> createCpuMicroseconds, evaluateCpuMicroseconds;
	};
	struct Tuning
	{
		float intensity = .8f, localToneStrength = .75f, localStructureStrength = .9f, skinStructureStrength = .9f;
		std::uint32_t style = 3;
		bool useAutoMask = true, uiCorrection = false;
		float singleSubrectScale = 1;
	};
	using CreateFeature = int (*)(ID3D12GraphicsCommandList*, unsigned, NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
	using EvaluateFeature = int (*)(ID3D12GraphicsCommandList*, NVSDK_NGX_Handle*, NVSDK_NGX_Parameter*, void*);
	std::vector<std::map<std::string, std::uint64_t>> creates, evaluations;
	int providerCreateResult = 0, providerEvaluateResult = 0;
	bool returnHandle = true;
	NVSDK_NGX_Handle providerHandle;
	int Create(ID3D12GraphicsCommandList*, unsigned feature, NVSDK_NGX_Parameter* parameters, NVSDK_NGX_Handle** output)
	{
		if (feature != kFeatureDlssNr)
			throw std::runtime_error("wrong feature");
		creates.push_back(parameters->values);
		*output = returnHandle ? &providerHandle : nullptr;
		return providerCreateResult;
	}
	int Evaluate(ID3D12GraphicsCommandList*, NVSDK_NGX_Handle*, NVSDK_NGX_Parameter* parameters, void*)
	{
		evaluations.push_back(parameters->values);
		return providerEvaluateResult;
	}
	struct Runtime
	{
		static constexpr unsigned kFeatureSlotCount = 4;
		struct FeatureConfiguration
		{
			UpscalingDLSS::Extent colorBacking{};
			NativeCreationExtents creation{};
			bool featureUpscaling = false, providerBlending = false, valid = false;
			bool Matches(const NativeEvaluationLayout&) const;
		};
		std::mutex mutex_;
		std::atomic<bool> abandonRequested_ = false;
		bool abandoned_ = false;
		RuntimeStatus status_ = RuntimeStatus::Initialized;
		RuntimeFailureStage failureStage_ = RuntimeFailureStage::None;
		std::array<void*, kFeatureSlotCount> featureHandles_{};
		std::array<FeatureConfiguration, kFeatureSlotCount> featureConfigurations_{};
		std::atomic<std::uint32_t> firstEvaluationResetMask_{ 0 };
		struct Exports
		{
			void* createFeature = reinterpret_cast<void*>(&Create);
			void* evaluateFeature = reinterpret_cast<void*>(&Evaluate);
		} runtimeExports_;
		NVSDK_NGX_Parameter parameterStorage;
		void* parameters_ = &parameterStorage;
		void* module_ = nullptr;
		bool lastPathProxyInstalled_ = false, featureCreateLogEmitted_ = false, featureEvaluateLogEmitted_ = false;
		unsigned lastPathProxyHits_ = 0, ngxResult_ = 0;
		std::uint64_t successfulFrames_ = 0;
		int trust_ = 0;
		std::string detail_;
		void SetFailureLocked(RuntimeStatus status, RuntimeFailureStage stage, std::string detail, unsigned result = 0)
		{
			status_ = status;
			failureStage_ = stage;
			detail_ = std::move(detail);
			ngxResult_ = result;
		}
		void LogOnceLocked(bool& emitted, const char*, bool) { emitted = true; }
		bool PrepareFeature(ID3D12GraphicsCommandList*, unsigned, const NativeEvaluationLayout&, ID3D12Resource* = nullptr, ID3D12Resource* = nullptr, bool = false, RuntimeExecutionEvidence* = nullptr);
		bool PrepareFeatureLocked(ID3D12GraphicsCommandList*, unsigned, const NativeEvaluationLayout&, ID3D12Resource*, ID3D12Resource*, bool, RuntimeExecutionEvidence*);
		void AcknowledgePresentedFeatures(unsigned);
		bool Execute(ID3D12GraphicsCommandList*, unsigned, ID3D12Resource*, ID3D12Resource*, ID3D12Resource*, ID3D12Resource*, ID3D12Resource*, const NativeEvaluationLayout&, const Tuning&, bool, bool* = nullptr, RuntimeExecutionEvidence* = nullptr, D3D12Interop* = nullptr, unsigned = 0, bool = false);
	};
#include "neural_native_creation_under_test.h"
#include "neural_native_key_under_test.h"
}

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	void NativeCreation()
	{
		using namespace NeuralRendering;
		const UpscalingDLSS::Extent extent{ 320, 192 };
		const auto layout = BuildNativeEvaluationLayout(extent, extent, extent, {}, { 0, 0, 320, 192 }, { true, 320.f, 192.f }, false);
		ID3D12GraphicsCommandList commands;
		ID3D12Resource resource;
		Tuning tuning;
		const auto run = [&](Runtime& runtime, unsigned slot, const NativeEvaluationLayout& demand, ID3D12Resource* mask = nullptr, bool provider = false) {
			return runtime.Execute(&commands, slot, &resource, &resource, &resource, &resource, mask, demand, tuning, false, nullptr, nullptr, nullptr, 0, provider);
		};
		Runtime lazy, warm;
		Require(run(lazy, 2, layout), "lazy creation failed");
		const auto lazyParameters = creates.back();
		const auto evaluationsBefore = evaluations.size();
		Require(warm.PrepareFeature(&commands, 2, layout), "prewarm failed");
		Require(creates.back() == lazyParameters, "lazy/prewarm creation parameter mismatch");
		Require(lazyParameters.at("Width") == 320 && lazyParameters.at("DLSSNR.InputHeight") == 192 && lazyParameters.at("DLSSNR.Upscaling") == 0, "creation contract changed");
		Require(warm.successfulFrames_ == 0 && evaluations.size() == evaluationsBefore, "prewarm evaluated a fabricated frame");
		const auto created = creates.size();
		Require(warm.PrepareFeature(&commands, 2, layout) && run(warm, 2, layout), "first use failed");
		Require(creates.size() == created && evaluations.back().at("DLSSNR.Reset") == 1, "first use recreated or lost reset");
		Require(run(warm, 2, layout) && evaluations.back().at("DLSSNR.Reset") == 1, "unpresented transaction consumed reset");
		warm.AcknowledgePresentedFeatures(1u << 3);
		Require((warm.firstEvaluationResetMask_.load() & 4u), "peer-only acknowledgement consumed reset");
		warm.AcknowledgePresentedFeatures(1u << 2);
		tuning.intensity = 1.7f;
		Require(run(warm, 2, layout) && evaluations.back().at("DLSSNR.Reset") == 0 && creates.size() == created, "tuning recreated or repeated first-use reset");
		Require(evaluations.back().at("DLSSNR.Intensity") == std::bit_cast<std::uint32_t>(1.7f), "cached feature lost live tuning");
		auto changed = BuildNativeEvaluationLayout({ 640, 192 }, { 640, 192 }, { 640, 192 }, {}, { 0, 0, 640, 192 }, { true, 640.f, 192.f }, false);
		Require(!warm.PrepareFeature(&commands, 2, changed) && creates.size() == created && warm.featureHandles_[2], "live incompatible feature was replaced");
		Runtime failed;
		providerCreateResult = 1;
		Require(!failed.PrepareFeature(&commands, 2, layout) && failed.featureHandles_[2] && !failed.featureConfigurations_[2].valid, "failed non-null handle was lost or advertised reusable");
		providerCreateResult = 0;
		Runtime missing;
		RuntimeCallerPathCall::installed = false;
		const auto beforeProxy = creates.size();
		Require(!missing.PrepareFeature(&commands, 2, layout) && creates.size() == beforeProxy, "missing proxy called provider");
		RuntimeCallerPathCall::installed = true;
		Runtime blendLazy, blendWarm, noBindings;
		auto blendLayout = layout;
		blendLayout.controlMask = layout.output;
		Require(!noBindings.PrepareFeature(&commands, 2, blendLayout, nullptr, nullptr, true), "provider prewarm accepted missing resources");
		Require(run(blendLazy, 2, blendLayout, &resource, true), "provider lazy creation failed");
		const auto blendedParameters = creates.back();
		Require(blendWarm.PrepareFeature(&commands, 2, blendLayout, &resource, &resource, true) && creates.back() == blendedParameters, "provider binding parameters differ");
		Runtime failedPair;
		Require(failedPair.PrepareFeature(&commands, 2, layout) && failedPair.PrepareFeature(&commands, 3, layout), "pair preparation failed");
		Require(run(failedPair, 2, layout), "left evaluation failed");
		providerEvaluateResult = 1;
		Require(!run(failedPair, 3, layout) && (failedPair.firstEvaluationResetMask_.load() & 12u) == 12u, "right failure consumed pair reset");
		providerEvaluateResult = 0;
	}
	void InvalidationAndLease()
	{
		using namespace NeuralRendering;
		ShaderInvalidation invalidation;
		invalidation.Request();
		const auto captured = invalidation.Requested();
		invalidation.Request();
		invalidation.Applied(captured);
		Require(invalidation.Pending(), "request arriving during apply was lost");
		std::vector<std::thread> threads;
		for (unsigned index = 0; index < 4; ++index)
			threads.emplace_back([&] { for (unsigned count = 0; count < 10000; ++count) invalidation.Request(); });
		for (auto& thread : threads)
			thread.join();
		Require(invalidation.Requested() == 40002, "concurrent invalidation was lost");
		invalidation.Applied(invalidation.Requested());
		Require(!invalidation.Pending(), "completed invalidations repeated");
		PrewarmPolicy policy;
		PrewarmKey key;
		key.slotMask = 12;
		policy.Capture(key, 100);
		Require(policy.BeginOpportunity(8) && !policy.BeginOpportunity(8) && policy.BeginOpportunity(9), "duplicate compositor work was admitted");
		Require(!policy.Expired(30099) && policy.Expired(30100), "unused lease did not expire independently of world frames");
		policy.backendOwned = true;
		policy.Adopt(0, false);
		Require(!policy.backendOwned && !policy.adoptedMask && !policy.missedMask, "gameplay failed to assume a backend without prepared models");
		policy.preparedMask = policy.unusedMask = 12;
		policy.Adopt(2, true);
		Require(policy.unusedMask == 8 && policy.adoptedMask == 4, "partial adoption lost unused peer");
		policy.Adopt(3, false);
		Require(policy.unusedMask == 0 && policy.adoptedMask == 4 && policy.missedMask == 8, "authoritative mismatch advertised reuse");
		policy.Capture(key, 100);
		Require(policy.SuppressRetryAfterRetirement(30100), "expired request restarted optional work");
		policy.backendOwned = policy.failed = true;
		policy.unusedMask = 4;
		policy.CancelRequest();
		Require(policy.cancelled && policy.failed && policy.backendOwned && policy.unusedMask == 4, "cancellation lost unsafe ownership");
		Require(!policy.SuppressRetryAfterRetirement(30100), "explicit cancellation left request permanently suppressed");
		policy = {};
		policy.Capture(key, 30200);
		Require(!policy.failed && !policy.Expired(30200), "new explicit request inherited the retired failure");
	}
}
int main()
{
	try {
		NativeCreation();
		InvalidationAndLease();
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
	return 0;
}
