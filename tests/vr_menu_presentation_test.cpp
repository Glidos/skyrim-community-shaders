#define NOMINMAX
#include "Features/VR/MenuPositioningPolicy.h"
#include "Features/VR/OpenVRDetection.h"
#include "Features/VR/WandSurfaceGeometry.h"
#include <SimpleMath.h>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <openvr.h>

using DirectX::SimpleMath::Matrix;
using DirectX::SimpleMath::Vector3;

namespace globals
{
	struct State
	{
		bool isMainMenuOpen = false;
	} stateValue;
	State* state = &stateValue;
	namespace features
	{
		struct RenderDoc
		{
			bool active = false;
			bool ShouldBlockUpscaling() const { return active; }
		} renderDoc;
	}
}

enum class ControllerDevice
{
	Primary,
	Secondary,
	Both
};
struct VR
{
	struct Config
	{
#include "vr_menu_defaults_under_test.h"
#include "vr_menu_scale_under_test.h"
	};
	struct Settings
	{
#include "vr_menu_path_under_test.h"
		MenuOverlayPath menuOverlayPath = MenuOverlayPath::Auto;
		enum class OverlayAttachMode
		{
			HMDOnly,
			ControllerOnly,
			Both,
			None
		};
		OverlayAttachMode attachMode = OverlayAttachMode::HMDOnly;
		ControllerDevice VRMenuAttachController = ControllerDevice::Secondary;
		int VRMenuPositioningMethod = 1;
		bool UnlockMenuPositionAndSize = false, EnableDragToReposition = false;
		float VRMenuScale = 1.75f, VRMenuOffsetX = .1f, VRMenuOffsetY = -.2f, VRMenuOffsetZ = -2.75f;
		float VRMenuControllerOffsetX = .2f, VRMenuControllerOffsetY = .3f, VRMenuControllerOffsetZ = .4f;
	} settings;
	VRDetection::OpenVRDetectionResult openVRInfo;
	bool ShouldUseInSceneOverlay() const;
	bool CanOpenMenuFromWorld() const;
	bool statusOnly = false;
	bool IsClosedMenuStatusOverlayActive() const { return statusOnly; }
	bool UseFixedWorldMenuPositioning() const;
	Settings::OverlayAttachMode GetEffectiveMenuAttachMode() const;
	ControllerDevice GetEffectiveMenuAttachController() const;
	float GetEffectiveMenuScale() const;
	float GetEffectiveHMDMenuScale() const;
	Vector3 GetEffectiveHMDMenuOffset() const;
	Vector3 GetEffectiveControllerMenuOffset() const;
	void SetMenuLayoutUnlocked(bool);
	void RequestFixedWorldMenuReanchor();
	void UpdateFixedWorldPositioning(const Matrix&);
	unsigned inputResets = 0;
	void ResetMenuInputRuntimeState() { ++inputResets; }
	struct
	{
		Matrix m = Matrix::Identity;
		bool initialized = false;
	} fixedWorldOverlayPosition;
	bool fixedWorldOverlayReanchorRequested = true;
};
using AttachMode = VR::Settings::OverlayAttachMode;
#include "vr_menu_effective_under_test.h"
#include "vr_menu_layout_lock_under_test.h"
#include "vr_menu_offsets_under_test.h"
#include "vr_menu_route_under_test.h"
#include "vr_menu_world_admission_under_test.h"
namespace
{
#include "vr_menu_fixed_helpers_under_test.h"
}
#include "vr_menu_fixed_update_under_test.h"

namespace
{
#include "vr_menu_matrix_conversion_under_test.h"
#include "vr_menu_native_relative_under_test.h"
#include "vr_menu_native_scale_under_test.h"

