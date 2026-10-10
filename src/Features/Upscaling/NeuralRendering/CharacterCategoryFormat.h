#pragma once

#include <dxgiformat.h>

namespace NeuralRendering
{
	inline constexpr DXGI_FORMAT kVertexAoFormat = DXGI_FORMAT_R16_UNORM;

	/** Preserve 16-bit vertex AO alongside the exact character category lane. */
	inline constexpr DXGI_FORMAT kCharacterCategoryFormat = DXGI_FORMAT_R16G16_UNORM;
}
