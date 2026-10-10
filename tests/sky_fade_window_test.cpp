#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace RE
{
	struct Setting
	{
		enum class Type
		{
			kFloat,
			kInteger
		};
		Type type = Type::kFloat;
		float value = 1.0f;
		Type GetType() const { return type; }
		float GetFloat() const { return value; }
	};

	struct BSSkyShaderProperty
	{
		struct
		{
			float alpha = 1.0f;
		} kBlendColor;
	};

	struct Geometry
	{
		struct RuntimeData
		{
			struct PropertyPointer
			{
				BSSkyShaderProperty* value = nullptr;
				BSSkyShaderProperty* get() const { return value; }
			} shaderProperty;
		} data;
		RuntimeData& GetGeometryRuntimeData() { return data; }
	};

	struct Sun
	{
		Geometry* sunBase = nullptr;
		Geometry* sunGlare = nullptr;
	};

	struct TESClimate
	{
		struct Timing
		{
			struct Interval
			{
				std::uint8_t begin;
				std::uint8_t end;
			};
			Interval sunrise = { 36, 48 };
			Interval sunset = { 108, 120 };
		} timing;
	};

	struct Sky
	{
		Sun* sun = nullptr;
		TESClimate* currentClimate = nullptr;
		float currentGameHour = 12.0f;
	};
}

template <class T>
T skyrim_cast(RE::BSSkyShaderProperty* property)
{
	return static_cast<T>(property);
}

struct SkySync
{
	inline static RE::Setting* gSunAlphaTransTime = nullptr;
	static void HideSunOutsideFadeWindow(const RE::Sky* sky);
};

#include "sky_fade_window_under_test.h"

namespace
{
	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	struct Fixture
	{
		RE::Setting transition;
		RE::BSSkyShaderProperty discProperty;
		RE::BSSkyShaderProperty glareProperty;
		RE::Geometry disc;
		RE::Geometry glare;
		RE::Sun sun;
		RE::TESClimate climate;
		RE::Sky sky;

		Fixture()
		{
			disc.data.shaderProperty.value = &discProperty;
			glare.data.shaderProperty.value = &glareProperty;
			sun = { &disc, &glare };
			sky = { &sun, &climate, 12.0f };
			SkySync::gSunAlphaTransTime = &transition;
		}

		void Check(float hour, bool hidden)
		{
			sky.currentGameHour = hour;
			discProperty.kBlendColor.alpha = 1.0f;
			glareProperty.kBlendColor.alpha = 0.4f;
			SkySync::HideSunOutsideFadeWindow(&sky);
			Require(discProperty.kBlendColor.alpha == (hidden ? 0.0f : 1.0f), "sun disc visibility disagrees with fade boundary");
			Require(glareProperty.kBlendColor.alpha == (hidden ? 0.0f : 0.4f), "glare alpha must be retained inside the fade window");
		}
	};

	void CheckBoundaries()
	{
		Fixture fixture;
		fixture.Check(0.0f, true);
		fixture.Check(24.0f, true);
		fixture.Check(12.0f, false);
		for (const float transition : { 0.0f, 0.3f, 1.0f, 2.0f }) {
			fixture.transition.value = transition;
			for (std::uint8_t begin = 1; begin < 120; ++begin) {
				fixture.climate.timing.sunrise = { begin, static_cast<std::uint8_t>(begin + 1) };
				fixture.climate.timing.sunset = { 130, 131 };
				constexpr float hoursPerUnit = 1.0f / 6.0f;
				const float start = ((begin + 1) * hoursPerUnit + begin * hoursPerUnit) * 0.5f - transition * 0.5f;
				const float end = (131 * hoursPerUnit + 130 * hoursPerUnit) * 0.5f + transition * 0.5f;
				fixture.Check(std::nextafter(start, -std::numeric_limits<float>::infinity()), true);
				fixture.Check(start, true);
				fixture.Check(std::nextafter(start, std::numeric_limits<float>::infinity()), false);
				fixture.Check(std::nextafter(end, -std::numeric_limits<float>::infinity()), false);
				fixture.Check(end, true);
				fixture.Check(std::nextafter(end, std::numeric_limits<float>::infinity()), true);
			}
		}
		fixture.climate.timing = {};
		fixture.transition.value = 1.0f;
		fixture.Check(6.25f, true);
		fixture.transition.value = 2.0f;
		fixture.Check(6.25f, false);
	}

	void CheckInvalidSettingFallback()
	{
		Fixture fixture;
		for (const float value : { -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() }) {
			fixture.transition.value = value;
			fixture.Check(0.0f, false);
		}
		fixture.transition.value = 1.0f;
		fixture.transition.type = RE::Setting::Type::kInteger;
		fixture.Check(0.0f, false);
		fixture.transition.type = RE::Setting::Type::kFloat;
		SkySync::gSunAlphaTransTime = nullptr;
		fixture.Check(0.0f, false);
		SkySync::gSunAlphaTransTime = &fixture.transition;
		fixture.Check(std::numeric_limits<float>::quiet_NaN(), false);
	}

	void CheckMissingGeometry()
	{
		Fixture fixture;
		fixture.sky.currentGameHour = 0.0f;
		fixture.sun.sunBase = nullptr;
		SkySync::HideSunOutsideFadeWindow(&fixture.sky);
		Require(fixture.glareProperty.kBlendColor.alpha == 0.0f, "missing disc must not prevent glare hiding");
		fixture.sun.sunBase = &fixture.disc;
		fixture.sun.sunGlare = nullptr;
		SkySync::HideSunOutsideFadeWindow(&fixture.sky);
		Require(fixture.discProperty.kBlendColor.alpha == 0.0f, "missing glare must not prevent disc hiding");
		fixture.disc.data.shaderProperty.value = nullptr;
		SkySync::HideSunOutsideFadeWindow(&fixture.sky);
		fixture.sky.currentClimate = nullptr;
		SkySync::HideSunOutsideFadeWindow(&fixture.sky);
		fixture.sky.sun = nullptr;
		SkySync::HideSunOutsideFadeWindow(&fixture.sky);
		SkySync::HideSunOutsideFadeWindow(nullptr);
	}
}

int main()
{
	try {
		CheckBoundaries();
		CheckInvalidSettingFallback();
		CheckMissingGeometry();
		std::cout << "Sun disc/glare fade boundaries, live settings and fallback cases passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
