/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * ノードの核: 標準種別・拡張種別からの生成（既定値の設定）、親からの取り外し、値の解放と
 * 部分木の破棄、種別の取得、親・子・兄弟の参照。破棄は後行順の反復で行い、子を親の子リストから
 * 外しながら降りるため、木の深さに比例するスタックや補助領域を使わない。
 * このファイルは node_kind.c 以外の node-tree のソースに依存しない。
 */
#include "tree/node_internal.h"

#include <amark/memory.h>

#include <stddef.h>

/* ノードの値を種別ごとの既定値にする（文字列は未設定 = 空、付随データは NULL） */
static void amark_node_init_values(amark_node_t *node)
{
    switch (node->kind->type)
    {
    case AMARK_NODE_HEADING:
        node->as.heading.level = 1;
        break;

    case AMARK_NODE_LIST:
        node->as.list.type = AMARK_NODE_LIST_BULLET;
        node->as.list.start = 1;
        node->as.list.tight = false;
        break;

    case AMARK_NODE_CODE_BLOCK:
        node->as.code_block.info.data = NULL;
        node->as.code_block.info.len = 0;
        node->as.code_block.literal.data = NULL;
        node->as.code_block.literal.len = 0;
        break;

    case AMARK_NODE_HTML_BLOCK:
    case AMARK_NODE_TEXT:
    case AMARK_NODE_CODE:
    case AMARK_NODE_HTML_INLINE:
        node->as.literal.data = NULL;
        node->as.literal.len = 0;
        break;

    case AMARK_NODE_LINK:
    case AMARK_NODE_IMAGE:
        node->as.link.url.data = NULL;
        node->as.link.url.len = 0;
        node->as.link.title.data = NULL;
        node->as.link.title.len = 0;
        break;

    case AMARK_NODE_EXTENSION:
        node->as.data = NULL;
        break;

    default:
        break;
    }
}

void amark_node_string_deinit(amark_node_string_t *string)
{
    amark_free(string->data);
    string->data = NULL;
    string->len = 0;
}

void amark_node_release_values(amark_node_t *node)
{
    switch (node->kind->type)
    {
    case AMARK_NODE_CODE_BLOCK:
        amark_node_string_deinit(&node->as.code_block.info);
        amark_node_string_deinit(&node->as.code_block.literal);
        break;

    case AMARK_NODE_HTML_BLOCK:
    case AMARK_NODE_TEXT:
    case AMARK_NODE_CODE:
    case AMARK_NODE_HTML_INLINE:
        amark_node_string_deinit(&node->as.literal);
        break;

    case AMARK_NODE_LINK:
    case AMARK_NODE_IMAGE:
        amark_node_string_deinit(&node->as.link.url);
        amark_node_string_deinit(&node->as.link.title);
        break;

    case AMARK_NODE_EXTENSION:
        if ((node->as.data != NULL) && (node->kind->destroy_data != NULL))
        {
            node->kind->destroy_data(node->as.data, node->kind->user_data);
        }

        node->as.data = NULL;
        break;

    default:
        break;
    }
}

amark_error_t amark_node_create(amark_node_type_t type, amark_node_t **node)
{
    const amark_node_kind_t *kind = amark_node_kind_get_standard(type);

    if (kind == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    return amark_node_create_with_kind(kind, node);
}

amark_error_t amark_node_create_with_kind(const amark_node_kind_t *kind, amark_node_t **node)
{
    amark_node_t *created;

    if ((kind == NULL) || (node == NULL))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    created = (amark_node_t *)amark_calloc(1, sizeof(*created));
    if (created == NULL)
    {
        return AMARK_ERROR_NO_MEMORY;
    }

    created->kind = kind;
    created->parent = NULL;
    created->first_child = NULL;
    created->last_child = NULL;
    created->prev = NULL;
    created->next = NULL;
    amark_node_init_values(created);

    *node = created;

    return AMARK_OK;
}

void amark_node_unlink(amark_node_t *node)
{
    amark_node_t *parent;

    if ((node == NULL) || (node->parent == NULL))
    {
        return;
    }

    parent = node->parent;
    if (node->prev != NULL)
    {
        node->prev->next = node->next;
    }
    else
    {
        parent->first_child = node->next;
    }

    if (node->next != NULL)
    {
        node->next->prev = node->prev;
    }
    else
    {
        parent->last_child = node->prev;
    }

    node->parent = NULL;
    node->prev = NULL;
    node->next = NULL;
}

void amark_node_destroy(amark_node_t *node)
{
    amark_node_t *current;

    if (node == NULL)
    {
        return;
    }

    amark_node_unlink(node);

    /*
     * 後行順の反復: 子があれば最初の子を子リストから外してそこへ降りる。外した子の parent は
     * 戻り先として残す。子がなければ値を解放して自分を解放し、親へ戻る。起点を解放したら終わる
     */
    current = node;
    for (;;)
    {
        amark_node_t *child = current->first_child;

        if (child != NULL)
        {
            current->first_child = child->next;
            if (child->next != NULL)
            {
                child->next->prev = NULL;
            }
            else
            {
                current->last_child = NULL;
            }

            child->next = NULL;
            current = child;
        }
        else
        {
            amark_node_t *parent = current->parent;
            bool is_start = (current == node);

            amark_node_release_values(current);
            amark_free(current);
            if (is_start)
            {
                break;
            }

            current = parent;
        }
    }
}

amark_node_type_t amark_node_get_type(const amark_node_t *node)
{
    if (node == NULL)
    {
        return AMARK_NODE_NONE;
    }

    return amark_node_kind_get_type(node->kind);
}

const amark_node_kind_t *amark_node_get_kind(const amark_node_t *node)
{
    if (node == NULL)
    {
        return NULL;
    }

    return node->kind;
}

amark_node_t *amark_node_get_parent(const amark_node_t *node)
{
    if (node == NULL)
    {
        return NULL;
    }

    return node->parent;
}

amark_node_t *amark_node_get_first_child(const amark_node_t *node)
{
    if (node == NULL)
    {
        return NULL;
    }

    return node->first_child;
}

amark_node_t *amark_node_get_last_child(const amark_node_t *node)
{
    if (node == NULL)
    {
        return NULL;
    }

    return node->last_child;
}

amark_node_t *amark_node_get_next(const amark_node_t *node)
{
    if (node == NULL)
    {
        return NULL;
    }

    return node->next;
}

amark_node_t *amark_node_get_previous(const amark_node_t *node)
{
    if (node == NULL)
    {
        return NULL;
    }

    return node->prev;
}
