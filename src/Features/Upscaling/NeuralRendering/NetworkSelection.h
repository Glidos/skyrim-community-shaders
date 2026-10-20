#pragma once

#include <cstdint>

namespace NeuralRendering
{
	inline constexpr std::uint32_t kNetworkDefault = 0;
	inline constexpr std::uint32_t kNetworkLegacy = 1;
	inline constexpr std::uint32_t kNetworkNewer = 2;

	/** Preserve provider defaults unless the installed family supports explicit networks. */
	[[nodiscard]] constexpr bool SupportsNetworkSelection(std::uint32_t major, std::uint32_t minor) noexcept
	{
		return major == 310 && minor == 9;
	}

	/** Invalid persisted choices return to the unchanged provider default. */
	[[nodiscard]] constexpr std::uint32_t SanitizeNetworkSelection(std::uint32_t selection) noexcept
	{
		return selection <= kNetworkNewer ? selection : kNetworkDefault;
	}

	/** Native Feature 18 creation value; 310.8 and unknown families retain zero. */
	[[nodiscard]] constexpr std::uint32_t ResolveNetworkPreset(std::uint32_t selection, bool supported) noexcept
	{
		return supported ? SanitizeNetworkSelection(selection) : kNetworkDefault;
	}
}
