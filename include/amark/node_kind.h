/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * ノード種別: 種別の識別値・分類・内容モデル、標準種別の問い合わせ、葉の判定と親子の受け入れ判定、
 * 拡張ノード種別のレジストリ。
 *
 * 種別はハンドル（const amark_node_kind_t *）で表す。標準種別（CommonMark の 18 種）のハンドルは
 * amark_node_kind_get_standard で得られ、どのレジストリにも依存せず、プロセス内で常に同一である。
 * 拡張種別のハンドルはレジストリが返し、そのレジストリが破棄されるまで同一のまま有効である。
 * 種別はハンドルの一致で識別できる。
 *
 * 共通の規約:
 * - amark_error_t を返す関数は、必須のポインタ引数が NULL なら AMARK_ERROR_INVALID_ARGUMENT を返し、
 *   何も変更しない。失敗したときはレジストリと出力引数を呼び出し前のまま保つ
 * - 値を返す問い合わせ関数は失敗しない。NULL を渡されたときは「ない」を表す値（NULL、{NULL, 0}、
 *   false、AMARK_NODE_NONE、AMARK_NODE_CATEGORY_NONE、AMARK_NODE_CONTENT_NONE）を返す
 * - メモリはグローバルアロケータ（amark_malloc 系）だけで確保・解放する
 *
 * 利用者の義務（ライブラリはいずれも検出しない）:
 * - レジストリは、そのレジストリに登録した種別のノードがすべて破棄されるまで破棄してはならない。
 *   また、その種別のハンドルを保持する他のオブジェクト（レンダラのフックなど）より長く生きなければならない
 * - コールバック（受け入れ判定 amark_node_can_contain_fn_t、付随データの後片付け
 *   amark_node_destroy_data_fn_t）の中から、そのコールバックを呼んだ木やレジストリを変更してはならない
 * - 受け入れ判定関数は決定的でなければならない。同じ親と子の組には常に同じ結果を返し、木を変更しない
 *
 * 並行利用:
 * - amark_node_registry_add は、同じレジストリに対する他の操作と同時に行ってはならない
 * - 登録が終わったレジストリは、amark_node_registry_find と、その種別を使うノードの生成・操作
 *   （異なる木に対するもの）を複数のスレッドから同時に行ってよい。種別は登録後に変更されない
 * - 標準種別の問い合わせと判定関数は可変の状態を持たず、どのスレッドからでも同時に呼べる
 */
#ifndef AMARK_NODE_KIND_H
#define AMARK_NODE_KIND_H

#include <amark/error.h>
#include <amark/export.h>
#include <amark/slice.h>

#include <stdbool.h>

/*
 * 種別の識別値。値は並び順で決まり、ライブラリの版を通じて変わらない。標準種別は CommonMark の
 * 18 種で閉じており、並べ替え・削除・挿入はしない。新しい種別は拡張として AMARK_NODE_EXTENSION で表す
 */
typedef enum amark_node_type
{
    AMARK_NODE_NONE, /* 種別なし（NULL の問い合わせ結果） */
    AMARK_NODE_DOCUMENT,
    AMARK_NODE_BLOCK_QUOTE,
    AMARK_NODE_LIST,
    AMARK_NODE_ITEM,
    AMARK_NODE_CODE_BLOCK,
    AMARK_NODE_HTML_BLOCK,
    AMARK_NODE_PARAGRAPH,
    AMARK_NODE_HEADING,
    AMARK_NODE_THEMATIC_BREAK,
    AMARK_NODE_TEXT,
    AMARK_NODE_SOFT_BREAK,
    AMARK_NODE_LINE_BREAK,
    AMARK_NODE_CODE, /* code span */
    AMARK_NODE_HTML_INLINE,
    AMARK_NODE_EMPHASIS,
    AMARK_NODE_STRONG,
    AMARK_NODE_LINK,
    AMARK_NODE_IMAGE,
    AMARK_NODE_EXTENSION /* すべての拡張種別に共通の値 */
} amark_node_type_t;

/* ブロック / インラインの別 */
typedef enum amark_node_category
{
    AMARK_NODE_CATEGORY_NONE, /* 分類なし（NULL の問い合わせ結果） */
    AMARK_NODE_CATEGORY_BLOCK,
    AMARK_NODE_CATEGORY_INLINE
} amark_node_category_t;

/* 子の受け入れ規則（内容モデル） */
typedef enum amark_node_content
{
    AMARK_NODE_CONTENT_NONE,    /* 子を取らない（葉） */
    AMARK_NODE_CONTENT_BLOCKS,  /* 明示親限定でない任意のブロック */
    AMARK_NODE_CONTENT_INLINES, /* 明示親限定でない任意のインライン */
    AMARK_NODE_CONTENT_CUSTOM   /* 種別の判定関数で決める */
} amark_node_content_t;

/* ノード種別（opaque）。標準種別はライブラリが、拡張種別はレジストリが所有する */
typedef struct amark_node_kind amark_node_kind_t;

/* 拡張ノード種別を登録・保持・検索するレジストリ（opaque）。グローバルな登録状態はない */
typedef struct amark_node_registry amark_node_registry_t;

/* 拡張種別の名前の最大バイト数 */
#define AMARK_NODE_KIND_NAME_MAX_LENGTH 64

AMARK_BEGIN_DECLS

