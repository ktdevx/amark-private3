/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * インストール済み Amark の利用者確認。ヘッダのバージョン定数と実行時のバージョンが一致し、
 * バッファへの追記・取り出し・解放が成功すれば 0、そうでなければ 1 を返す。
 */
#include <amark/amark.h>

#include <stdio.h>
#include <string.h>

int main(void)
{
    amark_buffer_t buf;
    char *data = NULL;
    size_t len = 0;
    int ok = 1;

    if (amark_version_number() != AMARK_VERSION_NUMBER)
    {
        fprintf(stderr, "version mismatch: header %s, library %s\n", AMARK_VERSION_STRING, amark_version_string());
        return 1;
    }

    amark_buffer_init(&buf);
    if ((amark_buffer_append_cstr(&buf, "Amark") != AMARK_OK) || (amark_buffer_append_char(&buf, '!') != AMARK_OK))
    {
        ok = 0;
    }

    if (ok && (amark_buffer_detach(&buf, &data, &len) != AMARK_OK))
    {
        ok = 0;
    }

    if (ok && ((len != 6) || (memcmp(data, "Amark!", 7) != 0)))
    {
        ok = 0;
    }

    amark_free(data);
    amark_buffer_deinit(&buf);
    if (!ok)
    {
        fprintf(stderr, "buffer operations failed\n");
        return 1;
    }

    printf("amark %s: package consumer ok\n", amark_version_string());
    return 0;
}
