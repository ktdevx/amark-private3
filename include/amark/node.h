/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * 文書の木のノード: 生成・取り外し・破棄、種別の取得、親・子・兄弟の参照、種別ごとの値と
 * 拡張種別の付随データ、親子規則を検証する編集操作、正規化。
 *
 * 所有規則:
 * - 親を持たないノード（根）は、生成した、または取り外し・置換で受け取った呼び出し側が所有し、
 *   amark_node_destroy で破棄する
 * - 親を持つノードは親が所有し、親の破棄とともに破棄される
 * - ノードは種別のハンドルを参照する。拡張種別のノードは、その種別を登録したレジストリより先に
 *   破棄しなければならない（amark/node_kind.h を参照）
 *
 * 共通の規約:
 * - amark_error_t を返す関数は、必須のポインタ引数が NULL なら AMARK_ERROR_INVALID_ARGUMENT を返し、
 *   何も変更しない。失敗したときは木とノードの値を呼び出し前のまま保つ（amark_node_normalize を除く）
 * - 値を返す問い合わせ関数（getter、参照）は失敗しない。NULL や値を持たない種別には「ない」を表す値を返す
 * - const amark_node_t * を受け取ってノードを返す参照関数は、非 const の amark_node_t * を返す
 *   （strchr と同じ慣習。木は呼び出し側がもともと非 const で所有している）
 * - メモリはグローバルアロケータ（amark_malloc 系）だけで確保・解放する
 * - 破棄・正規化は反復で行い、木の深さに比例するスタックを使わない
 * - 付随データの後片付け関数の中から、その関数を呼んだ木やレジストリを変更してはならない（検出しない）
 *
 * 並行利用:
 * - 異なる木は、別々のスレッドから同時に操作してよい（登録が終わったレジストリの種別を共有していてもよい）
 * - 同じ木を複数のスレッドから同時に操作してはならない。変更を伴わない読み取りだけなら同時に行ってよい
 */
#ifndef AMARK_NODE_H
#define AMARK_NODE_H

#include <amark/error.h>
#include <amark/export.h>
#include <amark/node_kind.h>
#include <amark/slice.h>

#include <stdbool.h>
#include <stdint.h>

/* 文書の木のノード（opaque） */
typedef struct amark_node amark_node_t;

/* リストの種類 */
typedef enum amark_node_list_type
{
    AMARK_NODE_LIST_NONE, /* list 以外への問い合わせ結果 */
    AMARK_NODE_LIST_BULLET,
    AMARK_NODE_LIST_ORDERED
} amark_node_list_type_t;

/* 見出しレベルの範囲 */
#define AMARK_NODE_HEADING_LEVEL_MIN 1
#define AMARK_NODE_HEADING_LEVEL_MAX 6

/* 順序付きリストの開始番号の最大値（最小値は 0） */
#define AMARK_NODE_LIST_START_MAX 999999999u

AMARK_BEGIN_DECLS

/* 生成・破棄・取り外し */

/*
 * 標準種別 type の新しいノードを生成して *node に書く。type が標準種別でない（AMARK_NODE_NONE、
 * AMARK_NODE_EXTENSION、未定義値）なら AMARK_ERROR_INVALID_ARGUMENT を返す。それ以外は
 * amark_node_create_with_kind と同じ
 */
AMARK_API amark_error_t amark_node_create(amark_node_type_t type, amark_node_t **node);

/*
 * 種別 kind の新しいノードを生成して *node に書く。ノードは親・子・兄弟を持たず、値は既定値
 * （見出しレベル 1、リスト種別 BULLET・開始番号 1・tight は偽、文字列は空、付随データは NULL）である。
 * 確保に失敗したら AMARK_ERROR_NO_MEMORY を返し、*node を変更しない。kind は標準種別のハンドルか、
 * 破棄されていないレジストリが返したハンドルでなければならない（検証しない）
 */
AMARK_API amark_error_t amark_node_create_with_kind(const amark_node_kind_t *kind, amark_node_t **node);

