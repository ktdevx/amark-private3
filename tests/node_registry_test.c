/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * 拡張ノード種別のレジストリのテスト。生成と破棄、登録と検索、名前の形式の境界、標準種別と
 * 登録済みの名前の重複、記述子の矛盾、検証の順序、多数登録後のハンドルの不変、名前バッファの
 * 書き換えに影響されないこと、拡張種別の内容モデルと明示親限定が受け入れ判定と葉の判定に
 * 反映されること、確保の失敗注入で失敗が状態を変えないことを確かめる。
 */
#include <amark/error.h>
#include <amark/memory.h>
#include <amark/node_kind.h>
#include <amark/slice.h>

#include <naos/naos.h>

#include "support/test_allocator.h"

#include <stdio.h>
#include <string.h>

/* 受け入れ判定関数の呼び出しを記録する */
typedef struct test_predicate_state
{
    size_t calls;
    const amark_node_kind_t *last_parent;
    const amark_node_kind_t *last_child;
    const amark_node_kind_t *accepted; /* この種別だけを受け入れる（NULL なら常に真） */
} test_predicate_state_t;

static bool test_predicate(const amark_node_kind_t *parent, const amark_node_kind_t *child, void *user_data)
{
    test_predicate_state_t *state = (test_predicate_state_t *)user_data;

    state->calls++;
    state->last_parent = parent;
    state->last_child = child;
    if (state->accepted == NULL)
    {
        return true;
    }

    return child == state->accepted;
}

static bool test_always_false(const amark_node_kind_t *parent, const amark_node_kind_t *child, void *user_data)
{
    (void)parent;
    (void)child;
    (void)user_data;

    return false;
}

static void test_destroy_data(void *data, void *user_data)
{
    (void)data;
    (void)user_data;
}

/* 標準種別の名前（設計の標準種別の表） */
static const char *const test_standard_names[] = {
    "document",  "block_quote", "list",           "item",   "code_block", "html_block",
    "paragraph", "heading",     "thematic_break", "text",   "soft_break", "line_break",
    "code",      "html_inline", "emphasis",       "strong", "link",       "image",
};

#define TEST_STANDARD_NAME_COUNT (sizeof(test_standard_names) / sizeof(test_standard_names[0]))

/* 子を取らないブロックの記述子を作る */
static amark_node_kind_desc_t test_desc(const char *name)
{
    amark_node_kind_desc_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.name = amark_slice_from_cstr(name);
    desc.category = AMARK_NODE_CATEGORY_BLOCK;
    desc.content = AMARK_NODE_CONTENT_NONE;
    desc.requires_explicit_parent = false;
    desc.can_contain = NULL;
    desc.destroy_data = NULL;
    desc.user_data = NULL;

    return desc;
}

static amark_node_kind_desc_t test_desc_with(const char *name, amark_node_category_t category,
                                             amark_node_content_t content, bool requires_explicit_parent)
{
    amark_node_kind_desc_t desc = test_desc(name);

    desc.category = category;
    desc.content = content;
    desc.requires_explicit_parent = requires_explicit_parent;

    return desc;
}

/* 呼び出し前と比べられるよう、*kind に入れておく番兵の値 */
static const amark_node_kind_t *test_sentinel_kind(void)
{
    return (const amark_node_kind_t *)(const void *)test_standard_names;
}

/* 登録が expected で失敗し、*kind と生存ブロック数を変えないことを確かめる */
static void test_check_add_fails(amark_node_registry_t *registry, const amark_node_kind_desc_t *desc,
                                 amark_error_t expected)
{
    const amark_node_kind_t *kind = test_sentinel_kind();
    size_t live = test_allocator_stats().live;

    NAOS_CHECK_INT_EQ((int)expected, (int)amark_node_registry_add(registry, desc, &kind));
    NAOS_CHECK_PTR_EQ(test_sentinel_kind(), kind);
    NAOS_CHECK_UINT_EQ(live, test_allocator_stats().live);
}

NAOS_TEARDOWN(restore_default)
{
    test_allocator_uninstall();
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_allocator_set(NULL));
}

NAOS_TEST(create_and_destroy_empty_registry)
{
    amark_node_registry_t *registry = NULL;

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));
    NAOS_REQUIRE_NOT_NULL(registry);
    NAOS_CHECK_UINT_EQ(1u, test_allocator_stats().live);

    amark_node_registry_destroy(registry);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(create_rejects_null)
{
    test_allocator_install();
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_registry_create(NULL));
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().allocations);
}

