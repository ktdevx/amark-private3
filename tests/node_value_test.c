/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * 種別ごとの値のアクセサと拡張種別の付随データのテスト。既定値、各値の往復と借用スライスの NUL 終端、
 * 範囲の境界と未定義のリスト種別、不正な UTF-8、NUL を含む値、呼び出し側バッファの書き換えに
 * 影響されないこと、スライスの不変条件とサイズのあふれ、種別不一致（setter のエラーと getter の
 * 「ない」値）、文字列 setter の失敗注入で旧値と生存ブロック数が変わらないこと、付随データの
 * 後片付けの回数（置き換え、同一値の再設定、破棄、後片付け関数なし）と標準種別の拒否を確かめる。
 */
#include <amark/error.h>
#include <amark/memory.h>
#include <amark/node.h>
#include <amark/node_kind.h>
#include <amark/slice.h>

#include <naos/naos.h>

#include "support/test_allocator.h"

#include <stdint.h>
#include <string.h>

/* 文字列の値の getter と setter */
typedef amark_slice_t (*test_string_getter_t)(const amark_node_t *node);
typedef amark_error_t (*test_string_setter_t)(amark_node_t *node, amark_slice_t value);

/* 文字列の値と、それを持つ種別の組 */
typedef struct test_string_field
{
    amark_node_type_t type;
    test_string_getter_t get;
    test_string_setter_t set;
    const char *name;
} test_string_field_t;

static const test_string_field_t test_string_fields[] = {
    {AMARK_NODE_CODE_BLOCK, amark_node_get_literal, amark_node_set_literal, "code_block.literal"},
    {AMARK_NODE_CODE_BLOCK, amark_node_get_info, amark_node_set_info, "code_block.info"},
    {AMARK_NODE_HTML_BLOCK, amark_node_get_literal, amark_node_set_literal, "html_block.literal"},
    {AMARK_NODE_TEXT, amark_node_get_literal, amark_node_set_literal, "text.literal"},
    {AMARK_NODE_CODE, amark_node_get_literal, amark_node_set_literal, "code.literal"},
    {AMARK_NODE_HTML_INLINE, amark_node_get_literal, amark_node_set_literal, "html_inline.literal"},
    {AMARK_NODE_LINK, amark_node_get_url, amark_node_set_url, "link.url"},
    {AMARK_NODE_LINK, amark_node_get_title, amark_node_set_title, "link.title"},
    {AMARK_NODE_IMAGE, amark_node_get_url, amark_node_set_url, "image.url"},
    {AMARK_NODE_IMAGE, amark_node_get_title, amark_node_set_title, "image.title"},
};

#define TEST_STRING_FIELD_COUNT (sizeof(test_string_fields) / sizeof(test_string_fields[0]))

/* 標準種別の識別値（DOCUMENT〜IMAGE） */
static const amark_node_type_t test_standard_types[] = {
    AMARK_NODE_DOCUMENT,   AMARK_NODE_BLOCK_QUOTE, AMARK_NODE_LIST,    AMARK_NODE_ITEM,           AMARK_NODE_CODE_BLOCK,
    AMARK_NODE_HTML_BLOCK, AMARK_NODE_PARAGRAPH,   AMARK_NODE_HEADING, AMARK_NODE_THEMATIC_BREAK, AMARK_NODE_TEXT,
    AMARK_NODE_SOFT_BREAK, AMARK_NODE_LINE_BREAK,  AMARK_NODE_CODE,    AMARK_NODE_HTML_INLINE,    AMARK_NODE_EMPHASIS,
    AMARK_NODE_STRONG,     AMARK_NODE_LINK,        AMARK_NODE_IMAGE,
};

#define TEST_STANDARD_TYPE_COUNT (sizeof(test_standard_types) / sizeof(test_standard_types[0]))

/* 付随データの後片付けの呼び出しを記録する */
typedef struct test_cleanup_state
{
    size_t calls;
    void *last_data;
    void *last_user_data;
} test_cleanup_state_t;

