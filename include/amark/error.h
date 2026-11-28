/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * ライブラリ全体で単一のエラーコード列挙と、その文字列化。
 * 失敗しうる公開関数は amark_error_t を返し、結果は出力引数で返す。成功は AMARK_OK（値 0）で、
 * if (err) で失敗を判定できる。
 */
#ifndef AMARK_ERROR_H
#define AMARK_ERROR_H

#include <amark/export.h>

/*
 * 全エラーコードの一覧（唯一の正本）。項目は X(名前, 文字列)。
 * 追加は末尾だけ。並べ替え・削除・再利用は禁止。AMARK_OK は常に先頭（値 0）。
 * 項目を追加したら foundation の設計のエラーコード一覧表にも同じ順で行を追加する。
 */
#define AMARK_ERROR_LIST(X)                             \
    X(AMARK_OK, "success")                              \
    X(AMARK_ERROR_NO_MEMORY, "out of memory")           \
    X(AMARK_ERROR_INVALID_ARGUMENT, "invalid argument") \
    X(AMARK_ERROR_OVERFLOW, "size overflow")            \
    X(AMARK_ERROR_INVALID_UTF8, "invalid UTF-8")        \
    X(AMARK_ERROR_INVALID_CHILD, "invalid child node")  \
    X(AMARK_ERROR_ALREADY_EXISTS, "already exists")

/* 内部用（末尾の _ は互換性の対象外を示す）: 一覧の 1 項目を列挙子 1 つに展開する */
#define AMARK_ERROR_ENUM_ENTRY_(name, string) name,

typedef enum amark_error
{
    AMARK_ERROR_LIST(AMARK_ERROR_ENUM_ENTRY_)
} amark_error_t;

#undef AMARK_ERROR_ENUM_ENTRY_

AMARK_BEGIN_DECLS

/*
 * err を説明する英語の短い文字列を返す。一覧にない値には "unknown error" を返す。
 * 戻り値は常に非 NULL の静的文字列で、解放不要
 */
AMARK_API const char *amark_error_string(amark_error_t err);

AMARK_END_DECLS

#endif /* AMARK_ERROR_H */