NAOS_TEST(create_failure_leaves_output_unchanged)
{
    amark_node_registry_t *registry = (amark_node_registry_t *)(void *)&registry;

    test_allocator_install();
    test_allocator_fail_after(0);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NO_MEMORY, amark_node_registry_create(&registry));
    NAOS_CHECK_PTR_EQ((amark_node_registry_t *)(void *)&registry, registry);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
    NAOS_CHECK_UINT_EQ(1u, test_allocator_stats().failures);
}

NAOS_TEST(destroy_null_does_nothing)
{
    test_allocator_install();
    amark_node_registry_destroy(NULL);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().deallocations);
}

NAOS_TEST(add_returns_extension_kind_with_descriptor_values)
{
    amark_node_registry_t *registry = NULL;
    const amark_node_kind_t *kind = NULL;
    test_predicate_state_t state;
    amark_node_kind_desc_t desc = test_desc_with("table", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_CUSTOM, false);
    amark_slice_t name;

    memset(&state, 0, sizeof(state));
    desc.can_contain = test_predicate;
    desc.destroy_data = test_destroy_data;
    desc.user_data = &state;

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &kind));
    NAOS_REQUIRE_NOT_NULL(kind);

    NAOS_CHECK_INT_EQ((int)AMARK_NODE_EXTENSION, (int)amark_node_kind_get_type(kind));
    name = amark_node_kind_get_name(kind);
    NAOS_REQUIRE_NOT_NULL(name.data);
    NAOS_CHECK_MEM_EQ("table", 5, name.data, name.len);
    NAOS_CHECK_INT_EQ('\0', name.data[name.len]);
    NAOS_CHECK_INT_EQ((int)AMARK_NODE_CATEGORY_BLOCK, (int)amark_node_kind_get_category(kind));
    NAOS_CHECK_INT_EQ((int)AMARK_NODE_CONTENT_CUSTOM, (int)amark_node_kind_get_content(kind));
    NAOS_CHECK_FALSE(amark_node_kind_is_leaf(kind));
    NAOS_CHECK_PTR_EQ(kind, amark_node_registry_find(registry, amark_slice_from_cstr("table")));

    /* 登録で判定関数は呼ばれない */
    NAOS_CHECK_UINT_EQ(0u, state.calls);

    amark_node_registry_destroy(registry);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(add_rejects_null_arguments)
{
    amark_node_registry_t *registry = NULL;
    const amark_node_kind_t *kind = test_sentinel_kind();
    amark_node_kind_desc_t desc = test_desc("table");

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));

    test_check_add_fails(NULL, &desc, AMARK_ERROR_INVALID_ARGUMENT);
    test_check_add_fails(registry, NULL, AMARK_ERROR_INVALID_ARGUMENT);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_registry_add(registry, &desc, NULL));
    NAOS_CHECK_PTR_EQ(test_sentinel_kind(), kind);
    NAOS_CHECK_NULL(amark_node_registry_find(registry, amark_slice_from_cstr("table")));

    amark_node_registry_destroy(registry);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(name_length_boundaries)
{
    amark_node_registry_t *registry = NULL;
    const amark_node_kind_t *kind = NULL;
    char name64[AMARK_NODE_KIND_NAME_MAX_LENGTH + 2];
    char name65[AMARK_NODE_KIND_NAME_MAX_LENGTH + 2];
    amark_node_kind_desc_t desc;

    memset(name64, 'b', AMARK_NODE_KIND_NAME_MAX_LENGTH);
    name64[AMARK_NODE_KIND_NAME_MAX_LENGTH] = '\0';
    memset(name65, 'c', AMARK_NODE_KIND_NAME_MAX_LENGTH + 1);
    name65[AMARK_NODE_KIND_NAME_MAX_LENGTH + 1] = '\0';

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));

    /* 0 バイト（data あり・なし） */
    desc = test_desc("");
    test_check_add_fails(registry, &desc, AMARK_ERROR_INVALID_ARGUMENT);
    desc.name = amark_slice_make(NULL, 0);
    test_check_add_fails(registry, &desc, AMARK_ERROR_INVALID_ARGUMENT);

    /* len > 0 で data が NULL */
    desc.name = amark_slice_make(NULL, 3);
    test_check_add_fails(registry, &desc, AMARK_ERROR_INVALID_ARGUMENT);

    /* 65 バイト */
    desc = test_desc(name65);
    test_check_add_fails(registry, &desc, AMARK_ERROR_INVALID_ARGUMENT);
    NAOS_CHECK_NULL(amark_node_registry_find(registry, amark_slice_from_cstr(name65)));

    /* 1 バイト */
    desc = test_desc("a");
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &kind));
    NAOS_CHECK_PTR_EQ(kind, amark_node_registry_find(registry, amark_slice_from_cstr("a")));

    /* 64 バイト */
    desc = test_desc(name64);
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &kind));
    NAOS_CHECK_UINT_EQ((size_t)AMARK_NODE_KIND_NAME_MAX_LENGTH, amark_node_kind_get_name(kind).len);
    NAOS_CHECK_PTR_EQ(kind, amark_node_registry_find(registry, amark_slice_from_cstr(name64)));

    amark_node_registry_destroy(registry);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(name_characters)
{
    static const char *const invalid_names[] = {
        "1table", "_table",    "Table",  "tAble", "table-row", "table row", "table.row",
        "table!", "t\xC3\xA9", "tableZ", "9",     "_",         "A",
    };
    static const char *const valid_names[] = {
        "t", "z", "table_row", "h1", "a_b_c", "x0_9", "abcdefghijklmnopqrstuvwxyz0123456789_"};
    amark_node_registry_t *registry = NULL;
    const amark_node_kind_t *kind = NULL;
    amark_node_kind_desc_t desc;
    size_t i;

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));

    for (i = 0; i < (sizeof(invalid_names) / sizeof(invalid_names[0])); i++)
    {
        NAOS_NOTE("name %s", invalid_names[i]);
        desc = test_desc(invalid_names[i]);
        test_check_add_fails(registry, &desc, AMARK_ERROR_INVALID_ARGUMENT);
        NAOS_CHECK_NULL(amark_node_registry_find(registry, desc.name));
    }

    /* 途中に NUL を含む名前 */
    desc = test_desc("ab");
    desc.name = amark_slice_make("a\0b", 3);
    test_check_add_fails(registry, &desc, AMARK_ERROR_INVALID_ARGUMENT);

    for (i = 0; i < (sizeof(valid_names) / sizeof(valid_names[0])); i++)
    {
        NAOS_NOTE("name %s", valid_names[i]);
        desc = test_desc(valid_names[i]);
        NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &kind));
        NAOS_CHECK_PTR_EQ(kind, amark_node_registry_find(registry, desc.name));
    }

    amark_node_registry_destroy(registry);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(name_is_taken_by_slice_length_only)
{
    amark_node_registry_t *registry = NULL;
    const amark_node_kind_t *kind = NULL;
    amark_node_kind_desc_t desc = test_desc("table");

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));

    /* NUL 終端でない領域の先頭 3 バイトを名前にする */
    desc.name = amark_slice_make("tabXYZ", 3);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &kind));
    NAOS_CHECK_MEM_EQ("tab", 3, amark_node_kind_get_name(kind).data, amark_node_kind_get_name(kind).len);
    NAOS_CHECK_INT_EQ('\0', amark_node_kind_get_name(kind).data[3]);

    amark_node_registry_destroy(registry);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(descriptor_contradictions_are_rejected)
{
    amark_node_registry_t *registry = NULL;
    amark_node_kind_desc_t desc;

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));

    /* 分類が BLOCK / INLINE 以外 */
    desc = test_desc_with("ext", AMARK_NODE_CATEGORY_NONE, AMARK_NODE_CONTENT_NONE, false);
    test_check_add_fails(registry, &desc, AMARK_ERROR_INVALID_ARGUMENT);
    desc = test_desc_with("ext", (amark_node_category_t)3, AMARK_NODE_CONTENT_NONE, false);
    test_check_add_fails(registry, &desc, AMARK_ERROR_INVALID_ARGUMENT);
    desc = test_desc_with("ext", (amark_node_category_t)-1, AMARK_NODE_CONTENT_NONE, false);
    test_check_add_fails(registry, &desc, AMARK_ERROR_INVALID_ARGUMENT);

    /* 内容モデルが未定義値 */
    desc = test_desc_with("ext", AMARK_NODE_CATEGORY_BLOCK, (amark_node_content_t)4, false);
    test_check_add_fails(registry, &desc, AMARK_ERROR_INVALID_ARGUMENT);
    desc = test_desc_with("ext", AMARK_NODE_CATEGORY_BLOCK, (amark_node_content_t)-1, false);
    test_check_add_fails(registry, &desc, AMARK_ERROR_INVALID_ARGUMENT);

    /* CUSTOM で判定関数がない */
    desc = test_desc_with("ext", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_CUSTOM, false);
    test_check_add_fails(registry, &desc, AMARK_ERROR_INVALID_ARGUMENT);

    /* CUSTOM 以外で判定関数がある */
    desc = test_desc_with("ext", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_NONE, false);
    desc.can_contain = test_always_false;
    test_check_add_fails(registry, &desc, AMARK_ERROR_INVALID_ARGUMENT);
    desc = test_desc_with("ext", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_BLOCKS, false);
    desc.can_contain = test_always_false;
    test_check_add_fails(registry, &desc, AMARK_ERROR_INVALID_ARGUMENT);
    desc = test_desc_with("ext", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_INLINES, false);
    desc.can_contain = test_always_false;
    test_check_add_fails(registry, &desc, AMARK_ERROR_INVALID_ARGUMENT);

    /* INLINE で BLOCKS */
    desc = test_desc_with("ext", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_BLOCKS, false);
    test_check_add_fails(registry, &desc, AMARK_ERROR_INVALID_ARGUMENT);

    NAOS_CHECK_NULL(amark_node_registry_find(registry, amark_slice_from_cstr("ext")));

    amark_node_registry_destroy(registry);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(consistent_descriptors_are_accepted)
{
    static const struct
    {
        const char *name;
        amark_node_category_t category;
        amark_node_content_t content;
        bool custom;
    } cases[] = {
        {"block_none", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_NONE, false},
        {"block_blocks", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_BLOCKS, false},
        {"block_inlines", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_INLINES, false},
        {"block_custom", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_CUSTOM, true},
        {"inline_none", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_NONE, false},
        {"inline_inlines", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_INLINES, false},
        {"inline_custom", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_CUSTOM, true},
    };
    amark_node_registry_t *registry = NULL;
    size_t i;

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));

    for (i = 0; i < (sizeof(cases) / sizeof(cases[0])); i++)
    {
        amark_node_kind_desc_t desc = test_desc_with(cases[i].name, cases[i].category, cases[i].content, true);
        const amark_node_kind_t *kind = NULL;

        NAOS_NOTE("case %s", cases[i].name);
        if (cases[i].custom)
        {
            desc.can_contain = test_always_false;
        }

        NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &kind));
        NAOS_CHECK_INT_EQ((int)cases[i].category, (int)amark_node_kind_get_category(kind));
        NAOS_CHECK_INT_EQ((int)cases[i].content, (int)amark_node_kind_get_content(kind));
        NAOS_CHECK_INT_EQ((int)(cases[i].content == AMARK_NODE_CONTENT_NONE), (int)amark_node_kind_is_leaf(kind));
    }

    amark_node_registry_destroy(registry);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(standard_names_are_already_taken)
{
    amark_node_registry_t *registry = NULL;
    amark_node_kind_desc_t desc;
    size_t i;

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));

    for (i = 0; i < TEST_STANDARD_NAME_COUNT; i++)
    {
        NAOS_NOTE("name %s", test_standard_names[i]);
        desc = test_desc(test_standard_names[i]);
        test_check_add_fails(registry, &desc, AMARK_ERROR_ALREADY_EXISTS);
    }

    amark_node_registry_destroy(registry);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(registered_names_are_already_taken)
{
    amark_node_registry_t *registry = NULL;
    const amark_node_kind_t *first = NULL;
    amark_node_kind_desc_t desc = test_desc("table");

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &first));

    /* 記述子が違っても名前が同じなら重複 */
    desc = test_desc_with("table", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_INLINES, false);
    test_check_add_fails(registry, &desc, AMARK_ERROR_ALREADY_EXISTS);
    NAOS_CHECK_PTR_EQ(first, amark_node_registry_find(registry, amark_slice_from_cstr("table")));
    NAOS_CHECK_INT_EQ((int)AMARK_NODE_CATEGORY_BLOCK, (int)amark_node_kind_get_category(first));

    /* 前方一致・延長は別の名前 */
    desc = test_desc("tabl");
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &first));
    desc = test_desc("table_");
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &first));

    amark_node_registry_destroy(registry);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(validation_order_reports_invalid_argument_before_duplicate)
{
    amark_node_registry_t *registry = NULL;
    const amark_node_kind_t *kind = NULL;
    amark_node_kind_desc_t desc = test_desc("table");

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &kind));

    /* 重複する名前でも、記述子が矛盾していれば不正な引数 */
    desc = test_desc_with("table", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_CUSTOM, false);
    test_check_add_fails(registry, &desc, AMARK_ERROR_INVALID_ARGUMENT);
    desc = test_desc_with("paragraph", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_BLOCKS, false);
    test_check_add_fails(registry, &desc, AMARK_ERROR_INVALID_ARGUMENT);

    amark_node_registry_destroy(registry);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(find_returns_standard_extension_or_null)
{
    amark_node_registry_t *registry = NULL;
    const amark_node_kind_t *table = NULL;
    amark_node_kind_desc_t desc = test_desc("table");
    size_t i;

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));

    /* 空のレジストリでも標準種別は見つかる */
    for (i = 0; i < TEST_STANDARD_NAME_COUNT; i++)
    {
        const amark_node_kind_t *found =
            amark_node_registry_find(registry, amark_slice_from_cstr(test_standard_names[i]));

        NAOS_NOTE("name %s", test_standard_names[i]);
        NAOS_REQUIRE_NOT_NULL(found);
        NAOS_CHECK_PTR_EQ(amark_node_kind_get_standard((amark_node_type_t)(AMARK_NODE_DOCUMENT + (int)i)), found);
    }

    NAOS_CHECK_NULL(amark_node_registry_find(registry, amark_slice_from_cstr("table")));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &table));
    NAOS_CHECK_PTR_EQ(table, amark_node_registry_find(registry, amark_slice_from_cstr("table")));

    /* バイト単位の一致だけ */
    NAOS_CHECK_NULL(amark_node_registry_find(registry, amark_slice_from_cstr("tabl")));
    NAOS_CHECK_NULL(amark_node_registry_find(registry, amark_slice_from_cstr("tables")));
    NAOS_CHECK_NULL(amark_node_registry_find(registry, amark_slice_from_cstr("Table")));
    NAOS_CHECK_NULL(amark_node_registry_find(registry, amark_slice_make("table\0", 6)));
    NAOS_CHECK_NULL(amark_node_registry_find(registry, amark_slice_from_cstr("Paragraph")));
    NAOS_CHECK_NULL(amark_node_registry_find(registry, amark_slice_from_cstr("")));
    NAOS_CHECK_NULL(amark_node_registry_find(registry, amark_slice_make(NULL, 0)));
    NAOS_CHECK_NULL(amark_node_registry_find(registry, amark_slice_make(NULL, 5)));

    /* registry が NULL なら標準名でも NULL */
    NAOS_CHECK_NULL(amark_node_registry_find(NULL, amark_slice_from_cstr("paragraph")));
    NAOS_CHECK_NULL(amark_node_registry_find(NULL, amark_slice_from_cstr("table")));

    amark_node_registry_destroy(registry);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(registries_are_independent)
{
    amark_node_registry_t *a = NULL;
    amark_node_registry_t *b = NULL;
    const amark_node_kind_t *in_a = NULL;
    const amark_node_kind_t *in_b = NULL;
    amark_node_kind_desc_t desc = test_desc("table");

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&a));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&b));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(a, &desc, &in_a));
    NAOS_CHECK_NULL(amark_node_registry_find(b, amark_slice_from_cstr("table")));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(b, &desc, &in_b));
    NAOS_CHECK_TRUE(in_a != in_b);
    NAOS_CHECK_PTR_EQ(in_a, amark_node_registry_find(a, amark_slice_from_cstr("table")));
    NAOS_CHECK_PTR_EQ(in_b, amark_node_registry_find(b, amark_slice_from_cstr("table")));

    amark_node_registry_destroy(a);
    NAOS_CHECK_PTR_EQ(in_b, amark_node_registry_find(b, amark_slice_from_cstr("table")));
    amark_node_registry_destroy(b);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

