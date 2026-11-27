/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include <amark/version.h>

unsigned long amark_version_number(void)
{
    return AMARK_VERSION_NUMBER;
}

const char *amark_version_string(void)
{
    return AMARK_VERSION_STRING;
}
