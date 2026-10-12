#include "Features/Upscaling/FoveatedMaskCalibrationJson.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <string_view>

namespace
{
	using namespace FoveatedMaskCalibration;
	void Require(bool condition, const char* message)
	{
		if (!condition) {
			std::cerr << message << '\n';
			std::exit(1);
		}
	}

	void CheckAutomaticWarnings()
	{
		const Projection identity{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
		Reference reference{ .version = FoveatedMaskCalibration::kReferenceVersion, .outer = { .scale = .4f, .centers = { .25f, .5f, .75f, .5f } }, .leftToRight = identity };
		const auto manual = CheckCoverage(reference, 100, reference.outer, .4f, false, true, kManualContourTolerance);
		Require(manual.available && manual.hasGap && manual.uncoveredVertices[0] && manual.uncoveredVertices[1],
			"Separated manual masks must warn about the shared centre gap");
		const auto fitted = Solve(reference, 100);
		Require(fitted.has_value(), "Manual centre gaps must have a safe calibrated fit");
		const auto calibrated = CheckCoverage(reference, 100, fitted->geometry, fitted->geometry.scale, fitted->fullImage, true);
		Require(calibrated.available && !calibrated.hasGap, "Calibration must clear the automatic shared-view warning");
		Require(!fitted->fullImage, "The centre-gap fixture must exercise a bounded fit");
		auto featherOnly = fitted->geometry;
		featherOnly.scale = std::max(.25f, featherOnly.scale - .02f);
		Require(CheckCoverage(reference, 100, featherOnly, featherOnly.scale, false, true).hasGap &&
					!CheckCoverage(reference, 100, featherOnly, fitted->geometry.scale, false, true).hasGap,
			"Feather-only overlap must not count as full-strength shared coverage");
		reference.outer.centers = { .5f, .5f, .5f, .5f };
		reference.innerLimits = { .7f, .3f };
		const auto aligned = CheckCoverage(reference, 100, reference.outer, .4f, false, true, kManualContourTolerance);
		Require(aligned.available && !aligned.hasGap, "Aligned manual masks must not warn about contour approximation");
		// A horizontal view shift gives each eye a different monocular outer strip.
		reference.leftToRight = { 1, 0, .5, 0, 1, 0, 0, 0, 1 };
		reference.outer.scale = .6f;
		reference.outer.centers = { .8f, .5f, .2f, .5f };
		Geometry geometry{ .95f, 1, { .25f, .5f, .75f, .5f } };
		const auto monocular = CheckCoverage(reference, 100, geometry, .95f, false, true);
		Require(CheckCoverage(reference, 100, geometry, .95f, false, false).hasGap,
			"The asymmetric fixture must have gaps only outside the shared headset view");
		Require(monocular.available && !monocular.hasGap,
			"Non-identical mask regions outside the shared headset view must not generate gaps");
		reference.leftToRight = {};
		Require(!CheckCoverage(reference, 100, geometry, .5f, false, true).available,
			"An unavailable stereo projection must never report verified shared coverage");
		reference.leftToRight = identity;
		geometry.centers[0] = std::numeric_limits<float>::quiet_NaN();
		Require(!CheckCoverage(reference, 100, geometry, .5f, false, true).available,
			"Invalid geometry must not enter shared-view checks");
	}

	void CheckInheritedBoundary()
	{
		struct Settings
		{
			float foveatedCenterArea = 0.6f, foveatedOuterBlendFeather = 0.05f;
			float periphery_taa_center_area = 0.3f, periphery_taa_outer_scale = 0.8f, periphery_taa_center_blend_feather = 0.02f;
			bool periphery_taa_enable = true;
			bool foveatedDisableOuterFeather = false;
			Reference foveatedCalibrationReference;
			SetupConfirmation foveatedMaskSetup;
			float foveatedCenterHorizontalScale = 1.0f;
			float foveatedLeftEyeMaskOffsetX = 0, foveatedLeftEyeMaskOffsetY = 0;
			float foveatedRightEyeMaskOffsetX = 0, foveatedRightEyeMaskOffsetY = 0;
		} settings;
		for (float invalid : { -1.0f, 1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() }) {
			const auto configured = FoveatedCommon::ClampBlendFeather(invalid);
			Require(std::isfinite(configured) && configured >= 0 && configured <= FoveatedCommon::kMaximumFeather,
				"Configured feather sanitization must keep all consumers within their shared bounds");
			Require(FoveatedCommon::GetOuterFeather(invalid, true) == 0,
				"The hard boundary must remain exact even for invalid saved feather values");
		}
		Require(FoveatedCommon::ClampBlendFeather(0) == 0 &&
					FoveatedCommon::GetOuterFeather(0) == FoveatedCommon::kMinimumFeather,
			"Configured zero and the enabled outer fade minimum have distinct ownership");
		InheritFovOnlyBoundary(settings);
		Require(!settings.periphery_taa_enable, "Uncalibrated saved TAA must fall back to FOV only");
		for (const float outer : { 0.25f, 0.6f, 0.85f, 0.95f }) {
			for (const float feather : { 0.0f, 0.05f, 0.1f }) {
				settings.foveatedMaskSetup = {};
				settings.foveatedCenterArea = outer;
				settings.foveatedOuterBlendFeather = feather;
				settings.foveatedCalibrationReference = { .version = FoveatedMaskCalibration::kReferenceVersion,
					.outer = { .scale = outer + 2.0f * std::max(feather, FoveatedCommon::kMinimumFeather) },
					.leftToRight = { 1, 0, 0, 0, 1, 0, 0, 0, 1 },
					.feather = std::max(feather, FoveatedCommon::kMinimumFeather) };
				const auto reference = settings.foveatedCalibrationReference;
				Require(!HasFovOnlySetup(settings), "Calibration alone must not unlock TAA");
				settings.foveatedMaskSetup = CaptureSetupConfirmation(settings);
				const auto serialized = nlohmann::json(settings.foveatedMaskSetup);
				settings.foveatedMaskSetup = serialized.get<SetupConfirmation>();
				Require(HasFovOnlySetup(settings), "Explicit confirmation must survive JSON round trips");
				for (const float inner : { 0.25f, 0.3f, 0.6f, 1.0f }) {
					settings.periphery_taa_enable = true;
					settings.periphery_taa_center_area = inner;
					InheritFovOnlyBoundary(settings);
					Require(settings.periphery_taa_enable && settings.foveatedCalibrationReference == reference &&
								settings.foveatedCenterArea == outer && settings.foveatedOuterBlendFeather == feather,
						"Mode and inner changes must preserve FOV-only geometry and its reference");
					Require(settings.periphery_taa_center_area <= outer && settings.periphery_taa_center_area >= MinimumInnerScale(settings),
						"The inner area must stay bounded by inherited feather support");
					Require(settings.periphery_taa_center_blend_feather == 0.02f, "TAA must retain its independent inner feather");
					for (const float horizontal : { 1.0f, 1.8f }) {
						for (const float offset : { -0.3f, 0.0f, 0.3f }) {
							const auto original = FoveatedMaskVisualization::MeasureCoverage(outer, FoveatedCommon::GetOuterFeather(feather), horizontal, offset, 0.04f, false, 1.0f);
							const auto inherited = FoveatedMaskVisualization::MeasureCoverage(settings.periphery_taa_center_area,
								GetInnerFeather(settings), horizontal, offset, 0.04f, true, settings.periphery_taa_outer_scale);
							Require(std::abs(original.totalPercent - inherited.totalPercent) < 0.0001f,
								"Inherited outer coverage must remain equal across centre sizes, clipping and wide feather support");
						}
					}
				}
			}
		}
		settings.foveatedCenterArea = 0.6f;
		settings.foveatedOuterBlendFeather = 0.05f;
		settings.foveatedCalibrationReference.feather = 0.05f;
		settings.periphery_taa_center_blend_feather = 0.05f;
		settings.foveatedMaskSetup = CaptureSetupConfirmation(settings);
		const auto savedReference = settings.foveatedCalibrationReference;
		for (bool disabled : { true, false, true }) {
			settings.foveatedDisableOuterFeather = disabled;
			settings.periphery_taa_enable = true;
			settings.periphery_taa_center_area = 0.3f;
			InheritFovOnlyBoundary(settings);
			Require(settings.periphery_taa_enable && settings.foveatedCalibrationReference == savedReference,
				"Outer fade experiments must preserve calibrated TAA availability and saved scaling references");
			Require(std::abs(settings.periphery_taa_outer_scale - (disabled ? 0.6f : 0.7f)) < 1e-6f &&
						GetInnerFeather(settings) == 0.05f && settings.periphery_taa_center_blend_feather == 0.05f,
				"Only outer support changes; the inner transition and configured width stay intact");
			settings.periphery_taa_center_area = 0.58f;
			InheritFovOnlyBoundary(settings);
			Require(!disabled || settings.periphery_taa_center_area + 2 * GetInnerFeather(settings) <= settings.periphery_taa_outer_scale,
				"A narrow inner transition must remain within the hard outer boundary");
			settings.periphery_taa_center_area = 0.5999f;
			InheritFovOnlyBoundary(settings);
			Require(!disabled || GetInnerFeather(settings) == 0,
				"A sub-minimum transition must not acquire the shader's minimum outside the hard boundary");
			settings.periphery_taa_center_area = settings.foveatedCenterArea;
			InheritFovOnlyBoundary(settings);
			Require(!disabled || GetInnerFeather(settings) == 0, "No TAA ring must not reintroduce outer feathering");
		}
		settings.foveatedDisableOuterFeather = false;
		for (auto member : { &Settings::foveatedCenterArea, &Settings::foveatedCenterHorizontalScale,
				 &Settings::foveatedLeftEyeMaskOffsetX, &Settings::foveatedLeftEyeMaskOffsetY,
				 &Settings::foveatedRightEyeMaskOffsetX, &Settings::foveatedRightEyeMaskOffsetY,
				 &Settings::foveatedOuterBlendFeather }) {
			auto changed = settings;
			changed.*member += 0.001f;
			Require(!HasFovOnlySetup(changed), "Every outer slider edit must invalidate confirmation");
			InheritFovOnlyBoundary(changed);
			changed.*member = settings.*member;
			Require(!HasFovOnlySetup(changed), "Returning a slider cannot silently restore confirmation");
		}
		settings.foveatedMaskSetup = {};
		settings.periphery_taa_center_area = std::numeric_limits<float>::quiet_NaN();
		InheritFovOnlyBoundary(settings);
		Require(!settings.periphery_taa_enable && std::isfinite(settings.periphery_taa_center_area), "Corrupt saved inner values must sanitize safely");
	}

	void CheckPersistence(const Reference& reference)
	{
		const nlohmann::json saved = reference;
		Require(nlohmann::json::parse(saved.dump()).get<Reference>() == reference, "Saved outer reference must round-trip exactly");
		Require(nlohmann::json(Reference{}).get<Reference>() == Reference{}, "Uncalibrated defaults must round-trip");
		auto legacy = saved;
		legacy["version"] = 1;
		legacy.erase("innerLimits");
		Require(legacy.get<Reference>() == Reference{}, "Legacy calibration must expire without changing manual settings");
		auto previous = saved;
		previous["version"] = 2;
		previous.erase("headsetOutlines");
		previous.erase("coveragePercent");
		Require(previous.get<Reference>() == reference, "Version-two manual references must remain usable without recalibration");
		auto versionThree = saved;
		versionThree["version"] = 3;
		versionThree.erase("coveragePercent");
		Require(versionThree.get<Reference>() == reference, "Version-three references retain their original 100-percent baseline");
		auto versionFour = saved;
		versionFour["version"] = 4;
		Require(versionFour.get<Reference>() == reference, "Version-four manual references must retain their captured boundary");
		versionFour["coveragePercent"] = 130;
		Require(versionFour.get<Reference>().coveragePercent == 130, "Legacy manual baselines must load without rejecting the profile");
		for (float percent : { 25.0f, 80.0f, 100.0f }) {
			auto captured = reference;
			captured.coveragePercent = percent;
			const auto reloaded = nlohmann::json::parse(nlohmann::json(captured).dump()).get<Reference>();
			Require(reloaded == captured && BuildTargets(reloaded, percent) == BuildTargets(reference, 100),
				"Saving and reloading a calibrated percentage must retain exactly the same outer target");
			const auto fit = Solve(reloaded, percent), baseline = Solve(reference, 100);
			Require(fit && baseline && fit->geometry == baseline->geometry,
				"Calibration at the current percentage must not scale the current fit again");
		}
		std::vector<nlohmann::json> corrupt;
		for (auto version : { nlohmann::json(-1), nlohmann::json(kReferenceVersion + 1), nlohmann::json(1.5), nlohmann::json(4294967297ULL) }) {
			auto changed = saved;
			changed["version"] = version;
			corrupt.push_back(changed);
		}
		for (const char* key : { "outer", "leftToRight", "innerLimits", "feather", "peripheryTaa", "coveragePercent" }) {
			auto changed = saved;
			changed.erase(key);
			corrupt.push_back(changed);
		}
		auto changed = saved;
		changed["outer"]["centers"].push_back(0.5);
		corrupt.push_back(changed);
		changed = saved;
		changed["leftToRight"] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		corrupt.push_back(changed);
		changed = saved;
		changed["outer"]["scale"] = 99;
		corrupt.push_back(changed);
		for (const char* key : { "scale", "horizontalScale" }) {
			changed = saved;
			changed["outer"][key] = true;
			corrupt.push_back(changed);
		}
		for (const char* key : { "centerScale", "feather" }) {
			changed = saved;
			changed[key] = key == std::string_view("centerScale");
			corrupt.push_back(changed);
		}
		changed = saved;
		changed["leftToRight"][0] = true;
		corrupt.push_back(changed);
		for (const auto limits : { nlohmann::json{ 1, 0, 0 }, nlohmann::json{ 2, 0 }, nlohmann::json{ true, 0 } }) {
			changed = saved;
			changed["innerLimits"] = limits;
			corrupt.push_back(changed);
		}
		for (const auto percent : { nlohmann::json(0), nlohmann::json(24), nlohmann::json(131), nlohmann::json(true), nlohmann::json("80"), nlohmann::json(nullptr) }) {
			changed = saved;
			changed["coveragePercent"] = percent;
			corrupt.push_back(changed);
		}
		for (const auto& json : corrupt) {
			auto retained = reference;
			bool rejected = false;
			try {
				json.get_to(retained);
			} catch (const std::exception&) {
				rejected = true;
			}
			Require(rejected && retained == reference, "Invalid persisted calibration must fail without replacing the reference");
		}
	}

	void CheckClipProjection()
	{
		struct ClipMatrix
		{
			double _11, _12, _13, _14, _21, _22, _23, _24, _31, _32, _33, _34, _41, _42, _43, _44;
		};
		const ClipMatrix matrix{ .93, .04, .03, .1, -.02, 1.03, -.04, .015, 0, 0, 1, 0, .08, .01, .02, 1 };
		const std::array<std::array<double, 4>, 4> rows{ std::array{ .93, .04, .03, .1 }, { -.02, 1.03, -.04, .015 }, { 0, 0, 1, 0 }, { .08, .01, .02, 1 } };
		const auto projection = FromClipProjection(matrix);
		for (Point uv : { Point{ 0, 0 }, Point{ 1, 0 }, Point{ 0, 1 }, Point{ 1, 1 }, Point{ .37, .61 } }) {
			const std::array clip{ uv.x * 2 - 1, 1 - uv.y * 2, .5, 1.0 };
			std::array<double, 4> mapped{};
			for (unsigned row = 0; row < 4; ++row)
				for (unsigned col = 0; col < 4; ++col)
					mapped[row] += rows[row][col] * clip[col];
			const auto actual = Project(projection, uv);
			Require(actual && std::abs(actual->x - (mapped[0] / mapped[3] * .5 + .5)) < 1e-12 &&
						std::abs(actual->y - (.5 - mapped[1] / mapped[3] * .5)) < 1e-12,
				"Calibration UV projection must match the shader clip projection");
			const auto restored = Project(*Inverse(projection), *actual);
			Require(restored && std::abs(restored->x - uv.x) < 1e-12 && std::abs(restored->y - uv.y) < 1e-12,
				"Both eye mappings must agree");
		}
	}

	void CheckReferenceCompatibility()
	{
		const Projection saved{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
		auto moved = saved;
		moved[2] = -0.001;
		Require(!MatchesProjection(saved, moved), "Projection drift larger than the fit margin must require recalibration");
		moved[2] = 1e-7;
		Require(MatchesProjection(saved, moved), "Numerical camera noise inside the coverage margin must be tolerated");
		for (double& value : moved)
			value *= 2;
		Require(MatchesProjection(saved, moved), "Equivalent homogeneous matrix scales must not invalidate calibration");
		moved = saved;
		moved[8] = 0;
		Require(!MatchesProjection(saved, moved), "An invalid live projection must fail closed");
		const Projection compressed{ .01, 0, .495, 0, 1, 0, 0, 0, 1 };
		auto compressedMoved = compressed;
		compressedMoved[2] += 1e-7;
		Require(!MatchesProjection(compressed, compressedMoved),
			"Projection drift must stay within the fit margin in both eye mappings");
		const Reference reference{ .version = FoveatedMaskCalibration::kReferenceVersion, .outer = { .scale = .6f }, .leftToRight = saved, .peripheryTaa = true };
		Require(MatchesProfile(reference, true, .3f, .05f), "Unchanged TAA controls must retain calibration");
		Require(!MatchesProfile(reference, false, .3f, .05f) && !MatchesProfile(reference, true, .4f, .05f) &&
					!MatchesProfile(reference, true, .3f, .06f),
			"Mode, centre and transition edits must invalidate TAA calibration");
	}

	bool Covered(const Reference& reference, const Solution& solution, unsigned eye, Point point)
	{
		if (solution.fullImage)
			return true;
		const auto& g = solution.geometry;
		const float center = reference.peripheryTaa ? reference.centerScale : g.scale;
		if (!reference.peripheryTaa)
			return FoveatedCommon::MaskDistanceUV(static_cast<float>(point.x), static_cast<float>(point.y), center,
					   g.horizontalScale, g.centers[eye * 2] - 0.5f, g.centers[eye * 2 + 1] - 0.5f) <= 1.0f;
		const double support = std::max(g.scale, center + 2.0f * std::max(reference.feather, 1e-4f));
		const double x = (point.x - g.centers[eye * 2]) / (support * g.horizontalScale * 0.5);
		const double y = (point.y - g.centers[eye * 2 + 1]) / (support * 0.5);
		return x * x * x * x + y * y * y * y <= 1.00001;
	}

	Solution CheckFit(const Reference& reference, float percent)
	{
		const auto solution = Solve(reference, percent);
		Require(solution.has_value(), "A valid FOV reference must have a safe result");
		const float visible = reference.peripheryTaa ? std::max(solution->geometry.scale, reference.centerScale + 2.0f * std::max(reference.feather, FoveatedCommon::kMinimumFeather)) : solution->geometry.scale;
		const auto verified = CheckCoverage(reference, percent, solution->geometry, visible, solution->fullImage, false);
		const auto shared = CheckCoverage(reference, percent, solution->geometry, visible, solution->fullImage, true);
		Require(verified.available && !verified.hasGap && shared.available && !shared.hasGap,
			"Every fitted mask must pass strict full-strength and shared-view coverage verification");
		const auto targets = BuildTargets(reference, percent);
		Require(targets.has_value(), "Targets must exist for a valid solution");
		for (unsigned eye = 0; eye < 2; ++eye) {
			const auto& polygon = (*targets)[eye];
			for (size_t i = 0; i < polygon.size(); ++i) {
				const Point start = polygon[i], end = polygon[(i + 1) % polygon.size()];
				for (unsigned sample = 0; sample <= 64; ++sample) {
					const double t = sample / 64.0;
					Require(Covered(reference, *solution, eye, { start.x + t * (end.x - start.x), start.y + t * (end.y - start.y) }),
						"All captured contours and filled centre must have full reconstruction coverage in both eyes");
				}
			}
			const double centerScale = reference.peripheryTaa ? reference.centerScale : solution->geometry.scale;
			const double expansion = centerScale * 0.5 * (solution->geometry.horizontalScale - 1.0) * (eye == 0 ? -1.0 : 1.0);
			const double manualX = solution->geometry.centers[eye * 2] - 0.5 - expansion;
			const float appliedX = 0.5f + FoveatedCommon::ResolveMaskOffsetX(static_cast<float>(manualX),
											  static_cast<float>(centerScale), solution->geometry.horizontalScale, eye != 0, true);
			Require(std::abs(appliedX - solution->geometry.centers[eye * 2]) < 1e-6,
				"A fitted centre must survive the live slider offset and saturation rules");
			Require(std::abs(manualX) <= 0.300001 && std::abs(solution->geometry.centers[eye * 2 + 1] - 0.5) <= 0.300001,
				"Fit must round-trip through existing manual offset limits");
		}
		if (percent == 100.0f && !reference.fullImage) {
			for (unsigned eye = 0; eye < 2; ++eye) {
				for (unsigned i = 0; i <= 512; ++i) {
					const double angle = -1.5707963267948966 + i * 3.141592653589793 / 512.0;
					const double cx = reference.outer.centers[eye * 2], cy = reference.outer.centers[eye * 2 + 1];
					const Point point{ cx + (eye == 0 ? -1 : 1) * reference.outer.scale * reference.outer.horizontalScale * 0.5 * std::sqrt(std::abs(std::cos(angle))),
						cy + std::copysign(reference.outer.scale * 0.5 * std::sqrt(std::abs(std::sin(angle))), std::sin(angle)) };
					if (point.x >= 0 && point.x <= 1 && point.y >= 0 && point.y <= 1)
						Require(Covered(reference, *solution, eye, point), "The analytic outward curve must remain covered in its own eye");
					const double innerRadius = std::max(0.0, (reference.innerLimits[eye] - cx) * (eye == 0 ? 1.0 : -1.0));
					const Point inner{ cx + (eye == 0 ? 1 : -1) * innerRadius * std::sqrt(std::abs(std::cos(angle))), point.y };
					if (inner.x >= 0 && inner.x <= 1 && inner.y >= 0 && inner.y <= 1)
						Require(Covered(reference, *solution, eye, inner), "The complete nasal curve must have full strength in its own eye");
				}
			}
		}
		return *solution;
	}
}

int main()
{
	CheckAutomaticWarnings();
	CheckInheritedBoundary();
	const auto started = std::chrono::steady_clock::now();
	Reference reference{ .version = FoveatedMaskCalibration::kReferenceVersion, .outer = { .scale = 0.6f, .horizontalScale = 1.0f }, .leftToRight = { 1, 0, 0, 0, 1, 0, 0, 0, 1 } };
	CheckPersistence(reference);
	CheckClipProjection();
	CheckReferenceCompatibility();
	Require(!Solve(Reference{}, 100), "An absent reference must not be interpreted as calibration");
	for (float percent : { 0.0f, 24.0f, 100.01f, std::numeric_limits<float>::quiet_NaN() })
		Require(!Solve(reference, percent), "Invalid scaling must fail closed");
	auto invalid = reference;
	invalid.leftToRight = {};
	Require(!Solve(invalid, 100), "Unknown stereo projection must fail closed");
	invalid = reference;
	invalid.outer.centers[0] = std::numeric_limits<float>::infinity();
	Require(!Solve(invalid, 100), "Nonfinite calibration values must fail closed");

	const auto baseline = CheckFit(reference, 100);
	const auto smaller = CheckFit(reference, 85);
	const auto smallest = CheckFit(reference, 50);
	Require(smallest.areaPercent < smaller.areaPercent && smaller.areaPercent < baseline.areaPercent,
		"Scaling must change processed area in the requested direction");
	Require(CheckFit(reference, 100).geometry == baseline.geometry, "Returning to 100 percent must restore the exact deterministic fit");

	// Dense analytic samples independently check preservation between circumscribed polygon vertices.
	for (unsigned eye = 0; eye < 2; ++eye) {
		for (unsigned i = 0; i <= 10000; ++i) {
			const double angle = -1.5707963267948966 + i * 3.141592653589793 / 10000.0;
			const double c = std::cos(angle), s = std::sin(angle);
			const Point point{ 0.5 + (eye == 0 ? -1 : 1) * 0.3 * std::sqrt(std::abs(c)),
				0.5 + std::copysign(0.3 * std::sqrt(std::abs(s)), s) };
			Require(Covered(reference, baseline, eye, point), "The complete curved reference, not just four extremes, must remain covered");
		}
	}

	reference.outer = { .scale = 0.4f, .horizontalScale = 1.0f, .centers = { .25f, .48f, .75f, .52f } };
	const auto gap = CheckFit(reference, 100);
	Require(Covered(reference, gap, 0, { .5, .5 }) && Covered(reference, gap, 1, { .5, .5 }),
		"Disjoint starting masks must gain central coverage");

	reference.outer = { .scale = 0.6f, .horizontalScale = 1.8f, .centers = { .6f, .5f, .4f, .5f } };
	const auto excess = CheckFit(reference, 100);
	Solution unoptimized{ .geometry = { .scale = .65f, .horizontalScale = reference.outer.horizontalScale } };
	const auto targets = *BuildTargets(reference, 100);
	float safeUnoptimizedArea = 0.0f;
	for (unsigned eye = 0; eye < 2; ++eye) {
		const float offset = FoveatedCommon::ResolveMaskOffsetX(eye == 0 ? kMaximumOffset : kMinimumOffset,
			unoptimized.geometry.scale, unoptimized.geometry.horizontalScale, eye != 0, true);
		unoptimized.geometry.centers[eye * 2] = .5f + offset;
		for (const Point point : targets[eye])
			Require(Covered(reference, unoptimized, eye, point), "The unoptimized area baseline must fully reconstruct both complete targets");
		safeUnoptimizedArea += FoveatedMaskVisualization::MeasureCoverage(unoptimized.geometry.scale,
								   reference.feather, unoptimized.geometry.horizontalScale, offset, 0, false, 1)
		                           .totalPercent *
		                       .5f;
	}
	Require(!excess.fullImage && excess.areaPercent < safeUnoptimizedArea,
		"Fitting must reduce excess inward coverage relative to a fully reconstructed baseline within the live controls");

	for (const Projection projection : { Projection{ 1, 0, .15, 0, 1, 0, 0, 0, 1 },
			 Projection{ .93, .04, .12, -.02, 1.03, .015, .08, .01, 1 } }) {
		reference.leftToRight = projection;
		reference.outer = { .scale = .58f, .horizontalScale = 1.3f, .centers = { .43f, .47f, .57f, .53f } };
		for (float percent : { 25.0f, 70.0f, 100.0f })
			CheckFit(reference, percent);
		reference.peripheryTaa = true;
		reference.centerScale = .3f;
		CheckFit(reference, 100);
		reference.peripheryTaa = false;
	}
	// A canted headset can put the other eye's image corners behind its projection plane.
	const double angle = 40.0 * 3.141592653589793 / 180.0;
	const double c = std::cos(angle), t = std::sin(angle) * std::tan(55.0 * 3.141592653589793 / 180.0);
	const Projection canted{ c + t, 0, -t, t, 1, (c - t - 1) * 0.5, 2 * t, 0, c - t };
	Require(!Project(canted, { 0, 0 }), "Canted fixture must include a monocular corner behind the peer view");
	Require(MatchesProjection(canted, canted), "Unchanged canted projections must remain usable");
	reference = { .version = FoveatedMaskCalibration::kReferenceVersion, .outer = { .scale = .6f }, .leftToRight = canted };
	for (float percent : { 25.0f, 70.0f, 100.0f })
		CheckFit(reference, percent);
	reference.fullImage = true;
	Require(CheckFit(reference, 100).fullImage, "A canted full-view reference must preserve both full eye images");

	std::mt19937 random(20261009);
	const auto range = [&](float low, float high) { return std::uniform_real_distribution<float>(low, high)(random); };
	for (unsigned sample = 0; sample < 24; ++sample) {
		reference = { .version = FoveatedMaskCalibration::kReferenceVersion, .outer = { .scale = range(.35f, 1.1f), .horizontalScale = range(1, 2), .centers = { range(.2f, .8f), range(.2f, .8f), range(.2f, .8f), range(.2f, .8f) } }, .leftToRight = { 1, range(-.04f, .04f), range(-.2f, .2f), range(-.04f, .04f), 1, range(-.05f, .05f), 0, 0, 1 }, .feather = range(0, .1f) };
		const auto retained = reference;
		CheckFit(reference, 100);
		Require(reference == retained, "Optimization must leave the outer reference immutable");
	}

	reference = { .version = FoveatedMaskCalibration::kReferenceVersion, .outer = { .scale = .4002f, .horizontalScale = 1.0f }, .leftToRight = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, .peripheryTaa = true, .centerScale = .4f, .feather = 0 };
	CheckFit(reference, 100);
	reference = { .version = FoveatedMaskCalibration::kReferenceVersion, .outer = { .scale = 1.1f, .horizontalScale = 2.0f, .centers = { .35f, .5f, .65f, .5f } }, .leftToRight = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, .peripheryTaa = true, .centerScale = .9f, .feather = .1f };
	reference.fullImage = true;
	const auto fullTaa = CheckFit(reference, 100);
	Require(!fullTaa.fullImage && fullTaa.areaPercent > 99.99f,
		"A feasible TAA fit covering the entire image must be accepted without changing its centre scale");

	reference.peripheryTaa = false;
	reference.fullImage = true;
	reference.leftToRight = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	Require(CheckFit(reference, 100).fullImage, "A rectangular full-view reference must retain safe full coverage");
	std::cout << "PASS: outer contours, central gaps, excess overlap, stereo projection, scaling, restoration, bounds and TAA ("
			  << std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() << " seconds)\n";
}
