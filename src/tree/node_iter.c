/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * enter / exit イテレータ。呼び出し側の領域に置く構造体に (起点, 現在ノード, 現在のイベント) だけを
 * 持ち、次の位置は amark_node_iter_next の呼び出しのたびに現在ノードの first_child・next・parent から
 * 計算する。確保を行わず、反復で動き、木の深さに比例するスタックを使わない。葉かどうかは公開の
 * amark_node_kind_is_leaf で決める。ノードのリンクは非公開の共有定義から直接読む。
 */
#include "tree/node_internal.h"

#include <amark/node_iter.h>

#include <stddef.h>

/* ノードが葉の種別なら true */
static bool amark_node_iter_is_leaf(const amark_node_t *node)
{
    return amark_node_kind_is_leaf(node->kind);
}

/* node が root またはその子孫なら true。親をたどって root に達するかで判定する */
static bool amark_node_iter_in_subtree(const amark_node_t *root, const amark_node_t *node)
{
    const amark_node_t *walk;

    for (walk = node; walk != NULL; walk = walk->parent)
    {
        if (walk == root)
        {
            return true;
        }
    }

    return false;
}

/* 現在位置を (node, event) にする */
static amark_node_iter_event_t amark_node_iter_move(amark_node_iter_t *iter, amark_node_t *node,
                                                    amark_node_iter_event_t event)
{
    iter->node = node;
    iter->event = event;

    return event;
}

/* 終了の状態にする */
static amark_node_iter_event_t amark_node_iter_finish(amark_node_iter_t *iter)
{
    return amark_node_iter_move(iter, NULL, AMARK_NODE_ITER_DONE);
}

void amark_node_iter_init(amark_node_iter_t *iter, amark_node_t *root)
{
    if (iter == NULL)
    {
        return;
    }

    iter->root = root;
    iter->node = NULL;
    iter->event = AMARK_NODE_ITER_NONE;
}

amark_node_iter_event_t amark_node_iter_next(amark_node_iter_t *iter)
{
    amark_node_t *node;

    if ((iter == NULL) || (iter->event == AMARK_NODE_ITER_DONE))
    {
        return AMARK_NODE_ITER_DONE;
    }

    if (iter->event == AMARK_NODE_ITER_NONE)
    {
        if (iter->root == NULL)
        {
            return amark_node_iter_finish(iter);
        }

        return amark_node_iter_move(iter, iter->root, AMARK_NODE_ITER_ENTER);
    }

    node = iter->node;

    /* 起点でない現在ノードが親を失っていれば（取り外された）終了する */
    if ((node != iter->root) && (node->parent == NULL))
    {
        return amark_node_iter_finish(iter);
    }

    /* 子を取れる種別の ENTER: 最初の子へ降りる。子がなければ同じノードの EXIT */
    if ((iter->event == AMARK_NODE_ITER_ENTER) && !amark_node_iter_is_leaf(node))
    {
        if (node->first_child != NULL)
        {
            return amark_node_iter_move(iter, node->first_child, AMARK_NODE_ITER_ENTER);
        }

        return amark_node_iter_move(iter, node, AMARK_NODE_ITER_EXIT);
    }

    /* EXIT または葉の ENTER: 起点なら終了、次の兄弟の ENTER、親の EXIT の順に試す */
    if (node == iter->root)
    {
        return amark_node_iter_finish(iter);
    }

    if (node->next != NULL)
    {
        return amark_node_iter_move(iter, node->next, AMARK_NODE_ITER_ENTER);
    }

    return amark_node_iter_move(iter, node->parent, AMARK_NODE_ITER_EXIT);
}

amark_node_t *amark_node_iter_get_node(const amark_node_iter_t *iter)
{
    if (iter == NULL)
    {
        return NULL;
    }

    return iter->node;
}

amark_node_iter_event_t amark_node_iter_get_event(const amark_node_iter_t *iter)
{
    if (iter == NULL)
    {
        return AMARK_NODE_ITER_DONE;
    }

    return iter->event;
}

amark_error_t amark_node_iter_reset(amark_node_iter_t *iter, amark_node_t *node, amark_node_iter_event_t event)
{
    if ((iter == NULL) || (node == NULL))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if ((event != AMARK_NODE_ITER_ENTER) && (event != AMARK_NODE_ITER_EXIT))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if ((iter->root == NULL) || !amark_node_iter_in_subtree(iter->root, node))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if ((event == AMARK_NODE_ITER_EXIT) && amark_node_iter_is_leaf(node))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    (void)amark_node_iter_move(iter, node, event);

    return AMARK_OK;
}
