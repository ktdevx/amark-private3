/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * 編集操作（末尾追加・先頭追加・前後への挿入・置換）と、木を組む必要のある取り外し・破棄・参照のテスト。
 * 各操作の後に親・兄弟・最初と最後の子の整合を参照 5 関数で確かめ、移動（同じ親内・別の親・別の木）、
 * 引数エラー、親子規則の違反と循環が不正な子のエラーになり全ノードのリンクと値が変わらないこと、
 * 置換で中身が包みを置き換える場合、親を持つノードの取り外し・破棄後の残りの木の整合と生存ブロック数、
 * 深さ 10 万段の木を上から下へ組んで破棄できることを確かめる。
 */
#include <amark/error.h>
#include <amark/memory.h>
#include <amark/node.h>
#include <amark/node_kind.h>
#include <amark/slice.h>

#include <naos/naos.h>

#include "support/test_allocator.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* 深い木のテストの段数 */
#define TEST_DEEP_DEPTH 100000u

/* スナップショットで扱うノード数の上限 */
#define TEST_SNAPSHOT_MAX 16u

/* 1 つのノードのリンク */
typedef struct test_links
{
    const amark_node_t *node;
    const amark_node_t *parent;
    const amark_node_t *first_child;
    const amark_node_t *last_child;
    const amark_node_t *previous;
    const amark_node_t *next;
} test_links_t;

/* 複数のノードのリンクの記録 */
typedef struct test_snapshot
{
    test_links_t links[TEST_SNAPSHOT_MAX];
    size_t count;
} test_snapshot_t;

/* 標準種別のノードを生成する */
static amark_node_t *test_node(amark_node_type_t type)
{
    amark_node_t *node = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create(type, &node));
    NAOS_REQUIRE_NOT_NULL(node);

    return node;
}

/* 種別ハンドルからノードを生成する */
static amark_node_t *test_node_with_kind(const amark_node_kind_t *kind)
{
    amark_node_t *node = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create_with_kind(kind, &node));
    NAOS_REQUIRE_NOT_NULL(node);

    return node;
}

/* 内容 value の text ノードを生成する */
static amark_node_t *test_text(const char *value)
{
    amark_node_t *node = test_node(AMARK_NODE_TEXT);

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_set_literal(node, amark_slice_from_cstr(value)));

    return node;
}

/* parent の子が children[0..count) のこの順であり、リンクと参照の結果が整合することを確かめる */
static void test_check_children(const amark_node_t *parent, amark_node_t *const *children, size_t count)
{
    const amark_node_t *walk;
    size_t i;

    if (count == 0)
    {
        NAOS_CHECK_NULL(amark_node_get_first_child(parent));
        NAOS_CHECK_NULL(amark_node_get_last_child(parent));
        return;
    }

    NAOS_CHECK_PTR_EQ(children[0], amark_node_get_first_child(parent));
    NAOS_CHECK_PTR_EQ(children[count - 1], amark_node_get_last_child(parent));
    for (i = 0; i < count; i++)
    {
        NAOS_CHECK_PTR_EQ(parent, amark_node_get_parent(children[i]));
        if (i == 0)
        {
            NAOS_CHECK_NULL(amark_node_get_previous(children[i]));
        }
        else
        {
            NAOS_CHECK_PTR_EQ(children[i - 1], amark_node_get_previous(children[i]));
        }

        if ((i + 1) == count)
        {
            NAOS_CHECK_NULL(amark_node_get_next(children[i]));
        }
        else
        {
            NAOS_CHECK_PTR_EQ(children[i + 1], amark_node_get_next(children[i]));
        }
    }

    /* 次の兄弟と前の兄弟のたどりがちょうど count 個で終わる */
    walk = amark_node_get_first_child(parent);
    for (i = 0; (i < count) && (walk != NULL); i++)
    {
        walk = amark_node_get_next(walk);
    }

    NAOS_CHECK_UINT_EQ(count, i);
    NAOS_CHECK_NULL(walk);

    walk = amark_node_get_last_child(parent);
    for (i = 0; (i < count) && (walk != NULL); i++)
    {
        walk = amark_node_get_previous(walk);
    }

    NAOS_CHECK_UINT_EQ(count, i);
    NAOS_CHECK_NULL(walk);
}

/* node が根（親と兄弟を持たない）であることを確かめる */
static void test_check_root(const amark_node_t *node)
{
    NAOS_CHECK_NULL(amark_node_get_parent(node));
    NAOS_CHECK_NULL(amark_node_get_previous(node));
    NAOS_CHECK_NULL(amark_node_get_next(node));
}

