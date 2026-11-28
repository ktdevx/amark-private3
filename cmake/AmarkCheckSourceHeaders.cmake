# ソースヘッダ表記の検査。cmake -DAMARK_SOURCE_DIR=<dir> -P AmarkCheckSourceHeaders.cmake で実行する。
#
# <dir> の include/、src/、tests/ にある *.c、*.h、*.cpp のそれぞれについて、先頭 5 行以内に
#   Copyright (c) <年> The Amark Authors
#   SPDX-License-Identifier: MIT
# の両方があることを確かめる。欠けたファイルがあれば、そのパスをすべて列挙して失敗する。

if(NOT DEFINED AMARK_SOURCE_DIR OR NOT IS_DIRECTORY "${AMARK_SOURCE_DIR}")
    message(FATAL_ERROR "AMARK_SOURCE_DIR must name the Amark source directory (got '${AMARK_SOURCE_DIR}')")
endif()

set(amark_head_lines 5)
set(amark_copyright_regex "Copyright \\(c\\) [0-9][0-9][0-9][0-9] The Amark Authors")
set(amark_spdx_regex "SPDX-License-Identifier: MIT")

set(amark_globs "")
foreach(amark_dir IN ITEMS include src tests)
    foreach(amark_ext IN ITEMS c h cpp)
        list(APPEND amark_globs "${AMARK_SOURCE_DIR}/${amark_dir}/*.${amark_ext}")
    endforeach()
endforeach()
file(GLOB_RECURSE amark_sources LIST_DIRECTORIES false ${amark_globs})
list(SORT amark_sources)

# 対象が 1 つもなければ AMARK_SOURCE_DIR の指定誤りとみなす（何も検査せずに成功させない）
if(NOT amark_sources)
    message(FATAL_ERROR "No source files found under ${AMARK_SOURCE_DIR}/{include,src,tests}")
endif()

set(amark_missing "")
foreach(amark_source IN LISTS amark_sources)
    # 先頭の 5 行を取り出す。行の内容をリストに分けると ; が区切りとして扱われるため、
    # 改行の位置を順にたどって先頭からの部分文字列として切り出す。
    file(READ "${amark_source}" amark_rest LIMIT 8192)
    set(amark_head "")
    foreach(amark_line RANGE 1 ${amark_head_lines})
        string(FIND "${amark_rest}" "\n" amark_newline)
        if(amark_newline EQUAL -1)
            string(APPEND amark_head "${amark_rest}")
            break()
        endif()
        math(EXPR amark_next "${amark_newline} + 1")
        string(SUBSTRING "${amark_rest}" 0 ${amark_next} amark_line_text)
        string(APPEND amark_head "${amark_line_text}")
        string(SUBSTRING "${amark_rest}" ${amark_next} -1 amark_rest)
    endforeach()

    if(NOT amark_head MATCHES "${amark_copyright_regex}" OR NOT amark_head MATCHES "${amark_spdx_regex}")
        file(RELATIVE_PATH amark_relative "${AMARK_SOURCE_DIR}" "${amark_source}")
        list(APPEND amark_missing "${amark_relative}")
    endif()
endforeach()

if(amark_missing)
    list(LENGTH amark_missing amark_missing_count)
    list(JOIN amark_missing "\n  " amark_missing_text)
    message(FATAL_ERROR
        "${amark_missing_count} source file(s) lack the source header "
        "(Copyright (c) <year> The Amark Authors / SPDX-License-Identifier: MIT within the first "
        "${amark_head_lines} lines):\n  ${amark_missing_text}")
endif()

list(LENGTH amark_sources amark_source_count)
message(STATUS "Source headers OK (${amark_source_count} files)")
