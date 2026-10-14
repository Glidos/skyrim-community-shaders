set(_pbr_texture_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/pbr-textures")
set(_pbr_texture_headers)
foreach(_name constants collection visitor)
    list(APPEND _pbr_texture_headers "${_pbr_texture_dir}/pbr_texture_${_name}_under_test.h")
endforeach()
add_custom_command(OUTPUT ${_pbr_texture_headers}
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT_DIRECTORY=${_pbr_texture_dir}" -P
        "${PROJECT_SOURCE_DIR}/tests/extract_pbr_texture_enumeration.cmake"
    DEPENDS src/TruePBR/BSLightingShaderMaterialPBRLandscape.h
        src/TruePBR/BSLightingShaderMaterialPBRLandscape.cpp src/TruePBR/TextureEnumeration.cpp
        tests/extract_source_region.cmake tests/extract_pbr_texture_enumeration.cmake
    VERBATIM)
add_controller_test(pbr_texture_enumeration_test PBRTextureEnumeration tests/pbr_texture_enumeration_test.cpp)
target_sources(pbr_texture_enumeration_test PRIVATE ${_pbr_texture_headers})
target_include_directories(pbr_texture_enumeration_test PRIVATE "${_pbr_texture_dir}")
