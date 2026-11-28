/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * enter / exit イテレータ: 起点のノードの部分木を深さ優先の前順で、子を取れる種別には ENTER と EXIT を、
 * 葉の種別（amark_node_kind_is_leaf が真）には ENTER だけを報告する。起点の外へは出ない。
 *
 * イテレータは呼び出し側の領域に置く構造体で、amark_node_iter_init で初期化する。所有する資源が
 * ないため後片付けの関数はなく、不要になれば捨ててよい。確保を行わず、amark_node_iter_next は
 * 失敗しない。次の位置は amark_node_iter_next の呼び出しのたびに現在ノードのリンクから計算するため、
 * 走査中に木を編集できる。反復で動き、木の深さに比例するスタックを使わない。
 *
 * 走査中の編集の安全性規則（現在位置を (N, E)、起点を R とする）:
 *
 * 安全な編集（走査を続けてよい）:
 * - 任意のノードの値・付随データの変更。走査結果に影響しない
 * - N とその祖先（R まで）以外のノードの挿入・取り外し・置換・破棄。まだ報告していない位置は編集後の
 *   木に従って報告する。報告済みの位置へ加えたノードは報告しない
 * - E が ENTER のときの N の子の追加・取り外し・置換・破棄。編集後の子を報告する
 * - E が EXIT のときの N の子の編集。子はもう報告しない
 * - N の後ろへの兄弟の挿入。挿入した兄弟を報告する
 *
 * 走査位置の設定し直し（amark_node_iter_reset）が必要な編集:
 * - N またはその祖先の取り外し: N が R でなければ、次の amark_node_iter_next は DONE を返す。
 *   走査を続けるには、先に amark_node_iter_reset で R の部分木内の有効な位置へ設定し直す
 * - N またはその祖先の置換・移動: amark_node_iter_reset で設定し直すまで結果は未規定
 * - N またはその祖先の破棄: amark_node_iter_reset で設定し直すまで amark_node_iter_next を呼んでは
 *   ならない（未定義動作）
 * - R の破棄: 以後そのイテレータを使ってはならない
 *
 * 同じ木を複数のスレッドから同時に走査・編集してはならない（amark/node.h の並行利用の規則に従う）。
 */
#ifndef AMARK_NODE_ITER_H
#define AMARK_NODE_ITER_H

#include <amark/error.h>
#include <amark/export.h>
#include <amark/node.h>

/* イテレータのイベント */
typedef enum amark_node_iter_event
{
    AMARK_NODE_ITER_NONE, /* まだ amark_node_iter_next を呼んでいない */
    AMARK_NODE_ITER_ENTER,
    AMARK_NODE_ITER_EXIT,
    AMARK_NODE_ITER_DONE
} amark_node_iter_event_t;

/*
 * enter / exit イテレータ。呼び出し側の領域に置けるよう中身を公開するが、フィールドは非公開扱いとし、
 * 直接読み書きせず下の関数を使う
 */
typedef struct amark_node_iter
{
    amark_node_t *root;
    amark_node_t *node;
    amark_node_iter_event_t event;
} amark_node_iter_t;

AMARK_BEGIN_DECLS

/*
 * root を起点に走査するようイテレータを初期化する。iter が NULL なら何もしない。root が NULL なら
 * 最初の amark_node_iter_next が AMARK_NODE_ITER_DONE を返す
 */
AMARK_API void amark_node_iter_init(amark_node_iter_t *iter, amark_node_t *root);

/*
 * 次の位置へ進み、そのイベントを返す。走査を終えると AMARK_NODE_ITER_DONE を返し、以後も返し続ける。
 * iter が NULL なら AMARK_NODE_ITER_DONE を返す。起点でないノードが親を失っていれば（現在ノードか
 * その祖先が取り外された）AMARK_NODE_ITER_DONE を返す
 */
AMARK_API amark_node_iter_event_t amark_node_iter_next(amark_node_iter_t *iter);

/* 現在のノードを返す。AMARK_NODE_ITER_NONE と AMARK_NODE_ITER_DONE の状態、および iter が NULL なら NULL */
AMARK_API amark_node_t *amark_node_iter_get_node(const amark_node_iter_t *iter);

/* 現在のイベントを返す */
AMARK_API amark_node_iter_event_t amark_node_iter_get_event(const amark_node_iter_t *iter);

/*
 * 現在位置を (node, event) に設定し直す。次の amark_node_iter_next はその続きを報告する（例えば
 * (node, AMARK_NODE_ITER_EXIT) に設定すると node の子を飛ばす）。iter または node が NULL、event が
 * AMARK_NODE_ITER_ENTER / AMARK_NODE_ITER_EXIT 以外、node が起点またはその子孫でない、葉の種別に
 * AMARK_NODE_ITER_EXIT を指定した場合は AMARK_ERROR_INVALID_ARGUMENT を返し、イテレータを変更しない
 */
AMARK_API amark_error_t amark_node_iter_reset(amark_node_iter_t *iter, amark_node_t *node,
                                              amark_node_iter_event_t event);

AMARK_END_DECLS

#endif /* AMARK_NODE_ITER_H */
