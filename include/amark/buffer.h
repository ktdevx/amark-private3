/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * 追記で伸長する NUL 終端付きの可変長バイト列バッファ。
 *
 * 構造体は呼び出し側の領域に置けるよう中身を公開する（amark_buffer_init / amark_buffer_deinit）。
 * そのためフィールドの名前・型・並びは API かつ ABI の一部である。利用者は data と len を読んでよいが、
 * フィールドを直接書き換えてはならず、変更は必ず関数を通す。cap は読めるが値は実装の伸長方針に
 * 依存し、契約ではない。1.0 までは互換性を約束しない。
 *
 * 状態は次の 2 つ:
 * - 空: data == NULL、len == 0、cap == 0（初期化直後と後片付け・取り出しの後）
 * - 領域あり: data != NULL、len <= cap、data[len] == '\0'（内容を NUL 終端文字列としても参照できる）
 *
 * 領域はグローバルアロケータ（amark_malloc 系）で確保する。容量は倍々に伸長し、追記は追記した
 * バイト数の合計に対して償却定数時間で行える。
 *
 * amark_error_t を返す関数は、必須のポインタ引数が NULL なら AMARK_ERROR_INVALID_ARGUMENT を返す。
 * 失敗したときは、バッファの内容・長さ・容量と出力引数を呼び出し前のまま保つ（強い保証）。
 * サイズ計算のあふれは確保を試みる前に AMARK_ERROR_OVERFLOW として検出する。
 * 1 つのバッファを複数のスレッドから同時に変更してはならない。
 */
#ifndef AMARK_BUFFER_H
#define AMARK_BUFFER_H

#include <amark/error.h>
#include <amark/export.h>
#include <amark/memory.h>
#include <amark/slice.h>
#include <amark/utf8.h>

#include <stddef.h>
#include <stdint.h>

/* 追記で伸長する NUL 終端付きバイト列バッファ */
typedef struct amark_buffer
{
    char *data; /* NULL または cap + 1 バイトの領域 */
    size_t len; /* 内容のバイト数 */
    size_t cap; /* 再確保なしに保持できる内容の最大バイト数（NUL を除く） */
} amark_buffer_t;

AMARK_BEGIN_DECLS

/* buf を確保なしに空のバッファにする。失敗しない。buf が NULL なら何もしない */
AMARK_API void amark_buffer_init(amark_buffer_t *buf);

/* 保持している領域を amark_free で解放し、buf を空に戻す。後で再利用してよい。buf が NULL なら何もしない */
AMARK_API void amark_buffer_deinit(amark_buffer_t *buf);

/*
 * さらに additional バイトを再確保なしに追記できるよう容量を確保する。
 * len + additional + 1 が size_t であふれるなら AMARK_ERROR_OVERFLOW、確保に失敗すれば
 * AMARK_ERROR_NO_MEMORY を返し、バッファを変更しない。既に足りていれば何もしない
 */
AMARK_API amark_error_t amark_buffer_reserve(amark_buffer_t *buf, size_t additional);

/* data から len バイトを追記する。len が 0 なら何もしない。len > 0 で data が NULL なら AMARK_ERROR_INVALID_ARGUMENT */
AMARK_API amark_error_t amark_buffer_append(amark_buffer_t *buf, const char *data, size_t len);

/* 1 バイト c を追記する */
AMARK_API amark_error_t amark_buffer_append_char(amark_buffer_t *buf, char c);

/* NUL 終端文字列 cstr の NUL の手前までを追記する。cstr が NULL なら AMARK_ERROR_INVALID_ARGUMENT */
AMARK_API amark_error_t amark_buffer_append_cstr(amark_buffer_t *buf, const char *cstr);

/* slice の内容を追記する。スライスの不変条件（len > 0 なら data != NULL）に反すれば AMARK_ERROR_INVALID_ARGUMENT */
AMARK_API amark_error_t amark_buffer_append_slice(amark_buffer_t *buf, amark_slice_t slice);

/*
 * コードポイント cp を UTF-8 で符号化して追記する。cp がサロゲート（0xD800–0xDFFF）または
 * 0x10FFFF を超える値なら AMARK_ERROR_INVALID_UTF8 を返し、バッファを変更しない
 */
AMARK_API amark_error_t amark_buffer_append_codepoint(amark_buffer_t *buf, uint32_t cp);

/* 長さを 0 にする。確保済みの容量は保つ。buf が NULL なら何もしない */
AMARK_API void amark_buffer_clear(amark_buffer_t *buf);

/*
 * 長さを len に切り詰める。確保済みの容量は保つ。len が現在の長さを超えるなら
 * AMARK_ERROR_INVALID_ARGUMENT を返し、バッファを変更しない
 */
AMARK_API amark_error_t amark_buffer_truncate(amark_buffer_t *buf, size_t len);

/*
 * 内容の所有権を NUL 終端付きの文字列として呼び出し側へ移し、*data に先頭、*len に長さを書いて
 * バッファを空に戻す。バッファが空なら 1 バイトの "" を amark_malloc で確保して渡す（確保に失敗すれば
 * AMARK_ERROR_NO_MEMORY を返し、バッファと出力引数を変更しない）。受け取った側は amark_free で解放する
 */
AMARK_API amark_error_t amark_buffer_detach(amark_buffer_t *buf, char **data, size_t *len);

/* 現在の内容を借用するスライスを返す。バッファを変更すると無効になる。buf が NULL なら空スライスを返す */
AMARK_API amark_slice_t amark_buffer_as_slice(const amark_buffer_t *buf);

AMARK_END_DECLS

#endif /* AMARK_BUFFER_H */
