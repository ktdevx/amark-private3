/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * 種別ごとの値のアクセサと拡張種別の付随データ: 見出しレベル、リスト種別・開始番号・tight、literal、
 * info、URL、タイトルの読み書き、所有文字列の検査付きの置き換え（強い保証）と借用スライスでの取得、
 * 付随データの設定（置き換え時の後片付け）と取得。値を持たない種別へのアクセスは、setter では
 * AMARK_ERROR_INVALID_ARGUMENT、getter では「ない」を表す値になる。
 */
#include "tree/node_internal.h"

#include <amark/memory.h>
#include <amark/utf8.h>

#include <stddef.h>
#include <stdint.h>

/* 値を持たないことを表すスライス */
static amark_slice_t amark_node_absent_slice(void)
{
    return amark_slice_make(NULL, 0);
}

amark_error_t amark_node_string_set(amark_node_string_t *string, amark_slice_t value)
{
    char *copy;

    if ((value.len > 0) && (value.data == NULL))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (value.len == SIZE_MAX)
    {
        return AMARK_ERROR_OVERFLOW;
    }

    if (amark_utf8_validate(value, NULL) != AMARK_OK)
    {
        return AMARK_ERROR_INVALID_UTF8;
    }

    /* 空の値は確保せず未設定に戻す */
    if (value.len == 0)
    {
        amark_node_string_deinit(string);
        return AMARK_OK;
    }

    copy = amark_slice_dup(value);
    if (copy == NULL)
    {
        return AMARK_ERROR_NO_MEMORY;
    }

    amark_free(string->data);
    string->data = copy;
    string->len = value.len;

    return AMARK_OK;
}

amark_slice_t amark_node_string_get(const amark_node_string_t *string)
{
    if (string->data == NULL)
    {
        return amark_slice_make("", 0);
    }

    return amark_slice_make(string->data, string->len);
}

/* literal を持つ種別なら、その所有文字列を返す。持たなければ NULL */
static amark_node_string_t *amark_node_literal_field(const amark_node_t *node)
{
    if (node == NULL)
    {
        return NULL;
    }

    switch (node->kind->type)
    {
    case AMARK_NODE_CODE_BLOCK:
        return (amark_node_string_t *)&node->as.code_block.literal;

    case AMARK_NODE_HTML_BLOCK:
    case AMARK_NODE_TEXT:
    case AMARK_NODE_CODE:
    case AMARK_NODE_HTML_INLINE:
        return (amark_node_string_t *)&node->as.literal;

    default:
        return NULL;
    }
}

/* info を持つ種別（CODE_BLOCK）なら、その所有文字列を返す。持たなければ NULL */
static amark_node_string_t *amark_node_info_field(const amark_node_t *node)
{
    if ((node == NULL) || (node->kind->type != AMARK_NODE_CODE_BLOCK))
    {
        return NULL;
    }

    return (amark_node_string_t *)&node->as.code_block.info;
}

/* ノードが url と title を持つ種別（LINK、IMAGE）か */
static bool amark_node_has_link_values(const amark_node_t *node)
{
    return (node != NULL) && ((node->kind->type == AMARK_NODE_LINK) || (node->kind->type == AMARK_NODE_IMAGE));
}

/* url を持つ種別なら、その所有文字列を返す。持たなければ NULL */
static amark_node_string_t *amark_node_url_field(const amark_node_t *node)
{
    if (!amark_node_has_link_values(node))
    {
        return NULL;
    }

    return (amark_node_string_t *)&node->as.link.url;
}

/* title を持つ種別なら、その所有文字列を返す。持たなければ NULL */
static amark_node_string_t *amark_node_title_field(const amark_node_t *node)
{
    if (!amark_node_has_link_values(node))
    {
        return NULL;
    }

    return (amark_node_string_t *)&node->as.link.title;
}

/* 文字列の値の取得の共通部分。値を持たない種別は {NULL, 0} */
static amark_slice_t amark_node_get_string_field(const amark_node_string_t *field)
{
    if (field == NULL)
    {
        return amark_node_absent_slice();
    }

    return amark_node_string_get(field);
}