#define TEST_MANY_KINDS 200

NAOS_TEST(handles_stay_stable_after_many_registrations)
{
    amark_node_registry_t *registry = NULL;
    const amark_node_kind_t *kinds[TEST_MANY_KINDS];
    amark_slice_t names[TEST_MANY_KINDS];
    char buf[32];
    size_t i;

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));

    for (i = 0; i < TEST_MANY_KINDS; i++)
    {
        amark_node_kind_desc_t desc;

        (void)snprintf(buf, sizeof(buf), "kind_%u", (unsigned)i);
        desc = test_desc_with(buf, ((i % 2u) == 0u) ? AMARK_NODE_CATEGORY_BLOCK : AMARK_NODE_CATEGORY_INLINE,
                              AMARK_NODE_CONTENT_NONE, false);
        NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &kinds[i]));
        names[i] = amark_node_kind_get_name(kinds[i]);
    }

    for (i = 0; i < TEST_MANY_KINDS; i++)
    {
        amark_slice_t name;

        (void)snprintf(buf, sizeof(buf), "kind_%u", (unsigned)i);
        NAOS_NOTE("kind %s", buf);
        NAOS_CHECK_PTR_EQ(kinds[i], amark_node_registry_find(registry, amark_slice_from_cstr(buf)));
        name = amark_node_kind_get_name(kinds[i]);
        NAOS_CHECK_PTR_EQ(names[i].data, name.data);
        NAOS_CHECK_MEM_EQ(buf, strlen(buf), name.data, name.len);
        NAOS_CHECK_INT_EQ((int)(((i % 2u) == 0u) ? AMARK_NODE_CATEGORY_BLOCK : AMARK_NODE_CATEGORY_INLINE),
                          (int)amark_node_kind_get_category(kinds[i]));
    }

    /* 種別ごとの種別と名前、レジストリ本体と配列 */
    NAOS_CHECK_UINT_EQ((size_t)((2 * TEST_MANY_KINDS) + 2), test_allocator_stats().live);

    amark_node_registry_destroy(registry);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(name_is_copied_at_registration)
{
    amark_node_registry_t *registry = NULL;
    const amark_node_kind_t *kind = NULL;
    char buf[] = "footnote";
    amark_node_kind_desc_t desc = test_desc(buf);
    amark_slice_t name;

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &kind));

    memcpy(buf, "xxxxxxxx", sizeof(buf));
    name = amark_node_kind_get_name(kind);
    NAOS_CHECK_TRUE(name.data != buf);
    NAOS_CHECK_MEM_EQ("footnote", 8, name.data, name.len);
    NAOS_CHECK_PTR_EQ(kind, amark_node_registry_find(registry, amark_slice_from_cstr("footnote")));
    NAOS_CHECK_NULL(amark_node_registry_find(registry, amark_slice_from_cstr("xxxxxxxx")));

    amark_node_registry_destroy(registry);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(extension_content_models_drive_can_contain_and_is_leaf)
{
    amark_node_registry_t *registry = NULL;
    const amark_node_kind_t *ext_blocks = NULL;
    const amark_node_kind_t *ext_inlines = NULL;
    const amark_node_kind_t *ext_leaf = NULL;
    const amark_node_kind_t *ext_block = NULL;
    const amark_node_kind_t *ext_inline = NULL;
    const amark_node_kind_t *ext_explicit_block = NULL;
    const amark_node_kind_t *ext_explicit_inline = NULL;
    const amark_node_kind_t *document = amark_node_kind_get_standard(AMARK_NODE_DOCUMENT);
    const amark_node_kind_t *paragraph = amark_node_kind_get_standard(AMARK_NODE_PARAGRAPH);
    const amark_node_kind_t *item = amark_node_kind_get_standard(AMARK_NODE_ITEM);
    const amark_node_kind_t *list = amark_node_kind_get_standard(AMARK_NODE_LIST);
    const amark_node_kind_t *text = amark_node_kind_get_standard(AMARK_NODE_TEXT);
    const amark_node_kind_t *emphasis = amark_node_kind_get_standard(AMARK_NODE_EMPHASIS);
    amark_node_kind_desc_t desc;

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));

    desc = test_desc_with("ext_blocks", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_BLOCKS, false);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &ext_blocks));
    desc = test_desc_with("ext_inlines", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_INLINES, false);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &ext_inlines));
    desc = test_desc_with("ext_leaf", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_NONE, false);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &ext_leaf));
    desc = test_desc_with("ext_block", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_NONE, false);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &ext_block));
    desc = test_desc_with("ext_inline", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_NONE, false);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &ext_inline));
    desc = test_desc_with("ext_explicit_block", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_INLINES, true);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &ext_explicit_block));
    desc = test_desc_with("ext_explicit_inline", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_NONE, true);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &ext_explicit_inline));

    /* 葉の判定は内容モデルだけで決まる */
    NAOS_CHECK_FALSE(amark_node_kind_is_leaf(ext_blocks));
    NAOS_CHECK_FALSE(amark_node_kind_is_leaf(ext_inlines));
    NAOS_CHECK_TRUE(amark_node_kind_is_leaf(ext_leaf));
    NAOS_CHECK_FALSE(amark_node_kind_is_leaf(ext_explicit_block));
    NAOS_CHECK_TRUE(amark_node_kind_is_leaf(ext_explicit_inline));

    /* 任意のブロック: 明示親限定と document を除くブロック */
    NAOS_CHECK_TRUE(amark_node_kind_can_contain(ext_blocks, paragraph));
    NAOS_CHECK_TRUE(amark_node_kind_can_contain(ext_blocks, list));
    NAOS_CHECK_TRUE(amark_node_kind_can_contain(ext_blocks, ext_block));
    NAOS_CHECK_TRUE(amark_node_kind_can_contain(ext_blocks, ext_blocks));
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(ext_blocks, item));
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(ext_blocks, document));
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(ext_blocks, ext_explicit_block));
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(ext_blocks, text));
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(ext_blocks, ext_inline));

    /* 任意のインライン: 明示親限定を除くインライン */
    NAOS_CHECK_TRUE(amark_node_kind_can_contain(ext_inlines, text));
    NAOS_CHECK_TRUE(amark_node_kind_can_contain(ext_inlines, emphasis));
    NAOS_CHECK_TRUE(amark_node_kind_can_contain(ext_inlines, ext_inline));
    NAOS_CHECK_TRUE(amark_node_kind_can_contain(ext_inlines, ext_inlines));
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(ext_inlines, ext_explicit_inline));
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(ext_inlines, paragraph));
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(ext_inlines, ext_block));

    /* 子なし: 何も受け入れない */
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(ext_leaf, paragraph));
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(ext_leaf, text));
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(ext_leaf, ext_block));

    /* 標準種別の親と拡張種別の子 */
    NAOS_CHECK_TRUE(amark_node_kind_can_contain(document, ext_block));
    NAOS_CHECK_TRUE(amark_node_kind_can_contain(document, ext_blocks));
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(document, ext_explicit_block));
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(document, ext_inline));
    NAOS_CHECK_TRUE(amark_node_kind_can_contain(paragraph, ext_inline));
    NAOS_CHECK_TRUE(amark_node_kind_can_contain(paragraph, ext_inlines));
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(paragraph, ext_explicit_inline));
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(paragraph, ext_block));
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(list, ext_block));
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(list, ext_explicit_block));

    amark_node_registry_destroy(registry);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(custom_predicate_decides_can_contain)
{
    amark_node_registry_t *registry = NULL;
    const amark_node_kind_t *table = NULL;
    const amark_node_kind_t *row = NULL;
    const amark_node_kind_t *always = NULL;
    const amark_node_kind_t *document = amark_node_kind_get_standard(AMARK_NODE_DOCUMENT);
    const amark_node_kind_t *paragraph = amark_node_kind_get_standard(AMARK_NODE_PARAGRAPH);
    const amark_node_kind_t *item = amark_node_kind_get_standard(AMARK_NODE_ITEM);
    test_predicate_state_t table_state;
    test_predicate_state_t always_state;
    amark_node_kind_desc_t desc;

    memset(&table_state, 0, sizeof(table_state));
    memset(&always_state, 0, sizeof(always_state));

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));

    /* 明示親限定の行を、独自判定の表だけが受け入れる */
    desc = test_desc_with("table_row", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_INLINES, true);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &row));
    desc = test_desc_with("table", AMARK_NODE_CATEGORY_BLOCK, AMARK_NODE_CONTENT_CUSTOM, false);
    desc.can_contain = test_predicate;
    desc.user_data = &table_state;
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &table));
    table_state.accepted = row;

    desc = test_desc_with("anything", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_CUSTOM, false);
    desc.can_contain = test_predicate;
    desc.user_data = &always_state;
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &always));

    NAOS_CHECK_FALSE(amark_node_kind_is_leaf(table));
    NAOS_CHECK_FALSE(amark_node_kind_is_leaf(always));

    NAOS_CHECK_TRUE(amark_node_kind_can_contain(table, row));
    NAOS_CHECK_UINT_EQ(1u, table_state.calls);
    NAOS_CHECK_PTR_EQ(table, table_state.last_parent);
    NAOS_CHECK_PTR_EQ(row, table_state.last_child);

    NAOS_CHECK_FALSE(amark_node_kind_can_contain(table, paragraph));
    NAOS_CHECK_UINT_EQ(2u, table_state.calls);
    NAOS_CHECK_PTR_EQ(paragraph, table_state.last_child);

    /* 行は明示親限定なので、任意のブロックの親には置けない */
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(document, row));

    /* 判定関数は明示親限定の標準種別も受け入れられる */
    NAOS_CHECK_TRUE(amark_node_kind_can_contain(always, item));
    NAOS_CHECK_TRUE(amark_node_kind_can_contain(always, paragraph));

    /* document は判定関数を呼ばずに拒否する */
    always_state.calls = 0;
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(always, document));
    NAOS_CHECK_UINT_EQ(0u, always_state.calls);

    /* 同じ組には同じ結果 */
    NAOS_CHECK_TRUE(amark_node_kind_can_contain(table, row));
    NAOS_CHECK_TRUE(amark_node_kind_can_contain(table, row));

    amark_node_registry_destroy(registry);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

