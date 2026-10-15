include("${PROJECT_ROOT}/tests/extract_source_region.cmake")
file(READ "${PROJECT_ROOT}/src/ShaderCache.h" _header)
file(READ "${PROJECT_ROOT}/src/ShaderCache.cpp" _source)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
extract_between(
    "${_header}" "template <typename ShaderType" "std::atomic_bool isEnabled"
    "shader_allocation_cache_under_test.h"
)
extract_between(
    "${_source}" "const auto bytecodeSize = shaderData.GetBufferSize();"
    "winrt::com_ptr<ID3D11ShaderReflection> reflector;"
    "shader_allocation_factory_under_test.h"
)
