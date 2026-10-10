set(_hang_test_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/hang-guards")
set(_hang_test_headers
    "${_hang_test_dir}/angle_wrap_under_test.h"
    "${_hang_test_dir}/frame_limiter_under_test.h"
)
add_custom_command(
    OUTPUT ${_hang_test_headers}
    COMMAND
        "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_hang_test_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_hang_guards.cmake"
    DEPENDS
        src/Utils/Game.h src/Features/Upscaling.cpp
        tests/extract_hang_guards.cmake tests/extract_source_region.cmake
    VERBATIM
)
add_controller_test(hang_guards_test HangGuards tests/hang_guards_test.cpp)
target_sources(hang_guards_test PRIVATE ${_hang_test_headers})
target_include_directories(hang_guards_test PRIVATE "${_hang_test_dir}")
target_compile_definitions(hang_guards_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_tests_properties(HangGuards PROPERTIES TIMEOUT 10)
