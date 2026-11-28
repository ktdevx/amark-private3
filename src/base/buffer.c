/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * 可変長バッファ。領域は cap + 1 バイトを確保し、内容の直後に常に NUL を置く。
 * 確保の要求サイズは常に 1 以上なので、amark_realloc / amark_malloc の NULL は確保失敗を意味する。
 */
#include <amark/buffer.h>

#include <stdint.h>
#include <string.h>

/* 領域を初めて確保するときの最小の容量（NUL を除く） */
#define AMARK_BUFFER_MIN_CAPACITY 16u

void amark_buffer_init(amark_buffer_t *buf)
{
    if (!buf)
    {
        return;
    }

    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

void amark_buffer_deinit(amark_buffer_t *buf)
{
    if (!buf)
    {
        return;
    }

    amark_free(buf->data);
    amark_buffer_init(buf);
}

amark_error_t amark_buffer_reserve(amark_buffer_t *buf, size_t additional)
{
    size_t required;
    size_t new_cap;
    char *new_data;

    if (!buf)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    /* len + additional + 1 があふれる要求は確保を試みずに失敗とする */
    if (additional > (SIZE_MAX - buf->len - 1))
    {
        return AMARK_ERROR_OVERFLOW;
    }

    required = buf->len + additional;
    if (required <= buf->cap)
    {
        return AMARK_OK;
    }

    /* 新容量は max(必要容量, cap * 2, 最小容量)。cap * 2 + 1 があふれるなら倍の候補は使わない */
    new_cap = required;
    if ((buf->cap <= ((SIZE_MAX - 1) / 2)) && ((buf->cap * 2) > new_cap))
    {
        new_cap = buf->cap * 2;
    }
    if (new_cap < AMARK_BUFFER_MIN_CAPACITY)
    {
        new_cap = AMARK_BUFFER_MIN_CAPACITY;
    }

    /* 要求サイズ new_cap + 1 は 1 以上なので、NULL は確保失敗を意味する。失敗時は元の領域が残る */
    new_data = (char *)amark_realloc(buf->data, new_cap + 1);
    if (!new_data)
    {
        return AMARK_ERROR_NO_MEMORY;
    }

    new_data[buf->len] = '\0';
    buf->data = new_data;
    buf->cap = new_cap;

    return AMARK_OK;
}

amark_error_t amark_buffer_append(amark_buffer_t *buf, const char *data, size_t len)
{
    amark_error_t err;

    if (!buf || ((len > 0) && !data))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (len == 0)
    {
        return AMARK_OK;
    }

    err = amark_buffer_reserve(buf, len); /* あふれ・確保失敗ではバッファを変えない */
    if (err)
    {
        return err;
    }

    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    buf->data[buf->len] = '\0';

    return AMARK_OK;
}

amark_error_t amark_buffer_append_char(amark_buffer_t *buf, char c)
{
    return amark_buffer_append(buf, &c, 1);
}

amark_error_t amark_buffer_append_cstr(amark_buffer_t *buf, const char *cstr)
{
    if (!buf || !cstr)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    return amark_buffer_append(buf, cstr, strlen(cstr));
}

amark_error_t amark_buffer_append_slice(amark_buffer_t *buf, amark_slice_t slice)
{
    return amark_buffer_append(buf, slice.data, slice.len);
}

amark_error_t amark_buffer_append_codepoint(amark_buffer_t *buf, uint32_t cp)
{
    char encoded[AMARK_UTF8_MAX_LENGTH];
    size_t written;
    amark_error_t err;

    if (!buf)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    err = amark_utf8_encode(cp, encoded, &written); /* 不正なコードポイントは AMARK_ERROR_INVALID_UTF8 */
    if (err)
    {
        return err;
    }

    return amark_buffer_append(buf, encoded, written);
}

void amark_buffer_clear(amark_buffer_t *buf)
{
    if (!buf)
    {
        return;
    }

    buf->len = 0;
    if (buf->data)
    {
        buf->data[0] = '\0';
    }
}

amark_error_t amark_buffer_truncate(amark_buffer_t *buf, size_t len)
{
    if (!buf || (len > buf->len))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    buf->len = len;
    if (buf->data)
    {
        buf->data[len] = '\0';
    }

    return AMARK_OK;
}

amark_error_t amark_buffer_detach(amark_buffer_t *buf, char **data, size_t *len)
{
    char *empty;

    if (!buf || !data || !len)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    /* 領域があればその所有権を移す */
    if (buf->data)
    {
        *data = buf->data;
        *len = buf->len;
        amark_buffer_init(buf);
        return AMARK_OK;
    }

    /* 空なら 1 バイトの "" を確保して渡す。要求サイズは 1 なので NULL は確保失敗を意味する */
    empty = (char *)amark_malloc(1);
    if (!empty)
    {
        return AMARK_ERROR_NO_MEMORY;
    }

    empty[0] = '\0';
    *data = empty;
    *len = 0;

    return AMARK_OK;
}

amark_slice_t amark_buffer_as_slice(const amark_buffer_t *buf)
{
    if (!buf)
    {
        return amark_slice_make(NULL, 0);
    }

    return amark_slice_make(buf->data, buf->len);
}
