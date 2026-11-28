/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * エラーコードのテスト。AMARK_ERROR_LIST をテスト内のヘルパマクロで展開して全コード、その文字列、
 * 個数を得て走査する。コードを一覧の末尾に追加しても、このテストを変更する必要はない。
 */
#include <amark/amark.h>

#include <naos/naos.h>

#include <stddef.h>

#define TEST_ERROR_COUNT_ENTRY(name, string) +1
#define TEST_ERROR_CODE_ENTRY(name, string) name,
#define TEST_ERROR_STRING_ENTRY(name, string) string,

enum
{
    test_error_count = 0 AMARK_ERROR_LIST(TEST_ERROR_COUNT_ENTRY)
};

static const amark_error_t test_error_codes[] = {AMARK_ERROR_LIST(TEST_ERROR_CODE_ENTRY)};

static const char *const test_error_strings[] = {AMARK_ERROR_LIST(TEST_ERROR_STRING_ENTRY)};

static const char test_error_unknown[] = "unknown error";

NAOS_TEST(ok_is_zero)
{
    amark_error_t err = AMARK_OK;

    NAOS_CHECK_INT_EQ(0, AMARK_OK);
    NAOS_CHECK_FALSE(err);
}

NAOS_TEST(foundation_codes_are_distinct_failures)
{
    const amark_error_t codes[] = {AMARK_ERROR_NO_MEMORY, AMARK_ERROR_INVALID_ARGUMENT, AMARK_ERROR_OVERFLOW,
                                   AMARK_ERROR_INVALID_UTF8};
    size_t count = sizeof(codes) / sizeof(codes[0]);
    size_t i;
    size_t j;

    for (i = 0; i < count; i++)
    {
        NAOS_CHECK_TRUE(codes[i]);
        for (j = i + 1; j < count; j++)
        {
            NAOS_CHECK_INT_NE(codes[i], codes[j]);
        }
    }
}

NAOS_TEST(list_starts_with_ok)
{
    NAOS_REQUIRE_INT_GE(test_error_count, 5);
    NAOS_CHECK_INT_EQ(AMARK_OK, test_error_codes[0]);
    NAOS_CHECK_UINT_EQ((size_t)test_error_count, sizeof(test_error_codes) / sizeof(test_error_codes[0]));
    NAOS_CHECK_UINT_EQ((size_t)test_error_count, sizeof(test_error_strings) / sizeof(test_error_strings[0]));
}

NAOS_TEST(codes_are_sequential_in_list_order)
{
    int i;

    for (i = 0; i < test_error_count; i++)
    {
        NAOS_CHECK_INT_EQ(i, test_error_codes[i]);
    }
}

NAOS_TEST(strings_match_list)
{
    int i;

    for (i = 0; i < test_error_count; i++)
    {
        const char *str = amark_error_string(test_error_codes[i]);

        NAOS_REQUIRE_NOT_NULL(str);
        NAOS_CHECK_STR_EQ(test_error_strings[i], str);
        NAOS_CHECK_PTR_EQ(str, amark_error_string(test_error_codes[i]));
    }
}

NAOS_TEST(strings_are_not_empty_and_not_unknown)
{
    int i;

    for (i = 0; i < test_error_count; i++)
    {
        const char *str = amark_error_string(test_error_codes[i]);

        NAOS_REQUIRE_NOT_NULL(str);
        NAOS_CHECK_TRUE(str[0] != '\0');
        NAOS_CHECK_STR_NE(test_error_unknown, str);
    }
}

NAOS_TEST(strings_are_mutually_distinct)
{
    int i;
    int j;

    for (i = 0; i < test_error_count; i++)
    {
        for (j = i + 1; j < test_error_count; j++)
        {
            NAOS_CHECK_STR_NE(amark_error_string(test_error_codes[i]), amark_error_string(test_error_codes[j]));
        }
    }
}

NAOS_TEST(value_past_list_is_unknown)
{
    const char *str = amark_error_string((amark_error_t)test_error_count);

    NAOS_REQUIRE_NOT_NULL(str);
    NAOS_CHECK_STR_EQ(test_error_unknown, str);
}

/* clang-format off */
NAOS_SUITE(error,
    NAOS_ADD_TEST(ok_is_zero),
    NAOS_ADD_TEST(foundation_codes_are_distinct_failures),
    NAOS_ADD_TEST(list_starts_with_ok),
    NAOS_ADD_TEST(codes_are_sequential_in_list_order),
    NAOS_ADD_TEST(strings_match_list),
    NAOS_ADD_TEST(strings_are_not_empty_and_not_unknown),
    NAOS_ADD_TEST(strings_are_mutually_distinct),
    NAOS_ADD_TEST(value_past_list_is_unknown)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(error)
)
/* clang-format on */
