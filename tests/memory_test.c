/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * グローバルアロケータのテスト。既定アロケータ、指定した関数への委譲、差し替えの拒否・復帰・問い合わせ、
 * サイズ 0 と積のあふれの扱い、計測アロケータのバイト数の統計を確かめる。各テストの後で既定に戻す。
 */
#include <amark/amark.h>

#include <naos/naos.h>

#include "support/test_allocator.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* 委譲の確認用に、呼ばれた回数を数えて C 標準ライブラリへ中継する関数 */
static size_t test_counting_malloc_calls;
static size_t test_counting_realloc_calls;
static size_t test_counting_calloc_calls;
static size_t test_counting_free_calls;

static void *test_counting_malloc(size_t size)
{
    test_counting_malloc_calls++;
    return malloc(size);
}

static void *test_counting_realloc(void *ptr, size_t size)
{
    test_counting_realloc_calls++;
    return realloc(ptr, size);
}

static void *test_counting_calloc(size_t n, size_t size)
{
    test_counting_calloc_calls++;
    return calloc(n, size);
}

static void test_counting_free(void *ptr)
{
    test_counting_free_calls++;
    free(ptr);
}

static amark_allocator_t test_counting_allocator(void)
{
    amark_allocator_t allocator;

    allocator.malloc_fn = test_counting_malloc;
    allocator.realloc_fn = test_counting_realloc;
    allocator.calloc_fn = test_counting_calloc;
    allocator.free_fn = test_counting_free;
    return allocator;
}

/* 既定アロケータ。各テストの前に（前のテストの後片付けで既定に戻った状態から）問い合わせて控える */
static amark_allocator_t test_default_allocator;

static void test_check_is_default(const amark_allocator_t *allocator)
{
    NAOS_CHECK_TRUE(allocator->malloc_fn == test_default_allocator.malloc_fn);
    NAOS_CHECK_TRUE(allocator->realloc_fn == test_default_allocator.realloc_fn);
    NAOS_CHECK_TRUE(allocator->calloc_fn == test_default_allocator.calloc_fn);
    NAOS_CHECK_TRUE(allocator->free_fn == test_default_allocator.free_fn);
}

NAOS_SETUP(reset_counters)
{
    test_counting_malloc_calls = 0;
    test_counting_realloc_calls = 0;
    test_counting_calloc_calls = 0;
    test_counting_free_calls = 0;
    amark_allocator_get(&test_default_allocator);
}

NAOS_TEARDOWN(restore_default)
{
    test_allocator_uninstall();
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_allocator_set(NULL));
}

NAOS_TEST(default_allocator_functions_work)
{
    amark_allocator_t allocator;
    unsigned char *p;
    unsigned char *z;
    size_t i;

    memset(&allocator, 0, sizeof(allocator));
    amark_allocator_get(&allocator);
    NAOS_REQUIRE_TRUE(allocator.malloc_fn != NULL);
    NAOS_REQUIRE_TRUE(allocator.realloc_fn != NULL);
    NAOS_REQUIRE_TRUE(allocator.calloc_fn != NULL);
    NAOS_REQUIRE_TRUE(allocator.free_fn != NULL);
    test_check_is_default(&allocator);

    p = (unsigned char *)allocator.malloc_fn(8);
    NAOS_REQUIRE_NOT_NULL(p);
    memset(p, 0x5A, 8);
    p = (unsigned char *)allocator.realloc_fn(p, 64);
    NAOS_REQUIRE_NOT_NULL(p);
    for (i = 0; i < 8; i++)
    {
        NAOS_CHECK_UINT_EQ(0x5Au, p[i]);
    }
    allocator.free_fn(p);

    z = (unsigned char *)allocator.calloc_fn(4, 4);
    NAOS_REQUIRE_NOT_NULL(z);
    for (i = 0; i < 16; i++)
    {
        NAOS_CHECK_UINT_EQ(0u, z[i]);
    }
    allocator.free_fn(z);
}

