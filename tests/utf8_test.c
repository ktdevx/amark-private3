/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * UTF-8 のテスト。1〜4 バイトの境界値の復号と符号化の往復、不正な並びの置換文字と読み飛ばし量
 * （Unicode 標準 Table 3-8 の最大部分列の例）、空入力と NULL の拒否、サロゲートと範囲外の符号化
 * の拒否、検証の不正位置、逆方向の復号が先頭から前方へ復号したときの最後の単位と一致すること
 * を確かめる。
 */
#include <amark/amark.h>

#include <naos/naos.h>

#include <stddef.h>
#include <stdint.h>

/* 出力引数が変更されていないことを確かめるための値 */
#define TEST_CP_SENTINEL 0xDEADBEEFu
#define TEST_CONSUMED_SENTINEL 99u

/* 1 文字の期待値: コードポイントと、その UTF-8 表現 */
typedef struct test_char
{
    uint32_t cp;
    size_t len;
    unsigned char bytes[AMARK_UTF8_MAX_LENGTH];
} test_char_t;

/* 1〜4 バイトの各長さの最小値と最大値 */
static const test_char_t test_boundaries[] = {
    {0x0000u, 1, {0x00}},
    {0x007Fu, 1, {0x7F}},
    {0x0080u, 2, {0xC2, 0x80}},
    {0x07FFu, 2, {0xDF, 0xBF}},
    {0x0800u, 3, {0xE0, 0xA0, 0x80}},
    {0xFFFFu, 3, {0xEF, 0xBF, 0xBF}},
    {0x10000u, 4, {0xF0, 0x90, 0x80, 0x80}},
    {0x10FFFFu, 4, {0xF4, 0x8F, 0xBF, 0xBF}},
};

/* サロゲートの直前・直後と、Table 3-7 の 2 バイト目の範囲が狭い先頭バイトの端 */
static const test_char_t test_edges[] = {
    {0xD7FFu, 3, {0xED, 0x9F, 0xBF}},         {0xE000u, 3, {0xEE, 0x80, 0x80}},
    {0x1000u, 3, {0xE1, 0x80, 0x80}},         {0x3042u, 3, {0xE3, 0x81, 0x82}},
    {0x3FFFFu, 4, {0xF0, 0xBF, 0xBF, 0xBF}},  {0x40000u, 4, {0xF1, 0x80, 0x80, 0x80}},
    {0x100000u, 4, {0xF4, 0x80, 0x80, 0x80}},
};

/* Unicode 標準 Table 3-8 の例: a FFFD FFFD FFFD b FFFD c FFFD FFFD d */
static const unsigned char test_table_3_8[] = {0x61, 0xF1, 0x80, 0x80, 0xE1, 0x80, 0xC2,
                                               0x62, 0x80, 0x63, 0x80, 0xBF, 0x64};

/* 境界値の文字をすべて連結した妥当な並び（NUL を含む） */
static const unsigned char test_all_boundaries[] = {
    0x00, 0x7F, 0xC2, 0x80, 0xDF, 0xBF, 0xE0, 0xA0, 0x80, 0xEF, 0xBF,
    0xBF, 0xF0, 0x90, 0x80, 0x80, 0xF4, 0x8F, 0xBF, 0xBF, 0x61,
};

static amark_slice_t test_bytes(const unsigned char *bytes, size_t len)
{
    return amark_slice_make((const char *)bytes, len);
}

/* bytes の先頭から 1 単位を復号し、戻り値・cp・consumed が期待どおりであることを確かめる */
static void check_decode(const unsigned char *bytes, size_t len, amark_error_t expected_err, uint32_t expected_cp,
                         size_t expected_consumed)
{
    uint32_t cp = TEST_CP_SENTINEL;
    size_t consumed = TEST_CONSUMED_SENTINEL;

    NAOS_CHECK_INT_EQ(expected_err, amark_utf8_decode(test_bytes(bytes, len), &cp, &consumed));
    NAOS_CHECK_UINT_EQ(expected_cp, cp);
    NAOS_CHECK_UINT_EQ(expected_consumed, consumed);
}

/*
 * bytes 全体を前方へ順に復号し、各単位の消費バイト数が expected_units と一致し、不正単位の数が
 * expected_invalid であることを確かめる
 */
