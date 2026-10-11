#include "Features/Upscaling/FoveatedHeadsetGeometry.h"
#include "Features/Upscaling/FoveatedMaskCalibrationJson.h"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace globals::game
{
	bool isVR = true;
}
namespace vr
{
	enum EVREye
	{
		Eye_Left,
		Eye_Right
	};
	enum EHiddenAreaMeshType
	{
		k_eHiddenAreaMesh_LineLoop,
		k_eHiddenAreaMesh_Inverse
	};
	struct HmdVector2_t
	{
		float v[2];
	};
	struct HiddenAreaMesh_t
	{
		const HmdVector2_t* pVertexData = nullptr;
		uint32_t unTriangleCount = 0;
	};
	struct HmdMatrix34_t
	{
		float m[3][4]{};
	};
}
struct HeadsetSystem
{
	std::array<std::array<float, 4>, 2> tangents{};
	std::array<FoveatedMaskCalibration::Projection, 2> rotations{};
	std::array<vr::HiddenAreaMesh_t, 2> loops{}, inverse{};
	std::array<uint32_t, 2> loopReads{}, inverseReads{};
	bool provideProjection = true;
	void GetProjectionRaw(vr::EVREye eye, float* left, float* right, float* bottom, float* top) const
	{
		if (provideProjection) {
			*left = tangents[eye][0];
			*right = tangents[eye][1];
			*bottom = tangents[eye][2];
			*top = tangents[eye][3];
		}
	}
	vr::HmdMatrix34_t GetEyeToHeadTransform(vr::EVREye eye) const
	{
		vr::HmdMatrix34_t result{};
		for (unsigned row = 0; row < 3; ++row)
			for (unsigned column = 0; column < 3; ++column)
				result.m[row][column] = static_cast<float>(rotations[eye][row * 3 + column]);
		return result;
	}
	vr::HiddenAreaMesh_t GetHiddenAreaMesh(vr::EVREye eye, vr::EHiddenAreaMeshType type)
	{
		if (type == vr::k_eHiddenAreaMesh_LineLoop) {
			++loopReads[eye];
			return loops[eye];
		}
		++inverseReads[eye];
		return inverse[eye];
	}
};
namespace RE
{
	struct BSOpenVR
	{
		inline static BSOpenVR* singleton = nullptr;
		HeadsetSystem* vrSystem = nullptr;
		static BSOpenVR* GetSingleton() { return singleton; }
	};
}

namespace
{
#include "foveated_headset_geometry_under_test.h"
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

