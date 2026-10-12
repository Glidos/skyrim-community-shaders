if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(READ "${PROJECT_ROOT}/src/Menu/ProfilingRenderer.h" header)
extract_between("${header}" "struct PerformanceTimingTotals"
    "/** Shared runtime capture switch" "vr_live_summary.h")
file(READ "${PROJECT_ROOT}/src/Menu/ProfilingRenderer.cpp" source)
extract_between("${source}" "static constexpr auto kOpenVRTimingRetryDelay"
    "#endif" "vr_live_constants.h")
extract_between("${source}" "struct OpenVRGameTimingCache"
    "#endif" "vr_live_cache.h")
extract_between("${source}" "static void CaptureOpenVRGameTiming"
    "static void CaptureFlatGameTiming" "vr_live_capture.h")
extract_between("${source}" "static bool IsPositiveFinite"
    "static bool IsDisplayTimingSampleValid" "vr_live_validity.h")
extract_between("${source}" "static void NormalizeGameFrameTiming"
    "static int ScaleToUiInt" "vr_live_normalize.h")
file(READ "${OUTPUT_DIRECTORY}/vr_live_summary.h" summary)
file(WRITE "${OUTPUT_DIRECTORY}/vr_live_timing_under_test.h"
    "#pragma once\nstruct ProfilingRenderer {\n${summary}\n};\n")
file(WRITE "${OUTPUT_DIRECTORY}/vr_live_sampler_under_test.h"
    "#pragma once\n#include \"vr_live_timing_fixture.h\"\n")
foreach(part IN ITEMS constants cache capture)
    file(READ "${OUTPUT_DIRECTORY}/vr_live_${part}.h" section)
    file(APPEND "${OUTPUT_DIRECTORY}/vr_live_sampler_under_test.h" "${section}\n")
endforeach()
file(APPEND "${OUTPUT_DIRECTORY}/vr_live_timing_under_test.h"
    "#include \"vr_live_sampler_under_test.h\"\n")
foreach(part IN ITEMS validity normalize)
    file(READ "${OUTPUT_DIRECTORY}/vr_live_${part}.h" section)
    file(APPEND "${OUTPUT_DIRECTORY}/vr_live_timing_under_test.h" "${section}\n")
endforeach()
