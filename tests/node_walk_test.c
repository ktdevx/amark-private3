/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * 振り分け走査（非公開の src/tree/node_walk.c をこの実行ファイルへ直接コンパイルする）のテスト。
 * 表の追加・置き換え・解除・未登録の解除・検索・空の種別の拒否、伸長の失敗注入で表が変わらないこと、
 * 訪問のイベント列がイテレータと一致し葉は ENTER の 1 回だけであること、子の省略（子孫を訪問せず、
 * 同じノードの EXIT をちょうど 1 回訪問し、続く兄弟と親の EXIT は通常どおり）、葉と EXIT での省略の
 * 要求の無視、ENTER と省略後の EXIT での訪問関数のエラーの伝播、走査中の確保回数 0 と後片付け後の
 * 生存ブロック数 0、深さ 10 万段の走査を確かめる。木はテスト内で公開 API だけで組む。
 */
#include "tree/node_walk.h"

#include <amark/error.h>
#include <amark/memory.h>
#include <amark/node.h>
#include <amark/node_iter.h>
#include <amark/node_kind.h>

#include <naos/naos.h>

#include "support/test_allocator.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* 深い木のテストの段数 */
#define TEST_DEEP_DEPTH 100000u

/* 記録できる訪問の最大数 */
#define TEST_MAX_VISITS 64u

/* 1 回の訪問の記録 */
typedef struct test_visit
{
    const amark_node_t *node;
    amark_node_iter_event_t event;
    const amark_node_walk_hook_t *hook;
    bool skip_children; /* 訪問関数に渡された時点の skip_children */
} test_visit_t;

/* 訪問関数の文脈 */
typedef struct test_ctx
{
    test_visit_t visits[TEST_MAX_VISITS];
    size_t count;                  /* 訪問の回数（記録しきれない分も数える） */
    const amark_node_t *skip_node; /* この節点の訪問で子の省略を要求する（NULL なら要求しない） */
    bool skip_every_exit;          /* EXIT のたびに子の省略を要求する */
    const amark_node_t *error_node; /* この節点の error_event の訪問でエラーを返す */
    amark_node_iter_event_t error_event;
    amark_error_t error;
} test_ctx_t;

/* 期待する 1 イベント */
typedef struct test_step
{
    const amark_node_t *node;
    amark_node_iter_event_t event;
} test_step_t;

/* 表に登録する処理（中身は使わない。ポインタの同一性だけを見る） */
static void test_hook_a(void)
{
}

static void test_hook_b(void)
{
}

static void test_ctx_init(test_ctx_t *ctx)
{
    ctx->count = 0;
    ctx->skip_node = NULL;
    ctx->skip_every_exit = false;
    ctx->error_node = NULL;
    ctx->error_event = AMARK_NODE_ITER_NONE;
    ctx->error = AMARK_OK;
}

static amark_error_t test_visit(void *data, amark_node_walk_t *walk, const amark_node_walk_hook_t *hook)
{
    test_ctx_t *ctx = (test_ctx_t *)data;

    if (ctx->count < TEST_MAX_VISITS)
    {
        ctx->visits[ctx->count].node = walk->node;
        ctx->visits[ctx->count].event = walk->event;
        ctx->visits[ctx->count].hook = hook;
        ctx->visits[ctx->count].skip_children = walk->skip_children;
    }

    ctx->count++;

    if ((walk->node == ctx->skip_node) || (ctx->skip_every_exit && (walk->event == AMARK_NODE_ITER_EXIT)))
    {
        amark_node_walk_skip_children(walk);
    }

    if ((walk->node == ctx->error_node) && (walk->event == ctx->error_event))
    {
        return ctx->error;
    }

    return AMARK_OK;
}

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

static const amark_node_kind_t *test_kind(amark_node_type_t type)
{
    return amark_node_kind_get_standard(type);
}

/* 記録した訪問が expected と一致することを確かめる */
static void test_check_visits(const test_ctx_t *ctx, const test_step_t *expected, size_t count)
{
    size_t i;

    NAOS_REQUIRE_UINT_EQ(count, ctx->count);
    for (i = 0; i < count; i++)
    {
        NAOS_CHECK_PTR_EQ(expected[i].node, ctx->visits[i].node);
        NAOS_CHECK_INT_EQ(expected[i].event, ctx->visits[i].event);
        NAOS_CHECK_FALSE(ctx->visits[i].skip_children);
    }
}

/*
 * 試験用の木:
 *   doc
 *     block_quote
 *       paragraph
 *         text
 *     paragraph2
 *       text2
 *     list
 *       item
 *         paragraph3
 *     thematic_break
 */
