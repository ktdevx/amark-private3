/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * 拡張ノード種別のレジストリ: 生成・破棄、記述子と名前の検証、重複の拒否、名前の複製を伴う登録、
 * 名前による検索。種別は 1 つずつ個別に確保し、レジストリはそのポインタの配列を持つため、配列を
 * 伸長してもハンドルは移動しない。登録に失敗したときは途中で確保したものをすべて解放し、
 * レジストリを呼び出し前のまま保つ。グローバルな登録状態は持たない。
 */
#include "tree/node_internal.h"

#include <amark/memory.h>

#include <stddef.h>
#include <stdint.h>

/* 最初の登録で確保する配列の容量 */
#define AMARK_NODE_REGISTRY_INITIAL_CAPACITY 4

struct amark_node_registry
{
    amark_node_kind_t **items; /* 登録 0 件の間は NULL */
    size_t len;
    size_t cap;
};

/* 名前が 1〜64 バイトで、[a-z][a-z0-9_]* の形式なら true */
static bool amark_node_registry_name_is_valid(amark_slice_t name)
{
    size_t i;

    if ((name.len == 0) || (name.len > AMARK_NODE_KIND_NAME_MAX_LENGTH) || (name.data == NULL))
    {
        return false;
    }

    if ((name.data[0] < 'a') || (name.data[0] > 'z'))
    {
        return false;
    }

    for (i = 1; i < name.len; i++)
    {
        char c = name.data[i];

        if (!(((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9')) || (c == '_')))
        {
            return false;
        }
    }

    return true;
}

/* 記述子の分類・内容モデル・判定関数の組み合わせが矛盾していなければ true */
static bool amark_node_registry_desc_is_consistent(const amark_node_kind_desc_t *desc)
{
    if ((desc->category != AMARK_NODE_CATEGORY_BLOCK) && (desc->category != AMARK_NODE_CATEGORY_INLINE))
    {
        return false;
    }

    switch (desc->content)
    {
    case AMARK_NODE_CONTENT_NONE:
    case AMARK_NODE_CONTENT_INLINES:
        break;
    case AMARK_NODE_CONTENT_BLOCKS:
        if (desc->category == AMARK_NODE_CATEGORY_INLINE)
        {
            return false;
        }

        break;
    case AMARK_NODE_CONTENT_CUSTOM:
        return desc->can_contain != NULL;
    default:
        return false;
    }

    return desc->can_contain == NULL;
}

/* 配列に 1 件を加える空きを用意する。空きがなければ容量を倍に伸長する（初回は確保）。失敗時は配列を変えない */
static amark_error_t amark_node_registry_reserve(amark_node_registry_t *registry)
{
    amark_node_kind_t **items;
    size_t cap;

    if (registry->len < registry->cap)
    {
        return AMARK_OK;
    }

    if (registry->cap == 0)
    {
        cap = AMARK_NODE_REGISTRY_INITIAL_CAPACITY;
    }
    else
    {
        if (registry->cap > ((SIZE_MAX / sizeof(*items)) / 2))
        {
            return AMARK_ERROR_NO_MEMORY;
        }

        cap = registry->cap * 2;
    }

    items = (amark_node_kind_t **)amark_realloc(registry->items, cap * sizeof(*items));
    if (items == NULL)
    {
        return AMARK_ERROR_NO_MEMORY;
    }

    registry->items = items;
    registry->cap = cap;

    return AMARK_OK;
}

static void amark_node_registry_free_kind(amark_node_kind_t *kind)
{
    amark_free((void *)kind->name);
    amark_free(kind);
}

amark_error_t amark_node_registry_create(amark_node_registry_t **registry)
{
    amark_node_registry_t *created;

    if (registry == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    created = (amark_node_registry_t *)amark_calloc(1, sizeof(*created));
    if (created == NULL)
    {
        return AMARK_ERROR_NO_MEMORY;
    }

    created->items = NULL;
    created->len = 0;
    created->cap = 0;
    *registry = created;

    return AMARK_OK;
}

void amark_node_registry_destroy(amark_node_registry_t *registry)
{
    size_t i;

    if (registry == NULL)
    {
        return;
    }

    for (i = 0; i < registry->len; i++)
    {
        amark_node_registry_free_kind(registry->items[i]);
    }

    amark_free(registry->items);
    amark_free(registry);
}

amark_error_t amark_node_registry_add(amark_node_registry_t *registry, const amark_node_kind_desc_t *desc,
                                      const amark_node_kind_t **kind)
{
    amark_node_kind_t *created;
    char *name;

    if ((registry == NULL) || (desc == NULL) || (kind == NULL))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (!amark_node_registry_name_is_valid(desc->name) || !amark_node_registry_desc_is_consistent(desc))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (amark_node_registry_find(registry, desc->name) != NULL)
    {
        return AMARK_ERROR_ALREADY_EXISTS;
    }

    /* 種別と名前を確保してから配列を伸長する。どの失敗でも、それまでの確保を解放して状態を戻す */
    created = (amark_node_kind_t *)amark_calloc(1, sizeof(*created));
    if (created == NULL)
    {
        return AMARK_ERROR_NO_MEMORY;
    }

    name = amark_slice_dup(desc->name);
    if (name == NULL)
    {
        amark_free(created);
        return AMARK_ERROR_NO_MEMORY;
    }

    if (amark_node_registry_reserve(registry) != AMARK_OK)
    {
        amark_free(name);
        amark_free(created);
        return AMARK_ERROR_NO_MEMORY;
    }

    created->type = AMARK_NODE_EXTENSION;
    created->name = name;
    created->name_len = desc->name.len;
    created->category = desc->category;
    created->content = desc->content;
    created->requires_explicit_parent = desc->requires_explicit_parent;
    created->root_only = false;
    created->can_contain = desc->can_contain;
    created->destroy_data = desc->destroy_data;
    created->user_data = desc->user_data;

    registry->items[registry->len] = created;
    registry->len++;
    *kind = created;

    return AMARK_OK;
}

const amark_node_kind_t *amark_node_registry_find(const amark_node_registry_t *registry, amark_slice_t name)
{
    int type;
    size_t i;

    /* data のない空でないスライスはどの名前とも一致しない（memcmp に NULL を渡さない） */
    if ((registry == NULL) || ((name.len > 0) && (name.data == NULL)))
    {
        return NULL;
    }

    for (type = AMARK_NODE_DOCUMENT; type <= AMARK_NODE_IMAGE; type++)
    {
        const amark_node_kind_t *standard = amark_node_kind_get_standard((amark_node_type_t)type);

        if (amark_slice_equal(amark_node_kind_get_name(standard), name))
        {
            return standard;
        }
    }

    for (i = 0; i < registry->len; i++)
    {
        const amark_node_kind_t *registered = registry->items[i];

        if (amark_slice_equal(amark_slice_make(registered->name, registered->name_len), name))
        {
            return registered;
        }
    }

    return NULL;
}