static void test_record_cleanup(void *data, void *user_data)
{
    test_cleanup_state_t *state = (test_cleanup_state_t *)user_data;

    state->calls++;
    state->last_data = data;
    state->last_user_data = user_data;
}

/* 種別 type が文字列の値 field を持つか */
static bool test_has_field(amark_node_type_t type, const test_string_field_t *field)
{
    size_t i;

    for (i = 0; i < TEST_STRING_FIELD_COUNT; i++)
    {
        if ((test_string_fields[i].type == type) && (test_string_fields[i].get == field->get))
        {
            return true;
        }
    }

    return false;
}

/* 借用スライスが期待する内容で、NUL 終端されていることを確かめる */
static void test_check_value(amark_slice_t actual, const char *expected, size_t expected_len)
{
    NAOS_REQUIRE_NOT_NULL(actual.data);
    NAOS_CHECK_UINT_EQ(expected_len, actual.len);
    NAOS_CHECK_MEM_EQ(expected, expected_len, actual.data, actual.len);
    NAOS_CHECK_INT_EQ(0, (int)actual.data[actual.len]);
}

/* 構造体を直接組んだスライスを返す（amark_slice_make は data == NULL を空スライスに直すため） */
static amark_slice_t test_raw_slice(const char *data, size_t len)
{
    amark_slice_t slice;

    slice.data = data;
    slice.len = len;

    return slice;
}

/* 値を持たないことを表すスライスであることを確かめる */
static void test_check_absent(amark_slice_t actual)
{
    NAOS_CHECK_NULL(actual.data);
    NAOS_CHECK_UINT_EQ(0u, actual.len);
}

/* 子を取らない拡張ブロックの種別を登録する */
static const amark_node_kind_t *test_add_extension(amark_node_registry_t *registry, const char *name,
                                                   amark_node_destroy_data_fn_t destroy_data, void *user_data)
{
    amark_node_kind_desc_t desc;
    const amark_node_kind_t *kind = NULL;

    memset(&desc, 0, sizeof(desc));
    desc.name = amark_slice_from_cstr(name);
    desc.category = AMARK_NODE_CATEGORY_BLOCK;
    desc.content = AMARK_NODE_CONTENT_NONE;
    desc.requires_explicit_parent = false;
    desc.can_contain = NULL;
    desc.destroy_data = destroy_data;
    desc.user_data = user_data;

    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_registry_add(registry, &desc, &kind));

    return kind;
}

NAOS_TEARDOWN(restore_default)
{
    test_allocator_uninstall();
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_allocator_set(NULL));
}

NAOS_TEST(defaults)
{
    amark_node_t *heading = NULL;
    amark_node_t *list = NULL;
    size_t i;

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create(AMARK_NODE_HEADING, &heading));
    NAOS_CHECK_INT_EQ(1, amark_node_get_heading_level(heading));

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create(AMARK_NODE_LIST, &list));
    NAOS_CHECK_INT_EQ((int)AMARK_NODE_LIST_BULLET, (int)amark_node_get_list_type(list));
    NAOS_CHECK_UINT_EQ(1u, amark_node_get_list_start(list));
    NAOS_CHECK_FALSE(amark_node_get_list_tight(list));

    for (i = 0; i < TEST_STRING_FIELD_COUNT; i++)
    {
        amark_node_t *node = NULL;

        NAOS_NOTE("field %s", test_string_fields[i].name);
        NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create(test_string_fields[i].type, &node));
        test_check_value(test_string_fields[i].get(node), "", 0);
        amark_node_destroy(node);
    }

    amark_node_destroy(heading);
    amark_node_destroy(list);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(heading_level_range)
{
    amark_node_t *node = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create(AMARK_NODE_HEADING, &node));

    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_heading_level(node, AMARK_NODE_HEADING_LEVEL_MAX));
    NAOS_CHECK_INT_EQ(6, amark_node_get_heading_level(node));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_heading_level(node, 7));
    NAOS_CHECK_INT_EQ(6, amark_node_get_heading_level(node));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_heading_level(node, 0));
    NAOS_CHECK_INT_EQ(6, amark_node_get_heading_level(node));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_heading_level(node, -1));
    NAOS_CHECK_INT_EQ(6, amark_node_get_heading_level(node));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_heading_level(node, AMARK_NODE_HEADING_LEVEL_MIN));
    NAOS_CHECK_INT_EQ(1, amark_node_get_heading_level(node));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_heading_level(node, 3));
    NAOS_CHECK_INT_EQ(3, amark_node_get_heading_level(node));

    amark_node_destroy(node);
}

