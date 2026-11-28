/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * バージョン情報のテスト。ヘッダの定数、数値から組み立てた文字列、実行時の値、ビルド定義が
 * version.h から読んだバージョン（TEST_PROJECT_VERSION としてビルドから渡される）の一致を確かめる。
 */
#include <amark/amark.h>

#include <naos/naos.h>

#include <stdio.h>

NAOS_TEST(parts_are_in_range)
{
    NAOS_CHECK_INT_GE(AMARK_VERSION_MAJOR, 0);
    NAOS_CHECK_INT_LE(AMARK_VERSION_MAJOR, 255);
    NAOS_CHECK_INT_GE(AMARK_VERSION_MINOR, 0);
    NAOS_CHECK_INT_LE(AMARK_VERSION_MINOR, 255);
    NAOS_CHECK_INT_GE(AMARK_VERSION_PATCH, 0);
    NAOS_CHECK_INT_LE(AMARK_VERSION_PATCH, 255);
}

NAOS_TEST(encode_packs_parts)
{
    NAOS_CHECK_UINT_EQ(0x010203ul, AMARK_VERSION_ENCODE(1, 2, 3));
    NAOS_CHECK_UINT_EQ(0xFFFFFFul, AMARK_VERSION_ENCODE(255, 255, 255));
    NAOS_CHECK_UINT_EQ(0ul, AMARK_VERSION_ENCODE(0, 0, 0));
}

NAOS_TEST(number_is_built_from_parts)
{
    unsigned long expected = ((unsigned long)AMARK_VERSION_MAJOR << 16) | ((unsigned long)AMARK_VERSION_MINOR << 8) |
                             (unsigned long)AMARK_VERSION_PATCH;

    NAOS_CHECK_UINT_EQ(expected, AMARK_VERSION_NUMBER);
}

NAOS_TEST(string_is_built_from_parts)
{
    char expected[16];

    snprintf(expected, sizeof(expected), "%d.%d.%d", AMARK_VERSION_MAJOR, AMARK_VERSION_MINOR, AMARK_VERSION_PATCH);

    NAOS_CHECK_STR_EQ(expected, AMARK_VERSION_STRING);
}

NAOS_TEST(runtime_number_matches_header)
{
    NAOS_CHECK_UINT_EQ(AMARK_VERSION_NUMBER, amark_version_number());
}

NAOS_TEST(runtime_string_matches_header)
{
    NAOS_REQUIRE_TRUE(amark_version_string() != NULL);
    NAOS_CHECK_STR_EQ(AMARK_VERSION_STRING, amark_version_string());
}

NAOS_TEST(build_version_matches_header)
{
    NAOS_CHECK_STR_EQ(TEST_PROJECT_VERSION, AMARK_VERSION_STRING);
}

/* clang-format off */
NAOS_SUITE(version,
    NAOS_ADD_TEST(parts_are_in_range),
    NAOS_ADD_TEST(encode_packs_parts),
    NAOS_ADD_TEST(number_is_built_from_parts),
    NAOS_ADD_TEST(string_is_built_from_parts),
    NAOS_ADD_TEST(runtime_number_matches_header),
    NAOS_ADD_TEST(runtime_string_matches_header),
    NAOS_ADD_TEST(build_version_matches_header)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(version)
)
/* clang-format on */