/* 文字列の値の設定の共通部分。値を持たない種別は AMARK_ERROR_INVALID_ARGUMENT */
static amark_error_t amark_node_set_string_field(amark_node_string_t *field, amark_slice_t value)
{
    if (field == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    return amark_node_string_set(field, value);
}

/* ノードが種別 type の標準種別か（NULL は偽） */
static bool amark_node_is(const amark_node_t *node, amark_node_type_t type)
{
    return (node != NULL) && (node->kind->type == type);
}

int amark_node_get_heading_level(const amark_node_t *node)
{
    if (!amark_node_is(node, AMARK_NODE_HEADING))
    {
        return 0;
    }

    return node->as.heading.level;
}

amark_error_t amark_node_set_heading_level(amark_node_t *node, int level)
{
    if (!amark_node_is(node, AMARK_NODE_HEADING))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if ((level < AMARK_NODE_HEADING_LEVEL_MIN) || (level > AMARK_NODE_HEADING_LEVEL_MAX))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    node->as.heading.level = level;

    return AMARK_OK;
}

amark_node_list_type_t amark_node_get_list_type(const amark_node_t *node)
{
    if (!amark_node_is(node, AMARK_NODE_LIST))
    {
        return AMARK_NODE_LIST_NONE;
    }

    return node->as.list.type;
}

amark_error_t amark_node_set_list_type(amark_node_t *node, amark_node_list_type_t type)
{
    if (!amark_node_is(node, AMARK_NODE_LIST))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if ((type != AMARK_NODE_LIST_BULLET) && (type != AMARK_NODE_LIST_ORDERED))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    node->as.list.type = type;

    return AMARK_OK;
}

uint32_t amark_node_get_list_start(const amark_node_t *node)
{
    if (!amark_node_is(node, AMARK_NODE_LIST))
    {
        return 0;
    }

    return node->as.list.start;
}

amark_error_t amark_node_set_list_start(amark_node_t *node, uint32_t start)
{
    if (!amark_node_is(node, AMARK_NODE_LIST))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (start > AMARK_NODE_LIST_START_MAX)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    node->as.list.start = start;

    return AMARK_OK;
}

bool amark_node_get_list_tight(const amark_node_t *node)
{
    if (!amark_node_is(node, AMARK_NODE_LIST))
    {
        return false;
    }

    return node->as.list.tight;
}

amark_error_t amark_node_set_list_tight(amark_node_t *node, bool tight)
{
    if (!amark_node_is(node, AMARK_NODE_LIST))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    node->as.list.tight = tight;

    return AMARK_OK;
}

amark_slice_t amark_node_get_literal(const amark_node_t *node)
{
    return amark_node_get_string_field(amark_node_literal_field(node));
}

amark_error_t amark_node_set_literal(amark_node_t *node, amark_slice_t literal)
{
    return amark_node_set_string_field(amark_node_literal_field(node), literal);
}

amark_slice_t amark_node_get_info(const amark_node_t *node)
{
    return amark_node_get_string_field(amark_node_info_field(node));
}

amark_error_t amark_node_set_info(amark_node_t *node, amark_slice_t info)
{
    return amark_node_set_string_field(amark_node_info_field(node), info);
}

amark_slice_t amark_node_get_url(const amark_node_t *node)
{
    return amark_node_get_string_field(amark_node_url_field(node));
}

amark_error_t amark_node_set_url(amark_node_t *node, amark_slice_t url)
{
    return amark_node_set_string_field(amark_node_url_field(node), url);
}

amark_slice_t amark_node_get_title(const amark_node_t *node)
{
    return amark_node_get_string_field(amark_node_title_field(node));
}

amark_error_t amark_node_set_title(amark_node_t *node, amark_slice_t title)
{
    return amark_node_set_string_field(amark_node_title_field(node), title);
}

void *amark_node_get_data(const amark_node_t *node)
{
    if (!amark_node_is(node, AMARK_NODE_EXTENSION))
    {
        return NULL;
    }

    return node->as.data;
}

amark_error_t amark_node_set_data(amark_node_t *node, void *data)
{
    void *old;

    if (!amark_node_is(node, AMARK_NODE_EXTENSION))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    old = node->as.data;
    if (old == data)
    {
        return AMARK_OK;
    }

    /* 後片付けの前に新しい値を保持し、後片付け中もノードが解放済みのデータを指さないようにする */
    node->as.data = data;

    if ((old != NULL) && (node->kind->destroy_data != NULL))
    {
        node->kind->destroy_data(old, node->kind->user_data);
    }

    return AMARK_OK;
}
