include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${PROJECT_ROOT}/src/Features/LightLimitFix/ShadowCulling.cpp" source)
extract_between("${source}" "\tvoid Install()" "\tvoid Update(" "shadow_startup_under_test.h")
