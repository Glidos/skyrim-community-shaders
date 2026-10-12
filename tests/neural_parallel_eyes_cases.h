// Included inside the existing WARP fixture namespace to exercise production interop methods.

// Forward real GPU work while failing one synchronization call at the production boundary.
class FaultQueue final : public ID3D12CommandQueue
{
	ULONG references_ = 1;
	ComPtr<ID3D12CommandQueue> queue_;

public:
	bool failSignal = false, failWait = false, failFrequency = false;
	unsigned frequencyCalls = 0;
	explicit FaultQueue(ID3D12CommandQueue* queue) : queue_(queue) {}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
	{
		if (!object)
			return E_POINTER;
		*object = nullptr;
		if (iid != __uuidof(IUnknown) && iid != __uuidof(ID3D12Object) && iid != __uuidof(ID3D12DeviceChild) &&
			iid != __uuidof(ID3D12Pageable) && iid != __uuidof(ID3D12CommandQueue))
			return E_NOINTERFACE;
		*object = static_cast<ID3D12CommandQueue*>(this);
		AddRef();
		return S_OK;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
	ULONG STDMETHODCALLTYPE Release() override
	{
		const auto count = --references_;
		if (!count)
			delete this;
		return count;
	}
	HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID id, UINT* size, void* data) override { return queue_->GetPrivateData(id, size, data); }
	HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID id, UINT size, const void* data) override { return queue_->SetPrivateData(id, size, data); }
	HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID id, const IUnknown* data) override { return queue_->SetPrivateDataInterface(id, data); }
	HRESULT STDMETHODCALLTYPE SetName(LPCWSTR name) override { return queue_->SetName(name); }
	HRESULT STDMETHODCALLTYPE GetDevice(REFIID id, void** device) override { return queue_->GetDevice(id, device); }
	void STDMETHODCALLTYPE UpdateTileMappings(ID3D12Resource* resource, UINT regions, const D3D12_TILED_RESOURCE_COORDINATE* coordinates,
		const D3D12_TILE_REGION_SIZE* sizes, ID3D12Heap* heap, UINT ranges, const D3D12_TILE_RANGE_FLAGS* flags,
		const UINT* offsets, const UINT* counts, D3D12_TILE_MAPPING_FLAGS mapping) override
	{
		queue_->UpdateTileMappings(resource, regions, coordinates, sizes, heap, ranges, flags, offsets, counts, mapping);
	}
	void STDMETHODCALLTYPE CopyTileMappings(ID3D12Resource* destination, const D3D12_TILED_RESOURCE_COORDINATE* destinationStart,
		ID3D12Resource* source, const D3D12_TILED_RESOURCE_COORDINATE* sourceStart, const D3D12_TILE_REGION_SIZE* size, D3D12_TILE_MAPPING_FLAGS flags) override
	{
		queue_->CopyTileMappings(destination, destinationStart, source, sourceStart, size, flags);
	}
	void STDMETHODCALLTYPE ExecuteCommandLists(UINT count, ID3D12CommandList* const* lists) override { queue_->ExecuteCommandLists(count, lists); }
	void STDMETHODCALLTYPE SetMarker(UINT metadata, const void* data, UINT size) override { queue_->SetMarker(metadata, data, size); }
	void STDMETHODCALLTYPE BeginEvent(UINT metadata, const void* data, UINT size) override { queue_->BeginEvent(metadata, data, size); }
	void STDMETHODCALLTYPE EndEvent() override { queue_->EndEvent(); }
	HRESULT STDMETHODCALLTYPE Signal(ID3D12Fence* fence, UINT64 value) override { return std::exchange(failSignal, false) ? E_FAIL : queue_->Signal(fence, value); }
	HRESULT STDMETHODCALLTYPE Wait(ID3D12Fence* fence, UINT64 value) override { return std::exchange(failWait, false) ? E_FAIL : queue_->Wait(fence, value); }
	HRESULT STDMETHODCALLTYPE GetTimestampFrequency(UINT64* frequency) override
	{
		++frequencyCalls;
		return failFrequency ? E_FAIL : queue_->GetTimestampFrequency(frequency);
	}
	HRESULT STDMETHODCALLTYPE GetClockCalibration(UINT64* gpu, UINT64* cpu) override { return queue_->GetClockCalibration(gpu, cpu); }
	D3D12_COMMAND_QUEUE_DESC STDMETHODCALLTYPE GetDesc() override { return queue_->GetDesc(); }
};