static void check_units(const unsigned char *bytes, size_t len, const size_t *expected_units, size_t expected_count,
                        size_t expected_invalid)
{
    size_t pos = 0;
    size_t count = 0;
    size_t invalid = 0;

    while (pos < len)
    {
        uint32_t cp = 0;
        size_t consumed = 0;
        amark_error_t err = amark_utf8_decode(test_bytes(&bytes[pos], len - pos), &cp, &consumed);

        NAOS_REQUIRE_UINT_GE(consumed, 1u);
        NAOS_REQUIRE_UINT_LE(consumed, (size_t)(len - pos));
        if (err == AMARK_ERROR_INVALID_UTF8)
        {
            NAOS_CHECK_UINT_EQ(AMARK_UTF8_REPLACEMENT_CHARACTER, cp);
            invalid++;
        }
        else
        {
            NAOS_CHECK_INT_EQ(AMARK_OK, err);
        }
        NAOS_REQUIRE_UINT_LT(count, expected_count);
        NAOS_CHECK_UINT_EQ(expected_units[count], consumed);

        pos += consumed;
        count++;
    }

    NAOS_CHECK_UINT_EQ(expected_count, count);
    NAOS_CHECK_UINT_EQ(expected_invalid, invalid);
}

/*
 * 1 から len までの各位置 offset について、先頭から offset までを前方へ順に復号したときの最後の
 * 単位と、amark_utf8_decode_previous の戻り値・cp・consumed が一致することを確かめる
 */
static void check_previous_matches_forward(const unsigned char *bytes, size_t len)
{
    amark_slice_t src = test_bytes(bytes, len);
    size_t offset;

    for (offset = 1; offset <= len; offset++)
    {
        size_t pos = 0;
        amark_error_t last_err = AMARK_OK;
        uint32_t last_cp = 0;
        size_t last_consumed = 0;
        uint32_t cp = TEST_CP_SENTINEL;
        size_t consumed = TEST_CONSUMED_SENTINEL;
        amark_error_t err;

        while (pos < offset)
        {
            last_err = amark_utf8_decode(test_bytes(&bytes[pos], offset - pos), &last_cp, &last_consumed);
            NAOS_REQUIRE_UINT_GE(last_consumed, 1u);
            pos += last_consumed;
        }
        NAOS_REQUIRE_UINT_EQ(offset, pos);

        NAOS_TRACE_PUSH("offset %u of %u", (unsigned)offset, (unsigned)len);
        err = amark_utf8_decode_previous(src, offset, &cp, &consumed);
        NAOS_CHECK_INT_EQ(last_err, err);
        NAOS_CHECK_UINT_EQ(last_cp, cp);
        NAOS_CHECK_UINT_EQ(last_consumed, consumed);
        NAOS_TRACE_POP();
    }
}

NAOS_TEST(constants)
{
    NAOS_CHECK_INT_EQ(4, AMARK_UTF8_MAX_LENGTH);
    NAOS_CHECK_UINT_EQ(0xFFFDu, AMARK_UTF8_REPLACEMENT_CHARACTER);
}

NAOS_TEST(decode_boundaries)
{
    size_t i;

    for (i = 0; i < (sizeof(test_boundaries) / sizeof(test_boundaries[0])); i++)
    {
        const test_char_t *c = &test_boundaries[i];

        NAOS_TRACE_PUSH("U+%04X", (unsigned)c->cp);
        check_decode(c->bytes, c->len, AMARK_OK, c->cp, c->len);
        NAOS_TRACE_POP();
    }
}

NAOS_TEST(decode_edges)
{
    size_t i;

    for (i = 0; i < (sizeof(test_edges) / sizeof(test_edges[0])); i++)
    {
        const test_char_t *c = &test_edges[i];

        NAOS_TRACE_PUSH("U+%04X", (unsigned)c->cp);
        check_decode(c->bytes, c->len, AMARK_OK, c->cp, c->len);
        NAOS_TRACE_POP();
    }
}

NAOS_TEST(decode_reads_only_first_character)
{
    static const unsigned char text[] = {0xE3, 0x81, 0x82, 0x61, 0x62};
    static const unsigned char nul_then_text[] = {0x00, 0x61};

    check_decode(text, sizeof(text), AMARK_OK, 0x3042u, 3);
    check_decode(nul_then_text, sizeof(nul_then_text), AMARK_OK, 0x0000u, 1);
}

NAOS_TEST(decode_nul_is_valid_character)
{
    static const unsigned char nul[] = {0x00};

    check_decode(nul, sizeof(nul), AMARK_OK, 0x0000u, 1);
}

