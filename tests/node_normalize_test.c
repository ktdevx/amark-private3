/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * 正規化（amark_node_normalize）のテスト。隣接する text の結合、空の text の除去、text 以外による区切り、
 * 強調の中の並び、起点が空の text の場合、冪等性、text 以外のノードの不変、確保失敗の網羅的な注入で
 * 各回の木が親子規則を満たし並びごとの連結内容が元と同じで後片付け後の生存ブロック数が 0 であること、
 * 深さ 10 万段の木の正規化が完了することを確かめる。
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

/* 木の要約文字列の容量 */
#define TEST_SUMMARY_CAPACITY 1024u

/* 失敗注入の回数の上限（無限ループの防止） */
#define TEST_FAIL_LIMIT 1000u

/* 木の要約文字列 */
typedef struct test_summary
{
    char text[TEST_SUMMARY_CAPACITY];
    size_t len;
} test_summary_t;

/* 標準種別のノードを生成する */
static amark_node_t *test_node(amark_node_type_t type)
{
    amark_node_t *node = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create(type, &node));
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

/* parent の末尾に child を追加する */
static void test_append(amark_node_t *parent, amark_node_t *child)
{
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_append_child(parent, child));
}

/* parent の末尾に内容 value の text を追加し、そのノードを返す */
static amark_node_t *test_append_text(amark_node_t *parent, const char *value)
{
    amark_node_t *text = test_text(value);

    test_append(parent, text);

    return text;
}

/* node の literal が expected と等しいことを確かめる */
static void test_check_literal(const amark_node_t *node, const char *expected)
{
    amark_slice_t value = amark_node_get_literal(node);

    NAOS_CHECK_MEM_EQ(expected, strlen(expected), value.data, value.len);
}

/* parent の子が children の count 個と順に一致し、リンクが整合することを確かめる */
static void test_check_children(const amark_node_t *parent, amark_node_t *const *children, size_t count)
{
    const amark_node_t *walk = amark_node_get_first_child(parent);
    size_t i;

    if (count == 0)
    {
        NAOS_CHECK_NULL(amark_node_get_first_child(parent));
        NAOS_CHECK_NULL(amark_node_get_last_child(parent));
        return;
    }

    NAOS_CHECK_PTR_EQ(children[count - 1], amark_node_get_last_child(parent));
    for (i = 0; i < count; i++)
    {
        NAOS_REQUIRE_NOT_NULL(walk);
        NAOS_CHECK_PTR_EQ(children[i], walk);
        NAOS_CHECK_PTR_EQ(parent, amark_node_get_parent(walk));
        NAOS_CHECK_PTR_EQ((i == 0) ? NULL : children[i - 1], amark_node_get_previous(walk));
        walk = amark_node_get_next(walk);
    }

    NAOS_CHECK_NULL(walk);
}

/*
 * node の部分木が親子規則とリンクの整合を満たすことを確かめる（小さな木だけに使う再帰）。
 * normalized が真なら、子孫に空の text と隣接する text がないことも確かめる
 */
static void test_check_tree(const amark_node_t *node, bool normalized)
{
    const amark_node_t *child = amark_node_get_first_child(node);
    const amark_node_t *previous = NULL;

    if (child == NULL)
    {
        NAOS_CHECK_NULL(amark_node_get_last_child(node));
        return;
    }

    NAOS_CHECK_NULL(amark_node_get_previous(child));
    while (child != NULL)
    {
        NAOS_CHECK_PTR_EQ(node, amark_node_get_parent(child));
        NAOS_CHECK_PTR_EQ(previous, amark_node_get_previous(child));
        NAOS_CHECK_TRUE(amark_node_kind_can_contain(amark_node_get_kind(node), amark_node_get_kind(child)));
        if (normalized && (amark_node_get_type(child) == AMARK_NODE_TEXT))
        {
            NAOS_CHECK_UINT_NE(0u, amark_node_get_literal(child).len);
            NAOS_CHECK_TRUE((previous == NULL) || (amark_node_get_type(previous) != AMARK_NODE_TEXT));
        }

        test_check_tree(child, normalized);
        previous = child;
        child = amark_node_get_next(child);
    }

    NAOS_CHECK_PTR_EQ(previous, amark_node_get_last_child(node));
}

/* 要約に文字列を追記する（値を持たない種別の {NULL, 0} も受け付ける） */
static void test_summary_put(test_summary_t *summary, const char *data, size_t len)
{
    NAOS_REQUIRE_UINT_LT((summary->len + len), TEST_SUMMARY_CAPACITY);
    if (len > 0)
    {
        memcpy(summary->text + summary->len, data, len);
    }

    summary->len += len;
    summary->text[summary->len] = '\0';
}

