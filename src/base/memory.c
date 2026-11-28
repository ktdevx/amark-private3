/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * グローバルアロケータ。現在のアロケータ（ライブラリ唯一の可変グローバル状態）を持ち、サイズ 0、
 * NULL、積のあふれをここで処理してから利用者の関数へ委ねる。C 標準ライブラリの確保関数を直接
 * 参照するのは、ここの既定アロケータだけである。
 */
#include <amark/memory.h>

#include <stdint.h>
#include <stdlib.h>

/*
 * 既定アロケータ: C 標準ライブラリの 4 関数へ中継する。標準ライブラリの関数そのものではなく
 * この翻訳単位の関数を置くのは、下の静的初期化子にアドレス定数として書けるようにするため
 * （MSVC の C では dllimport された関数（/MD の malloc など）のアドレスを静的初期化子に書けない）。
 */
static void *amark_default_malloc(size_t size)
{
    return malloc(size);
}

static void *amark_default_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

static void *amark_default_calloc(size_t n, size_t size)
{
    return calloc(n, size);
}

static void amark_default_free(void *ptr)
{
    free(ptr);
}

static const amark_allocator_t amark_default_allocator = {amark_default_malloc, amark_default_realloc,
                                                          amark_default_calloc, amark_default_free};

/* 現在のアロケータ。ライブラリ唯一の可変グローバル状態で、初期値は既定アロケータ */
static amark_allocator_t amark_current_allocator = {amark_default_malloc, amark_default_realloc, amark_default_calloc,
                                                    amark_default_free};

void *amark_malloc(size_t size)
{
    if (size == 0)
    {
        return NULL;
    }
    return amark_current_allocator.malloc_fn(size);
}

void *amark_realloc(void *ptr, size_t size)
{
    if (ptr == NULL)
    {
        return amark_malloc(size);
    }
    if (size == 0)
    {
        amark_current_allocator.free_fn(ptr);
        return NULL;
    }
    return amark_current_allocator.realloc_fn(ptr, size);
}

void *amark_calloc(size_t n, size_t size)
{
    if ((n == 0) || (size == 0) || (n > (SIZE_MAX / size)))
    {
        return NULL;
    }
    return amark_current_allocator.calloc_fn(n, size);
}

void amark_free(void *ptr)
{
    if (ptr == NULL)
    {
        return;
    }
    amark_current_allocator.free_fn(ptr);
}

amark_error_t amark_allocator_set(const amark_allocator_t *allocator)
{
    if (allocator == NULL)
    {
        amark_current_allocator = amark_default_allocator;
        return AMARK_OK;
    }
    if ((allocator->malloc_fn == NULL) || (allocator->realloc_fn == NULL) || (allocator->calloc_fn == NULL) ||
        (allocator->free_fn == NULL))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    amark_current_allocator = *allocator;
    return AMARK_OK;
}

void amark_allocator_get(amark_allocator_t *allocator)
{
    if (allocator == NULL)
    {
        return;
    }
    *allocator = amark_current_allocator;
}
