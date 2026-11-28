/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * ノードの生成・取り外し・破棄・参照のテスト。標準種別と拡張種別の生成と初期リンク、未知種別と
 * 空ポインタの拒否、空ポインタへの問い合わせ、根と空ポインタの取り外しが何もしないこと、空ポインタの
 * 破棄、生成の失敗注入で出力と生存ブロック数が変わらないこと、破棄後に生存ブロック数が 0 であることを
 * 確かめる。木を組む必要がある確認（親を持つノードの取り外し・破棄、参照、深い木の破棄）は
 * 編集操作のテスト（node_edit_test.c）で行う。
 */
#include <amark/error.h>
#include <amark/memory.h>
#include <amark/node.h>
#include <amark/node_kind.h>
#include <amark/slice.h>

#include <naos/naos.h>

#include "support/test_allocator.h"

#include <string.h>

/* 標準種別の識別値（DOCUMENT〜IMAGE） */
static const amark_node_type_t test_standard_types[] = {
    AMARK_NODE_DOCUMENT,   AMARK_NODE_BLOCK_QUOTE, AMARK_NODE_LIST,    AMARK_NODE_ITEM,           AMARK_NODE_CODE_BLOCK,
    AMARK_NODE_HTML_BLOCK, AMARK_NODE_PARAGRAPH,   AMARK_NODE_HEADING, AMARK_NODE_THEMATIC_BREAK, AMARK_NODE_TEXT,
    AMARK_NODE_SOFT_BREAK, AMARK_NODE_LINE_BREAK,  AMARK_NODE_CODE,    AMARK_NODE_HTML_INLINE,    AMARK_NODE_EMPHASIS,
    AMARK_NODE_STRONG,     AMARK_NODE_LINK,        AMARK_NODE_IMAGE,
};

#define TEST_STANDARD_TYPE_COUNT (sizeof(test_standard_types) / sizeof(test_standard_types[0]))

/* 呼び出し前と比べられるよう、*node に入れておく番兵の値 */
static amark_node_t *test_sentinel_node(void)
{
    return (amark_node_t *)(void *)&test_standard_types[0];
}

/* ノードが親・子・兄弟を持たないことを確かめる */
static void test_check_unlinked(const amark_node_t *node)
{
    NAOS_CHECK_NULL(amark_node_get_parent(node));
    NAOS_CHECK_NULL(amark_node_get_first_child(node));
    NAOS_CHECK_NULL(amark_node_get_last_child(node));
    NAOS_CHECK_NULL(amark_node_get_next(node));
    NAOS_CHECK_NULL(amark_node_get_previous(node));
}

/* 子を取らない拡張ブロックの記述子を作る */
static amark_node_kind_desc_t test_extension_desc(const char *name)
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

NAOS_TEARDOWN(restore_default)
{
    test_allocator_uninstall();
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_allocator_set(NULL));
}

NAOS_TEST(create_standard_kinds)
{
    size_t i;

    test_allocator_install();
    for (i = 0; i < TEST_STANDARD_TYPE_COUNT; i++)
    {
        amark_node_t *node = NULL;

        NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create(test_standard_types[i], &node));
        NAOS_REQUIRE_NOT_NULL(node);
        NAOS_CHECK_INT_EQ((int)test_standard_types[i], (int)amark_node_get_type(node));
        NAOS_CHECK_PTR_EQ(amark_node_kind_get_standard(test_standard_types[i]), amark_node_get_kind(node));
        test_check_unlinked(node);
        NAOS_CHECK_UINT_EQ(1u, test_allocator_stats().live);

        amark_node_destroy(node);
        NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
    }
}

