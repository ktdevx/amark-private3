/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * enter / exit イテレータのテスト。代表的な文書木のイベント列の完全一致、空のコンテナと葉（標準・拡張）、
 * 起点が葉・空ポインタの場合、部分木の外を報告しないこと、終了の継続、位置の再設定の正常系とエラー、
 * 走査中の確保回数 0、走査中の安全な編集（値の変更、未報告の兄弟の削除、ENTER 時の子の追加、後ろへの
 * 兄弟の挿入、EXIT 時の子の編集）の結果、現在ノード取り外し後の終了と再設定による再開、ヘッダに記した
 * 現在ノードを破棄して続ける手順、深さ 10 万段の走査を確かめる。公開 API だけで文書木（見出し、強調・リンク・
 * コードスパンを含む段落、2 項目のリスト、コードブロック、独自判定で行だけを受け入れる拡張の表）を組み、
 * 走査・特定種別の置換・正規化・破棄までを通すシナリオも確かめる。
 */
#include <amark/error.h>
#include <amark/memory.h>
#include <amark/node.h>
#include <amark/node_iter.h>
#include <amark/node_kind.h>
#include <amark/slice.h>

#include <naos/naos.h>

#include "support/test_allocator.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* 深い木のテストの段数 */
#define TEST_DEEP_DEPTH 100000u

/* 期待する 1 イベント */
typedef struct test_step
{
    const amark_node_t *node;
    amark_node_iter_event_t event;
} test_step_t;

/* 標準種別のノードを生成する */
static amark_node_t *test_node(amark_node_type_t type)
{
    amark_node_t *node = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create(type, &node));
    NAOS_REQUIRE_NOT_NULL(node);

    return node;
}

/* 標準種別のノードを生成して parent の末尾の子にする */
static amark_node_t *test_child(amark_node_t *parent, amark_node_type_t type)
{
    amark_node_t *node = test_node(type);

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(parent, node));

    return node;
}

/* 種別ハンドルからノードを生成して parent の末尾の子にする */
static amark_node_t *test_child_with_kind(amark_node_t *parent, const amark_node_kind_t *kind)
{
    amark_node_t *node = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create_with_kind(kind, &node));
    NAOS_REQUIRE_NOT_NULL(node);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(parent, node));

    return node;
}

/* イテレータの現在位置が (node, event) であることを確かめる */
static void test_check_position(const amark_node_iter_t *iter, const amark_node_t *node, amark_node_iter_event_t event)
{
    NAOS_CHECK_PTR_EQ(node, amark_node_iter_get_node(iter));
    NAOS_CHECK_INT_EQ(event, amark_node_iter_get_event(iter));
}

/* iter から count 個のイベントが expected どおりに報告されることを確かめる */
static void test_check_steps(amark_node_iter_t *iter, const test_step_t *expected, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
    {
        amark_node_iter_event_t event = amark_node_iter_next(iter);

        NAOS_CHECK_INT_EQ(expected[i].event, event);
        NAOS_CHECK_PTR_EQ(expected[i].node, amark_node_iter_get_node(iter));
        NAOS_CHECK_INT_EQ(expected[i].event, amark_node_iter_get_event(iter));
        if ((event != expected[i].event) || (amark_node_iter_get_node(iter) != expected[i].node))
        {
            return;
        }
    }
}

/* iter が終了を報告し、以後も報告し続けることを確かめる */
static void test_check_done(amark_node_iter_t *iter)
{
    size_t i;

    for (i = 0; i < 3u; i++)
    {
        NAOS_CHECK_INT_EQ(AMARK_NODE_ITER_DONE, amark_node_iter_next(iter));
        test_check_position(iter, NULL, AMARK_NODE_ITER_DONE);
    }
}

/* root から走査し、count 個のイベントの後に終了することを確かめる */
static void test_check_walk(amark_node_t *root, const test_step_t *expected, size_t count)
{
    amark_node_iter_t iter;

    amark_node_iter_init(&iter, root);
    test_check_position(&iter, NULL, AMARK_NODE_ITER_NONE);
    test_check_steps(&iter, expected, count);
    test_check_done(&iter);
}

/* 拡張種別を登録する */
static const amark_node_kind_t *test_add_kind(amark_node_registry_t *registry, const char *name,
                                              amark_node_content_t content)
{
    amark_node_kind_desc_t desc;
    const amark_node_kind_t *kind = NULL;

    memset(&desc, 0, sizeof(desc));
    desc.name = amark_slice_from_cstr(name);
    desc.category = AMARK_NODE_CATEGORY_BLOCK;
    desc.content = content;
    desc.requires_explicit_parent = false;
    desc.can_contain = NULL;
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

NAOS_TEST(representative_tree_sequence)
{
    amark_node_t *doc;
    amark_node_t *heading;
    amark_node_t *heading_text;
    amark_node_t *paragraph;
    amark_node_t *emphasis;
    amark_node_t *emphasis_text;
    amark_node_t *soft_break;
    amark_node_t *code;
    amark_node_t *empty_paragraph;
    amark_node_t *thematic_break;
    amark_node_t *list;
    amark_node_t *item;
    amark_node_t *item_paragraph;
    amark_node_t *item_text;
    amark_node_t *code_block;

    doc = test_node(AMARK_NODE_DOCUMENT);
    heading = test_child(doc, AMARK_NODE_HEADING);
    heading_text = test_child(heading, AMARK_NODE_TEXT);
    paragraph = test_child(doc, AMARK_NODE_PARAGRAPH);
    emphasis = test_child(paragraph, AMARK_NODE_EMPHASIS);
    emphasis_text = test_child(emphasis, AMARK_NODE_TEXT);
    soft_break = test_child(paragraph, AMARK_NODE_SOFT_BREAK);
    code = test_child(paragraph, AMARK_NODE_CODE);
    empty_paragraph = test_child(doc, AMARK_NODE_PARAGRAPH);
    thematic_break = test_child(doc, AMARK_NODE_THEMATIC_BREAK);
    list = test_child(doc, AMARK_NODE_LIST);
    item = test_child(list, AMARK_NODE_ITEM);
    item_paragraph = test_child(item, AMARK_NODE_PARAGRAPH);
    item_text = test_child(item_paragraph, AMARK_NODE_TEXT);
    code_block = test_child(doc, AMARK_NODE_CODE_BLOCK);

    {
        const test_step_t expected[] = {
            {doc, AMARK_NODE_ITER_ENTER},
            {heading, AMARK_NODE_ITER_ENTER},
            {heading_text, AMARK_NODE_ITER_ENTER},
            {heading, AMARK_NODE_ITER_EXIT},
            {paragraph, AMARK_NODE_ITER_ENTER},
            {emphasis, AMARK_NODE_ITER_ENTER},
            {emphasis_text, AMARK_NODE_ITER_ENTER},
            {emphasis, AMARK_NODE_ITER_EXIT},
            {soft_break, AMARK_NODE_ITER_ENTER},
            {code, AMARK_NODE_ITER_ENTER},
            {paragraph, AMARK_NODE_ITER_EXIT},
            {empty_paragraph, AMARK_NODE_ITER_ENTER},
            {empty_paragraph, AMARK_NODE_ITER_EXIT},
            {thematic_break, AMARK_NODE_ITER_ENTER},
            {list, AMARK_NODE_ITER_ENTER},
            {item, AMARK_NODE_ITER_ENTER},
            {item_paragraph, AMARK_NODE_ITER_ENTER},
            {item_text, AMARK_NODE_ITER_ENTER},
            {item_paragraph, AMARK_NODE_ITER_EXIT},
            {item, AMARK_NODE_ITER_EXIT},
            {list, AMARK_NODE_ITER_EXIT},
            {code_block, AMARK_NODE_ITER_ENTER},
            {doc, AMARK_NODE_ITER_EXIT},
        };

        test_check_walk(doc, expected, sizeof(expected) / sizeof(expected[0]));
    }

    amark_node_destroy(doc);
}

NAOS_TEST(empty_containers_and_leaves)
{
    amark_node_t *paragraph = test_node(AMARK_NODE_PARAGRAPH);
    amark_node_t *text = test_node(AMARK_NODE_TEXT);
    amark_node_t *doc = test_node(AMARK_NODE_DOCUMENT);

    /* 子を持たない「子を取れる種別」の起点: ENTER と EXIT を続けて報告する */
    {
        const test_step_t expected[] = {
            {paragraph, AMARK_NODE_ITER_ENTER},
            {paragraph, AMARK_NODE_ITER_EXIT},
        };

        test_check_walk(paragraph, expected, 2);
    }

    {
        const test_step_t expected[] = {
            {doc, AMARK_NODE_ITER_ENTER},
            {doc, AMARK_NODE_ITER_EXIT},
        };

        test_check_walk(doc, expected, 2);
    }

    /* 葉の起点: ENTER だけ */
    {
        const test_step_t expected[] = {
            {text, AMARK_NODE_ITER_ENTER},
        };

        test_check_walk(text, expected, 1);
    }

    amark_node_destroy(paragraph);
    amark_node_destroy(text);
    amark_node_destroy(doc);
}

NAOS_TEST(extension_leaf_and_container)
{
    amark_node_registry_t *registry = NULL;
    const amark_node_kind_t *leaf_kind;
    const amark_node_kind_t *box_kind;
    amark_node_t *doc;
    amark_node_t *box;
    amark_node_t *empty_box;
    amark_node_t *leaf;

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));
    leaf_kind = test_add_kind(registry, "marker", AMARK_NODE_CONTENT_NONE);
    box_kind = test_add_kind(registry, "box", AMARK_NODE_CONTENT_BLOCKS);

    doc = test_node(AMARK_NODE_DOCUMENT);
    box = test_child_with_kind(doc, box_kind);
    leaf = test_child_with_kind(box, leaf_kind);
    empty_box = test_child_with_kind(doc, box_kind);

    {
        const test_step_t expected[] = {
            {doc, AMARK_NODE_ITER_ENTER}, {box, AMARK_NODE_ITER_ENTER},       {leaf, AMARK_NODE_ITER_ENTER},
            {box, AMARK_NODE_ITER_EXIT},  {empty_box, AMARK_NODE_ITER_ENTER}, {empty_box, AMARK_NODE_ITER_EXIT},
            {doc, AMARK_NODE_ITER_EXIT},
        };

        test_check_walk(doc, expected, sizeof(expected) / sizeof(expected[0]));
    }

    amark_node_destroy(doc);
    amark_node_registry_destroy(registry);
}