/*
 * ノードとその子孫をすべて破棄し、所有する値を解放する。拡張種別のノードの付随データが NULL でなく
 * 後片付け関数があれば、それを 1 回呼ぶ。親を持つノードは先に親から取り外す。NULL は何もしない
 */
AMARK_API void amark_node_destroy(amark_node_t *node);

/*
 * ノードを親から取り外し、子孫ごと根にする。所有権は呼び出し側へ移る。親を持たないノードと NULL は
 * 何もしない
 */
AMARK_API void amark_node_unlink(amark_node_t *node);

/* 種別 */

/* ノードの種別の識別値を返す。拡張種別は AMARK_NODE_EXTENSION、NULL は AMARK_NODE_NONE */
AMARK_API amark_node_type_t amark_node_get_type(const amark_node_t *node);

/* ノードの種別のハンドルを返す。NULL は NULL */
AMARK_API const amark_node_kind_t *amark_node_get_kind(const amark_node_t *node);

/* 参照（木を変更しない。該当するノードがない場合と NULL は NULL を返す） */

AMARK_API amark_node_t *amark_node_get_parent(const amark_node_t *node);
AMARK_API amark_node_t *amark_node_get_first_child(const amark_node_t *node);
AMARK_API amark_node_t *amark_node_get_last_child(const amark_node_t *node);
AMARK_API amark_node_t *amark_node_get_next(const amark_node_t *node);
AMARK_API amark_node_t *amark_node_get_previous(const amark_node_t *node);

/*
 * 値のアクセサ
 *
 * 値を持つ種別:
 * - heading_level: HEADING（既定 1、AMARK_NODE_HEADING_LEVEL_MIN〜AMARK_NODE_HEADING_LEVEL_MAX）
 * - list_type、list_start、list_tight: LIST（既定 BULLET、1、偽。list_start は 0〜AMARK_NODE_LIST_START_MAX）
 * - literal: CODE_BLOCK、HTML_BLOCK、TEXT、CODE、HTML_INLINE（既定は空文字列）
 * - info: CODE_BLOCK（既定は空文字列）
 * - url、title: LINK、IMAGE（既定は空文字列）
 * - data（付随データ）: 拡張種別（既定 NULL）
 *
 * setter は、node が NULL、その値を持たない種別、範囲外の値なら AMARK_ERROR_INVALID_ARGUMENT を返し、
 * ノードを変更しない。文字列の setter は値を複製して保持し（NUL バイトを含む値も長さどおり）、
 * 不正な UTF-8 は AMARK_ERROR_INVALID_UTF8、サイズのあふれは AMARK_ERROR_OVERFLOW、確保の失敗は
 * AMARK_ERROR_NO_MEMORY を返す。いずれの失敗でも旧値は残る。
 *
 * 文字列の getter は、値を持つ種別では data が NULL でなく data[len] == '\0' の借用スライスを返す。
 * スライスは次にその値を設定するか、ノードが破棄されるまで有効で、値を設定し直すと無効になる。
 * 値を持たない種別と NULL は {NULL, 0} を返す。数値・真偽値の getter は、値を持たない種別と NULL に
 * heading_level と list_start は 0、list_type は AMARK_NODE_LIST_NONE、list_tight は false を返す
 * （list_start の 0 は有効値でもあるため、種別は amark_node_get_type で確かめる）
 */

AMARK_API int amark_node_get_heading_level(const amark_node_t *node);
AMARK_API amark_error_t amark_node_set_heading_level(amark_node_t *node, int level);

AMARK_API amark_node_list_type_t amark_node_get_list_type(const amark_node_t *node);
AMARK_API amark_error_t amark_node_set_list_type(amark_node_t *node, amark_node_list_type_t type);
AMARK_API uint32_t amark_node_get_list_start(const amark_node_t *node);
AMARK_API amark_error_t amark_node_set_list_start(amark_node_t *node, uint32_t start);
AMARK_API bool amark_node_get_list_tight(const amark_node_t *node);
AMARK_API amark_error_t amark_node_set_list_tight(amark_node_t *node, bool tight);

