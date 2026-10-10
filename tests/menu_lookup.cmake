set(_menu_lookup_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/menu-lookup")
set(_menu_lookup_headers
    "${_menu_lookup_dir}/menu_lookup_under_test.h"
    "${_menu_lookup_dir}/menu_lookup_names_under_test.h"
    "${_menu_lookup_dir}/menu_lookup_presentation_under_test.h"
    "${_menu_lookup_dir}/menu_lookup_loading_under_test.h"
    "${_menu_lookup_dir}/menu_lookup_main_under_test.h"
    "${_menu_lookup_dir}/menu_lookup_other_contexts_under_test.h"
)
add_custom_command(
    OUTPUT ${_menu_lookup_headers}
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_menu_lookup_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_menu_lookup.cmake"
    DEPENDS
        src/Utils/MenuLookup.h
        src/Features/Upscaling.cpp
        tests/extract_source_region.cmake
        tests/extract_menu_lookup.cmake
    VERBATIM
)
add_controller_test(menu_lookup_test MenuLookup tests/menu_lookup_test.cpp)
target_sources(menu_lookup_test PRIVATE ${_menu_lookup_headers})
target_include_directories(menu_lookup_test PRIVATE "${_menu_lookup_dir}")
