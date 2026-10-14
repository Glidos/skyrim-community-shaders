#pragma once

#include "CharacterSettings.h"
#include "PackedCropPolicy.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace NeuralRendering
{
	inline constexpr std::uint32_t kCharacterOccupancyTileSize = 16;
	inline constexpr std::uint32_t kMaximumCharacterOccupancyTiles = 262144;
	inline constexpr std::size_t kMaximumCharacterCropSupportRects = 16384;

	/** Completed grids propose layouts only; exact current-mask GPU validation owns coverage. */
	[[nodiscard]] inline bool CharacterTileProposalFresh(std::uint32_t capturedFrame, std::uint32_t currentFrame) noexcept
	{
		return currentFrame - capturedFrame <= 4u;
	}

	/** A missing proposal falls back immediately, then requires sustained availability before rebuilding an atlas. */
	struct CharacterTileCropReadiness
	{
		std::uint32_t readyFrames = 0, cooldownFrames = 0;
		[[nodiscard]] bool Admit(bool ready) noexcept
		{
			constexpr std::uint32_t warmup = 8, cooldown = 60;
			if (!ready) {
				if (readyFrames == warmup)
					cooldownFrames = cooldown;
				readyFrames = 0;
				return false;
			}
			if (cooldownFrames) {
				--cooldownFrames;
				return false;
			}
			readyFrames = std::min(readyFrames + 1u, warmup);
			return readyFrames == warmup;
		}
	};

	struct CharacterTileCrops
	{
		std::vector<CharacterRect> regions;
		std::uint32_t occupiedTiles = 0;
		bool valid = false;
	};

	namespace CharacterTilePolicy
	{
		[[nodiscard]] inline CharacterRect Enclose(std::span<const CharacterRect> tiles) noexcept
		{
			CharacterRect bounds{};
			for (const auto& tile : tiles)
				bounds = CharacterRegionPolicy::Union(bounds, tile);
			return bounds;
		}

		struct Split
		{
			std::vector<CharacterRect> first, second;
			std::uint64_t saving = 0;
		};

		/** Spatial cuts retain neighbouring tiles together and never discard coverage. */
		[[nodiscard]] inline Split FindSplit(std::span<const CharacterRect> tiles)
		{
			Split best;
			const auto area = Enclose(tiles).Area();
			std::size_t bestIndex = 0;
			for (const bool vertical : { false, true }) {
				std::vector<CharacterRect> ordered(tiles.begin(), tiles.end());
				std::ranges::sort(ordered, [vertical](const auto& a, const auto& b) {
					return vertical ? std::tie(a.minY, a.minX, a.maxY, a.maxX) < std::tie(b.minY, b.minX, b.maxY, b.maxX) :
					                  std::tie(a.minX, a.minY, a.maxX, a.maxY) < std::tie(b.minX, b.minY, b.maxX, b.maxY);
				});
				std::vector<CharacterRect> suffix(ordered.size());
				for (std::size_t i = ordered.size(); i-- > 0;)
					suffix[i] = CharacterRegionPolicy::Union(ordered[i], i + 1 < ordered.size() ? suffix[i + 1] : CharacterRect{});
				CharacterRect prefix{};
				for (std::size_t i = 1; i < ordered.size(); ++i) {
					prefix = CharacterRegionPolicy::Union(prefix, ordered[i - 1]);
					const auto& remaining = suffix[i];
					if (vertical ? prefix.maxY > remaining.minY : prefix.maxX > remaining.minX)
						continue;
					const auto splitArea = prefix.Area() + remaining.Area();
					if (splitArea < area && area - splitArea > best.saving) {
						bestIndex = i;
						best.saving = area - splitArea;
					}
				}
				if (bestIndex != 0) {
					best.first.assign(ordered.begin(), ordered.begin() + bestIndex);
					best.second.assign(ordered.begin() + bestIndex, ordered.end());
					bestIndex = 0;
				}
			}
			return best;
		}
	}

	/** Validate a complete eye grid, map source support conservatively, then bound it with at most four crops. */
	[[nodiscard]] inline CharacterTileCrops BuildCharacterTileCrops(std::span<const CharacterRect> sourceTiles,
		UpscalingDLSS::Extent sourceSize, ComputeSubrect sourceCrop, UpscalingDLSS::Extent outputSize,
		std::span<const CharacterRect> eligibility, std::uint32_t guardX, std::uint32_t guardY)
	{
		CharacterTileCrops result;
		if (!sourceSize.IsValid() || !outputSize.IsValid() || !sourceCrop.Fits(sourceSize.width, sourceSize.height) ||
			sourceSize.width > kMaximumNeuralImageDimension || sourceSize.height > kMaximumNeuralImageDimension ||
			outputSize.width > kMaximumNeuralImageDimension || outputSize.height > kMaximumNeuralImageDimension ||
			guardX > outputSize.width || guardY > outputSize.height || eligibility.size() > CharacterPolicy::kMaximumEligibilityRegions)
			return result;
		const auto columns = (sourceSize.width + kCharacterOccupancyTileSize - 1u) / kCharacterOccupancyTileSize;
		const auto rows = (sourceSize.height + kCharacterOccupancyTileSize - 1u) / kCharacterOccupancyTileSize;
		if (std::uint64_t(columns) * rows > kMaximumCharacterOccupancyTiles || sourceTiles.size() != std::uint64_t(columns) * rows)
			return result;
		for (const auto& region : eligibility)
			if (!region.IsValid() || region.maxX > outputSize.width || region.maxY > outputSize.height)
				return result;
		std::vector<CharacterRect> mapped;
		for (std::size_t i = 0; i < sourceTiles.size(); ++i) {
			const auto& tile = sourceTiles[i];
			if (tile == CharacterRect{})
				continue;
			const auto x = static_cast<std::uint32_t>(i % columns) * kCharacterOccupancyTileSize;
			const auto y = static_cast<std::uint32_t>(i / columns) * kCharacterOccupancyTileSize;
			if (!tile.IsValid() || tile.minX < x || tile.minY < y || tile.maxX > std::min(sourceSize.width, x + kCharacterOccupancyTileSize) ||
				tile.maxY > std::min(sourceSize.height, y + kCharacterOccupancyTileSize))
				return {};
			const auto lower = [](std::uint32_t value, std::uint32_t origin, std::uint32_t input, std::uint32_t output, std::uint32_t guard) {
				return static_cast<std::uint32_t>(std::clamp(std::floor((double(value) - origin) * output / input) - guard, 0.0, double(output)));
			};
			const auto upper = [](std::uint32_t value, std::uint32_t origin, std::uint32_t input, std::uint32_t output, std::uint32_t guard) {
				return static_cast<std::uint32_t>(std::clamp(std::ceil((double(value) - origin) * output / input) + guard, 0.0, double(output)));
			};
			const CharacterRect support{
				lower(tile.minX, sourceCrop.baseX, sourceCrop.width, outputSize.width, guardX),
				lower(tile.minY, sourceCrop.baseY, sourceCrop.height, outputSize.height, guardY),
				upper(tile.maxX, sourceCrop.baseX, sourceCrop.width, outputSize.width, guardX),
				upper(tile.maxY, sourceCrop.baseY, sourceCrop.height, outputSize.height, guardY)
			};
			bool occupied = false;
			for (const auto& region : eligibility) {
				const CharacterRect selected{ std::max(support.minX, region.minX), std::max(support.minY, region.minY),
					std::min(support.maxX, region.maxX), std::min(support.maxY, region.maxY) };
				if (selected.IsValid()) {
					if (mapped.size() == kMaximumCharacterCropSupportRects)
						return {};
					mapped.push_back(selected);
					occupied = true;
				}
			}
			result.occupiedTiles += occupied;
		}
		result.valid = true;
		if (mapped.empty())
			return result;
		std::vector<std::vector<CharacterRect>> groups;
		groups.push_back(std::move(mapped));
		while (groups.size() < kMaximumPackedCrops) {
			CharacterTilePolicy::Split best;
			std::size_t selected = 0;
			for (std::size_t i = 0; i < groups.size(); ++i) {
				auto candidate = CharacterTilePolicy::FindSplit(groups[i]);
				if (candidate.saving > best.saving) {
					best = std::move(candidate);
					selected = i;
				}
			}
			if (!best.saving)
				break;
			groups[selected] = std::move(best.first);
			groups.push_back(std::move(best.second));
		}
		for (const auto& group : groups)
			result.regions.push_back(CharacterTilePolicy::Enclose(group));
		std::ranges::sort(result.regions, [](const auto& a, const auto& b) {
			return std::tie(a.minY, a.minX, a.maxY, a.maxX) < std::tie(b.minY, b.minX, b.maxY, b.maxX);
		});
		return result;
	}
}