/* nodes[0..count) のリンクを記録する */
static void test_take_snapshot(test_snapshot_t *snapshot, amark_node_t *const *nodes, size_t count)
{
    size_t i;

    NAOS_REQUIRE_UINT_LE(count, TEST_SNAPSHOT_MAX);
    snapshot->count = count;
    for (i = 0; i < count; i++)
    {
        snapshot->links[i].node = nodes[i];
        snapshot->links[i].parent = amark_node_get_parent(nodes[i]);
        snapshot->links[i].first_child = amark_node_get_first_child(nodes[i]);
        snapshot->links[i].last_child = amark_node_get_last_child(nodes[i]);
        snapshot->links[i].previous = amark_node_get_previous(nodes[i]);
        snapshot->links[i].next = amark_node_get_next(nodes[i]);
    }
}

/* 記録したリンクが変わっていないことを確かめる */
static void test_check_snapshot(const test_snapshot_t *snapshot)
{
    size_t i;

    for (i = 0; i < snapshot->count; i++)
    {
        const test_links_t *links = &snapshot->links[i];

        NAOS_CHECK_PTR_EQ(links->parent, amark_node_get_parent(links->node));
        NAOS_CHECK_PTR_EQ(links->first_child, amark_node_get_first_child(links->node));
        NAOS_CHECK_PTR_EQ(links->last_child, amark_node_get_last_child(links->node));
        NAOS_CHECK_PTR_EQ(links->previous, amark_node_get_previous(links->node));
        NAOS_CHECK_PTR_EQ(links->next, amark_node_get_next(links->node));
    }
}

/* text ノードの内容が expected であることを確かめる */
static void test_check_literal(const amark_node_t *node, const char *expected)
{
    amark_slice_t value = amark_node_get_literal(node);

    NAOS_CHECK_MEM_EQ(expected, strlen(expected), value.data, value.len);
}

/* 独自判定の親: 拡張の明示親限定ブロック "row" と標準の item だけを受け入れる */
static bool test_table_can_contain(const amark_node_kind_t *parent, const amark_node_kind_t *child, void *user_data)
{
    amark_slice_t name = amark_node_kind_get_name(child);

    (void)parent;
    (void)user_data;

    if (amark_node_kind_get_type(child) == AMARK_NODE_ITEM)
    {
        return true;
    }

    return (amark_node_kind_get_type(child) == AMARK_NODE_EXTENSION) && (name.len == 3) &&
           (memcmp(name.data, "row", 3) == 0);
}

/* 拡張種別を登録する */
static const amark_node_kind_t *test_add_kind(amark_node_registry_t *registry, const char *name,
                                              amark_node_content_t content, bool requires_explicit_parent,
                                              amark_node_can_contain_fn_t can_contain)
{
    amark_node_kind_desc_t desc;
    const amark_node_kind_t *kind = NULL;

    memset(&desc, 0, sizeof(desc));
    desc.name = amark_slice_from_cstr(name);
    desc.category = AMARK_NODE_CATEGORY_BLOCK;
    desc.content = content;
    desc.requires_explicit_parent = requires_explicit_parent;
    desc.can_contain = can_contain;
    desc.destroy_data = NULL;
    desc.user_data = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &kind));
    NAOS_REQUIRE_NOT_NULL(kind);

    return kind;
}

NAOS_TEARDOWN(restore_default)
{
    test_allocator_uninstall();
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_allocator_set(NULL));
}

