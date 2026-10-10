if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(READ "${PROJECT_ROOT}/src/Features/SkySync.cpp" source)
extract_between(
    "${source}"
    "void SkySync::HideSunOutsideFadeWindow("
    "void SkySync::ProcessMoon("
    "sky_fade_window_under_test.h"
)