NAOS_TEST(list_values)
{
    amark_node_t *node = NULL;

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create(AMARK_NODE_LIST, &node));

    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_list_type(node, AMARK_NODE_LIST_ORDERED));
    NAOS_CHECK_INT_EQ((int)AMARK_NODE_LIST_ORDERED, (int)amark_node_get_list_type(node));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_list_type(node, AMARK_NODE_LIST_NONE));
    NAOS_CHECK_INT_EQ((int)AMARK_NODE_LIST_ORDERED, (int)amark_node_get_list_type(node));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_list_type(node, (amark_node_list_type_t)3));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_list_type(node, (amark_node_list_type_t)-1));
    NAOS_CHECK_INT_EQ((int)AMARK_NODE_LIST_ORDERED, (int)amark_node_get_list_type(node));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_list_type(node, AMARK_NODE_LIST_BULLET));
    NAOS_CHECK_INT_EQ((int)AMARK_NODE_LIST_BULLET, (int)amark_node_get_list_type(node));

    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_list_start(node, 0u));
    NAOS_CHECK_UINT_EQ(0u, amark_node_get_list_start(node));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_list_start(node, AMARK_NODE_LIST_START_MAX));
    NAOS_CHECK_UINT_EQ(999999999u, amark_node_get_list_start(node));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_list_start(node, 1000000000u));
    NAOS_CHECK_UINT_EQ(999999999u, amark_node_get_list_start(node));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_list_start(node, UINT32_MAX));
    NAOS_CHECK_UINT_EQ(999999999u, amark_node_get_list_start(node));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_list_start(node, 1u));
    NAOS_CHECK_UINT_EQ(1u, amark_node_get_list_start(node));

    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_list_tight(node, true));
    NAOS_CHECK_TRUE(amark_node_get_list_tight(node));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_list_tight(node, false));
    NAOS_CHECK_FALSE(amark_node_get_list_tight(node));

    amark_node_destroy(node);
}

NAOS_TEST(string_round_trip)
{
    size_t i;

    test_allocator_install();
    for (i = 0; i < TEST_STRING_FIELD_COUNT; i++)
    {
        const test_string_field_t *field = &test_string_fields[i];
        char buffer[] = "caf\xC3\xA9 \xE3\x81\x82";
        amark_node_t *node = NULL;
        amark_slice_t value;

        NAOS_NOTE("field %s", field->name);
        NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create(field->type, &node));
        NAOS_CHECK_INT_EQ(AMARK_OK, field->set(node, amark_slice_make(buffer, strlen(buffer))));
        value = field->get(node);
        test_check_value(value, "caf\xC3\xA9 \xE3\x81\x82", strlen(buffer));
        NAOS_CHECK_TRUE(value.data != buffer);
        NAOS_CHECK_UINT_EQ(2u, test_allocator_stats().live);

        /* 呼び出し側のバッファを書き換えても値は変わらない */
        memset(buffer, 'x', strlen(buffer));
        test_check_value(field->get(node), "caf\xC3\xA9 \xE3\x81\x82", strlen("caf\xC3\xA9 \xE3\x81\x82"));

        /* 置き換えで旧値が解放される */
        NAOS_CHECK_INT_EQ(AMARK_OK, field->set(node, amark_slice_from_cstr("second")));
        test_check_value(field->get(node), "second", 6);
        NAOS_CHECK_UINT_EQ(2u, test_allocator_stats().live);

        /* 空の値で未設定に戻り、確保を残さない */
        NAOS_CHECK_INT_EQ(AMARK_OK, field->set(node, amark_slice_make("", 0)));
        test_check_value(field->get(node), "", 0);
        NAOS_CHECK_UINT_EQ(1u, test_allocator_stats().live);

        NAOS_CHECK_INT_EQ(AMARK_OK, field->set(node, amark_slice_from_cstr("third")));
        NAOS_CHECK_INT_EQ(AMARK_OK, field->set(node, test_raw_slice(NULL, 0)));
        test_check_value(field->get(node), "", 0);
        NAOS_CHECK_UINT_EQ(1u, test_allocator_stats().live);

        /* 破棄で値も解放される */
        NAOS_CHECK_INT_EQ(AMARK_OK, field->set(node, amark_slice_from_cstr("fourth")));
        amark_node_destroy(node);
        NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
    }
}