NAOS_TEST(append_child_keeps_order)
{
    amark_node_t *doc;
    amark_node_t *p[3];
    size_t i;

    test_allocator_install();
    doc = test_node(AMARK_NODE_DOCUMENT);
    for (i = 0; i < 3; i++)
    {
        p[i] = test_node(AMARK_NODE_PARAGRAPH);
    }

    test_check_children(doc, p, 0);
    for (i = 0; i < 3; i++)
    {
        NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_append_child(doc, p[i]));
        test_check_children(doc, p, i + 1);
    }

    test_check_root(doc);
    amark_node_destroy(doc);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(prepend_child_keeps_order)
{
    amark_node_t *doc;
    amark_node_t *p[3];
    amark_node_t *expected[3];

    test_allocator_install();
    doc = test_node(AMARK_NODE_DOCUMENT);
    p[0] = test_node(AMARK_NODE_PARAGRAPH);
    p[1] = test_node(AMARK_NODE_PARAGRAPH);
    p[2] = test_node(AMARK_NODE_PARAGRAPH);

    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_prepend_child(doc, p[0]));
    test_check_children(doc, p, 1);

    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_prepend_child(doc, p[1]));
    expected[0] = p[1];
    expected[1] = p[0];
    test_check_children(doc, expected, 2);

    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_prepend_child(doc, p[2]));
    expected[0] = p[2];
    expected[1] = p[1];
    expected[2] = p[0];
    test_check_children(doc, expected, 3);

    amark_node_destroy(doc);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(insert_before_and_after_each_position)
{
    amark_node_t *para;
    amark_node_t *b;
    amark_node_t *n[6];
    amark_node_t *expected[6];
    size_t i;

    test_allocator_install();
    para = test_node(AMARK_NODE_PARAGRAPH);
    b = test_text("b");
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(para, b));
    for (i = 0; i < 6; i++)
    {
        n[i] = test_node(AMARK_NODE_SOFT_BREAK);
    }

    /* 唯一の子の前と後 */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_insert_before(b, n[0]));
    expected[0] = n[0];
    expected[1] = b;
    test_check_children(para, expected, 2);

    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_insert_after(b, n[1]));
    expected[2] = n[1];
    test_check_children(para, expected, 3);

    /* 先頭の子の前（新しい先頭）と末尾の子の後（新しい末尾） */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_insert_before(n[0], n[2]));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_insert_after(n[1], n[3]));
    expected[0] = n[2];
    expected[1] = n[0];
    expected[2] = b;
    expected[3] = n[1];
    expected[4] = n[3];
    test_check_children(para, expected, 5);

    /* 中間の子の前と後 */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_insert_before(b, n[4]));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_insert_after(b, n[5]));
    {
        amark_node_t *all[7];

        all[0] = n[2];
        all[1] = n[0];
        all[2] = n[4];
        all[3] = b;
        all[4] = n[5];
        all[5] = n[1];
        all[6] = n[3];
        test_check_children(para, all, 7);
    }

    amark_node_destroy(para);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(replace_each_position)
{
    amark_node_t *doc;
    amark_node_t *c[3];
    amark_node_t *r[3];
    amark_node_t *inner;
    size_t i;

    test_allocator_install();
    doc = test_node(AMARK_NODE_DOCUMENT);
    for (i = 0; i < 3; i++)
    {
        c[i] = test_node(AMARK_NODE_BLOCK_QUOTE);
        r[i] = test_node(AMARK_NODE_THEMATIC_BREAK);
        NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(doc, c[i]));
    }

    inner = test_node(AMARK_NODE_PARAGRAPH);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(c[1], inner));

    /* 中間・先頭・末尾の順に置き換える。置き換えられたノードは子孫ごと根になる */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_replace(c[1], r[1]));
    test_check_root(c[1]);
    test_check_children(c[1], &inner, 1);
    {
        amark_node_t *expected[3];

        expected[0] = c[0];
        expected[1] = r[1];
        expected[2] = c[2];
        test_check_children(doc, expected, 3);
    }

    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_replace(c[0], r[0]));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_replace(c[2], r[2]));
    test_check_root(c[0]);
    test_check_root(c[2]);
    test_check_children(doc, r, 3);

    for (i = 0; i < 3; i++)
    {
        amark_node_destroy(c[i]);
    }

    amark_node_destroy(doc);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(replace_only_child)
{
    amark_node_t *para;
    amark_node_t *a;
    amark_node_t *b;

    test_allocator_install();
    para = test_node(AMARK_NODE_PARAGRAPH);
    a = test_text("a");
    b = test_text("b");
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(para, a));

    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_replace(a, b));
    test_check_children(para, &b, 1);
    test_check_root(a);
    test_check_literal(a, "a");
    test_check_literal(b, "b");

    amark_node_destroy(a);
    amark_node_destroy(para);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

/* 中身で包みを置き換える: replacement が target の子孫 */
NAOS_TEST(replace_wrapper_with_descendant)
{
    amark_node_t *doc;
    amark_node_t *before;
    amark_node_t *after;
    amark_node_t *quote;
    amark_node_t *p1;
    amark_node_t *p2;
    amark_node_t *inner_quote;
    amark_node_t *deep;
    amark_node_t *expected[3];

    test_allocator_install();
    doc = test_node(AMARK_NODE_DOCUMENT);
    before = test_node(AMARK_NODE_THEMATIC_BREAK);
    after = test_node(AMARK_NODE_THEMATIC_BREAK);
    quote = test_node(AMARK_NODE_BLOCK_QUOTE);
    p1 = test_node(AMARK_NODE_PARAGRAPH);
    p2 = test_node(AMARK_NODE_PARAGRAPH);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(doc, before));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(doc, quote));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(doc, after));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(quote, p1));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(quote, p2));

    /* 直接の子で置き換える: 残りの子は取り外された包みに残る */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_replace(quote, p1));
    expected[0] = before;
    expected[1] = p1;
    expected[2] = after;
    test_check_children(doc, expected, 3);
    test_check_root(quote);
    test_check_children(quote, &p2, 1);

    /* 孫（子を持つ）で置き換える: doc > quote > inner_quote > deep を doc > inner_quote > deep にする */
    inner_quote = test_node(AMARK_NODE_BLOCK_QUOTE);
    deep = test_node(AMARK_NODE_PARAGRAPH);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_replace(p1, quote));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(quote, inner_quote));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(inner_quote, deep));
    test_check_root(p1);

    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_replace(quote, inner_quote));
    expected[1] = inner_quote;
    test_check_children(doc, expected, 3);
    test_check_children(inner_quote, &deep, 1);
    test_check_root(quote);
    test_check_children(quote, &p2, 1);

    amark_node_destroy(p1);
    amark_node_destroy(quote);
    amark_node_destroy(doc);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

