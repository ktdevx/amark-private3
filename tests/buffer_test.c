/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * 可変長バッファのテスト。確保を伴わない初期化、各種の追記と NUL 終端、容量の予約と伸長、
 * 消去・切り詰め、所有権の取り出し、後片付けと再利用、スライスとしての参照、確保失敗とサイズの
 * あふれに対する強い保証、多数回の追記での再確保回数を確かめる。各テストの後で既定のアロケータに戻す。
 */
#include <amark/amark.h>

#include <naos/naos.h>

#include "support/test_allocator.h"

#include <stdint.h>
#include <string.h>

/* 失敗注入の網羅で、各操作の前の状態を写し取る */
typedef struct test_buffer_snapshot
{
    char *data;
    size_t len;
    size_t cap;
    char content[512];
} test_buffer_snapshot_t;

static void test_buffer_take_snapshot(const amark_buffer_t *buf, test_buffer_snapshot_t *snapshot)
{
    snapshot->data = buf->data;
    snapshot->len = buf->len;
    snapshot->cap = buf->cap;
    if (buf->data)
    {
        memcpy(snapshot->content, buf->data, buf->len + 1);
    }
}

/* buf が snapshot と同じ領域・長さ・容量・内容（NUL 終端を含む）を持つときだけ 1 を返す */
static int test_buffer_matches_snapshot(const amark_buffer_t *buf, const test_buffer_snapshot_t *snapshot)
{
    if ((buf->data != snapshot->data) || (buf->len != snapshot->len) || (buf->cap != snapshot->cap))
    {
        return 0;
    }

    if (!buf->data)
    {
        return 1;
    }

    return memcmp(buf->data, snapshot->content, buf->len + 1) == 0;
}

/* 網羅パターンで実行する操作の列。index 番目の操作を buf に行う */
static amark_error_t test_buffer_run_step(amark_buffer_t *buf, size_t index)
{
    static const char long_text[] = "0123456789abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz"
                                    "0123456789abcdefghijklmnopqrstuvwxyz";

    switch (index)
    {
    case 0:
        return amark_buffer_append(buf, "hello", 5);
    case 1:
        return amark_buffer_append_cstr(buf, ", world and more text");
    case 2:
        return amark_buffer_append_slice(buf, amark_slice_make(long_text, sizeof(long_text) - 1));
    case 3:
        return amark_buffer_append_codepoint(buf, 0x1F600u);
    case 4:
        return amark_buffer_reserve(buf, 300);
    case 5:
        return amark_buffer_append_char(buf, '!');
    default:
        return AMARK_OK;
    }
}

enum
{
    test_buffer_step_count = 6
};

/* 操作の列をすべて成功させたときの内容 */
static const char test_buffer_expected[] = "hello, world and more text"
                                           "0123456789abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz"
                                           "0123456789abcdefghijklmnopqrstuvwxyz"
                                           "\xF0\x9F\x98\x80"
                                           "!";

NAOS_TEARDOWN(restore_default)
{
    test_allocator_uninstall();
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_allocator_set(NULL));
}

NAOS_TEST(init_allocates_nothing)
{
    amark_buffer_t buf;
    test_allocator_stats_t stats;

    test_allocator_install();
    memset(&buf, 0xA5, sizeof(buf));
    amark_buffer_init(&buf);
    NAOS_CHECK_NULL(buf.data);
    NAOS_CHECK_UINT_EQ(0u, buf.len);
    NAOS_CHECK_UINT_EQ(0u, buf.cap);

    stats = test_allocator_stats();
    NAOS_CHECK_UINT_EQ(0u, stats.allocations);
    NAOS_CHECK_UINT_EQ(0u, stats.bytes_live);

    amark_buffer_deinit(&buf);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().deallocations);
}

