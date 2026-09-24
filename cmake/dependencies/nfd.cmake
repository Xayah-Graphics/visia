include_guard(GLOBAL)
set(NFD_BUILD_TESTS OFF CACHE INTERNAL "")
set(NFD_INSTALL OFF CACHE INTERNAL "")
FetchContent_Declare(nfd
        URL "https://codeload.github.com/btzy/nativefiledialog-extended/tar.gz/refs/tags/v1.3.0"
        SYSTEM EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(nfd)
