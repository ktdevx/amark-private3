/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * ノード種別: 18 の標準種別の定数表、種別の問い合わせ、葉の判定、親子の受け入れ判定。
 * 標準種別の表は static const で、可変のグローバル状態を持たない。受け入れ判定は標準種別と
 * 拡張種別に同じ手順で答え、編集操作も必ずこの判定を使う。
 */
#include "tree/node_internal.h"

#include <stddef.h>

/* 標準種別の数（AMARK_NODE_DOCUMENT〜AMARK_NODE_IMAGE） */
#define AMARK_NODE_STANDARD_KIND_COUNT 18

static bool amark_node_list_can_contain(const amark_node_kind_t *parent, const amark_node_kind_t *child,
                                        void *user_data);

/* 標準種別の表の 1 行。添字は識別値 - AMARK_NODE_DOCUMENT */
#define AMARK_NODE_STANDARD_KIND(type, name, category, content, explicit_parent, root_only, can_contain)              \
    {                                                                                                                 \
        (type), (name), sizeof(name) - 1, (category), (content), (explicit_parent), (root_only), (can_contain), NULL, \
            NULL                                                                                                      \
    }

static const struct amark_node_kind amark_node_standard_kinds[AMARK_NODE_STANDARD_KIND_COUNT] = {
    AMARK_NODE_STANDARD_KIND(AMARK_NODE_DOCUMENT, "document", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_BLOCKS,
                             false, true, NULL),
    AMARK_NODE_STANDARD_KIND(AMARK_NODE_BLOCK_QUOTE, "block_quote", AMARK_NODE_CATEGORY_BLOCK,
                             AMARK_NODE_CONTENT_BLOCKS, false, false, NULL),
    AMARK_NODE_STANDARD_KIND(AMARK_NODE_LIST, "list", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_CUSTOM, false,
                             false, amark_node_list_can_contain),
    AMARK_NODE_STANDARD_KIND(AMARK_NODE_ITEM, "item", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_BLOCKS, true, false,
                             NULL),
    AMARK_NODE_STANDARD_KIND(AMARK_NODE_CODE_BLOCK, "code_block", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_NONE,
                             false, false, NULL),
    AMARK_NODE_STANDARD_KIND(AMARK_NODE_HTML_BLOCK, "html_block", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_NONE,
                             false, false, NULL),
    AMARK_NODE_STANDARD_KIND(AMARK_NODE_PARAGRAPH, "paragraph", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_INLINES,
                             false, false, NULL),
    AMARK_NODE_STANDARD_KIND(AMARK_NODE_HEADING, "heading", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_INLINES,
                             false, false, NULL),
    AMARK_NODE_STANDARD_KIND(AMARK_NODE_THEMATIC_BREAK, "thematic_break", AMARK_NODE_CATEGORY_BLOCK,
                             AMARK_NODE_CONTENT_NONE, false, false, NULL),
    AMARK_NODE_STANDARD_KIND(AMARK_NODE_TEXT, "text", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_NONE, false, false,
                             NULL),
    AMARK_NODE_STANDARD_KIND(AMARK_NODE_SOFT_BREAK, "soft_break", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_NONE,
                             false, false, NULL),
    AMARK_NODE_STANDARD_KIND(AMARK_NODE_LINE_BREAK, "line_break", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_NONE,
                             false, false, NULL),
    AMARK_NODE_STANDARD_KIND(AMARK_NODE_CODE, "code", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_NONE, false, false,
                             NULL),
    AMARK_NODE_STANDARD_KIND(AMARK_NODE_HTML_INLINE, "html_inline", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_NONE,
                             false, false, NULL),
    AMARK_NODE_STANDARD_KIND(AMARK_NODE_EMPHASIS, "emphasis", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_INLINES,
                             false, false, NULL),
    AMARK_NODE_STANDARD_KIND(AMARK_NODE_STRONG, "strong", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_INLINES, false,
                             false, NULL),
    AMARK_NODE_STANDARD_KIND(AMARK_NODE_LINK, "link", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_INLINES, false,
                             false, NULL),
    AMARK_NODE_STANDARD_KIND(AMARK_NODE_IMAGE, "image", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_INLINES, false,
                             false, NULL),
};

/* list の受け入れ判定: 子が標準の item のときだけ真 */
static bool amark_node_list_can_contain(const amark_node_kind_t *parent, const amark_node_kind_t *child,
                                        void *user_data)
{
    (void)parent;
    (void)user_data;

    return child == &amark_node_standard_kinds[AMARK_NODE_ITEM - AMARK_NODE_DOCUMENT];
}

const amark_node_kind_t *amark_node_kind_get_standard(amark_node_type_t type)
{
    if ((type < AMARK_NODE_DOCUMENT) || (type > AMARK_NODE_IMAGE))
    {
        return NULL;
    }

    return &amark_node_standard_kinds[type - AMARK_NODE_DOCUMENT];
}

amark_node_type_t amark_node_kind_get_type(const amark_node_kind_t *kind)
{
    if (kind == NULL)
    {
        return AMARK_NODE_NONE;
    }

    return kind->type;
}

amark_slice_t amark_node_kind_get_name(const amark_node_kind_t *kind)
{
    if (kind == NULL)
    {
        return amark_slice_make(NULL, 0);
    }

    return amark_slice_make(kind->name, kind->name_len);
}

amark_node_category_t amark_node_kind_get_category(const amark_node_kind_t *kind)
{
    if (kind == NULL)
    {
        return AMARK_NODE_CATEGORY_NONE;
    }

    return kind->category;
}

amark_node_content_t amark_node_kind_get_content(const amark_node_kind_t *kind)
{
    if (kind == NULL)
    {
        return AMARK_NODE_CONTENT_NONE;
    }

    return kind->content;
}

bool amark_node_kind_is_leaf(const amark_node_kind_t *kind)
{
    if (kind == NULL)
    {
        return false;
    }

    return kind->content == AMARK_NODE_CONTENT_NONE;
}

bool amark_node_kind_can_contain(const amark_node_kind_t *parent, const amark_node_kind_t *child)
{
    if ((parent == NULL) || (child == NULL) || child->root_only)
    {
        return false;
    }

    switch (parent->content)
    {
    case AMARK_NODE_CONTENT_BLOCKS:
        return (child->category == AMARK_NODE_CATEGORY_BLOCK) && !child->requires_explicit_parent;
    case AMARK_NODE_CONTENT_INLINES:
        return (child->category == AMARK_NODE_CATEGORY_INLINE) && !child->requires_explicit_parent;
    case AMARK_NODE_CONTENT_CUSTOM:
        /* 明示親限定の種別も、判定関数が受け入れれば置ける */
        if (parent->can_contain == NULL)
        {
            return false;
        }

        return parent->can_contain(parent, child, parent->user_data);
    case AMARK_NODE_CONTENT_NONE:
    default:
        return false;
    }
}