NAOS_TEST(code_block_fields_are_independent)
{
    amark_node_t *node = NULL;

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create(AMARK_NODE_CODE_BLOCK, &node));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_info(node, amark_slice_from_cstr("c")));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_literal(node, amark_slice_from_cstr("int x;\n")));
    test_check_value(amark_node_get_info(node), "c", 1);
    test_check_value(amark_node_get_literal(node), "int x;\n", 7);
    amark_node_destroy(node);

    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create(AMARK_NODE_LINK, &node));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_url(node, amark_slice_from_cstr("https://example.com/")));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_title(node, amark_slice_from_cstr("Example")));
    test_check_value(amark_node_get_url(node), "https://example.com/", 20);
    test_check_value(amark_node_get_title(node), "Example", 7);
    amark_node_destroy(node);

    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(string_with_nul_bytes)
{
    static const char value[] = {'a', '\0', 'b', '\0'};
    size_t i;

    test_allocator_install();
    for (i = 0; i < TEST_STRING_FIELD_COUNT; i++)
    {
        amark_node_t *node = NULL;

        NAOS_NOTE("field %s", test_string_fields[i].name);
        NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create(test_string_fields[i].type, &node));
        NAOS_CHECK_INT_EQ(AMARK_OK, test_string_fields[i].set(node, amark_slice_make(value, sizeof(value))));
        test_check_value(test_string_fields[i].get(node), value, sizeof(value));
        amark_node_destroy(node);
    }

    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(string_rejects_invalid_utf8)
{
    static const char overlong[] = {(char)0xC0, (char)0x80};
    static const char truncated[] = {'a', (char)0xE3, (char)0x81};
    static const char lone_continuation[] = {(char)0x80};
    size_t i;

    test_allocator_install();
    for (i = 0; i < TEST_STRING_FIELD_COUNT; i++)
    {
        const test_string_field_t *field = &test_string_fields[i];
        amark_node_t *node = NULL;
        size_t live;

        NAOS_NOTE("field %s", field->name);
        NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create(field->type, &node));
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_UTF8, field->set(node, amark_slice_make(overlong, sizeof(overlong))));
        test_check_value(field->get(node), "", 0);

        NAOS_CHECK_INT_EQ(AMARK_OK, field->set(node, amark_slice_from_cstr("old")));
        live = test_allocator_stats().live;
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_UTF8, field->set(node, amark_slice_make(overlong, sizeof(overlong))));
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_UTF8, field->set(node, amark_slice_make(truncated, sizeof(truncated))));
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_UTF8,
                          field->set(node, amark_slice_make(lone_continuation, sizeof(lone_continuation))));
        test_check_value(field->get(node), "old", 3);
        NAOS_CHECK_UINT_EQ(live, test_allocator_stats().live);

        amark_node_destroy(node);
    }

    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(string_rejects_bad_slice_and_overflow)
{
    static const char bytes[] = "abc";
    size_t i;

    test_allocator_install();
    for (i = 0; i < TEST_STRING_FIELD_COUNT; i++)
    {
        const test_string_field_t *field = &test_string_fields[i];
        amark_node_t *node = NULL;
        size_t allocations;

        NAOS_NOTE("field %s", field->name);
        NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create(field->type, &node));
        NAOS_CHECK_INT_EQ(AMARK_OK, field->set(node, amark_slice_from_cstr("old")));
        allocations = test_allocator_stats().allocations;

        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, field->set(node, test_raw_slice(NULL, 1)));
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, field->set(node, test_raw_slice(NULL, SIZE_MAX)));
        NAOS_CHECK_INT_EQ(AMARK_ERROR_OVERFLOW, field->set(node, test_raw_slice(bytes, SIZE_MAX)));
        test_check_value(field->get(node), "old", 3);
        NAOS_CHECK_UINT_EQ(allocations, test_allocator_stats().allocations);

        amark_node_destroy(node);
    }

    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(string_failure_injection_keeps_old_value)
{
    size_t i;

    test_allocator_install();
    for (i = 0; i < TEST_STRING_FIELD_COUNT; i++)
    {
        const test_string_field_t *field = &test_string_fields[i];
        amark_node_t *node = NULL;
        size_t live;

        NAOS_NOTE("field %s", field->name);
        NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create(field->type, &node));

        /* 未設定からの設定の失敗 */
        live = test_allocator_stats().live;
        test_allocator_fail_after(0);
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NO_MEMORY, field->set(node, amark_slice_from_cstr("new")));
        test_allocator_never_fail();
        test_check_value(field->get(node), "", 0);
        NAOS_CHECK_UINT_EQ(live, test_allocator_stats().live);

        /* 設定済みの値の置き換えの失敗 */
        NAOS_CHECK_INT_EQ(AMARK_OK, field->set(node, amark_slice_from_cstr("old")));
        live = test_allocator_stats().live;
        test_allocator_fail_after(0);
        NAOS_CHECK_INT_EQ(AMARK_ERROR_NO_MEMORY, field->set(node, amark_slice_from_cstr("new")));
        test_allocator_never_fail();
        test_check_value(field->get(node), "old", 3);
        NAOS_CHECK_UINT_EQ(live, test_allocator_stats().live);

        /* 空の値の設定は確保しないため失敗しない */
        test_allocator_fail_after(0);
        NAOS_CHECK_INT_EQ(AMARK_OK, field->set(node, amark_slice_make("", 0)));
        test_allocator_never_fail();
        test_check_value(field->get(node), "", 0);
        NAOS_CHECK_UINT_EQ(live - 1u, test_allocator_stats().live);

        amark_node_destroy(node);
    }

    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(string_kind_mismatch)
{
    size_t t;

    test_allocator_install();
    for (t = 0; t < TEST_STANDARD_TYPE_COUNT; t++)
    {
        amark_node_t *node = NULL;
        size_t i;

        NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create(test_standard_types[t], &node));
        for (i = 0; i < TEST_STRING_FIELD_COUNT; i++)
        {
            const test_string_field_t *field = &test_string_fields[i];

            if (test_has_field(test_standard_types[t], field))
            {
                continue;
            }

            NAOS_NOTE("field %s", field->name);
            NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, field->set(node, amark_slice_from_cstr("x")));
            test_check_absent(field->get(node));
        }

        /* 失敗した setter はほかの値も変えない */
        for (i = 0; i < TEST_STRING_FIELD_COUNT; i++)
        {
            if (test_has_field(test_standard_types[t], &test_string_fields[i]))
            {
                test_check_value(test_string_fields[i].get(node), "", 0);
            }
        }

        amark_node_destroy(node);
    }

    for (t = 0; t < TEST_STRING_FIELD_COUNT; t++)
    {
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, test_string_fields[t].set(NULL, amark_slice_from_cstr("x")));
        test_check_absent(test_string_fields[t].get(NULL));
    }

    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(numeric_kind_mismatch)
{
    size_t t;

    for (t = 0; t < TEST_STANDARD_TYPE_COUNT; t++)
    {
        amark_node_type_t type = test_standard_types[t];
        amark_node_t *node = NULL;

        NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create(type, &node));
        if (type != AMARK_NODE_HEADING)
        {
            NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_heading_level(node, 2));
            NAOS_CHECK_INT_EQ(0, amark_node_get_heading_level(node));
        }

        if (type != AMARK_NODE_LIST)
        {
            NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_list_type(node, AMARK_NODE_LIST_ORDERED));
            NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_list_start(node, 5u));
            NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_list_tight(node, true));
            NAOS_CHECK_INT_EQ((int)AMARK_NODE_LIST_NONE, (int)amark_node_get_list_type(node));
            NAOS_CHECK_UINT_EQ(0u, amark_node_get_list_start(node));
            NAOS_CHECK_FALSE(amark_node_get_list_tight(node));
        }

        if (type == AMARK_NODE_HEADING)
        {
            NAOS_CHECK_INT_EQ(1, amark_node_get_heading_level(node));
        }

        if (type == AMARK_NODE_LIST)
        {
            NAOS_CHECK_INT_EQ((int)AMARK_NODE_LIST_BULLET, (int)amark_node_get_list_type(node));
            NAOS_CHECK_UINT_EQ(1u, amark_node_get_list_start(node));
            NAOS_CHECK_FALSE(amark_node_get_list_tight(node));
        }

        amark_node_destroy(node);
    }

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_heading_level(NULL, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_list_type(NULL, AMARK_NODE_LIST_BULLET));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_list_start(NULL, 1u));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_list_tight(NULL, false));
    NAOS_CHECK_INT_EQ(0, amark_node_get_heading_level(NULL));
    NAOS_CHECK_INT_EQ((int)AMARK_NODE_LIST_NONE, (int)amark_node_get_list_type(NULL));
    NAOS_CHECK_UINT_EQ(0u, amark_node_get_list_start(NULL));
    NAOS_CHECK_FALSE(amark_node_get_list_tight(NULL));
}