NAOS_TEST(decode_overlong)
{
    static const unsigned char two[] = {0xC0, 0x80};
    static const unsigned char two_c1[] = {0xC1, 0xBF};
    static const unsigned char three[] = {0xE0, 0x80, 0x80};
    static const unsigned char four[] = {0xF0, 0x80, 0x80, 0x80};
    static const size_t two_units[] = {1, 1};
    static const size_t three_units[] = {1, 1, 1};
    static const size_t four_units[] = {1, 1, 1, 1};

    /* C0、C1 は先頭バイトになれず、E0・F0 の後の 80 は 2 バイト目の範囲外。いずれも 1 バイトずつ */
    check_decode(two, sizeof(two), AMARK_ERROR_INVALID_UTF8, AMARK_UTF8_REPLACEMENT_CHARACTER, 1);
    check_decode(three, sizeof(three), AMARK_ERROR_INVALID_UTF8, AMARK_UTF8_REPLACEMENT_CHARACTER, 1);
    check_units(two, sizeof(two), two_units, 2, 2);
    check_units(two_c1, sizeof(two_c1), two_units, 2, 2);
    check_units(three, sizeof(three), three_units, 3, 3);
    check_units(four, sizeof(four), four_units, 4, 4);
}

NAOS_TEST(decode_surrogate)
{
    static const unsigned char high[] = {0xED, 0xA0, 0x80};
    static const unsigned char low[] = {0xED, 0xBF, 0xBF};
    static const size_t units[] = {1, 1, 1};

    check_decode(high, sizeof(high), AMARK_ERROR_INVALID_UTF8, AMARK_UTF8_REPLACEMENT_CHARACTER, 1);
    check_units(high, sizeof(high), units, 3, 3);
    check_units(low, sizeof(low), units, 3, 3);
}

NAOS_TEST(decode_out_of_range)
{
    static const unsigned char above_max[] = {0xF4, 0x90, 0x80, 0x80};
    static const unsigned char f5[] = {0xF5};
    static const unsigned char f5_seq[] = {0xF5, 0x80, 0x80, 0x80};
    static const unsigned char ff[] = {0xFF};
    static const size_t four_units[] = {1, 1, 1, 1};

    check_decode(above_max, sizeof(above_max), AMARK_ERROR_INVALID_UTF8, AMARK_UTF8_REPLACEMENT_CHARACTER, 1);
    check_units(above_max, sizeof(above_max), four_units, 4, 4);
    check_decode(f5, sizeof(f5), AMARK_ERROR_INVALID_UTF8, AMARK_UTF8_REPLACEMENT_CHARACTER, 1);
    check_units(f5_seq, sizeof(f5_seq), four_units, 4, 4);
    check_decode(ff, sizeof(ff), AMARK_ERROR_INVALID_UTF8, AMARK_UTF8_REPLACEMENT_CHARACTER, 1);
}

NAOS_TEST(decode_truncated)
{
    static const unsigned char three_of_two[] = {0xE3, 0x81};
    static const unsigned char lead_only[] = {0xE3};
    static const unsigned char four_of_three[] = {0xF0, 0x90, 0x80};
    static const unsigned char two_of_one[] = {0xC2};
    static const unsigned char interrupted[] = {0xE3, 0x81, 0x61};
    static const size_t interrupted_units[] = {2, 1};

    /* 妥当な並びの接頭辞として成立する最長のバイト数を 1 単位として読み飛ばす */
    check_decode(three_of_two, sizeof(three_of_two), AMARK_ERROR_INVALID_UTF8, AMARK_UTF8_REPLACEMENT_CHARACTER, 2);
    check_decode(lead_only, sizeof(lead_only), AMARK_ERROR_INVALID_UTF8, AMARK_UTF8_REPLACEMENT_CHARACTER, 1);
    check_decode(four_of_three, sizeof(four_of_three), AMARK_ERROR_INVALID_UTF8, AMARK_UTF8_REPLACEMENT_CHARACTER, 3);
    check_decode(two_of_one, sizeof(two_of_one), AMARK_ERROR_INVALID_UTF8, AMARK_UTF8_REPLACEMENT_CHARACTER, 1);
    check_decode(interrupted, sizeof(interrupted), AMARK_ERROR_INVALID_UTF8, AMARK_UTF8_REPLACEMENT_CHARACTER, 2);
    check_units(interrupted, sizeof(interrupted), interrupted_units, 2, 1);
}