	bool CoversRoutes(VRDetection::RuntimeType runtime)
	{
		VR vr;
		vr.openVRInfo = { .isCompatible = true, .hasOverlayInterface = true, .runtimeType = runtime };
		if (!vr.CanOpenMenuFromWorld())
			return false;
		// Auto keeps one runtime path through startup, world loads and return to the main menu.
		for (const bool mainMenu : { false, true, true, false, true, false }) {
			globals::stateValue.isMainMenuOpen = mainMenu;
			if (vr.ShouldUseInSceneOverlay() != (runtime == VRDetection::RuntimeType::OpenComposite))
				return false;
		}
		vr.settings.menuOverlayPath = VR::Settings::MenuOverlayPath::InScene;
		if (!vr.ShouldUseInSceneOverlay() || !vr.CanOpenMenuFromWorld())
			return false;
		globals::features::renderDoc.active = true;
		if (vr.ShouldUseInSceneOverlay() != (runtime != VRDetection::RuntimeType::SteamVR))
			return false;
		globals::features::renderDoc.active = false;
		vr.settings.menuOverlayPath = VR::Settings::MenuOverlayPath::IVROverlay;
		if (vr.ShouldUseInSceneOverlay())
			return false;
		vr.settings.menuOverlayPath = VR::Settings::MenuOverlayPath::Auto;
		vr.openVRInfo.hasOverlayInterface = false;
		if (!vr.ShouldUseInSceneOverlay() || !vr.CanOpenMenuFromWorld())
			return false;
		vr.openVRInfo.isCompatible = false;
		return !vr.ShouldUseInSceneOverlay();
	}

	unsigned CheckSurface(float scale, bool hmd, float yaw, bool relative, float distance)
	{
		const float aspect = hmd ? VR::Config::kHMDOverlayAspect : VR::Config::kOverlayAspect;
		const float referenceScale = scale;
		if (hmd)
			scale = VRMenuPositioningPolicy::ScaleForDistance(scale, distance, VRMenuPositioningPolicy::ScaleReferenceDistance);
		const Vector3 offset(0.26f, -0.04f, -distance);
		const Matrix anchor = Matrix::CreateFromYawPitchRoll(yaw, relative ? 0.2f : 0.0f, 0.0f) *
		                      Matrix::CreateTranslation(0.7f, 1.6f, 0.3f);
		const Matrix placement = Matrix::CreateTranslation(offset) * anchor;
		const Matrix sceneScale = hmd ? VR::Config::CreateHMDOverlayScaleMatrix(scale) :
		                                VR::Config::CreateOverlayScaleMatrix(scale);
		const Matrix scene = sceneScale * placement;
		auto fixedTransform = MatrixToHmdMatrix34(placement);
		ScaleOverlayTransform(fixedTransform, scale, scale * aspect);
		const Matrix native = relative ?
		                          HmdMatrix34ToMatrix(CreateControllerOverlayTransform(offset.x, offset.y, offset.z, scale, scale * aspect)) * anchor :
		                          HmdMatrix34ToMatrix(fixedTransform);
		const auto toGeometry = [](const Vector3& v) { return WandSurfaceGeometry::Vector{ v.x, v.y, v.z }; };
		const Vector3 topLeft = Vector3::Transform(Vector3(-0.5f, 0.5f, 0.0f), scene);
		const WandSurfaceGeometry::Surface surface{ toGeometry(topLeft),
			toGeometry(Vector3::Transform(Vector3(0.5f, 0.5f, 0.0f), scene) - topLeft),
			toGeometry(Vector3::Transform(Vector3(-0.5f, -0.5f, 0.0f), scene) - topLeft) };
		unsigned failures = 0;
		if (hmd) {
			const float actualWidth = (Vector3::Transform(Vector3(0.5f, 0.5f, 0), scene) - topLeft).Length();
			const float actualHeight = (Vector3::Transform(Vector3(-0.5f, -0.5f, 0), scene) - topLeft).Length();
			const float referenceWidth = referenceScale * referenceScale / VRMenuPositioningPolicy::ScaleReferenceDistance;
			if (std::abs(actualWidth / distance - referenceWidth) > 1e-5f ||
				std::abs(actualHeight / distance - referenceWidth * aspect * aspect) > 1e-5f)
				++failures;
		}
		Vector3 normal = placement.Backward();
		normal.Normalize();
		for (const float u : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
			for (const float v : { 0.0f, 0.125f, 0.5f, 0.875f, 1.0f }) {
				// IVROverlay applies width and texture aspect before the supplied transform.
				const Vector3 expected = Vector3::Transform(Vector3((u - 0.5f) * scale, (0.5f - v) * scale * aspect, 0.0f), native);
				const Vector3 actual = Vector3::Transform(Vector3(u - 0.5f, 0.5f - v, 0.0f), scene);
				if ((actual - expected).Length() > 1e-5f)
					++failures;
				WandSurfaceGeometry::Hit hit;
				if (u > 0.0f && u < 1.0f && v > 0.0f && v < 1.0f &&
					(!WandSurfaceGeometry::TryIntersect(surface, toGeometry(expected + normal), toGeometry(-normal), hit) ||
						std::abs(hit.u - u) > 1e-5f || std::abs(hit.v - v) > 1e-5f))
					++failures;
			}
		}
		return failures;
	}
}

