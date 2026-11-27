/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * C++ から包括ヘッダを include し、公開 API を C リンケージで呼べる（リンクできる）ことを確かめる。
 * 追記専用。後続の spec は自分の主要 API の呼び出しを、spec 名のコメント付きブロックとして
 * calls_public_api の末尾に追記する（他の spec のブロックを書き換えない）。
 * 呼び出しの確認には NAOS_CHECK* だけを使う（NAOS_REQUIRE* は longjmp で抜けるため、
 * デストラクタを持つ局所変数と併用できない）。
 */
#include <amark/amark.h>

#include <naos/naos.h>

#include <cstring>

NAOS_TEST(calls_public_api)
{
    /* foundation */
    {
        const char *version = amark_version_string();

        NAOS_CHECK_UINT_EQ(AMARK_VERSION_NUMBER, amark_version_number());
        NAOS_CHECK_TRUE(version != nullptr);
        NAOS_CHECK_TRUE(version != nullptr && std::strcmp(version, AMARK_VERSION_STRING) == 0);
    }
}

/* clang-format off */
NAOS_SUITE(cxx_header,
    NAOS_ADD_TEST(calls_public_api)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(cxx_header)
)
/* clang-format on */