NAOS_TEST(decode_lone_continuation)
{
    static const unsigned char lone[] = {0x80};
    static const unsigned char last[] = {0xBF};
    static const unsigned char run[] = {0x80, 0x80};
    static const size_t run_units[] = {1, 1};

    check_decode(lone, sizeof(lone), AMARK_ERROR_INVALID_UTF8, AMARK_UTF8_REPLACEMENT_CHARACTER, 1);
    check_decode(last, sizeof(last), AMARK_ERROR_INVALID_UTF8, AMARK_UTF8_REPLACEMENT_CHARACTER, 1);
    check_units(run, sizeof(run), run_units, 2, 2);
}

NAOS_TEST(decode_table_3_8_example)
{
    /* a、F1 80 80、E1 80、C2、b、80、c、80、BF、d: 不正単位 6 つ */
    static const size_t units[] = {1, 3, 2, 1, 1, 1, 1, 1, 1, 1};

    check_units(test_table_3_8, sizeof(test_table_3_8), units, 10, 6);
    check_decode(&test_table_3_8[1], sizeof(test_table_3_8) - 1, AMARK_ERROR_INVALID_UTF8,
                 AMARK_UTF8_REPLACEMENT_CHARACTER, 3);
    check_decode(&test_table_3_8[4], sizeof(test_table_3_8) - 4, AMARK_ERROR_INVALID_UTF8,
                 AMARK_UTF8_REPLACEMENT_CHARACTER, 2);
}

NAOS_TEST(decode_rejects_invalid_arguments)
{
    static const unsigned char text[] = {0x61};
    uint32_t cp = TEST_CP_SENTINEL;
    size_t consumed = TEST_CONSUMED_SENTINEL;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_utf8_decode(amark_slice_make(NULL, 0), &cp, &consumed));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_utf8_decode(test_bytes(text, 0), &cp, &consumed));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_utf8_decode(test_bytes(text, sizeof(text)), NULL, &consumed));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_utf8_decode(test_bytes(text, sizeof(text)), &cp, NULL));
    NAOS_CHECK_UINT_EQ(TEST_CP_SENTINEL, cp);
    NAOS_CHECK_UINT_EQ(TEST_CONSUMED_SENTINEL, consumed);
}

NAOS_TEST(encode_boundaries_round_trip)
{
    size_t i;

    for (i = 0; i < (sizeof(test_boundaries) / sizeof(test_boundaries[0])); i++)
    {
        const test_char_t *c = &test_boundaries[i];
        char out[AMARK_UTF8_MAX_LENGTH];
        size_t written = TEST_CONSUMED_SENTINEL;
        uint32_t cp = TEST_CP_SENTINEL;
        size_t consumed = TEST_CONSUMED_SENTINEL;

        NAOS_TRACE_PUSH("U+%04X", (unsigned)c->cp);
        NAOS_CHECK_INT_EQ(AMARK_OK, amark_utf8_encode(c->cp, out, &written));
        NAOS_CHECK_UINT_EQ(c->len, written);
        NAOS_CHECK_MEM_EQ(out, written, c->bytes, c->len);
        NAOS_CHECK_INT_EQ(AMARK_OK, amark_utf8_decode(amark_slice_make(out, written), &cp, &consumed));
        NAOS_CHECK_UINT_EQ(c->cp, cp);
        NAOS_CHECK_UINT_EQ(written, consumed);
        NAOS_TRACE_POP();
    }
}

NAOS_TEST(encode_edges)
{
    size_t i;

    for (i = 0; i < (sizeof(test_edges) / sizeof(test_edges[0])); i++)
    {
        const test_char_t *c = &test_edges[i];
        char out[AMARK_UTF8_MAX_LENGTH];
        size_t written = TEST_CONSUMED_SENTINEL;

        NAOS_TRACE_PUSH("U+%04X", (unsigned)c->cp);
        NAOS_CHECK_INT_EQ(AMARK_OK, amark_utf8_encode(c->cp, out, &written));
        NAOS_CHECK_MEM_EQ(out, written, c->bytes, c->len);
        NAOS_TRACE_POP();
    }
}