typedef struct test_tree
{
    amark_node_t *doc;
    amark_node_t *quote;
    amark_node_t *paragraph;
    amark_node_t *text;
    amark_node_t *paragraph2;
    amark_node_t *text2;
    amark_node_t *list;
    amark_node_t *item;
    amark_node_t *paragraph3;
    amark_node_t *thematic_break;
} test_tree_t;

static void test_tree_build(test_tree_t *tree)
{
    tree->doc = test_node(AMARK_NODE_DOCUMENT);
    tree->quote = test_child(tree->doc, AMARK_NODE_BLOCK_QUOTE);
    tree->paragraph = test_child(tree->quote, AMARK_NODE_PARAGRAPH);
    tree->text = test_child(tree->paragraph, AMARK_NODE_TEXT);
    tree->paragraph2 = test_child(tree->doc, AMARK_NODE_PARAGRAPH);
    tree->text2 = test_child(tree->paragraph2, AMARK_NODE_TEXT);
    tree->list = test_child(tree->doc, AMARK_NODE_LIST);
    tree->item = test_child(tree->list, AMARK_NODE_ITEM);
    tree->paragraph3 = test_child(tree->item, AMARK_NODE_PARAGRAPH);
    tree->thematic_break = test_child(tree->doc, AMARK_NODE_THEMATIC_BREAK);
}

NAOS_TEARDOWN(restore_default)
{
    test_allocator_uninstall();
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_allocator_set(NULL));
}

NAOS_TEST(table_set_and_find)
{
    amark_node_walk_table_t table;
    const amark_node_kind_t *paragraph = test_kind(AMARK_NODE_PARAGRAPH);
    const amark_node_kind_t *text = test_kind(AMARK_NODE_TEXT);
    const amark_node_kind_t *heading = test_kind(AMARK_NODE_HEADING);
    const amark_node_walk_hook_t *hook;
    int data_a = 0;
    int data_b = 0;

    test_allocator_install();
    amark_node_walk_table_init(&table);
    NAOS_CHECK_NULL(table.items);
    NAOS_CHECK_UINT_EQ(0u, table.len);
    NAOS_CHECK_UINT_EQ(0u, table.cap);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().allocations);
    NAOS_CHECK_NULL(amark_node_walk_table_find(&table, paragraph));

    /* 追加 */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_table_set(&table, paragraph, test_hook_a, &data_a));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_table_set(&table, text, test_hook_b, &data_b));
    NAOS_CHECK_UINT_EQ(2u, table.len);
    hook = amark_node_walk_table_find(&table, paragraph);
    NAOS_REQUIRE_NOT_NULL(hook);
    NAOS_CHECK_PTR_EQ(paragraph, hook->kind);
    NAOS_CHECK_TRUE(hook->fn == test_hook_a);
    NAOS_CHECK_PTR_EQ(&data_a, hook->user_data);
    hook = amark_node_walk_table_find(&table, text);
    NAOS_REQUIRE_NOT_NULL(hook);
    NAOS_CHECK_PTR_EQ(text, hook->kind);
    NAOS_CHECK_TRUE(hook->fn == test_hook_b);
    NAOS_CHECK_PTR_EQ(&data_b, hook->user_data);
    NAOS_CHECK_NULL(amark_node_walk_table_find(&table, heading));

    /* 置き換え（件数は変わらない） */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_table_set(&table, paragraph, test_hook_b, NULL));
    NAOS_CHECK_UINT_EQ(2u, table.len);
    hook = amark_node_walk_table_find(&table, paragraph);
    NAOS_REQUIRE_NOT_NULL(hook);
    NAOS_CHECK_TRUE(hook->fn == test_hook_b);
    NAOS_CHECK_NULL(hook->user_data);

    /* 未登録の解除は何もしない */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_table_set(&table, heading, NULL, &data_a));
    NAOS_CHECK_UINT_EQ(2u, table.len);
    NAOS_CHECK_NULL(amark_node_walk_table_find(&table, heading));

    /* 解除 */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_table_set(&table, paragraph, NULL, NULL));
    NAOS_CHECK_UINT_EQ(1u, table.len);
    NAOS_CHECK_NULL(amark_node_walk_table_find(&table, paragraph));
    hook = amark_node_walk_table_find(&table, text);
    NAOS_REQUIRE_NOT_NULL(hook);
    NAOS_CHECK_TRUE(hook->fn == test_hook_b);
    NAOS_CHECK_PTR_EQ(&data_b, hook->user_data);

    /* 最後の 1 件の解除。縮小の再確保はしない */
    {
        size_t cap = table.cap;
        amark_node_walk_hook_t *items = table.items;

        NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_table_set(&table, text, NULL, NULL));
        NAOS_CHECK_UINT_EQ(0u, table.len);
        NAOS_CHECK_UINT_EQ(cap, table.cap);
        NAOS_CHECK_PTR_EQ(items, table.items);
        NAOS_CHECK_NULL(amark_node_walk_table_find(&table, text));
    }

    /* 空の種別・空の表の拒否と、空の引数の検索 */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_walk_table_set(&table, NULL, test_hook_a, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_walk_table_set(&table, NULL, NULL, NULL));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_walk_table_set(NULL, text, test_hook_a, NULL));
    NAOS_CHECK_UINT_EQ(0u, table.len);
    NAOS_CHECK_NULL(amark_node_walk_table_find(NULL, text));
    NAOS_CHECK_NULL(amark_node_walk_table_find(&table, NULL));

    amark_node_walk_table_deinit(&table);
    NAOS_CHECK_NULL(table.items);
    NAOS_CHECK_UINT_EQ(0u, table.len);
    NAOS_CHECK_UINT_EQ(0u, table.cap);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);

    /* NULL は何もしない */
    amark_node_walk_table_init(NULL);
    amark_node_walk_table_deinit(NULL);
}