NAOS_TEST(null_arguments)
{
    amark_node_iter_t iter;

    /* iter が NULL: 何もしない / 終了 / NULL / 終了 */
    amark_node_iter_init(NULL, NULL);
    NAOS_CHECK_INT_EQ(AMARK_NODE_ITER_DONE, amark_node_iter_next(NULL));
    NAOS_CHECK_NULL(amark_node_iter_get_node(NULL));
    NAOS_CHECK_INT_EQ(AMARK_NODE_ITER_DONE, amark_node_iter_get_event(NULL));

    /* 起点が NULL: 最初の next から終了 */
    amark_node_iter_init(&iter, NULL);
    test_check_position(&iter, NULL, AMARK_NODE_ITER_NONE);
    test_check_done(&iter);
}

NAOS_TEST(subtree_limited)
{
    amark_node_t *doc = test_node(AMARK_NODE_DOCUMENT);
    amark_node_t *before = test_child(doc, AMARK_NODE_PARAGRAPH);
    amark_node_t *quote = test_child(doc, AMARK_NODE_BLOCK_QUOTE);
    amark_node_t *paragraph = test_child(quote, AMARK_NODE_PARAGRAPH);
    amark_node_t *first = test_child(paragraph, AMARK_NODE_TEXT);
    amark_node_t *second = test_child(paragraph, AMARK_NODE_TEXT);
    amark_node_t *after = test_child(doc, AMARK_NODE_PARAGRAPH);

    (void)before;
    (void)after;

    /* 兄弟を持つ内側のノードを起点にすると、兄弟と祖先を報告しない */
    {
        const test_step_t expected[] = {
            {quote, AMARK_NODE_ITER_ENTER},  {paragraph, AMARK_NODE_ITER_ENTER}, {first, AMARK_NODE_ITER_ENTER},
            {second, AMARK_NODE_ITER_ENTER}, {paragraph, AMARK_NODE_ITER_EXIT},  {quote, AMARK_NODE_ITER_EXIT},
        };

        test_check_walk(quote, expected, sizeof(expected) / sizeof(expected[0]));
    }

    /* 次の兄弟を持つ葉を起点: ENTER の後に終了 */
    {
        const test_step_t expected[] = {
            {first, AMARK_NODE_ITER_ENTER},
        };

        test_check_walk(first, expected, 1);
    }

    /* 最後の子のコンテナを起点: 親の EXIT を報告しない */
    {
        const test_step_t expected[] = {
            {after, AMARK_NODE_ITER_ENTER},
            {after, AMARK_NODE_ITER_EXIT},
        };

        test_check_walk(after, expected, 2);
    }

    amark_node_destroy(doc);
}

NAOS_TEST(reset_valid_positions)
{
    amark_node_t *doc = test_node(AMARK_NODE_DOCUMENT);
    amark_node_t *first = test_child(doc, AMARK_NODE_PARAGRAPH);
    amark_node_t *first_text = test_child(first, AMARK_NODE_TEXT);
    amark_node_t *second = test_child(doc, AMARK_NODE_PARAGRAPH);
    amark_node_t *second_text = test_child(second, AMARK_NODE_TEXT);
    amark_node_t *last_text = test_child(second, AMARK_NODE_TEXT);
    amark_node_iter_t iter;

    /* EXIT へ設定し直すと子を飛ばす */
    amark_node_iter_init(&iter, doc);
    NAOS_CHECK_INT_EQ(AMARK_NODE_ITER_ENTER, amark_node_iter_next(&iter));
    NAOS_CHECK_INT_EQ(AMARK_NODE_ITER_ENTER, amark_node_iter_next(&iter));
    test_check_position(&iter, first, AMARK_NODE_ITER_ENTER);
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_iter_reset(&iter, first, AMARK_NODE_ITER_EXIT));
    test_check_position(&iter, first, AMARK_NODE_ITER_EXIT);
    {
        const test_step_t expected[] = {
            {second, AMARK_NODE_ITER_ENTER},
            {second_text, AMARK_NODE_ITER_ENTER},
        };

        test_check_steps(&iter, expected, 2);
    }

    /* 後ろの葉の ENTER へ進める */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_iter_reset(&iter, last_text, AMARK_NODE_ITER_ENTER));
    {
        const test_step_t expected[] = {
            {second, AMARK_NODE_ITER_EXIT},
            {doc, AMARK_NODE_ITER_EXIT},
        };

        test_check_steps(&iter, expected, 2);
    }

    test_check_done(&iter);

    /* 終了後に前の位置へ戻して再開する */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_iter_reset(&iter, first_text, AMARK_NODE_ITER_ENTER));
    test_check_position(&iter, first_text, AMARK_NODE_ITER_ENTER);
    {
        const test_step_t expected[] = {
            {first, AMARK_NODE_ITER_EXIT},
            {second, AMARK_NODE_ITER_ENTER},
        };

        test_check_steps(&iter, expected, 2);
    }

    /* 起点の ENTER へ戻すと最初からやり直す */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_iter_reset(&iter, doc, AMARK_NODE_ITER_ENTER));
    {
        const test_step_t expected[] = {
            {first, AMARK_NODE_ITER_ENTER},
        };

        test_check_steps(&iter, expected, 1);
    }

    /* 起点の EXIT へ設定すると次は終了 */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_iter_reset(&iter, doc, AMARK_NODE_ITER_EXIT));
    test_check_done(&iter);

    /* まだ next を呼んでいないイテレータも設定し直せる */
    amark_node_iter_init(&iter, doc);
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_iter_reset(&iter, second, AMARK_NODE_ITER_EXIT));
    {
        const test_step_t expected[] = {
            {doc, AMARK_NODE_ITER_EXIT},
        };

        test_check_steps(&iter, expected, 1);
    }

    test_check_done(&iter);

    amark_node_destroy(doc);
}

