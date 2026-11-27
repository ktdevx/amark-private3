/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * 計測付きアロケータ。各ブロックの先頭に要求サイズを記録するヘッダを付けて、差し替え前のアロケータで
 * 確保し、利用者へはヘッダの直後のアドレスを返す。再確保・解放ではヘッダから旧サイズを読んで統計を
 * 更新する。ヘッダの大きさはあらゆるオブジェクト型の整列を保つよう、整列の厳しい型の共用体にする
 * （C99 には max_align_t がないため）。
 */
#include "support/test_allocator.h"

#include <amark/memory.h>

#include <stdint.h>

typedef union test_allocator_header
{
    size_t size;
    long double align_long_double;
    long long align_long_long;
    double align_double;
    void *align_pointer;
    void (*align_function)(void);
} test_allocator_header_t;

#define TEST_ALLOCATOR_HEADER_SIZE sizeof(test_allocator_header_t)

static test_allocator_stats_t test_allocator_state;
static amark_allocator_t test_allocator_underlying;
static int test_allocator_failing;
static size_t test_allocator_remaining;

static void test_allocator_update_peak(void)
{
    if (test_allocator_state.bytes_live > test_allocator_state.bytes_peak)
    {
        test_allocator_state.bytes_peak = test_allocator_state.bytes_live;
    }
}

/* 失敗注入の判定。失敗させるなら failures を数えて 1 を返す */
static int test_allocator_should_fail(void)
{
    if (!test_allocator_failing)
    {
        return 0;
    }
    if (test_allocator_remaining == 0)
    {
        test_allocator_state.failures++;
        return 1;
    }
    return 0;
}

/* 成功した確保・再確保を数え、失敗注入の残り回数を減らす */
static void test_allocator_count_success(size_t size)
{
    test_allocator_state.allocations++;
    test_allocator_state.bytes_total += size;
    if (test_allocator_failing)
    {
        test_allocator_remaining--;
    }
}

static void *test_allocator_to_user(test_allocator_header_t *header)
{
    return (void *)(header + 1);
}

static test_allocator_header_t *test_allocator_to_header(void *ptr)
{
    return (test_allocator_header_t *)ptr - 1;
}

static void *test_allocator_malloc(size_t size)
{
    test_allocator_header_t *header;

    if (test_allocator_should_fail() || (size > (SIZE_MAX - TEST_ALLOCATOR_HEADER_SIZE)))
    {
        return NULL;
    }
    header = (test_allocator_header_t *)test_allocator_underlying.malloc_fn(TEST_ALLOCATOR_HEADER_SIZE + size);
    if (header == NULL)
    {
        return NULL;
    }
    header->size = size;

    test_allocator_count_success(size);
    test_allocator_state.live++;
    test_allocator_state.bytes_live += size;
    test_allocator_update_peak();
    return test_allocator_to_user(header);
}

static void *test_allocator_realloc(void *ptr, size_t size)
{
    test_allocator_header_t *header;
    size_t old_size;

    if (test_allocator_should_fail() || (size > (SIZE_MAX - TEST_ALLOCATOR_HEADER_SIZE)))
    {
        return NULL;
    }
    header = test_allocator_to_header(ptr);
    old_size = header->size;
    header = (test_allocator_header_t *)test_allocator_underlying.realloc_fn(header, TEST_ALLOCATOR_HEADER_SIZE + size);
    if (header == NULL)
    {
        return NULL;
    }
    header->size = size;

    test_allocator_count_success(size);
    test_allocator_state.bytes_live = test_allocator_state.bytes_live - old_size + size;
    test_allocator_update_peak();
    return test_allocator_to_user(header);
}

static void *test_allocator_calloc(size_t n, size_t size)
{
    test_allocator_header_t *header;
    size_t total;

    if (test_allocator_should_fail() || (size > ((SIZE_MAX - TEST_ALLOCATOR_HEADER_SIZE) / n)))
    {
        return NULL;
    }
    total = n * size;
    /* ヘッダを含めて 1 要素とみなし、ゼロ埋めは差し替え前のアロケータに任せる */
    header = (test_allocator_header_t *)test_allocator_underlying.calloc_fn(1, TEST_ALLOCATOR_HEADER_SIZE + total);
    if (header == NULL)
    {
        return NULL;
    }
    header->size = total;

    test_allocator_count_success(total);
    test_allocator_state.live++;
    test_allocator_state.bytes_live += total;
    test_allocator_update_peak();
    return test_allocator_to_user(header);
}

static void test_allocator_free(void *ptr)
{
    test_allocator_header_t *header = test_allocator_to_header(ptr);

    test_allocator_state.deallocations++;
    test_allocator_state.live--;
    test_allocator_state.bytes_live -= header->size;
    test_allocator_underlying.free_fn(header);
}

static void test_allocator_reset(void)
{
    test_allocator_stats_t zero = {0, 0, 0, 0, 0, 0, 0};

    test_allocator_state = zero;
    test_allocator_failing = 0;
    test_allocator_remaining = 0;
}

void test_allocator_install(void)
{
    amark_allocator_t current;
    amark_allocator_t measuring;

    test_allocator_reset();
    amark_allocator_get(&current);
    if (current.malloc_fn == test_allocator_malloc)
    {
        return;
    }

    test_allocator_underlying = current;
    measuring.malloc_fn = test_allocator_malloc;
    measuring.realloc_fn = test_allocator_realloc;
    measuring.calloc_fn = test_allocator_calloc;
    measuring.free_fn = test_allocator_free;
    (void)amark_allocator_set(&measuring);
}

void test_allocator_uninstall(void)
{
    (void)amark_allocator_set(NULL);
}

void test_allocator_fail_after(size_t successes)
{
    test_allocator_failing = 1;
    test_allocator_remaining = successes;
}

void test_allocator_never_fail(void)
{
    test_allocator_failing = 0;
    test_allocator_remaining = 0;
}

test_allocator_stats_t test_allocator_stats(void)
{
    return test_allocator_state;
}