NAOS_TEST(create_with_standard_kind_handle)
{
    size_t i;

    test_allocator_install();
    for (i = 0; i < TEST_STANDARD_TYPE_COUNT; i++)
    {
        const amark_node_kind_t *kind = amark_node_kind_get_standard(test_standard_types[i]);
        amark_node_t *node = NULL;

        NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create_with_kind(kind, &node));
        NAOS_REQUIRE_NOT_NULL(node);
        NAOS_CHECK_INT_EQ((int)test_standard_types[i], (int)amark_node_get_type(node));
        NAOS_CHECK_PTR_EQ(kind, amark_node_get_kind(node));
        test_check_unlinked(node);

        amark_node_destroy(node);
    }

    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(create_extension_kind)
{
    amark_node_registry_t *registry = NULL;
    amark_node_kind_desc_t desc = test_extension_desc("table");
    const amark_node_kind_t *kind = NULL;
    amark_node_t *node = NULL;

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &kind));
    NAOS_REQUIRE_NOT_NULL(kind);

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create_with_kind(kind, &node));
    NAOS_REQUIRE_NOT_NULL(node);
    NAOS_CHECK_INT_EQ((int)AMARK_NODE_EXTENSION, (int)amark_node_get_type(node));
    NAOS_CHECK_PTR_EQ(kind, amark_node_get_kind(node));
    test_check_unlinked(node);

    amark_node_destroy(node);
    amark_node_registry_destroy(registry);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(create_rejects_unknown_types)
{
    static const amark_node_type_t unknown[] = {AMARK_NODE_NONE, AMARK_NODE_EXTENSION, (amark_node_type_t)0x7fff};
    size_t i;

    test_allocator_install();
    for (i = 0; i < sizeof(unknown) / sizeof(unknown[0]); i++)
    {
        amark_node_t *node = test_sentinel_node();

        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_create(unknown[i], &node));
        NAOS_CHECK_PTR_EQ(test_sentinel_node(), node);
    }

    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().allocations);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(create_rejects_null)
{
    amark_node_t *node = test_sentinel_node();

    test_allocator_install();
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_create(AMARK_NODE_PARAGRAPH, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_create_with_kind(amark_node_kind_get_standard(AMARK_NODE_TEXT), NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_create_with_kind(NULL, &node));
    NAOS_CHECK_PTR_EQ(test_sentinel_node(), node);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_create_with_kind(NULL, NULL));

    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().allocations);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(queries_on_null)
{
    NAOS_CHECK_INT_EQ((int)AMARK_NODE_NONE, (int)amark_node_get_type(NULL));
    NAOS_CHECK_NULL(amark_node_get_kind(NULL));
    test_check_unlinked(NULL);
}

NAOS_TEST(unlink_root_and_null_do_nothing)
{
    amark_node_t *node = NULL;
    test_allocator_stats_t before;
    test_allocator_stats_t after;

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create(AMARK_NODE_DOCUMENT, &node));

    before = test_allocator_stats();
    amark_node_unlink(node);
    amark_node_unlink(NULL);
    after = test_allocator_stats();

    test_check_unlinked(node);
    NAOS_CHECK_INT_EQ((int)AMARK_NODE_DOCUMENT, (int)amark_node_get_type(node));
    NAOS_CHECK_UINT_EQ(before.allocations, after.allocations);
    NAOS_CHECK_UINT_EQ(before.deallocations, after.deallocations);

    amark_node_destroy(node);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(destroy_null_does_nothing)
{
    test_allocator_stats_t stats;

    test_allocator_install();
    amark_node_destroy(NULL);

    stats = test_allocator_stats();
    NAOS_CHECK_UINT_EQ(0u, stats.allocations);
    NAOS_CHECK_UINT_EQ(0u, stats.deallocations);
}

NAOS_TEST(create_failure_injection)
{
    size_t i;

    test_allocator_install();
    for (i = 0; i < TEST_STANDARD_TYPE_COUNT; i++)
    {
        amark_node_t *node = test_sentinel_node();

        test_allocator_fail_after(0);
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NO_MEMORY, amark_node_create(test_standard_types[i], &node));
        NAOS_CHECK_PTR_EQ(test_sentinel_node(), node);
        NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
    }

    test_allocator_never_fail();
}

NAOS_TEST(create_with_extension_kind_failure_injection)
{
    amark_node_registry_t *registry = NULL;
    amark_node_kind_desc_t desc = test_extension_desc("table");
    const amark_node_kind_t *kind = NULL;
    amark_node_t *node = test_sentinel_node();
    size_t live;

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &kind));
    live = test_allocator_stats().live;

    test_allocator_fail_after(0);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NO_MEMORY, amark_node_create_with_kind(kind, &node));
    NAOS_CHECK_PTR_EQ(test_sentinel_node(), node);
    NAOS_CHECK_UINT_EQ(live, test_allocator_stats().live);
    test_allocator_never_fail();

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create_with_kind(kind, &node));
    NAOS_CHECK_UINT_EQ(live + 1u, test_allocator_stats().live);
    amark_node_destroy(node);
    NAOS_CHECK_UINT_EQ(live, test_allocator_stats().live);

    amark_node_registry_destroy(registry);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

/* clang-format off */
NAOS_SUITE(node,
    NAOS_SET_TEARDOWN(restore_default),
    NAOS_ADD_TEST(create_standard_kinds),
    NAOS_ADD_TEST(create_with_standard_kind_handle),
    NAOS_ADD_TEST(create_extension_kind),
    NAOS_ADD_TEST(create_rejects_unknown_types),
    NAOS_ADD_TEST(create_rejects_null),
    NAOS_ADD_TEST(queries_on_null),
    NAOS_ADD_TEST(unlink_root_and_null_do_nothing),
    NAOS_ADD_TEST(destroy_null_does_nothing),
    NAOS_ADD_TEST(create_failure_injection),
    NAOS_ADD_TEST(create_with_extension_kind_failure_injection)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(node)
)
/* clang-format on */