NAOS_TEST(extension_kind_has_no_standard_values)
{
    amark_node_registry_t *registry = NULL;
    const amark_node_kind_t *kind;
    amark_node_t *node = NULL;
    size_t i;

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));
    kind = test_add_extension(registry, "table", NULL, NULL);
    NAOS_REQUIRE_NOT_NULL(kind);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create_with_kind(kind, &node));

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_heading_level(node, 1));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_list_start(node, 1u));
    NAOS_CHECK_INT_EQ(0, amark_node_get_heading_level(node));
    NAOS_CHECK_INT_EQ((int)AMARK_NODE_LIST_NONE, (int)amark_node_get_list_type(node));
    for (i = 0; i < TEST_STRING_FIELD_COUNT; i++)
    {
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, test_string_fields[i].set(node, amark_slice_from_cstr("x")));
        test_check_absent(test_string_fields[i].get(node));
    }

    NAOS_CHECK_NULL(amark_node_get_data(node));

    amark_node_destroy(node);
    amark_node_registry_destroy(registry);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(data_cleanup_counts)
{
    test_cleanup_state_t state;
    amark_node_registry_t *registry = NULL;
    const amark_node_kind_t *kind;
    amark_node_t *node = NULL;
    int first = 1;
    int second = 2;
    int third = 3;

    memset(&state, 0, sizeof(state));
    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));
    kind = test_add_extension(registry, "table", test_record_cleanup, &state);
    NAOS_REQUIRE_NOT_NULL(kind);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create_with_kind(kind, &node));
    NAOS_CHECK_NULL(amark_node_get_data(node));

    /* 設定: 旧データが NULL なので後片付けしない */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_data(node, &first));
    NAOS_CHECK_PTR_EQ(&first, amark_node_get_data(node));
    NAOS_CHECK_UINT_EQ(0u, state.calls);

    /* 同一値の再設定は何もしない */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_data(node, &first));
    NAOS_CHECK_PTR_EQ(&first, amark_node_get_data(node));
    NAOS_CHECK_UINT_EQ(0u, state.calls);

    /* 置き換え: 旧データに 1 回 */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_data(node, &second));
    NAOS_CHECK_PTR_EQ(&second, amark_node_get_data(node));
    NAOS_CHECK_UINT_EQ(1u, state.calls);
    NAOS_CHECK_PTR_EQ(&first, state.last_data);
    NAOS_CHECK_PTR_EQ(&state, state.last_user_data);

    /* 解除: 旧データに 1 回 */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_data(node, NULL));
    NAOS_CHECK_NULL(amark_node_get_data(node));
    NAOS_CHECK_UINT_EQ(2u, state.calls);
    NAOS_CHECK_PTR_EQ(&second, state.last_data);

    /* NULL の再設定は何もしない */
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_data(node, NULL));
    NAOS_CHECK_UINT_EQ(2u, state.calls);

    /* 付随データなしの破棄は後片付けしない */
    amark_node_destroy(node);
    NAOS_CHECK_UINT_EQ(2u, state.calls);

    /* 破棄: 保持中のデータに 1 回 */
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create_with_kind(kind, &node));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_data(node, &third));
    NAOS_CHECK_UINT_EQ(2u, state.calls);
    amark_node_destroy(node);
    NAOS_CHECK_UINT_EQ(3u, state.calls);
    NAOS_CHECK_PTR_EQ(&third, state.last_data);
    NAOS_CHECK_PTR_EQ(&state, state.last_user_data);

    amark_node_registry_destroy(registry);
    NAOS_CHECK_UINT_EQ(3u, state.calls);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(data_without_cleanup_function)
{
    amark_node_registry_t *registry = NULL;
    const amark_node_kind_t *kind;
    amark_node_t *node = NULL;
    int first = 1;
    int second = 2;

    test_allocator_install();
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_registry_create(&registry));
    kind = test_add_extension(registry, "table", NULL, NULL);
    NAOS_REQUIRE_NOT_NULL(kind);
    NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create_with_kind(kind, &node));

    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_data(node, &first));
    NAOS_CHECK_PTR_EQ(&first, amark_node_get_data(node));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_data(node, &second));
    NAOS_CHECK_PTR_EQ(&second, amark_node_get_data(node));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_data(node, NULL));
    NAOS_CHECK_NULL(amark_node_get_data(node));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_data(node, &first));
    amark_node_destroy(node);

    amark_node_registry_destroy(registry);
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

