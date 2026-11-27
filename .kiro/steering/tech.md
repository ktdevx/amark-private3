# Technology Stack

## Architecture

構文非依存のパーサエンジンに、構文を拡張として登録する構成を取る。

- **2 フェーズ解析**: 行単位のブロック解析の後にインライン解析を行う
- **エンジンは構文を知らない**: エンジン内に特定構文の名前による分岐を持たない。CommonMark の各構文も公開拡張 API だけを使って実装する。エンジンの内部に手を入れたくなったら、裏口を作らず拡張 API を改訂する
- **方言は拡張セット**: CommonMark などの方言は拡張の集合として定義する
- **値は木に持つ**: ノードは値を複製して所有する。解析中の一時状態（開いているブロック、デリミタスタックなど）はノードに持たせない
- **レンダラは表引き**: ノード種別ごとの出力関数を呼び出す。HTML と Markdown のレンダラは同じ形のフック機構を持つ

## Core Technologies

- **Language**: C99（コンパイラ拡張に依存しない）
- **Build**: CMake（3.21 以上）
- **Platforms**: Windows（MSVC）/ Linux（GCC、Clang）/ macOS（Clang）
- **Artifacts**: 静的ライブラリと動的ライブラリの両方
- **License**: MIT。copyright 表記は `The Amark Authors`

## Key Libraries

