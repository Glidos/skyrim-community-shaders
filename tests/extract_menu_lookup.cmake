if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(READ "${PROJECT_ROOT}/src/Utils/MenuLookup.h" _lookup)
file(READ "${PROJECT_ROOT}/src/Features/Upscaling.cpp" _upscaling)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
string(FIND "${_lookup}" "namespace Util\n" _start)
if(_start EQUAL -1)
    message(FATAL_ERROR "Menu lookup namespace not found")
endif()
string(SUBSTRING "${_lookup}" ${_start} -1 _body)
file(WRITE "${OUTPUT_DIRECTORY}/menu_lookup_under_test.h" "${_body}")
extract_between(
    "${_upscaling}"
    "constexpr std::string_view kSkyrimPresentationMenuNames[] = {"
    "\n\tconstexpr const char* kFoveatedUpscalingMethodAvailabilityText"
    "menu_lookup_names_under_test.h"
)
extract_between(
    "${_upscaling}"
    "bool IsSkyrimMenuPresentationContextActive(RE::UI* a_ui)\n\t{"
    "\n\tbool IsKnownGameMenuContextActive()"
    "menu_lookup_presentation_under_test.h"
)
extract_between(
    "${_upscaling}"
    "bool IsLoadingMenuContextActive()\n\t{"
    "\n\tbool IsLoadingMenuPhysicallyOpen()"
    "menu_lookup_loading_under_test.h"
)
extract_between(
    "${_upscaling}"
    "bool IsMainMenuContextActive()\n\t{"
    "\n\tbool IsVRNativeVendorDirectMenuPresentationContextActive("
    "menu_lookup_main_under_test.h"
)
extract_between(
    "${_upscaling}"
    "bool IsRaceSexMenuContextActive(RE::UI* a_ui)\n\t{"
    "\n\tvoid TrackVRFastTravelInteractionMenuLifecycle("
    "menu_lookup_other_contexts_under_test.h"
)