NAOS_TEST(table_grows_and_releases)
{
    amark_node_walk_table_t table;
    amark_node_type_t type;
    size_t count = 0;

    test_allocator_install();
    amark_node_walk_table_init(&table);

    for (type = AMARK_NODE_DOCUMENT; type <= AMARK_NODE_IMAGE; type = (amark_node_type_t)(type + 1))
    {
        const amark_node_kind_t *kind = test_kind(type);

        if (kind == NULL)
        {
            continue;
        }

        NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_walk_table_set(&table, kind, test_hook_a, NULL));
        count++;
    }

    NAOS_CHECK_UINT_GT(count, 8u);
    NAOS_CHECK_UINT_EQ(count, table.len);
    NAOS_CHECK_UINT_GE(table.cap, table.len);

    for (type = AMARK_NODE_DOCUMENT; type <= AMARK_NODE_IMAGE; type = (amark_node_type_t)(type + 1))
    {
        const amark_node_kind_t *kind = test_kind(type);
        const amark_node_walk_hook_t *hook;

        if (kind == NULL)
        {
            continue;
        }

        hook = amark_node_walk_table_find(&table, kind);
        NAOS_REQUIRE_NOT_NULL(hook);
        NAOS_CHECK_PTR_EQ(kind, hook->kind);
    }

    amark_node_walk_table_deinit(&table);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(table_growth_failure_changes_nothing)
{
    amark_node_walk_table_t table;
    const amark_node_kind_t *kinds[5];
    amark_node_walk_hook_t *items;
    size_t i;
    int data = 0;

    kinds[0] = test_kind(AMARK_NODE_DOCUMENT);
    kinds[1] = test_kind(AMARK_NODE_PARAGRAPH);
    kinds[2] = test_kind(AMARK_NODE_TEXT);
    kinds[3] = test_kind(AMARK_NODE_HEADING);
    kinds[4] = test_kind(AMARK_NODE_LIST);

    test_allocator_install();
    amark_node_walk_table_init(&table);

    /* 最初の確保の失敗: 空の表のまま */
    test_allocator_fail_after(0);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NO_MEMORY, amark_node_walk_table_set(&table, kinds[0], test_hook_a, &data));
    NAOS_CHECK_NULL(table.items);
    NAOS_CHECK_UINT_EQ(0u, table.len);
    NAOS_CHECK_UINT_EQ(0u, table.cap);
    NAOS_CHECK_NULL(amark_node_walk_table_find(&table, kinds[0]));
    test_allocator_never_fail();

    /* 初期容量まで埋める */
    for (i = 0; i < 4u; i++)
    {
        NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_walk_table_set(&table, kinds[i], test_hook_a, &data));
    }

    NAOS_CHECK_UINT_EQ(4u, table.len);
    NAOS_CHECK_UINT_EQ(4u, table.cap);
    items = table.items;

    /* 伸長の失敗: 表は変わらない */
    test_allocator_fail_after(0);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NO_MEMORY, amark_node_walk_table_set(&table, kinds[4], test_hook_b, NULL));
    NAOS_CHECK_PTR_EQ(items, table.items);
    NAOS_CHECK_UINT_EQ(4u, table.len);
    NAOS_CHECK_UINT_EQ(4u, table.cap);
    NAOS_CHECK_NULL(amark_node_walk_table_find(&table, kinds[4]));
    for (i = 0; i < 4u; i++)
    {
        const amark_node_walk_hook_t *hook = amark_node_walk_table_find(&table, kinds[i]);

        NAOS_REQUIRE_NOT_NULL(hook);
        NAOS_CHECK_TRUE(hook->fn == test_hook_a);
        NAOS_CHECK_PTR_EQ(&data, hook->user_data);
    }

    /* 置き換え・解除・未登録の解除は確保しないため、確保の失敗中でも成功する */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_table_set(&table, kinds[0], test_hook_b, NULL));
    NAOS_CHECK_TRUE(amark_node_walk_table_find(&table, kinds[0])->fn == test_hook_b);
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_table_set(&table, kinds[4], NULL, NULL));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_table_set(&table, kinds[1], NULL, NULL));
    NAOS_CHECK_UINT_EQ(3u, table.len);
    NAOS_CHECK_UINT_EQ(2u, test_allocator_stats().failures);
    test_allocator_never_fail();

    /* 回復後は伸長できる */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_table_set(&table, kinds[1], test_hook_a, NULL));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_table_set(&table, kinds[4], test_hook_a, NULL));
    NAOS_CHECK_UINT_EQ(5u, table.len);
    NAOS_CHECK_UINT_EQ(8u, table.cap);

    amark_node_walk_table_deinit(&table);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(run_matches_iterator)
{
    test_tree_t tree;
    amark_node_walk_table_t table;
    amark_node_walk_t walk;
    amark_node_iter_t iter;
    amark_node_iter_event_t event;
    test_ctx_t ctx;
    int data = 0;
    size_t i = 0;

    test_tree_build(&tree);
    amark_node_walk_table_init(&table);
    NAOS_REQUIRE_INT_EQ(AMARK_OK,
                        amark_node_walk_table_set(&table, test_kind(AMARK_NODE_PARAGRAPH), test_hook_a, &data));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_walk_table_set(&table, test_kind(AMARK_NODE_TEXT), test_hook_b, NULL));

    test_ctx_init(&ctx);
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_run(&walk, tree.doc, &table, test_visit, &ctx));

    {
        const test_step_t expected[] = {
            {tree.doc, AMARK_NODE_ITER_ENTER},        {tree.quote, AMARK_NODE_ITER_ENTER},
            {tree.paragraph, AMARK_NODE_ITER_ENTER},  {tree.text, AMARK_NODE_ITER_ENTER},
            {tree.paragraph, AMARK_NODE_ITER_EXIT},   {tree.quote, AMARK_NODE_ITER_EXIT},
            {tree.paragraph2, AMARK_NODE_ITER_ENTER}, {tree.text2, AMARK_NODE_ITER_ENTER},
            {tree.paragraph2, AMARK_NODE_ITER_EXIT},  {tree.list, AMARK_NODE_ITER_ENTER},
            {tree.item, AMARK_NODE_ITER_ENTER},       {tree.paragraph3, AMARK_NODE_ITER_ENTER},
            {tree.paragraph3, AMARK_NODE_ITER_EXIT},  {tree.item, AMARK_NODE_ITER_EXIT},
            {tree.list, AMARK_NODE_ITER_EXIT},        {tree.thematic_break, AMARK_NODE_ITER_ENTER},
            {tree.doc, AMARK_NODE_ITER_EXIT},
        };

        test_check_visits(&ctx, expected, sizeof(expected) / sizeof(expected[0]));
    }

    /* イテレータの報告と 1 対 1 に一致し、項目は種別で表を引いたもの */
    amark_node_iter_init(&iter, tree.doc);
    while ((event = amark_node_iter_next(&iter)) != AMARK_NODE_ITER_DONE)
    {
        const amark_node_t *node = amark_node_iter_get_node(&iter);

        NAOS_REQUIRE_UINT_LT(i, ctx.count);
        NAOS_CHECK_PTR_EQ(node, ctx.visits[i].node);
        NAOS_CHECK_INT_EQ(event, ctx.visits[i].event);
        NAOS_CHECK_PTR_EQ(amark_node_walk_table_find(&table, amark_node_get_kind(node)), ctx.visits[i].hook);
        i++;
    }

    NAOS_CHECK_UINT_EQ(ctx.count, i);
    NAOS_CHECK_NULL(ctx.visits[0].hook);
    NAOS_REQUIRE_NOT_NULL(ctx.visits[2].hook);
    NAOS_CHECK_TRUE(ctx.visits[2].hook->fn == test_hook_a);
    NAOS_CHECK_PTR_EQ(&data, ctx.visits[2].hook->user_data);
    NAOS_REQUIRE_NOT_NULL(ctx.visits[3].hook);
    NAOS_CHECK_TRUE(ctx.visits[3].hook->fn == test_hook_b);

    /* 空の表でも走査でき、項目はすべて NULL */
    amark_node_walk_table_deinit(&table);
    test_ctx_init(&ctx);
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_run(&walk, tree.paragraph2, &table, test_visit, &ctx));
    {
        const test_step_t expected[] = {
            {tree.paragraph2, AMARK_NODE_ITER_ENTER},
            {tree.text2, AMARK_NODE_ITER_ENTER},
            {tree.paragraph2, AMARK_NODE_ITER_EXIT},
        };

        test_check_visits(&ctx, expected, sizeof(expected) / sizeof(expected[0]));
    }

    NAOS_CHECK_NULL(ctx.visits[0].hook);
    NAOS_CHECK_NULL(ctx.visits[1].hook);
    NAOS_CHECK_NULL(ctx.visits[2].hook);

    /* 起点が葉なら ENTER の 1 回だけ */
    test_ctx_init(&ctx);
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_run(&walk, tree.text, &table, test_visit, &ctx));
    {
        const test_step_t expected[] = {
            {tree.text, AMARK_NODE_ITER_ENTER},
        };

        test_check_visits(&ctx, expected, sizeof(expected) / sizeof(expected[0]));
    }

    amark_node_destroy(tree.doc);
}