void ParallelSynchronizationFailures()
{
	for (const bool failPrimary : { false, true })
		for (const bool failSignal : { false, true }) {
			Fixture f;
			ID3D12GraphicsCommandList* main = nullptr;
			Require(f.interop.BeginD3D12(&main), "fault primary begin failed");
			auto* parallel = f.interop.ParallelList();
			Require(parallel && f.BeginFeatureScope(main, Fixture::Timing()), "fault parallel scope failed");
			f.Evaluate(main, 0);
			f.Evaluate(parallel, 1);
			Require(f.EndFeatureScope(main), "fault timing scope failed");
			auto& queue = failPrimary ? f.interop.queue12_ : f.interop.parallelQueue_;
			auto* fault = new FaultQueue(queue.Get());
			queue.Attach(fault);
			fault->failSignal = failSignal;
			fault->failWait = !failSignal;
			Require(!f.interop.EndD3D12(), "queue synchronization failure was ignored");
			Require(f.interop.IsParallelDisabled() && f.interop.unfencedSubmission_ && !f.interop.GetLifetimeSnapshot().completedKnown,
				"partial synchronization failure claimed retirement or lost its serial latch");
			Require(f.interop.WaitForIdle(), "safe drain could not join work after a failed synchronization call");
			Require(f.interop.GetLifetimeSnapshot().completedKnown && f.interop.IsParallelDisabled(),
				"safe drain cleared the session fallback or lost completion proof");
		}
}

void ParallelTimingFailure()
{
	Fixture f;
	globals::source.epoch = 1;
	FaultQueue* fault = nullptr;
	for (std::uint32_t frame = 0; frame < 2; ++frame) {
		ID3D12GraphicsCommandList* main = nullptr;
		Require(f.interop.BeginD3D12(&main), "optional parallel timing primary begin failed");
		auto* parallel = f.interop.ParallelList();
		if (!fault) {
			fault = new FaultQueue(f.interop.parallelQueue_.Get());
			f.interop.parallelQueue_.Attach(fault);
			fault->failFrequency = true;
		}
		Require(parallel && f.BeginFeatureScope(main, Fixture::Timing()), "optional parallel timing disabled rendering");
		f.Evaluate(main, 0);
		f.Evaluate(parallel, 1);
		Require(f.EndFeatureScope(main) && f.interop.EndD3D12() && f.interop.WaitForIdle(),
			"unavailable profiling blocked a valid parallel submission");
	}
	Require(!f.interop.IsParallelDisabled() && fault->frequencyCalls == 1 && globals::source.samples.empty(),
		"profiling failure disabled parallel rendering, retried per frame or fabricated samples");
}

void ParallelSubmissionAndTiming()
{
	for (const auto epoch : { 0u, 1u }) {
		Fixture f;
		globals::source.epoch = epoch;
		const auto buffer = [&](D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state) {
			D3D12_HEAP_PROPERTIES heap{};
			heap.Type = type;
			D3D12_RESOURCE_DESC desc{};
			desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Width = 256;
			desc.Height = desc.SampleDesc.Count = 1;
			desc.DepthOrArraySize = desc.MipLevels = 1;
			desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			ComPtr<ID3D12Resource> result;
			Check(f.interop.device12_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&result)));
			return result;
		};
		std::array<ComPtr<ID3D12Resource>, 2> inputs, outputs;
		for (std::size_t eye = 0; eye < 2; ++eye) {
			inputs[eye] = buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
			outputs[eye] = buffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
		}
		for (std::uint32_t frame = 0; frame < 7; ++frame) {
			ID3D12GraphicsCommandList* main = nullptr;
			Require(f.interop.BeginD3D12(&main), "parallel primary begin failed");
			auto* parallel = f.interop.ParallelList();
			Require(parallel && parallel != main && parallel->GetType() == D3D12_COMMAND_LIST_TYPE_COMPUTE,
				"second eye did not acquire an independent compute list");
			Require(f.interop.ParallelList() == parallel, "parallel acquisition reset a recording list");
			auto timing = Fixture::Timing();
			timing.frameId = frame;
			Require(f.BeginFeatureScope(main, timing), "parallel timing begin failed");
			const std::array<ID3D12GraphicsCommandList*, 2> lists{ main, parallel };
			for (std::uint32_t eye = 0; eye < 2; ++eye) {
				void* mapped = nullptr;
				const D3D12_RANGE noRead{ 0, 0 };
				Check(inputs[eye]->Map(0, &noRead, &mapped));
				*static_cast<std::uint32_t*>(mapped) = frame * 2 + eye + 1;
				const D3D12_RANGE written{ 0, sizeof(std::uint32_t) };
				inputs[eye]->Unmap(0, &written);
				f.interop.BeginEvaluationTiming(lists[eye], eye);
				lists[eye]->CopyBufferRegion(outputs[eye].Get(), 0, inputs[eye].Get(), 0, sizeof(std::uint32_t));
				f.interop.EndEvaluationTiming(lists[eye], eye);
			}
			Require(f.EndFeatureScope(main), "parallel timing end failed");
			Require(f.interop.EndD3D12(), "parallel submission failed");
			Require(f.interop.WaitForIdle(), "parallel join did not retire both queues");
			for (std::uint32_t eye = 0; eye < 2; ++eye) {
				void* mapped = nullptr;
				const D3D12_RANGE read{ 0, sizeof(std::uint32_t) }, noWrite{ 0, 0 };
				Check(outputs[eye]->Map(0, &read, &mapped));
				const auto actual = *static_cast<std::uint32_t*>(mapped);
				outputs[eye]->Unmap(0, &noWrite);
				Require(actual == frame * 2 + eye + 1, "joined completion exposed stale output from an eye");
			}
			const auto lifetime = f.interop.GetLifetimeSnapshot();
			Require(lifetime.completedKnown && lifetime.parallelCompleted >= lifetime.parallelIssued,
				"joined completion omitted the compute queue");
		}
		Require(globals::source.samples.size() == 7 * epoch, "parallel ring reuse lost or fabricated timing samples");