/*
 * node の子の並びを要約する（小さな木だけに使う再帰）。連続する text は内容を連結して 1 つの
 * "<...>" とし、連結が空の並びは書かない。text 以外は種別名と値の後に子の要約を "(...)" で書く
 */
static void test_summarize_children(const amark_node_t *node, test_summary_t *summary)
{
    const amark_node_t *child = amark_node_get_first_child(node);

    while (child != NULL)
    {
        if (amark_node_get_type(child) == AMARK_NODE_TEXT)
        {
            size_t start = summary->len;

            test_summary_put(summary, "<", 1);
            while ((child != NULL) && (amark_node_get_type(child) == AMARK_NODE_TEXT))
            {
                amark_slice_t value = amark_node_get_literal(child);

                test_summary_put(summary, value.data, value.len);
                child = amark_node_get_next(child);
            }

            if (summary->len == (start + 1))
            {
                summary->len = start;
                summary->text[start] = '\0';
            }
            else
            {
                test_summary_put(summary, ">", 1);
            }
        }
        else
        {
            amark_slice_t name = amark_node_kind_get_name(amark_node_get_kind(child));
            amark_slice_t literal = amark_node_get_literal(child);
            amark_slice_t url = amark_node_get_url(child);

            test_summary_put(summary, name.data, name.len);
            test_summary_put(summary, "[", 1);
            test_summary_put(summary, literal.data, literal.len);
            test_summary_put(summary, "|", 1);
            test_summary_put(summary, url.data, url.len);
            test_summary_put(summary, "](", 2);
            test_summarize_children(child, summary);
            test_summary_put(summary, ")", 1);
            child = amark_node_get_next(child);
        }
    }
}

/* root の子の並びの要約を作る */
static void test_summarize(const amark_node_t *root, test_summary_t *summary)
{
    summary->len = 0;
    summary->text[0] = '\0';
    test_summarize_children(root, summary);
}

/*
 * 失敗注入用の木を組む:
 * document > paragraph [a]["" ][b] link(url)[c][d] [e] code(x) [""] [f][g] emphasis([h][""][i] strong([j][k]) [""]) [l]
 *          > paragraph [m][n]
 */
static amark_node_t *test_build_failure_tree(void)
{
    amark_node_t *doc = test_node(AMARK_NODE_DOCUMENT);
    amark_node_t *p1 = test_node(AMARK_NODE_PARAGRAPH);
    amark_node_t *p2 = test_node(AMARK_NODE_PARAGRAPH);
    amark_node_t *link = test_node(AMARK_NODE_LINK);
    amark_node_t *code = test_node(AMARK_NODE_CODE);
    amark_node_t *emph = test_node(AMARK_NODE_EMPHASIS);
    amark_node_t *strong = test_node(AMARK_NODE_STRONG);

    test_append(doc, p1);
    test_append(doc, p2);
    test_append_text(p1, "a");
    test_append_text(p1, "");
    test_append_text(p1, "b");
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_set_url(link, amark_slice_from_cstr("u")));
    test_append(p1, link);
    test_append_text(link, "c");
    test_append_text(link, "d");
    test_append_text(p1, "e");
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_set_literal(code, amark_slice_from_cstr("x")));
    test_append(p1, code);
    test_append_text(p1, "");
    test_append_text(p1, "f");
    test_append_text(p1, "g");
    test_append(p1, emph);
    test_append_text(emph, "h");
    test_append_text(emph, "");
    test_append_text(emph, "i");
    test_append(emph, strong);
    test_append_text(strong, "j");
    test_append_text(strong, "k");
    test_append_text(emph, "");
    test_append_text(p1, "l");
    test_append_text(p2, "m");
    test_append_text(p2, "n");

    return doc;
}

NAOS_TEARDOWN(restore_default)
{
    test_allocator_uninstall();
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_allocator_set(NULL));
}

NAOS_TEST(null_root_is_invalid_argument)
{
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_normalize(NULL));
}