NAOS_TEST(run_null_arguments)
{
    amark_node_walk_table_t table;
    amark_node_walk_t walk;
    amark_node_t *doc = test_node(AMARK_NODE_DOCUMENT);
    test_ctx_t ctx;

    amark_node_walk_table_init(&table);
    test_ctx_init(&ctx);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_walk_run(NULL, doc, &table, test_visit, &ctx));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_walk_run(&walk, NULL, &table, test_visit, &ctx));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_walk_run(&walk, doc, NULL, test_visit, &ctx));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_walk_run(&walk, doc, &table, NULL, &ctx));
    NAOS_CHECK_UINT_EQ(0u, ctx.count);

    /* 引数がそろえば走査する */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_run(&walk, doc, &table, test_visit, &ctx));
    NAOS_CHECK_UINT_EQ(2u, ctx.count);

    amark_node_walk_skip_children(NULL);
    amark_node_destroy(doc);
}

NAOS_TEST(skip_children_visits_exit_once)
{
    test_tree_t tree;
    amark_node_walk_table_t table;
    amark_node_walk_t walk;
    test_ctx_t ctx;
    int data = 0;

    test_tree_build(&tree);
    amark_node_walk_table_init(&table);
    NAOS_REQUIRE_INT_EQ(AMARK_OK,
                        amark_node_walk_table_set(&table, test_kind(AMARK_NODE_BLOCK_QUOTE), test_hook_a, &data));

    /* 最初の子（block_quote）で省略: 子孫を訪問せず、EXIT を 1 回、続く兄弟と doc の EXIT は通常どおり */
    test_ctx_init(&ctx);
    ctx.skip_node = tree.quote;
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_run(&walk, tree.doc, &table, test_visit, &ctx));
    {
        const test_step_t expected[] = {
            {tree.doc, AMARK_NODE_ITER_ENTER},
            {tree.quote, AMARK_NODE_ITER_ENTER},
            {tree.quote, AMARK_NODE_ITER_EXIT},
            {tree.paragraph2, AMARK_NODE_ITER_ENTER},
            {tree.text2, AMARK_NODE_ITER_ENTER},
            {tree.paragraph2, AMARK_NODE_ITER_EXIT},
            {tree.list, AMARK_NODE_ITER_ENTER},
            {tree.item, AMARK_NODE_ITER_ENTER},
            {tree.paragraph3, AMARK_NODE_ITER_ENTER},
            {tree.paragraph3, AMARK_NODE_ITER_EXIT},
            {tree.item, AMARK_NODE_ITER_EXIT},
            {tree.list, AMARK_NODE_ITER_EXIT},
            {tree.thematic_break, AMARK_NODE_ITER_ENTER},
            {tree.doc, AMARK_NODE_ITER_EXIT},
        };

        test_check_visits(&ctx, expected, sizeof(expected) / sizeof(expected[0]));
    }

    /* 省略後の EXIT には ENTER で引いたのと同じ項目を渡す */
    NAOS_REQUIRE_NOT_NULL(ctx.visits[1].hook);
    NAOS_CHECK_PTR_EQ(ctx.visits[1].hook, ctx.visits[2].hook);
    NAOS_CHECK_TRUE(ctx.visits[2].hook->fn == test_hook_a);

    /* 入れ子のノード（item）で省略: 親（list、doc）の EXIT は通常どおり */
    test_ctx_init(&ctx);
    ctx.skip_node = tree.item;
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_run(&walk, tree.doc, &table, test_visit, &ctx));
    {
        const test_step_t expected[] = {
            {tree.doc, AMARK_NODE_ITER_ENTER},        {tree.quote, AMARK_NODE_ITER_ENTER},
            {tree.paragraph, AMARK_NODE_ITER_ENTER},  {tree.text, AMARK_NODE_ITER_ENTER},
            {tree.paragraph, AMARK_NODE_ITER_EXIT},   {tree.quote, AMARK_NODE_ITER_EXIT},
            {tree.paragraph2, AMARK_NODE_ITER_ENTER}, {tree.text2, AMARK_NODE_ITER_ENTER},
            {tree.paragraph2, AMARK_NODE_ITER_EXIT},  {tree.list, AMARK_NODE_ITER_ENTER},
            {tree.item, AMARK_NODE_ITER_ENTER},       {tree.item, AMARK_NODE_ITER_EXIT},
            {tree.list, AMARK_NODE_ITER_EXIT},        {tree.thematic_break, AMARK_NODE_ITER_ENTER},
            {tree.doc, AMARK_NODE_ITER_EXIT},
        };

        test_check_visits(&ctx, expected, sizeof(expected) / sizeof(expected[0]));
    }

    /* 空のコンテナ（子のない段落）で省略しても ENTER・EXIT はちょうど 1 回ずつ */
    {
        amark_node_t *empty = test_child(tree.quote, AMARK_NODE_PARAGRAPH);

        test_ctx_init(&ctx);
        ctx.skip_node = empty;
        NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_run(&walk, tree.quote, &table, test_visit, &ctx));
        {
            const test_step_t expected[] = {
                {tree.quote, AMARK_NODE_ITER_ENTER}, {tree.paragraph, AMARK_NODE_ITER_ENTER},
                {tree.text, AMARK_NODE_ITER_ENTER},  {tree.paragraph, AMARK_NODE_ITER_EXIT},
                {empty, AMARK_NODE_ITER_ENTER},      {empty, AMARK_NODE_ITER_EXIT},
                {tree.quote, AMARK_NODE_ITER_EXIT},
            };

            test_check_visits(&ctx, expected, sizeof(expected) / sizeof(expected[0]));
        }
    }

    /* 起点で省略: 起点の ENTER と EXIT だけ */
    test_ctx_init(&ctx);
    ctx.skip_node = tree.doc;
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_run(&walk, tree.doc, &table, test_visit, &ctx));
    {
        const test_step_t expected[] = {
            {tree.doc, AMARK_NODE_ITER_ENTER},
            {tree.doc, AMARK_NODE_ITER_EXIT},
        };

        test_check_visits(&ctx, expected, sizeof(expected) / sizeof(expected[0]));
    }

    amark_node_walk_table_deinit(&table);
    amark_node_destroy(tree.doc);
}

