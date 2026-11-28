/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * 正規化: 起点の部分木を前行順の反復で走査し、子を持つ各ノードの子の並びで、連続する text の並び（run）
 * ごとに空の text を取り外して破棄し、残りが 2 つ以上なら 1 回の確保で連結した内容を先頭の text に
 * 置き、残りを取り外して破棄する。起点自身は取り除かない。木の深さに比例するスタックを使わない。
 * エラーではその時点で止める（基本保証: 止めた並びは空の除去だけが適用され、連結内容は変わらない）。
 */
#include "tree/node_internal.h"

#include <amark/memory.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* node が text なら真 */
static bool amark_node_is_text(const amark_node_t *node)
{
    return (node != NULL) && (node->kind->type == AMARK_NODE_TEXT);
}

/*
 * start から始まる text の並びを正規化し、*end に並びの直後のノード（text 以外または NULL）を返す。
 * 失敗時も *end を設定し、並びは空の除去だけが適用された状態に保つ
 */
static amark_error_t amark_node_normalize_run(amark_node_t *start, amark_node_t **end)
{
    amark_node_t *first = NULL;
    amark_node_t *node = start;
    size_t count = 0;
    size_t total = 0;
    bool overflow = false;
    char *buffer;
    size_t offset;

    /* 手順 1: 空の text を取り外して破棄し、残りの数と合計長を数える（確保なし） */
    while (amark_node_is_text(node))
    {
        amark_node_t *next = node->next;

        if (node->as.literal.len == 0)
        {
            amark_node_destroy(node);
        }
        else
        {
            if (first == NULL)
            {
                first = node;
            }

            if (node->as.literal.len > (SIZE_MAX - 1 - total))
            {
                overflow = true;
            }
            else
            {
                total += node->as.literal.len;
            }

            count++;
        }

        node = next;
    }

    *end = node;

    if (count < 2)
    {
        return AMARK_OK;
    }

    /* 手順 2: 合計長 + 1 のあふれを確保前に検出し、1 回の確保で連結する */
    if (overflow)
    {
        return AMARK_ERROR_OVERFLOW;
    }

    buffer = (char *)amark_malloc(total + 1);
    if (buffer == NULL)
    {
        return AMARK_ERROR_NO_MEMORY;
    }

    offset = 0;
    for (node = first; node != *end; node = node->next)
    {
        memcpy(buffer + offset, node->as.literal.data, node->as.literal.len);
        offset += node->as.literal.len;
    }

    buffer[total] = '\0';

    amark_node_string_deinit(&first->as.literal);
    first->as.literal.data = buffer;
    first->as.literal.len = total;

    /* 先頭以外の text を取り外して破棄する */
    while (first->next != *end)
    {
        amark_node_destroy(first->next);
    }

    return AMARK_OK;
}

/* parent の子の並びの text の並びをすべて正規化する */
static amark_error_t amark_node_normalize_children(amark_node_t *parent)
{
    amark_node_t *child = parent->first_child;

    while (child != NULL)
    {
        if (amark_node_is_text(child))
        {
            amark_error_t error = amark_node_normalize_run(child, &child);

            if (error != AMARK_OK)
            {
                return error;
            }
        }
        else
        {
            child = child->next;
        }
    }

    return AMARK_OK;
}

amark_error_t amark_node_normalize(amark_node_t *root)
{
    amark_node_t *current;

    if (root == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    /*
     * 前行順の反復: 現在のノードの子の並びを正規化してから最初の子へ降りる。子がなければ、次の兄弟を
     * 持つ祖先（起点を含まない）まで親へ戻り、その兄弟へ進む。起点まで戻れば終わる。
     * 子の並びの変更は現在のノードの子だけに及び、まだ訪れていない部分の位置を変えない
     */
    current = root;
    for (;;)
    {
        if (current->first_child != NULL)
        {
            amark_error_t error = amark_node_normalize_children(current);

            if (error != AMARK_OK)
            {
                return error;
            }
        }

        if (current->first_child != NULL)
        {
            current = current->first_child;
            continue;
        }

        while ((current != root) && (current->next == NULL))
        {
            current = current->parent;
        }

        if (current == root)
        {
            return AMARK_OK;
        }

        current = current->next;
    }
}