NAOS_TEST(append_bytes_and_terminate)
{
    amark_buffer_t buf;

    test_allocator_install();
    amark_buffer_init(&buf);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append(&buf, "abc", 3));
    NAOS_REQUIRE_NOT_NULL(buf.data);
    NAOS_CHECK_UINT_EQ(3u, buf.len);
    NAOS_CHECK_TRUE(buf.cap >= 3u);
    NAOS_CHECK_STR_EQ("abc", buf.data);

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append(&buf, "de", 2));
    NAOS_CHECK_UINT_EQ(5u, buf.len);
    NAOS_CHECK_STR_EQ("abcde", buf.data);
    NAOS_CHECK_INT_EQ('\0', buf.data[buf.len]);

    amark_buffer_deinit(&buf);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(append_keeps_nul_bytes)
{
    static const char bytes[] = {'a', '\0', 'b'};
    amark_buffer_t buf;

    amark_buffer_init(&buf);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append(&buf, bytes, sizeof(bytes)));
    NAOS_CHECK_UINT_EQ(3u, buf.len);
    NAOS_CHECK_INT_EQ(0, memcmp(buf.data, bytes, sizeof(bytes)));
    NAOS_CHECK_INT_EQ('\0', buf.data[3]);
    amark_buffer_deinit(&buf);
}

NAOS_TEST(append_zero_length_does_nothing)
{
    amark_buffer_t buf;

    test_allocator_install();
    amark_buffer_init(&buf);
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_buffer_append(&buf, NULL, 0));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_buffer_append(&buf, "abc", 0));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_buffer_append_cstr(&buf, ""));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_buffer_append_slice(&buf, amark_slice_make(NULL, 0)));
    NAOS_CHECK_NULL(buf.data);
    NAOS_CHECK_UINT_EQ(0u, buf.len);
    NAOS_CHECK_UINT_EQ(0u, buf.cap);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().allocations);
}

NAOS_TEST(append_char_cstr_and_slice)
{
    static const char padded[] = "xyzw";
    amark_buffer_t buf;

    amark_buffer_init(&buf);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append_char(&buf, 'a'));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append_cstr(&buf, "bcd"));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append_slice(&buf, amark_slice_make(&padded[1], 2)));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append_char(&buf, '\0'));
    NAOS_CHECK_UINT_EQ(7u, buf.len);
    NAOS_CHECK_INT_EQ(0, memcmp(buf.data, "abcdyz\0", 8));
    amark_buffer_deinit(&buf);
}

NAOS_TEST(append_codepoint_encodes_utf8)
{
    amark_buffer_t buf;

    amark_buffer_init(&buf);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append_codepoint(&buf, 0x41u));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append_codepoint(&buf, 0xE9u));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append_codepoint(&buf, 0x3042u));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append_codepoint(&buf, 0x10FFFFu));
    NAOS_CHECK_UINT_EQ(10u, buf.len);
    NAOS_CHECK_STR_EQ("A\xC3\xA9\xE3\x81\x82\xF4\x8F\xBF\xBF", buf.data);
    amark_buffer_deinit(&buf);
}

NAOS_TEST(append_invalid_codepoint_is_rejected)
{
    amark_buffer_t buf;
    test_buffer_snapshot_t before;

    test_allocator_install();
    amark_buffer_init(&buf);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_UTF8, amark_buffer_append_codepoint(&buf, 0xD800u));
    NAOS_CHECK_NULL(buf.data);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().allocations);

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append_cstr(&buf, "ab"));
    test_buffer_take_snapshot(&buf, &before);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_UTF8, amark_buffer_append_codepoint(&buf, 0xDFFFu));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_UTF8, amark_buffer_append_codepoint(&buf, 0x110000u));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_UTF8, amark_buffer_append_codepoint(&buf, UINT32_MAX));
    NAOS_CHECK_TRUE(test_buffer_matches_snapshot(&buf, &before));

    amark_buffer_deinit(&buf);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(null_arguments_are_rejected)
{
    amark_buffer_t buf;
    char *data = NULL;
    size_t len = 0;
    amark_slice_t slice;

    amark_buffer_init(&buf);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_reserve(NULL, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_append(NULL, "a", 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_append(&buf, NULL, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_append_char(NULL, 'a'));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_append_cstr(NULL, "a"));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_append_cstr(&buf, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_append_slice(NULL, amark_slice_from_cstr("a")));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_append_codepoint(NULL, 0x41u));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_truncate(NULL, 0));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_detach(NULL, &data, &len));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_detach(&buf, NULL, &len));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_detach(&buf, &data, NULL));
    NAOS_CHECK_NULL(data);
    NAOS_CHECK_UINT_EQ(0u, len);
    NAOS_CHECK_NULL(buf.data);
    NAOS_CHECK_UINT_EQ(0u, buf.len);

    /* void を返す後片付け系は NULL を受け取っても何もしない */
    amark_buffer_init(NULL);
    amark_buffer_deinit(NULL);
    amark_buffer_clear(NULL);
    slice = amark_buffer_as_slice(NULL);
    NAOS_CHECK_UINT_EQ(0u, slice.len);
}

