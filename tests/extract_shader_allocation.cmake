include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(READ "${PROJECT_ROOT}/src/ShaderCache.h" _header)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
extract_between(
    "${_header}" "template <typename ShaderType" "std::atomic_bool isEnabled"
    "shader_allocation_cache_under_test.h"
)
