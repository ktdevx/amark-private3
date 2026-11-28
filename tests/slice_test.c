/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * 文字列スライスのテスト。ポインタと長さ・NUL 終端文字列からの生成、NUL を含む全バイト比較、
 * 範囲外指定を切り詰める部分範囲、アロケータ経由の NUL 終端付き複製と確保失敗時の扱いを確かめる。
 * 各テストの後で既定のアロケータに戻す。
 */
#include <amark/amark.h>

#include <naos/naos.h>

#include "support/test_allocator.h"

#include <stdint.h>
#include <string.h>

/* NUL を含む 5 バイトの入力 */
static const char test_nul_bytes[] = {'a', '\0', 'b', '\0', 'c'};

NAOS_TEARDOWN(restore_default)
{
    test_allocator_uninstall();
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_allocator_set(NULL));
}

NAOS_TEST(make_refers_to_given_range)
{
    const char *text = "hello";
    amark_slice_t slice = amark_slice_make(text, 3);

    NAOS_CHECK_TRUE(slice.data == text);
    NAOS_CHECK_UINT_EQ(3u, slice.len);
}

NAOS_TEST(make_keeps_nul_bytes)
{
    amark_slice_t slice = amark_slice_make(test_nul_bytes, sizeof(test_nul_bytes));

    NAOS_CHECK_TRUE(slice.data == test_nul_bytes);
    NAOS_CHECK_UINT_EQ(5u, slice.len);
}

NAOS_TEST(make_with_null_data_is_empty)
{
    amark_slice_t slice = amark_slice_make(NULL, 7);

    NAOS_CHECK_UINT_EQ(0u, slice.len);
}

NAOS_TEST(from_cstr_uses_length_before_nul)
{
    const char *text = "abc";
    amark_slice_t slice = amark_slice_from_cstr(text);

    NAOS_CHECK_TRUE(slice.data == text);
    NAOS_CHECK_UINT_EQ(3u, slice.len);
}

NAOS_TEST(from_cstr_empty_and_null)
{
    amark_slice_t empty = amark_slice_from_cstr("");
    amark_slice_t null_slice = amark_slice_from_cstr(NULL);

    NAOS_CHECK_UINT_EQ(0u, empty.len);
    NAOS_CHECK_UINT_EQ(0u, null_slice.len);
}

NAOS_TEST(equal_compares_length_and_bytes)
{
    static const char padded[] = "xabcx";
    amark_slice_t abc = amark_slice_from_cstr("abc");
    amark_slice_t abc_other = amark_slice_make(&padded[1], 3);
    amark_slice_t abd = amark_slice_from_cstr("abd");
    amark_slice_t ab = amark_slice_from_cstr("ab");

    NAOS_CHECK_TRUE(amark_slice_equal(abc, abc_other));
    NAOS_CHECK_FALSE(amark_slice_equal(abc, abd));
    NAOS_CHECK_FALSE(amark_slice_equal(abc, ab));
    NAOS_CHECK_FALSE(amark_slice_equal(ab, abc));
}

NAOS_TEST(equal_compares_bytes_after_nul)
{
    static const char same[] = {'a', '\0', 'b', '\0', 'c'};
    static const char differs_after_nul[] = {'a', '\0', 'b', '\0', 'd'};
    amark_slice_t original = amark_slice_make(test_nul_bytes, sizeof(test_nul_bytes));
    amark_slice_t copy = amark_slice_make(same, sizeof(same));
    amark_slice_t other = amark_slice_make(differs_after_nul, sizeof(differs_after_nul));
    amark_slice_t prefix = amark_slice_make(test_nul_bytes, 2);

    NAOS_CHECK_TRUE(amark_slice_equal(original, copy));
    NAOS_CHECK_FALSE(amark_slice_equal(original, other));
    NAOS_CHECK_FALSE(amark_slice_equal(original, prefix));
    NAOS_CHECK_FALSE(amark_slice_equal(original, amark_slice_from_cstr("a")));
}

NAOS_TEST(equal_empty_slices_are_equal)
{
    amark_slice_t null_empty = amark_slice_make(NULL, 0);
    amark_slice_t text_empty = amark_slice_make("abc", 0);

    NAOS_CHECK_TRUE(amark_slice_equal(null_empty, text_empty));
    NAOS_CHECK_TRUE(amark_slice_equal(null_empty, null_empty));
    NAOS_CHECK_FALSE(amark_slice_equal(null_empty, amark_slice_from_cstr("a")));
}