NAOS_TEST(default_allocator_allocates_and_frees)
{
    unsigned char *p = (unsigned char *)amark_malloc(16);
    unsigned char *z;
    size_t i;

    NAOS_REQUIRE_NOT_NULL(p);
    memset(p, 0xAB, 16);

    p = (unsigned char *)amark_realloc(p, 4096);
    NAOS_REQUIRE_NOT_NULL(p);
    for (i = 0; i < 16; i++)
    {
        NAOS_CHECK_UINT_EQ(0xABu, p[i]);
    }
    amark_free(p);

    z = (unsigned char *)amark_calloc(8, 32);
    NAOS_REQUIRE_NOT_NULL(z);
    for (i = 0; i < (8 * 32); i++)
    {
        NAOS_CHECK_UINT_EQ(0u, z[i]);
    }
    amark_free(z);
}

NAOS_TEST(get_with_null_does_nothing)
{
    amark_allocator_get(NULL);
    NAOS_PASS();
}

NAOS_TEST(set_delegates_to_given_functions)
{
    amark_allocator_t allocator = test_counting_allocator();
    amark_allocator_t current;
    unsigned char *p;
    unsigned char *z;

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_allocator_set(&allocator));

    memset(&current, 0, sizeof(current));
    amark_allocator_get(&current);
    NAOS_CHECK_TRUE(current.malloc_fn == test_counting_malloc);
    NAOS_CHECK_TRUE(current.realloc_fn == test_counting_realloc);
    NAOS_CHECK_TRUE(current.calloc_fn == test_counting_calloc);
    NAOS_CHECK_TRUE(current.free_fn == test_counting_free);

    p = (unsigned char *)amark_malloc(8);
    NAOS_REQUIRE_NOT_NULL(p);
    NAOS_CHECK_UINT_EQ(1u, test_counting_malloc_calls);

    p = (unsigned char *)amark_realloc(p, 64);
    NAOS_REQUIRE_NOT_NULL(p);
    NAOS_CHECK_UINT_EQ(1u, test_counting_realloc_calls);

    z = (unsigned char *)amark_calloc(4, 4);
    NAOS_REQUIRE_NOT_NULL(z);
    NAOS_CHECK_UINT_EQ(1u, test_counting_calloc_calls);

    amark_free(p);
    amark_free(z);
    NAOS_CHECK_UINT_EQ(2u, test_counting_free_calls);

    NAOS_CHECK_UINT_EQ(1u, test_counting_malloc_calls);
    NAOS_CHECK_UINT_EQ(1u, test_counting_realloc_calls);
    NAOS_CHECK_UINT_EQ(1u, test_counting_calloc_calls);
}

NAOS_TEST(set_copies_the_allocator)
{
    amark_allocator_t allocator = test_counting_allocator();
    amark_allocator_t current;
    void *p;

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_allocator_set(&allocator));
    /* 渡した構造体を後から書き換えても、現在のアロケータは変わらない */
    memset(&allocator, 0, sizeof(allocator));

    amark_allocator_get(&current);
    NAOS_CHECK_TRUE(current.malloc_fn == test_counting_malloc);
    NAOS_CHECK_TRUE(current.free_fn == test_counting_free);

    p = amark_malloc(1);
    NAOS_REQUIRE_NOT_NULL(p);
    amark_free(p);
    NAOS_CHECK_UINT_EQ(1u, test_counting_malloc_calls);
    NAOS_CHECK_UINT_EQ(1u, test_counting_free_calls);
}

