set(_hmd_mask_resources_test_dir
    "${CMAKE_CURRENT_BINARY_DIR}/generated/hmd-mask-resources-test"
)
set(_hmd_mask_resources_test_headers
    "${_hmd_mask_resources_test_dir}/hmd_mask_resources_under_test.h"
    "${_hmd_mask_resources_test_dir}/hmd_mask_shader_cache_under_test.h"
    "${_hmd_mask_resources_test_dir}/hmd_mask_resources_constants.h"
    "${_hmd_mask_resources_test_dir}/hmd_mask_resources_members.h"
    "${_hmd_mask_resources_test_dir}/hmd_mask_resource_reset_under_test.h"
    "${_hmd_mask_resources_test_dir}/hmd_mask_device_change_under_test.h"
)
add_custom_command(
    OUTPUT ${_hmd_mask_resources_test_headers}
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_hmd_mask_resources_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_hmd_mask_resources.cmake"
    DEPENDS
        src/Features/Upscaling.cpp src/Features/Upscaling.h
        tests/extract_hmd_mask_resources.cmake tests/extract_source_region.cmake
    VERBATIM
)
add_custom_target(
    hmd_mask_resources_test_headers
    DEPENDS ${_hmd_mask_resources_test_headers}
)
function(add_hmd_mask_resources_test TARGET_NAME TEST_NAME)
    add_d3d_shader_test(
        ${TARGET_NAME} ${TEST_NAME} tests/hmd_mask_resources_test.cpp
    )
    add_dependencies(${TARGET_NAME} hmd_mask_resources_test_headers)
    target_include_directories(
        ${TARGET_NAME} PRIVATE "${_hmd_mask_resources_test_dir}"
    )
    target_link_libraries(
        ${TARGET_NAME}
        PRIVATE CommonLibSSE::CommonLibSSE spdlog::spdlog windowsapp.lib
    )
    target_compile_definitions(${TARGET_NAME} PRIVATE NOMINMAX)
endfunction()
add_hmd_mask_resources_test(hmd_mask_resources_test HMDMaskResources)
add_hmd_mask_resources_test(
    hmd_mask_resources_devbench_test HMDMaskResourcesDevBench
)
target_compile_definitions(
    hmd_mask_resources_devbench_test PRIVATE DEVBENCH_BRIDGE_ENABLED
)
