/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * 文字列スライスと、ライブラリ全体の文字列の所有権規約。
 *
 * 文字列の所有権規約:
 * - 借用する文字列は amark_slice_t で表す。参照先の寿命は貸し手が保証し、借用した側は解放しない
 * - 所有する文字列は、グローバルアロケータ（amark_malloc 系）で確保した char * と、size_t の
 *   バイト長の組で表す。常に data[len] == '\0' を満たす。所有者が amark_free で解放する
 * - 入力と内部表現の文字コードは UTF-8 のバイト列とする。妥当性の検査と不正なバイトの扱いは、
 *   入力を受け取るモジュールが amark/utf8.h を用いて定める
 *
 * スライスの不変条件: len が 0 より大きければ data は NULL でない。空スライスの data は NULL でも
 * よい。data は NUL 終端を仮定せず、範囲内に NUL を含みうる。
 */
#ifndef AMARK_SLICE_H
#define AMARK_SLICE_H

#include <amark/export.h>
#include <amark/memory.h>

#include <stdbool.h>
#include <stddef.h>

/* 所有権を持たない、先頭位置とバイト長の組で表す文字列の参照 */
typedef struct amark_slice
{
    const char *data;
    size_t len;
} amark_slice_t;

AMARK_BEGIN_DECLS

/* data から len バイトを参照するスライスを返す。data が NULL なら空スライスを返す */
AMARK_API amark_slice_t amark_slice_make(const char *data, size_t len);

/* NUL 終端文字列 cstr の NUL の手前までを参照するスライスを返す。cstr が NULL なら空スライスを返す */
AMARK_API amark_slice_t amark_slice_from_cstr(const char *cstr);

/* 長さが等しく、全バイト（NUL を含む）が一致するときだけ true を返す。空スライス同士は等しい */
AMARK_API bool amark_slice_equal(amark_slice_t a, amark_slice_t b);

/*
 * slice の offset バイト目から len バイトを参照する部分スライスを返す。offset が slice.len を
 * 超えれば slice.len に、len が残りのバイト数を超えれば残りのバイト数に切り詰める
 */
AMARK_API amark_slice_t amark_slice_sub(amark_slice_t slice, size_t offset, size_t len);

/*
 * slice の内容を複製した NUL 終端付きの所有文字列を返す。slice.len + 1 バイトを amark_malloc で
 * 確保し、空スライスでも 1 バイトの "" を返す。呼び出し側が amark_free で解放する。
 * slice.len が SIZE_MAX のとき、または確保に失敗したときは NULL を返し、何も確保したまま残さない
 */
AMARK_API char *amark_slice_dup(amark_slice_t slice);

AMARK_END_DECLS

#endif /* AMARK_SLICE_H */