NAOS_TEST(reset_errors_change_nothing)
{
    amark_node_t *doc = test_node(AMARK_NODE_DOCUMENT);
    amark_node_t *before = test_child(doc, AMARK_NODE_PARAGRAPH);
    amark_node_t *quote = test_child(doc, AMARK_NODE_BLOCK_QUOTE);
    amark_node_t *paragraph = test_child(quote, AMARK_NODE_PARAGRAPH);
    amark_node_t *text = test_child(paragraph, AMARK_NODE_TEXT);
    amark_node_t *other = test_node(AMARK_NODE_PARAGRAPH);
    amark_node_iter_t iter;
    amark_node_iter_t empty;

    amark_node_iter_init(&iter, quote);
    NAOS_CHECK_INT_EQ(AMARK_NODE_ITER_ENTER, amark_node_iter_next(&iter));
    NAOS_CHECK_INT_EQ(AMARK_NODE_ITER_ENTER, amark_node_iter_next(&iter));
    test_check_position(&iter, paragraph, AMARK_NODE_ITER_ENTER);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_iter_reset(NULL, paragraph, AMARK_NODE_ITER_ENTER));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_iter_reset(&iter, NULL, AMARK_NODE_ITER_ENTER));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_iter_reset(&iter, quote, AMARK_NODE_ITER_NONE));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_iter_reset(&iter, quote, AMARK_NODE_ITER_DONE));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_node_iter_reset(&iter, quote, (amark_node_iter_event_t)(AMARK_NODE_ITER_DONE + 1)));

    /* 部分木の外: 起点の兄弟、起点の祖先、別の木 */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_iter_reset(&iter, before, AMARK_NODE_ITER_ENTER));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_iter_reset(&iter, doc, AMARK_NODE_ITER_ENTER));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_iter_reset(&iter, other, AMARK_NODE_ITER_ENTER));

    /* 葉への EXIT */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_iter_reset(&iter, text, AMARK_NODE_ITER_EXIT));

    /* 起点のないイテレータにはどのノードも設定できない */
    amark_node_iter_init(&empty, NULL);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_iter_reset(&empty, other, AMARK_NODE_ITER_ENTER));
    test_check_position(&empty, NULL, AMARK_NODE_ITER_NONE);
    test_check_done(&empty);

    /* いずれもイテレータを変えない */
    test_check_position(&iter, paragraph, AMARK_NODE_ITER_ENTER);
    {
        const test_step_t expected[] = {
            {text, AMARK_NODE_ITER_ENTER},
            {paragraph, AMARK_NODE_ITER_EXIT},
            {quote, AMARK_NODE_ITER_EXIT},
        };

        test_check_steps(&iter, expected, 3);
    }

    test_check_done(&iter);

    amark_node_destroy(doc);
    amark_node_destroy(other);
}

NAOS_TEST(iteration_allocates_nothing)
{
    amark_node_t *doc;
    amark_node_t *paragraph;
    amark_node_iter_t iter;
    test_allocator_stats_t before;
    test_allocator_stats_t after;
    size_t events = 0;

    test_allocator_install();
    doc = test_node(AMARK_NODE_DOCUMENT);
    paragraph = test_child(doc, AMARK_NODE_PARAGRAPH);
    (void)test_child(paragraph, AMARK_NODE_TEXT);
    (void)test_child(paragraph, AMARK_NODE_SOFT_BREAK);
    (void)test_child(doc, AMARK_NODE_THEMATIC_BREAK);

    before = test_allocator_stats();
    amark_node_iter_init(&iter, doc);
    while (amark_node_iter_next(&iter) != AMARK_NODE_ITER_DONE)
    {
        events++;
    }

    (void)amark_node_iter_next(&iter);
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_iter_reset(&iter, paragraph, AMARK_NODE_ITER_EXIT));
    (void)amark_node_iter_next(&iter);
    after = test_allocator_stats();

    NAOS_CHECK_UINT_EQ(7u, events);
    NAOS_CHECK_UINT_EQ(before.allocations, after.allocations);
    NAOS_CHECK_UINT_EQ(before.deallocations, after.deallocations);
    NAOS_CHECK_UINT_EQ(before.failures, after.failures);

    /* 確保をすべて失敗させても走査は進む */
    test_allocator_fail_after(0);
    amark_node_iter_init(&iter, doc);
    events = 0;
    while (amark_node_iter_next(&iter) != AMARK_NODE_ITER_DONE)
    {
        events++;
    }

    NAOS_CHECK_UINT_EQ(7u, events);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().failures);
    test_allocator_never_fail();

    amark_node_destroy(doc);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(edit_values_during_iteration)
{
    amark_node_t *doc = test_node(AMARK_NODE_DOCUMENT);
    amark_node_t *heading = test_child(doc, AMARK_NODE_HEADING);
    amark_node_t *text = test_child(heading, AMARK_NODE_TEXT);
    amark_node_iter_t iter;
    amark_node_iter_event_t event;
    size_t count = 0;

    amark_node_iter_init(&iter, doc);
    while ((event = amark_node_iter_next(&iter)) != AMARK_NODE_ITER_DONE)
    {
        amark_node_t *node = amark_node_iter_get_node(&iter);

        if ((event == AMARK_NODE_ITER_ENTER) && (amark_node_get_type(node) == AMARK_NODE_TEXT))
        {
            NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_literal(node, amark_slice_from_cstr("changed")));
        }
        else if ((event == AMARK_NODE_ITER_ENTER) && (amark_node_get_type(node) == AMARK_NODE_HEADING))
        {
            NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_heading_level(node, 3));
        }

        count++;
    }

    NAOS_CHECK_UINT_EQ(5u, count);
    NAOS_CHECK_INT_EQ(3, amark_node_get_heading_level(heading));
    {
        amark_slice_t literal = amark_node_get_literal(text);

        NAOS_CHECK_MEM_EQ("changed", 7u, literal.data, literal.len);
    }

    amark_node_destroy(doc);
}

NAOS_TEST(remove_unreported_siblings)
{
    amark_node_t *doc = test_node(AMARK_NODE_DOCUMENT);
    amark_node_t *first = test_child(doc, AMARK_NODE_PARAGRAPH);
    amark_node_t *second = test_child(doc, AMARK_NODE_THEMATIC_BREAK);
    amark_node_t *third = test_child(doc, AMARK_NODE_PARAGRAPH);
    amark_node_t *third_text = test_child(third, AMARK_NODE_TEXT);
    amark_node_t *fourth = test_child(doc, AMARK_NODE_PARAGRAPH);
    amark_node_t *detached;
    amark_node_iter_t iter;

    (void)third_text;

    amark_node_iter_init(&iter, doc);
    {
        const test_step_t expected[] = {
            {doc, AMARK_NODE_ITER_ENTER},
            {first, AMARK_NODE_ITER_ENTER},
        };

        test_check_steps(&iter, expected, 2);
    }

    /* まだ報告していない兄弟を破棄・取り外す: 報告されない */
    amark_node_destroy(third);
    detached = second;
    amark_node_unlink(detached);

    /* 報告済みの位置へ加えたノードは報告しない（first の前） */
    {
        amark_node_t *early = test_node(AMARK_NODE_PARAGRAPH);

        NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_insert_before(first, early));
    }

    {
        const test_step_t expected[] = {
            {first, AMARK_NODE_ITER_EXIT},
            {fourth, AMARK_NODE_ITER_ENTER},
            {fourth, AMARK_NODE_ITER_EXIT},
            {doc, AMARK_NODE_ITER_EXIT},
        };

        test_check_steps(&iter, expected, 4);
    }

    test_check_done(&iter);

    amark_node_destroy(detached);
    amark_node_destroy(doc);
}

