# ============================================================================
# Project
# ============================================================================
cmake_minimum_required(VERSION 4.4)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/VERSION" VISIA_VERSION LIMIT_COUNT 1)
project(visia VERSION "${VISIA_VERSION}" LANGUAGES C CXX)

# ============================================================================
# Project Configuration
# ============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_MODULE_STD ON)

if (NOT WIN32)
    message(FATAL_ERROR "Visia requires Windows")
endif ()

add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8> $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus> $<$<COMPILE_LANGUAGE:CXX>:/Zc:preprocessor>)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/cxx-modules")

# ============================================================================
# Dependencies
# ============================================================================
include(cmake/Dependencies.cmake)
visia_require_dependency(vulkan)
visia_require_dependency(glfw)
visia_require_dependency(slang)
visia_require_dependency(stb)
visia_require_dependency(miniz)
visia_require_dependency(nlohmann_json)
visia_require_dependency(nfd)
visia_require_dependency(imgui)
include(cmake/Shaders.cmake)

# ============================================================================
# Executable
# ============================================================================
add_executable(visia)
target_sources(visia
        PRIVATE
        main.cpp
        visia/canvas.cpp
        visia/document.cpp
        visia/window.cpp
        visia/renderer.cpp
        visia/app.cpp
        PRIVATE FILE_SET cxx_modules TYPE CXX_MODULES FILES
        visia/canvas.ixx
        visia/document.ixx
        visia/window.ixx
        visia/renderer.ixx
        visia/app.ixx)
target_link_libraries(visia PRIVATE visia_vulkan glfw nfd visia_stb visia_miniz visia_imgui nlohmann_json::nlohmann_json dwmapi)
target_compile_definitions(visia PRIVATE GLFW_INCLUDE_NONE VISIA_SHADER_DIRECTORY="${VISIA_SHADER_OUTPUT_DIRECTORY}")
target_link_options(visia PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
add_dependencies(visia visia_shaders)