NAOS_TEST(reserve_grows_capacity_without_changing_content)
{
    amark_buffer_t buf;
    size_t cap;

    test_allocator_install();
    amark_buffer_init(&buf);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_reserve(&buf, 0));
    NAOS_CHECK_NULL(buf.data);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().allocations);

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_reserve(&buf, 1));
    NAOS_REQUIRE_NOT_NULL(buf.data);
    NAOS_CHECK_UINT_EQ(0u, buf.len);
    NAOS_CHECK_UINT_EQ(16u, buf.cap);
    NAOS_CHECK_INT_EQ('\0', buf.data[0]);
    NAOS_CHECK_UINT_EQ(17u, test_allocator_stats().bytes_live);

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append_cstr(&buf, "abc"));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_reserve(&buf, 13));
    NAOS_CHECK_UINT_EQ(16u, buf.cap);
    NAOS_CHECK_UINT_EQ(1u, test_allocator_stats().allocations);

    /* 足りなければ max(必要容量, 2 倍, 16) に伸ばす */
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_reserve(&buf, 14));
    NAOS_CHECK_UINT_EQ(32u, buf.cap);
    NAOS_CHECK_STR_EQ("abc", buf.data);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_reserve(&buf, 100));
    NAOS_CHECK_UINT_EQ(103u, buf.cap);
    NAOS_CHECK_STR_EQ("abc", buf.data);
    NAOS_CHECK_UINT_EQ(104u, test_allocator_stats().bytes_live);

    cap = buf.cap;
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_reserve(&buf, 1));
    NAOS_CHECK_UINT_EQ(cap, buf.cap);

    amark_buffer_deinit(&buf);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(clear_keeps_capacity)
{
    amark_buffer_t buf;
    char *data;
    size_t cap;

    amark_buffer_init(&buf);
    amark_buffer_clear(&buf);
    NAOS_CHECK_NULL(buf.data);
    NAOS_CHECK_UINT_EQ(0u, buf.len);

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append_cstr(&buf, "hello"));
    data = buf.data;
    cap = buf.cap;
    amark_buffer_clear(&buf);
    NAOS_CHECK_TRUE(buf.data == data);
    NAOS_CHECK_UINT_EQ(cap, buf.cap);
    NAOS_CHECK_UINT_EQ(0u, buf.len);
    NAOS_CHECK_STR_EQ("", buf.data);

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append_cstr(&buf, "x"));
    NAOS_CHECK_STR_EQ("x", buf.data);
    amark_buffer_deinit(&buf);
}