NAOS_TEST(add_children_at_enter)
{
    amark_node_t *doc = test_node(AMARK_NODE_DOCUMENT);
    amark_node_t *paragraph = test_child(doc, AMARK_NODE_PARAGRAPH);
    amark_node_t *old_text = test_child(paragraph, AMARK_NODE_TEXT);
    amark_node_t *added_first = NULL;
    amark_node_t *added_last = NULL;
    amark_node_t *sibling = NULL;
    amark_node_iter_t iter;

    amark_node_iter_init(&iter, doc);
    {
        const test_step_t expected[] = {
            {doc, AMARK_NODE_ITER_ENTER},
            {paragraph, AMARK_NODE_ITER_ENTER},
        };

        test_check_steps(&iter, expected, 2);
    }

    /* ENTER の位置で子を先頭・末尾に追加し、既存の子を破棄する: 編集後の子を報告する */
    added_first = test_node(AMARK_NODE_CODE);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_prepend_child(paragraph, added_first));
    added_last = test_child(paragraph, AMARK_NODE_LINE_BREAK);
    amark_node_destroy(old_text);

    {
        const test_step_t expected[] = {
            {added_first, AMARK_NODE_ITER_ENTER},
            {added_last, AMARK_NODE_ITER_ENTER},
        };

        test_check_steps(&iter, expected, 2);
    }

    /* 現在ノードの後ろへ兄弟を挿入する: 報告する */
    sibling = test_node(AMARK_NODE_TEXT);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_insert_after(added_last, sibling));

    {
        const test_step_t expected[] = {
            {sibling, AMARK_NODE_ITER_ENTER},
            {paragraph, AMARK_NODE_ITER_EXIT},
        };

        test_check_steps(&iter, expected, 2);
    }

    /* EXIT の位置で子を追加する: もう報告しない */
    (void)test_child(paragraph, AMARK_NODE_TEXT);

    {
        const test_step_t expected[] = {
            {doc, AMARK_NODE_ITER_EXIT},
        };

        test_check_steps(&iter, expected, 1);
    }

    test_check_done(&iter);

    amark_node_destroy(doc);
}

NAOS_TEST(add_child_to_empty_container_at_enter)
{
    amark_node_t *doc = test_node(AMARK_NODE_DOCUMENT);
    amark_node_t *paragraph = test_child(doc, AMARK_NODE_PARAGRAPH);
    amark_node_t *text;
    amark_node_iter_t iter;

    amark_node_iter_init(&iter, doc);
    NAOS_CHECK_INT_EQ(AMARK_NODE_ITER_ENTER, amark_node_iter_next(&iter));
    NAOS_CHECK_INT_EQ(AMARK_NODE_ITER_ENTER, amark_node_iter_next(&iter));
    text = test_child(paragraph, AMARK_NODE_TEXT);

    {
        const test_step_t expected[] = {
            {text, AMARK_NODE_ITER_ENTER},
            {paragraph, AMARK_NODE_ITER_EXIT},
            {doc, AMARK_NODE_ITER_EXIT},
        };

        test_check_steps(&iter, expected, 3);
    }

    test_check_done(&iter);

    amark_node_destroy(doc);
}

NAOS_TEST(detach_current_then_reset)
{
    amark_node_t *doc = test_node(AMARK_NODE_DOCUMENT);
    amark_node_t *first = test_child(doc, AMARK_NODE_PARAGRAPH);
    amark_node_t *first_text = test_child(first, AMARK_NODE_TEXT);
    amark_node_t *second = test_child(doc, AMARK_NODE_PARAGRAPH);
    amark_node_t *second_text = test_child(second, AMARK_NODE_TEXT);
    amark_node_t *rule = test_child(doc, AMARK_NODE_THEMATIC_BREAK);
    amark_node_iter_t iter;

    (void)first_text;

    /* 子を持つコンテナの ENTER で現在ノードを取り外す: 次は終了で、以後も終了 */
    amark_node_iter_init(&iter, doc);
    NAOS_CHECK_INT_EQ(AMARK_NODE_ITER_ENTER, amark_node_iter_next(&iter));
    NAOS_CHECK_INT_EQ(AMARK_NODE_ITER_ENTER, amark_node_iter_next(&iter));
    test_check_position(&iter, first, AMARK_NODE_ITER_ENTER);
    amark_node_unlink(first);
    test_check_done(&iter);

    /* 取り外したノードは部分木の外: 設定し直せない */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_iter_reset(&iter, first, AMARK_NODE_ITER_EXIT));

    /* 部分木内の有効な位置へ設定し直して再開する */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_iter_reset(&iter, doc, AMARK_NODE_ITER_ENTER));
    {
        const test_step_t expected[] = {
            {second, AMARK_NODE_ITER_ENTER},
            {second_text, AMARK_NODE_ITER_ENTER},
        };

        test_check_steps(&iter, expected, 2);
    }

    /* 葉の現在ノードを取り外す: 次は終了。再設定で再開する */
    amark_node_unlink(second_text);
    test_check_done(&iter);
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_iter_reset(&iter, second, AMARK_NODE_ITER_EXIT));
    {
        const test_step_t expected[] = {
            {rule, AMARK_NODE_ITER_ENTER},
            {doc, AMARK_NODE_ITER_EXIT},
        };

        test_check_steps(&iter, expected, 2);
    }

    test_check_done(&iter);

    /* EXIT の位置で現在ノードを取り外す: 次は終了 */
    amark_node_iter_init(&iter, doc);
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_iter_reset(&iter, second, AMARK_NODE_ITER_EXIT));
    amark_node_unlink(second);
    test_check_done(&iter);

    amark_node_destroy(first);
    amark_node_destroy(second);
    amark_node_destroy(second_text);
    amark_node_destroy(doc);
}

NAOS_TEST(detach_current_root_keeps_iterating)
{
    amark_node_t *doc = test_node(AMARK_NODE_DOCUMENT);
    amark_node_t *quote = test_child(doc, AMARK_NODE_BLOCK_QUOTE);
    amark_node_t *paragraph = test_child(quote, AMARK_NODE_PARAGRAPH);
    amark_node_iter_t iter;

    /* 起点は親を失っても走査を続ける */
    amark_node_iter_init(&iter, quote);
    NAOS_CHECK_INT_EQ(AMARK_NODE_ITER_ENTER, amark_node_iter_next(&iter));
    amark_node_unlink(quote);
    {
        const test_step_t expected[] = {
            {paragraph, AMARK_NODE_ITER_ENTER},
            {paragraph, AMARK_NODE_ITER_EXIT},
            {quote, AMARK_NODE_ITER_EXIT},
        };

        test_check_steps(&iter, expected, 3);
    }

    test_check_done(&iter);

    amark_node_destroy(quote);
    amark_node_destroy(doc);
}

/*
 * ヘッダに記した手順: 現在ノード N（起点でない）を破棄して続けるには、破棄の前に、前の兄弟 P が
 * あれば (P, P が葉なら ENTER、そうでなければ EXIT)、なければ (N の親, ENTER) へ設定し直す
 */
