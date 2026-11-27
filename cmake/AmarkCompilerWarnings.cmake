# amark_target_warnings(<target>)
#
# コンパイラ別の警告を <target> に PRIVATE で付ける（利用者へ伝播させない）。
# ライブラリとテストのターゲットにだけ適用する。
#   GCC / Clang: -Wall -Wextra -Wpedantic
#   MSVC:        /W4
# AMARK_WARNINGS_AS_ERRORS が真なら -Werror / /WX を加える。
function(amark_target_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(AMARK_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(AMARK_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