int main()
{
	unsigned failures = 0;
	unsigned surfaces = 0;
	VR placement;
	for (const float yaw : { -2.5f, -.8f, 0.0f, 1.6f, 3.0f }) {
		Matrix pose = Matrix::CreateFromYawPitchRoll(yaw, .2f, -.1f);
		pose.Translation(Vector3(1, 1.7f, 2));
		placement.RequestFixedWorldMenuReanchor();
		placement.UpdateFixedWorldPositioning(pose);
		const auto anchor = placement.fixedWorldOverlayPosition.m;
		Vector3 direction = pose.Backward();
		direction.y = 0;
		direction.Normalize();
		if (!placement.fixedWorldOverlayPosition.initialized ||
			(anchor.Translation() - (pose.Translation() - direction * 2.75f)).Length() > 1e-5f)
			++failures;
		for (const bool unlocked : { true, false, true, false }) {
			placement.SetMenuLayoutUnlocked(unlocked);
			Matrix moved = Matrix::CreateFromYawPitchRoll(yaw + 1, .4f, .2f);
			moved.Translation(Vector3(4, 1.5f, 3));
			placement.UpdateFixedWorldPositioning(moved);
			if (placement.fixedWorldOverlayPosition.m != anchor ||
				placement.GetEffectiveMenuScale() != 1.75f || placement.GetEffectiveHMDMenuOffset() != Vector3(.1f, -.2f, -2.75f) ||
				placement.settings.EnableDragToReposition || !placement.UseFixedWorldMenuPositioning())
				++failures;
		}
	}
	placement.settings.VRMenuPositioningMethod = 0;
	placement.settings.attachMode = VR::Settings::OverlayAttachMode::Both;
	placement.SetMenuLayoutUnlocked(false);
	if (placement.UseFixedWorldMenuPositioning() || placement.GetEffectiveMenuAttachMode() != VR::Settings::OverlayAttachMode::Both)
		++failures;
	placement.settings.VRMenuScale = 1.7f;
	placement.settings.VRMenuOffsetZ = -2.4f;
	if (std::abs(placement.GetEffectiveHMDMenuScale() - 1.7f * std::sqrt(2.4f / 2.25f)) > 1e-6f)
		++failures;
	placement.statusOnly = true;
	if (placement.GetEffectiveMenuScale() != VR::Config::kDefaultMenuScale ||
		placement.GetEffectiveMenuAttachMode() != VR::Settings::OverlayAttachMode::HMDOnly)
		++failures;

	for (const auto runtime : { VRDetection::RuntimeType::SteamVR, VRDetection::RuntimeType::OpenComposite, VRDetection::RuntimeType::Unknown }) {
		if (!CoversRoutes(runtime)) {
			std::cout << "route mismatch for runtime=" << static_cast<int>(runtime) << '\n';
			++failures;
		}
	}
	for (const bool hmd : { true, false }) {
		unsigned surfaceFailures = 0;
		for (const float scale : { 1.5f, 1.75f, 2.0f }) {
			for (const float yaw : { -0.7f, 0.0f, 1.2f }) {
				for (const bool relative : { false, true }) {
					for (const float distance : { VRMenuPositioningPolicy::MinDistance, VRMenuPositioningPolicy::DefaultDistance, VRMenuPositioningPolicy::MaxDistance }) {
						if (!hmd && distance != VRMenuPositioningPolicy::DefaultDistance)
							continue;
						surfaceFailures += CheckSurface(scale, hmd, yaw, relative, distance);
						++surfaces;
					}
				}
			}
		}
		failures += surfaceFailures;
		std::cout << "hmd=" << hmd << " mismatches=" << surfaceFailures << '\n';
	}
	std::cout << surfaces << " production surface transforms; " << failures << " failures\n";
	return failures ? 1 : 0;
}