NAOS_TEST(set_rejects_missing_function)
{
    amark_allocator_t installed = test_counting_allocator();
    int missing;

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_allocator_set(&installed));

    for (missing = 0; missing < 4; missing++)
    {
        amark_allocator_t allocator = test_counting_allocator();
        amark_allocator_t current;

        switch (missing)
        {
        case 0:
            allocator.malloc_fn = NULL;
            break;
        case 1:
            allocator.realloc_fn = NULL;
            break;
        case 2:
            allocator.calloc_fn = NULL;
            break;
        default:
            allocator.free_fn = NULL;
            break;
        }
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_allocator_set(&allocator));

        memset(&current, 0, sizeof(current));
        amark_allocator_get(&current);
        NAOS_CHECK_TRUE(current.malloc_fn == test_counting_malloc);
        NAOS_CHECK_TRUE(current.realloc_fn == test_counting_realloc);
        NAOS_CHECK_TRUE(current.calloc_fn == test_counting_calloc);
        NAOS_CHECK_TRUE(current.free_fn == test_counting_free);
    }
}

NAOS_TEST(set_rejects_all_missing_from_default)
{
    amark_allocator_t allocator;
    amark_allocator_t current;

    memset(&allocator, 0, sizeof(allocator));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_allocator_set(&allocator));

    amark_allocator_get(&current);
    test_check_is_default(&current);
}

NAOS_TEST(set_null_restores_default)
{
    amark_allocator_t allocator = test_counting_allocator();
    amark_allocator_t current;
    void *p;

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_allocator_set(&allocator));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_allocator_set(NULL));

    amark_allocator_get(&current);
    test_check_is_default(&current);

    p = amark_malloc(32);
    NAOS_REQUIRE_NOT_NULL(p);
    amark_free(p);
    NAOS_CHECK_UINT_EQ(0u, test_counting_malloc_calls);
    NAOS_CHECK_UINT_EQ(0u, test_counting_free_calls);
}

NAOS_TEST(zero_size_requests_return_null_without_calling)
{
    amark_allocator_t allocator = test_counting_allocator();

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_allocator_set(&allocator));

    NAOS_CHECK_NULL(amark_malloc(0));
    NAOS_CHECK_NULL(amark_calloc(0, 16));
    NAOS_CHECK_NULL(amark_calloc(16, 0));
    NAOS_CHECK_NULL(amark_calloc(0, 0));
    NAOS_CHECK_NULL(amark_realloc(NULL, 0));
    amark_free(NULL);

    NAOS_CHECK_UINT_EQ(0u, test_counting_malloc_calls);
    NAOS_CHECK_UINT_EQ(0u, test_counting_realloc_calls);
    NAOS_CHECK_UINT_EQ(0u, test_counting_calloc_calls);
    NAOS_CHECK_UINT_EQ(0u, test_counting_free_calls);
}

NAOS_TEST(realloc_null_acts_as_malloc)
{
    amark_allocator_t allocator = test_counting_allocator();
    void *p;

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_allocator_set(&allocator));

    p = amark_realloc(NULL, 24);
    NAOS_REQUIRE_NOT_NULL(p);
    NAOS_CHECK_UINT_EQ(1u, test_counting_malloc_calls);
    NAOS_CHECK_UINT_EQ(0u, test_counting_realloc_calls);
    amark_free(p);
}

NAOS_TEST(realloc_to_zero_frees)
{
    amark_allocator_t allocator = test_counting_allocator();
    void *p;

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_allocator_set(&allocator));

    p = amark_malloc(24);
    NAOS_REQUIRE_NOT_NULL(p);
    NAOS_CHECK_NULL(amark_realloc(p, 0));
    NAOS_CHECK_UINT_EQ(0u, test_counting_realloc_calls);
    NAOS_CHECK_UINT_EQ(1u, test_counting_free_calls);
}

NAOS_TEST(calloc_overflow_returns_null_without_calling)
{
    amark_allocator_t allocator = test_counting_allocator();

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_allocator_set(&allocator));

    NAOS_CHECK_NULL(amark_calloc(SIZE_MAX, 2));
    NAOS_CHECK_NULL(amark_calloc(2, SIZE_MAX));
    NAOS_CHECK_NULL(amark_calloc(SIZE_MAX / 2 + 1, 2));
    NAOS_CHECK_UINT_EQ(0u, test_counting_calloc_calls);
    NAOS_CHECK_UINT_EQ(0u, test_counting_malloc_calls);
}