NAOS_TEST(truncate_shortens_and_keeps_capacity)
{
    amark_buffer_t buf;
    char *data;
    size_t cap;

    amark_buffer_init(&buf);
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_buffer_truncate(&buf, 0));
    NAOS_CHECK_NULL(buf.data);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_truncate(&buf, 1));
    NAOS_CHECK_NULL(buf.data);

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append_cstr(&buf, "hello"));
    data = buf.data;
    cap = buf.cap;
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_buffer_truncate(&buf, 5));
    NAOS_CHECK_STR_EQ("hello", buf.data);
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_buffer_truncate(&buf, 2));
    NAOS_CHECK_TRUE(buf.data == data);
    NAOS_CHECK_UINT_EQ(cap, buf.cap);
    NAOS_CHECK_UINT_EQ(2u, buf.len);
    NAOS_CHECK_STR_EQ("he", buf.data);
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_buffer_truncate(&buf, 0));
    NAOS_CHECK_UINT_EQ(0u, buf.len);
    NAOS_CHECK_STR_EQ("", buf.data);
    amark_buffer_deinit(&buf);
}

NAOS_TEST(truncate_past_length_is_rejected)
{
    amark_buffer_t buf;
    test_buffer_snapshot_t before;

    amark_buffer_init(&buf);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append_cstr(&buf, "abc"));
    test_buffer_take_snapshot(&buf, &before);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_truncate(&buf, 4));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_buffer_truncate(&buf, SIZE_MAX));
    NAOS_CHECK_TRUE(test_buffer_matches_snapshot(&buf, &before));
    amark_buffer_deinit(&buf);
}

NAOS_TEST(detach_transfers_ownership)
{
    amark_buffer_t buf;
    char *data = NULL;
    char *area;
    size_t len = 0;

    test_allocator_install();
    amark_buffer_init(&buf);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append_cstr(&buf, "owned"));
    area = buf.data;
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_detach(&buf, &data, &len));
    NAOS_CHECK_TRUE(data == area);
    NAOS_CHECK_UINT_EQ(5u, len);
    NAOS_CHECK_STR_EQ("owned", data);
    NAOS_CHECK_NULL(buf.data);
    NAOS_CHECK_UINT_EQ(0u, buf.len);
    NAOS_CHECK_UINT_EQ(0u, buf.cap);
    NAOS_CHECK_UINT_EQ(1u, test_allocator_stats().live);

    amark_buffer_deinit(&buf);
    NAOS_CHECK_UINT_EQ(1u, test_allocator_stats().live);
    amark_free(data);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(detach_of_empty_buffer_returns_empty_string)
{
    amark_buffer_t buf;
    char *data = NULL;
    size_t len = 99;

    test_allocator_install();
    amark_buffer_init(&buf);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_detach(&buf, &data, &len));
    NAOS_REQUIRE_NOT_NULL(data);
    NAOS_CHECK_UINT_EQ(0u, len);
    NAOS_CHECK_INT_EQ('\0', data[0]);
    NAOS_CHECK_NULL(buf.data);
    NAOS_CHECK_UINT_EQ(1u, test_allocator_stats().live);
    NAOS_CHECK_UINT_EQ(1u, test_allocator_stats().bytes_live);

    amark_free(data);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(detach_of_empty_buffer_allocation_failure_changes_nothing)
{
    static char sentinel[] = "sentinel";
    amark_buffer_t buf;
    char *data = sentinel;
    size_t len = 99;

    test_allocator_install();
    amark_buffer_init(&buf);
    test_allocator_fail_after(0);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NO_MEMORY, amark_buffer_detach(&buf, &data, &len));
    NAOS_CHECK_TRUE(data == sentinel);
    NAOS_CHECK_UINT_EQ(99u, len);
    NAOS_CHECK_NULL(buf.data);
    NAOS_CHECK_UINT_EQ(0u, buf.len);
    NAOS_CHECK_UINT_EQ(0u, buf.cap);
    NAOS_CHECK_UINT_EQ(1u, test_allocator_stats().failures);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(deinit_frees_and_allows_reuse)
{
    amark_buffer_t buf;
    test_allocator_stats_t stats;

    test_allocator_install();
    amark_buffer_init(&buf);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append_cstr(&buf, "first"));
    amark_buffer_deinit(&buf);
    NAOS_CHECK_NULL(buf.data);
    NAOS_CHECK_UINT_EQ(0u, buf.len);
    NAOS_CHECK_UINT_EQ(0u, buf.cap);

    stats = test_allocator_stats();
    NAOS_CHECK_UINT_EQ(1u, stats.deallocations);
    NAOS_CHECK_UINT_EQ(0u, stats.live);
    NAOS_CHECK_UINT_EQ(0u, stats.bytes_live);

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append_cstr(&buf, "second"));
    NAOS_CHECK_STR_EQ("second", buf.data);
    amark_buffer_deinit(&buf);
    amark_buffer_deinit(&buf);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(as_slice_borrows_content)
{
    amark_buffer_t buf;
    amark_slice_t slice;

    amark_buffer_init(&buf);
    slice = amark_buffer_as_slice(&buf);
    NAOS_CHECK_UINT_EQ(0u, slice.len);

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append_cstr(&buf, "view"));
    slice = amark_buffer_as_slice(&buf);
    NAOS_CHECK_TRUE(slice.data == buf.data);
    NAOS_CHECK_UINT_EQ(4u, slice.len);
    NAOS_CHECK_TRUE(amark_slice_equal(slice, amark_slice_from_cstr("view")));
    amark_buffer_deinit(&buf);
}

