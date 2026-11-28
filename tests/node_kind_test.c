/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * ノード種別のテスト。18 の標準種別の識別値・名前・分類・内容モデル、葉の判定、標準種別の問い合わせの
 * 「ない」結果とハンドルの同一性、NULL に対する問い合わせ、18 × 18 の受け入れ判定表を確かめる。
 * 期待値は設計の標準種別の表（design.md の NodeKind）から書き起こし、実装の結果から導かない。
 */
#include <amark/node_kind.h>

#include <naos/naos.h>

#include <stddef.h>
#include <string.h>

#define TEST_STANDARD_KIND_COUNT 18

/* 設計の標準種別の表の 1 行 */
typedef struct test_standard_kind
{
    amark_node_type_t type;
    const char *name;
    amark_node_category_t category;
    amark_node_content_t content;
    bool is_leaf;
} test_standard_kind_t;

/* 並びは amark_node_type_t の並び（DOCUMENT〜IMAGE）と同じ */
static const test_standard_kind_t test_standard_kinds[TEST_STANDARD_KIND_COUNT] = {
    {AMARK_NODE_DOCUMENT, "document", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_BLOCKS, false},
    {AMARK_NODE_BLOCK_QUOTE, "block_quote", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_BLOCKS, false},
    {AMARK_NODE_LIST, "list", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_CUSTOM, false},
    {AMARK_NODE_ITEM, "item", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_BLOCKS, false},
    {AMARK_NODE_CODE_BLOCK, "code_block", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_NONE, true},
    {AMARK_NODE_HTML_BLOCK, "html_block", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_NONE, true},
    {AMARK_NODE_PARAGRAPH, "paragraph", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_INLINES, false},
    {AMARK_NODE_HEADING, "heading", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_INLINES, false},
    {AMARK_NODE_THEMATIC_BREAK, "thematic_break", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_NONE, true},
    {AMARK_NODE_TEXT, "text", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_NONE, true},
    {AMARK_NODE_SOFT_BREAK, "soft_break", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_NONE, true},
    {AMARK_NODE_LINE_BREAK, "line_break", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_NONE, true},
    {AMARK_NODE_CODE, "code", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_NONE, true},
    {AMARK_NODE_HTML_INLINE, "html_inline", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_NONE, true},
    {AMARK_NODE_EMPHASIS, "emphasis", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_INLINES, false},
    {AMARK_NODE_STRONG, "strong", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_INLINES, false},
    {AMARK_NODE_LINK, "link", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_INLINES, false},
    {AMARK_NODE_IMAGE, "image", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_INLINES, false},
};

/*
 * 受け入れ判定の期待表。行が親、列が子で、並びは test_standard_kinds と同じ。
 * 列: DOC BQ LIST ITEM CB HB PARA HEAD TB | TEXT SB LB CODE HI EM STRONG LINK IMG
 * - document・block quote・item: document と item を除く任意のブロック
 * - list: item だけ
 * - paragraph・heading・emphasis・strong・link・image: 任意のインライン
 * - それ以外（葉）: 何も受け入れない
 */
