#pragma once

#include "ModelResolutionPolicy.h"
#include "Renderer.h"

#include <array>
#include <limits>
#include <memory>
#include <span>

namespace NeuralRendering
{
	/** Model-grid preparation with private reconstruction retained across native resets. */
	class ModelResolution
	{
		struct Work;
		struct CoverageWork;

	public:
		/** Retained views of admitted renderer-owned model textures. */
		struct Targets
		{
			Color::Texture color, depth, motion, output;
		};

		struct Batch
		{
			std::array<RendererApplyArgs, kMaximumRegionEvaluations> arguments{};
			std::array<std::shared_ptr<Work>, kMaximumRegionEvaluations> resources{};
			std::array<Targets, kMaximumRegionEvaluations> targets{};
			std::array<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>, kMaximumRegionEvaluations> selectedOutputs{};
			std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, kMaximumRegionEvaluations> selectionMasks{};
			std::array<ComputeSubrect, kMaximumRegionEvaluations> selectionSupports{};
			std::array<bool, kMaximumRegionEvaluations> providerSelections{};
			std::array<Microsoft::WRL::ComPtr<ID3D11Resource>, kMaximumRegionEvaluations> selectionSources{}, selectionDestinations{};
			Microsoft::WRL::ComPtr<ID3D11Device> device;
			Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
			Microsoft::WRL::ComPtr<ID3D11ComputeShader> reconstructionShader;
			Microsoft::WRL::ComPtr<ID3D11Buffer> constants;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> packedCoverage;
			std::size_t count = 0;
		};

		/** One reconstruction boundary handles either independent model scale or area restriction. */
		static bool Required(const RendererApplyArgs& args
#ifdef DEVBENCH_BRIDGE_ENABLED
			,
			PackedCropAdmissionDiagnostics* diagnostics = nullptr
#endif
			) noexcept;
		/** Project validated source geometry without allocating or dispatching. */
		static bool Project(const RendererApplyArgs&, RendererApplyArgs&
#ifdef DEVBENCH_BRIDGE_ENABLED
			,
			PackedCropAdmissionDiagnostics* diagnostics = nullptr
#endif
			) noexcept;
		/** Raw shared preparation retains private actor selection and native protection semantics. */
		static bool CanUseSharedTargets(std::span<const RendererApplyArgs>, bool colorProcessing, bool compactInputs) noexcept;
		/** Count additional private textures before memory admission; shared aliases are excluded. */
		std::optional<std::uint64_t> AdditionalBytes(std::span<const RendererApplyArgs>, bool sharedTargets = false) const;
		[[nodiscard]] std::optional<std::uint64_t> RetainedBytes(std::uint32_t) const noexcept;

		/** Prepare both eyes into validated shared targets or private proxies before native evaluation. */
		bool Prepare(std::span<const RendererApplyArgs>, Batch&, HRESULT&, std::span<const Targets> = {});
		/** Reconstruct the complete batch into retained private or explicitly unpublished output targets. */
		bool Reconstruct(std::span<const RendererApplyArgs>, const Batch&, HRESULT&);
		/** Commit only the original owned rectangles after successful reconstruction. */
		void Commit(std::span<const RendererApplyArgs>, const Batch&) const;
		/** Retain textures, constants and outstanding batches while rearming shader compilation. */
		void InvalidateShaders() noexcept;
		/** Release caches; outstanding batch ownership remains valid. */
		void Reset() noexcept;
		void ReleaseSlot(std::uint32_t) noexcept;
		/** Drop proxy caches only when the whole source transaction no longer needs them. */
		void ReleaseUnscaledSlots(std::span<const RendererApplyArgs>) noexcept;
#ifdef DEVBENCH_BRIDGE_ENABLED
		/** Explicit inspection samples the last coverage guard without waits or gameplay polling. */
		nlohmann::json GetPackedCoverageDiagnostics();
#endif

	private:
		bool EnsureShaders(ID3D11Device*, HRESULT&);
		bool EnsureCoverage(ID3D11Device*, HRESULT&);
		bool EnsureSelectionShader(bool guarded, HRESULT&);
		void DispatchCoverage(std::span<const RendererApplyArgs>, Batch&);
		template <std::size_t Bindings>
		void DispatchReconstruction(std::span<const RendererApplyArgs>, const Batch&);
#ifdef DEVBENCH_BRIDGE_ENABLED
		void ServiceCoverageInspection(ID3D11DeviceContext*);
#endif
		std::shared_ptr<CoverageWork> coverage_;
		Microsoft::WRL::ComPtr<ID3D11ComputeShader> coverageShader_, guardedReconstruct_;
		std::uint32_t lastCoverageFrame_ = std::numeric_limits<std::uint32_t>::max();
		bool lastBatchGuarded_ = false;
		std::array<std::shared_ptr<Work>, kLogicalFeatureSlotCount> slots_{};
		Microsoft::WRL::ComPtr<ID3D11Device> device_;
		Microsoft::WRL::ComPtr<ID3D11ComputeShader> prepare_, reconstruct_, selection_, guardedSelection_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
		bool compileFailed_ = false;
	};
}
