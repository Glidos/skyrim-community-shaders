#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace SKSE::stl
{
	template <class F>
	struct scope_exit
	{
		F function;
		explicit scope_exit(F f) : function(std::move(f)) {}
		~scope_exit() { function(); }
	};
}
struct Menu
{
	enum class FontRole
	{
		Subtext
	};
	ImFont* GetFont(FontRole) { return nullptr; }
};
namespace globals
{
	Menu* menu = nullptr;
}
namespace Util
{
#include "tooltip_under_test.h"
#include "tooltip_wrapper_declaration.h"
#include "tooltip_wrapper_under_test.h"
}

namespace
{
	constexpr const char* detail = "Near-terrain diffuse and normal layers. Shared layers keep the detail needed by the nearest patch. LOD, parallax, overlays and noise remain excluded.";

	bool Check(bool condition, const char* message)
	{
		if (!condition)
			std::printf("FAIL: %s\n", message);
		return condition;
	}

	bool Run(ImVec2 size, ImVec2 mouse, float fontSize, const char* text, bool defaultFirst, bool wrapper = false)
	{
		ImGui::CreateContext();
		auto& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = size;
		io.DeltaTime = 1.0f;
		ImFontConfig fontConfig;
		fontConfig.SizePixels = fontSize;
		io.Fonts->AddFontDefault(&fontConfig);
		io.Fonts->Build();
		bool passed = true;
		for (int frame = 0; frame < 8; ++frame) {
			io.AddMousePosEvent(mouse.x, mouse.y);
			ImGui::NewFrame();
			ImGui::SetNextWindowPos({ 0, 0 });
			ImGui::SetNextWindowSize(size);
			ImGui::Begin("Fixture", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
			ImGui::SetCursorScreenPos({ mouse.x - 4, mouse.y - 4 });
			ImGui::Dummy({ 8, 8 });
			const auto styleDepth = ImGui::GetCurrentContext()->StyleVarStack.Size;
			if (defaultFirst)
				Util::AddTooltip("Turn Terrain on or off.", ImGuiHoveredFlags_None);
			if (wrapper) {
				if (auto help = Util::HoverTooltipWrapper())
					ImGui::TextWrapped("%s", text);
			} else {
				Util::AddTooltip(text, ImGuiHoveredFlags_None);
			}
			passed &= Check(ImGui::GetCurrentContext()->StyleVarStack.Size == styleDepth,
				"tooltip must restore style state");
			ImGui::End();
			ImGui::Render();
		}
		ImGuiWindow* tooltip = nullptr;
		int visible = 0;
		for (auto* window : ImGui::GetCurrentContext()->Windows) {
			if ((window->Flags & ImGuiWindowFlags_Tooltip) && window->Active && !window->Hidden) {
				tooltip = window;
				++visible;
			}
		}
		passed &= Check(visible == 1 && tooltip, "one complete tooltip must be visible");
		if (tooltip) {
			const auto padding = tooltip->WindowPadding;
			const float available = size.x - padding.x * 2;
			const auto expected = ImGui::CalcTextSize(text, nullptr, false, available);
			passed &= Check(tooltip->Pos.x >= -.5f && tooltip->Pos.y >= -.5f &&
								tooltip->Pos.x + tooltip->Size.x <= size.x + .5f &&
								tooltip->Pos.y + tooltip->Size.y <= size.y + .5f,
				"tooltip must fit the viewport at every pointer edge");
			passed &= Check(tooltip->InnerClipRect.Min.x <= tooltip->Pos.x + padding.x + .5f &&
								tooltip->InnerClipRect.Max.x >= tooltip->Pos.x + padding.x + expected.x - .5f,
				"the entire text width must be inside the tooltip clip rectangle");
			passed &= Check(std::abs(tooltip->ContentSize.y - expected.y) < .5f,
				"specific help must replace default help and grow to fit all lines");
		}
		ImGui::DestroyContext();
		return passed;
	}
}

int main()
{
	bool passed = true;
	for (const auto size : { ImVec2(1920, 1080), ImVec2(1024, 1024), ImVec2(320, 720) }) {
		for (const auto mouse : { ImVec2(16, 16), ImVec2(size.x - 16, size.y / 2), ImVec2(size.x / 2, size.y - 16) }) {
			passed &= Run(size, mouse, 24, detail, true);
			passed &= Run(size, mouse, 24, detail, true, true);
		}
	}
	passed &= Run({ 1920, 1080 }, { 1500, 40 }, 16, detail, false);
	passed &= Run({ 320, 720 }, { 300, 100 }, 24,
		"AnUnbrokenTextureIdentifierThatMustRemainInsideTheTooltipEvenInANarrowViewport", true);
	if (passed)
		std::puts("PASS: tooltip replacement, edge placement, wrapping and vertical growth");
	return passed ? 0 : 1;
}
