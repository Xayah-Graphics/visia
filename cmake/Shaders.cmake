set(VISIA_SHADER_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/generated/shaders")
set(VISIA_SHADER_BINARIES)
foreach (shader IN ITEMS grid picture shadow shape group)
    foreach (stage IN ITEMS vertex fragment)
        set(output "${VISIA_SHADER_OUTPUT_DIRECTORY}/${shader}_${stage}.spv")
        add_custom_command(OUTPUT "${output}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${VISIA_SHADER_OUTPUT_DIRECTORY}"
                COMMAND "${VISIA_SLANG_COMPILER}" "${PROJECT_SOURCE_DIR}/visia/shaders/canvas.slang"
                -target spirv -profile spirv_1_6 -entry ${shader}_${stage} -stage ${stage}
                -emit-spirv-directly -fvk-use-entrypoint-name -fvk-use-c-layout -O2 -o "${output}"
                DEPENDS "${PROJECT_SOURCE_DIR}/visia/shaders/canvas.slang" VERBATIM)
        list(APPEND VISIA_SHADER_BINARIES "${output}")
    endforeach ()
endforeach ()
add_custom_target(visia_shaders DEPENDS ${VISIA_SHADER_BINARIES})
