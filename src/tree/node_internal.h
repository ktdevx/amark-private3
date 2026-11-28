/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * 非公開: node-tree の .c が共有する構造体定義（種別、ノード、所有文字列）と、モジュール間の内部関数。
 * node-tree の .c だけが include し、公開ヘッダからは include しない。内部関数には AMARK_API を付けず、
 * 動的ライブラリから公開しない（ライブラリは既定で非公開の可視性でビルドする）。
 */
#ifndef AMARK_TREE_NODE_INTERNAL_H
#define AMARK_TREE_NODE_INTERNAL_H

#include <amark/error.h>
#include <amark/node.h>
#include <amark/node_kind.h>
#include <amark/slice.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 木が所有する文字列 */
typedef struct amark_node_string
{
    char *data; /* NULL（未設定 = 空文字列）または amark_malloc で確保した len + 1 バイト */
    size_t len; /* data が NULL なら 0 */
} amark_node_string_t;

/* ノード種別。標準種別は node_kind.c の定数表、拡張種別はレジストリが所有する */
struct amark_node_kind
{
    amark_node_type_t type; /* 標準種別の値、または AMARK_NODE_EXTENSION */
    const char *name;       /* 標準: 文字列リテラル。拡張: 所有（NUL 終端） */
    size_t name_len;
    amark_node_category_t category;
    amark_node_content_t content;
    bool requires_explicit_parent;
    bool root_only;                          /* document だけが真。どの親の子にもなれない */
    amark_node_can_contain_fn_t can_contain; /* content == CUSTOM のときだけ非 NULL */
    amark_node_destroy_data_fn_t destroy_data;
    void *user_data;
};

/*
 * ノード。不変条件: first_child == NULL と last_child == NULL は同値。兄弟リストは双方向で、
 * first_child->prev == NULL、last_child->next == NULL、各子の parent は親を指す。根は parent、prev、
 * next がすべて NULL
 */
struct amark_node
{
    const amark_node_kind_t *kind;
    amark_node_t *parent;
    amark_node_t *first_child;
    amark_node_t *last_child;
    amark_node_t *prev;
    amark_node_t *next;
    union
    {
        struct
        {
            int level;
        } heading;
        struct
        {
            amark_node_list_type_t type;
            uint32_t start;
            bool tight;
        } list;
        struct
        {
            amark_node_string_t info;
            amark_node_string_t literal;
        } code_block;
        amark_node_string_t literal; /* html_block、text、code、html_inline */
        struct
        {
            amark_node_string_t url;
            amark_node_string_t title;
        } link;     /* link、image */
        void *data; /* 拡張種別 */
    } as;
};

/* node.c: 文字列を解放して未設定に戻す */
void amark_node_string_deinit(amark_node_string_t *string);

/* node.c: ノードの値（文字列）を解放し、拡張種別の付随データに destroy_data を呼ぶ */
void amark_node_release_values(amark_node_t *node);

/* node_value.c: value を検査・複製して string を置き換える。失敗時は旧値を残す */
amark_error_t amark_node_string_set(amark_node_string_t *string, amark_slice_t value);

/* node_value.c: 文字列を借用スライスで返す。未設定なら "" */
amark_slice_t amark_node_string_get(const amark_node_string_t *string);

#endif /* AMARK_TREE_NODE_INTERNAL_H */
