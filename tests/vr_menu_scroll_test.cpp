#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace RE
{
	enum class ControllerRole
	{
		Primary,
		Secondary
	};
	struct BSOpenVRControllerDevice
	{
		static bool IsPrimaryController(size_t device) { return device == 0; }
		static bool IsSecondaryController(size_t device) { return device == 1; }
	};
	struct VRControllerState
	{
		std::array<ImVec2, 2> thumbsticks{};
		struct Button
		{
			bool isPressed = false;
		};
		std::array<Button, 2> buttons{};
		const Button& operator[](size_t key) const { return buttons.at(key); }
	};
}

struct Menu
{
	struct KeyEvent
	{
		size_t device;
		float thumbstickX, thumbstickY;
		int keyCode = 0;
		float value = 0;
		bool IsPressed() const { return value > 0; }
	};
};

struct VR
{
	struct VRControllerEventLog
	{
		int device, keyCode, value;
		bool pressed;
		double heldTime;
		std::string heldSource;
		float thumbstickX, thumbstickY;
		std::string controllerRole;
	};
	std::vector<VRControllerEventLog> vrControllerEventLog;
	RE::VRControllerState primaryControllerState, secondaryControllerState;
	bool customVRCursorVisible = false;
	ImVec2 customVRCursorPos{};
	void ReleaseOCUPointerState() {}
	void ReleaseMenuImGuiInputState();
	void UpdateControllerState(const Menu::KeyEvent&);
	void ProcessThumbstickScroll(RE::VRControllerState&, size_t, float, ImGuiIO&);
	void ProcessThumbstickScrollMouseNavigation(RE::VRControllerState&, size_t, float, ImGuiIO&);
};

namespace
{
#include "ui_vr_cursor_delta.h"
#include "ui_vr_scroll_types.h"
	std::unordered_map<size_t, ScrollAccum> gVRScrollAccums;
	bool gWandActivationPending = false;
	bool gHasOCUPointerInputSource = false;
	bool gUsingOCUPointerInput = false;
	bool gHasLastMenuNavigationMode = false;
	bool gLastMenuNavigationUsesWand = false;
	std::array<bool, 2> gBlockRawTriggerUntilRelease{};
	bool gPrevPrimaryVRInputStates[2]{};
	bool gPrevSecondaryVRInputStates[2]{};
	struct Mapping
	{
		size_t keyCode;
	};
	std::array<Mapping, 2> GetVRInputMappings(bool secondary)
	{
		return secondary ? std::array{ Mapping{ 1 }, Mapping{ 0 } } : std::array{ Mapping{ 0 }, Mapping{ 1 } };
	}
	void CancelWandMouseInput(VR&) {}
	void ResetCursorOwnershipState() {}
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
}

// Runtime dependencies are substituted; input updates, reset and scrolling use production code.
#include "ui_vr_input_release.h"
#include "ui_vr_scroll.h"