NAOS_TEST(skip_request_ignored_on_leaf_and_exit)
{
    test_tree_t tree;
    amark_node_walk_table_t table;
    amark_node_walk_t walk;
    test_ctx_t ctx;
    test_step_t full[17];
    size_t count;

    test_tree_build(&tree);
    amark_node_walk_table_init(&table);

    /* 省略なしの完全なイベント列 */
    {
        const test_step_t steps[17] = {
            {tree.doc, AMARK_NODE_ITER_ENTER},        {tree.quote, AMARK_NODE_ITER_ENTER},
            {tree.paragraph, AMARK_NODE_ITER_ENTER},  {tree.text, AMARK_NODE_ITER_ENTER},
            {tree.paragraph, AMARK_NODE_ITER_EXIT},   {tree.quote, AMARK_NODE_ITER_EXIT},
            {tree.paragraph2, AMARK_NODE_ITER_ENTER}, {tree.text2, AMARK_NODE_ITER_ENTER},
            {tree.paragraph2, AMARK_NODE_ITER_EXIT},  {tree.list, AMARK_NODE_ITER_ENTER},
            {tree.item, AMARK_NODE_ITER_ENTER},       {tree.paragraph3, AMARK_NODE_ITER_ENTER},
            {tree.paragraph3, AMARK_NODE_ITER_EXIT},  {tree.item, AMARK_NODE_ITER_EXIT},
            {tree.list, AMARK_NODE_ITER_EXIT},        {tree.thematic_break, AMARK_NODE_ITER_ENTER},
            {tree.doc, AMARK_NODE_ITER_EXIT},
        };

        memcpy(full, steps, sizeof(full));
        count = sizeof(steps) / sizeof(steps[0]);
    }

    /* 葉（text）での要求は無視する */
    test_ctx_init(&ctx);
    ctx.skip_node = tree.text;
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_run(&walk, tree.doc, &table, test_visit, &ctx));
    test_check_visits(&ctx, full, count);

    /* 葉（thematic_break）での要求も無視する */
    test_ctx_init(&ctx);
    ctx.skip_node = tree.thematic_break;
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_run(&walk, tree.doc, &table, test_visit, &ctx));
    test_check_visits(&ctx, full, count);

    /* EXIT のたびの要求は無視する */
    test_ctx_init(&ctx);
    ctx.skip_every_exit = true;
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_run(&walk, tree.doc, &table, test_visit, &ctx));
    test_check_visits(&ctx, full, count);

    /* 省略後の EXIT での再度の要求も無視する（EXIT は 1 回だけ） */
    test_ctx_init(&ctx);
    ctx.skip_node = tree.quote;
    ctx.skip_every_exit = true;
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_run(&walk, tree.doc, &table, test_visit, &ctx));
    NAOS_CHECK_UINT_EQ(14u, ctx.count);
    NAOS_CHECK_PTR_EQ(tree.quote, ctx.visits[2].node);
    NAOS_CHECK_INT_EQ(AMARK_NODE_ITER_EXIT, ctx.visits[2].event);
    NAOS_CHECK_PTR_EQ(tree.paragraph2, ctx.visits[3].node);
    NAOS_CHECK_INT_EQ(AMARK_NODE_ITER_ENTER, ctx.visits[3].event);

    amark_node_walk_table_deinit(&table);
    amark_node_destroy(tree.doc);
}

