/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * バージョン情報。このヘッダは手書きで、バージョンの唯一の出所である。
 * 数値はメジャー・マイナー・パッチの 3 行に 1 回だけ書き、ビルド定義（CMakeLists.txt）と
 * リリースワークフローはここから読む。リリースでバージョンを上げるときは、この 3 行だけを書き換える。
 * 各行は "#define AMARK_VERSION_<PART> <数値>" の形とし、接尾辞・括弧・式を付けない 10 進の
 * 整数リテラルで書く。各番号は 0〜255。
 */
#ifndef AMARK_VERSION_H
#define AMARK_VERSION_H

#include <amark/export.h>

#define AMARK_VERSION_MAJOR 0
#define AMARK_VERSION_MINOR 1
#define AMARK_VERSION_PATCH 0

/* (major << 16) | (minor << 8) | patch */
#define AMARK_VERSION_ENCODE(major, minor, patch) \
    ((((unsigned long)(major)) << 16) | (((unsigned long)(minor)) << 8) | ((unsigned long)(patch)))
#define AMARK_VERSION_NUMBER AMARK_VERSION_ENCODE(AMARK_VERSION_MAJOR, AMARK_VERSION_MINOR, AMARK_VERSION_PATCH)

/* 内部用（末尾の _ は互換性の対象外を示す）: マクロを展開してから文字列にする */
#define AMARK_VERSION_STRINGIFY_(x) #x
#define AMARK_VERSION_XSTRINGIFY_(x) AMARK_VERSION_STRINGIFY_(x)
#define AMARK_VERSION_STRING                       \
    AMARK_VERSION_XSTRINGIFY_(AMARK_VERSION_MAJOR) \
    "." AMARK_VERSION_XSTRINGIFY_(AMARK_VERSION_MINOR) "." AMARK_VERSION_XSTRINGIFY_(AMARK_VERSION_PATCH)

AMARK_BEGIN_DECLS

/* ライブラリをビルドしたときの AMARK_VERSION_NUMBER を返す */
AMARK_API unsigned long amark_version_number(void);

/* ライブラリをビルドしたときの AMARK_VERSION_STRING を返す。文字列は静的領域にあり、解放不要 */
AMARK_API const char *amark_version_string(void);

AMARK_END_DECLS

#endif /* AMARK_VERSION_H */
