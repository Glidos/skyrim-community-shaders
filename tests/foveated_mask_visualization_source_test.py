"""Guard preview wiring that sits outside the extracted controller tests."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/Features/Upscaling.cpp").read_text(encoding="utf-8")


def section(begin, end):
    start = SOURCE.index(begin)
    return SOURCE[start:SOURCE.index(end, start)]


class PreviewIntegration(unittest.TestCase):
    def test_menu_close_ends_preview_on_both_close_paths(self):
        menu = (ROOT / "src/Menu.cpp").read_text(encoding="utf-8")
        close = menu[menu.index("void Menu::CloseMenu()"):]
        close = close[:close.index("static std::vector<InputCombo>")]
        self.assertIn("globals::features::upscaling.EndFoveatedMaskVisualization();", close)
        draw = menu[menu.index("void Menu::DrawSettings()"):menu.index("void Menu::DrawGeneralSettings()")]
        self.assertIn("globals::features::upscaling.EndFoveatedMaskVisualization();", draw[draw.rindex("if (!IsEnabled)"):])

    def test_visualization_is_excluded_from_saved_settings(self):
        fields = section("#define UPSCALING_SETTINGS_JSON_FIELDS(OP)", "void to_json(")
        self.assertNotIn("OP(foveatedPeripheryMaskVisualization)", fields)
        defaults = section("void Upscaling::RestoreDefaultSettings(", "struct BSOpenVR_GetRenderTargetSize")
        self.assertIn("settings.foveatedPeripheryMaskVisualization = false", defaults)

    def test_submit_preview_uses_presentation_before_temporal_inputs(self):
        submit = section("bool Upscaling::SubmitVRUpscaledFrame(", "bool Upscaling::TryReplaceVanillaDynamicResolutionUpsample(")
        admission = submit.index("if (foveatedMaskVisualizationPreview) {")
        temporal = submit.index("SubmitTemporalInputs temporalInputs;")
        self.assertLess(admission, temporal)
        self.assertIn("presentationOnly = true;", submit[admission:admission + 160])
        self.assertIn("if (!presentationOnly)", submit[temporal:temporal + 100])
        stretch = submit[submit.index("const auto presentStretchOutput ="):submit.index("auto presentDeferredVendorOutput =")]
        self.assertIn("DispatchFoveatedMaskVisualization(eyeIndex, inputWidth, inputHeight)", stretch)
        self.assertIn("UpdateVRSubmitDesktopMirror(", stretch)
        self.assertIn("submitStageVendorEyeState = {};", stretch)
        self.assertIn("submitStageRuntimeFSRStereoState = {};", stretch)
        self.assertNotIn("VendorEvaluated", stretch)

    def test_neural_patch_preserves_live_preview(self):
        patch = section("bool Upscaling::ApplyNeuralRenderingConfiguration(", "bool Upscaling::ResetNeuralRenderingConfiguration(")
        preserve = patch.index("candidate.foveatedPeripheryMaskVisualization = settings.foveatedPeripheryMaskVisualization;")
        self.assertLess(preserve, patch.index("settings = std::move(candidate);"))

    def test_main_preview_respects_output_ownership_and_skips_encoding(self):
        main = section("void Upscaling::Upscale()", "void Upscaling::PerformUpscaling()")
        preview = main.index("if (IsFoveatedMaskVisualizationEnabled(upscaleMethod)) {")
        self.assertLess(main.index("if (vrRenderScaleSubmitStageOwnsOutput)"), preview)
        self.assertLess(preview, main.index("auto encodeUpscalingTextures ="))
        draw = main[preview:main.index("pendingDLSSHistoryReset.exchange", preview)]
        self.assertIn("TryDrawMainFoveatedMaskVisualization(!vendorLifecycleMutationDeferred)", draw)
        self.assertIn("|| IsSubmitStageDeviceLost()", draw)
        self.assertIn("RequestHistoryReset();", draw)

    def test_failed_preview_cannot_bypass_vendor_recovery_admission(self):
        main = section("void Upscaling::Upscale()", "void Upscaling::PerformUpscaling()")
        preview = main.index("TryDrawMainFoveatedMaskVisualization(")
        guard = main.index("if (vendorLifecycleMutationDeferred && !CanDispatchExistingVRVendorEvaluation(upscaleMethod))")
        encoding = main.index("auto encodeUpscalingTextures =")
        self.assertLess(preview, guard)
        self.assertLess(guard, encoding)
        self.assertIn("return;", main[guard:encoding])

    def test_native_preview_uses_scene_format_without_widening_submit_contract(self):
        helper = section("bool Upscaling::TryDrawMainFoveatedMaskVisualization(", "bool Upscaling::DispatchFoveatedPeripheryPass(")
        self.assertIn("a_allowResourceCreation, mainDesc.Format", helper)
        allocator = section("bool Upscaling::EnsureVRPresentationTextures(", "void Upscaling::",)
        self.assertIn("colorSrcDesc.Format != colorFormat", allocator)
        header = (ROOT / "src/Features/Upscaling.h").read_text(encoding="utf-8")
        self.assertIn("DXGI_FORMAT colorFormat = DXGI_FORMAT_R8G8B8A8_UNORM", header)

    def test_preview_mirror_cannot_allocate_during_protection(self):
        mirror = section("bool Upscaling::UpdateVRSubmitDesktopMirror(", "bool Upscaling::SubmitVRUpscaledFrame(")
        guard = mirror.index("ShouldReuseOrdinarySaveResources() || ShouldDeferVRVendorLifecycleMutation()")
        blit = mirror.index("BlitVRRenderScaleDesktopMirror(")
        self.assertLess(guard, blit)
        for resource in ("vrDesktopMirrorBlitTarget != sourceTexture", "!vrDesktopMirrorBlitRTV", "!vrDesktopMirrorBlitPS", "!upscaleVS"):
            self.assertIn(resource, mirror[guard:blit])
        self.assertIn("return true;", mirror[guard:blit])

    def test_eye_preview_preserves_bindings_outside_its_compute_slots(self):
        dispatch = section("bool Upscaling::DispatchFoveatedMaskVisualization(", "bool Upscaling::TryDrawMainFoveatedMaskVisualization(")
        self.assertIn("NeuralRendering::ComputeStateGuard<1>", dispatch)
        self.assertIn("CSGetSamplers", dispatch)
        self.assertIn("CSSetSamplers", dispatch)
        self.assertNotIn("UnbindUpscalingResources()", dispatch)


if __name__ == "__main__":
    unittest.main()