NAOS_TEST(data_rejects_standard_kinds_and_null)
{
    int value = 1;
    size_t t;

    test_allocator_install();
    for (t = 0; t < TEST_STANDARD_TYPE_COUNT; t++)
    {
        amark_node_t *node = NULL;

        NAOS_REQUIRE_INT_EQ(AMARK_OK, amark_node_create(test_standard_types[t], &node));
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_data(node, &value));
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_data(node, NULL));
        NAOS_CHECK_NULL(amark_node_get_data(node));
        amark_node_destroy(node);
    }

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_node_set_data(NULL, &value));
    NAOS_CHECK_NULL(amark_node_get_data(NULL));
    NAOS_CHECK_UINT_EQ(0u, test_allocator_stats().live);
}

/* clang-format off */
NAOS_SUITE(node_value,
    NAOS_SET_TEARDOWN(restore_default),
    NAOS_ADD_TEST(defaults),
    NAOS_ADD_TEST(heading_level_range),
    NAOS_ADD_TEST(list_values),
    NAOS_ADD_TEST(string_round_trip),
    NAOS_ADD_TEST(code_block_fields_are_independent),
    NAOS_ADD_TEST(string_with_nul_bytes),
    NAOS_ADD_TEST(string_rejects_invalid_utf8),
    NAOS_ADD_TEST(string_rejects_bad_slice_and_overflow),
    NAOS_ADD_TEST(string_failure_injection_keeps_old_value),
    NAOS_ADD_TEST(string_kind_mismatch),
    NAOS_ADD_TEST(numeric_kind_mismatch),
    NAOS_ADD_TEST(extension_kind_has_no_standard_values),
    NAOS_ADD_TEST(data_cleanup_counts),
    NAOS_ADD_TEST(data_without_cleanup_function),
    NAOS_ADD_TEST(data_rejects_standard_kinds_and_null)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(node_value)
)
/* clang-format on */
