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
 * 差し替えられたアロケータ。malloc_fn が NULL（初期値）の間は既定アロケータを使う。
 * 既定アロケータの関数を静的初期化子で設定しないのは、MSVC の C では dllimport された関数
 * （/MD の malloc など）のアドレスが静的初期化子では import thunk のアドレスになり、
 * 式で得るアドレスと一致しなくなるため。
 */
static amark_allocator_t amark_custom_allocator;

/* 既定アロケータ: 型が一致するため C 標準ライブラリの 4 関数をそのまま設定する */
static void amark_default_allocator(amark_allocator_t *allocator)
{
    allocator->malloc_fn = malloc;
    allocator->realloc_fn = realloc;
    allocator->calloc_fn = calloc;
    allocator->free_fn = free;
}

/* 現在のアロケータ。読むだけなので複数スレッドから同時に呼んでよい */
static amark_allocator_t amark_current_allocator(void)
{
    amark_allocator_t allocator;

    if (amark_custom_allocator.malloc_fn != NULL)
    {
        return amark_custom_allocator;
    }
    amark_default_allocator(&allocator);
    return allocator;
}

void *amark_malloc(size_t size)
{
    if (size == 0)
    {
        return NULL;
    }
    return amark_current_allocator().malloc_fn(size);
}

void *amark_realloc(void *ptr, size_t size)
{
    if (ptr == NULL)
    {
        return amark_malloc(size);
    }
    if (size == 0)
    {
        amark_current_allocator().free_fn(ptr);
        return NULL;
    }
    return amark_current_allocator().realloc_fn(ptr, size);
}

void *amark_calloc(size_t n, size_t size)
{
    if ((n == 0) || (size == 0) || (n > (SIZE_MAX / size)))
    {
        return NULL;
    }
    return amark_current_allocator().calloc_fn(n, size);
}

void amark_free(void *ptr)
{
    if (ptr == NULL)
    {
        return;
    }
    amark_current_allocator().free_fn(ptr);
}

amark_error_t amark_allocator_set(const amark_allocator_t *allocator)
{
    if (allocator == NULL)
    {
        amark_allocator_t none = {NULL, NULL, NULL, NULL};

        amark_custom_allocator = none;
        return AMARK_OK;
    }
    if ((allocator->malloc_fn == NULL) || (allocator->realloc_fn == NULL) || (allocator->calloc_fn == NULL) ||
        (allocator->free_fn == NULL))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }
    amark_custom_allocator = *allocator;
    return AMARK_OK;
}

void amark_allocator_get(amark_allocator_t *allocator)
{
    if (allocator == NULL)
    {
        return;
    }
    *allocator = amark_current_allocator();
}
