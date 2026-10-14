#pragma once
#include <cstddef>

namespace RE
{
	class NiSourceTexture;
}
namespace Util::NativeTextureLoading
{
	/** Shared shader-resource-manager creation ABI for SE, AE and VR. */
	using Create = void (*)(void*, RE::NiSourceTexture*);
	inline constexpr std::size_t kCreateSlot = 0x1A;
}
