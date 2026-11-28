/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * 種別ごとの振り分け走査（非公開。html-renderer と markdown-renderer の内部契約）。種別ハンドルを
 * キーとする処理（フック）の表と、表を引きながら公開のイテレータで部分木を回す走査の駆動部を持つ。
 *
 * - 宣言に AMARK_API を付けず、動的ライブラリから export しない。公開ヘッダから include しない
 * - 公開 API（amark_node_iter_*、amark_node_get_kind、amark_node_kind_is_leaf、amark_malloc 系）
 *   だけの上に作り、tree/node_internal.h を include しない
 * - レンダラ固有の処理（出力の形式、行頭プレフィックス、折り返し、出力の巻き戻し）を持たない
 * - 確保は表の伸長だけで行い、走査の実行は確保しない
 *
 * 処理の関数はレンダラごとに型が異なるため、汎用の関数ポインタ型 amark_node_walk_hook_fn_t で保持し、
 * 取り出した側が元の型へ戻して呼ぶ（C99 で往復が保証される関数ポインタ間の変換）。
 */
#ifndef AMARK_TREE_NODE_WALK_H
#define AMARK_TREE_NODE_WALK_H

#include <amark/error.h>
#include <amark/node_iter.h>
#include <amark/node_kind.h>

#include <stdbool.h>
#include <stddef.h>

/* 保持用の汎用の処理の型。呼び出し側がレンダラの型へ戻す */
typedef void (*amark_node_walk_hook_fn_t)(void);

/* 表の 1 項目 */
typedef struct amark_node_walk_hook
{
    const amark_node_kind_t *kind;
    amark_node_walk_hook_fn_t fn;
    void *user_data;
} amark_node_walk_hook_t;

/* 種別ハンドルをキーとする処理の表。空の表は items == NULL、len == cap == 0 */
typedef struct amark_node_walk_table
{
    amark_node_walk_hook_t *items;
    size_t len;
    size_t cap;
} amark_node_walk_table_t;

/* 空の表にする。確保しない。table が NULL なら何もしない */
void amark_node_walk_table_init(amark_node_walk_table_t *table);

/* 表のために確保したメモリを解放して空の表に戻す。table が NULL なら何もしない */
void amark_node_walk_table_deinit(amark_node_walk_table_t *table);

/*
 * kind の処理を設定する。
 * - table または kind が NULL: AMARK_ERROR_INVALID_ARGUMENT
 * - 登録済みで fn != NULL: その項目の fn と user_data を置き換える（確保しない）
 * - 登録済みで fn == NULL: その項目を取り除く（末尾と入れ替える。縮小の再確保はしない）
 * - 未登録で fn == NULL: 何もせず AMARK_OK
 * - 未登録で fn != NULL: 必要なら容量を 2 倍（初期 4）に伸長して末尾に追加する。容量計算のあふれは
 *   AMARK_ERROR_OVERFLOW、確保失敗は AMARK_ERROR_NO_MEMORY を返し、表を変更しない
 */
amark_error_t amark_node_walk_table_set(amark_node_walk_table_t *table, const amark_node_kind_t *kind,
                                        amark_node_walk_hook_fn_t fn, void *user_data);

/*
 * kind とポインタが等しい項目を返す。なければ NULL。table または kind が NULL、または登録が 0 件なら
 * 探索せずに NULL を返す。計算量は登録数に比例する
 */
const amark_node_walk_hook_t *amark_node_walk_table_find(const amark_node_walk_table_t *table,
                                                         const amark_node_kind_t *kind);

/* 走査の状態。訪問関数は node・event・skip_children 以外を読み書きしない */
typedef struct amark_node_walk
{
    amark_node_iter_t iter;
    const amark_node_t *node;      /* 現在のノード */
    amark_node_iter_event_t event; /* 現在のイベント（ENTER / EXIT） */
    bool skip_children;            /* 現在のイベントで子の省略が要求されたか */
} amark_node_walk_t;

/*
 * 訪問関数。hook は現在のノードの種別で表を引いた項目（なければ NULL）。AMARK_OK 以外を返すと走査を
 * 直ちに中止し、その値が amark_node_walk_run の戻り値になる
 */
typedef amark_error_t (*amark_node_walk_visit_fn_t)(void *ctx, amark_node_walk_t *walk,
                                                    const amark_node_walk_hook_t *hook);

/*
 * root の部分木を公開のイテレータで反復し、各イベントについて walk->node・walk->event を設定し、
 * walk->skip_children を false にし、表を引いて visit(ctx, walk, hook) を呼ぶ。子を取りうる種別は
 * ENTER と EXIT の 2 回、葉の種別は ENTER の 1 回だけ訪問する。
 *
 * 訪問関数が子を取りうる種別のノードの ENTER で amark_node_walk_skip_children を呼ぶと、そのノードの
 * 子孫を訪問せず、続けて同じノードの EXIT を（同じ hook で）ちょうど 1 回訪問し、以後はその EXIT の
 * 続きから走査を進める。ENTER 以外のイベントと葉での要求は無視する。
 *
 * walk、root、table、visit のいずれかが NULL なら AMARK_ERROR_INVALID_ARGUMENT。訪問関数のエラーは
 * そのまま返す（出力の巻き戻しなどの後始末は呼び出し側が行う）。走査は木を変更せず、確保しない。
 * 走査中に木を変更してはならない
 */
amark_error_t amark_node_walk_run(amark_node_walk_t *walk, const amark_node_t *root,
                                  const amark_node_walk_table_t *table, amark_node_walk_visit_fn_t visit, void *ctx);

/* 現在のイベントで子の省略を要求する。walk が NULL なら何もしない */
void amark_node_walk_skip_children(amark_node_walk_t *walk);

#endif /* AMARK_TREE_NODE_WALK_H */