NAOS_TEST(merges_adjacent_text)
{
    amark_node_t *para;
    amark_node_t *first;

    test_allocator_install();
    para = test_node(AMARK_NODE_PARAGRAPH);
    first = test_append_text(para, "a");
    test_append_text(para, "bc");
    test_append_text(para, "d");

    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_normalize(para));
    test_check_children(para, &first, 1);
    test_check_literal(first, "abcd");
    test_check_tree(para, true);

    /* 段落 1 + text 1 + 文字列 1 */
    NAOS_CHECK_UINT_EQ(3u, test_allocator_stats().live);

    amark_node_destroy(para);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(removes_empty_text)
{
    amark_node_t *para;
    amark_node_t *first;
    amark_node_t *lone;

    test_allocator_install();

    /* [a][""][b] -> [ab] */
    para = test_node(AMARK_NODE_PARAGRAPH);
    first = test_append_text(para, "a");
    test_append_text(para, "");
    test_append_text(para, "b");
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_normalize(para));
    test_check_children(para, &first, 1);
    test_check_literal(first, "ab");
    amark_node_destroy(para);

    /* [""][x][""] -> [x]（残る 1 つは連結しない） */
    para = test_node(AMARK_NODE_PARAGRAPH);
    test_append_text(para, "");
    lone = test_append_text(para, "x");
    test_append_text(para, "");
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_normalize(para));
    test_check_children(para, &lone, 1);
    test_check_literal(lone, "x");
    amark_node_destroy(para);

    /* 空だけの並びはすべて取り除く */
    para = test_node(AMARK_NODE_PARAGRAPH);
    test_append_text(para, "");
    test_append_text(para, "");
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_normalize(para));
    test_check_children(para, NULL, 0);
    NAOS_CHECK_UINT_EQ(1u, test_allocator_stats().live);
    amark_node_destroy(para);

    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(non_text_separates_runs)
{
    amark_node_t *para;
    amark_node_t *a;
    amark_node_t *code;
    amark_node_t *c;
    amark_node_t *empty_code;
    amark_node_t *br;
    amark_node_t *f;
    amark_node_t *expected[6];

    test_allocator_install();
    para = test_node(AMARK_NODE_PARAGRAPH);
    a = test_append_text(para, "a");
    test_append_text(para, "b");
    code = test_node(AMARK_NODE_CODE);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_set_literal(code, amark_slice_from_cstr("x")));
    test_append(para, code);
    c = test_append_text(para, "c");
    test_append_text(para, "");
    test_append_text(para, "d");
    empty_code = test_node(AMARK_NODE_CODE);
    test_append(para, empty_code);
    test_append_text(para, "");
    br = test_node(AMARK_NODE_SOFT_BREAK);
    test_append(para, br);
    f = test_append_text(para, "f");

    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_normalize(para));
    expected[0] = a;
    expected[1] = code;
    expected[2] = c;
    expected[3] = empty_code;
    expected[4] = br;
    expected[5] = f;
    test_check_children(para, expected, 6);
    test_check_literal(a, "ab");
    test_check_literal(code, "x");
    test_check_literal(c, "cd");
    test_check_literal(empty_code, "");
    test_check_literal(f, "f");
    test_check_tree(para, true);

    amark_node_destroy(para);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(runs_inside_emphasis)
{
    amark_node_t *doc;
    amark_node_t *para;
    amark_node_t *emph;
    amark_node_t *strong;
    amark_node_t *a;
    amark_node_t *b;
    amark_node_t *d;
    amark_node_t *f;
    amark_node_t *expected[3];

    test_allocator_install();
    doc = test_node(AMARK_NODE_DOCUMENT);
    para = test_node(AMARK_NODE_PARAGRAPH);
    emph = test_node(AMARK_NODE_EMPHASIS);
    strong = test_node(AMARK_NODE_STRONG);
    test_append(doc, para);
    a = test_append_text(para, "a");
    test_append(para, emph);
    b = test_append_text(emph, "b");
    test_append_text(emph, "c");
    test_append(emph, strong);
    test_append_text(strong, "");
    d = test_append_text(strong, "d");
    test_append_text(strong, "");
    test_append_text(strong, "e");
    f = test_append_text(para, "f");
    test_append_text(para, "g");

    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_normalize(doc));
    expected[0] = a;
    expected[1] = emph;
    expected[2] = f;
    test_check_children(para, expected, 3);
    expected[0] = b;
    expected[1] = strong;
    test_check_children(emph, expected, 2);
    test_check_children(strong, &d, 1);
    test_check_literal(a, "a");
    test_check_literal(b, "bc");
    test_check_literal(d, "de");
    test_check_literal(f, "fg");
    test_check_tree(doc, true);

    amark_node_destroy(doc);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(root_is_not_removed)
{
    amark_node_t *text;
    amark_node_t *emph;

    test_allocator_install();

    /* 起点が空の text でも取り除かない */
    text = test_node(AMARK_NODE_TEXT);
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_normalize(text));
    NAOS_CHECK_INT_EQ(AMARK_NODE_TEXT, amark_node_get_type(text));
    test_check_literal(text, "");
    NAOS_CHECK_UINT_EQ(1u, test_allocator_stats().live);
    amark_node_destroy(text);

    /* 内容のある text の起点も変えない */
    text = test_text("t");
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_normalize(text));
    test_check_literal(text, "t");
    amark_node_destroy(text);

    /* 起点が子を持てば、その子の空の text は取り除く */
    emph = test_node(AMARK_NODE_EMPHASIS);
    test_append_text(emph, "");
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_normalize(emph));
    test_check_children(emph, NULL, 0);
    amark_node_destroy(emph);

    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(normalized_tree_is_unchanged)
{
    amark_node_t *doc;
    test_summary_t before;
    test_summary_t after;
    test_allocator_stats_t stats;

    test_allocator_install();
    doc = test_build_failure_tree();
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_normalize(doc));
    test_check_tree(doc, true);
    test_summarize(doc, &before);
    NAOS_CHECK_STR_EQ("paragraph[|](<ab>link[|u](<cd>)<e>code[x|]()<fg>emphasis[|](<hi>strong[|](<jk>))<l>)"
                      "paragraph[|](<mn>)",
                      before.text);

    /* 2 回目は確保も解放もせず、木を変えない */
    stats = test_allocator_stats();
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_normalize(doc));
    NAOS_CHECK_UINT_EQ(stats.allocations, test_allocator_stats().allocations);
    NAOS_CHECK_UINT_EQ(stats.deallocations, test_allocator_stats().deallocations);
    test_summarize(doc, &after);
    NAOS_CHECK_STR_EQ(before.text, after.text);

    amark_node_destroy(doc);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(other_nodes_are_unchanged)
{
    amark_node_t *doc;
    amark_node_t *heading;
    amark_node_t *list;
    amark_node_t *item;
    amark_node_t *code_block;
    amark_node_t *para;
    amark_node_t *link;
    amark_node_t *image;
    amark_node_t *html;
    amark_node_t *expected[4];

    test_allocator_install();
    doc = test_node(AMARK_NODE_DOCUMENT);
    heading = test_node(AMARK_NODE_HEADING);
    list = test_node(AMARK_NODE_LIST);
    item = test_node(AMARK_NODE_ITEM);
    code_block = test_node(AMARK_NODE_CODE_BLOCK);
    para = test_node(AMARK_NODE_PARAGRAPH);
    link = test_node(AMARK_NODE_LINK);
    image = test_node(AMARK_NODE_IMAGE);
    html = test_node(AMARK_NODE_HTML_INLINE);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_set_heading_level(heading, 3));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_set_list_type(list, AMARK_NODE_LIST_ORDERED));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_set_list_start(list, 7));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_set_list_tight(list, true));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_set_info(code_block, amark_slice_from_cstr("c")));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_set_literal(code_block, amark_slice_from_cstr("int x;")));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_set_url(link, amark_slice_from_cstr("https://a")));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_set_title(link, amark_slice_from_cstr("T")));
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_set_url(image, amark_slice_from_cstr("i.png")));
    test_append(doc, heading);
    test_append(doc, list);
    test_append(doc, code_block);
    test_append(doc, para);
    test_append(list, item);
    test_append_text(heading, "h");
    test_append_text(para, "");
    test_append(para, link);
    test_append(para, html);
    test_append(para, image);
    test_append_text(para, "");

    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_normalize(doc));
    expected[0] = heading;
    expected[1] = list;
    expected[2] = code_block;
    expected[3] = para;
    test_check_children(doc, expected, 4);
    test_check_children(list, &item, 1);
    test_check_children(item, NULL, 0);
    expected[0] = link;
    expected[1] = html;
    expected[2] = image;
    test_check_children(para, expected, 3);
    test_check_tree(doc, true);
    NAOS_CHECK_INT_EQ(3, amark_node_get_heading_level(heading));
    NAOS_CHECK_INT_EQ(AMARK_NODE_LIST_ORDERED, amark_node_get_list_type(list));
    NAOS_CHECK_UINT_EQ(7u, amark_node_get_list_start(list));
    NAOS_CHECK_TRUE(amark_node_get_list_tight(list));
    test_check_literal(code_block, "int x;");
    NAOS_CHECK_MEM_EQ("c", 1, amark_node_get_info(code_block).data, amark_node_get_info(code_block).len);
    NAOS_CHECK_MEM_EQ("https://a", 9, amark_node_get_url(link).data, amark_node_get_url(link).len);
    NAOS_CHECK_MEM_EQ("T", 1, amark_node_get_title(link).data, amark_node_get_title(link).len);
    NAOS_CHECK_MEM_EQ("i.png", 5, amark_node_get_url(image).data, amark_node_get_url(image).len);
    test_check_literal(html, "");
    test_check_literal(amark_node_get_first_child(heading), "h");

    amark_node_destroy(doc);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(failure_injection_keeps_runs)
{
    test_summary_t expected;
    test_summary_t actual;
    size_t successes;
    size_t failures = 0;
    bool completed = false;

    test_allocator_install();
    for (successes = 0; (successes < TEST_FAIL_LIMIT) && !completed; successes++)
    {
        amark_node_t *doc;
        amark_error_t error;

        test_allocator_install();
        doc = test_build_failure_tree();
        test_summarize(doc, &expected);

        test_allocator_fail_after(successes);
        error = amark_node_normalize(doc);
        test_allocator_never_fail();

        if (error == AMARK_OK)
        {
            completed = true;
            test_check_tree(doc, true);
        }
        else
        {
            NAOS_CHECK_INT_EQ(AMARK_ERROR_NO_MEMORY, error);
            test_check_tree(doc, false);
            failures++;
        }

        /* 成功・失敗のどちらでも、並びごとの連結内容は正規化前と同じ */
        test_summarize(doc, &actual);
        NAOS_CHECK_STR_EQ(expected.text, actual.text);

        amark_node_destroy(doc);
        NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
    }

    NAOS_CHECK_TRUE(completed);

    /* 連結が必要な並び（ab、cd、fg、hi、jk、mn）の数だけ失敗の位置がある */
    NAOS_CHECK_UINT_EQ(6u, failures);
}