NAOS_TEST(allocation_failure_keeps_content_for_every_pattern)
{
    size_t successes;
    int completed = 0;

    /* successes 回の確保の後で失敗させる注入を、操作の列が失敗なく完走するまで 0 から順に試す */
    for (successes = 0; (successes < 64) && !completed; successes++)
    {
        amark_buffer_t buf;
        size_t step;
        int failed = 0;

        test_allocator_install();
        test_allocator_fail_after(successes);
        amark_buffer_init(&buf);

        for (step = 0; (step < test_buffer_step_count) && !failed; step++)
        {
            test_buffer_snapshot_t before;
            amark_error_t err;

            test_buffer_take_snapshot(&buf, &before);
            err = test_buffer_run_step(&buf, step);
            if (err == AMARK_ERROR_NO_MEMORY)
            {
                NAOS_CHECK_TRUE(test_buffer_matches_snapshot(&buf, &before));
                failed = 1;
            }
            else
            {
                NAOS_CHECK_INT_EQ(AMARK_OK, err);
            }
        }

        if (!failed)
        {
            NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().failures);
            NAOS_CHECK_UINT_EQ(sizeof(test_buffer_expected) - 1, buf.len);
            NAOS_CHECK_INT_EQ(0, memcmp(buf.data, test_buffer_expected, sizeof(test_buffer_expected)));
            completed = 1;
        }
        else
        {
            NAOS_CHECK_UINT_EQ(1u, test_allocator_stats().failures);
        }

        amark_buffer_deinit(&buf);
        NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
        NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().bytes_live);
        test_allocator_uninstall();
    }

    /* 操作の列は複数回の確保を伴うため、少なくとも 1 つの注入で失敗したうえで完走する */
    NAOS_CHECK_TRUE(completed);
    NAOS_CHECK_TRUE(successes > 1u);
}