NAOS_TEST(encode_rejects_surrogates_and_out_of_range)
{
    static const uint32_t rejected[] = {0xD800u, 0xDBFFu, 0xDC00u, 0xDFFFu, 0x110000u, 0x7FFFFFFFu, 0xFFFFFFFFu};
    static const char untouched[AMARK_UTF8_MAX_LENGTH] = {'w', 'x', 'y', 'z'};
    size_t i;

    for (i = 0; i < (sizeof(rejected) / sizeof(rejected[0])); i++)
    {
        char out[AMARK_UTF8_MAX_LENGTH] = {'w', 'x', 'y', 'z'};
        size_t written = TEST_CONSUMED_SENTINEL;

        NAOS_TRACE_PUSH("0x%X", (unsigned)rejected[i]);
        NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_UTF8, amark_utf8_encode(rejected[i], out, &written));
        NAOS_CHECK_UINT_EQ(TEST_CONSUMED_SENTINEL, written);
        NAOS_CHECK_MEM_EQ(out, sizeof(out), untouched, sizeof(untouched));
        NAOS_TRACE_POP();
    }
}

NAOS_TEST(encode_rejects_null_arguments)
{
    char out[AMARK_UTF8_MAX_LENGTH];
    size_t written = TEST_CONSUMED_SENTINEL;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_utf8_encode(0x61u, NULL, &written));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_utf8_encode(0x61u, out, NULL));
    NAOS_CHECK_UINT_EQ(TEST_CONSUMED_SENTINEL, written);
}

NAOS_TEST(validate_accepts_valid_and_empty)
{
    static const unsigned char nul_inside[] = {0x61, 0x00, 0x62};
    size_t error_offset = TEST_CONSUMED_SENTINEL;

    NAOS_CHECK_INT_EQ(AMARK_OK, amark_utf8_validate(amark_slice_make(NULL, 0), &error_offset));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_utf8_validate(amark_slice_from_cstr(""), &error_offset));
    NAOS_CHECK_INT_EQ(AMARK_OK,
                      amark_utf8_validate(test_bytes(test_all_boundaries, sizeof(test_all_boundaries)), &error_offset));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_utf8_validate(test_bytes(nul_inside, sizeof(nul_inside)), &error_offset));
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_utf8_validate(test_bytes(nul_inside, sizeof(nul_inside)), NULL));
}

NAOS_TEST(validate_reports_first_invalid_offset)
{
    static const unsigned char overlong_after_ascii[] = {0x61, 0x62, 0xC0, 0x80};
    static const unsigned char truncated_at_start[] = {0xE3, 0x81};
    static const unsigned char truncated_inside[] = {0x61, 0xE3, 0x81, 0x62};
    static const unsigned char surrogate_after_char[] = {0xE3, 0x81, 0x82, 0xED, 0xA0, 0x80};
    static const unsigned char truncated_at_end[] = {0xF0, 0x90, 0x80, 0x80, 0xF0, 0x90, 0x80};
    static const unsigned char lone_after_nul[] = {0x00, 0x80};
    size_t error_offset = TEST_CONSUMED_SENTINEL;

    NAOS_CHECK_INT_EQ(
        AMARK_ERROR_INVALID_UTF8,
        amark_utf8_validate(test_bytes(overlong_after_ascii, sizeof(overlong_after_ascii)), &error_offset));
    NAOS_CHECK_UINT_EQ(2u, error_offset);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_UTF8,
                      amark_utf8_validate(test_bytes(truncated_at_start, sizeof(truncated_at_start)), &error_offset));
    NAOS_CHECK_UINT_EQ(0u, error_offset);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_UTF8,
                      amark_utf8_validate(test_bytes(truncated_inside, sizeof(truncated_inside)), &error_offset));
    NAOS_CHECK_UINT_EQ(1u, error_offset);
    NAOS_CHECK_INT_EQ(
        AMARK_ERROR_INVALID_UTF8,
        amark_utf8_validate(test_bytes(surrogate_after_char, sizeof(surrogate_after_char)), &error_offset));
    NAOS_CHECK_UINT_EQ(3u, error_offset);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_UTF8,
                      amark_utf8_validate(test_bytes(truncated_at_end, sizeof(truncated_at_end)), &error_offset));
    NAOS_CHECK_UINT_EQ(4u, error_offset);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_UTF8,
                      amark_utf8_validate(test_bytes(lone_after_nul, sizeof(lone_after_nul)), &error_offset));
    NAOS_CHECK_UINT_EQ(1u, error_offset);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_UTF8,
                      amark_utf8_validate(test_bytes(test_table_3_8, sizeof(test_table_3_8)), &error_offset));
    NAOS_CHECK_UINT_EQ(1u, error_offset);
}

