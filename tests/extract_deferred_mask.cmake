if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Deferred.cpp" _source)
extract_between(
    "${_source}" "static void ReleaseRenderTargetSlot"
    "void Deferred::ReleaseRenderTargets" "deferred_target_under_test.h"
)
extract_between(
    "${_source}" "void Deferred::UpdateCharacterCategoryTarget"
    "void Deferred::StartDeferred" "deferred_mask_under_test.h"
)

file(READ "${OUTPUT_DIRECTORY}/deferred_mask_under_test.h" _mask)
file(WRITE "${OUTPUT_DIRECTORY}/deferred_mask_under_test.h"
    "#include \"deferred_target_under_test.h\"\n${_mask}")