/* 置換するノードが対象の兄弟、別の親の子、別の木にある場合 */
NAOS_TEST(replace_moves_replacement)
{
    amark_node_t *doc;
    amark_node_t *other;
    amark_node_t *a;
    amark_node_t *b;
    amark_node_t *c;
    amark_node_t *x;
    amark_node_t *y;
    amark_node_t *expected[2];

    test_allocator_install();
    doc = test_node(AMARK_NODE_DOCUMENT);
    other = test_node(AMARK_NODE_BLOCK_QUOTE);
    a = test_node(AMARK_NODE_PARAGRAPH);
    b = test_node(AMARK_NODE_PARAGRAPH);
    c = test_node(AMARK_NODE_PARAGRAPH);
    x = test_node(AMARK_NODE_PARAGRAPH);
    y = test_node(AMARK_NODE_PARAGRAPH);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(doc, a));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(doc, b));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(doc, c));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(other, x));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(other, y));

    /* 隣の兄弟で置き換える */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_replace(b, c));
    expected[0] = a;
    expected[1] = c;
    test_check_children(doc, expected, 2);
    test_check_root(b);

    /* 離れた兄弟で置き換える（先頭を末尾で） */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_replace(a, c));
    test_check_children(doc, &c, 1);
    test_check_root(a);

    /* 別の木の子で置き換える: 元の木から取り外される */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_replace(c, x));
    test_check_children(doc, &x, 1);
    test_check_children(other, &y, 1);
    test_check_root(c);

    amark_node_destroy(a);
    amark_node_destroy(b);
    amark_node_destroy(c);
    amark_node_destroy(other);
    amark_node_destroy(doc);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(move_within_same_parent)
{
    amark_node_t *doc;
    amark_node_t *n[3];
    amark_node_t *expected[3];
    size_t i;

    test_allocator_install();
    doc = test_node(AMARK_NODE_DOCUMENT);
    for (i = 0; i < 3; i++)
    {
        n[i] = test_node(AMARK_NODE_PARAGRAPH);
        NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(doc, n[i]));
    }

    /* 先頭を末尾へ: 1 2 0 */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_append_child(doc, n[0]));
    expected[0] = n[1];
    expected[1] = n[2];
    expected[2] = n[0];
    test_check_children(doc, expected, 3);

    /* 末尾をさらに末尾へ（位置は変わらない） */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_append_child(doc, n[0]));
    test_check_children(doc, expected, 3);

    /* 末尾を先頭へ: 0 1 2 */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_prepend_child(doc, n[0]));
    test_check_children(doc, n, 3);

    /* 先頭をさらに先頭へ（位置は変わらない） */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_prepend_child(doc, n[0]));
    test_check_children(doc, n, 3);

    /* 直前の兄弟を前へ、直後の兄弟を後へ挿入する（位置は変わらない） */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_insert_before(n[1], n[0]));
    test_check_children(doc, n, 3);
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_insert_after(n[1], n[2]));
    test_check_children(doc, n, 3);

    /* 先頭を末尾の後へ: 1 2 0、末尾を先頭の前へ: 0 1 2 */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_insert_after(n[2], n[0]));
    test_check_children(doc, expected, 3);
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_insert_before(n[1], n[0]));
    test_check_children(doc, n, 3);

    /* 隣り合う 2 つを入れ替える: 1 0 2 */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_insert_after(n[1], n[0]));
    expected[0] = n[1];
    expected[1] = n[0];
    expected[2] = n[2];
    test_check_children(doc, expected, 3);

    amark_node_destroy(doc);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(move_between_parents_and_trees)
{
    amark_node_t *doc;
    amark_node_t *quote;
    amark_node_t *other;
    amark_node_t *p;
    amark_node_t *q;
    amark_node_t *text;
    amark_node_t *expected[2];

    test_allocator_install();
    doc = test_node(AMARK_NODE_DOCUMENT);
    quote = test_node(AMARK_NODE_BLOCK_QUOTE);
    other = test_node(AMARK_NODE_DOCUMENT);
    p = test_node(AMARK_NODE_PARAGRAPH);
    q = test_node(AMARK_NODE_PARAGRAPH);
    text = test_text("t");
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(doc, quote));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(doc, p));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(p, text));

    /* 別の親へ（子孫ごと移る） */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_append_child(quote, p));
    test_check_children(doc, &quote, 1);
    test_check_children(quote, &p, 1);
    test_check_children(p, &text, 1);

    /* 別の木へ: 元の親は子を失う */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_prepend_child(other, p));
    test_check_children(quote, NULL, 0);
    test_check_children(other, &p, 1);

    /* 兄弟挿入による別の木への移動 */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_append_child(other, q));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_insert_after(quote, q));
    test_check_children(other, &p, 1);
    expected[0] = quote;
    expected[1] = q;
    test_check_children(doc, expected, 2);

    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_insert_before(quote, p));
    {
        amark_node_t *all[3];

        all[0] = p;
        all[1] = quote;
        all[2] = q;
        test_check_children(doc, all, 3);
    }

    test_check_children(other, NULL, 0);
    test_check_children(p, &text, 1);
    test_check_literal(text, "t");

    amark_node_destroy(other);
    amark_node_destroy(doc);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(argument_errors)
{
    amark_node_t *doc;
    amark_node_t *p;
    amark_node_t *t;
    amark_node_t *loose;
    amark_node_t *nodes[4];
    test_snapshot_t snapshot;

    test_allocator_install();
    doc = test_node(AMARK_NODE_DOCUMENT);
    p = test_node(AMARK_NODE_PARAGRAPH);
    t = test_text("x");
    loose = test_node(AMARK_NODE_PARAGRAPH);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(doc, p));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(p, t));
    nodes[0] = doc;
    nodes[1] = p;
    nodes[2] = t;
    nodes[3] = loose;
    test_take_snapshot(&snapshot, nodes, 4);

    /* 空ポインタ */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(NULL, loose));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(doc, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(NULL, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_prepend_child(NULL, loose));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_prepend_child(doc, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_before(NULL, loose));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_before(p, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_after(NULL, loose));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_after(p, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_replace(NULL, loose));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_replace(p, NULL));

    /* 動かすノードと基準のノードが同一（親の有無や子の有無によらず手順 1 で拒否する） */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(loose, loose));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_append_child(p, p));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_prepend_child(p, p));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_before(p, p));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_after(p, p));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_replace(p, p));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_before(doc, doc));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_replace(doc, doc));

    /* 根を基準にした兄弟挿入・置換（新しい親がない） */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_before(doc, loose));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_after(doc, loose));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_replace(doc, loose));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_before(loose, p));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_after(loose, t));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_replace(loose, p));

    /* 根を基準にした挿入は、動かすノードが自分の祖先でも手順 2 の引数エラーになる */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_insert_before(doc, p));

    test_check_snapshot(&snapshot);
    test_check_literal(t, "x");

    amark_node_destroy(loose);
    amark_node_destroy(doc);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(rule_violations_change_nothing)
{
    amark_node_t *doc;
    amark_node_t *quote;
    amark_node_t *list;
    amark_node_t *item;
    amark_node_t *para;
    amark_node_t *text;
    amark_node_t *loose_para;
    amark_node_t *loose_doc;
    amark_node_t *loose_item;
    amark_node_t *loose_text;
    amark_node_t *nodes[10];
    test_snapshot_t snapshot;

    test_allocator_install();
    doc = test_node(AMARK_NODE_DOCUMENT);
    quote = test_node(AMARK_NODE_BLOCK_QUOTE);
    list = test_node(AMARK_NODE_LIST);
    item = test_node(AMARK_NODE_ITEM);
    para = test_node(AMARK_NODE_PARAGRAPH);
    text = test_text("abc");
    loose_para = test_node(AMARK_NODE_PARAGRAPH);
    loose_doc = test_node(AMARK_NODE_DOCUMENT);
    loose_item = test_node(AMARK_NODE_ITEM);
    loose_text = test_text("def");
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(doc, quote));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(doc, list));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(list, item));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(item, para));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(para, text));
    nodes[0] = doc;
    nodes[1] = quote;
    nodes[2] = list;
    nodes[3] = item;
    nodes[4] = para;
    nodes[5] = text;
    nodes[6] = loose_para;
    nodes[7] = loose_doc;
    nodes[8] = loose_item;
    nodes[9] = loose_text;
    test_take_snapshot(&snapshot, nodes, 10);

    /* paragraph に paragraph（インラインの親にブロック） */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_append_child(para, loose_para));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_insert_after(text, loose_para));

    /* list に paragraph（list は item だけ） */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_append_child(list, loose_para));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_prepend_child(list, loose_para));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_insert_before(item, loose_para));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_replace(item, loose_para));

    /* document を子に */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_append_child(doc, loose_doc));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_append_child(quote, loose_doc));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_insert_after(quote, loose_doc));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_replace(quote, loose_doc));

    /* item を block quote・document の下に（明示親限定） */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_append_child(quote, loose_item));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_insert_before(quote, loose_item));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_replace(quote, loose_item));

    /* ブロックの親にインライン、葉の親 */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_append_child(doc, loose_text));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_append_child(text, loose_text));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_insert_before(quote, loose_text));

    /* 既に親を持つノードの移動の失敗: 元の位置に残る */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_append_child(quote, item));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_append_child(doc, text));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_insert_after(quote, text));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_replace(quote, item));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_replace(text, quote));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_insert_before(item, quote));

    test_check_snapshot(&snapshot);
    test_check_literal(text, "abc");
    test_check_literal(loose_text, "def");

    amark_node_destroy(loose_para);
    amark_node_destroy(loose_doc);
    amark_node_destroy(loose_item);
    amark_node_destroy(loose_text);
    amark_node_destroy(doc);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(extension_rules)
{
    amark_node_registry_t *registry = NULL;
    const amark_node_kind_t *table_kind;
    const amark_node_kind_t *row_kind;
    const amark_node_kind_t *box_kind;
    amark_node_t *doc;
    amark_node_t *table;
    amark_node_t *row;
    amark_node_t *item;
    amark_node_t *para;
    amark_node_t *box;
    amark_node_t *nodes[6];
    amark_node_t *expected[2];
    test_snapshot_t snapshot;

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));
    table_kind = test_add_kind(registry, "table", AMARK_NODE_CONTENT_CUSTOM, false, test_table_can_contain);
    row_kind = test_add_kind(registry, "row", AMARK_NODE_CONTENT_INLINES, true, NULL);
    box_kind = test_add_kind(registry, "box", AMARK_NODE_CONTENT_NONE, false, NULL);

    doc = test_node(AMARK_NODE_DOCUMENT);
    table = test_node_with_kind(table_kind);
    row = test_node_with_kind(row_kind);
    item = test_node(AMARK_NODE_ITEM);
    para = test_node(AMARK_NODE_PARAGRAPH);
    box = test_node_with_kind(box_kind);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(doc, table));
    nodes[0] = doc;
    nodes[1] = table;
    nodes[2] = row;
    nodes[3] = item;
    nodes[4] = para;
    nodes[5] = box;
    test_take_snapshot(&snapshot, nodes, 6);

    /* 明示親限定の拡張は任意のブロックの親の下に置けない。独自判定が拒否する種別、子なしの拡張の下も不可 */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_append_child(doc, row));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_insert_after(table, row));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_append_child(table, para));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_append_child(box, para));
    test_check_snapshot(&snapshot);

    /* 独自判定が受け入れる種別（明示親限定の拡張と item）は置ける */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_append_child(table, row));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_append_child(table, item));
    expected[0] = row;
    expected[1] = item;
    test_check_children(table, expected, 2);

    /* 子なしの拡張ブロックは document の下に置ける */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_insert_after(table, box));
    expected[0] = table;
    expected[1] = box;
    test_check_children(doc, expected, 2);

    amark_node_destroy(para);
    amark_node_destroy(doc);
    amark_node_registry_destroy(registry);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(cycles_change_nothing)
{
    amark_node_t *doc;
    amark_node_t *q1;
    amark_node_t *q2;
    amark_node_t *q3;
    amark_node_t *p;
    amark_node_t *nodes[5];
    test_snapshot_t snapshot;

    test_allocator_install();
    doc = test_node(AMARK_NODE_DOCUMENT);
    q1 = test_node(AMARK_NODE_BLOCK_QUOTE);
    q2 = test_node(AMARK_NODE_BLOCK_QUOTE);
    q3 = test_node(AMARK_NODE_BLOCK_QUOTE);
    p = test_node(AMARK_NODE_PARAGRAPH);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(doc, q1));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(q1, q2));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(q2, q3));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(q3, p));
    nodes[0] = doc;
    nodes[1] = q1;
    nodes[2] = q2;
    nodes[3] = q3;
    nodes[4] = p;
    test_take_snapshot(&snapshot, nodes, 5);

    /* 祖先を子孫の子に（直接の子・孫・ひ孫） */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_append_child(q2, q1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_append_child(q3, q1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_prepend_child(q3, q2));

    /* 兄弟挿入・置換で新しい親が動かすノード自身または子孫になる */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_insert_before(q2, q1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_insert_after(q3, q1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_insert_before(p, q2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_replace(q3, q2));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_replace(p, q1));

    /* 循環で、かつ受け入れ判定でも拒否される組（document は子にできない） */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_append_child(q3, doc));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_insert_after(q1, doc));

    test_check_snapshot(&snapshot);

    /* 子孫を祖先の子へ移すのは循環ではない */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_append_child(doc, q3));
    {
        amark_node_t *expected[2];

        expected[0] = q1;
        expected[1] = q3;
        test_check_children(doc, expected, 2);
        test_check_children(q2, NULL, 0);
        test_check_children(q3, &p, 1);
    }

    amark_node_destroy(doc);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(unlink_each_position)
{
    amark_node_t *doc;
    amark_node_t *n[4];
    amark_node_t *inner;
    amark_node_t *expected[3];
    size_t i;

    test_allocator_install();
    doc = test_node(AMARK_NODE_DOCUMENT);
    for (i = 0; i < 4; i++)
    {
        n[i] = test_node(AMARK_NODE_BLOCK_QUOTE);
        NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(doc, n[i]));
    }

    inner = test_node(AMARK_NODE_PARAGRAPH);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(n[1], inner));

    /* 中間の子: 子孫ごと根になり、前後がつなぎ直される */
    amark_node_unlink(n[1]);
    test_check_root(n[1]);
    test_check_children(n[1], &inner, 1);
    expected[0] = n[0];
    expected[1] = n[2];
    expected[2] = n[3];
    test_check_children(doc, expected, 3);

    /* 先頭の子 */
    amark_node_unlink(n[0]);
    test_check_root(n[0]);
    test_check_children(doc, &n[2], 2);

    /* 末尾の子 */
    amark_node_unlink(n[3]);
    test_check_root(n[3]);
    test_check_children(doc, &n[2], 1);

    /* 唯一の子 */
    amark_node_unlink(n[2]);
    test_check_root(n[2]);
    test_check_children(doc, NULL, 0);

    /* 取り外したノードは呼び出し側が破棄する。2 回目の取り外しは何もしない */
    amark_node_unlink(n[1]);
    test_check_children(n[1], &inner, 1);
    for (i = 0; i < 4; i++)
    {
        amark_node_destroy(n[i]);
    }

    amark_node_destroy(doc);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(destroy_parented_node_keeps_tree)
{
    amark_node_t *doc;
    amark_node_t *n[3];
    amark_node_t *para;
    amark_node_t *text;
    amark_node_t *emphasis;
    amark_node_t *inner_text;
    size_t live;

    test_allocator_install();
    doc = test_node(AMARK_NODE_DOCUMENT);
    n[0] = test_node(AMARK_NODE_THEMATIC_BREAK);
    n[1] = test_node(AMARK_NODE_PARAGRAPH);
    n[2] = test_node(AMARK_NODE_THEMATIC_BREAK);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(doc, n[0]));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(doc, n[1]));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(doc, n[2]));
    para = n[1];
    text = test_text("before");
    emphasis = test_node(AMARK_NODE_EMPHASIS);
    inner_text = test_text("inner");
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(para, text));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(para, emphasis));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(emphasis, inner_text));

    /* 値を持つ子孫ごと中間の子を破棄する: 4 ノードと 2 文字列が解放される */
    live = test_allocator_stats().live;
    amark_node_destroy(para);
    NAOS_CHECK_UINT_EQ(live - 6u, test_allocator_stats().live);
    {
        amark_node_t *expected[2];

        expected[0] = n[0];
        expected[1] = n[2];
        test_check_children(doc, expected, 2);
    }

    /* 先頭の子、末尾の子 */
    amark_node_destroy(n[0]);
    test_check_children(doc, &n[2], 1);
    amark_node_destroy(n[2]);
    test_check_children(doc, NULL, 0);
    NAOS_CHECK_UINT_EQ(1u, test_allocator_stats().live);

    amark_node_destroy(doc);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

