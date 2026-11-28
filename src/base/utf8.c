/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * UTF-8 の復号・符号化・検証。妥当性の基準は Unicode 標準 Table 3-7 で、先頭バイトごとに
 * 2 バイト目の範囲を狭めることで過長表現、サロゲート、U+10FFFF 超を排除する。不正な並びは
 * 最大部分列（妥当な並びの接頭辞として成立する最長のバイト列）を 1 単位として読み飛ばす。
 * 逆方向の復号は前方の復号を再利用し、前方へ順に復号したときの単位と一致させる。
 */
#include <amark/utf8.h>

#include <stdbool.h>

/* 継続バイト（80–BF）なら真 */
static bool utf8_is_continuation(unsigned char byte)
{
    return (byte >= 0x80u) && (byte <= 0xBFu);
}

/*
 * 先頭バイト lead から、並びの長さ、2 バイト目の範囲 [*second_min, *second_max]、先頭バイトが
 * 持つコードポイントの上位ビットを求める。先頭バイトになれなければ 0 を返す
 */
static size_t utf8_sequence_info(unsigned char lead, unsigned char *second_min, unsigned char *second_max,
                                 uint32_t *initial)
{
    *second_min = 0x80u;
    *second_max = 0xBFu;

    if (lead <= 0x7Fu)
    {
        *initial = lead;
        return 1;
    }

    if ((lead >= 0xC2u) && (lead <= 0xDFu))
    {
        *initial = lead & 0x1Fu;
        return 2;
    }

    if ((lead >= 0xE0u) && (lead <= 0xEFu))
    {
        if (lead == 0xE0u)
        {
            *second_min = 0xA0u; /* 過長表現（U+0800 未満）を除く */
        }
        else if (lead == 0xEDu)
        {
            *second_max = 0x9Fu; /* サロゲート（U+D800–U+DFFF）を除く */
        }
        *initial = lead & 0x0Fu;
        return 3;
    }

    if ((lead >= 0xF0u) && (lead <= 0xF4u))
    {
        if (lead == 0xF0u)
        {
            *second_min = 0x90u; /* 過長表現（U+10000 未満）を除く */
        }
        else if (lead == 0xF4u)
        {
            *second_max = 0x8Fu; /* U+10FFFF を超える値を除く */
        }
        *initial = lead & 0x07u;
        return 4;
    }

    /* 80–C1、F5–FF は先頭バイトになれない */
    return 0;
}

amark_error_t amark_utf8_decode(amark_slice_t src, uint32_t *cp, size_t *consumed)
{
    const unsigned char *bytes;
    unsigned char second_min;
    unsigned char second_max;
    uint32_t value;
    size_t length;
    size_t i;

    if (!cp || !consumed || (src.len == 0) || !src.data)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    bytes = (const unsigned char *)src.data;
    length = utf8_sequence_info(bytes[0], &second_min, &second_max, &value);
    if (length == 0)
    {
        *cp = AMARK_UTF8_REPLACEMENT_CHARACTER;
        *consumed = 1;
        return AMARK_ERROR_INVALID_UTF8;
    }

    /* i バイト目が範囲外か入力の終わりなら、そこまでの i バイトが最大部分列になる */
    for (i = 1; i < length; i++)
    {
        unsigned char min = (i == 1) ? second_min : 0x80u;
        unsigned char max = (i == 1) ? second_max : 0xBFu;

        if ((i >= src.len) || (bytes[i] < min) || (bytes[i] > max))
        {
            *cp = AMARK_UTF8_REPLACEMENT_CHARACTER;
            *consumed = i;
            return AMARK_ERROR_INVALID_UTF8;
        }

        value = (value << 6) | (uint32_t)(bytes[i] & 0x3Fu);
    }

    *cp = value;
    *consumed = length;
    return AMARK_OK;
}

amark_error_t amark_utf8_decode_previous(amark_slice_t src, size_t offset, uint32_t *cp, size_t *consumed)
{
    const unsigned char *bytes;
    size_t start;
    size_t p;
    uint32_t unit_cp;
    size_t unit_consumed;
    amark_error_t err;

    if (!cp || !consumed || (offset == 0) || (offset > src.len) || !src.data)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    /* [max(0, offset - 4), offset) で継続バイトでない最後の位置を探す */
    bytes = (const unsigned char *)src.data;
    start = (offset > AMARK_UTF8_MAX_LENGTH) ? (offset - AMARK_UTF8_MAX_LENGTH) : 0;
    p = offset;
    while ((p > start) && utf8_is_continuation(bytes[p - 1]))
    {
        p--;
    }

    /*
     * 見つからなければ、直前の継続バイトはどの先頭バイトにも続かない 1 バイトの不正単位になる。
     * 継続バイトでないバイトは前方の復号で必ず新しい単位を始めるため、p から復号した単位が
     * offset でちょうど終わればそれが最後の単位で、余りがあれば最後の継続バイトが 1 バイトの
     * 不正単位になる
     */
    if (p == start)
    {
        *cp = AMARK_UTF8_REPLACEMENT_CHARACTER;
        *consumed = 1;
        return AMARK_ERROR_INVALID_UTF8;
    }
    p--;

    err = amark_utf8_decode(amark_slice_make(src.data + p, offset - p), &unit_cp, &unit_consumed);
    if (unit_consumed != (offset - p))
    {
        *cp = AMARK_UTF8_REPLACEMENT_CHARACTER;
        *consumed = 1;
        return AMARK_ERROR_INVALID_UTF8;
    }

    *cp = unit_cp;
    *consumed = unit_consumed;
    return err;
}

amark_error_t amark_utf8_encode(uint32_t cp, char *dst, size_t *written)
{
    unsigned char *out = (unsigned char *)dst;

    if (!dst || !written)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (((cp >= 0xD800u) && (cp <= 0xDFFFu)) || (cp > 0x10FFFFu))
    {
        return AMARK_ERROR_INVALID_UTF8;
    }

    if (cp <= 0x7Fu)
    {
        out[0] = (unsigned char)cp;
        *written = 1;
    }
    else if (cp <= 0x7FFu)
    {
        out[0] = (unsigned char)(0xC0u | (cp >> 6));
        out[1] = (unsigned char)(0x80u | (cp & 0x3Fu));
        *written = 2;
    }
    else if (cp <= 0xFFFFu)
    {
        out[0] = (unsigned char)(0xE0u | (cp >> 12));
        out[1] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (unsigned char)(0x80u | (cp & 0x3Fu));
        *written = 3;
    }
    else
    {
        out[0] = (unsigned char)(0xF0u | (cp >> 18));
        out[1] = (unsigned char)(0x80u | ((cp >> 12) & 0x3Fu));
        out[2] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[3] = (unsigned char)(0x80u | (cp & 0x3Fu));
        *written = 4;
    }

    return AMARK_OK;
}

amark_error_t amark_utf8_validate(amark_slice_t src, size_t *error_offset)
{
    size_t pos = 0;

    if ((src.len > 0) && !src.data)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    while (pos < src.len)
    {
        uint32_t cp;
        size_t consumed;

        if (amark_utf8_decode(amark_slice_make(src.data + pos, src.len - pos), &cp, &consumed) != AMARK_OK)
        {
            if (error_offset)
            {
                *error_offset = pos;
            }
            return AMARK_ERROR_INVALID_UTF8;
        }
        pos += consumed;
    }

    return AMARK_OK;
}
