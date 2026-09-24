include_guard(GLOBAL)
find_package(Vulkan 1.4 REQUIRED GLOBAL)
list(GET Vulkan_INCLUDE_DIRS 0 VISIA_VULKAN_INCLUDE_DIRECTORY)
cmake_path(GET VISIA_VULKAN_INCLUDE_DIRECTORY PARENT_PATH VISIA_VULKAN_SDK_DIRECTORY)
add_library(visia_vulkan STATIC)
target_sources(visia_vulkan PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS "${VISIA_VULKAN_INCLUDE_DIRECTORY}"
        FILES "${VISIA_VULKAN_INCLUDE_DIRECTORY}/vulkan/vulkan.cppm")
target_link_libraries(visia_vulkan PUBLIC Vulkan::Vulkan)
target_compile_definitions(visia_vulkan PUBLIC VK_USE_PLATFORM_WIN32_KHR NOMINMAX)
