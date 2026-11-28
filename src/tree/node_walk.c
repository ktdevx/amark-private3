/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * 種別ごとの振り分け走査（非公開）。処理の表は種別ハンドルをキーとする可変長の配列で、ポインタの
 * 同一性で線形に探索する。走査の駆動部は公開のイテレータで部分木を反復し、ENTER で子の省略が
 * 要求されたノード（葉でない）はイテレータを同じノードの EXIT へ設定し直したうえで、その EXIT を
 * 自ら訪問する（設定し直しは位置の続きから報告するため）。公開 API だけを使い、tree/node_internal.h を
 * include しない。確保は表の伸長だけで行う。
 */
#include "tree/node_walk.h"

#include <amark/error.h>
#include <amark/memory.h>
#include <amark/node_iter.h>
#include <amark/node_kind.h>

#include <stdint.h>

/* 表の初期容量 */
#define AMARK_NODE_WALK_TABLE_INITIAL_CAPACITY 4u

/* kind の項目の位置を返す。なければ table->len */
static size_t amark_node_walk_table_index(const amark_node_walk_table_t *table, const amark_node_kind_t *kind)
{
    size_t i;

    for (i = 0; i < table->len; i++)
    {
        if (table->items[i].kind == kind)
        {
            return i;
        }
    }

    return table->len;
}

/* 1 件を加える空きを用意する。空きがなければ容量を倍（初回は初期容量）に伸長する。失敗時は表を変えない */
static amark_error_t amark_node_walk_table_reserve(amark_node_walk_table_t *table)
{
    amark_node_walk_hook_t *items;
    size_t cap;

    if (table->len < table->cap)
    {
        return AMARK_OK;
    }

    if (table->cap == 0)
    {
        cap = AMARK_NODE_WALK_TABLE_INITIAL_CAPACITY;
    }
    else
    {
        if (table->cap > ((SIZE_MAX / sizeof(*items)) / 2))
        {
            return AMARK_ERROR_OVERFLOW;
        }

        cap = table->cap * 2;
    }

    items = (amark_node_walk_hook_t *)amark_realloc(table->items, cap * sizeof(*items));
    if (items == NULL)
    {
        return AMARK_ERROR_NO_MEMORY;
    }

    table->items = items;
    table->cap = cap;

    return AMARK_OK;
}

void amark_node_walk_table_init(amark_node_walk_table_t *table)
{
    if (table == NULL)
    {
        return;
    }

    table->items = NULL;
    table->len = 0;
    table->cap = 0;
}

void amark_node_walk_table_deinit(amark_node_walk_table_t *table)
{
    if (table == NULL)
    {
        return;
    }

    amark_free(table->items);
    amark_node_walk_table_init(table);
}

amark_error_t amark_node_walk_table_set(amark_node_walk_table_t *table, const amark_node_kind_t *kind,
                                        amark_node_walk_hook_fn_t fn, void *user_data)
{
    size_t index;
    amark_error_t error;

    if ((table == NULL) || (kind == NULL))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    index = amark_node_walk_table_index(table, kind);

    if (index < table->len)
    {
        if (fn != NULL)
        {
            table->items[index].fn = fn;
            table->items[index].user_data = user_data;
        }
        else
        {
            /* 末尾と入れ替えて取り除く。縮小の再確保はしない */
            table->items[index] = table->items[table->len - 1];
            table->len--;
        }

        return AMARK_OK;
    }

    if (fn == NULL)
    {
        return AMARK_OK;
    }

    error = amark_node_walk_table_reserve(table);
    if (error != AMARK_OK)
    {
        return error;
    }

    table->items[table->len].kind = kind;
    table->items[table->len].fn = fn;
    table->items[table->len].user_data = user_data;
    table->len++;

    return AMARK_OK;
}

const amark_node_walk_hook_t *amark_node_walk_table_find(const amark_node_walk_table_t *table,
                                                         const amark_node_kind_t *kind)
{
    size_t index;

    if ((table == NULL) || (kind == NULL) || (table->len == 0))
    {
        return NULL;
    }

    index = amark_node_walk_table_index(table, kind);
    if (index == table->len)
    {
        return NULL;
    }

    return &table->items[index];
}

amark_error_t amark_node_walk_run(amark_node_walk_t *walk, const amark_node_t *root,
                                  const amark_node_walk_table_t *table, amark_node_walk_visit_fn_t visit, void *ctx)
{
    amark_node_iter_event_t event;

    if ((walk == NULL) || (root == NULL) || (table == NULL) || (visit == NULL))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    /* const を外すのはここだけ。イテレータは木を変更しない */
    amark_node_iter_init(&walk->iter, (amark_node_t *)root);
    walk->node = NULL;
    walk->event = AMARK_NODE_ITER_NONE;
    walk->skip_children = false;

    while ((event = amark_node_iter_next(&walk->iter)) != AMARK_NODE_ITER_DONE)
    {
        amark_node_t *node = amark_node_iter_get_node(&walk->iter);
        const amark_node_kind_t *kind = amark_node_get_kind(node);
        const amark_node_walk_hook_t *hook = amark_node_walk_table_find(table, kind);
        amark_error_t error;

        walk->node = node;
        walk->event = event;
        walk->skip_children = false;

        error = visit(ctx, walk, hook);
        if (error != AMARK_OK)
        {
            return error;
        }

        if ((event != AMARK_NODE_ITER_ENTER) || !walk->skip_children || amark_node_kind_is_leaf(kind))
        {
            continue;
        }

        /*
         * 子を省略する。設定し直しの後の next は EXIT の続きを報告するため、同じノードの EXIT はここで
         * 訪問する（node は起点の部分木内の葉でないノードなので設定し直しは失敗しない）
         */
        (void)amark_node_iter_reset(&walk->iter, node, AMARK_NODE_ITER_EXIT);
        walk->event = AMARK_NODE_ITER_EXIT;
        walk->skip_children = false;

        error = visit(ctx, walk, hook);
        if (error != AMARK_OK)
        {
            return error;
        }
    }

    return AMARK_OK;
}

void amark_node_walk_skip_children(amark_node_walk_t *walk)
{
    if (walk == NULL)
    {
        return;
    }

    walk->skip_children = true;
}