static void test_destroy_current(amark_node_iter_t *iter)
{
    amark_node_t *node = amark_node_iter_get_node(iter);
    amark_node_t *previous = amark_node_get_previous(node);

    if (previous != NULL)
    {
        amark_node_iter_event_t event = AMARK_NODE_ITER_EXIT;

        if (amark_node_kind_is_leaf(amark_node_get_kind(previous)))
        {
            event = AMARK_NODE_ITER_ENTER;
        }

        NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_iter_reset(iter, previous, event));
    }
    else
    {
        NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_iter_reset(iter, amark_node_get_parent(node), AMARK_NODE_ITER_ENTER));
    }

    amark_node_destroy(node);
}

NAOS_TEST(destroy_current_procedure)
{
    amark_node_t *doc;
    amark_node_t *paragraph;
    amark_node_t *keep_text;
    amark_node_t *keep_break;
    amark_node_t *rule;
    amark_node_iter_t iter;
    amark_node_iter_event_t event;
    size_t steps = 0;

    test_allocator_install();
    doc = test_node(AMARK_NODE_DOCUMENT);
    (void)test_child(doc, AMARK_NODE_CODE_BLOCK); /* 最初の子（前の兄弟なし）: 破棄 */
    paragraph = test_child(doc, AMARK_NODE_PARAGRAPH);
    (void)test_child(paragraph, AMARK_NODE_CODE); /* 最初の子: 破棄 */
    keep_text = test_child(paragraph, AMARK_NODE_TEXT);
    (void)test_child(paragraph, AMARK_NODE_CODE); /* 前の兄弟が葉: 破棄 */
    keep_break = test_child(paragraph, AMARK_NODE_SOFT_BREAK);
    (void)test_child(paragraph, AMARK_NODE_CODE); /* 最後の子: 破棄 */
    {
        amark_node_t *quote = test_child(doc, AMARK_NODE_BLOCK_QUOTE); /* 前の兄弟がコンテナ: 破棄 */

        (void)test_child(quote, AMARK_NODE_PARAGRAPH);
    }
    rule = test_child(doc, AMARK_NODE_THEMATIC_BREAK);

    {
        const test_step_t expected[] = {
            {doc, AMARK_NODE_ITER_ENTER},        {paragraph, AMARK_NODE_ITER_ENTER}, {keep_text, AMARK_NODE_ITER_ENTER},
            {keep_break, AMARK_NODE_ITER_ENTER}, {paragraph, AMARK_NODE_ITER_EXIT},  {rule, AMARK_NODE_ITER_ENTER},
            {doc, AMARK_NODE_ITER_EXIT},
        };
        const size_t count = sizeof(expected) / sizeof(expected[0]);

        amark_node_iter_init(&iter, doc);
        while ((event = amark_node_iter_next(&iter)) != AMARK_NODE_ITER_DONE)
        {
            amark_node_t *node = amark_node_iter_get_node(&iter);
            amark_node_type_t type = amark_node_get_type(node);

            if ((event == AMARK_NODE_ITER_ENTER) &&
                ((type == AMARK_NODE_CODE) || (type == AMARK_NODE_CODE_BLOCK) || (type == AMARK_NODE_BLOCK_QUOTE)))
            {
                test_destroy_current(&iter);
                continue;
            }

            NAOS_REQUIRE_UINT_LT(steps, count);
            NAOS_CHECK_PTR_EQ(expected[steps].node, node);
            NAOS_CHECK_INT_EQ(expected[steps].event, event);
            steps++;
        }

        NAOS_CHECK_UINT_EQ(count, steps);
    }

    NAOS_CHECK_PTR_EQ(paragraph, amark_node_get_first_child(doc));
    NAOS_CHECK_PTR_EQ(keep_text, amark_node_get_first_child(paragraph));
    NAOS_CHECK_PTR_EQ(keep_break, amark_node_get_last_child(paragraph));
    NAOS_CHECK_PTR_EQ(rule, amark_node_get_last_child(doc));

    amark_node_destroy(doc);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(deep_tree_iteration)
{
    amark_node_t *doc;
    amark_node_t *current;
    amark_node_t *deepest;
    amark_node_iter_t iter;
    amark_node_iter_event_t event;
    size_t enters = 0;
    size_t exits = 0;
    size_t depth = 0;
    size_t max_depth = 0;
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

        current = quote;
    }

    current = test_child(current, AMARK_NODE_PARAGRAPH);
    deepest = test_child(current, AMARK_NODE_TEXT);

    amark_node_iter_init(&iter, doc);
    while ((event = amark_node_iter_next(&iter)) != AMARK_NODE_ITER_DONE)
    {
        if (event == AMARK_NODE_ITER_ENTER)
        {
            enters++;
            if (amark_node_iter_get_node(&iter) == deepest)
            {
                max_depth = depth;
            }
            else
            {
                depth++;
            }
        }
        else
        {
            exits++;
            depth--;
        }
    }

    /* document + 10 万段 + paragraph + text の ENTER、text 以外の EXIT */
    NAOS_CHECK_UINT_EQ(TEST_DEEP_DEPTH + 3u, enters);
    NAOS_CHECK_UINT_EQ(TEST_DEEP_DEPTH + 2u, exits);
    NAOS_CHECK_UINT_EQ(TEST_DEEP_DEPTH + 2u, max_depth);
    NAOS_CHECK_UINT_EQ(0u, depth);

    /* 最深部の葉への設定し直しは祖先を 10 万段たどって成功する */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_iter_reset(&iter, deepest, AMARK_NODE_ITER_ENTER));
    NAOS_CHECK_INT_EQ(AMARK_NODE_ITER_EXIT, amark_node_iter_next(&iter));
    NAOS_CHECK_PTR_EQ(current, amark_node_iter_get_node(&iter));

    amark_node_destroy(doc);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

/*
 * 文書木シナリオ（1.6、6.4、12.6）: 公開 API だけで見出し・段落（強調・リンク・コードスパン）・2 項目の
 * リスト・コードブロック・拡張ブロック（独自判定で拡張の行だけを受け入れる表）を組み、走査・特定種別の
 * 置換・正規化・破棄までを通す
 */

/* 表と行の登録で共有する状態。表の判定関数と行の後片付け関数の user_data */
typedef struct scenario_context
{
    const amark_node_kind_t *row; /* 表が受け入れる唯一の種別 */
    size_t destroyed;             /* 後片付けの回数 */
} scenario_context_t;

/* 期待する 1 イベントと、ENTER 時の種別と値（文字列の値を持つ種別の内容、link の url、拡張の種別名） */
typedef struct scenario_step
{
    const amark_node_t *node;
    amark_node_iter_event_t event;
    amark_node_type_t type;
    const char *value; /* NULL は値を確かめない */
} scenario_step_t;

/* 表の受け入れ判定: 登録済みの行の種別だけを受け入れる */
static bool scenario_table_can_contain(const amark_node_kind_t *parent, const amark_node_kind_t *child, void *user_data)
{
    const scenario_context_t *context = (const scenario_context_t *)user_data;

    (void)parent;

    return (child != NULL) && (child == context->row);
}

/* 行の付随データの後片付け: 確保したデータを解放して回数を数える */
static void scenario_row_destroy_data(void *data, void *user_data)
{
    scenario_context_t *context = (scenario_context_t *)user_data;

    amark_free(data);
    context->destroyed++;
}

/* 拡張種別を記述子どおりに登録する */
static const amark_node_kind_t *scenario_add_kind(amark_node_registry_t *registry, const char *name,
                                                  amark_node_content_t content, bool requires_explicit_parent,
                                                  amark_node_can_contain_fn_t can_contain,
                                                  amark_node_destroy_data_fn_t destroy_data, void *user_data)
{
    amark_node_kind_desc_t desc;
    const amark_node_kind_t *kind = NULL;

    memset(&desc, 0, sizeof(desc));
    desc.name = amark_slice_from_cstr(name);
    desc.category = AMARK_NODE_CATEGORY_BLOCK;
    desc.content = content;
    desc.requires_explicit_parent = requires_explicit_parent;
    desc.can_contain = can_contain;
    desc.destroy_data = destroy_data;
    desc.user_data = user_data;

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &kind));
    NAOS_REQUIRE_NOT_NULL(kind);

    return kind;
}