/*
 * 受け入れ判定関数（content == AMARK_NODE_CONTENT_CUSTOM の種別）。parent の種別の子として child の
 * 種別を置けるなら true を返す。決定的でなければならず、木やレジストリを変更してはならない。
 * user_data は登録時の記述子の user_data
 */
typedef bool (*amark_node_can_contain_fn_t)(const amark_node_kind_t *parent, const amark_node_kind_t *child,
                                            void *user_data);

/*
 * 付随データの後片付け関数。拡張種別のノードの付随データ data（NULL でない）を置き換えるとき、または
 * ノードを破棄するときに 1 回呼ばれる。木やレジストリを変更してはならない。user_data は登録時の
 * 記述子の user_data
 */
typedef void (*amark_node_destroy_data_fn_t)(void *data, void *user_data);

/* 拡張ノード種別の記述子。amark_node_registry_add に渡す。登録時に名前を複製し、記述子自体は保持しない */
typedef struct amark_node_kind_desc
{
    amark_slice_t name;             /* 1〜64 バイト、[a-z][a-z0-9_]* */
    amark_node_category_t category; /* BLOCK または INLINE */
    amark_node_content_t content;
    bool requires_explicit_parent;             /* 真なら BLOCKS / INLINES の親には置けない */
    amark_node_can_contain_fn_t can_contain;   /* content == CUSTOM なら必須、それ以外は NULL */
    amark_node_destroy_data_fn_t destroy_data; /* NULL 可（付随データを解放しない） */
    void *user_data; /* 両関数に渡す。所有しない。レジストリより長く生きること */
} amark_node_kind_desc_t;

/*
 * 識別値 type の標準種別のハンドルを返す。AMARK_NODE_DOCUMENT〜AMARK_NODE_IMAGE 以外
 * （AMARK_NODE_NONE、AMARK_NODE_EXTENSION、未定義値）は NULL を返す
 */
AMARK_API const amark_node_kind_t *amark_node_kind_get_standard(amark_node_type_t type);

/* 種別の識別値を返す。標準種別はその値、拡張種別は AMARK_NODE_EXTENSION、NULL は AMARK_NODE_NONE */
AMARK_API amark_node_type_t amark_node_kind_get_type(const amark_node_kind_t *kind);

/*
 * 種別の名前を借用スライスで返す（data[len] == '\0'）。寿命は種別と同じ。NULL は {NULL, 0}
 */
AMARK_API amark_slice_t amark_node_kind_get_name(const amark_node_kind_t *kind);

/* 種別の分類を返す。NULL は AMARK_NODE_CATEGORY_NONE */
AMARK_API amark_node_category_t amark_node_kind_get_category(const amark_node_kind_t *kind);

/* 種別の内容モデルを返す。NULL は AMARK_NODE_CONTENT_NONE */
AMARK_API amark_node_content_t amark_node_kind_get_content(const amark_node_kind_t *kind);

/*
 * 種別が子を取らない種別（葉、content == AMARK_NODE_CONTENT_NONE）なら true を返す。それ以外と NULL は
 * false。標準種別と拡張種別に同じ方法で答える。イテレータが ENTER だけを報告する種別は、ちょうど
 * この判定が真の種別である
 */
AMARK_API bool amark_node_kind_is_leaf(const amark_node_kind_t *kind);

/*
 * parent の種別のノードの子として child の種別のノードを置けるなら true を返す。どちらかが NULL、
 * child が document、parent が葉なら false。BLOCKS / INLINES はそれぞれ明示親限定でない任意の
 * ブロック / インラインを受け入れ、CUSTOM は parent の判定関数の結果に従う。木を変更せず、
 * 同じ組には常に同じ結果を返す（判定関数が決定的である限り）。編集操作はこの判定を使う
 */
AMARK_API bool amark_node_kind_can_contain(const amark_node_kind_t *parent, const amark_node_kind_t *child);

/*
 * 空のレジストリを生成して *registry に書く。確保に失敗したら AMARK_ERROR_NO_MEMORY を返し、
 * *registry を変更しない。amark_node_registry_destroy で破棄する
 */
AMARK_API amark_error_t amark_node_registry_create(amark_node_registry_t **registry);

/*
 * レジストリと、登録されたすべての拡張種別（名前を含む）を解放する。NULL は何もしない。
 * そのレジストリの種別のノードがすべて破棄された後で呼ばなければならない（検出しない）
 */
AMARK_API void amark_node_registry_destroy(amark_node_registry_t *registry);

/*
 * desc に従って拡張ノード種別を登録し、*kind にハンドルを書く。名前は複製して保持する。
 * 名前の形式違反と記述子の矛盾は AMARK_ERROR_INVALID_ARGUMENT、標準種別または登録済みの名前との
 * 重複は AMARK_ERROR_ALREADY_EXISTS、確保の失敗は AMARK_ERROR_NO_MEMORY を返す。失敗時は
 * レジストリと *kind を変更しない。同じレジストリに対する他の操作と同時に呼んではならない
 */
AMARK_API amark_error_t amark_node_registry_add(amark_node_registry_t *registry, const amark_node_kind_desc_t *desc,
                                                const amark_node_kind_t **kind);

/*
 * 名前がバイト単位で一致する標準種別、次に登録済みの拡張種別を返す。どれにも一致しない、または
 * registry が NULL なら NULL を返す
 */
AMARK_API const amark_node_kind_t *amark_node_registry_find(const amark_node_registry_t *registry, amark_slice_t name);

AMARK_END_DECLS

#endif /* AMARK_NODE_KIND_H */
