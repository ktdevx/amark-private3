/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include <amark/error.h>

const char *amark_error_string(amark_error_t err)
{
    switch (err)
    {
#define AMARK_ERROR_STRING_CASE_(name, string) \
    case name:                                 \
        return string;
        AMARK_ERROR_LIST(AMARK_ERROR_STRING_CASE_)
#undef AMARK_ERROR_STRING_CASE_
    default:
        break;
    }

    return "unknown error";
}
