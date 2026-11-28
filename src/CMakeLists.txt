# ライブラリターゲット amark。
# 追記専用: 後続の spec は spec 名のコメント付きブロックを末尾に追記し、自分のソースを
# target_sources(amark PRIVATE ...) で src/ からの相対パスで列挙する（glob は使わない）。

add_library(amark)
add_library(Amark::amark ALIAS amark)

# 1.0 前は 0.<minor>、以後は <major>
if(PROJECT_VERSION_MAJOR EQUAL 0)
    set(amark_soversion "0.${PROJECT_VERSION_MINOR}")
else()
    set(amark_soversion "${PROJECT_VERSION_MAJOR}")
endif()

set_target_properties(amark PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    DEBUG_POSTFIX d
    OUTPUT_NAME amark
    VERSION ${PROJECT_VERSION}
    SOVERSION ${amark_soversion}
    EXPORT_NAME amark
)

target_include_directories(amark
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_compile_definitions(amark PRIVATE AMARK_BUILDING)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(amark PUBLIC AMARK_SHARED)
endif()

amark_target_warnings(amark)

# foundation
target_sources(amark PRIVATE
    base/version.c
    base/error.c
    base/memory.c
    base/slice.c
    base/utf8.c
    base/buffer.c
)
