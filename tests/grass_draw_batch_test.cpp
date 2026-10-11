#include "Features/GrassOptimizations/GrassFrustum.h"
#include "Features/GrassOptimizations/GrassPolicy.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>
int main()
{
	try {
		const auto require = [](bool condition) {if(!condition)throw std::runtime_error("Grass settings policy failed"); };
		GrassFrustum::Matrix identity{};
		for (size_t i = 0; i < 4; ++i) identity[i * 5] = 1;
		const auto planes = GrassFrustum::Extract(identity);
		require(planes.has_value());
		require(GrassFrustum::Visible(*planes, { 0, 0, .1f }, { .1f, .1f, .9f }));
		require(GrassFrustum::Visible(*planes, { 128, 0, 0 }, { 129, 1, 1 }));
		require(!GrassFrustum::Visible(*planes, { 130, 0, 0 }, { 132, 1, 1 }));
		require(GrassFrustum::Visible(*planes, { NAN, 0, 0 }, { 1, 1, 1 }));
		require(!GrassFrustum::Extract({}).has_value());
		auto stale = identity;
		stale[3] = 5;
		require(GrassFrustum::SelectProjection(identity, identity, stale, identity) == identity);
		auto jittered = identity;
		jittered[3] = .001f;
		require(GrassFrustum::SelectProjection(jittered, identity, jittered, identity) == identity);
		const auto dense = GrassPolicy::PlanCullCapacity({}, 32769, 1);
		require(dense && *dense == GrassPolicy::CullCapacity{ 65536, 1 });
		const auto stereoLOD = GrassPolicy::PlanCullCapacity(*dense, 64, 6);
		require(stereoLOD && *stereoLOD == GrassPolicy::CullCapacity{ 65536, 6 });
		require(GrassPolicy::PlanCullCapacity(*stereoLOD, 32769, 1) == stereoLOD);
		require(!GrassPolicy::PlanCullCapacity({}, 0, 1));
		require(!GrassPolicy::PlanCullCapacity({}, 1, 0));
		require(!GrassPolicy::PlanCullCapacity({}, UINT64_MAX, 1));
		require(!GrassPolicy::PlanCullCapacity({}, 1, 7));
		require(!GrassPolicy::PlanCullCapacity({ UINT32_MAX, 1 }, 1, 1));
		GrassPolicy::FrameQueue<int> queue;
		std::array<int, 16> shapes{};
		for (uint32_t frame : { UINT32_MAX - 1, UINT32_MAX, 0u, 1u }) {
			std::atomic_uint winners{ 0 };
			std::vector<std::thread> workers;
			for (auto& shape : shapes)
				workers.emplace_back([&queue, &winners, frame, identity = &shape]() {
					if (queue.Claim(frame, identity))
						++winners;
				});
			for (auto& worker : workers) worker.join();
			require(winners == 1 && queue.queuedFrame == frame);
			require(queue.representative >= shapes.data() && queue.representative < shapes.data() + shapes.size());
			require(!queue.Claim(frame, &shapes.front()));
		}
		require(GrassFrustum::WithinDistance(identity, { 0, 0, 0 }, { 10, 10, 10 }, 100));
		require(GrassFrustum::WithinDistance(identity, { 227, 0, 0 }, { 229, 1, 1 }, 100));
		require(!GrassFrustum::WithinDistance(identity, { 230, 0, 0 }, { 240, 1, 1 }, 100));
		auto rotated = identity;
		rotated[0] = rotated[5] = 0;
		rotated[1] = -1;
		rotated[4] = 1;
		rotated[3] = -200;
		require(GrassFrustum::WithinDistance(rotated, { 0, -210, 0 }, { 10, -190, 10 }, 100));
		require(!GrassFrustum::WithinDistance(rotated, { 1000, 0, 0 }, { 1010, 10, 10 }, 100));
		require(GrassFrustum::WithinDistance(rotated, { NAN, 0, 0 }, { 1, 1, 1 }, 100));
		rotated[15] = 0;
		require(GrassFrustum::WithinDistance(rotated, { 1000, 0, 0 }, { 1010, 10, 10 }, 100));
		using Basis = std::array<std::array<float, 3>, 3>;
		const Basis unit{ std::array{ 1.f, 0.f, 0.f }, std::array{ 0.f, 1.f, 0.f }, std::array{ 0.f, 0.f, 1.f } };
		require(GrassPolicy::BasisNorm(unit) >= 1 && GrassPolicy::BasisNorm(unit) < 1.0001f);
		for (const auto& basis : { unit, Basis{ std::array{ 1.f, 2.f, -.5f }, std::array{ 0.f, .1f, 1.f }, std::array{ 3.f, 0.f, .1f } } }) {
			const float bound = GrassPolicy::BasisNorm(basis);
			for (int x = -4; x <= 4; ++x)
				for (int y = -4; y <= 4; ++y)
					for (int z = -4; z <= 4; ++z) {
						if (!x && !y && !z)
							continue;
						const float length = std::sqrt(float(x * x + y * y + z * z));
						float squared = 0;
						for (const auto& row : basis) {
							const float transformed = (row[0] * x + row[1] * y + row[2] * z) / length;
							squared += transformed * transformed;
						}
						require(std::sqrt(squared) <= bound);
					}
		}
		GrassPolicy::Settings settings;
		require(settings.Valid());
		require(settings.Enabled && settings.CrossCellBatching && settings.FrustumCulling &&
				settings.DensityReduction && settings.EnableOcclusionCulling && !settings.EnableMeshLOD);
		require(settings.CollisionDistance == 2048.0f);
		for (float distance : { 0.0f, GrassPolicy::kMaxCollisionDistance }) {
			settings.CollisionDistance = distance;
			require(settings.Valid());
		}
		settings.CollisionDistance = GrassPolicy::kMaxCollisionDistance + 1;
		require(!settings.Valid());
		settings = {};
		require(GrassPolicy::BatchCapacityValid(GrassPolicy::kMaxBatchInstances, GrassPolicy::kMaxBatchSlices));
		require(!GrassPolicy::BatchCapacityValid(GrassPolicy::kMaxBatchInstances + 1ull, 1));
		require(!GrassPolicy::BatchCapacityValid(1, GrassPolicy::kMaxBatchSlices + 1ull));
		require(!GrassPolicy::BatchCapacityValid(0, 1));
		require(!GrassPolicy::BatchCapacityValid(1, 0));
		require(!GrassPolicy::BatchCapacityValid(UINT64_MAX, UINT64_MAX));
		require(GrassPolicy::SnapshotBudgetValid(GrassPolicy::kMaxCpuRecordBytes, 32, 32));
		require(!GrassPolicy::SnapshotBudgetValid(GrassPolicy::kMaxCpuRecordBytes, 32, 33));
		require(!GrassPolicy::SnapshotBudgetValid(0, 32, 0));
		require(!GrassPolicy::SnapshotBudgetValid(UINT64_MAX, 0, 0));
		GrassPolicy::Residency generated{ 10, {} };
		generated.Observe(200, true);
		require(generated.VisibleInFrame(200));
		generated.Observe(250, false);
		require(generated.IdleFrames(250) == 50 && !generated.VisibleInFrame(250));
		GrassPolicy::Residency wrapped{ UINT32_MAX - 60, {} };
		require(wrapped.IdleFrames(59) == 120);
		wrapped.Observe(0, true);
		require(wrapped.VisibleInFrame(0) && wrapped.IdleFrames(0) == 0);
		require(GrassPolicy::MeshStride(0x8000000000000087ull) == 28);
		require(GrassPolicy::MeshStride(0x8000000000000080ull) == 0);
		require(GrassPolicy::NativeCountMatches(10, 10));
		require(!GrassPolicy::NativeCountMatches(10, 20));
		require(!GrassPolicy::NativeCountMatches(10, 21));
		require(!GrassPolicy::NativeCountMatches(UINT32_MAX, UINT32_MAX - 1));
		for (float invalid : { -1.0f, 2.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() }) {
			settings = {};
			settings.MinDensity = invalid;
			require(!settings.Valid());
		}
		settings = {};
		settings.FullDetailPixelSize = settings.MinPixelSize;
		require(!settings.Valid());
		settings = {};
		settings.FarLODPixelSize = settings.MidLODPixelSize + 1;
		require(!settings.Valid());
		settings = {};
		settings.OcclusionBias = .051f;
		require(!settings.Valid());
		settings = {};
		settings.MinDensity = 0;
		require(settings.Valid());
		settings.MinDensity = 1;
		require(settings.Valid());
		for (float GrassPolicy::Settings::* field : { &GrassPolicy::Settings::MeshCostBias,
				 &GrassPolicy::Settings::CostBiasStartDistance, &GrassPolicy::Settings::InvisibleFadeCull,
				 &GrassPolicy::Settings::RenderDistanceOverride, &GrassPolicy::Settings::EdgeFadeStart,
				 &GrassPolicy::Settings::SimpleShadingPixelSize, &GrassPolicy::Settings::CollisionDistance }) {
			settings = {};
			settings.*field = std::numeric_limits<float>::quiet_NaN();
			require(!settings.Valid());
			settings.*field = -1;
			require(!settings.Valid());
		}
		std::cout << "Grass bounded settings and threshold ordering passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