#ifdef DEVBENCH_BRIDGE_ENABLED
		const auto telemetry = f.interop.GetTelemetry();
		Require(telemetry.parallelCommandSubmissions == 7 && telemetry.featureGpuSamples == 7 * epoch,
			"lightweight scheduling telemetry lost submissions or fabricated timings");
		Require(telemetry.lastFeatureParallel == (epoch != 0) &&
					telemetry.lastEvaluationGpuMicroseconds[0].has_value() == (epoch != 0) &&
					telemetry.lastEvaluationGpuMicroseconds[1].has_value() == (epoch != 0),
			"per-eye timings were absent or recorded while profiling was disabled");
#endif
		if (!epoch) {
			Require(!f.interop.timestampReadback_ && !f.interop.timestampQueryHeap_ && !f.interop.parallelTimestampFrequency_,
				"disabled profiling allocated queries or sampled the compute frequency");
			for (const auto& context : f.interop.commandContexts_)
				Require(!context.timingBegin.commandList && !context.timingEnd.commandList, "disabled profiling allocated timestamp command lists");
		}
		f.interop.DisableParallel("test fallback");
		ID3D12GraphicsCommandList* main = nullptr;
		Require(f.interop.BeginD3D12(&main) && !f.interop.ParallelList(), "session fallback still uses parallel queues");
		f.RecordSubmission(main, 8);
		Require(f.interop.Shutdown(), "fallback backend shutdown failed");
		Require(f.interop.parallelDisabled_, "backend retirement cleared the session fallback");
	}
}