NAOS_TEST(visitor_error_stops_walk)
{
    test_tree_t tree;
    amark_node_walk_table_t table;
    amark_node_walk_t walk;
    test_ctx_t ctx;

    test_tree_build(&tree);
    amark_node_walk_table_init(&table);

    /* ENTER でのエラー: 直ちに止めてそのまま返す */
    test_ctx_init(&ctx);
    ctx.error_node = tree.paragraph2;
    ctx.error_event = AMARK_NODE_ITER_ENTER;
    ctx.error = AMARK_ERROR_INVALID_CHILD;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CHILD, amark_node_walk_run(&walk, tree.doc, &table, test_visit, &ctx));
    NAOS_CHECK_UINT_EQ(7u, ctx.count);
    NAOS_CHECK_PTR_EQ(tree.paragraph2, ctx.visits[6].node);
    NAOS_CHECK_INT_EQ(AMARK_NODE_ITER_ENTER, ctx.visits[6].event);

    /* 省略を要求した ENTER でのエラー: EXIT を訪問せずに返す */
    test_ctx_init(&ctx);
    ctx.skip_node = tree.quote;
    ctx.error_node = tree.quote;
    ctx.error_event = AMARK_NODE_ITER_ENTER;
    ctx.error = AMARK_ERROR_NO_MEMORY;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NO_MEMORY, amark_node_walk_run(&walk, tree.doc, &table, test_visit, &ctx));
    NAOS_CHECK_UINT_EQ(2u, ctx.count);

    /* 省略後の EXIT でのエラー: そのまま返し、以後を訪問しない */
    test_ctx_init(&ctx);
    ctx.skip_node = tree.quote;
    ctx.error_node = tree.quote;
    ctx.error_event = AMARK_NODE_ITER_EXIT;
    ctx.error = AMARK_ERROR_OVERFLOW;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_OVERFLOW, amark_node_walk_run(&walk, tree.doc, &table, test_visit, &ctx));
    {
        const test_step_t expected[] = {
            {tree.doc, AMARK_NODE_ITER_ENTER},
            {tree.quote, AMARK_NODE_ITER_ENTER},
            {tree.quote, AMARK_NODE_ITER_EXIT},
        };

        test_check_visits(&ctx, expected, sizeof(expected) / sizeof(expected[0]));
    }

    /* 通常の EXIT でのエラー */
    test_ctx_init(&ctx);
    ctx.error_node = tree.list;
    ctx.error_event = AMARK_NODE_ITER_EXIT;
    ctx.error = AMARK_ERROR_INVALID_UTF8;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_UTF8, amark_node_walk_run(&walk, tree.doc, &table, test_visit, &ctx));
    NAOS_CHECK_UINT_EQ(15u, ctx.count);

    /* 起点の最後の EXIT でのエラー */
    test_ctx_init(&ctx);
    ctx.error_node = tree.doc;
    ctx.error_event = AMARK_NODE_ITER_EXIT;
    ctx.error = AMARK_ERROR_ALREADY_EXISTS;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_ALREADY_EXISTS, amark_node_walk_run(&walk, tree.doc, &table, test_visit, &ctx));
    NAOS_CHECK_UINT_EQ(17u, ctx.count);

    amark_node_walk_table_deinit(&table);
    amark_node_destroy(tree.doc);
}