NAOS_TEST(validate_without_error_offset)
{
    static const unsigned char lone[] = {0x61, 0x80};

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_UTF8, amark_utf8_validate(test_bytes(lone, sizeof(lone)), NULL));
}

NAOS_TEST(validate_rejects_null_data_with_length)
{
    size_t error_offset = TEST_CONSUMED_SENTINEL;
    amark_slice_t broken;

    /* amark_slice_make は NULL を空スライスにするため、不変条件に反するスライスを直接作る */
    broken.data = NULL;
    broken.len = 3;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_utf8_validate(broken, &error_offset));
    NAOS_CHECK_UINT_EQ(TEST_CONSUMED_SENTINEL, error_offset);
}

NAOS_TEST(decode_previous_after_each_character)
{
    size_t i;

    for (i = 0; i < (sizeof(test_boundaries) / sizeof(test_boundaries[0])); i++)
    {
        const test_char_t *c = &test_boundaries[i];
        unsigned char text[AMARK_UTF8_MAX_LENGTH + 1];
        uint32_t cp = TEST_CP_SENTINEL;
        size_t consumed = TEST_CONSUMED_SENTINEL;
        size_t j;

        /* 先頭に 1 バイトの文字を置き、その後ろの文字を末尾から逆方向に復号する */
        text[0] = 0x61;
        for (j = 0; j < c->len; j++)
        {
            text[j + 1] = c->bytes[j];
        }

        NAOS_TRACE_PUSH("U+%04X", (unsigned)c->cp);
        NAOS_CHECK_INT_EQ(AMARK_OK,
                          amark_utf8_decode_previous(test_bytes(text, c->len + 1), c->len + 1, &cp, &consumed));
        NAOS_CHECK_UINT_EQ(c->cp, cp);
        NAOS_CHECK_UINT_EQ(c->len, consumed);
        NAOS_TRACE_POP();
    }
}

NAOS_TEST(decode_previous_invalid_units)
{
    static const unsigned char truncated[] = {0x61, 0xE3, 0x81};
    static const unsigned char run[] = {0x80, 0x80};
    static const unsigned char extra[] = {0xF0, 0x90, 0x80, 0x80, 0x80};
    static const unsigned char overlong[] = {0xC0, 0x80};
    uint32_t cp = TEST_CP_SENTINEL;
    size_t consumed = TEST_CONSUMED_SENTINEL;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_UTF8,
                      amark_utf8_decode_previous(test_bytes(truncated, sizeof(truncated)), 3, &cp, &consumed));
    NAOS_CHECK_UINT_EQ(AMARK_UTF8_REPLACEMENT_CHARACTER, cp);
    NAOS_CHECK_UINT_EQ(2u, consumed);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_UTF8,
                      amark_utf8_decode_previous(test_bytes(run, sizeof(run)), 2, &cp, &consumed));
    NAOS_CHECK_UINT_EQ(1u, consumed);

    /* 4 バイトの文字の後ろに余った継続バイトは 1 バイトの不正単位。その直前は妥当な 4 バイト */
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_UTF8,
                      amark_utf8_decode_previous(test_bytes(extra, sizeof(extra)), 5, &cp, &consumed));
    NAOS_CHECK_UINT_EQ(1u, consumed);
    NAOS_CHECK_INT_EQ(AMARK_OK, amark_utf8_decode_previous(test_bytes(extra, sizeof(extra)), 4, &cp, &consumed));
    NAOS_CHECK_UINT_EQ(0x10000u, cp);
    NAOS_CHECK_UINT_EQ(4u, consumed);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_UTF8,
                      amark_utf8_decode_previous(test_bytes(overlong, sizeof(overlong)), 2, &cp, &consumed));
    NAOS_CHECK_UINT_EQ(AMARK_UTF8_REPLACEMENT_CHARACTER, cp);
    NAOS_CHECK_UINT_EQ(1u, consumed);
}

NAOS_TEST(decode_previous_matches_forward_for_valid)
{
    check_previous_matches_forward(test_all_boundaries, sizeof(test_all_boundaries));
}

