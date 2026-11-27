/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * UTF-8 の 1 文字復号（前方・逆方向）、コードポイントの符号化、バイト列全体の検証。
 *
 * 妥当性の基準は Unicode 標準 Table 3-7 の妥当なバイト列とする。過長表現、サロゲート
 * （U+D800–U+DFFF）の符号化、U+10FFFF を超える値、途切れた並び、不正な先頭・継続バイトは
 * 不正として扱う。NUL バイト（U+0000）は妥当な 1 バイトの文字である。
 *
 * 不正な並びは、Unicode 標準の推奨する最大部分列（妥当な並びの接頭辞として成立する最長の
 * バイト列。成立しなければ 1 バイト）ごとに 1 つの不正単位として扱い、置換文字 U+FFFD を返す。
 * 復号は入力の長さを超えて読まない。
 */
#ifndef AMARK_UTF8_H
#define AMARK_UTF8_H

#include <amark/error.h>
#include <amark/export.h>
#include <amark/slice.h>

#include <stddef.h>
#include <stdint.h>

/* 1 文字の UTF-8 表現の最大バイト数 */
#define AMARK_UTF8_MAX_LENGTH 4

/* 不正な並びの代わりに返す置換文字 U+FFFD */
#define AMARK_UTF8_REPLACEMENT_CHARACTER 0xFFFDu

AMARK_BEGIN_DECLS

/*
 * src の先頭から 1 文字を復号する。
 * - 妥当: AMARK_OK を返し、*cp にコードポイント、*consumed に 1〜4 のバイト数を書く
 * - 不正: AMARK_ERROR_INVALID_UTF8 を返し、*cp に AMARK_UTF8_REPLACEMENT_CHARACTER、*consumed に
 *   読み飛ばすべき最大部分列のバイト数（1〜3）を書く
 * - cp または consumed が NULL、src が空、または src.len > 0 で src.data が NULL:
 *   AMARK_ERROR_INVALID_ARGUMENT を返し、出力を変更しない
 */
AMARK_API amark_error_t amark_utf8_decode(amark_slice_t src, uint32_t *cp, size_t *consumed);

/*
 * src の位置 offset の直前で終わる 1 文字（不正単位）を逆方向に復号する。結果（戻り値、*cp、
 * *consumed）は、src の先頭から offset までを amark_utf8_decode で前方へ順に復号したときの最後の
 * 単位と一致する。その単位は src.data[offset - *consumed] から始まる。読み取るのは
 * offset の直前の最大 AMARK_UTF8_MAX_LENGTH バイトに限る。
 * cp または consumed が NULL、offset が 0、offset が src.len を超える、または src.data が NULL の
 * ときは AMARK_ERROR_INVALID_ARGUMENT を返し、出力を変更しない
 */
AMARK_API amark_error_t amark_utf8_decode_previous(amark_slice_t src, size_t offset, uint32_t *cp, size_t *consumed);

/*
 * コードポイント cp を UTF-8 で dst に書き、*written に 1〜4 のバイト数を書いて AMARK_OK を返す。
 * dst は AMARK_UTF8_MAX_LENGTH バイト以上の領域とする。NUL 終端は書かない。
 * cp がサロゲート（0xD800–0xDFFF）または 0x10FFFF を超える値なら AMARK_ERROR_INVALID_UTF8 を
 * 返し、dst と *written に何も書かない。dst または written が NULL なら
 * AMARK_ERROR_INVALID_ARGUMENT を返す
 */
AMARK_API amark_error_t amark_utf8_encode(uint32_t cp, char *dst, size_t *written);

/*
 * src 全体が妥当な UTF-8（空を含む）なら AMARK_OK を返す。不正なら AMARK_ERROR_INVALID_UTF8 を
 * 返し、error_offset が NULL でなければ最初の不正単位の先頭バイトの位置を書く。
 * src.len > 0 で src.data が NULL のときは AMARK_ERROR_INVALID_ARGUMENT を返す
 */
AMARK_API amark_error_t amark_utf8_validate(amark_slice_t src, size_t *error_offset);

AMARK_END_DECLS

#endif /* AMARK_UTF8_H */