NAOS_TEST(test_allocator_zero_size_and_overflow_change_nothing)
{
    test_allocator_stats_t stats;
    void *p;

    test_allocator_install();

    NAOS_CHECK_NULL(amark_malloc(0));
    NAOS_CHECK_NULL(amark_calloc(0, 8));
    NAOS_CHECK_NULL(amark_calloc(8, 0));
    NAOS_CHECK_NULL(amark_realloc(NULL, 0));
    NAOS_CHECK_NULL(amark_calloc(SIZE_MAX, 2));
    amark_free(NULL);

    stats = test_allocator_stats();
    NAOS_CHECK_UINT_EQ(0u, stats.allocations);
    NAOS_CHECK_UINT_EQ(0u, stats.deallocations);
    NAOS_CHECK_UINT_EQ(0u, stats.failures);
    NAOS_CHECK_UINT_EQ(0u, stats.live);
    NAOS_CHECK_UINT_EQ(0u, stats.bytes_total);

    /* サイズ 0 への再確保は解放として数える */
    p = amark_malloc(10);
    NAOS_REQUIRE_NOT_NULL(p);
    NAOS_CHECK_NULL(amark_realloc(p, 0));
    stats = test_allocator_stats();
    NAOS_CHECK_UINT_EQ(1u, stats.allocations);
    NAOS_CHECK_UINT_EQ(1u, stats.deallocations);
    NAOS_CHECK_UINT_EQ(0u, stats.live);
    NAOS_CHECK_UINT_EQ(0u, stats.bytes_live);
    NAOS_CHECK_UINT_EQ(10u, stats.bytes_total);
    NAOS_CHECK_UINT_EQ(10u, stats.bytes_peak);
}