static void CheckScrolling(bool mouseNavigation, size_t hand)
{
	const std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context(ImGui::CreateContext(), ImGui::DestroyContext);
	auto& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = { 1024, 768 };
	io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_DockingEnable;
	io.ConfigInputTrickleEventQueue = false;
	io.Fonts->AddFontDefault();
	io.Fonts->Build();
	VR vr;
	auto& controller = hand == 0 ? vr.primaryControllerState : vr.secondaryControllerState;
	const auto setStick = [&](float x, float y) { vr.UpdateControllerState({ hand, x, y }); };
	auto scroll = [&] {
		if (mouseNavigation)
			vr.ProcessThumbstickScrollMouseNavigation(controller, hand, .15f, io);
		else
			vr.ProcessThumbstickScroll(controller, hand, .15f, io);
	};
	ImGuiWindow* body = nullptr;
	auto frame = [&] {
		scroll();
		ImGui::NewFrame();
		ImGui::SetNextWindowPos({ 20, 20 });
		ImGui::SetNextWindowSize({ 400, 300 });
		ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoSavedSettings);
		ImGui::BeginChild("Body", {}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
		body = ImGui::GetCurrentWindow();
		for (int item = 0; item < 50; ++item) {
			ImGui::PushID(item);
			ImGui::Button("Setting", { 650, 30 });
			ImGui::PopID();
		}
		ImGui::EndChild();
		ImGui::End();
		ImGui::Render();
	};
	const auto frames = [&](int count) {
		for (int frameIndex = 0; frameIndex < count; ++frameIndex)
			frame();
	};
	const auto pressKey = [&](ImGuiKey key) {
		io.AddKeyEvent(key, true);
		frame();
		io.AddKeyEvent(key, false);
		frames(2);
	};
	const auto requireIdle = [&] {
		const ImVec2 stoppedAt = body->Scroll;
		frames(30);
		Require(body->Scroll.x == stoppedAt.x && body->Scroll.y == stoppedAt.y,
			"menu must stay still without fresh stick input");
	};
	io.AddMousePosEvent(100, 100);
	frames(2);
	Require(body->ScrollMax.x > 0 && body->ScrollMax.y > 0, "fixture must scroll on both axes");

	// A menu transition can occur after the last gameplay stick event.
	setStick(1, -1);
	controller.buttons[0].isPressed = true;
	vr.ReleaseMenuImGuiInputState();
	requireIdle();
	Require(controller.buttons[0].isPressed, "menu resets must preserve held shortcut buttons");
	for (bool secondary : { false, true }) {
		const auto mappings = GetVRInputMappings(secondary);
		const auto& physical = secondary ? vr.secondaryControllerState : vr.primaryControllerState;
		const auto* previous = secondary ? gPrevSecondaryVRInputStates : gPrevPrimaryVRInputStates;
		for (size_t index = 0; index < mappings.size(); ++index)
			Require(previous[index] == physical[mappings[index].keyCode].isPressed, "reset must latch physical held buttons in mapping order");
	}
	ImVec2 cursor{ 200, 200 };
	Require(!ApplyThumbstickCursorDelta(controller, hand, .15f, 10, cursor, io.DisplaySize),
		"reset must not replay stale cursor movement");

	// Fresh input must scroll both before and after keyboard navigation.
	setStick(0, -1);
	frames(12);
	Require(body->Scroll.y > 0, "fresh controller input must scroll");
	setStick(0, 0);
	frame();
	requireIdle();

	for (bool enterBody : { false, true }) {
		if (enterBody)
			pressKey(ImGuiKey_Enter);
		pressKey(ImGuiKey_Tab);
		Require(GImGui->NavId != 0 && GImGui->NavCursorVisible && (!enterBody || GImGui->NavWindow == body),
			"Tab must focus the requested window before the scrolling check");
		const float afterTab = body->Scroll.y;
		setStick(0, -1);
		frames(12);
		Require(body->Scroll.y > afterTab, "Tab must not disable fresh controller scrolling");
		vr.ReleaseMenuImGuiInputState();
		requireIdle();
	}
	setStick(1, 0);
	frames(12);
	Require(mouseNavigation ? body->Scroll.x > 0 : body->Scroll.x == 0,
		"horizontal scrolling must retain the navigation mode policy");
	Require(ApplyThumbstickCursorDelta(controller, hand, .15f, 10, cursor, io.DisplaySize) && cursor.x > 200,
		"fresh stick input must still move the cursor");
	vr.ReleaseMenuImGuiInputState();
	requireIdle();

	// Reset must also discard a partial wheel step before accepting fresh input.
	setStick(0, -.75f);
	frames(3);
	vr.ReleaseMenuImGuiInputState();
	const float beforePartialStep = body->Scroll.y;
	setStick(0, -.75f);
	frames(2);
	Require(body->Scroll.y == beforePartialStep, "reset must clear partial scroll accumulation");
	setStick(.15f, -.15f);
	requireIdle();
}

int main()
{
	try {
		for (bool mouseNavigation : { false, true })
			for (size_t hand : { 0u, 1u })
				CheckScrolling(mouseNavigation, hand);
		std::cout << "VR menu scrolling passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
