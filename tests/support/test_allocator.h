/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * テスト用差し替えアロケータ。公開 API（amark_allocator_set）でグローバルアロケータを計測付きの
 * アロケータへ差し替え、確保・解放の回数とバイト数を数え、指定回数の成功後に確保を失敗させる。
 * 状態は test_allocator.c の静的変数に持つ（グローバルアロケータは利用者データを持たないため）。
 * スレッドセーフでない。
 *
 * サイズ 0 の要求はラッパが NULL を返すため計測アロケータに届かず、統計を変えない。
 * amark_realloc(p, 0) はラッパが free_fn を呼ぶため解放として数える。再確保の成功は bytes_live を
 * 旧サイズから新サイズへ置き換え、bytes_total に新サイズを加える。失敗した要求は failures 以外を変えない。
 */
#ifndef TEST_ALLOCATOR_H
#define TEST_ALLOCATOR_H

#include <amark/export.h>

#include <stddef.h>

AMARK_BEGIN_DECLS

typedef struct test_allocator_stats
{
    size_t allocations;   /* 成功した確保・再確保の回数 */
    size_t deallocations; /* 解放の回数 */
    size_t live;          /* 生存中のブロック数 */
    size_t failures;      /* 注入した失敗の回数 */
    size_t bytes_live;    /* 生存中のブロックの要求バイト数の合計 */
    size_t bytes_total;   /* 成功した確保・再確保の要求バイト数の累計 */
    size_t bytes_peak;    /* bytes_live の最大値 */
} test_allocator_stats_t;

/*
 * 計測付きアロケータへ差し替え、統計を 0 にし、失敗注入を解除する。計測付きアロケータは
 * 差し替え前のアロケータで実際の確保を行う。差し替え済みで呼ぶと統計と失敗注入だけを初期化する
 */
void test_allocator_install(void);

/* 既定アロケータへ戻す。統計は次の test_allocator_install まで読める */
void test_allocator_uninstall(void);

/* この呼び出しの後、successes 回成功した後の確保要求（確保・再確保・ゼロ初期化確保）をすべて失敗させる */
void test_allocator_fail_after(size_t successes);

/* 失敗注入を解除する */
void test_allocator_never_fail(void);

/* 現在の統計を返す */
test_allocator_stats_t test_allocator_stats(void);

AMARK_END_DECLS

#endif /* TEST_ALLOCATOR_H */