	void CheckRuntimeCapture()
	{
		HeadsetSystem system;
		system.tangents = optics;
		system.rotations = { identity, identity };
		RE::BSOpenVR runtime{ &system };
		RE::BSOpenVR::singleton = &runtime;
		const std::array<vr::HmdVector2_t, 4> loop{
			vr::HmdVector2_t{ .04f, .5f }, { .5f, 0 }, { .953f, .5f }, { .5f, 1 }
		};
		const std::array<vr::HmdVector2_t, 6> triangles{ loop[0], loop[1], loop[2], loop[0], loop[2], loop[3] };
		system.loops.fill({ loop.data(), uint32_t(loop.size()) });
		const auto baseline = GetFoveatedHeadsetGeometry();
		Require(baseline && baseline->innerLimits == std::array{ .953f, .04f }, "Runtime capture must retain valid per-eye visibility bounds");
		Require(system.inverseReads == std::array<uint32_t, 2>{}, "Valid line loops must not request inverse meshes");
		const std::array<vr::HiddenAreaMesh_t, 4> invalidLoops{
			vr::HiddenAreaMesh_t{ nullptr, 1 }, { loop.data(), 12289 }, { loop.data(), 1 }, { nullptr, 0 }
		};
		for (const auto mesh : invalidLoops) {
			system.loops[0] = mesh;
			system.inverse[0] = { triangles.data(), 2 };
			const auto fallback = GetFoveatedHeadsetGeometry();
			Require(fallback && fallback->leftToRight == baseline->leftToRight && fallback->outlines == baseline->outlines,
				"Missing, malformed or oversized line loops must retry the valid inverse mesh without losing optics");
		}
		auto corrupt = loop;
		for (float coordinate : { 2.0f, std::numeric_limits<float>::quiet_NaN() }) {
			corrupt[0].v[0] = coordinate;
			system.loops[0] = { corrupt.data(), uint32_t(corrupt.size()) };
			const auto fallback = GetFoveatedHeadsetGeometry();
			Require(fallback && fallback->outlines == baseline->outlines, "Invalid mesh coordinates must retry the inverse mesh");
		}
		system.loops[0] = { nullptr, 1 };
		system.inverse[0] = { triangles.data(), 4097 };
		const auto fullEye = GetFoveatedHeadsetGeometry();
		const Polygon rectangle{ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
		Require(fullEye && fullEye->innerLimits[0] == 1 && fullEye->outlines[0] == rectangle &&
					fullEye->innerLimits[1] == baseline->innerLimits[1] && fullEye->outlines[1] == baseline->outlines[1],
			"Unusable visibility meshes must conservatively cover only the affected eye and keep calibration available");
		Reference reference{ .version = kReferenceVersion, .leftToRight = fullEye->leftToRight, .innerLimits = fullEye->innerLimits, .centerScale = 1.0f };
		reference.headsetOutlines = fullEye->outlines;
		for (float feather : { .05f, 0.0f }) {
			reference.feather = feather;
			for (float percent : { 100.0f, 70.0f }) {
				const auto fit = Solve(reference, percent);
				Require(fit && !CheckCoverage(reference, percent, fit->geometry, fit->geometry.scale, fit->fullImage, false).hasGap,
					"Mesh fallback must support automatic setup and outer scaling with feathered or hard boundaries");
			}
		}
		system.loopReads = system.inverseReads = {};
		Require(GetFoveatedHeadsetGeometry(false).has_value() && system.loopReads == std::array<uint32_t, 2>{} && system.inverseReads == std::array<uint32_t, 2>{},
			"Preview projection must never access visibility meshes");
		system.tangents[0][0] = std::numeric_limits<float>::quiet_NaN();
		Require(!GetFoveatedHeadsetGeometry(), "Invalid optics must not be replaced with invented projection data");
		system.tangents = optics;
		system.provideProjection = false;
		Require(!GetFoveatedHeadsetGeometry(), "A runtime that omits projection outputs must not leave uninitialized optics");
		system.provideProjection = true;
		system.rotations[1] = {};
		Require(!GetFoveatedHeadsetGeometry(), "Missing eye rotations must fail closed even with mesh fallback");
		system.rotations = { identity, identity };
		runtime.vrSystem = nullptr;
		Require(!GetFoveatedHeadsetGeometry(), "An unavailable VR system must fail closed");
		RE::BSOpenVR::singleton = nullptr;
		Require(!GetFoveatedHeadsetGeometry(), "An unavailable VR singleton must fail closed");
		globals::game::isVR = false;
		Require(!GetFoveatedHeadsetGeometry(), "Flat runtimes must not access headset interfaces");
		globals::game::isVR = true;
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
			for (float percent : { 50.0f, 70.0f, 100.0f, 130.0f }) {
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
	void CheckAutomaticHeadsetFit()
	{
		struct Vertex
		{
			float v[2];
		};
		Reference reference{ .version = kReferenceVersion, .leftToRight = *FoveatedHeadsetGeometry::BuildProjection(optics, { identity, identity }), .innerLimits = { .953f, .047f }, .centerScale = 1.0f };
		const std::array<Polygon, 2> visible{ Polygon{ { 0.947000027, 0.356000006 }, { 0.941999972, 0.444000006 }, { 0.940999985, 0.531000018 }, { 0.945999980, 0.613000035 }, { 0.953000009, 0.698000014 }, { 0.913999975, 0.787000000 }, { 0.846000016, 0.870999992 }, { 0.771000028, 0.930000007 }, { 0.670000017, 0.976999998 }, { 0.560999989, 1.000000000 }, { 0.416999996, 1.000000000 }, { 0.294000000, 0.964999974 }, { 0.192000002, 0.903999984 }, { 0.128999993, 0.848999977 }, { 0.071000002, 0.768999994 }, { 0.041999999, 0.692999959 }, { 0.048999999, 0.574000001 }, { 0.050999999, 0.503000021 }, { 0.048000000, 0.452000022 }, { 0.043000001, 0.380999982 }, { 0.039000001, 0.310000002 }, { 0.086000003, 0.209999978 }, { 0.172000006, 0.111999989 }, { 0.282000005, 0.044000030 }, { 0.437000006, 0.000000000 }, { 0.561999977, 0.000000000 }, { 0.750999987, 0.055999994 } },
			Polygon{ { 0.052999973, 0.356000006 }, { 0.058000028, 0.444000006 }, { 0.059000015, 0.531000018 }, { 0.054000020, 0.613000035 }, { 0.046999991, 0.698000014 }, { 0.086000025, 0.787000000 }, { 0.153999984, 0.870999992 }, { 0.228999972, 0.930000007 }, { 0.329999983, 0.976999998 }, { 0.439000010, 1.000000000 }, { 0.583000004, 1.000000000 }, { 0.705999970, 0.964999974 }, { 0.807999968, 0.903999984 }, { 0.870999992, 0.848999977 }, { 0.929000020, 0.768999994 }, { 0.958000004, 0.692999959 }, { 0.950999975, 0.574000001 }, { 0.949000001, 0.503000021 }, { 0.952000022, 0.452000022 }, { 0.957000017, 0.380999982 }, { 0.961000025, 0.310000002 }, { 0.913999975, 0.209999978 }, { 0.828000009, 0.111999989 }, { 0.717999995, 0.044000030 }, { 0.562999964, 0.000000000 }, { 0.438000023, 0.000000000 }, { 0.249000013, 0.055999994 } } };
		for (unsigned eye = 0; eye < 2; ++eye) {
			std::vector<Vertex> vertices;
			for (auto point : visible[eye])
				vertices.push_back({ float(point.x), float(point.y) });
			const auto outline = FoveatedHeadsetGeometry::VisibleOutline(vertices.data(), uint32_t(vertices.size()));
			Require(outline.has_value(), "The measured visible mesh must yield a conservative outline");
			reference.headsetOutlines[eye] = *outline;
		}
		const auto saved = reference;
		const auto baseline = Solve(reference, 100);
		Require(baseline && baseline->fullImage, "Full-height headset optics require conservative full-eye startup with current controls");
		Require(nlohmann::json::parse(nlohmann::json(reference).dump()).get<Reference>() == reference,
			"Automatic headset outlines must survive save/load exactly");
		for (float feather : { 0.0f, .05f, .1f }) {
			reference.feather = feather;
			float previousArea = 0;
			for (float percent : { 50.0f, 65.0f, 80.0f, 100.0f, 130.0f }) {
				const auto fit = Solve(reference, percent);
				Require(fit.has_value(), "Headset outer scaling must always have a conservative result");
				Require(fit->areaPercent >= previousArea - .02f, "Increasing headset outer coverage cannot reduce area");
				previousArea = fit->areaPercent;
				if (percent <= 65)
					Require(!fit->fullImage && fit->areaPercent < 90, "Reducing outer coverage must escape the initial full-eye fit");
				const auto coverage = CheckCoverage(reference, percent, fit->geometry, fit->geometry.scale, fit->fullImage, false);
				Require(coverage.available && !coverage.hasGap, "Automatically fitted headset contours must be covered at full strength");
				for (unsigned eye = 0; eye < 2; ++eye) {
					const auto& polygon = visible[eye];
					for (size_t edge = 0; edge < polygon.size(); ++edge) {
						const auto a = polygon[edge], b = polygon[(edge + 1) % polygon.size()];
						for (unsigned sample = 0; sample <= 128; ++sample) {
							const double t = sample / 128.0;
							Point point{ a.x + t * (b.x - a.x), a.y + t * (b.y - a.y) };
							if ((point.x < .5) == (eye == 0))
								point.x = .5 + (point.x - .5) * percent / 100;
							point.y = .5 + (point.y - .5) * percent / 100;
							if (point.x >= 0 && point.x <= 1 && point.y >= 0 && point.y <= 1)
								Require(fit->fullImage || Distance(fit->geometry, eye, point) <= 1.00001,
									"Every transformed mesh edge, including nasal edges and seam crossings, must retain full core coverage");
						}
					}
				}
				std::cout << "Headset fit feather=" << feather << " coverage=" << percent << " saved=" << 100 - fit->areaPercent << '\n';
			}
			Require(Solve(reference, 100)->geometry == baseline->geometry, "100 percent must restore the initial headset setup");
		}
		reference.feather = saved.feather;
		Require(reference == saved, "Automatic scaling must not rewrite the original headset outline");
		const auto fallback = FoveatedHeadsetGeometry::VisibleOutline<Vertex>(nullptr, 0);
		Require(fallback && fallback->size() == 4, "Absent visibility data must provide full-eye bounds");
		auto corrupt = nlohmann::json(reference);
		for (auto invalid : { nlohmann::json{ true, .5 }, nlohmann::json{ 2, .5 }, nlohmann::json{ .5 } }) {
			corrupt["headsetOutlines"][0][0] = invalid;
			auto retained = reference;
			bool rejected = false;
			try {
				corrupt.get_to(retained);
			} catch (const std::exception&) {
				rejected = true;
			}
			Require(rejected && retained == reference, "Corrupt saved headset outlines must be rejected transactionally");
		}
		std::vector<Vertex> detailed;
		for (unsigned i = 0; i < 1024; ++i) {
			const double a = i * 6.283185307179586 / 1024;
			detailed.push_back({ float(.5 + .45 * std::cos(a)), float(.5 + .45 * std::sin(a)) });
		}
		const auto bounded = FoveatedHeadsetGeometry::VisibleOutline(detailed.data(), uint32_t(detailed.size()));
		Require(bounded && bounded->size() <= kMaximumHeadsetOutlineVertices, "Detailed driver meshes must be reduced to bounded conservative outlines");
		for (const auto& vertex : detailed) {
			for (size_t i = 0; i < bounded->size(); ++i) {
				const auto a = (*bounded)[i], b = (*bounded)[(i + 1) % bounded->size()];
				Require((b.x - a.x) * (vertex.v[1] - a.y) - (b.y - a.y) * (vertex.v[0] - a.x) >= -1e-8,
					"Contour reduction must never cut off an original visible vertex");
			}
		}
	}

}

int main()
{
	CheckOptics();
	CheckNasalLimits();
	CheckRuntimeCapture();
	CheckManualRegression();
	CheckAutomaticHeadsetFit();
	std::cout << "PASS: headset optics, preview projection, nasal limits, independent eyes, manual regression, outer scaling and hard edges\n";
}