/*
 * 確保の失敗注入の網羅パターン。prefill 件を登録したレジストリに 1 件を登録する操作を、成功させる
 * 確保の回数 0, 1, 2, ... で失敗させ、成功するまで続ける。各失敗で NO_MEMORY、*kind・生存ブロック数・
 * 登録内容が変わらないことを確かめ、成功までに注入した失敗の回数を返す
 */
static size_t test_exhaust_add_failures(size_t prefill)
{
    size_t successes;
    size_t failures = 0;

    for (successes = 0;; successes++)
    {
        amark_node_registry_t *registry = NULL;
        const amark_node_kind_t *prefilled[8];
        const amark_node_kind_t *kind = test_sentinel_kind();
        amark_node_kind_desc_t desc;
        amark_error_t err;
        char buf[16];
        size_t live;
        size_t i;

        test_allocator_install();
        NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));
        for (i = 0; i < prefill; i++)
        {
            (void)snprintf(buf, sizeof(buf), "pre_%u", (unsigned)i);
            desc = test_desc(buf);
            NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &prefilled[i]));
        }

        live = test_allocator_stats().live;
        desc = test_desc_with("target", AMARK_NODE_CATEGORY_INLINE, AMARK_NODE_CONTENT_INLINES, false);
        test_allocator_fail_after(successes);
        err = amark_node_registry_add(registry, &desc, &kind);
        test_allocator_never_fail();

        NAOS_NOTE("prefill %u, successes %u", (unsigned)prefill, (unsigned)successes);
        if (err == AMARK_OK)
        {
            NAOS_CHECK_NOT_NULL(kind);
            NAOS_CHECK_PTR_EQ(kind, amark_node_registry_find(registry, amark_slice_from_cstr("target")));
            amark_node_registry_destroy(registry);
            NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
            break;
        }

        failures++;
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NO_MEMORY, err);
        NAOS_CHECK_PTR_EQ(test_sentinel_kind(), kind);
        NAOS_CHECK_UINT_EQ(live, test_allocator_stats().live);
        NAOS_CHECK_NULL(amark_node_registry_find(registry, amark_slice_from_cstr("target")));
        for (i = 0; i < prefill; i++)
        {
            (void)snprintf(buf, sizeof(buf), "pre_%u", (unsigned)i);
            NAOS_CHECK_PTR_EQ(prefilled[i], amark_node_registry_find(registry, amark_slice_from_cstr(buf)));
        }

        /* 失敗の後も同じ登録が成功する */
        kind = NULL;
        NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &kind));
        NAOS_CHECK_PTR_EQ(kind, amark_node_registry_find(registry, amark_slice_from_cstr("target")));

        amark_node_registry_destroy(registry);
        NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
        if (successes > 16u)
        {
            NAOS_FAIL("add never succeeded");
        }
    }

    return failures;
}

