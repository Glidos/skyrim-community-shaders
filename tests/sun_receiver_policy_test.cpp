#include "Diagnostics/SunReceiverTraceRequest.h"
#include "EngineFixes/SunReceiverPolicy.h"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	using namespace SunShadowReceiver::Policy;
	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}
	Input Eligible()
	{
		Input input;
		input.primaryPass = input.available = input.identityStable = input.shadowsEnabled = true;
		input.flags = ReceiveShadows;
		input.lightData = 0x1000;
		input.activeMask = 0xA;
		input.sunMask = 0x40;
		input.shadowDistance = 100;
		input.radius = 10;
		input.center = { 109, 0, 0 };
		return input;
	}
	void Eligibility()
	{
		auto input = Eligible();
		auto mask = input.activeMask;
		const auto decision = Evaluate(input);
		Require(decision.reason == Reason::candidate && decision.rangeValid, "sphere overlaps shadow range");
		Require(!Apply(mask, input.lightData, input, decision, false) && mask == 0xA, "observe must preserve every native bit");
		Require(Apply(mask, input.lightData, input, decision, true) && mask == 0x4A, "repair must add only the missing sun bit");
		Require(!Apply(mask, input.lightData, input, decision, true) && mask == 0x4A, "stale eligibility must not write twice");
		input.activeMask = mask;
		Require(Evaluate(input).reason == Reason::alreadyPresent, "already-shadowed receivers must be skipped");
		for (std::size_t bit : { 33, 34, 52, 63 }) {
			input = Eligible();
			input.flags |= std::uint64_t{ 1 } << bit;
			Require(Evaluate(input).reason == Reason::excludedLod, "each LOD exclusion is independent");
		}
		input = Eligible();
		input.flags = 0;
		Require(Evaluate(input).reason == Reason::notReceiver, "respect receive-shadows opt out");
		for (auto member : { &Input::primaryPass, &Input::available, &Input::identityStable, &Input::shadowsEnabled }) {
			input = Eligible();
			input.*member = false;
			mask = input.activeMask;
			Require(!Apply(mask, input.lightData, input, Evaluate(input), true) && mask == input.activeMask, "invalid context cannot repair");
		}
		input = Eligible();
		input.sunMask = 0;
		Require(Evaluate(input).reason == Reason::noSun, "missing sun mapping cannot manufacture a receiver");
		input = Eligible();
		input.sunMask = 0x42;
		Require(Evaluate(input).reason == Reason::alreadyPresent, "one matching sun bit already supplies reception");
		input = Eligible();
		mask = input.activeMask;
		Require(!Apply(mask, 0, input, Evaluate(input), true) && mask == input.activeMask, "missing light data cannot repair");
		Require(!Apply(mask, input.lightData + 8, input, Evaluate(input), true) && mask == input.activeMask,
			"replacement light data with an identical mask must not receive a stale repair");
		input = Eligible();
		mask = 0x80;
		Require(!Apply(mask, input.lightData, input, Evaluate(input), true) && mask == 0x80, "changed native mask cannot be overwritten");
	}
	void Bounds()
	{
		auto input = Eligible();
		input.center[0] = 110;
		Require(Evaluate(input).reason == Reason::outsideRange, "strict range boundary is excluded");
		input.center[0] = 111;
		Require(Evaluate(input).reason == Reason::outsideRange, "outside-range receiver is excluded");
		input = Eligible();
		input.radius = 0;
		input.center = { 0, 0, 0 };
		Require(Evaluate(input).reason == Reason::candidate, "finite point bounds are valid");
		for (const auto invalid : { -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() }) {
			input = Eligible();
			input.radius = invalid;
			Require(Evaluate(input).reason == Reason::invalidBounds, "invalid radius fails closed");
			input = Eligible();
			input.shadowDistance = invalid;
			Require(Evaluate(input).reason == Reason::invalidBounds, "invalid shadow range fails closed");
		}
		input = Eligible();
		input.shadowDistance = 0;
		Require(Evaluate(input).reason == Reason::invalidBounds, "disabled range fails closed");
		for (std::size_t axis = 0; axis < 3; ++axis) {
			input = Eligible();
			input.center[axis] = std::numeric_limits<float>::quiet_NaN();
			Require(Evaluate(input).reason == Reason::invalidBounds, "nonfinite center fails closed");
			input = Eligible();
			input.camera[axis] = std::numeric_limits<float>::infinity();
			Require(Evaluate(input).reason == Reason::invalidBounds, "nonfinite camera fails closed");
		}
		input = Eligible();
		input.center = { std::numeric_limits<float>::max(), 0, 0 };
		input.camera = { -std::numeric_limits<float>::max(), 0, 0 };
		Require(Evaluate(input).reason == Reason::outsideRange, "large finite coordinates cannot overflow into eligibility");
		input = Eligible();
		input.center = { 500, 600, 700 };
		input.camera = input.center;
		Require(Evaluate(input).reason == Reason::candidate, "distance is relative to the camera");
	}
	void Requests()
	{
		using namespace Diagnostics::SunReceiverTrace;
		using json = nlohmann::json;
		json request{ { "traceId", "test" }, { "maximumFrames", 240 }, { "scope", "all" } };
		auto parsed = ParseStart(request);
		Require(parsed.allGeometry && !parsed.repair && parsed.maximumFrames == 240, "observe is the safe default");
		request["mode"] = "repair";
		Require(ParseStart(request).repair, "repair requires explicit mode");
		auto rejects = [](const json& input) {
			try {
				(void)ParseStart(input);
				return false;
			} catch (const std::exception&) {
				return true;
			}
		};
		for (const auto& invalid : { json(0), json(-1), json(3601), json(1.0), json(true), json(nullptr), json(std::numeric_limits<std::uint64_t>::max()) }) {
			auto copy = request;
			copy["maximumFrames"] = invalid;
			Require(rejects(copy), "invalid duration rejected before capture");
		}
		for (const auto& invalid : { json(""), json(std::string(65, 'x')), json(1) }) {
			auto copy = request;
			copy["traceId"] = invalid;
			Require(rejects(copy), "invalid owner rejected");
		}
		for (const auto* key : { "mode", "scope" }) {
			auto copy = request;
			copy[key] = "unknown";
			Require(rejects(copy), "unknown mode or scope rejected");
		}
		request["formId"] = "00012345";
		Require(rejects(request), "all scope cannot silently ignore mesh identity");
		request.erase("scope");
		Require(rejects(request), "selected scope requires full identity");
		request["geometryIndex"] = 0;
		request["expectedGeometry"] = 1;
		request["expectedProperty"] = 2;
		request.erase("mode");
		Require(!ParseStart(request).allGeometry && !ParseStart(request).repair, "selected observe is backward compatible");
		for (const auto* key : { "geometryIndex", "expectedGeometry", "expectedProperty" })
			for (const auto& invalid : { json(-1), json(0.5), json(1.0), json(true), json(nullptr), json("1") }) {
				auto copy = request;
				copy[key] = invalid;
				Require(rejects(copy), "mesh identity must reject conversion instead of silently narrowing");
			}
		for (const auto& invalid : { json(256), json(std::numeric_limits<std::uint64_t>::max()) }) {
			auto copy = request;
			copy["geometryIndex"] = invalid;
			Require(rejects(copy), "mesh index is bounded before conversion");
		}
		for (const auto* key : { "expectedGeometry", "expectedProperty" }) {
			auto copy = request;
			copy[key] = 0;
			Require(rejects(copy), "expected identities cannot be null");
		}
		request["expectedGeometry"] = std::numeric_limits<std::uintptr_t>::max();
		Require(ParseStart(request).expectedGeometry == std::numeric_limits<std::uintptr_t>::max(), "identity values preserve all bits");
		for (const auto* key : { "lane", "offset", "limit" }) {
			const std::uint64_t minimum = std::string_view(key) == "limit" ? 1 : 0;
			const std::uint64_t maximum = std::string_view(key) == "lane" ? 15 : std::string_view(key) == "offset" ? 4096 :
			                                                                                                         256;
			for (const auto& invalid : { json(-1), json(0.5), json(true), json(nullptr), json("0"),
					 json(maximum + 1), json(std::numeric_limits<std::uint64_t>::max()) }) {
				bool rejected = false;
				try {
					(void)UnsignedArgument(json{ { key, invalid } }, key, minimum, maximum);
				} catch (const std::exception&) {
					rejected = true;
				}
				Require(rejected, "pagination rejects invalid numeric types and bounds");
			}
			Require(UnsignedArgument(json{ { key, minimum } }, key, minimum, maximum) == minimum, "pagination minimum is inclusive");
			Require(UnsignedArgument(json{ { key, maximum } }, key, minimum, maximum) == maximum, "pagination maximum is inclusive");
		}
		request["maximumFrames"] = MaximumCaptureFrames;
		Require(ParseStart(request).maximumFrames == MaximumCaptureFrames, "documented maximum duration accepted");
	}

	void Mapping()
	{
		std::array<std::uintptr_t, 40> lights{};
		const std::uintptr_t sun = 99;
		lights[3] = lights[31] = lights[32] = sun;
		Require(SunMask(lights.data(), 40, sun, true) == 8, "VR uses first matching sun entry");
		Require(SunMask(lights.data(), 40, sun, false) == 0x80000008, "flat collects duplicate representable entries");
		Require(SunMask(lights.data(), 3, sun, false) == 0, "respect array length");
		Require(SunMask(lights.data(), 40, std::uintptr_t{}, false) == 0, "null sun cannot match null entries");
		Require(SunMask(static_cast<const std::uintptr_t*>(nullptr), 40, sun, false) == 0, "missing array fails closed");
	}
}
int main()
{
	try {
		Eligibility();
		Bounds();
		Mapping();
		Requests();
		std::cout << "PASS: observe/repair, eligibility, finite bounds and VR/flat sun mapping\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