NAOS_TEST(decode_previous_matches_forward_for_invalid)
{
    static const unsigned char truncated[] = {0x61, 0xE3, 0x81, 0x62, 0xE3, 0x81};
    static const unsigned char continuation_run[] = {0x80, 0x80};
    static const unsigned char long_run[] = {0x61, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x62};
    static const unsigned char char_then_run[] = {0xF0, 0x90, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
    static const unsigned char lead_then_run[] = {0xE3, 0x81, 0x82, 0x80, 0x80, 0x80, 0x80};
    static const unsigned char overlong[] = {0x61, 0xC0, 0x80, 0xE0, 0x80, 0x80, 0xC1, 0xBF};
    static const unsigned char surrogate_and_range[] = {0xED, 0xA0, 0x80, 0xF4, 0x90, 0x80, 0x80, 0xF5, 0x62};
    static const unsigned char truncated_four[] = {0xF0, 0x90, 0x80, 0x61, 0xF4, 0x8F, 0xBF};

    check_previous_matches_forward(truncated, sizeof(truncated));
    check_previous_matches_forward(continuation_run, sizeof(continuation_run));
    check_previous_matches_forward(long_run, sizeof(long_run));
    check_previous_matches_forward(char_then_run, sizeof(char_then_run));
    check_previous_matches_forward(lead_then_run, sizeof(lead_then_run));
    check_previous_matches_forward(overlong, sizeof(overlong));
    check_previous_matches_forward(surrogate_and_range, sizeof(surrogate_and_range));
    check_previous_matches_forward(truncated_four, sizeof(truncated_four));
    check_previous_matches_forward(test_table_3_8, sizeof(test_table_3_8));
}

NAOS_TEST(decode_previous_rejects_start_and_out_of_range)
{
    static const unsigned char text[] = {0x61, 0x62};
    amark_slice_t src = test_bytes(text, sizeof(text));
    uint32_t cp = TEST_CP_SENTINEL;
    size_t consumed = TEST_CONSUMED_SENTINEL;

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_utf8_decode_previous(src, 0, &cp, &consumed));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_utf8_decode_previous(src, 3, &cp, &consumed));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_utf8_decode_previous(src, SIZE_MAX, &cp, &consumed));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT,
                      amark_utf8_decode_previous(amark_slice_make(NULL, 0), 0, &cp, &consumed));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_utf8_decode_previous(src, 2, NULL, &consumed));
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_ARGUMENT, amark_utf8_decode_previous(src, 2, &cp, NULL));
    NAOS_CHECK_UINT_EQ(TEST_CP_SENTINEL, cp);
    NAOS_CHECK_UINT_EQ(TEST_CONSUMED_SENTINEL, consumed);
}

/* clang-format off */
NAOS_SUITE(utf8,
    NAOS_ADD_TEST(constants),
    NAOS_ADD_TEST(decode_boundaries),
    NAOS_ADD_TEST(decode_edges),
    NAOS_ADD_TEST(decode_reads_only_first_character),
    NAOS_ADD_TEST(decode_nul_is_valid_character),
    NAOS_ADD_TEST(decode_overlong),
    NAOS_ADD_TEST(decode_surrogate),
    NAOS_ADD_TEST(decode_out_of_range),
    NAOS_ADD_TEST(decode_truncated),
    NAOS_ADD_TEST(decode_lone_continuation),
    NAOS_ADD_TEST(decode_table_3_8_example),
    NAOS_ADD_TEST(decode_rejects_invalid_arguments),
    NAOS_ADD_TEST(encode_boundaries_round_trip),
    NAOS_ADD_TEST(encode_edges),
    NAOS_ADD_TEST(encode_rejects_surrogates_and_out_of_range),
    NAOS_ADD_TEST(encode_rejects_null_arguments),
    NAOS_ADD_TEST(validate_accepts_valid_and_empty),
    NAOS_ADD_TEST(validate_reports_first_invalid_offset),
    NAOS_ADD_TEST(validate_without_error_offset),
    NAOS_ADD_TEST(validate_rejects_null_data_with_length),
    NAOS_ADD_TEST(decode_previous_after_each_character),
    NAOS_ADD_TEST(decode_previous_invalid_units),
    NAOS_ADD_TEST(decode_previous_matches_forward_for_valid),
    NAOS_ADD_TEST(decode_previous_matches_forward_for_invalid),
    NAOS_ADD_TEST(decode_previous_rejects_start_and_out_of_range)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(utf8)
)
/* clang-format on */
