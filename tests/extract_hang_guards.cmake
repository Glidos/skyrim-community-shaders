if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Utils/Game.h" _game)
extract_between(
    "${_game}" "inline float NormalizeDegrees0To360("
    "// Formatted string helpers" "angle_wrap_under_test.h"
)
file(READ "${PROJECT_ROOT}/src/Features/Upscaling.cpp" _upscaling)
extract_between(
    "${_upscaling}" "void Upscaling::FrameLimiter()"
    "double Upscaling::GetRefreshRate(" "frame_limiter_under_test.h"
)