NAOS_TEST(overflow_is_detected_before_allocation)
{
    amark_buffer_t buf;
    test_buffer_snapshot_t before;
    test_allocator_stats_t stats;

    test_allocator_install();
    amark_buffer_init(&buf);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OVERFLOW, amark_buffer_reserve(&buf, SIZE_MAX));
    NAOS_CHECK_NULL(buf.data);
    NAOS_CHECK_UINT_EQ(0u, buf.cap);

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append_cstr(&buf, "abc"));
    test_buffer_take_snapshot(&buf, &before);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OVERFLOW, amark_buffer_reserve(&buf, SIZE_MAX));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OVERFLOW, amark_buffer_reserve(&buf, (SIZE_MAX - 3)));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OVERFLOW, amark_buffer_append(&buf, "x", SIZE_MAX));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OVERFLOW, amark_buffer_append(&buf, "x", (SIZE_MAX - 3)));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OVERFLOW, amark_buffer_append_slice(&buf, amark_slice_make("x", (SIZE_MAX - 3))));
    NAOS_CHECK_TRUE(test_buffer_matches_snapshot(&buf, &before));

    stats = test_allocator_stats();
    NAOS_CHECK_UINT_EQ(1u, stats.allocations);
    NAOS_CHECK_UINT_EQ(0u, stats.failures);

    amark_buffer_deinit(&buf);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(largest_non_overflowing_reserve_requests_allocation)
{
    amark_buffer_t buf;
    test_buffer_snapshot_t before;

    /* len + additional + 1 がちょうど SIZE_MAX になる要求はあふれではなく確保を試みる（注入で失敗させる） */
    test_allocator_install();
    amark_buffer_init(&buf);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append_cstr(&buf, "abc"));
    test_buffer_take_snapshot(&buf, &before);
    test_allocator_fail_after(0);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NO_MEMORY, amark_buffer_reserve(&buf, (SIZE_MAX - 4)));
    NAOS_CHECK_UINT_EQ(1u, test_allocator_stats().failures);
    NAOS_CHECK_TRUE(test_buffer_matches_snapshot(&buf, &before));

    amark_buffer_deinit(&buf);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(many_single_byte_appends_reallocate_logarithmically)
{
    amark_buffer_t buf;
    size_t i;
    size_t mismatches = 0;
    test_allocator_stats_t stats;

    test_allocator_install();
    amark_buffer_init(&buf);
    for (i = 0; i < 100000u; i++)
    {
        NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_buffer_append_char(&buf, (char)('a' + (i % 26))));
    }
    NAOS_CHECK_UINT_EQ(100000u, buf.len);
    NAOS_CHECK_INT_EQ('\0', buf.data[buf.len]);
    for (i = 0; i < buf.len; i++)
    {
        if (buf.data[i] != (char)('a' + (i % 26)))
        {
            mismatches++;
        }
    }
    NAOS_CHECK_UINT_EQ(0u, mismatches);

    stats = test_allocator_stats();
    NAOS_CHECK_TRUE(stats.allocations <= 64u);

    amark_buffer_deinit(&buf);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

/* clang-format off */
NAOS_SUITE(buffer,
    NAOS_SET_TEARDOWN(restore_default),
    NAOS_ADD_TEST(init_allocates_nothing),
    NAOS_ADD_TEST(append_bytes_and_terminate),
    NAOS_ADD_TEST(append_keeps_nul_bytes),
    NAOS_ADD_TEST(append_zero_length_does_nothing),
    NAOS_ADD_TEST(append_char_cstr_and_slice),
    NAOS_ADD_TEST(append_codepoint_encodes_utf8),
    NAOS_ADD_TEST(append_invalid_codepoint_is_rejected),
    NAOS_ADD_TEST(null_arguments_are_rejected),
    NAOS_ADD_TEST(reserve_grows_capacity_without_changing_content),
    NAOS_ADD_TEST(clear_keeps_capacity),
    NAOS_ADD_TEST(truncate_shortens_and_keeps_capacity),
    NAOS_ADD_TEST(truncate_past_length_is_rejected),
    NAOS_ADD_TEST(detach_transfers_ownership),
    NAOS_ADD_TEST(detach_of_empty_buffer_returns_empty_string),
    NAOS_ADD_TEST(detach_of_empty_buffer_allocation_failure_changes_nothing),
    NAOS_ADD_TEST(deinit_frees_and_allows_reuse),
    NAOS_ADD_TEST(as_slice_borrows_content),
    NAOS_ADD_TEST(allocation_failure_keeps_content_for_every_pattern),
    NAOS_ADD_TEST(overflow_is_detected_before_allocation),
    NAOS_ADD_TEST(largest_non_overflowing_reserve_requests_allocation),
    NAOS_ADD_TEST(many_single_byte_appends_reallocate_logarithmically)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(buffer)
)
/* clang-format on */
