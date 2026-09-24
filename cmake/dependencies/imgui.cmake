include_guard(GLOBAL)
FetchContent_Declare(imgui
        URL "https://codeload.github.com/ocornut/imgui/zip/b334d19b667958ed970000073644d911fae17e57"
        URL_HASH SHA256=504BC8171B80B8C92F035EBC899F6B3086C9CFA56EFADEE4962753DEB38626A2
        SOURCE_SUBDIR visia-unused SYSTEM EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(imgui)
add_library(visia_imgui STATIC
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp")
target_include_directories(visia_imgui SYSTEM PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
target_compile_definitions(visia_imgui PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(visia_imgui PUBLIC glfw Vulkan::Vulkan)