NAOS_TEST(deep_tree_is_normalized)
{
    amark_node_t *para;
    amark_node_t *current;
    amark_node_t *walk;
    size_t depth;
    size_t i;

    test_allocator_install();

    /* 各段: emphasis の子を [x][""][emphasis][y][z] とする。最深部の emphasis は [x][""][y][z] */
    para = test_node(AMARK_NODE_PARAGRAPH);
    current = para;
    for (i = 0; i < TEST_DEEP_DEPTH; i++)
    {
        amark_node_t *emph = test_node(AMARK_NODE_EMPHASIS);

        test_append(current, emph);
        test_append_text(emph, "x");
        test_append_text(emph, "");
        if (current != para)
        {
            test_append_text(current, "y");
            test_append_text(current, "z");
        }

        current = emph;
    }

    test_append_text(current, "y");
    test_append_text(current, "z");

    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_normalize(para));

    /* 各段は [x][emphasis][yz]、最深部は [xyz] */
    depth = 0;
    walk = amark_node_get_first_child(para);
    while (walk != NULL)
    {
        amark_node_t *first = amark_node_get_first_child(walk);
        amark_node_t *middle = amark_node_get_next(first);

        depth++;
        if ((middle != NULL) && (amark_node_get_type(middle) == AMARK_NODE_EMPHASIS))
        {
            amark_node_t *last = amark_node_get_next(middle);

            NAOS_REQUIRE_NOT_NULL(last);
            NAOS_REQUIRE_NULL(amark_node_get_next(last));
            NAOS_REQUIRE_UINT_EQ(1u, amark_node_get_literal(first).len);
            NAOS_REQUIRE_MEM_EQ("yz", 2, amark_node_get_literal(last).data, amark_node_get_literal(last).len);
            walk = middle;
        }
        else
        {
            NAOS_REQUIRE_NULL(middle);
            NAOS_REQUIRE_MEM_EQ("xyz", 3, amark_node_get_literal(first).data, amark_node_get_literal(first).len);
            walk = NULL;
        }
    }

    NAOS_CHECK_UINT_EQ(TEST_DEEP_DEPTH, depth);

    amark_node_destroy(para);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

/* clang-format off */
NAOS_SUITE(node_normalize,
    NAOS_SET_TEARDOWN(restore_default),
    NAOS_ADD_TEST(null_root_is_invalid_argument),
    NAOS_ADD_TEST(merges_adjacent_text),
    NAOS_ADD_TEST(removes_empty_text),
    NAOS_ADD_TEST(non_text_separates_runs),
    NAOS_ADD_TEST(runs_inside_emphasis),
    NAOS_ADD_TEST(root_is_not_removed),
    NAOS_ADD_TEST(normalized_tree_is_unchanged),
    NAOS_ADD_TEST(other_nodes_are_unchanged),
    NAOS_ADD_TEST(failure_injection_keeps_runs),
    NAOS_ADD_TEST(deep_tree_is_normalized)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(node_normalize)
)
/* clang-format on */