void ParallelJoinedInterval()
{
	Fixture f;
	globals::source.epoch = 1;
	ID3D12GraphicsCommandList* main = nullptr;
	Require(f.interop.BeginD3D12(&main), "interval primary begin failed");
	auto* parallel = f.interop.ParallelList();
	auto timing = Fixture::Timing();
	timing.execution = std::make_shared<ExecutionEvidence>(ExecutionDescriptor{});
	Require(parallel && f.BeginFeatureScope(main, timing), "parallel interval scope failed");
	f.Evaluate(main, 0);
	f.Evaluate(parallel, 1);
	Require(f.EndFeatureScope(main) && f.interop.EndD3D12(), "parallel interval submission failed");
	// Completed synthetic ticks distinguish overlap from summed work and exercise different queue frequencies.
	f.interop.context11_->Flush();
	auto& context = f.interop.commandContexts_[0];
	Require(f.interop.WaitForFenceLocked(context.fenceValue, 5000, "parallel interval"), "parallel interval did not retire");
	void* mapped = nullptr;
	const D3D12_RANGE noRead{ 0, 0 };
	Check(f.interop.timestampReadback_->Map(0, &noRead, &mapped));
	auto* ticks = static_cast<std::uint64_t*>(mapped);
	ticks[0] = 1000;
	ticks[1] = 5000;
	ticks[2] = 1100;
	ticks[3] = 4100;
	ticks[4] = 2200;
	ticks[5] = 8200;
	const D3D12_RANGE written{ 0, 6 * sizeof(std::uint64_t) };
	f.interop.timestampReadback_->Unmap(0, &written);
	f.interop.timestampFrequency_ = 1000000;
	f.interop.parallelTimestampFrequency_ = 2000000;
	f.interop.CollectCompletedTimingsLocked(context.fenceValue);
	Require(globals::source.samples.size() == 1 && std::abs(globals::source.samples[0].milliseconds - 4.0f) < .0001f,
		"parallel inference summed overlapping eye durations instead of measuring the joined interval");
	const auto evidence = timing.execution->Snapshot();
#ifdef DEVBENCH_BRIDGE_ENABLED
	const auto telemetry = f.interop.GetTelemetry();
	Require(telemetry.lastFeatureParallel && telemetry.lastFeatureFrameId == timing.frameId &&
				telemetry.lastFeatureGpuMicroseconds == 4000 && telemetry.lastEvaluationGpuMicroseconds[0] == 3000 &&
				telemetry.lastEvaluationGpuMicroseconds[1] == 3000,
		"lightweight timing telemetry mixed frames, queue clocks or overlapping work");
#endif
	Require(evidence.parallelEyes && evidence.batchGpu.microseconds == 4000 &&
				evidence.regions[0].evaluationGpu.microseconds == 3000 && evidence.regions[1].evaluationGpu.microseconds == 3000,
		"capture lost parallel execution identity or used the wrong queue frequency");
}

void ParallelPartialSubmissionRetirement()
{
	Fixture f;
	ID3D12GraphicsCommandList* main = nullptr;
	Require(f.interop.BeginD3D12(&main), "partial primary begin failed");
	auto* parallel = f.interop.ParallelList();
	Require(parallel != nullptr, "partial compute begin failed");
	ComPtr<ID3D12Fence> gate;
	Check(f.interop.device12_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)));
	struct ReleaseGate
	{
		ID3D12Fence* value;
		~ReleaseGate() { (void)value->Signal(1); }
	} release{ gate.Get() };
	Check(f.interop.parallelQueue_->Wait(gate.Get(), 1));
	Require(f.interop.CloseRecordingLocked(f.interop.commandContexts_[0]), "partial lists did not close");
	f.interop.recording_ = false;
	f.interop.unfencedSubmission_ = true;
	ID3D12CommandList* lists[]{ parallel };
	f.interop.parallelQueue_->ExecuteCommandLists(1, lists);
	// Model failure after Execute but before a completion signal or primary-queue join.
	++f.interop.parallelFenceValue_;
	Check(f.interop.queue12_->Signal(f.interop.fence12_.Get(), ++f.interop.fenceValue_));
	Require(!f.interop.GetLifetimeSnapshot().completedKnown, "unfenced compute work was reported safe");
	Require(!f.interop.WaitForIdle(), "primary progress incorrectly proved stalled compute retirement");
	Require(f.interop.unfencedSubmission_ && f.interop.parallelDisabled_, "unsafe drain lost ownership or its fallback latch");
	Check(gate->Signal(1));
	Require(f.interop.WaitForIdle(), "completed compute work could not be safely drained");
	Require(f.interop.GetLifetimeSnapshot().completedKnown && !f.interop.unfencedSubmission_, "safe drain did not restore completion evidence");
}

void ParallelAbortAndCaptureCancellation()
{
	Fixture f;
	for (const bool abort : { false, true }) {
		globals::source.epoch = 1;
		ID3D12GraphicsCommandList* main = nullptr;
		Require(f.interop.BeginD3D12(&main), "cancel primary begin failed");
		auto* parallel = f.interop.ParallelList();
		Require(parallel && f.BeginFeatureScope(main, Fixture::Timing()), "cancel parallel begin failed");
		f.Evaluate(main, 0);
		f.interop.BeginEvaluationTiming(parallel, 1);
		globals::source.epoch = 0;
		f.interop.EndEvaluationTiming(parallel, 1);
		if (abort)
			Require(f.interop.AbortD3D12(), "parallel abort failed");
		else
			Require(f.EndFeatureScope(main) && f.interop.EndD3D12(), "capture cancellation blocked parallel submission");
		Require(f.interop.WaitForIdle(), "cancelled parallel work could not drain");
		Require(globals::source.samples.empty(), "cancelled parallel capture published partial timestamps");
	}
}
