#include "../src/Features/Upscaling/NeuralRendering/PackedCropPolicy.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "../src/Features/Upscaling/NeuralRendering/PackedCropEvidenceJson.h"
#endif

#include <iostream>
#include <limits>
#include <stdexcept>

using namespace NeuralRendering;

static void Require(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << std::endl;
		throw std::runtime_error(message);
	}
}

int main()
{
	constexpr ComputeSubrect full{ 0, 0, 2048, 1024 };
	std::array actors{ CharacterRect{ 200, 300, 280, 400 }, CharacterRect{ 1500, 300, 1580, 400 } };
	const std::array closeActors{ CharacterRect{ 96, 160, 176, 320 }, CharacterRect{ 208, 160, 288, 320 },
		CharacterRect{ 320, 160, 400, 320 }, CharacterRect{ 432, 160, 512, 320 } };
	const auto tight = BuildPackedCropSources(closeActors, 2048, 1024, full);
	Require(tight.count == 4, "Neighbouring separated masks were merged by unnecessary context or alignment");
	for (unsigned i = 0; i < tight.count; ++i)
		Require(tight.regions[i] == BuildCharacterComputeSubrect(std::span(closeActors).subspan(i, 1), 2048, 1024),
			"Tight crops expanded beyond guarded sampling support");
	StablePackedCropSources state;
	const auto initial = ResolvePackedCropSources(actors, 2048, 1024, full, state);
	Require(initial.count == 2, "Separated actors were not packed");
	for (const auto& actor : actors) {
		const auto support = BuildCharacterComputeSubrect(std::span(&actor, 1), 2048, 1024);
		Require(std::ranges::any_of(initial.regions, [&](auto rect) { return ContainsComputeSubrect(rect, support); }), "Selected actor coverage was discarded");
	}
	std::ranges::reverse(actors);
	Require(BuildPackedCropSources(actors, 2048, 1024, full) == initial, "Input enumeration changed the initial mapping");
	actors[0].minX += 1;
	actors[0].maxX += 1;
	for (unsigned i = 0; i < 120; ++i)
		Require(ResolvePackedCropSources(actors, 2048, 1024, full, state) == initial, "Contained motion churned the atlas");
	StablePackedCropSources contextState{ initial };
	auto contextMotion = actors;
	contextMotion[1].minX += 7;
	contextMotion[1].maxX += 7;
	const auto& movedActor = contextMotion[1];
	const ComputeSubrect movedPixels{ movedActor.minX, movedActor.minY, movedActor.maxX - movedActor.minX, movedActor.maxY - movedActor.minY };
	Require(std::ranges::any_of(initial.regions, [&](auto r) { return ContainsComputeSubrect(r, movedPixels); }), "Guard-motion fixture left the selected-pixel crop");
	const auto grown = ResolvePackedCropSources(contextMotion, 2048, 1024, full, contextState);
	Require(grown != initial && grown.count == 2, "Selected pixels inside the atlas concealed a sampling-guard escape");
	for (const auto& actor : contextMotion) {
		const auto required = BuildPackedCropContext(actor, 2048, 1024, full);
		Require(std::ranges::any_of(grown.regions, [&](auto r) { return ContainsComputeSubrect(r, required); }), "Atlas growth discarded required context");
	}
	StablePackedCropSources singleState{ initial };
	for (unsigned i = 1; i < kCharacterProviderRoiHistoryFrames; ++i)
		Require(ResolvePackedCropSources(std::span(actors).first(1), 2048, 1024, full, singleState) == initial, "A transient single actor churned atlas ownership");
	Require(!ResolvePackedCropSources(std::span(actors).first(1), 2048, 1024, full, singleState).count, "A departed actor retained an unused tile indefinitely");
	StablePackedCropSources transientState{ initial };
	for (unsigned i = 1; i < kCharacterProviderRoiHistoryFrames; ++i)
		(void)ResolvePackedCropSources(actors, 2048, 1024, full, transientState);
	Require(ResolvePackedCropSources(std::span(actors).first(1), 2048, 1024, full, transientState) == initial, "A one-frame disappearance at the periodic check retired the atlas");
	for (unsigned i = 0; i < 2 * kCharacterProviderRoiHistoryFrames; ++i)
		Require(ResolvePackedCropSources(i % 2 ? std::span(actors) : std::span(actors).first(1), 2048, 1024, full, transientState) == initial, "Intermittent selection churned the retained atlas");
	actors[0] = { 1800, 700, 1900, 800 };
	Require(ResolvePackedCropSources(actors, 2048, 1024, full, state) != initial, "Escaped selection reused stale history coordinates");
	Require(!ResolvePackedCropSources({}, 2048, 1024, full, state).count && !state.sources.count, "Empty selection retained an atlas");
	for (unsigned percent : { 30u, 75u, 100u }) {
		const UpscalingDLSS::Extent model{ (2048 * percent + 99) / 100, (1024 * percent + 99) / 100 };
		const auto layout = BuildPackedCropLayout(initial, { 2048, 1024 }, model, full);
		Require(layout.count == 2, "Admissible sparse layout was rejected");
		Require(std::uint64_t(layout.extent.width) * layout.extent.height * 4 <= std::uint64_t(model.width) * model.height * 3, "Padding removed the required area saving");
		Require(!IntersectComputeSubrect(layout.tiles[0].packed, layout.tiles[1].packed).IsValid(), "Atlas tiles overlap");
		for (unsigned i = 0; i < layout.count; ++i)
			Require(layout.tiles[i].packed.Fits(layout.extent.width, layout.extent.height), "Tile exceeds admitted texture size");
	}
	PackedCropSources dense{ { ComputeSubrect{ 0, 0, 1024, 1024 }, ComputeSubrect{ 1024, 0, 1024, 1024 } }, 2 };
	Require(!BuildPackedCropLayout(dense, { 2048, 1024 }, { 2048, 1024 }, full).count, "Dense contexts were packed without a saving");
	auto malformed = initial;
	malformed.count = 5;
	Require(!BuildPackedCropLayout(malformed, { 2048, 1024 }, { 2048, 1024 }, full).count, "Unbounded tile count was admitted");
	malformed = initial;
	malformed.regions[1] = malformed.regions[0];
	Require(!BuildPackedCropLayout(malformed, { 2048, 1024 }, { 2048, 1024 }, full).count, "Ambiguous source ownership was admitted");
	Require(!BuildPackedCropSources(actors, UINT32_MAX, 1024, { 0, 0, UINT32_MAX, 1024 }).count, "Oversize source escaped validation");
	Require(!BuildPackedCropLayout(initial, { UINT32_MAX, 1024 }, { 2048, 1024 }, full).count, "Invalid source dimension reached layout arithmetic");
	std::vector<CharacterRect> crowded;
	for (unsigned x = 100; x < 500; x += 32)
		crowded.push_back({ x, 300, x + 40, 400 });
	Require(!BuildPackedCropSources(crowded, 2048, 1024, full).count, "Overlapping context chain did not merge to one enclosing crop");
	const std::array edge{ CharacterRect{ 0, 0, 16, 16 }, CharacterRect{ 2030, 1000, 2048, 1024 } };
	const auto clipped = BuildPackedCropSources(edge, 2048, 1024, full);
	Require(clipped.count == 2, "Image-edge actors were dropped");
	for (unsigned i = 0; i < clipped.count; ++i)
		Require(clipped.regions[i].Fits(2048, 1024), "Context padding exceeded image bounds");
	const auto smallAtlas = BuildPackedCropLayout(clipped, { 2048, 1024 }, { 2048, 1024 }, full);
	Require(smallAtlas.count == 2 && QualifiedExperimentalContextGeometry(smallAtlas.extent),
		"Tight source crops bypassed the native minimum-dimension safety floor");
	const ComputeSubrect smallBaseline{ 0, 0, 64, 64 };
	const PackedCropSources smallSources{ { ComputeSubrect{ 0, 0, 16, 16 }, ComputeSubrect{ 32, 0, 16, 16 } }, 2 };
	Require(!BuildPackedCropLayout(smallSources, { 64, 64 }, { 64, 64 }, smallBaseline).count,
		"Native minimum-dimension allocation was omitted from the saving gate");
#ifdef DEVBENCH_BRIDGE_ENABLED
	PackedCropPlanningDiagnostics planning;
	const auto sparse = BuildPackedCropSources(edge, 2048, 1024, full, &planning);
	Require(sparse == clipped && planning.reason == PackedCropReason::SourcesReady && planning.mergedContexts.count == 2,
		"Diagnostic collection changed sparse admission or lost its contexts");
	Require(!BuildPackedCropSources(crowded, 2048, 1024, full, &planning).count &&
				planning.reason == PackedCropReason::MergedToSingle && planning.inputRegions == crowded.size() && planning.mergedContexts.count == 1,
		"Context merging lost its actual rejection reason");
	Require(!BuildPackedCropSources(std::span(edge).first(1), 2048, 1024, full, &planning).count &&
				planning.reason == PackedCropReason::TooFewRegions,
		"Single-region rejection is ambiguous");
	Require(!BuildPackedCropSources(edge, 2048, 1024, { 0, 0, 1024, 1024 }, &planning).count &&
				planning.reason == PackedCropReason::ContextOutsideAvailable,
		"Available-region rejection is ambiguous");
	StablePackedCropSources retained{ clipped };
	Require(ResolvePackedCropSources(edge, 2048, 1024, full, retained, &planning) == clipped &&
				planning.reason == PackedCropReason::RetainedSources && !planning.candidateExamined && planning.selectedSources == clipped,
		"Retained contexts were falsely reported as a fresh candidate");
	const auto planningJson = PackedCropPlanningJson(planning);
	Require(planningJson["contextPixels"] == 0 && planningJson["samplingGuardPixels"] == 2 && planningJson["alignmentPixels"] == 8,
		"Tight crop diagnostics do not distinguish context, sampling support and alignment");
	Require(planningJson["mergedRegionCount"].is_null() && planningJson["selectedRegionCount"] == 2 && planningJson["tileSourceFrame"].is_null(),
		"Unexamined source diagnostics invented a count or tile frame");
	PackedCropAdmissionDiagnostics admission;
	Require(!BuildPackedCropLayout(dense, { 2048, 1024 }, { 2048, 1024 }, full, &admission).count &&
				admission.reason == PackedCropReason::InsufficientAreaSaving && admission.candidateAreaAvailable &&
				admission.candidateModelPixels == full.Area() && admission.layoutBaselineModelPixels == full.Area(),
		"Rejected dense atlas discarded its smallest candidate area");
	admission = {};
	Require(BuildPackedCropLayout(clipped, { 2048, 1024 }, { 2048, 1024 }, full, &admission).count == 2 &&
				admission.reason == PackedCropReason::Admitted && admission.candidateAreaAvailable,
		"Admitted sparse atlas lost its decision");
	admission = {};
	Require(!BuildPackedCropLayout(malformed, { 2048, 1024 }, { 2048, 1024 }, full, &admission).count &&
				admission.reason == PackedCropReason::OverlappingSources && !admission.candidateAreaAvailable,
		"Invalid source ownership was confused with an area rejection");
	admission.frame = 11;
	admission.sourceFrame = 10;
	const auto admissionJson = PackedCropAdmissionJson(admission, 2);
	Require(admissionJson["frame"] == 11 && admissionJson["sourceWorldFrame"] == 10 && admissionJson["featureSlot"] == 2 &&
				admissionJson["candidateModelPixels"].is_null() && admissionJson["effectiveBaselineModelPixels"].is_null(),
		"Admission JSON lost frame identity or replaced unmeasured areas with zero");
#endif
}
