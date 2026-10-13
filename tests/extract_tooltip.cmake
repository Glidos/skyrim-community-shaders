if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(READ "${PROJECT_ROOT}/src/Utils/UI.cpp" source)
extract_between(
    "${source}"
    "void SetTooltipPositionNearMouse("
    "void HelpMarker("
    "tooltip_under_test.h"
)
extract_between(
    "${source}"
    "HoverTooltipWrapper::HoverTooltipWrapper()"
    "CenteredPopupModal::CenteredPopupModal("
    "tooltip_wrapper_under_test.h"
)
file(READ "${PROJECT_ROOT}/src/Utils/UI.h" source)
extract_between(
    "${source}"
    "class HoverTooltipWrapper"
    "class CenteredPopupModal"
    "tooltip_wrapper_declaration.h"
)
