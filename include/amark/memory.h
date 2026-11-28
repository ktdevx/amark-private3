/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * 差し替え可能なグローバルアロケータ。ライブラリ内の動的確保と解放はすべて amark_malloc、
 * amark_realloc、amark_calloc、amark_free を経由する。差し替えない間は C 標準ライブラリの
 * malloc、realloc、calloc、free を使う。
 *
 * サイズ 0 の要求の扱い（全プラットフォーム共通）:
 * - amark_malloc(0) と、要素数か要素サイズが 0 の amark_calloc は、アロケータを呼ばずに NULL を返す
 * - amark_realloc(NULL, n) は amark_malloc(n) と同じ。amark_realloc(p, 0) は p を解放して NULL を返す
 * - amark_free(NULL) は何もしない
 * したがって確保系の戻り値 NULL が確保失敗を意味するのは、要求サイズ（amark_calloc では
 * 要素数と要素サイズの積）が 0 でないときだけである。サイズ 0 の要求は常に NULL を返す。
 *
 * 差し替えの制約:
 * - ある時点のアロケータで確保されたメモリは、同じアロケータで解放しなければならない。差し替えは、
 *   以前のアロケータで確保された Amark のメモリが残っていない時点で行う
 * - 差し替え（amark_allocator_set）はスレッドセーフでない。Amark を使う他のスレッドが動いていない
 *   時点で行う。ライブラリはどちらの制約の違反も検出しない
 * - 確保・解放の呼び出しは現在のアロケータを読むだけで、複数スレッドから同時に呼んでよい
 * - アロケータはプロセス全体で 1 つのため、関数へ渡す利用者データは持たない。状態が必要なら
 *   利用者側の静的変数などに持つ
 */
#ifndef AMARK_MEMORY_H
#define AMARK_MEMORY_H

#include <amark/export.h>
#include <amark/error.h>

#include <stddef.h>

/*
 * 利用者が指定するアロケータの関数。ラッパがサイズ 0 と NULL を処理するため、size と n は常に
 * 1 以上、realloc_fn と free_fn の ptr は常に非 NULL で届く。戻り値はあらゆるオブジェクト型に
 * 適切に整列していること。calloc_fn は確保した領域をゼロで埋めること。確保に失敗したら NULL を
 * 返し、realloc_fn は失敗時に ptr を有効なまま残すこと
 */
typedef void *(*amark_malloc_fn_t)(size_t size);
typedef void *(*amark_realloc_fn_t)(void *ptr, size_t size);
typedef void *(*amark_calloc_fn_t)(size_t n, size_t size);
typedef void (*amark_free_fn_t)(void *ptr);

/* アロケータ。4 つの関数はすべて非 NULL でなければならない */
typedef struct amark_allocator
{
    amark_malloc_fn_t malloc_fn;
    amark_realloc_fn_t realloc_fn;
    amark_calloc_fn_t calloc_fn;
    amark_free_fn_t free_fn;
} amark_allocator_t;

AMARK_BEGIN_DECLS

/* size バイトを確保する。size が 0 ならアロケータを呼ばずに NULL を返す */
AMARK_API void *amark_malloc(size_t size);

/*
 * ptr の領域を size バイトに再確保する。ptr が NULL なら amark_malloc(size) と同じ。size が 0 なら
 * ptr を解放して NULL を返す。失敗時は NULL を返し、ptr は有効なまま残る
 */
AMARK_API void *amark_realloc(void *ptr, size_t size);

/*
 * 要素サイズ size の要素 n 個分を確保してゼロで埋める。n か size が 0 のとき、または n * size が
 * size_t で表せないときは、アロケータを呼ばずに NULL を返す
 */
AMARK_API void *amark_calloc(size_t n, size_t size);

/* ptr を解放する。ptr が NULL なら何もしない */
AMARK_API void amark_free(void *ptr);

/*
 * 現在のアロケータを差し替える。allocator が NULL なら既定（C 標準ライブラリ）に戻す。
 * 4 つの関数のいずれかが NULL なら、現在のアロケータを変えずに AMARK_ERROR_INVALID_ARGUMENT を返す。
 * それ以外は内容を複製して現在のアロケータとし、以降の確保と解放を指定された関数へ委ねる。
 * スレッドセーフでない（先頭のコメントの差し替えの制約を参照）
 */
AMARK_API amark_error_t amark_allocator_set(const amark_allocator_t *allocator);

AMARK_END_DECLS

#endif /* AMARK_MEMORY_H */