NAOS_TEST(sub_within_range)
{
    const char *text = "abcdef";
    amark_slice_t slice = amark_slice_from_cstr(text);
    amark_slice_t sub = amark_slice_sub(slice, 2, 3);

    NAOS_CHECK_TRUE(sub.data == (text + 2));
    NAOS_CHECK_UINT_EQ(3u, sub.len);
    NAOS_CHECK_TRUE(amark_slice_equal(sub, amark_slice_from_cstr("cde")));
}

NAOS_TEST(sub_truncates_len_past_end)
{
    const char *text = "abcdef";
    amark_slice_t slice = amark_slice_from_cstr(text);
    amark_slice_t sub = amark_slice_sub(slice, 4, 10);

    NAOS_CHECK_TRUE(sub.data == (text + 4));
    NAOS_CHECK_UINT_EQ(2u, sub.len);
}

NAOS_TEST(sub_truncates_len_overflow)
{
    const char *text = "abcdef";
    amark_slice_t slice = amark_slice_from_cstr(text);
    amark_slice_t sub = amark_slice_sub(slice, 1, SIZE_MAX);

    NAOS_CHECK_TRUE(sub.data == (text + 1));
    NAOS_CHECK_UINT_EQ(5u, sub.len);
}

NAOS_TEST(sub_offset_at_end_is_empty)
{
    const char *text = "abcdef";
    amark_slice_t slice = amark_slice_from_cstr(text);
    amark_slice_t sub = amark_slice_sub(slice, 6, 1);

    NAOS_CHECK_TRUE(sub.data == (text + 6));
    NAOS_CHECK_UINT_EQ(0u, sub.len);
}

NAOS_TEST(sub_offset_past_end_is_clamped)
{
    const char *text = "abcdef";
    amark_slice_t slice = amark_slice_from_cstr(text);
    amark_slice_t past = amark_slice_sub(slice, 7, 2);
    amark_slice_t far = amark_slice_sub(slice, SIZE_MAX, SIZE_MAX);

    NAOS_CHECK_TRUE(past.data == (text + 6));
    NAOS_CHECK_UINT_EQ(0u, past.len);
    NAOS_CHECK_TRUE(far.data == (text + 6));
    NAOS_CHECK_UINT_EQ(0u, far.len);
}

NAOS_TEST(sub_keeps_nul_bytes)
{
    amark_slice_t slice = amark_slice_make(test_nul_bytes, sizeof(test_nul_bytes));
    amark_slice_t sub = amark_slice_sub(slice, 1, 3);

    NAOS_CHECK_TRUE(sub.data == (test_nul_bytes + 1));
    NAOS_CHECK_UINT_EQ(3u, sub.len);
}

NAOS_TEST(sub_of_empty_slice_is_empty)
{
    amark_slice_t sub = amark_slice_sub(amark_slice_make(NULL, 0), 3, 4);

    NAOS_CHECK_UINT_EQ(0u, sub.len);
}

