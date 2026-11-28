/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * C++ から包括ヘッダを include し、公開 API を C リンケージで呼べる（リンクできる）ことを確かめる。
 * 追記専用。後続の spec は自分の主要 API の呼び出しを、spec 名のコメント付きブロックとして
 * calls_public_api の末尾に追記する（他の spec のブロックを書き換えない）。
 * 呼び出しの確認には NAOS_CHECK* だけを使う（NAOS_REQUIRE* は longjmp で抜けるため、
 * デストラクタを持つ局所変数と併用できない）。
 */
#include <amark/amark.h>

#include <naos/naos.h>

#include <cstring>

NAOS_TEST(calls_public_api)
{
    /* foundation */
    {
        const char *version = amark_version_string();

        NAOS_CHECK_UINT_EQ(AMARK_VERSION_NUMBER, amark_version_number());
        NAOS_CHECK_TRUE(version != nullptr);
        NAOS_CHECK_TRUE((version != nullptr) && (std::strcmp(version, AMARK_VERSION_STRING) == 0));
    }

    /* foundation: エラーの文字列化、スライスの生成、バッファの追記と後片付け */
    {
        const char *message = amark_error_string(AMARK_ERROR_NO_MEMORY);
        amark_slice_t slice = amark_slice_from_cstr("abc");
        amark_buffer_t buf;

        NAOS_CHECK_TRUE((message != nullptr) && (message[0] != '\0'));
        NAOS_CHECK_UINT_EQ(3u, slice.len);

        amark_buffer_init(&buf);
        NAOS_CHECK_INT_EQ(AMARK_OK, amark_buffer_append_slice(&buf, slice));
        NAOS_CHECK_INT_EQ(AMARK_OK, amark_buffer_append_cstr(&buf, "def"));
        NAOS_CHECK_UINT_EQ(6u, buf.len);
        NAOS_CHECK_TRUE((buf.data != nullptr) && (std::strcmp(buf.data, "abcdef") == 0));
        amark_buffer_deinit(&buf);
        NAOS_CHECK_TRUE(buf.data == nullptr);
        NAOS_CHECK_UINT_EQ(0u, buf.len);
    }

    /* node-tree: 木の生成、子の追加、走査、破棄 */
    {
        amark_node_t *doc = nullptr;
        amark_node_t *paragraph = nullptr;
        amark_node_t *text = nullptr;
        amark_node_iter_t iter;
        amark_node_iter_event_t event;
        unsigned events = 0;

        NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_create(AMARK_NODE_DOCUMENT, &doc));
        NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_create(AMARK_NODE_PARAGRAPH, &paragraph));
        NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_create(AMARK_NODE_TEXT, &text));
        NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_set_literal(text, amark_slice_from_cstr("hi")));
        NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_append_child(paragraph, text));
        NAOS_CHECK_INT_EQ(AMARK_OK, amark_node_append_child(doc, paragraph));
        NAOS_CHECK_TRUE(amark_node_get_parent(text) == paragraph);
        NAOS_CHECK_TRUE(amark_node_get_first_child(doc) == paragraph);

        /* doc ENTER、paragraph ENTER、text ENTER、paragraph EXIT、doc EXIT */
        amark_node_iter_init(&iter, doc);
        while ((event = amark_node_iter_next(&iter)) != AMARK_NODE_ITER_DONE)
        {
            if ((events == 2u) && (event == AMARK_NODE_ITER_ENTER))
            {
                NAOS_CHECK_TRUE(amark_node_iter_get_node(&iter) == text);
            }

            events++;
        }

        NAOS_CHECK_UINT_EQ(5u, events);
        NAOS_CHECK_INT_EQ(AMARK_NODE_ITER_DONE, amark_node_iter_get_event(&iter));

        amark_node_destroy(doc);
    }
}

/* clang-format off */
NAOS_SUITE(cxx_header,
    NAOS_ADD_TEST(calls_public_api)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(cxx_header)
)
/* clang-format on */