NAOS_TEST(run_allocates_nothing)
{
    test_tree_t tree;
    amark_node_walk_table_t table;
    amark_node_walk_t walk;
    test_ctx_t ctx;
    test_allocator_stats_t before;
    test_allocator_stats_t after;

    test_allocator_install();
    test_tree_build(&tree);
    amark_node_walk_table_init(&table);
    NAOS_REQUIRE_INT_EQ(AMARK_OK,
                        amark_node_walk_table_set(&table, test_kind(AMARK_NODE_PARAGRAPH), test_hook_a, NULL));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_walk_table_set(&table, test_kind(AMARK_NODE_ITEM), test_hook_b, NULL));

    before = test_allocator_stats();

    test_ctx_init(&ctx);
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_run(&walk, tree.doc, &table, test_visit, &ctx));
    NAOS_CHECK_UINT_EQ(17u, ctx.count);

    test_ctx_init(&ctx);
    ctx.skip_node = tree.item;
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_run(&walk, tree.doc, &table, test_visit, &ctx));
    NAOS_CHECK_UINT_EQ(15u, ctx.count);

    /* 確保をすべて失敗させても走査は進む */
    test_allocator_fail_after(0);
    test_ctx_init(&ctx);
    ctx.skip_node = tree.quote;
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_run(&walk, tree.doc, &table, test_visit, &ctx));
    NAOS_CHECK_UINT_EQ(14u, ctx.count);
    test_allocator_never_fail();

    after = test_allocator_stats();
    NAOS_CHECK_UINT_EQ(before.allocations, after.allocations);
    NAOS_CHECK_UINT_EQ(before.deallocations, after.deallocations);
    NAOS_CHECK_UINT_EQ(before.failures, after.failures);

    amark_node_walk_table_deinit(&table);
    amark_node_destroy(tree.doc);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

