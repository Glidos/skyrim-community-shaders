#include "/Shaders/Common/Permutation.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

// The DXC fixture exposes legacy cbuffer members at global scope.
namespace Permutation
{
	static uint TestPixelShaderDescriptor = 0;
}
#define PixelShaderDescriptor TestPixelShaderDescriptor

#define LIGHTING
namespace OrdinaryMaterial
{
#include "/Shaders/Common/LightingFlags.hlsli"
}

#undef CSX_LIGHTING_FLAGS_HLSLI
#define EYE
namespace EyeMaterial
{
#include "/Shaders/Common/LightingFlags.hlsli"
}
#undef EYE

#undef CSX_LIGHTING_FLAGS_HLSLI
#define SSS
#define SKIN
#define DEFERRED
namespace DeferredSkin
{
#include "/Shaders/Common/LightingFlags.hlsli"
}
#undef DEFERRED
#undef SKIN
#undef SSS

#undef CSX_LIGHTING_FLAGS_HLSLI
#define LANDSCAPE
namespace LandscapeMaterial
{
#include "/Shaders/Common/LightingFlags.hlsli"
}
#undef LANDSCAPE

#undef CSX_LIGHTING_FLAGS_HLSLI
#define LODOBJECTS
#define SPECULAR
#define RIM_LIGHTING
namespace LodMaterial
{
#include "/Shaders/Common/LightingFlags.hlsli"
}
#undef RIM_LIGHTING
#undef SPECULAR
#undef LODOBJECTS

#undef CSX_LIGHTING_FLAGS_HLSLI
#define LOAD_SOFT_LIGHTING
namespace ForcedSoftMap
{
#include "/Shaders/Common/LightingFlags.hlsli"
}
#undef LOAD_SOFT_LIGHTING

#undef CSX_LIGHTING_FLAGS_HLSLI
#undef LIGHTING
namespace OtherShaderFamily
{
#include "/Shaders/Common/LightingFlags.hlsli"
}
#undef PixelShaderDescriptor

/// @tags lighting, permutations, eye
/// Every merged flag combination must survive without unrelated descriptor bits.
[numthreads(1, 1, 1)] void TestLightingMaterialFlags() {
	for (uint combination = 0; combination < 16; ++combination) {
		uint flags = combination << 9;
		uint descriptor = flags | Permutation::LightingFlags::VertexColor | Permutation::LightingFlags::DefShadow;
		Permutation::TestPixelShaderDescriptor = descriptor;
		ASSERT(AreEqual, OrdinaryMaterial::Lighting::HasSpecular(), (flags & Permutation::LightingFlags::Specular) != 0u);
		ASSERT(AreEqual, OrdinaryMaterial::Lighting::HasSoftLighting(), (flags & Permutation::LightingFlags::SoftLighting) != 0u);
		ASSERT(AreEqual, OrdinaryMaterial::Lighting::HasRimLighting(), (flags & Permutation::LightingFlags::RimLighting) != 0u);
		ASSERT(AreEqual, OrdinaryMaterial::Lighting::HasBackLighting(), (flags & Permutation::LightingFlags::BackLighting) != 0u);
		ASSERT(AreEqual, OrdinaryMaterial::Lighting::GetFlags(descriptor), flags);
		ASSERT(AreEqual, OrdinaryMaterial::Lighting::GetShadingFlags(descriptor), flags);
		ASSERT(AreEqual, EyeMaterial::Lighting::GetShadingFlags(descriptor), flags);
	}
}

	/// @tags lighting, permutations, skin, shadows
	/// SSS replaces the soft lobe while retaining its eligibility for soft shadows.
	[numthreads(1, 1, 1)] void TestDeferredSkinLightingFlags()
{
	uint flags = Permutation::LightingFlags::Specular | Permutation::LightingFlags::SoftLighting | Permutation::LightingFlags::RimLighting | Permutation::LightingFlags::BackLighting;
	ASSERT(AreEqual, DeferredSkin::Lighting::GetFlags(flags), flags);
	ASSERT(AreEqual, DeferredSkin::Lighting::GetShadingFlags(flags), Permutation::LightingFlags::Specular | Permutation::LightingFlags::RimLighting | Permutation::LightingFlags::BackLighting);
	ASSERT(AreEqual, DeferredSkin::Lighting::GetShadingFlags(Permutation::LightingFlags::SoftLighting), 0u);
	ASSERT(IsTrue, ForcedSoftMap::Lighting::LoadRimSoftLighting());
}

/// @tags lighting, permutations, landscape, lod
/// Landscape sampler aliases, static LOD keys and other families stay isolated.
[numthreads(1, 1, 1)] void TestLightingTechniqueFlags() {
	ASSERT(AreEqual, LandscapeMaterial::Lighting::GetFlags(~0u), Permutation::LightingFlags::Specular);
	ASSERT(AreEqual, LandscapeMaterial::Lighting::GetFlags(Permutation::LightingFlags::BackLighting), 0u);
	ASSERT(AreEqual, LodMaterial::Lighting::GetFlags(0u), Permutation::LightingFlags::Specular | Permutation::LightingFlags::RimLighting);
	ASSERT(AreEqual, LodMaterial::Lighting::GetFlags(~0u), Permutation::LightingFlags::Specular | Permutation::LightingFlags::RimLighting);
	ASSERT(AreEqual, OtherShaderFamily::Lighting::GetFlags(~0u), 0u);
}