static const unsigned char test_can_contain_table[TEST_STANDARD_KIND_COUNT][TEST_STANDARD_KIND_COUNT] = {
    /* DOCUMENT */ {0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* BLOCK_QUOTE */ {0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* LIST */ {0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* ITEM */ {0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* CODE_BLOCK */ {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* HTML_BLOCK */ {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* PARAGRAPH */ {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    /* HEADING */ {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    /* THEMATIC_BREAK */ {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* TEXT */ {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* SOFT_BREAK */ {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* LINE_BREAK */ {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* CODE */ {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* HTML_INLINE */ {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* EMPHASIS */ {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    /* STRONG */ {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    /* LINK */ {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    /* IMAGE */ {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1},
};

NAOS_TEST(type_values_are_fixed)
{
    NAOS_CHECK_INT_EQ(0, (int)AMARK_NODE_NONE);
    NAOS_CHECK_INT_EQ(1, (int)AMARK_NODE_DOCUMENT);
    NAOS_CHECK_INT_EQ(9, (int)AMARK_NODE_THEMATIC_BREAK);
    NAOS_CHECK_INT_EQ(10, (int)AMARK_NODE_TEXT);
    NAOS_CHECK_INT_EQ(18, (int)AMARK_NODE_IMAGE);
    NAOS_CHECK_INT_EQ(19, (int)AMARK_NODE_EXTENSION);
}

NAOS_TEST(standard_kinds_match_design_table)
{
    size_t i;

    for (i = 0; i < TEST_STANDARD_KIND_COUNT; i++)
    {
        const test_standard_kind_t *expected = &test_standard_kinds[i];
        const amark_node_kind_t *kind = amark_node_kind_get_standard(expected->type);
        amark_slice_t name;

        NAOS_NOTE("kind %s", expected->name);
        NAOS_REQUIRE_NOT_NULL(kind);
        NAOS_CHECK_INT_EQ((int)expected->type, (int)amark_node_kind_get_type(kind));
        name = amark_node_kind_get_name(kind);
        NAOS_REQUIRE_NOT_NULL(name.data);
        NAOS_CHECK_UINT_EQ(strlen(expected->name), name.len);
        NAOS_CHECK_MEM_EQ(expected->name, strlen(expected->name), name.data, name.len);
        NAOS_CHECK_INT_EQ('\0', name.data[name.len]);
        NAOS_CHECK_INT_EQ((int)expected->category, (int)amark_node_kind_get_category(kind));
        NAOS_CHECK_INT_EQ((int)expected->content, (int)amark_node_kind_get_content(kind));
    }
}

NAOS_TEST(standard_kind_names_are_unique)
{
    size_t i;
    size_t j;

    for (i = 0; i < TEST_STANDARD_KIND_COUNT; i++)
    {
        amark_slice_t a = amark_node_kind_get_name(amark_node_kind_get_standard(test_standard_kinds[i].type));

        for (j = i + 1; j < TEST_STANDARD_KIND_COUNT; j++)
        {
            amark_slice_t b = amark_node_kind_get_name(amark_node_kind_get_standard(test_standard_kinds[j].type));

            NAOS_CHECK_FALSE((a.len == b.len) && (memcmp(a.data, b.data, a.len) == 0));
        }
    }
}

NAOS_TEST(standard_kind_handle_is_stable_and_distinct)
{
    size_t i;
    size_t j;

    for (i = 0; i < TEST_STANDARD_KIND_COUNT; i++)
    {
        amark_node_type_t type = test_standard_kinds[i].type;

        NAOS_NOTE("kind %s", test_standard_kinds[i].name);
        NAOS_CHECK_PTR_EQ(amark_node_kind_get_standard(type), amark_node_kind_get_standard(type));
        for (j = 0; j < TEST_STANDARD_KIND_COUNT; j++)
        {
            if (i != j)
            {
                NAOS_CHECK_TRUE(amark_node_kind_get_standard(type) !=
                                amark_node_kind_get_standard(test_standard_kinds[j].type));
            }
        }
    }
}

NAOS_TEST(get_standard_returns_null_for_undefined_values)
{
    NAOS_CHECK_NULL(amark_node_kind_get_standard(AMARK_NODE_NONE));
    NAOS_CHECK_NULL(amark_node_kind_get_standard(AMARK_NODE_EXTENSION));
    NAOS_CHECK_NULL(amark_node_kind_get_standard((amark_node_type_t)20));
    NAOS_CHECK_NULL(amark_node_kind_get_standard((amark_node_type_t)255));
    NAOS_CHECK_NULL(amark_node_kind_get_standard((amark_node_type_t)-1));
}

NAOS_TEST(queries_on_null_return_none)
{
    amark_slice_t name = amark_node_kind_get_name(NULL);

    NAOS_CHECK_INT_EQ((int)AMARK_NODE_NONE, (int)amark_node_kind_get_type(NULL));
    NAOS_CHECK_NULL(name.data);
    NAOS_CHECK_UINT_EQ(0u, name.len);
    NAOS_CHECK_INT_EQ((int)AMARK_NODE_CATEGORY_NONE, (int)amark_node_kind_get_category(NULL));
    NAOS_CHECK_INT_EQ((int)AMARK_NODE_CONTENT_NONE, (int)amark_node_kind_get_content(NULL));
    NAOS_CHECK_FALSE(amark_node_kind_is_leaf(NULL));
}

NAOS_TEST(is_leaf_is_true_for_exactly_the_childless_kinds)
{
    size_t i;
    size_t leaf_count = 0;

    for (i = 0; i < TEST_STANDARD_KIND_COUNT; i++)
    {
        const amark_node_kind_t *kind = amark_node_kind_get_standard(test_standard_kinds[i].type);
        bool is_leaf = amark_node_kind_is_leaf(kind);

        NAOS_NOTE("kind %s", test_standard_kinds[i].name);
        NAOS_CHECK_INT_EQ((int)test_standard_kinds[i].is_leaf, (int)is_leaf);
        if (is_leaf)
        {
            leaf_count++;
        }
    }

    NAOS_CHECK_UINT_EQ(8u, leaf_count);
}

NAOS_TEST(can_contain_matches_design_table)
{
    size_t parent_index;
    size_t child_index;

    for (parent_index = 0; parent_index < TEST_STANDARD_KIND_COUNT; parent_index++)
    {
        const amark_node_kind_t *parent = amark_node_kind_get_standard(test_standard_kinds[parent_index].type);

        for (child_index = 0; child_index < TEST_STANDARD_KIND_COUNT; child_index++)
        {
            const amark_node_kind_t *child = amark_node_kind_get_standard(test_standard_kinds[child_index].type);
            bool expected = (test_can_contain_table[parent_index][child_index] != 0);

            NAOS_NOTE("parent %s, child %s", test_standard_kinds[parent_index].name,
                      test_standard_kinds[child_index].name);
            NAOS_CHECK_INT_EQ((int)expected, (int)amark_node_kind_can_contain(parent, child));
        }
    }
}

NAOS_TEST(can_contain_is_repeatable)
{
    const amark_node_kind_t *list = amark_node_kind_get_standard(AMARK_NODE_LIST);
    const amark_node_kind_t *item = amark_node_kind_get_standard(AMARK_NODE_ITEM);
    const amark_node_kind_t *paragraph = amark_node_kind_get_standard(AMARK_NODE_PARAGRAPH);
    int i;

    for (i = 0; i < 3; i++)
    {
        NAOS_CHECK_TRUE(amark_node_kind_can_contain(list, item));
        NAOS_CHECK_FALSE(amark_node_kind_can_contain(list, paragraph));
    }
}

NAOS_TEST(can_contain_with_null_is_false)
{
    const amark_node_kind_t *document = amark_node_kind_get_standard(AMARK_NODE_DOCUMENT);
    const amark_node_kind_t *paragraph = amark_node_kind_get_standard(AMARK_NODE_PARAGRAPH);

    NAOS_CHECK_FALSE(amark_node_kind_can_contain(NULL, paragraph));
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(document, NULL));
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(NULL, NULL));
}

/* clang-format off */
NAOS_SUITE(node_kind,
    NAOS_ADD_TEST(type_values_are_fixed),
    NAOS_ADD_TEST(standard_kinds_match_design_table),
    NAOS_ADD_TEST(standard_kind_names_are_unique),
    NAOS_ADD_TEST(standard_kind_handle_is_stable_and_distinct),
    NAOS_ADD_TEST(get_standard_returns_null_for_undefined_values),
    NAOS_ADD_TEST(queries_on_null_return_none),
    NAOS_ADD_TEST(is_leaf_is_true_for_exactly_the_childless_kinds),
    NAOS_ADD_TEST(can_contain_matches_design_table),
    NAOS_ADD_TEST(can_contain_is_repeatable),
    NAOS_ADD_TEST(can_contain_with_null_is_false)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(node_kind)
)
/* clang-format on */
