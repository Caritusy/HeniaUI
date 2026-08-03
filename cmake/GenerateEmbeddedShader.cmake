foreach(required SOURCE VERTEX PIXEL OUTPUT NAMESPACE PACKAGE_KEY)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "GenerateEmbeddedShader.cmake requires ${required}")
    endif()
endforeach()

file(READ "${SOURCE}" shader_source)
file(READ "${VERTEX}" vertex_hex HEX)
file(READ "${PIXEL}" pixel_hex HEX)
file(SHA256 "${SOURCE}" source_hash)
file(SHA256 "${VERTEX}" vertex_hash)
file(SHA256 "${PIXEL}" pixel_hash)

string(REGEX REPLACE "(..)" "0x\\1," vertex_bytes "${vertex_hex}")
string(REGEX REPLACE "(..)" "0x\\1," pixel_bytes "${pixel_hex}")

set(variant_declaration "")
if(DEFINED PIXEL_VARIANT AND NOT PIXEL_VARIANT STREQUAL "")
    file(READ "${PIXEL_VARIANT}" variant_hex HEX)
    file(SHA256 "${PIXEL_VARIANT}" variant_hash)
    string(REGEX REPLACE "(..)" "0x\\1," variant_bytes "${variant_hex}")
    set(variant_declaration
        "inline constexpr unsigned char kPixelVariantShader[] = {${variant_bytes}};\n")
else()
    set(variant_hash "none")
endif()

string(SHA256 package_version
    "${source_hash}|${vertex_hash}|${pixel_hash}|${variant_hash}|${PACKAGE_KEY}")

if(shader_source MATCHES "\\)HENIA_SHADER\"")
    message(FATAL_ERROR "Shader source contains the generated raw-string delimiter")
endif()

# MSVC limits the size of each individual string-literal token. Adjacent raw
# literals are concatenated by C++ while keeping every generated token small.
string(LENGTH "${shader_source}" shader_source_length)
set(shader_source_offset 0)
set(shader_source_literals "")
while(shader_source_offset LESS shader_source_length)
    math(EXPR shader_source_remaining "${shader_source_length} - ${shader_source_offset}")
    if(shader_source_remaining GREATER 8000)
        set(shader_source_chunk_length 8000)
    else()
        set(shader_source_chunk_length ${shader_source_remaining})
    endif()
    string(SUBSTRING "${shader_source}"
        ${shader_source_offset} ${shader_source_chunk_length} shader_source_chunk)
    string(APPEND shader_source_literals
        "R\"HENIA_SHADER(${shader_source_chunk})HENIA_SHADER\"\n")
    math(EXPR shader_source_offset
        "${shader_source_offset} + ${shader_source_chunk_length}")
endwhile()

get_filename_component(output_directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")
file(WRITE "${OUTPUT}"
"#pragma once

#include <string_view>

namespace ${NAMESPACE} {

inline constexpr std::string_view kVersion = \"${package_version}\";
inline constexpr unsigned char kVertexShader[] = {${vertex_bytes}};
inline constexpr unsigned char kPixelShader[] = {${pixel_bytes}};
${variant_declaration}inline constexpr char kSource[] =
${shader_source_literals};

} // namespace ${NAMESPACE}
")
