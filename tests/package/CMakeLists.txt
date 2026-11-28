# インストール済みの Amark を find_package で見つけて使えることを確かめる最小の利用者プロジェクト。
# 本体のビルドには含めない（tests/CMakeLists.txt から add_subdirectory しない）。CI がインストール後に
#   cmake -S tests/package -B build-package -DCMAKE_PREFIX_PATH=<インストール先>
# で別途構成・ビルドし、amark_package_consumer を実行して終了コードで結果を確かめる。
cmake_minimum_required(VERSION 3.21)
project(amark_package_consumer LANGUAGES C)

find_package(Amark 0.1 REQUIRED)

add_executable(amark_package_consumer main.c)
set_target_properties(amark_package_consumer PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_link_libraries(amark_package_consumer PRIVATE Amark::amark)

# Windows の動的構成では、実行ファイルが Amark の DLL を見つけられるよう実行ファイルの隣へコピーする。
# 静的構成では $<TARGET_RUNTIME_DLLS> が空になり、コピー先だけを渡した copy_if_different が失敗するため、
# インポートした Amark::amark が動的ライブラリのときだけコピーの手順を加える。
get_target_property(amark_library_type Amark::amark TYPE)
if(WIN32 AND (amark_library_type STREQUAL "SHARED_LIBRARY"))
    add_custom_command(TARGET amark_package_consumer POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                $<TARGET_RUNTIME_DLLS:amark_package_consumer> $<TARGET_FILE_DIR:amark_package_consumer>
        COMMAND_EXPAND_LISTS
    )
endif()
