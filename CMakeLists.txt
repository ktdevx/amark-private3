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

add_subdirectory(src)