/* 深い木の訪問関数の文脈（記録せずに数える） */
typedef struct test_deep_ctx
{
    size_t enters;
    size_t exits;
    const amark_node_t *skip_node;
} test_deep_ctx_t;

static amark_error_t test_deep_visit(void *data, amark_node_walk_t *walk, const amark_node_walk_hook_t *hook)
{
    test_deep_ctx_t *ctx = (test_deep_ctx_t *)data;

    (void)hook;
    if (walk->event == AMARK_NODE_ITER_ENTER)
    {
        ctx->enters++;
        if (walk->node == ctx->skip_node)
        {
            amark_node_walk_skip_children(walk);
        }
    }
    else
    {
        ctx->exits++;
    }

    return AMARK_OK;
}

NAOS_TEST(deep_tree_walk)
{
    amark_node_t *doc;
    amark_node_t *current;
    amark_node_t *paragraph;
    amark_node_walk_table_t table;
    amark_node_walk_t walk;
    test_deep_ctx_t ctx;
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

    paragraph = test_child(current, AMARK_NODE_PARAGRAPH);
    (void)test_child(paragraph, AMARK_NODE_TEXT);

    amark_node_walk_table_init(&table);
    NAOS_REQUIRE_INT_EQ(AMARK_OK,
                        amark_node_walk_table_set(&table, test_kind(AMARK_NODE_BLOCK_QUOTE), test_hook_a, NULL));

    /* 省略なし: doc・引用 10 万段・段落は ENTER と EXIT、text は ENTER だけ */
    ctx.enters = 0;
    ctx.exits = 0;
    ctx.skip_node = NULL;
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_run(&walk, doc, &table, test_deep_visit, &ctx));
    NAOS_CHECK_UINT_EQ(TEST_DEEP_DEPTH + 3u, ctx.enters);
    NAOS_CHECK_UINT_EQ(TEST_DEEP_DEPTH + 2u, ctx.exits);

    /* 最深の段落で省略: text を訪問せず、残りの EXIT は通常どおり */
    ctx.enters = 0;
    ctx.exits = 0;
    ctx.skip_node = paragraph;
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_run(&walk, doc, &table, test_deep_visit, &ctx));
    NAOS_CHECK_UINT_EQ(TEST_DEEP_DEPTH + 2u, ctx.enters);
    NAOS_CHECK_UINT_EQ(TEST_DEEP_DEPTH + 2u, ctx.exits);

    /* 最上段の引用で省略: 10 万段の子孫を訪問しない */
    ctx.enters = 0;
    ctx.exits = 0;
    ctx.skip_node = amark_node_get_first_child(doc);
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_walk_run(&walk, doc, &table, test_deep_visit, &ctx));
    NAOS_CHECK_UINT_EQ(2u, ctx.enters);
    NAOS_CHECK_UINT_EQ(2u, ctx.exits);

    amark_node_walk_table_deinit(&table);
    amark_node_destroy(doc);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

/* clang-format off */
NAOS_SUITE(node_walk,
    NAOS_SET_TEARDOWN(restore_default),
    NAOS_ADD_TEST(table_set_and_find),
    NAOS_ADD_TEST(table_grows_and_releases),
    NAOS_ADD_TEST(table_growth_failure_changes_nothing),
    NAOS_ADD_TEST(run_matches_iterator),
    NAOS_ADD_TEST(run_null_arguments),
    NAOS_ADD_TEST(skip_children_visits_exit_once),
    NAOS_ADD_TEST(skip_request_ignored_on_leaf_and_exit),
    NAOS_ADD_TEST(visitor_error_stops_walk),
    NAOS_ADD_TEST(run_allocates_nothing),
    NAOS_ADD_TEST(deep_tree_walk)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(node_walk)
)
/* clang-format on */
