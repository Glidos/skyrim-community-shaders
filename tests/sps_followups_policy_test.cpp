#include "Features/LightLimitFix/ShadowCullingPolicy.h"
#include "Features/TextureStreaming/RestorationPriority.h"
#include <iostream>
#include <limits>
#include <stdexcept>

void Require(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}
namespace ShadowStartupFixture
{
	bool spsLoaded = false, conflict = false, available = false;
	unsigned writes = 0;
	std::string failure;
	namespace RE
	{
		constexpr std::array<std::uintptr_t, 1> VTABLE_BSCullingProcess{ 1 }, VTABLE_BSParabolicCullingProcess{ 2 };
	}
	using AppendFunction = void (*)();
	AppendFunction appendOriginal = nullptr, parabolicOriginal = nullptr;
	void Append() {}
	void Parabolic() {}
	void Original() {}
	void* Module(const wchar_t*) { return spsLoaded ? reinterpret_cast<void*>(1) : nullptr; }
	namespace REL
	{
		template <class T>
		struct Relocation
		{
			explicit Relocation(T) {}
			T write_vfunc(unsigned, AppendFunction)
			{
				++writes;
				return reinterpret_cast<T>(&Original);
			}
		};
	}
	namespace logger
	{
		template <class... T>
		void warn(const char*, T&&...)
		{}
	}
#define GetModuleHandleW Module
#include "shadow_startup_under_test.h"
#undef GetModuleHandleW
	void CheckStartup()
	{
		spsLoaded = true;
		Install();
		Require(conflict && !available && writes == 0, "SPS detection still replaced native shadow hooks");
		conflict = spsLoaded = false;
		Install();
		Require(available && writes == 2, "Native shadow hooks were not installed without SPS");
		Install();
		Require(writes == 2, "Repeated initialization chained the hook to itself");
	}
}
int main()
{
	try {
		ShadowStartupFixture::CheckStartup();
		namespace C = LightLimitFixDetail::ShadowCulling;
		const std::array<double, 3> actorRoot{ 100, 0, 0 }, view{ 0, 0, 0 }, staleCenter{ 10000, 0, 0 };
		Require(C::SurfaceDistance(staleCenter, 16, view, &actorRoot) == 84, "Stale skin bounds overrode the nearby actor root");
		Require(!std::isfinite(C::SurfaceDistance({ std::numeric_limits<double>::infinity(), 0, 0 }, 16, view, &actorRoot)), "An actor root hid invalid geometry bounds");
		C::Settings settings;
		Require(settings.sun && settings.torch && settings.actors && settings.interiorTorches, "Culling defaults did not enable all four controls");
		Require(C::Parse(nlohmann::json::object()) == settings, "Missing saved settings did not use the new defaults");
		C::Demand demand{ C::Pass::Sun, { 10000, 10000 }, 2, 16, 1, 1000, 2000, false, false, false };
		Require(C::ShouldCull(settings, demand), "Default sun culling was inactive");
		demand.pass = C::Pass::Point;
		Require(C::ShouldCull(settings, demand), "Default torch culling was inactive");
		demand.interior = true;
		Require(C::ShouldCull(settings, demand), "Default indoor torch culling was inactive");
		demand.actor = true;
		Require(C::ShouldCull(settings, demand), "Default distant NPC culling was inactive");
		demand.interior = false;
		settings.sun = settings.torch = settings.actors = settings.interiorTorches = false;
		Require(nlohmann::json(settings).get<C::Settings>() == settings, "Saved disabled preferences were overwritten by defaults");
		demand.pass = C::Pass::Sun;
		demand.actor = false;
		Require(!C::ShouldCull(settings, demand), "Disabled culling still discarded shadows");
		settings.sun = true;
		Require(C::ShouldCull(settings, demand), "Tiny distant sun caster was not admitted");
		demand.eyeDistance[1] = 1000;
		Require(!C::ShouldCull(settings, demand), "A close second VR eye lost its shadow");
		demand.eyeDistance[1] = 10000;
		demand.sunElevationSine = 0.1;
		Require(!C::ShouldCull(settings, demand), "Low sun lost a long shadow");
		demand.sunElevationSine = 0.2;
		Require(!C::ShouldCull(settings, demand), "Magnified sun shadow was treated as a tiny caster");
		demand.sunElevationSine = 1;
		demand.dynamic = true;
		Require(!C::ShouldCull(settings, demand), "Animated geometry was culled as static");
		demand.dynamic = false;
		demand.pass = C::Pass::Unknown;
		Require(!C::ShouldCull(settings, demand), "A world or preview pass was culled");
		demand.pass = C::Pass::Point;
		settings.torch = true;
		Require(C::ShouldCull(settings, demand), "Small distant point-light shadow was not admitted");
		demand.lightDistance = 100;
		Require(!C::ShouldCull(settings, demand), "A near-light caster lost its magnified shadow");
		demand.lightDistance = 500;
		demand.lightRadius = 10000;
		Require(!C::ShouldCull(settings, demand), "A large projected wall shadow was discarded");
		demand.lightDistance = 1000;
		demand.lightRadius = 2000;
		demand.interior = true;
		Require(!C::ShouldCull(settings, demand), "Indoor shadow culling started without permission");
		settings.interiorTorches = true;
		Require(C::ShouldCull(settings, demand), "Indoor torch opt-in was not respected");
		demand.actor = true;
		Require(!C::ShouldCull(settings, demand), "NPC shadows were culled without opting in");
		settings.actors = true;
		Require(C::ShouldCull(settings, demand), "Distant NPC shadow option was ignored");
		demand.protectedGeometry = true;
		Require(!C::ShouldCull(settings, demand), "NPC effects bypassed the special-geometry guard");
		demand.protectedGeometry = false;
		demand.lightRadius = 20000;
		Require(!C::ShouldCull(settings, demand), "NPC lost a large projected wall shadow");
		demand.lightRadius = 2000;
		demand.eyeDistance[1] = 2000;
		Require(!C::ShouldCull(settings, demand), "Nearby NPC lost its shadow in the second eye");
		demand.eyeDistance = { 10000, 10000 };
		demand.radius = std::numeric_limits<double>::quiet_NaN();
		Require(!C::ShouldCull(settings, demand), "Invalid bounds authorized culling");
		for (const auto& bad : { nlohmann::json{ { "sun", 1 } }, nlohmann::json{ { "minimumDistanceMeters", 29 } },
				 nlohmann::json{ { "actorDistanceMeters", 49 } }, nlohmann::json{ { "torch", true }, { "unknown", false } } }) {
			bool rejected = false;
			try {
				(void)C::Parse(bad, settings);
			} catch (const std::invalid_argument&) {
				rejected = true;
			}
			Require(rejected, "Malformed shadow settings escaped validation");
		}
		Require(C::Parse({ { "torch", false } }, settings).sun, "Partial settings discarded the existing sun preference");
		Require(nlohmann::json(settings).get<C::Settings>() == settings, "Saved culling preferences changed on reload");
		using P = StreamingTextures::RestorationPriority;
		const P near{ 2, 8, 4000, 0, 9 }, far{ 2, 2, 4000, 0, 1 }, aged{ 2, 1, 1, 0, 10 }, refill{ 1, 64, 1, 0, 5 }, preview{ 3, 1, 4000, 0, 6 };
		Require(near.BetterThan(far, 4500), "Urgent visible restoration queued behind a less-needed texture");
		Require(aged.BetterThan(near, 6000), "Urgency starved an older restoration");
		Require(near.BetterThan(refill, 6000), "Optional refill displaced a required restore");
		Require(preview.BetterThan(aged, 6000), "World restoration displaced an active preview");
		Require(P{ 0, 0, 0, 100, 2 }.BetterThan(P{ 0, 0, 0, 50, 1 }, 6000), "Shrink selection lost largest-saving priority");
		std::cout << "SPS follow-up culling and restoration policies passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