- **[Naos](https://github.com/vabelorg/naos)**: テストフレームワーク。テスト時のみ使用し、FetchContent でタグを固定して取り込む（1.0 前で互換性が壊れうるため）。CTest への登録は `naos_add_executable` で行う
- ライブラリ本体は C 標準ライブラリ以外に依存しない

## Development Standards

### メモリ

ライブラリ内の動的確保はすべて、差し替え可能なグローバルアロケータを経由する: `amark_malloc`, `amark_realloc`, `amark_calloc`, `amark_free`。`malloc` などを直接呼ばない（直接呼ぶのは `src/base/memory.c` の既定アロケータだけ）。

- アロケータは `malloc_fn` / `realloc_fn` / `calloc_fn` / `free_fn` の 4 関数で、利用者データを持たない（プロセス全体で 1 つのため）
- サイズ 0 の要求（`amark_malloc(0)`、要素数 0 の `amark_calloc`）はアロケータを呼ばずに `NULL` を返し、`amark_realloc(p, 0)` は `p` を解放して `NULL` を返す。`amark_free(NULL)` は何もしない。利用者のアロケータ関数にはサイズ 0 と `NULL` のポインタが渡らない
- したがって `NULL` が確保失敗を意味するのは要求サイズが 0 でないときだけである。**ライブラリのコードは、`NULL` を `AMARK_ERROR_NO_MEMORY` と解釈する箇所でサイズ 0 を要求しない**。個数が 0 になりうる配列・表・スタックは確保の前に個数を確かめ、空は `NULL` と個数 0 で表す（確保は個数が 1 以上のときだけ）
- 正本は `foundation` spec の設計（Memory）

### 文字列の所有権

- 借用は `amark_slice_t`（先頭とバイト長。NUL 終端を仮定せず、NUL を含みうる）。参照先の寿命は貸し手が保証する
- 所有は `amark_malloc` 系で確保した `char *` と長さの組で、常に `data[len] == '\0'`。所有者が `amark_free` で解放する
- 正本は `foundation` spec の設計（Slice）

### エラー処理

エラーコードはライブラリ全体で単一の列挙 `amark_error_t` とする。モジュールや spec ごとの独自体系を作らず、必要なコードは共通の列挙に追加する。

- 失敗しうる公開関数は `amark_error_t` を返し、結果は出力引数で返す。成功は `AMARK_OK`（一覧の先頭なので 0。`if (error)` で判定できる）
- 全コードは `include/amark/error.h` の X マクロ `AMARK_ERROR_LIST(X)` に `X(名前, 文字列)` として 1 回だけ書き、列挙と `amark_error_string` はそこから生成する。列挙子に明示的な数値を書かない（値は並び順で決まる）。一覧の中に spec ごとの区切りコメントを置かない
- **各コードの追加した spec・文字列・意味の正本は `foundation` spec の設計にあるエラーコード一覧表**（`AMARK_ERROR_LIST` と同じ並び）。他の spec の設計は表を参照し、文字列や意味を定義し直さない
- 追加は末尾のみ。並べ替え・削除・再利用は禁止（これで公開済みの値が変わらない）。新しいコードが必要な spec は一覧表の末尾へ行を追記し、`AMARK_ERROR_LIST` の末尾へ `X(...)` を 1 項目追記する。`src/base/error.c` と `tests/error_test.c` は一覧から生成・走査するため変更しない
- どの spec も、意味が合えばどのコードでも使う。「追加した spec」は使える spec を限定しない

### 命名

- 公開シンボルは `amark_`、マクロと列挙定数は `AMARK_` で始める
- typedef した型名には `_t` を付ける
- 構造体の中身の初期化・後片付けを行う関数は `_init` / `_deinit`（領域は呼び出し側が持つ）
- opaque な構造体の生成・破棄を行う関数は `_create` / `_destroy`（領域はライブラリが確保する）

```c
typedef struct amark_buffer amark_buffer_t;

void amark_buffer_init(amark_buffer_t *buffer);   /* 確保しないので失敗しない */
void amark_buffer_deinit(amark_buffer_t *buffer);

amark_error_t amark_node_create(amark_node_type_t type, amark_node_t **node);   /* 結果は出力引数 */
void amark_node_destroy(amark_node_t *node);
```

### コードスタイル

- 波括弧は Allman 方式
- 演算の順序は括弧で明示する
  - `&&` / `||` のオペランドになる比較は括弧で囲む
  - 比較の左右に算術式（`+ - * / % << >>`）があれば、それも括弧で囲む
  - 比較でないオペランド（変数、関数呼び出し、`!x`）には括弧を付けない
- 処理のまとまり（ステップ）が変わるところに空行を 1 つ入れる。1 つのステップの文は空行を挟まずに続ける

```c
amark_error_t amark_buffer_append(amark_buffer_t *buffer, const char *data, size_t len)
{
    amark_error_t error;

    if (!buffer || ((len > 0) && !data))
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    if (len == 0)
    {
        return AMARK_OK;
    }

    error = amark_buffer_reserve(buffer, len);   /* あふれ・確保失敗ではバッファを変えない */
    if (error)
    {
        return error;
    }

    memcpy(buffer->data + buffer->len, data, len);
    buffer->len += len;
    buffer->data[buffer->len] = '\0';

    return AMARK_OK;
}
```

比較の左右に算術式がある場合の例: `if (additional > (SIZE_MAX - buffer->len - 1))`

### 機械的に検査する規約と検査しない規約

- 検査する: 整形（`clang-format` 18 の `--dry-run --Werror`、CI の format ジョブ）、ソースヘッダ表記（CTest の `amark.source_headers`）、公開ヘッダの単独 include と C++ からの利用（ヘッダ検査とテスト）
- 検査しない（レビューで確認する）: 演算順序の括弧付け、ステップ間の空行、命名規約の一部（`_init` / `_create` の使い分けなど）、モジュール間の include の向き
- 検査が不要になるよう構造で防ぐ: エラー文字列の定義漏れ（名前と文字列を `AMARK_ERROR_LIST` の 1 項目で組にするため、文字列のないコードは書けない）

```c
/* 全ソースの先頭に置く定型ヘッダ */
/*
 * Copyright (c) 2026 The Amark Authors
 * SPDX-License-Identifier: MIT
 */
```

### Testing

- Naos で書き、CTest から実行する
- **ライブラリ本体をテストするためだけのコードを、ライブラリ側のソースコードに持たない**。テスト用のフック、テスト専用の関数、テスト用の条件コンパイルを本体に入れない。確保失敗の注入などはテスト側でアロケータを差し替えて実現する
- テストは公開 API を通して行う
- モジュールごとに 1 実行ファイル（`naos_add_executable(amark_<module>_test ...)`）。CTest 名は `amark_<module>_test.<suite>.<test>`
- テスト補助は `tests/support/` の静的ライブラリ `amark_test_support`。計測アロケータ（`test_allocator_*`）で確保失敗の注入、確保・解放の釣り合い、バイト数（生存・累計・最大）を検証する
- CommonMark 方言は仕様の例を適合テストとして実行する

### CI/CD

GitHub Actions で 3 OS すべてについて、静的・動的の両方をビルドしテストする。

- 行列: `{linux-gcc, linux-clang, macos-clang, windows-msvc} × {static, shared}`。警告をエラーとし、テスト後にインストールして `tests/package/` の利用者プロジェクトを `find_package(Amark)` でビルド・実行する
- 整形検査は `ubuntu-24.04` の `clang-format-18` に版を固定して行う
- バージョンの唯一の出所は手書きの `include/amark/version.h`（`AMARK_VERSION_MAJOR` / `MINOR` / `PATCH`）。トップレベルの `CMakeLists.txt` が `project()` の前にそこから読む
- リリース: `include/amark/version.h` の 3 つの数値を上げてマージ → `vX.Y.Z` タグを push → タグと `version.h` のバージョンの一致確認と全ジョブの成功の後に `gh release create` で公開

## Development Environment

### Required Tools

- C99 コンパイラ（GCC / Clang / MSVC）。テストの C++ 検査用に C++11 コンパイラ
- CMake 3.21 以上
- clang-format 18（整形）

### Build Options

| Option | Default | Content |
|---|---|---|
| `BUILD_SHARED_LIBS` | `OFF` | `ON` で動的ライブラリ |
| `AMARK_BUILD_TESTS` | トップレベル時 `ON` | テストを構成する（Naos の取得もこのときだけ） |
| `AMARK_INSTALL` | トップレベル時 `ON` | インストール規則を生成する |
| `AMARK_WARNINGS_AS_ERRORS` | `OFF` | 警告をエラーにする（CI では `ON`） |

利用者は `find_package(Amark)` またはサブディレクトリ取り込みで `Amark::amark` にリンクする

### Common Commands

```bash
# Configure: cmake -S . -B build
# Build:     cmake --build build --config Release
# Test:      ctest --test-dir build -C Release --output-on-failure
# Shared:    cmake -S . -B build -DBUILD_SHARED_LIBS=ON
# Strict:    cmake -S . -B build -DAMARK_WARNINGS_AS_ERRORS=ON
# Install:   cmake --install build --config Release --prefix <dir>
# Format:    clang-format-18 --dry-run --Werror <files>
```

## Key Technical Decisions

- **全構文を拡張として登録する**: 「拡張の組み合わせで方言を表現する」ことを、コア固定＋フック方式より優先した。拡張 API がライブラリ本体によって常に使われるため、API の不足が早期に露見する
- **イベント列方式を採らない**: ロスレスを目的としないため、トークナイザ→イベント列→木の構成は過剰と判断した
- **グローバルアロケータ**: オブジェクトごとにアロケータを渡す方式ではなく、プロセス全体で 1 つを差し替える方式とする
- **標準ノード種別は木のモジュールが持つ**: レンダラがパーサに依存せず、手組みの木だけでテストできるようにするため

---
_Document standards and patterns, not every dependency_

_updated_at: 2026-11-27 — foundation の設計に合わせて命名例（`void amark_buffer_init`、出力引数の `amark_node_create`）とコード例を修正し、エラーコード一覧表の規則、所有権規約、ビルドオプション、検査する規約としない規約、CI とリリースを追記。foundation の設計レビューに合わせ、アロケータの形とサイズ 0 の規則、`AMARK_ERROR_LIST` によるエラーコードの規則、手書きの `version.h` を出所とするリリース手順に更新_