/* 文字列の値（literal）を持つ標準種別のノードを生成して parent の末尾の子にする */
static amark_node_t *scenario_literal(amark_node_t *parent, amark_node_type_t type, const char *literal)
{
    amark_node_t *node = test_child(parent, type);

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_set_literal(node, amark_slice_from_cstr(literal)));

    return node;
}

/* ENTER 時に報告されたノードの種別と値が step どおりであることを確かめる */
static void scenario_check_value(const amark_node_t *node, const scenario_step_t *step)
{
    amark_slice_t actual = {NULL, 0};

    NAOS_CHECK_INT_EQ(step->type, amark_node_get_type(node));
    if (step->value == NULL)
    {
        return;
    }

    switch (step->type)
    {
    case AMARK_NODE_TEXT:
    case AMARK_NODE_CODE:
    case AMARK_NODE_CODE_BLOCK:
        actual = amark_node_get_literal(node);
        break;
    case AMARK_NODE_LINK:
        actual = amark_node_get_url(node);
        break;
    case AMARK_NODE_EXTENSION:
        actual = amark_node_kind_get_name(amark_node_get_kind(node));
        break;
    default:
        NAOS_FAIL("value check is not defined for this type");
        return;
    }

    NAOS_CHECK_TRUE(amark_slice_equal(amark_slice_from_cstr(step->value), actual));
    NAOS_CHECK_TRUE((actual.data != NULL) && (actual.data[actual.len] == '\0'));
}

/* root から走査し、イベント列と ENTER 時の種別・値が expected どおりで、その後に終了することを確かめる */
static void scenario_check_walk(amark_node_t *root, const scenario_step_t *expected, size_t count)
{
    amark_node_iter_t iter;
    amark_node_iter_event_t event;
    size_t steps = 0;

    amark_node_iter_init(&iter, root);
    while ((event = amark_node_iter_next(&iter)) != AMARK_NODE_ITER_DONE)
    {
        amark_node_t *node = amark_node_iter_get_node(&iter);

        NAOS_REQUIRE_UINT_LT(steps, count);
        NAOS_CHECK_PTR_EQ(expected[steps].node, node);
        NAOS_CHECK_INT_EQ(expected[steps].event, event);
        if (event == AMARK_NODE_ITER_ENTER)
        {
            scenario_check_value(node, &expected[steps]);
        }

        steps++;
    }

    NAOS_CHECK_UINT_EQ(count, steps);
    test_check_done(&iter);
}