AMARK_API amark_slice_t amark_node_get_literal(const amark_node_t *node);
AMARK_API amark_error_t amark_node_set_literal(amark_node_t *node, amark_slice_t literal);
AMARK_API amark_slice_t amark_node_get_info(const amark_node_t *node);
AMARK_API amark_error_t amark_node_set_info(amark_node_t *node, amark_slice_t info);
AMARK_API amark_slice_t amark_node_get_url(const amark_node_t *node);
AMARK_API amark_error_t amark_node_set_url(amark_node_t *node, amark_slice_t url);
AMARK_API amark_slice_t amark_node_get_title(const amark_node_t *node);
AMARK_API amark_error_t amark_node_set_title(amark_node_t *node, amark_slice_t title);

/* 拡張種別のノードの付随データを返す。設定がない、標準種別、NULL は NULL を返す */
AMARK_API void *amark_node_get_data(const amark_node_t *node);

/*
 * 拡張種別のノードに付随データ data を設定する（NULL は解除）。node が NULL または標準種別なら
 * AMARK_ERROR_INVALID_ARGUMENT を返し、data の所有権は移らない。data が現在の付随データと同じなら
 * 何もしない。それ以外は、旧データが NULL でなく後片付け関数があれば、それを旧データに対して 1 回
 * 呼んでから data を保持する
 */
AMARK_API amark_error_t amark_node_set_data(amark_node_t *node, void *data);

/*
 * 編集操作
 *
 * 引数は「位置の基準（親・兄弟・置換対象）、動かすノード」の順に取る。新しい親は、append_child /
 * prepend_child では parent、insert_before / insert_after では sibling の親、replace では target の親である。
 * すべての検証をリンクの変更前に行い、失敗したら何も変更しない:
 * - 引数が NULL、動かすノードと基準のノードが同一、新しい親がない（兄弟挿入・置換の基準が根）:
 *   AMARK_ERROR_INVALID_ARGUMENT
 * - 動かすノードが新しい親またはその祖先である（循環）、または新しい親の種別が動かすノードの種別を
 *   受け入れない（amark_node_kind_can_contain）: AMARK_ERROR_INVALID_CHILD
 * 既に親を持つノードは取り外してから置く（移動）。子の順序は指定どおりに保つ
 */

AMARK_API amark_error_t amark_node_append_child(amark_node_t *parent, amark_node_t *child);
AMARK_API amark_error_t amark_node_prepend_child(amark_node_t *parent, amark_node_t *child);
AMARK_API amark_error_t amark_node_insert_before(amark_node_t *sibling, amark_node_t *node);
AMARK_API amark_error_t amark_node_insert_after(amark_node_t *sibling, amark_node_t *node);

/*
 * target の位置に replacement を置き、target を子孫ごと根にする（所有権は呼び出し側へ移る）。
 * replacement は target の部分木内にあってもよい（中身で包みを置き換える）
 */
AMARK_API amark_error_t amark_node_replace(amark_node_t *target, amark_node_t *replacement);

/*
 * root の部分木で、兄弟として隣接する text ノードを内容を順に連結した 1 つの text ノードに結合し、
 * 子孫のうち内容が空の text ノードを取り除く（root 自身は取り除かない）。text 以外のノードの種別・値・
 * 順序は変えない。正規化済みの部分木は変更しない。連結長のあふれは AMARK_ERROR_OVERFLOW、確保の失敗は
 * AMARK_ERROR_NO_MEMORY を返してその時点で止める。このとき木は親子規則を満たし、各兄弟並びの text の
 * 連結内容は正規化前と同じだが、一部の並びは正規化済みでない（他の関数と異なり呼び出し前の状態には戻さない）
 */
AMARK_API amark_error_t amark_node_normalize(amark_node_t *root);

AMARK_END_DECLS

#endif /* AMARK_NODE_H */