/* 深さ 10 万段の木を上から下へ（最後に作ったノードへ末尾追加して）組み、破棄する */
NAOS_TEST(deep_tree_built_top_down_and_destroyed)
{
    amark_node_t *doc;
    amark_node_t *current;
    amark_node_t *middle = NULL;
    amark_node_t *walk;
    size_t depth;
    size_t i;

    test_allocator_install();
    doc = test_node(AMARK_NODE_DOCUMENT);
    current = doc;
    for (i = 0; i < TEST_DEEP_DEPTH; i++)
    {
        amark_node_t *quote = NULL;
        amark_error_t error = amark_node_create(AMARK_NODE_BLOCK_QUOTE, &quote);

        if (error == AMARK_OK)
        {
            error = amark_node_append_child(current, quote);
        }

        if (error != AMARK_OK)
        {
            amark_node_destroy(quote);
            NAOS_REQUIRE_INT_EQ(AMARK_OK, error);
        }

        if (i == (TEST_DEEP_DEPTH / 2))
        {
            middle = quote;
        }

        current = quote;
    }

    NAOS_CHECK_UINT_EQ(TEST_DEEP_DEPTH + 1u, test_allocator_stats().live);

    /* 最深部から根まで親をたどると段数どおり */
    depth = 0;
    for (walk = current; amark_node_get_parent(walk) != NULL; walk = amark_node_get_parent(walk))
    {
        depth++;
    }

    NAOS_CHECK_PTR_EQ(doc, walk);
    NAOS_CHECK_UINT_EQ(TEST_DEEP_DEPTH, depth);

    /* 中間の段を部分木ごと破棄すると、親は子を失い、残りの段だけが生存する */
    NAOS_REQUIRE_NOT_NULL(middle);
    walk = amark_node_get_parent(middle);
    amark_node_destroy(middle);
    test_check_children(walk, NULL, 0);
    NAOS_CHECK_UINT_EQ((TEST_DEEP_DEPTH / 2) + 1u, test_allocator_stats().live);

    amark_node_destroy(doc);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

/* 深い位置への挿入で、子を持つノードの循環検査が祖先を根までたどっても完了する */
NAOS_TEST(deep_tree_cycle_check)
{
    amark_node_t *doc;
    amark_node_t *top;
    amark_node_t *current;
    amark_node_t *loose;
    amark_node_t *leaf;
    size_t i;

    test_allocator_install();
    doc = test_node(AMARK_NODE_DOCUMENT);
    top = test_node(AMARK_NODE_BLOCK_QUOTE);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(doc, top));
    current = top;
    for (i = 0; i < TEST_DEEP_DEPTH; i++)
    {
        amark_node_t *quote = NULL;
        amark_error_t error = amark_node_create(AMARK_NODE_BLOCK_QUOTE, &quote);

        if (error == AMARK_OK)
        {
            error = amark_node_append_child(current, quote);
        }

        if (error != AMARK_OK)
        {
            amark_node_destroy(quote);
            NAOS_REQUIRE_INT_EQ(AMARK_OK, error);
        }

        current = quote;
    }

    /* 根に近い祖先を最深部へ: 循環 */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_append_child(current, top));
    NAOS_CHECK_PTR_EQ(doc, amark_node_get_parent(top));
    NAOS_CHECK_NULL(amark_node_get_first_child(current));

    /* 子を持つ別の木のノードを最深部へ: 祖先を根までたどってから成功する */
    loose = test_node(AMARK_NODE_BLOCK_QUOTE);
    leaf = test_node(AMARK_NODE_PARAGRAPH);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(loose, leaf));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_append_child(current, loose));
    test_check_children(current, &loose, 1);

    amark_node_destroy(doc);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

/* clang-format off */
NAOS_SUITE(node_edit,
    NAOS_SET_TEARDOWN(restore_default),
    NAOS_ADD_TEST(append_child_keeps_order),
    NAOS_ADD_TEST(prepend_child_keeps_order),
    NAOS_ADD_TEST(insert_before_and_after_each_position),
    NAOS_ADD_TEST(replace_each_position),
    NAOS_ADD_TEST(replace_only_child),
    NAOS_ADD_TEST(replace_wrapper_with_descendant),
    NAOS_ADD_TEST(replace_moves_replacement),
    NAOS_ADD_TEST(move_within_same_parent),
    NAOS_ADD_TEST(move_between_parents_and_trees),
    NAOS_ADD_TEST(argument_errors),
    NAOS_ADD_TEST(rule_violations_change_nothing),
    NAOS_ADD_TEST(extension_rules),
    NAOS_ADD_TEST(cycles_change_nothing),
    NAOS_ADD_TEST(unlink_each_position),
    NAOS_ADD_TEST(destroy_parented_node_keeps_tree),
    NAOS_ADD_TEST(deep_tree_built_top_down_and_destroyed),
    NAOS_ADD_TEST(deep_tree_cycle_check)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(node_edit)
)
/* clang-format on */