NAOS_TEST(document_tree_scenario)
{
    scenario_context_t context = {NULL, 0};
    amark_node_registry_t *registry = NULL;
    const amark_node_kind_t *row_kind;
    const amark_node_kind_t *table_kind;
    amark_node_t *doc;
    amark_node_t *heading;
    amark_node_t *heading_text;
    amark_node_t *paragraph;
    amark_node_t *see_text;
    amark_node_t *emphasis;
    amark_node_t *emphasis_text;
    amark_node_t *and_text;
    amark_node_t *link;
    amark_node_t *link_text;
    amark_node_t *or_text;
    amark_node_t *code;
    amark_node_t *list;
    amark_node_t *item1;
    amark_node_t *item1_paragraph;
    amark_node_t *item1_text;
    amark_node_t *item2;
    amark_node_t *item2_paragraph;
    amark_node_t *item2_text;
    amark_node_t *code_block;
    amark_node_t *table;
    amark_node_t *row1;
    amark_node_t *row1_text;
    amark_node_t *row1_code;
    amark_node_t *row2;
    amark_node_t *replaced[2];
    amark_node_t *replacements[2];
    size_t replaced_count = 0;
    size_t i;
    int *row_data;

    test_allocator_install();

    /* レジストリ: 行（明示親限定のインラインのコンテナ）と、行だけを受け入れる表 */
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));
    row_kind = scenario_add_kind(registry, "table_row", AMARK_NODE_CONTENT_INLINES, true, NULL,
                                 scenario_row_destroy_data, &context);
    context.row = row_kind;
    table_kind = scenario_add_kind(registry, "table", AMARK_NODE_CONTENT_CUSTOM, false, scenario_table_can_contain,
                                   NULL, &context);
    NAOS_CHECK_PTR_EQ(table_kind, amark_node_registry_find(registry, amark_slice_from_cstr("table")));
    NAOS_CHECK_PTR_EQ(row_kind, amark_node_registry_find(registry, amark_slice_from_cstr("table_row")));

    /* 独自判定は拡張の行だけを受け入れ、明示親限定の行は任意のブロックの親に置けない */
    NAOS_CHECK_TRUE(amark_node_kind_can_contain(table_kind, row_kind));
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(table_kind, table_kind));
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(table_kind, amark_node_kind_get_standard(AMARK_NODE_PARAGRAPH)));
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(table_kind, amark_node_kind_get_standard(AMARK_NODE_TEXT)));
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(table_kind, amark_node_kind_get_standard(AMARK_NODE_ITEM)));
    NAOS_CHECK_TRUE(amark_node_kind_can_contain(amark_node_kind_get_standard(AMARK_NODE_DOCUMENT), table_kind));
    NAOS_CHECK_FALSE(amark_node_kind_can_contain(amark_node_kind_get_standard(AMARK_NODE_DOCUMENT), row_kind));

    /* 文書木を API だけで組む */
    doc = test_node(AMARK_NODE_DOCUMENT);

    heading = test_child(doc, AMARK_NODE_HEADING);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_set_heading_level(heading, 2));
    heading_text = scenario_literal(heading, AMARK_NODE_TEXT, "Amark");

    paragraph = test_child(doc, AMARK_NODE_PARAGRAPH);
    see_text = scenario_literal(paragraph, AMARK_NODE_TEXT, "See ");
    emphasis = test_child(paragraph, AMARK_NODE_EMPHASIS);
    emphasis_text = scenario_literal(emphasis, AMARK_NODE_TEXT, "this");
    and_text = scenario_literal(paragraph, AMARK_NODE_TEXT, " and ");
    link = test_child(paragraph, AMARK_NODE_LINK);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_set_url(link, amark_slice_from_cstr("https://example.com/")));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_set_title(link, amark_slice_from_cstr("Example")));
    link_text = scenario_literal(link, AMARK_NODE_TEXT, "site");
    or_text = scenario_literal(paragraph, AMARK_NODE_TEXT, " or ");
    code = scenario_literal(paragraph, AMARK_NODE_CODE, "x = 1");
    (void)scenario_literal(paragraph, AMARK_NODE_TEXT, ""); /* 空の text: 正規化で取り除かれる */

    list = test_child(doc, AMARK_NODE_LIST);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_set_list_type(list, AMARK_NODE_LIST_ORDERED));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_set_list_start(list, 3u));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_set_list_tight(list, true));
    item1 = test_child(list, AMARK_NODE_ITEM);
    item1_paragraph = test_child(item1, AMARK_NODE_PARAGRAPH);
    item1_text = scenario_literal(item1_paragraph, AMARK_NODE_TEXT, "one");
    item2 = test_child(list, AMARK_NODE_ITEM);
    item2_paragraph = test_child(item2, AMARK_NODE_PARAGRAPH);
    item2_text = scenario_literal(item2_paragraph, AMARK_NODE_TEXT, "two");

    code_block = scenario_literal(doc, AMARK_NODE_CODE_BLOCK, "int x;\n");
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_set_info(code_block, amark_slice_from_cstr("c")));

    table = test_child_with_kind(doc, table_kind);
    row1 = test_child_with_kind(table, row_kind);
    row1_text = scenario_literal(row1, AMARK_NODE_TEXT, "a");
    row1_code = scenario_literal(row1, AMARK_NODE_CODE, "b");
    row2 = test_child_with_kind(table, row_kind);
    (void)scenario_literal(row2, AMARK_NODE_TEXT, ""); /* 正規化で行が空のコンテナになる */

    row_data = (int *)amark_malloc(sizeof(*row_data));
    NAOS_REQUIRE_NOT_NULL(row_data);
    *row_data = 42;
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_set_data(row1, row_data));

    /* 表は行以外を拒否し、行は表以外に置けない。どちらも木を変えない */
    {
        amark_node_t *stray_paragraph = test_node(AMARK_NODE_PARAGRAPH);
        amark_node_t *stray_row = NULL;

        NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create_with_kind(row_kind, &stray_row));
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_append_child(table, stray_paragraph));
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_append_child(doc, stray_row));
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_insert_after(table, stray_row));
        NAOS_CHECK_NULL(amark_node_get_parent(stray_paragraph));
        NAOS_CHECK_NULL(amark_node_get_parent(stray_row));
        NAOS_CHECK_PTR_EQ(row2, amark_node_get_last_child(table));
        NAOS_CHECK_PTR_EQ(table, amark_node_get_last_child(doc));
        amark_node_destroy(stray_paragraph);
        amark_node_destroy(stray_row);
    }

    /* 走査 1: 組んだとおりのイベント列と値 */
    {
        const scenario_step_t expected[] = {
            {doc, AMARK_NODE_ITER_ENTER, AMARK_NODE_DOCUMENT, NULL},
            {heading, AMARK_NODE_ITER_ENTER, AMARK_NODE_HEADING, NULL},
            {heading_text, AMARK_NODE_ITER_ENTER, AMARK_NODE_TEXT, "Amark"},
            {heading, AMARK_NODE_ITER_EXIT, AMARK_NODE_HEADING, NULL},
            {paragraph, AMARK_NODE_ITER_ENTER, AMARK_NODE_PARAGRAPH, NULL},
            {see_text, AMARK_NODE_ITER_ENTER, AMARK_NODE_TEXT, "See "},
            {emphasis, AMARK_NODE_ITER_ENTER, AMARK_NODE_EMPHASIS, NULL},
            {emphasis_text, AMARK_NODE_ITER_ENTER, AMARK_NODE_TEXT, "this"},
            {emphasis, AMARK_NODE_ITER_EXIT, AMARK_NODE_EMPHASIS, NULL},
            {and_text, AMARK_NODE_ITER_ENTER, AMARK_NODE_TEXT, " and "},
            {link, AMARK_NODE_ITER_ENTER, AMARK_NODE_LINK, "https://example.com/"},
            {link_text, AMARK_NODE_ITER_ENTER, AMARK_NODE_TEXT, "site"},
            {link, AMARK_NODE_ITER_EXIT, AMARK_NODE_LINK, NULL},
            {or_text, AMARK_NODE_ITER_ENTER, AMARK_NODE_TEXT, " or "},
            {code, AMARK_NODE_ITER_ENTER, AMARK_NODE_CODE, "x = 1"},
            {amark_node_get_last_child(paragraph), AMARK_NODE_ITER_ENTER, AMARK_NODE_TEXT, ""},
            {paragraph, AMARK_NODE_ITER_EXIT, AMARK_NODE_PARAGRAPH, NULL},
            {list, AMARK_NODE_ITER_ENTER, AMARK_NODE_LIST, NULL},
            {item1, AMARK_NODE_ITER_ENTER, AMARK_NODE_ITEM, NULL},
            {item1_paragraph, AMARK_NODE_ITER_ENTER, AMARK_NODE_PARAGRAPH, NULL},
            {item1_text, AMARK_NODE_ITER_ENTER, AMARK_NODE_TEXT, "one"},
            {item1_paragraph, AMARK_NODE_ITER_EXIT, AMARK_NODE_PARAGRAPH, NULL},
            {item1, AMARK_NODE_ITER_EXIT, AMARK_NODE_ITEM, NULL},
            {item2, AMARK_NODE_ITER_ENTER, AMARK_NODE_ITEM, NULL},
            {item2_paragraph, AMARK_NODE_ITER_ENTER, AMARK_NODE_PARAGRAPH, NULL},
            {item2_text, AMARK_NODE_ITER_ENTER, AMARK_NODE_TEXT, "two"},
            {item2_paragraph, AMARK_NODE_ITER_EXIT, AMARK_NODE_PARAGRAPH, NULL},
            {item2, AMARK_NODE_ITER_EXIT, AMARK_NODE_ITEM, NULL},
            {list, AMARK_NODE_ITER_EXIT, AMARK_NODE_LIST, NULL},
            {code_block, AMARK_NODE_ITER_ENTER, AMARK_NODE_CODE_BLOCK, "int x;\n"},
            {table, AMARK_NODE_ITER_ENTER, AMARK_NODE_EXTENSION, "table"},
            {row1, AMARK_NODE_ITER_ENTER, AMARK_NODE_EXTENSION, "table_row"},
            {row1_text, AMARK_NODE_ITER_ENTER, AMARK_NODE_TEXT, "a"},
            {row1_code, AMARK_NODE_ITER_ENTER, AMARK_NODE_CODE, "b"},
            {row1, AMARK_NODE_ITER_EXIT, AMARK_NODE_EXTENSION, NULL},
            {row2, AMARK_NODE_ITER_ENTER, AMARK_NODE_EXTENSION, "table_row"},
            {amark_node_get_first_child(row2), AMARK_NODE_ITER_ENTER, AMARK_NODE_TEXT, ""},
            {row2, AMARK_NODE_ITER_EXIT, AMARK_NODE_EXTENSION, NULL},
            {table, AMARK_NODE_ITER_EXIT, AMARK_NODE_EXTENSION, NULL},
            {doc, AMARK_NODE_ITER_EXIT, AMARK_NODE_DOCUMENT, NULL},
        };

        scenario_check_walk(doc, expected, sizeof(expected) / sizeof(expected[0]));
    }

    /* 数値・真偽値・info・title・付随データの値 */
    NAOS_CHECK_INT_EQ(2, amark_node_get_heading_level(heading));
    NAOS_CHECK_INT_EQ(AMARK_NODE_LIST_ORDERED, amark_node_get_list_type(list));
    NAOS_CHECK_UINT_EQ(3u, amark_node_get_list_start(list));
    NAOS_CHECK_TRUE(amark_node_get_list_tight(list));
    NAOS_CHECK_TRUE(amark_slice_equal(amark_slice_from_cstr("c"), amark_node_get_info(code_block)));
    NAOS_CHECK_TRUE(amark_slice_equal(amark_slice_from_cstr("Example"), amark_node_get_title(link)));
    NAOS_CHECK_PTR_EQ(row_data, amark_node_get_data(row1));
    NAOS_CHECK_NULL(amark_node_get_data(row2));
    NAOS_CHECK_NULL(amark_node_get_data(table));

    /* 特定種別の置換: 走査で code span を集め、同じ内容の text に置き換える */
    {
        amark_node_iter_t iter;
        amark_node_iter_event_t event;

        amark_node_iter_init(&iter, doc);
        while ((event = amark_node_iter_next(&iter)) != AMARK_NODE_ITER_DONE)
        {
            amark_node_t *node = amark_node_iter_get_node(&iter);

            if ((event == AMARK_NODE_ITER_ENTER) && (amark_node_get_type(node) == AMARK_NODE_CODE))
            {
                NAOS_REQUIRE_UINT_LT(replaced_count, 2u);
                replaced[replaced_count] = node;
                replaced_count++;
            }
        }
    }

    NAOS_REQUIRE_UINT_EQ(2u, replaced_count);
    NAOS_CHECK_PTR_EQ(code, replaced[0]);
    NAOS_CHECK_PTR_EQ(row1_code, replaced[1]);
    for (i = 0; i < replaced_count; i++)
    {
        amark_node_t *target = replaced[i];
        amark_node_t *parent = amark_node_get_parent(target);
        amark_node_t *previous = amark_node_get_previous(target);
        amark_node_t *next = amark_node_get_next(target);

        replacements[i] = test_node(AMARK_NODE_TEXT);
        NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_set_literal(replacements[i], amark_node_get_literal(target)));
        NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_replace(target, replacements[i]));

        NAOS_CHECK_PTR_EQ(parent, amark_node_get_parent(replacements[i]));
        NAOS_CHECK_PTR_EQ(previous, amark_node_get_previous(replacements[i]));
        NAOS_CHECK_PTR_EQ(next, amark_node_get_next(replacements[i]));
        NAOS_CHECK_NULL(amark_node_get_parent(target));
        NAOS_CHECK_NULL(amark_node_get_previous(target));
        NAOS_CHECK_NULL(amark_node_get_next(target));
        amark_node_destroy(target); /* 取り外された元のノードは呼び出し側の所有 */
    }

    /* 正規化: 置換で隣り合った text が結合され、空の text が取り除かれる */
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_normalize(doc));

    /* 走査 2: 置換と正規化の後のイベント列と値 */
    {
        const scenario_step_t expected[] = {
            {doc, AMARK_NODE_ITER_ENTER, AMARK_NODE_DOCUMENT, NULL},
            {heading, AMARK_NODE_ITER_ENTER, AMARK_NODE_HEADING, NULL},
            {heading_text, AMARK_NODE_ITER_ENTER, AMARK_NODE_TEXT, "Amark"},
            {heading, AMARK_NODE_ITER_EXIT, AMARK_NODE_HEADING, NULL},
            {paragraph, AMARK_NODE_ITER_ENTER, AMARK_NODE_PARAGRAPH, NULL},
            {see_text, AMARK_NODE_ITER_ENTER, AMARK_NODE_TEXT, "See "},
            {emphasis, AMARK_NODE_ITER_ENTER, AMARK_NODE_EMPHASIS, NULL},
            {emphasis_text, AMARK_NODE_ITER_ENTER, AMARK_NODE_TEXT, "this"},
            {emphasis, AMARK_NODE_ITER_EXIT, AMARK_NODE_EMPHASIS, NULL},
            {and_text, AMARK_NODE_ITER_ENTER, AMARK_NODE_TEXT, " and "},
            {link, AMARK_NODE_ITER_ENTER, AMARK_NODE_LINK, "https://example.com/"},
            {link_text, AMARK_NODE_ITER_ENTER, AMARK_NODE_TEXT, "site"},
            {link, AMARK_NODE_ITER_EXIT, AMARK_NODE_LINK, NULL},
            {amark_node_get_last_child(paragraph), AMARK_NODE_ITER_ENTER, AMARK_NODE_TEXT, " or x = 1"},
            {paragraph, AMARK_NODE_ITER_EXIT, AMARK_NODE_PARAGRAPH, NULL},
            {list, AMARK_NODE_ITER_ENTER, AMARK_NODE_LIST, NULL},
            {item1, AMARK_NODE_ITER_ENTER, AMARK_NODE_ITEM, NULL},
            {item1_paragraph, AMARK_NODE_ITER_ENTER, AMARK_NODE_PARAGRAPH, NULL},
            {item1_text, AMARK_NODE_ITER_ENTER, AMARK_NODE_TEXT, "one"},
            {item1_paragraph, AMARK_NODE_ITER_EXIT, AMARK_NODE_PARAGRAPH, NULL},
            {item1, AMARK_NODE_ITER_EXIT, AMARK_NODE_ITEM, NULL},
            {item2, AMARK_NODE_ITER_ENTER, AMARK_NODE_ITEM, NULL},
            {item2_paragraph, AMARK_NODE_ITER_ENTER, AMARK_NODE_PARAGRAPH, NULL},
            {item2_text, AMARK_NODE_ITER_ENTER, AMARK_NODE_TEXT, "two"},
            {item2_paragraph, AMARK_NODE_ITER_EXIT, AMARK_NODE_PARAGRAPH, NULL},
            {item2, AMARK_NODE_ITER_EXIT, AMARK_NODE_ITEM, NULL},
            {list, AMARK_NODE_ITER_EXIT, AMARK_NODE_LIST, NULL},
            {code_block, AMARK_NODE_ITER_ENTER, AMARK_NODE_CODE_BLOCK, "int x;\n"},
            {table, AMARK_NODE_ITER_ENTER, AMARK_NODE_EXTENSION, "table"},
            {row1, AMARK_NODE_ITER_ENTER, AMARK_NODE_EXTENSION, "table_row"},
            {amark_node_get_first_child(row1), AMARK_NODE_ITER_ENTER, AMARK_NODE_TEXT, "ab"},
            {row1, AMARK_NODE_ITER_EXIT, AMARK_NODE_EXTENSION, NULL},
            {row2, AMARK_NODE_ITER_ENTER, AMARK_NODE_EXTENSION, "table_row"},
            {row2, AMARK_NODE_ITER_EXIT, AMARK_NODE_EXTENSION, NULL},
            {table, AMARK_NODE_ITER_EXIT, AMARK_NODE_EXTENSION, NULL},
            {doc, AMARK_NODE_ITER_EXIT, AMARK_NODE_DOCUMENT, NULL},
        };

        scenario_check_walk(doc, expected, sizeof(expected) / sizeof(expected[0]));
    }

    /* 結合された並びは 1 つの text になり、正規化は付随データを変えない */
    NAOS_CHECK_PTR_EQ(amark_node_get_first_child(row1), amark_node_get_last_child(row1));
    NAOS_CHECK_NULL(amark_node_get_first_child(row2));
    NAOS_CHECK_PTR_EQ(row_data, amark_node_get_data(row1));
    NAOS_CHECK_UINT_EQ(0u, context.destroyed);

    /* 破棄: 根の破棄で付随データが 1 回だけ後片付けされ、レジストリの破棄後に生存ブロックが残らない */
    amark_node_destroy(doc);
    NAOS_CHECK_UINT_EQ(1u, context.destroyed);
    NAOS_CHECK_UINT_LT(0u, test_allocator_stats().live); /* レジストリの分が残る */
    amark_node_registry_destroy(registry);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

/* clang-format off */
NAOS_SUITE(node_iter,
    NAOS_SET_TEARDOWN(restore_default),
    NAOS_ADD_TEST(representative_tree_sequence),
    NAOS_ADD_TEST(empty_containers_and_leaves),
    NAOS_ADD_TEST(extension_leaf_and_container),
    NAOS_ADD_TEST(null_arguments),
    NAOS_ADD_TEST(subtree_limited),
    NAOS_ADD_TEST(reset_valid_positions),
    NAOS_ADD_TEST(reset_errors_change_nothing),
    NAOS_ADD_TEST(iteration_allocates_nothing),
    NAOS_ADD_TEST(edit_values_during_iteration),
    NAOS_ADD_TEST(remove_unreported_siblings),
    NAOS_ADD_TEST(add_children_at_enter),
    NAOS_ADD_TEST(add_child_to_empty_container_at_enter),
    NAOS_ADD_TEST(detach_current_then_reset),
    NAOS_ADD_TEST(detach_current_root_keeps_iterating),
    NAOS_ADD_TEST(destroy_current_procedure),
    NAOS_ADD_TEST(deep_tree_iteration),
    NAOS_ADD_TEST(document_tree_scenario)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(node_iter)
)
/* clang-format on */