NAOS_TEST(test_allocator_tracks_bytes)
{
    test_allocator_stats_t stats;
    unsigned char *a;
    unsigned char *b;
    unsigned char *c;
    size_t i;

    test_allocator_install();
    stats = test_allocator_stats();
    NAOS_CHECK_UINT_EQ(0u, stats.allocations);
    NAOS_CHECK_UINT_EQ(0u, stats.bytes_peak);

    a = (unsigned char *)amark_malloc(100);
    NAOS_REQUIRE_NOT_NULL(a);
    memset(a, 0x5A, 100);
    stats = test_allocator_stats();
    NAOS_CHECK_UINT_EQ(1u, stats.allocations);
    NAOS_CHECK_UINT_EQ(1u, stats.live);
    NAOS_CHECK_UINT_EQ(100u, stats.bytes_live);
    NAOS_CHECK_UINT_EQ(100u, stats.bytes_total);
    NAOS_CHECK_UINT_EQ(100u, stats.bytes_peak);

    b = (unsigned char *)amark_calloc(5, 10);
    NAOS_REQUIRE_NOT_NULL(b);
    for (i = 0; i < 50; i++)
    {
        NAOS_CHECK_UINT_EQ(0u, b[i]);
    }
    stats = test_allocator_stats();
    NAOS_CHECK_UINT_EQ(2u, stats.allocations);
    NAOS_CHECK_UINT_EQ(2u, stats.live);
    NAOS_CHECK_UINT_EQ(150u, stats.bytes_live);
    NAOS_CHECK_UINT_EQ(150u, stats.bytes_total);
    NAOS_CHECK_UINT_EQ(150u, stats.bytes_peak);

    /* 拡大: 生存バイト数は旧サイズから新サイズへ置き換わり、累計には新サイズが加わる */
    a = (unsigned char *)amark_realloc(a, 300);
    NAOS_REQUIRE_NOT_NULL(a);
    for (i = 0; i < 100; i++)
    {
        NAOS_CHECK_UINT_EQ(0x5Au, a[i]);
    }
    stats = test_allocator_stats();
    NAOS_CHECK_UINT_EQ(3u, stats.allocations);
    NAOS_CHECK_UINT_EQ(2u, stats.live);
    NAOS_CHECK_UINT_EQ(350u, stats.bytes_live);
    NAOS_CHECK_UINT_EQ(450u, stats.bytes_total);
    NAOS_CHECK_UINT_EQ(350u, stats.bytes_peak);

    /* 縮小: 最大値は保たれる */
    a = (unsigned char *)amark_realloc(a, 20);
    NAOS_REQUIRE_NOT_NULL(a);
    for (i = 0; i < 20; i++)
    {
        NAOS_CHECK_UINT_EQ(0x5Au, a[i]);
    }
    stats = test_allocator_stats();
    NAOS_CHECK_UINT_EQ(4u, stats.allocations);
    NAOS_CHECK_UINT_EQ(2u, stats.live);
    NAOS_CHECK_UINT_EQ(70u, stats.bytes_live);
    NAOS_CHECK_UINT_EQ(470u, stats.bytes_total);
    NAOS_CHECK_UINT_EQ(350u, stats.bytes_peak);

    /* realloc(NULL, n) は確保として数える */
    c = (unsigned char *)amark_realloc(NULL, 7);
    NAOS_REQUIRE_NOT_NULL(c);
    stats = test_allocator_stats();
    NAOS_CHECK_UINT_EQ(5u, stats.allocations);
    NAOS_CHECK_UINT_EQ(3u, stats.live);
    NAOS_CHECK_UINT_EQ(77u, stats.bytes_live);
    NAOS_CHECK_UINT_EQ(477u, stats.bytes_total);

    amark_free(a);
    stats = test_allocator_stats();
    NAOS_CHECK_UINT_EQ(1u, stats.deallocations);
    NAOS_CHECK_UINT_EQ(2u, stats.live);
    NAOS_CHECK_UINT_EQ(57u, stats.bytes_live);

    amark_free(b);
    amark_free(c);
    stats = test_allocator_stats();
    NAOS_CHECK_UINT_EQ(5u, stats.allocations);
    NAOS_CHECK_UINT_EQ(3u, stats.deallocations);
    NAOS_CHECK_UINT_EQ(0u, stats.live);
    NAOS_CHECK_UINT_EQ(0u, stats.bytes_live);
    NAOS_CHECK_UINT_EQ(477u, stats.bytes_total);
    NAOS_CHECK_UINT_EQ(350u, stats.bytes_peak);
    NAOS_CHECK_UINT_EQ(0u, stats.failures);
}

NAOS_TEST(test_allocator_returns_aligned_blocks)
{
    void *p;

    test_allocator_install();
    p = amark_malloc(1);
    NAOS_REQUIRE_NOT_NULL(p);
    NAOS_CHECK_UINT_EQ(0u, (size_t)((uintptr_t)p % sizeof(void *)));
    NAOS_CHECK_UINT_EQ(0u, (size_t)((uintptr_t)p % sizeof(double)));
    amark_free(p);
}

