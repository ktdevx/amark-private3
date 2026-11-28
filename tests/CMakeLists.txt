# テスト。トップレベル構成で AMARK_BUILD_TESTS と BUILD_TESTING が真のときだけ読み込まれる。
#
# 追記専用: 後続の spec は spec 名のコメント付きブロックを末尾に追記し、自分のモジュールの
# テストを次の形で登録する（他の spec のブロックや本 spec の行を書き換えない）。
#   naos_add_executable(amark_<module>_test SOURCES <module>_test.c LIBRARIES Amark::amark amark_test_support)
#   amark_target_warnings(amark_<module>_test)
# CTest 名は amark_<module>_test.<suite>.<test> になる。テストは公開ヘッダ（<amark/...>）と
# テスト補助のヘッダ（"support/test_<area>.h"）だけを include し、src/ を include しない。
# テスト補助を追加する spec は、同じブロックで support/test_<area>.{h,c} を作り、
#   target_sources(amark_test_support PRIVATE support/test_<area>.c)
# で amark_test_support のソース一覧に追記する。

# このディレクトリのターゲット（テスト実行ファイルとヘッダ検査）は C99・拡張無効、C++ は C++11 とする
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_language(CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(Naos
    GIT_REPOSITORY https://github.com/vabelorg/naos.git
    GIT_TAG v0.4.0
)
FetchContent_MakeAvailable(Naos)

# foundation
# テスト補助の静的ライブラリ。ライブラリ本体には含めず、公開 API だけを使う。補助のヘッダは
# tests/ からの相対パス（"support/test_<area>.h"）で include する
add_library(amark_test_support STATIC support/test_allocator.c)
target_include_directories(amark_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(amark_test_support PUBLIC Amark::amark)
amark_target_warnings(amark_test_support)

naos_add_executable(amark_version_test SOURCES version_test.c LIBRARIES Amark::amark amark_test_support)
amark_target_warnings(amark_version_test)
# ビルド定義が version.h から読んだバージョンをテストへ渡し、ヘッダの値と一致することを確かめる
target_compile_definitions(amark_version_test PRIVATE TEST_PROJECT_VERSION="${PROJECT_VERSION}")

naos_add_executable(amark_error_test SOURCES error_test.c LIBRARIES Amark::amark amark_test_support)
amark_target_warnings(amark_error_test)

naos_add_executable(amark_memory_test SOURCES memory_test.c LIBRARIES Amark::amark amark_test_support)
amark_target_warnings(amark_memory_test)

naos_add_executable(amark_slice_test SOURCES slice_test.c LIBRARIES Amark::amark amark_test_support)
amark_target_warnings(amark_slice_test)

naos_add_executable(amark_utf8_test SOURCES utf8_test.c LIBRARIES Amark::amark amark_test_support)
amark_target_warnings(amark_utf8_test)

naos_add_executable(amark_buffer_test SOURCES buffer_test.c LIBRARIES Amark::amark amark_test_support)
amark_target_warnings(amark_buffer_test)

# C++ から包括ヘッダを include して公開 API を呼べることを確かめる（後続 spec は
# cxx_header_test.cpp に自分のブロックを追記する。ここの登録は変えない）
naos_add_executable(amark_cxx_header_test SOURCES cxx_header_test.cpp LIBRARIES Amark::amark amark_test_support)
amark_target_warnings(amark_cxx_header_test)

# 公開ヘッダの単独 include 検査。include/amark/*.h のそれぞれについて、そのヘッダだけを
# include する翻訳単位をビルドツリーに生成し、オブジェクトライブラリとして既定のビルドに含める。
# Amark::amark からは公開の include パスと定義だけが伝わり、src/ は include パスに入らないため、
# 公開ヘッダが非公開ヘッダに依存していればコンパイルに失敗する。公開ヘッダを追加すると
# 次の構成で自動的に対象になる（ここを書き換える必要はない）。
file(GLOB amark_public_headers CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/include/amark/*.h")
set(amark_header_check_sources "")
foreach(amark_header IN LISTS amark_public_headers)
    get_filename_component(amark_header_name "${amark_header}" NAME_WE)
    set(amark_header_check_source "${CMAKE_CURRENT_BINARY_DIR}/header_check/${amark_header_name}_header_check.c")
    # ヘッダがマクロだけでも空の翻訳単位にならないよう、ダミーの宣言を 1 つ置く
    file(CONFIGURE OUTPUT "${amark_header_check_source}" CONTENT
"/* tests/CMakeLists.txt が生成: <amark/${amark_header_name}.h> が単独でコンパイルできることを確かめる */
#include <amark/${amark_header_name}.h>

extern int header_check_${amark_header_name};
" @ONLY)
    list(APPEND amark_header_check_sources "${amark_header_check_source}")
endforeach()
add_library(amark_header_check OBJECT ${amark_header_check_sources})
target_link_libraries(amark_header_check PRIVATE Amark::amark)
amark_target_warnings(amark_header_check)

# ソースヘッダ表記の検査。include/、src/、tests/ の *.c、*.h、*.cpp の先頭 5 行以内に
# 著作権表記と SPDX 表記があることを cmake -P で確かめる（コンパイラに依存せず全 OS で動く）。
# 対象のファイルは実行のたびに列挙するため、後続 spec のソースは自動的に対象になる。
add_test(NAME amark.source_headers
    COMMAND "${CMAKE_COMMAND}" "-DAMARK_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            -P "${PROJECT_SOURCE_DIR}/cmake/AmarkCheckSourceHeaders.cmake")

# node-tree
naos_add_executable(amark_node_kind_test SOURCES node_kind_test.c LIBRARIES Amark::amark amark_test_support)
amark_target_warnings(amark_node_kind_test)

naos_add_executable(amark_node_registry_test SOURCES node_registry_test.c LIBRARIES Amark::amark amark_test_support)
amark_target_warnings(amark_node_registry_test)

naos_add_executable(amark_node_test SOURCES node_test.c LIBRARIES Amark::amark amark_test_support)
amark_target_warnings(amark_node_test)

naos_add_executable(amark_node_value_test SOURCES node_value_test.c LIBRARIES Amark::amark amark_test_support)
amark_target_warnings(amark_node_value_test)

naos_add_executable(amark_node_edit_test SOURCES node_edit_test.c LIBRARIES Amark::amark amark_test_support)
amark_target_warnings(amark_node_edit_test)

naos_add_executable(amark_node_iter_test SOURCES node_iter_test.c LIBRARIES Amark::amark amark_test_support)
amark_target_warnings(amark_node_iter_test)
