if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()

file(READ "${PROJECT_ROOT}/src/Features/Upscaling.cpp" _source)
file(READ "${PROJECT_ROOT}/src/Features/Upscaling.h" _header)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")

extract_between(
    "${_source}" "bool Upscaling::EnsureHMDMaskClearResources()"
    "bool Upscaling::ShouldClearHMDMaskInPhase("
    hmd_mask_resources_under_test.h
)
extract_between(
    "${_source}" "void Upscaling::ClearShaderCache()"
    "void Upscaling::PrepareFrameGenerationInputs()"
    hmd_mask_shader_cache_under_test.h
)
extract_between(
    "${_source}" "struct HMDMaskClearConstants"
    "HMDMaskClearConstants MakeHMDMaskClearConstants("
    hmd_mask_resources_constants.h
)
extract_between(
    "${_header}" "// Shared VR HMD mask clearing"
    "#ifdef DEVBENCH_BRIDGE_ENABLED"
    hmd_mask_resources_members.h
)

extract_between(
    "${_source}" "void Upscaling::ResetHMDMaskClearResources()"
    "bool Upscaling::EnsureHMDMaskClearResources()"
    hmd_mask_resource_reset_under_test.h
)
extract_between(
    "${_source}" "if (shaderDevice != device) {"
    "\n\tQueryPerformanceFrequency(&qpf);"
    hmd_mask_device_change_under_test.h
)
