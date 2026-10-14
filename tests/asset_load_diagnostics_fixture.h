#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Diagnostics/AssetLoadHooks.h"
#	include "Diagnostics/EngineStutterMonitor.h"

// Texture fixtures run without captures while retaining the production diagnostics ABI.
namespace CSX::Diagnostics::Stutters
{
	inline AssetScope::AssetScope(AssetKind, std::string_view, std::string_view) noexcept {}
	inline AssetScope::~AssetScope() noexcept {}
	inline void AssetScope::Result(bool) noexcept {}
	inline void AssetScope::Result(bool, std::int64_t) noexcept {}
	inline void AssetScope::CacheHit(bool) noexcept {}
	inline void AssetScope::Bytes(std::uint64_t) noexcept {}
	inline void AssetScope::Dimensions(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) noexcept {}
	namespace AssetLoadHooks
	{
		inline bool ddsAvailable = false;
		inline void SetDDSAvailable(bool available) noexcept { ddsAvailable = available; }
	}
}
#endif
