cmake_minimum_required(VERSION 3.21)

# バージョンの唯一の出所は include/amark/version.h。project() の前にそこから 3 つの数値を読む。
# ここにバージョンの数値を書かない。
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/amark/version.h" amark_version_lines
     REGEX "^#define AMARK_VERSION_(MAJOR|MINOR|PATCH) +[0-9]+$")
foreach(amark_part IN ITEMS MAJOR MINOR PATCH)
    string(REGEX REPLACE ".*#define AMARK_VERSION_${amark_part} +([0-9]+).*" "\\1"
           AMARK_VERSION_${amark_part} "${amark_version_lines}")
    if(NOT AMARK_VERSION_${amark_part} MATCHES "^[0-9]+$")
        message(FATAL_ERROR "include/amark/version.h does not define AMARK_VERSION_${amark_part}")
    endif()
endforeach()
set(AMARK_VERSION "${AMARK_VERSION_MAJOR}.${AMARK_VERSION_MINOR}.${AMARK_VERSION_PATCH}")
project(amark VERSION ${AMARK_VERSION} LANGUAGES C)

option(BUILD_SHARED_LIBS "Build Amark as a shared library" OFF)
option(AMARK_BUILD_TESTS "Configure the Amark tests (also requires BUILD_TESTING)" ${PROJECT_IS_TOP_LEVEL})
option(AMARK_INSTALL "Generate the Amark install rules" ${PROJECT_IS_TOP_LEVEL})
option(AMARK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)
include(AmarkCompilerWarnings)

# トップレベル時は実行ファイルと DLL を同じディレクトリに出し、動的構成でもビルド直後の
# テストの列挙（Naos が実行ファイルを起動する）で Windows が DLL を見つけられるようにする。
# Linux / macOS はビルドツリーの rpath で解決する。
if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
    if(NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
    endif()
endif()

add_subdirectory(src)

# テストと Naos の取得はトップレベル構成でだけ行う。サブプロジェクトとして取り込まれた場合は
# AMARK_BUILD_TESTS の値によらず取得しない。
if(PROJECT_IS_TOP_LEVEL AND AMARK_BUILD_TESTS AND BUILD_TESTING)
    add_subdirectory(tests)
endif()
