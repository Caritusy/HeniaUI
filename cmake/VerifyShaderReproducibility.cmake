foreach(required COMPILER SOURCE OUTPUT_DIRECTORY ENTRY TARGET)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "VerifyShaderReproducibility.cmake requires ${required}")
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}/first" "${OUTPUT_DIRECTORY}/second")
set(first_source "${OUTPUT_DIRECTORY}/first/shader.hlsl")
set(second_source "${OUTPUT_DIRECTORY}/second/shader.hlsl")
configure_file("${SOURCE}" "${first_source}" COPYONLY)
configure_file("${SOURCE}" "${second_source}" COPYONLY)
set(first "${OUTPUT_DIRECTORY}/first/shader.cso")
set(second "${OUTPUT_DIRECTORY}/second/shader.cso")

foreach(index RANGE 0 1)
    if(index EQUAL 0)
        set(input "${first_source}")
        set(output "${first}")
    else()
        set(input "${second_source}")
        set(output "${second}")
    endif()
    execute_process(
        COMMAND "${COMPILER}"
            --source "${input}"
            --entry "${ENTRY}"
            --target "${TARGET}"
            --output "${output}"
        RESULT_VARIABLE compile_result
        OUTPUT_VARIABLE compile_output
        ERROR_VARIABLE compile_error
    )
    if(NOT compile_result EQUAL 0)
        message(FATAL_ERROR
            "Reproducibility shader compile failed:\n${compile_output}${compile_error}")
    endif()
endforeach()

file(SHA256 "${first}" first_hash)
file(SHA256 "${second}" second_hash)
if(NOT first_hash STREQUAL second_hash)
    message(FATAL_ERROR
        "Repeated HLSL compilation was not reproducible: ${first_hash} != ${second_hash}")
endif()
