include_guard(GLOBAL)
set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "")
set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "")
set(GLFW_INSTALL OFF CACHE INTERNAL "")
FetchContent_Declare(glfw
        URL "https://codeload.github.com/glfw/glfw/zip/refs/tags/3.5.1"
        SYSTEM EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(glfw)
