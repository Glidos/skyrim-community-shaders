#include "Features/Upscaling/FoveatedHeadsetGeometry.h"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
	using namespace FoveatedMaskCalibration;
	constexpr Projection identity{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	constexpr std::array<std::array<float, 4>, 2> optics{
		std::array{ -1.541454434f, 1.050492644f, -1.412014484f, 1.407404184f },
		std::array{ -1.050125599f, 1.541308999f, -1.410493255f, 1.407939792f }
	};

	void Require(bool condition, const char* message)
	{
		if (!condition) {
			std::cerr << message << '\n';
			std::exit(1);
		}
	}

	struct ClipMatrix
	{
		float _11, _12, _13, _14, _21, _22, _23, _24, _31, _32, _33, _34, _41, _42, _43, _44;
	};

	void CheckOptics()
	{
		using FoveatedHeadsetGeometry::BuildProjection;
		const auto aligned = BuildProjection(optics, { identity, identity });
		Require(aligned && (*aligned)[2] < -.18 && (*aligned)[2] > -.20,
			"The asymmetric headset must map left-eye UV toward the right eye's nasal edge");
		for (double angle : { 0.0, .15, -.35 }) {
			const double c = std::cos(angle), s = std::sin(angle);
			const std::array<Projection, 2> rotations{ Projection{ c, 0, s, 0, 1, 0, -s, 0, c }, identity };
			const auto mapping = BuildProjection(optics, rotations);
			Require(mapping.has_value(), "Valid canted headset optics must be accepted");
			const auto clip = FoveatedHeadsetGeometry::ToClipProjection<ClipMatrix>(*mapping);
			for (Point uv : { Point{ 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 }, { .37, .61 } }) {
				// Independently unproject a left-eye ray, rotate it and project through the right optics.
				const double x = optics[0][0] + uv.x * double(optics[0][1] - optics[0][0]);
				const double y = optics[0][3] + uv.y * double(optics[0][2] - optics[0][3]);
				const double forward = s * x + c;
				const Point expected{ ((c * x - s) / forward - optics[1][0]) / double(optics[1][1] - optics[1][0]),
					(optics[1][3] - y / forward) / double(optics[1][3] - optics[1][2]) };
				const auto projected = Project(*mapping, uv);
				Require(projected && std::abs(projected->x - expected.x) < 1e-7 && std::abs(projected->y - expected.y) < 1e-7,
					"The calibration mapping must match independent optical ray projection");
				const double px = uv.x * 2 - 1, py = 1 - uv.y * 2;
				const double w = clip._41 * px + clip._42 * py + clip._44;
				const Point preview{ .5 + .5 * (clip._11 * px + clip._12 * py + clip._14) / w,
					.5 - .5 * (clip._21 * px + clip._22 * py + clip._24) / w };
				Require(std::abs(preview.x - expected.x) < 2e-6 && std::abs(preview.y - expected.y) < 2e-6,
					"The actual column-vector preview transform must agree with headset optics");
			}
		}
		auto invalid = optics;
		invalid[0][0] = std::numeric_limits<float>::quiet_NaN();
		Require(!BuildProjection(invalid, { identity, identity }), "Nonfinite optics must fail closed");
		invalid = optics;
		invalid[1][1] = invalid[1][0];
		Require(!BuildProjection(invalid, { identity, identity }), "Zero-width optics must fail closed");
		auto rotation = identity;
		rotation[0] = 2;
		Require(!BuildProjection(optics, { rotation, identity }), "Non-rotational eye transforms must fail closed");
		Require(!BuildProjection(optics, { Projection{}, identity }), "Unavailable optics must not become identity optics");
	}

	void CheckNasalLimits()
	{
		using FoveatedHeadsetGeometry::InnerLimit;
		struct Vertex
		{
			float v[2];
		};
		const std::array<Vertex, 4> vertices{ Vertex{ .04f, .5f }, { .5f, 0 }, { .953f, .5f }, { .5f, 1 } };
		Require(InnerLimit(vertices.data(), 4, 0) == .953f && InnerLimit(vertices.data(), 4, 1) == .04f,
			"Each eye must use its own most nasal visible extent");
		Require(InnerLimit<Vertex>(nullptr, 0, 0) == 1.0f && InnerLimit<Vertex>(nullptr, 0, 1) == 0.0f,
			"Absent visibility meshes must conservatively cover the full nasal extent");
		Require(!InnerLimit<Vertex>(nullptr, 1, 0) && !InnerLimit(vertices.data(), 12289, 0) && !InnerLimit(vertices.data(), 4, 2),
			"Invalid mesh pointers, counts and eye indices must be rejected before reading vertices");
		auto corrupt = vertices;
		corrupt[1].v[1] = std::numeric_limits<float>::infinity();
		Require(!InnerLimit(corrupt.data(), 4, 0), "Invalid mesh coordinates must fail closed");
		corrupt[1].v[1] = 2;
		Require(!InnerLimit(corrupt.data(), 4, 0), "Out-of-range mesh coordinates must fail closed");
		Require(!InnerLimit(vertices.data(), 1, 0), "Degenerate visibility meshes cannot certify coverage");
	}

	double Distance(const Geometry& geometry, unsigned eye, Point uv, float padding = 0)
	{
		const double ry = geometry.scale * .5 + padding;
		const double x = (uv.x - geometry.centers[eye * 2]) / (ry * geometry.horizontalScale);
		const double y = (uv.y - geometry.centers[eye * 2 + 1]) / ry;
		return x * x * x * x + y * y * y * y;
	}

	float Area(const Geometry& geometry, float feather)
	{
		float area = 0;
		for (unsigned eye = 0; eye < 2; ++eye)
			area += FoveatedMaskVisualization::MeasureCoverage(geometry.scale, feather, geometry.horizontalScale,
						geometry.centers[eye * 2] - .5f, geometry.centers[eye * 2 + 1] - .5f, false, 1)
			            .totalPercent *
			        .5f;
		return area;
	}

	Reference ManualFit()
	{
		Reference reference{ .version = kReferenceVersion,
			.outer = { .scale = .66f, .horizontalScale = 1.1f, .centers = { 0, .604f, 0, .497f } },
			.leftToRight = *FoveatedHeadsetGeometry::BuildProjection(optics, { identity, identity }),
			.innerLimits = { .953f, .047f },
			.centerScale = .66f,
			.feather = .05f };
		reference.outer.centers[0] = .5f + FoveatedCommon::ResolveMaskOffsetX(.105f, .66f, 1.1f, false, true);
		reference.outer.centers[2] = .5f + FoveatedCommon::ResolveMaskOffsetX(-.168f, .66f, 1.1f, true, true);
		return reference;
	}

	void CheckIndependentEyes(const Reference& reference)
	{
		const auto baseline = *BuildTargets(reference, 100);
		for (unsigned eye = 0; eye < 2; ++eye) {
			auto changed = reference;
			const auto peer = 1 - eye;
			changed.outer.centers[peer * 2] = eye == 0 ? .2f : .8f;
			changed.outer.centers[peer * 2 + 1] = .8f;
			const auto target = *BuildTargets(changed, 100);
			Require(target[eye].size() == baseline[eye].size(), "Peer offsets must not change an eye's target");
			for (size_t i = 0; i < target[eye].size(); ++i)
				Require(target[eye][i].x == baseline[eye][i].x && target[eye][i].y == baseline[eye][i].y,
					"Oversized or displaced peer coverage must not redefine this eye's fitted edges");
		}
	}

	void CheckContours(const Reference& reference, const Solution& solution, float percent)
	{
		const auto coverage = CheckCoverage(reference, percent, solution.geometry, solution.geometry.scale, solution.fullImage, false);
		Require(coverage.available && !coverage.hasGap, "Fitted slider geometry must pass coverage validation");
		if (solution.fullImage)
			return;
		for (unsigned eye = 0; eye < 2; ++eye) {
			const double cx = .5 + (reference.outer.centers[eye * 2] - .5) * percent / 100;
			const double cy = .5 + (reference.outer.centers[eye * 2 + 1] - .5) * percent / 100;
			const double radius = reference.outer.scale * .5 * percent / 100;
			const double sign = eye == 0 ? -1 : 1;
			const double nasal = std::max(0.0, (cx - reference.innerLimits[eye]) * sign);
			for (unsigned i = 0; i <= 2000; ++i) {
				const double angle = -1.5707963267948966 + i * 3.141592653589793 / 2000;
				const double x = std::sqrt(std::abs(std::cos(angle)));
				const double y = std::copysign(std::sqrt(std::abs(std::sin(angle))), std::sin(angle));
				for (unsigned contour = 0; contour < 3; ++contour) {
					const double padding = contour == 1 ? reference.feather : 0;
					const double rx = contour == 2 ? -nasal : (radius + padding) * reference.outer.horizontalScale;
					const Point point{ cx + sign * rx * x, cy + (radius + padding) * y };
					if (point.x >= 0 && point.x <= 1 && point.y >= 0 && point.y <= 1)
						Require(Distance(solution.geometry, eye, point, static_cast<float>(padding)) <= 1.00001,
							"Dense independent curves must retain outer core, feather support and complete nasal coverage");
				}
			}
		}
	}

	void CheckManualRegression()
	{
		auto reference = ManualFit();
		CheckIndependentEyes(reference);
		for (float feather : { .05f, 0.0f, .01f }) {
			reference.feather = feather;
			const auto retained = reference;
			const auto fit = Solve(reference, 100);
			Require(fit && !fit->fullImage, "The practical manual fit must not collapse to full-eye coverage");
			CheckContours(reference, *fit, 100);
			Require(fit->areaPercent < 60 && fit->areaPercent <= Area(reference.outer, feather) + 2,
				"Automatic calibration must retain at least 40 percent savings and stay near the manual baseline");
			for (unsigned eye = 0; eye < 2; ++eye) {
				for (unsigned y = 0; y < 300; ++y) {
					for (unsigned x = 0; x < 300; ++x) {
						const Point uv{ (x + .5) / 300, (y + .5) / 300 };
						if (Distance(reference.outer, eye, uv) <= 1)
							Require(Distance(fit->geometry, eye, uv) <= 1,
								"Neither eye may lose any working full-strength coverage in the known manual fixture");
					}
				}
			}
			float previousArea = 0;
			for (float percent : { 70.0f, 100.0f, 130.0f }) {
				const auto scaled = Solve(reference, percent);
				Require(scaled.has_value(), "Automatic outer scaling must remain feasible");
				CheckContours(reference, *scaled, percent);
				Require(scaled->areaPercent > previousArea, "Larger outer scaling must increase coverage");
				previousArea = scaled->areaPercent;
			}
			Require(Solve(reference, 100)->geometry == fit->geometry && reference == retained,
				"Scaling must preserve the reference and restore the deterministic fit at 100 percent");
			std::cout << "Manual regression feather=" << feather << " manualSaved=" << 100 - Area(reference.outer, feather)
					  << " calibratedSaved=" << 100 - fit->areaPercent << " scale=" << fit->geometry.scale
					  << " horizontal=" << fit->geometry.horizontalScale << " centres=" << fit->geometry.centers[0] << ','
					  << fit->geometry.centers[1] << ',' << fit->geometry.centers[2] << ',' << fit->geometry.centers[3] << '\n';
		}
	}
}

int main()
{
	CheckOptics();
	CheckNasalLimits();
	CheckManualRegression();
	std::cout << "PASS: headset optics, preview projection, nasal limits, independent eyes, manual regression, outer scaling and hard edges\n";
}