NAOS_TEST(dup_copies_content_and_terminates)
{
    char *copy;
    test_allocator_stats_t stats;

    test_allocator_install();
    copy = amark_slice_dup(amark_slice_make("hello world", 5));
    NAOS_REQUIRE_NOT_NULL(copy);
    NAOS_CHECK_INT_EQ(0, memcmp(copy, "hello", 5));
    NAOS_CHECK_INT_EQ('\0', copy[5]);

    stats = test_allocator_stats();
    NAOS_CHECK_UINT_EQ(1u, stats.allocations);
    NAOS_CHECK_UINT_EQ(1u, stats.live);
    NAOS_CHECK_UINT_EQ(6u, stats.bytes_live);

    amark_free(copy);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(dup_keeps_nul_bytes)
{
    char *copy;

    test_allocator_install();
    copy = amark_slice_dup(amark_slice_make(test_nul_bytes, sizeof(test_nul_bytes)));
    NAOS_REQUIRE_NOT_NULL(copy);
    NAOS_CHECK_INT_EQ(0, memcmp(copy, test_nul_bytes, sizeof(test_nul_bytes)));
    NAOS_CHECK_INT_EQ('\0', copy[sizeof(test_nul_bytes)]);

    amark_free(copy);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(dup_of_empty_slice_returns_empty_string)
{
    char *from_null;
    char *from_text;

    test_allocator_install();
    from_null = amark_slice_dup(amark_slice_make(NULL, 0));
    from_text = amark_slice_dup(amark_slice_make("abc", 0));
    NAOS_REQUIRE_NOT_NULL(from_null);
    NAOS_REQUIRE_NOT_NULL(from_text);
    NAOS_CHECK_INT_EQ('\0', from_null[0]);
    NAOS_CHECK_INT_EQ('\0', from_text[0]);
    NAOS_CHECK_UINT_EQ(2u, test_allocator_stats().live);
    NAOS_CHECK_UINT_EQ(2u, test_allocator_stats().bytes_live);

    amark_free(from_null);
    amark_free(from_text);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(dup_allocation_failure_returns_null_and_leaves_nothing)
{
    char *copy;
    test_allocator_stats_t stats;

    test_allocator_install();
    test_allocator_fail_after(0);
    copy = amark_slice_dup(amark_slice_from_cstr("abc"));
    NAOS_CHECK_NULL(copy);

    stats = test_allocator_stats();
    NAOS_CHECK_UINT_EQ(1u, stats.failures);
    NAOS_CHECK_UINT_EQ(0u, stats.allocations);
    NAOS_CHECK_UINT_EQ(0u, stats.live);
    NAOS_CHECK_UINT_EQ(0u, stats.bytes_live);
}

NAOS_TEST(dup_of_empty_slice_allocation_failure_returns_null)
{
    char *copy;

    test_allocator_install();
    test_allocator_fail_after(0);
    copy = amark_slice_dup(amark_slice_make(NULL, 0));
    NAOS_CHECK_NULL(copy);
    NAOS_CHECK_UINT_EQ(1u, test_allocator_stats().failures);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(dup_of_size_max_length_returns_null_without_allocating)
{
    char *copy;
    test_allocator_stats_t stats;

    /* len + 1 があふれる長さ。dup は内容を読まずに NULL を返す */
    test_allocator_install();
    copy = amark_slice_dup(amark_slice_make("a", SIZE_MAX));
    NAOS_CHECK_NULL(copy);

    stats = test_allocator_stats();
    NAOS_CHECK_UINT_EQ(0u, stats.allocations);
    NAOS_CHECK_UINT_EQ(0u, stats.failures);
    NAOS_CHECK_UINT_EQ(0u, stats.live);
}

/* clang-format off */
NAOS_SUITE(slice,
    NAOS_SET_TEARDOWN(restore_default),
    NAOS_ADD_TEST(make_refers_to_given_range),
    NAOS_ADD_TEST(make_keeps_nul_bytes),
    NAOS_ADD_TEST(make_with_null_data_is_empty),
    NAOS_ADD_TEST(from_cstr_uses_length_before_nul),
    NAOS_ADD_TEST(from_cstr_empty_and_null),
    NAOS_ADD_TEST(equal_compares_length_and_bytes),
    NAOS_ADD_TEST(equal_compares_bytes_after_nul),
    NAOS_ADD_TEST(equal_empty_slices_are_equal),
    NAOS_ADD_TEST(sub_within_range),
    NAOS_ADD_TEST(sub_truncates_len_past_end),
    NAOS_ADD_TEST(sub_truncates_len_overflow),
    NAOS_ADD_TEST(sub_offset_at_end_is_empty),
    NAOS_ADD_TEST(sub_offset_past_end_is_clamped),
    NAOS_ADD_TEST(sub_keeps_nul_bytes),
    NAOS_ADD_TEST(sub_of_empty_slice_is_empty),
    NAOS_ADD_TEST(dup_copies_content_and_terminates),
    NAOS_ADD_TEST(dup_keeps_nul_bytes),
    NAOS_ADD_TEST(dup_of_empty_slice_returns_empty_string),
    NAOS_ADD_TEST(dup_allocation_failure_returns_null_and_leaves_nothing),
    NAOS_ADD_TEST(dup_of_empty_slice_allocation_failure_returns_null),
    NAOS_ADD_TEST(dup_of_size_max_length_returns_null_without_allocating)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(slice)
)
/* clang-format on */
