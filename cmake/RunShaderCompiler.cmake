foreach(required COMPILER SOURCE ENTRY TARGET OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "RunShaderCompiler requires ${required}")
    endif()
endforeach()

if(DEFINED RUNTIME_DIRECTORY AND NOT "${RUNTIME_DIRECTORY}" STREQUAL "")
    set(ENV{PATH} "${RUNTIME_DIRECTORY};$ENV{PATH}")
endif()

set(
    shader_command
    "${COMPILER}"
    --source "${SOURCE}"
    --entry "${ENTRY}"
    --target "${TARGET}"
    --output "${OUTPUT}"
)
if(DEFINED DEFINE AND NOT "${DEFINE}" STREQUAL "")
    list(APPEND shader_command --define "${DEFINE}")
endif()

execute_process(
    COMMAND ${shader_command}
    RESULT_VARIABLE shader_result
    OUTPUT_VARIABLE shader_output
    ERROR_VARIABLE shader_error
)
if(NOT "${shader_result}" STREQUAL "0")
    message(FATAL_ERROR
        "HeniaUIShaderCompiler exited with ${shader_result}\n"
        "${shader_output}${shader_error}"
    )
endif()
