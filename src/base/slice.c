/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * 文字列スライス。空スライスの data は NULL でありうるため、長さ 0 のときは memcmp や memcpy に
 * data を渡さない（NULL を渡すことは長さ 0 でも未定義動作になる）。
 */
#include <amark/slice.h>

#include <stdint.h>
#include <string.h>

amark_slice_t amark_slice_make(const char *data, size_t len)
{
    amark_slice_t slice;

    if (data == NULL)
    {
        slice.data = NULL;
        slice.len = 0;
        return slice;
    }

    slice.data = data;
    slice.len = len;
    return slice;
}

amark_slice_t amark_slice_from_cstr(const char *cstr)
{
    if (cstr == NULL)
    {
        return amark_slice_make(NULL, 0);
    }

    return amark_slice_make(cstr, strlen(cstr));
}

bool amark_slice_equal(amark_slice_t a, amark_slice_t b)
{
    if (a.len != b.len)
    {
        return false;
    }

    if (a.len == 0)
    {
        return true;
    }

    return memcmp(a.data, b.data, a.len) == 0;
}

amark_slice_t amark_slice_sub(amark_slice_t slice, size_t offset, size_t len)
{
    amark_slice_t sub;

    if (offset > slice.len)
    {
        offset = slice.len;
    }
    if (len > (slice.len - offset))
    {
        len = slice.len - offset;
    }

    /* 空スライスの data が NULL のとき、NULL への加算（未定義動作）を避ける */
    if (slice.data == NULL)
    {
        return amark_slice_make(NULL, 0);
    }

    sub.data = slice.data + offset;
    sub.len = len;
    return sub;
}

char *amark_slice_dup(amark_slice_t slice)
{
    char *copy;

    /* len + 1 があふれる長さは確保を要求せずに失敗とする */
    if (slice.len == SIZE_MAX)
    {
        return NULL;
    }

    copy = (char *)amark_malloc(slice.len + 1);
    if (copy == NULL)
    {
        return NULL;
    }

    if (slice.len > 0)
    {
        memcpy(copy, slice.data, slice.len);
    }
    copy[slice.len] = '\0';
    return copy;
}
