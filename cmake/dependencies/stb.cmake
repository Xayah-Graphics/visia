include_guard(GLOBAL)
FetchContent_Declare(stb
        URL "https://github.com/nothings/stb/archive/28d546d5eb77d4585506a20480f4de2e706dff4c.tar.gz"
        URL_HASH SHA256=4EF16A0E174BC33887FEC582B01CA239155466E0B48081CC27304298556BED47
        SOURCE_SUBDIR visia-unused SYSTEM EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(stb)
add_library(visia_stb INTERFACE)
target_include_directories(visia_stb SYSTEM INTERFACE "${stb_SOURCE_DIR}")
