#include "Features/Upscaling/NeuralRendering/CharacterTileCropPolicy.h"

#include <iostream>
#include <random>
#include <stdexcept>

using namespace NeuralRendering;

static void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

static void AddPixel(std::vector<CharacterRect>& grid, unsigned width, unsigned x, unsigned y)
{
	const auto columns = (width + 15u) / 16u;
	auto& tile = grid[(y / 16u) * columns + x / 16u];
	tile = CharacterRegionPolicy::Union(tile, { x, y, x + 1u, y + 1u });
}

static bool Covered(const CharacterTileCrops& crops, CharacterRect required)
{
	return std::ranges::any_of(crops.regions, [&](auto r) {
		return r.minX <= required.minX && r.minY <= required.minY && r.maxX >= required.maxX && r.maxY >= required.maxY;
	});
}

int main()
{
	constexpr unsigned width = 2048, height = 1024;
	std::vector<CharacterRect> grid((width / 16u) * (height / 16u));
	const std::array eligibility{ CharacterRect{ 0, 0, width, height } };
	for (auto [x, y] : { std::pair{ 101u, 201u }, { 109u, 289u }, { 921u, 501u }, { 1901u, 101u }, { 1917u, 401u } })
		AddPixel(grid, width, x, y);
	auto crops = BuildCharacterTileCrops(grid, { width, height }, { 0, 0, width, height }, { width, height }, eligibility, 2, 2);
	Require(crops.valid && crops.regions.size() >= 3 && crops.regions.size() <= 4, "Separated occupied clusters were not retained as coherent crops");
	for (const auto& tile : grid) {
		if (tile.IsValid())
			Require(Covered(crops, { tile.minX - 2, tile.minY - 2, tile.maxX + 2, tile.maxY + 2 }), "An occupied tile's filtering footprint was discarded");
	}
	std::uint64_t area = 0;
	for (const auto& r : crops.regions)
		area += r.Area();
	Require(area < CharacterTilePolicy::Enclose(crops.regions).Area() / 2, "Clustering retained a mostly empty bounding rectangle");
	const auto sources = BuildPackedCropSources(crops.regions, width, height, { 0, 0, width, height });
	Require(sources.count >= 2, "Sparse silhouette crops did not reach atlas packing");
	Require(BuildPackedCropLayout(sources, { width, height }, { width, height }, { 0, 0, width, height }).count >= 2,
		"Sparse silhouette atlas failed its area gate");

	constexpr unsigned oddWidth = 63, oddHeight = 49;
	std::vector<CharacterRect> odd(4 * 4);
	AddPixel(odd, oddWidth, 0, 0);
	AddPixel(odd, oddWidth, 62, 48);
	const std::array oddEligibility{ CharacterRect{ 0, 0, 126, 98 } };
	auto mapped = BuildCharacterTileCrops(odd, { 63, 49 }, { 0, 0, 63, 49 }, { 126, 98 }, oddEligibility, 3, 3);
	Require(mapped.valid && Covered(mapped, { 0, 0, 5, 5 }) && Covered(mapped, { 121, 93, 126, 98 }),
		"Upscaling lost odd-grid edge coverage");
	const std::array croppedEligibility{ CharacterRect{ 0, 0, 40, 32 } };
	std::fill(odd.begin(), odd.end(), CharacterRect{});
	AddPixel(odd, oddWidth, 9, 10);
	AddPixel(odd, oddWidth, 29, 25);
	mapped = BuildCharacterTileCrops(odd, { 63, 49 }, { 10, 10, 20, 16 }, { 40, 32 }, croppedEligibility, 2, 2);
	Require(mapped.valid && Covered(mapped, { 0, 0, 2, 4 }) && Covered(mapped, { 36, 28, 40, 32 }),
		"Cropped input mapping lost filtering support outside the input edge");

	auto malformed = grid;
	malformed[0] = { 15, 0, 17, 1 };
	Require(!BuildCharacterTileCrops(malformed, { width, height }, { 0, 0, width, height }, { width, height }, eligibility, 2, 2).valid,
		"Out-of-tile bounds were treated as current coverage");
	Require(!BuildCharacterTileCrops(std::span(grid).first(grid.size() - 1), { width, height }, { 0, 0, width, height }, { width, height }, eligibility, 2, 2).valid,
		"A partial eye grid was admitted");
	Require(!BuildCharacterTileCrops(grid, { UINT32_MAX, height }, { 0, 0, width, height }, { width, height }, eligibility, 2, 2).valid,
		"Unbounded source dimensions reached tile arithmetic");
	std::fill(grid.begin(), grid.end(), CharacterRect{});
	Require(BuildCharacterTileCrops(grid, { width, height }, { 0, 0, width, height }, { width, height }, eligibility, 2, 2).regions.empty(),
		"An empty current grid retained a stale crop");

	Require(CharacterTileProposalFresh(100, 101) && CharacterTileProposalFresh(100, 104), "Completed proposals were discarded before their bounded lifetime");
	Require(!CharacterTileProposalFresh(100, 105) && !CharacterTileProposalFresh(101, 100), "Stale or future proposals reached packing");
	Require(CharacterTileProposalFresh(UINT32_MAX - 1, 1), "Frame wrap lost a fresh completed proposal");
	CharacterTileCropReadiness admission;
	for (unsigned i = 0; i < 7; ++i)
		Require(!admission.Admit(true), "Transient readiness rebuilt the atlas");
	Require(admission.Admit(true), "Sustained readiness was not admitted");
	Require(!admission.Admit(false), "A pending current grid retained stale occupancy");
	for (unsigned i = 0; i < 60 + 7; ++i)
		Require(!admission.Admit(true), "An interrupted atlas bypassed its cooldown or readiness window");
	Require(admission.Admit(true), "Occupancy did not recover after a bounded cooldown");
	for (unsigned i = 0; i < 100; ++i)
		Require(admission.Admit(true), "Steady readiness churned crop admission");

	std::mt19937 random(12345);
	for (unsigned repeat = 0; repeat < 30; ++repeat) {
		std::fill(odd.begin(), odd.end(), CharacterRect{});
		for (unsigned i = 0; i < 40; ++i)
			AddPixel(odd, oddWidth, random() % oddWidth, random() % oddHeight);
		const std::array full{ CharacterRect{ 0, 0, 63, 49 } };
		const auto candidate = BuildCharacterTileCrops(odd, { 63, 49 }, { 0, 0, 63, 49 }, { 63, 49 }, full, 0, 0);
		Require(candidate.valid && candidate.regions.size() <= 4, "Random coverage escaped the crop limit");
		for (const auto& tile : odd)
			if (tile.IsValid())
				Require(Covered(candidate, tile), "Spatial partition discarded random occupied coverage");
	}
	std::cout << "16x16 silhouette crop coverage, mapping, validation and readiness passed\n";
}
