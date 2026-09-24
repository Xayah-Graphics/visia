include_guard(GLOBAL)
FetchContent_Declare(miniz
        URL "https://codeload.github.com/richgel999/miniz/tar.gz/refs/tags/3.1.2"
        SOURCE_SUBDIR visia-unused SYSTEM EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(miniz)
add_library(visia_miniz STATIC
        "${miniz_SOURCE_DIR}/miniz.c"
        "${miniz_SOURCE_DIR}/miniz_zip.c"
        "${miniz_SOURCE_DIR}/miniz_tinfl.c"
        "${miniz_SOURCE_DIR}/miniz_tdef.c")
include(GenerateExportHeader)
generate_export_header(visia_miniz BASE_NAME miniz EXPORT_FILE_NAME "${PROJECT_BINARY_DIR}/generated/miniz_export.h")
target_compile_definitions(visia_miniz PUBLIC MINIZ_STATIC_DEFINE)
target_include_directories(visia_miniz SYSTEM PUBLIC "${miniz_SOURCE_DIR}" "${PROJECT_BINARY_DIR}/generated")