NAOS_TEST(add_failure_injection_first_registration)
{
    /* 種別、名前の複製、配列の初回確保 */
    NAOS_CHECK_UINT_EQ(3u, test_exhaust_add_failures(0));
}

NAOS_TEST(add_failure_injection_without_growth)
{
    /* 種別、名前の複製（配列の容量 4 に空きがある） */
    NAOS_CHECK_UINT_EQ(2u, test_exhaust_add_failures(1));
    NAOS_CHECK_UINT_EQ(2u, test_exhaust_add_failures(3));
}

NAOS_TEST(add_failure_injection_with_growth)
{
    /* 種別、名前の複製、配列の伸長（容量 4 から 8） */
    NAOS_CHECK_UINT_EQ(3u, test_exhaust_add_failures(4));
    /* 容量 8 から 16 */
    NAOS_CHECK_UINT_EQ(3u, test_exhaust_add_failures(8));
}

/* clang-format off */
NAOS_SUITE(node_registry,
    NAOS_SET_TEARDOWN(restore_default),
    NAOS_ADD_TEST(create_and_destroy_empty_registry),
    NAOS_ADD_TEST(create_rejects_null),
    NAOS_ADD_TEST(create_failure_leaves_output_unchanged),
    NAOS_ADD_TEST(destroy_null_does_nothing),
    NAOS_ADD_TEST(add_returns_extension_kind_with_descriptor_values),
    NAOS_ADD_TEST(add_rejects_null_arguments),
    NAOS_ADD_TEST(name_length_boundaries),
    NAOS_ADD_TEST(name_characters),
    NAOS_ADD_TEST(name_is_taken_by_slice_length_only),
    NAOS_ADD_TEST(descriptor_contradictions_are_rejected),
    NAOS_ADD_TEST(consistent_descriptors_are_accepted),
    NAOS_ADD_TEST(standard_names_are_already_taken),
    NAOS_ADD_TEST(registered_names_are_already_taken),
    NAOS_ADD_TEST(validation_order_reports_invalid_argument_before_duplicate),
    NAOS_ADD_TEST(find_returns_standard_extension_or_null),
    NAOS_ADD_TEST(registries_are_independent),
    NAOS_ADD_TEST(handles_stay_stable_after_many_registrations),
    NAOS_ADD_TEST(name_is_copied_at_registration),
    NAOS_ADD_TEST(extension_content_models_drive_can_contain_and_is_leaf),
    NAOS_ADD_TEST(custom_predicate_decides_can_contain),
    NAOS_ADD_TEST(add_failure_injection_first_registration),
    NAOS_ADD_TEST(add_failure_injection_without_growth),
    NAOS_ADD_TEST(add_failure_injection_with_growth)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(node_registry)
)
/* clang-format on */
