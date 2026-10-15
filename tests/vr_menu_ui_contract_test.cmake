if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

file(READ "${PROJECT_ROOT}/src/Features/VR.cpp" _vr_source)
file(READ "${PROJECT_ROOT}/src/Features/VR/InSceneOverlay.cpp" _in_scene_source)
file(READ "${PROJECT_ROOT}/src/Features/VR/WandPointing.cpp" _wand_source)

function(assert_section_contains _start_marker _end_marker _required_text _surface)
    string(FIND "${_vr_source}" "${_start_marker}" _start)
    string(FIND "${_vr_source}" "${_end_marker}" _end)
    if(_start EQUAL -1 OR _end EQUAL -1 OR _end LESS_EQUAL _start)
        message(FATAL_ERROR "Unable to inspect the ${_surface} menu surface")
    endif()

    math(EXPR _length "${_end} - ${_start}")
    string(SUBSTRING "${_vr_source}" ${_start} ${_length} _section)
    string(FIND "${_section}" "${_required_text}" _required_position)
    if(_required_position EQUAL -1)
        message(FATAL_ERROR
            "${_surface} is missing required behavior: ${_required_text}"
        )
    endif()
endfunction()

assert_section_contains(
    "void VR::UpdateVROverlayPosition()"
    "void VR::UpdateVROverlayControllerPosition()"
    "if (IsHMDMenuPlacementReady())"
    "native HMD transform admission"
)
assert_section_contains(
    "void VR::SubmitOverlayFrame()"
    "void VR::UpdateDepthBufferCulling()"
    "if (wantsHMDOverlay && IsHMDMenuPlacementReady())"
    "native HMD presentation admission"
)
foreach(_required IN ITEMS
    "a_type == OverlayType::HMD && !IsHMDMenuPlacementReady()"
    "&& menuTexture && IsHMDMenuPlacementReady()"
    "attachMode == AttachMode::Both) && IsHMDMenuPlacementReady()"
    "attachMode == AttachMode::Both && !showOnHMD"
)
    string(FIND "${_in_scene_source}" "${_required}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "In-scene placement admission is missing: ${_required}")
    endif()
endforeach()
string(FIND "${_wand_source}" "a_type == VR::OverlayType::HMD && !a_vr.IsHMDMenuPlacementReady()" _wand_admission)
if(_wand_admission EQUAL -1)
    message(FATAL_ERROR "Fallback wand geometry must reject an uninitialized HMD anchor")
endif()


assert_section_contains(
    "bool CanConfigureMenuLayout()"
    "void DrawKeepDesktopWindowFocusedForVRMenuSetting();"
    "return REL::Module::IsVR();"
    "menu layout runtime gate"
)
assert_section_contains(
    "if (!CanConfigureMenuLayout())"
    "Util::Widgets::SliderFloat(\"Mouse Speed\""
    "DrawHeadsetMenuSettings();"
    "VR settings UI"
)
assert_section_contains(
    "void DrawHeadsetMenuSettings()\n\t{"
    "void DrawMenuSettings()\n\t{"
    "DrawMenuLayoutUnlockSetting();"
    "headset layout controls"
)
assert_section_contains(
    "void DrawMenuSettings()\n\t{"
    "void DrawMouseSettings()\n\t{"
    "DrawKeepDesktopWindowFocusedForVRMenuSetting();"
    "desktop menu controls"
)
assert_section_contains(
    "void DrawMenuSettings()\n\t{"
    "void DrawMouseSettings()\n\t{"
    "DrawStabilizeRenderScaleDesktopMirrorSetting();"
    "desktop mirror controls"
)
assert_section_contains(
    "float VR::GetEffectiveMenuScale() const"
    "Vector3 VR::GetEffectiveHMDMenuOffset() const"
    "Config::kDefaultMenuScale"
    "effective headset scale policy"
)
assert_section_contains(
    "void DrawHeadsetMenuSettings()\n\t{"
    "void DrawMenuSettings()\n\t{"
    "Util::DisableGuard placementLocked(!settings.UnlockMenuPositionAndSize)"
    "placement edit lock"
)

foreach(_required_behavior IN ITEMS
    "Util::Widgets::Checkbox(\"Unlock Menu Position and Size\""
    "vr.settings.UnlockMenuPositionAndSize"
    "vr.SetMenuLayoutUnlocked(layoutUnlocked)"
    "float VR::GetEffectiveMenuScale() const"
)
    string(FIND "${_vr_source}" "${_required_behavior}" _behavior_position)
    if(_behavior_position EQUAL -1)
        message(FATAL_ERROR
            "Shared menu layout toggle behavior is missing: ${_required_behavior}"
        )
    endif()
endforeach()

string(FIND
    "${_in_scene_source}"
    "const float menuScale = GetEffectiveMenuScale();"
    _effective_in_scene_scale_position
)
if(_effective_in_scene_scale_position EQUAL -1)
    message(FATAL_ERROR "In-scene menu presentation bypasses effective scale")
endif()

foreach(_forbidden_scale_path IN ITEMS
    "baseWidth * settings.VRMenuScale"
    "CreateHMDOverlayScaleMatrix(settings.VRMenuScale)"
    "CreateOverlayScaleMatrix(settings.VRMenuScale)"
)
    string(FIND "${_vr_source}" "${_forbidden_scale_path}" _vr_scale_position)
    string(FIND
        "${_in_scene_source}"
        "${_forbidden_scale_path}"
        _in_scene_scale_position
    )
    if(NOT _vr_scale_position EQUAL -1 OR NOT _in_scene_scale_position EQUAL -1)
        message(FATAL_ERROR
            "Menu presentation bypasses effective scale: ${_forbidden_scale_path}"
        )
    endif()
endforeach()

message(STATUS "VR menu layout UI and effective policy are coherent")

file(READ "${PROJECT_ROOT}/src/Features/VR.h" vr_header)
file(READ "${PROJECT_ROOT}/src/Features/VR/OverlayDrag.cpp" placement_source)
foreach(required IN ITEMS "kMinMenuScale = 1.5f" "kMaxMenuScale = 2.0f" "kDefaultWandAimPitchTrimDegrees = 0.0f" "VRMenuPositioningPolicy::SanitizeDistance(-VRMenuOffsetZ)")
    string(FIND "${vr_header}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "VR settings boundary is missing: ${required}")
    endif()
endforeach()
foreach(required IN ITEMS "SliderFloatOnRelease(\"Menu Scale\"" "SliderFloatOnRelease(\"Menu Distance\"" "Checkbox(\"Show Both Lasers\", &settings.ShowBothWandLasers" "GetEffectiveHMDMenuScale()")
    string(FIND "${_vr_source}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Menu controls or compensated scale are missing: ${required}")
    endif()
endforeach()
string(FIND "${placement_source}" "const float depth = GetEffectiveHMDMenuOffset().z;" effective_depth)
if(effective_depth EQUAL -1)
    message(FATAL_ERROR "Fixed-world reanchor must use the bounded menu distance")
endif()
