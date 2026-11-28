/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * 編集操作: 末尾追加・先頭追加・前後への挿入・置換。検証は「引数、新しい親の有無、循環、受け入れ判定」の
 * 順にすべてリンクの変更前に行い、失敗したら何も変えない。確保を伴わない。既に親を持つノードは検証の後で
 * 取り外してから置く（移動）。循環の検査は動かすノードが子を持つ場合だけ新しい親から祖先をたどるため、
 * 計算量は新しい親の深さに比例する範囲に収まる。受け入れ判定は amark_node_kind_can_contain だけを使う。
 */
#include "tree/node_internal.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * 手順 3 と 4: node を parent の子にできるかを検証する。node が子を持ち、parent またはその祖先と
 * 一致すれば循環、parent の種別が node の種別を受け入れなければ規則違反で、どちらも
 * AMARK_ERROR_INVALID_CHILD。子を持たないノードは自分自身以外の祖先になれず、自己参照は手順 1 で
 * 除外済みのため祖先をたどらない
 */
static amark_error_t amark_node_check_child(const amark_node_t *parent, const amark_node_t *node)
{
    if (node->first_child != NULL)
    {
        const amark_node_t *ancestor;

        for (ancestor = parent; ancestor != NULL; ancestor = ancestor->parent)
        {
            if (ancestor == node)
            {
                return AMARK_ERROR_INVALID_CHILD;
            }
        }
    }

    if (!amark_node_kind_can_contain(parent->kind, node->kind))
    {
        return AMARK_ERROR_INVALID_CHILD;
    }

    return AMARK_OK;
}

/*
 * 根の node を parent の子として prev と next の間にリンクする。prev と next は parent の隣り合う子
 * （先頭なら prev == NULL、末尾なら next == NULL）
 */
static void amark_node_link(amark_node_t *parent, amark_node_t *prev, amark_node_t *next, amark_node_t *node)
{
    node->parent = parent;
    node->prev = prev;
    node->next = next;

    if (prev != NULL)
    {
        prev->next = node;
    }
    else
    {
        parent->first_child = node;
    }

    if (next != NULL)
    {
        next->prev = node;
    }
    else
    {
        parent->last_child = node;
    }
}

amark_error_t amark_node_append_child(amark_node_t *parent, amark_node_t *child)
{
    amark_error_t error;

    if ((parent == NULL) || (child == NULL) || (parent == child))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    error = amark_node_check_child(parent, child);
    if (error != AMARK_OK)
    {
        return error;
    }

    /* 取り外しの後で末尾を読む（child が parent の末尾の子だった場合に備える） */
    amark_node_unlink(child);
    amark_node_link(parent, parent->last_child, NULL, child);

    return AMARK_OK;
}

amark_error_t amark_node_prepend_child(amark_node_t *parent, amark_node_t *child)
{
    amark_error_t error;

    if ((parent == NULL) || (child == NULL) || (parent == child))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    error = amark_node_check_child(parent, child);
    if (error != AMARK_OK)
    {
        return error;
    }

    amark_node_unlink(child);
    amark_node_link(parent, NULL, parent->first_child, child);

    return AMARK_OK;
}

amark_error_t amark_node_insert_before(amark_node_t *sibling, amark_node_t *node)
{
    amark_error_t error;

    if ((sibling == NULL) || (node == NULL) || (sibling == node))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (sibling->parent == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    error = amark_node_check_child(sibling->parent, node);
    if (error != AMARK_OK)
    {
        return error;
    }

    /* 取り外しの後で sibling の前を読む（node が sibling の直前の兄弟だった場合に備える） */
    amark_node_unlink(node);
    amark_node_link(sibling->parent, sibling->prev, sibling, node);

    return AMARK_OK;
}

amark_error_t amark_node_insert_after(amark_node_t *sibling, amark_node_t *node)
{
    amark_error_t error;

    if ((sibling == NULL) || (node == NULL) || (sibling == node))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (sibling->parent == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    error = amark_node_check_child(sibling->parent, node);
    if (error != AMARK_OK)
    {
        return error;
    }

    amark_node_unlink(node);
    amark_node_link(sibling->parent, sibling, sibling->next, node);

    return AMARK_OK;
}

amark_error_t amark_node_replace(amark_node_t *target, amark_node_t *replacement)
{
    amark_node_t *parent;
    amark_node_t *prev;
    amark_node_t *next;
    amark_error_t error;

    if ((target == NULL) || (replacement == NULL) || (target == replacement))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (target->parent == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    error = amark_node_check_child(target->parent, replacement);
    if (error != AMARK_OK)
    {
        return error;
    }

    /*
     * replacement を先に取り外す。target の部分木内や target の兄弟にあっても、target 自身は動かないため、
     * 取り外しの後の target の親と前後が置く位置になる
     */
    amark_node_unlink(replacement);

    parent = target->parent;
    prev = target->prev;
    next = target->next;
    amark_node_unlink(target);
    amark_node_link(parent, prev, next, replacement);

    return AMARK_OK;
}
