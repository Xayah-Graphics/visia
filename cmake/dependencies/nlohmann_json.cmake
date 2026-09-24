include_guard(GLOBAL)
FetchContent_Declare(nlohmann_json
        URL "https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.tar.gz"
        URL_HASH SHA256=4B92EB0C06D10683F7447CE9406CB97CD4B453BE18D7279320F7B2F025C10187
        SYSTEM EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(nlohmann_json)
