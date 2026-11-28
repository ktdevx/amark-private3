/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * 公開関数の export / import と C リンケージを切り替えるマクロ。
 * 静的・動的のどちらのビルドでも同じヘッダを使う。利用者は何も定義しなくてよい
 * （動的ライブラリの利用時は Amark::amark が AMARK_SHARED を伝播する。CMake 以外の
 * ビルドシステムから動的ライブラリを使う場合は利用者が AMARK_SHARED を定義する）。
 * AMARK_BUILDING はライブラリ自身のビルドでのみ定義される。
 */
#ifndef AMARK_EXPORT_H
#define AMARK_EXPORT_H

#if defined(AMARK_BUILDING) || defined(AMARK_SHARED)
#  if defined(_WIN32) || defined(__CYGWIN__)
#    if defined(AMARK_BUILDING) && defined(AMARK_SHARED)
#      define AMARK_API __declspec(dllexport)
#    elif defined(AMARK_SHARED)
#      define AMARK_API __declspec(dllimport)
#    else
#      define AMARK_API
#    endif
#  elif defined(__GNUC__) || defined(__clang__)
#    define AMARK_API __attribute__((visibility("default")))
#  else
#    define AMARK_API
#  endif
#else
#  define AMARK_API
#endif

/* clang-format off */
#ifdef __cplusplus
#  define AMARK_BEGIN_DECLS extern "C" {
#  define AMARK_END_DECLS }
#else
#  define AMARK_BEGIN_DECLS
#  define AMARK_END_DECLS
#endif
/* clang-format on */

#endif /* AMARK_EXPORT_H */