NAOS_TEST(test_allocator_fail_after)
{
    test_allocator_stats_t stats;
    void *a;
    void *b;
    void *grown;

    test_allocator_install();
    test_allocator_fail_after(2);

    a = amark_malloc(8);
    NAOS_REQUIRE_NOT_NULL(a);
    b = amark_calloc(2, 8);
    NAOS_REQUIRE_NOT_NULL(b);

    /* 2 回成功した後の確保要求はすべて失敗し、統計は failures だけが変わる */
    NAOS_CHECK_NULL(amark_malloc(8));
    NAOS_CHECK_NULL(amark_calloc(1, 8));
    grown = amark_realloc(a, 64);
    NAOS_CHECK_NULL(grown);

    stats = test_allocator_stats();
    NAOS_CHECK_UINT_EQ(2u, stats.allocations);
    NAOS_CHECK_UINT_EQ(3u, stats.failures);
    NAOS_CHECK_UINT_EQ(2u, stats.live);
    NAOS_CHECK_UINT_EQ(24u, stats.bytes_live);
    NAOS_CHECK_UINT_EQ(24u, stats.bytes_total);

    /* 失敗した再確保の元の領域は有効なまま。解放は失敗注入の影響を受けない */
    amark_free(a);
    amark_free(b);
    stats = test_allocator_stats();
    NAOS_CHECK_UINT_EQ(0u, stats.live);
    NAOS_CHECK_UINT_EQ(0u, stats.bytes_live);

    test_allocator_never_fail();
    a = amark_malloc(8);
    NAOS_REQUIRE_NOT_NULL(a);
    amark_free(a);
    stats = test_allocator_stats();
    NAOS_CHECK_UINT_EQ(3u, stats.allocations);
    NAOS_CHECK_UINT_EQ(3u, stats.failures);
}

NAOS_TEST(test_allocator_fail_after_zero)
{
    test_allocator_install();
    test_allocator_fail_after(0);

    NAOS_CHECK_NULL(amark_malloc(1));
    NAOS_CHECK_UINT_EQ(1u, test_allocator_stats().failures);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().allocations);
}

NAOS_TEST(test_allocator_install_resets_and_uninstall_restores)
{
    amark_allocator_t current;
    void *p;

    test_allocator_install();
    amark_allocator_get(&current);
    NAOS_CHECK_FALSE(current.malloc_fn == test_default_allocator.malloc_fn);

    p = amark_malloc(4);
    NAOS_REQUIRE_NOT_NULL(p);
    amark_free(p);
    test_allocator_fail_after(0);

    /* 差し替え済みで呼んでも統計と失敗注入だけを初期化する */
    test_allocator_install();
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().allocations);
    p = amark_malloc(4);
    NAOS_REQUIRE_NOT_NULL(p);
    amark_free(p);
    NAOS_CHECK_UINT_EQ(1u, test_allocator_stats().allocations);

    test_allocator_uninstall();
    amark_allocator_get(&current);
    test_check_is_default(&current);
}

/* clang-format off */
NAOS_SUITE(memory,
    NAOS_SET_SETUP(reset_counters),
    NAOS_SET_TEARDOWN(restore_default),
    NAOS_ADD_TEST(default_allocator_functions_work),
    NAOS_ADD_TEST(default_allocator_allocates_and_frees),
    NAOS_ADD_TEST(get_with_null_does_nothing),
    NAOS_ADD_TEST(set_delegates_to_given_functions),
    NAOS_ADD_TEST(set_copies_the_allocator),
    NAOS_ADD_TEST(set_rejects_missing_function),
    NAOS_ADD_TEST(set_rejects_all_missing_from_default),
    NAOS_ADD_TEST(set_null_restores_default),
    NAOS_ADD_TEST(zero_size_requests_return_null_without_calling),
    NAOS_ADD_TEST(realloc_null_acts_as_malloc),
    NAOS_ADD_TEST(realloc_to_zero_frees),
    NAOS_ADD_TEST(calloc_overflow_returns_null_without_calling),
    NAOS_ADD_TEST(test_allocator_zero_size_and_overflow_change_nothing),
    NAOS_ADD_TEST(test_allocator_tracks_bytes),
    NAOS_ADD_TEST(test_allocator_returns_aligned_blocks),
    NAOS_ADD_TEST(test_allocator_fail_after),
    NAOS_ADD_TEST(test_allocator_fail_after_zero),
    NAOS_ADD_TEST(test_allocator_install_resets_and_uninstall_restores)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(memory)
)
/* clang-format on */
