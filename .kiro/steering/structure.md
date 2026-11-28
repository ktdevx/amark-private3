# Project Structure

> 以下は各 spec（`foundation` ほか）の設計で確定した構成を反映している。実装時に実際のファイルと食い違えば、spec の設計に合わせて直すか、ここを同期する。

## Organization Philosophy

責務ごとのモジュールを層として積む。下の層は上の層を知らない。

```
基盤（エクスポート、バージョン、エラーコード、アロケータ、スライス、UTF-8、バッファ）
  └─ 木（ノード、編集、走査、振り分け走査 node_walk〔非公開〕）
       ├─ パーサエンジン（拡張登録 API、方言）
       │    └─ 規則（方言に属さない構文拡張と、その共有部品）
       │         └─ 方言プリセット（CommonMark、将来の GFM など。規則を選んで集めるだけ）
       ├─ HTML レンダラ
       └─ Markdown レンダラ
```

- 基盤の内部の依存方向は `export → version, error → memory → slice → utf8 → buffer → amark.h`。各モジュールは自分より下位のモジュールだけを include する。逆向きの include はレビューでエラーとして扱う
- レンダラはパーサに依存しない。パーサはレンダラに依存しない
- 構文拡張はパーサエンジンの公開拡張 API だけに依存し、エンジンの非公開部分に触れない
- **規則と方言プリセットを分ける**: 構文は特定の方言に属さない「規則」として規則の層に置く（例: 強調、フェンスコード、将来のテーブル）。方言プリセットは構文のロジックを持たず、規則の公開 API だけで規則を集める。プリセットは規則の層の非公開ヘッダを include せず、プリセット同士も互いに依存しない。2 つの方言で共有したい部品が出たら、それは規則の層の共有部品に置く
- 1 つの拡張が構文・ノード種別・レンダラフックをまとめて提供する場合、それらを結び付けるのは拡張の側である
- **振り分け走査** `src/tree/node_walk.h`（非公開）は、種別ハンドルをキーとするフック表と反復の走査駆動部で、node-tree spec が所有する。利用者は HTML と Markdown の両レンダラ。変更したときは html-renderer と markdown-renderer を再検証する。公開や共有の文字列ライターの切り出しは、実際の第 2 の利用者が現れるまで行わない（Markdown の行の書き出しは markdown-renderer に置く）

## Directory Patterns

### 公開ヘッダ
**Location**: `include/amark/`
**Purpose**: 利用者と拡張の作者に公開する API。ここにあるものだけが互換性の対象になる
**Pattern**: モジュール単位で `include/amark/<module>.h`（例: `error.h`、`memory.h`、`slice.h`、`utf8.h`、`buffer.h`、`node.h`）。`export.h` が `AMARK_API` と `AMARK_BEGIN_DECLS` / `AMARK_END_DECLS` を、`version.h` がバージョンの唯一の出所（手書きの数値定数。ビルド定義とリリースがここから読む）を持つ。公開ヘッダはすべてソースツリーに手書きで置き、ビルドツリーに生成しない。包括ヘッダ `amark.h` がすべての公開ヘッダを include する
**Example**: `#include <amark/amark.h>`

### ライブラリ本体
**Location**: `src/`
**Purpose**: 実装と非公開ヘッダ。テストのためだけのコードは置かない
**Pattern**: 層・モジュール群ごとのサブディレクトリに分け、`src/` 直下にはビルド定義だけを置く

| ディレクトリ | 中身 |
|---|---|
| `src/base/` | 基盤（エラー、アロケータ、スライス、UTF-8、バッファ、バージョン）と、複数のモジュールが内部で共有する文字分類（`unicode_class`） |
| `src/tree/` | ノード、種別、編集、走査、レンダラが共有する非公開の振り分け走査（`node_walk`） |
| `src/parser/` | 構文非依存のパーサエンジン、拡張、方言 |
| `src/html/`, `src/markdown/` | 各レンダラ |
| `src/syntax/` | 規則（方言に属さない構文拡張）と、規則が共有する非公開の部品・表（`syntax_internal.h`） |
| `src/cmark/` | CommonMark 方言プリセットと簡易 API（規則の公開 API だけを使う） |

- 新しい構文（例: 将来の GFM のテーブル）は規則として `src/syntax/` に足し、新しい方言（例: 将来の GFM）はプリセットとして `src/<name>/` を足す。ディレクトリはさらに入れ子にしない（`src/syntax/` の中もフラットに置き、共有部品と規則はファイル名で区別する）
- 各ディレクトリの中は `<module>.c` と、必要なら非公開の `<module>.h` を対にする。生成したデータは `<module>_data.c` としてリポジトリに含める（例: `src/base/unicode_class_data.c`）
- ファイルのベース名は `src/` 全体で一意にする（静的ライブラリのメンバ名の衝突を避け、ファイル名だけで所属が分かるようにするため）
- ディレクトリは置き場所であって所有の単位ではない。たとえば `src/base/unicode_class.*` は commonmark spec が所有するが、基盤層の部品なので `base/` に置く

### テスト
**Location**: `tests/`
**Purpose**: Naos によるテスト
**Pattern**: モジュールごとに `tests/<module>_test.c` を 1 実行ファイルとし、`naos_add_executable(amark_<module>_test ...)` で登録する。`tests/cxx_header_test.cpp` が C++ からの利用を、`tests/package/` がインストール後の `find_package` を確認する（後者は本体のビルドに含めない）

### テスト補助
**Location**: `tests/support/`
**Purpose**: テスト用の補助コード（差し替えアロケータ、木の組み立てとダンプ、仕様の例の読み込みなど）。静的ライブラリ `amark_test_support` にまとめ、ライブラリ本体には含めない
**Pattern**: ファイルは `test_<area>.{h,c}`、外部に見える関数・型は `test_<area>_*`（例: `test_allocator`、`test_tree`、`test_tree_dump`、`test_spec_examples`）。`amark_` の名前空間は使わない

### テストデータ
**Location**: `tests/data/`
**Purpose**: テストが読むデータ（例: `tests/data/commonmark/` の仕様の例）

### 生成ツール
**Location**: `tools/`
**Purpose**: 表などを生成するスクリプト（例: Unicode の分類表、HTML の実体参照表）。手で実行し、生成物を `src/` にコミットする。ビルドや CI からは実行しない

### ビルド補助
**Location**: `cmake/`
**Purpose**: package config のテンプレート、警告設定、ソースヘッダ検査のスクリプトなど CMake の補助ファイル

## Shared Skeleton Files（追記専用）

次のファイルは `foundation` spec が所有し、後続 spec は spec 名のコメント付きブロックを末尾に追記するだけとする。他の spec のブロックを書き換えない。並行する spec の変更が衝突したら、両方のブロックを残して解消する。

- `include/amark/amark.h`（包括ヘッダの include 一覧）
- `src/CMakeLists.txt`（ライブラリのソース一覧）
- `tests/CMakeLists.txt`（テストの登録）と `amark_test_support` のソース一覧
- `tests/cxx_header_test.cpp`（C++ からの呼び出し確認）

## Naming Conventions

- **Files**: 小文字のスネークケース。モジュール名をファイル名にする
- **Functions**: `amark_<モジュール>_<動作>`（例: `amark_node_append_child`）
- **Types**: `amark_<名前>_t`
- **Macros / 列挙定数**: `AMARK_<名前>`
- **生成・破棄**: opaque 型は `_create` / `_destroy`、中身を呼び出し側が持つ構造体は `_init` / `_deinit`
- **非公開シンボル**: ファイル内に閉じるものは `static` とする。翻訳単位をまたぐ非公開シンボルは動的ライブラリから export しない
- **テスト補助**: `test_<area>_*`（上記「テスト補助」）
- **規則の名前**: 識別子・ファイル・ヘッダは `syntax` を使う（`amark_syntax_<rule>_create`、`AMARK_SYNTAX_<RULE>_NAME`、`src/syntax/syntax_*`、`amark/syntax.h`）。規則のソースは `syntax_<規則名>.c`。拡張名・構文名は方言の接頭辞を付けない規則の名前（例: `emphasis`、`html_block`）。方言の出身ではなく規則として参照できるようにするため
- **CommonMark プリセットの名前**: 識別子・ファイル・ヘッダは `cmark` を使う（`amark_cmark_*`、`AMARK_CMARK_*`、`src/cmark/`、`amark/cmark.h`）。仕様そのものは「CommonMark」、kiro の spec 名とテストデータの置き場（`tests/data/commonmark/`）は `commonmark` のまま。文書中で接頭辞やパスを伴わない単独の「cmark」は参照実装 commonmark/cmark を指す

## Include Organization

```c
#include <amark/node.h> /* 公開ヘッダ: 山括弧、amark/ 付き */

#include "tree/node_internal.h" /* 非公開ヘッダ: 二重引用符、src/ からの相対パス */
```

- 非公開ヘッダは `src/` を起点にディレクトリ付きで include する。同じディレクトリ内のヘッダも同じ書き方にする
- 公開ヘッダは非公開ヘッダを include しない
- テストとテスト補助は公開ヘッダとテスト補助のヘッダ（`tests/` を起点とする `"support/test_<area>.h"`）だけを include し、`src/` のヘッダを include しない
- 公開ヘッダは単体で include でき、C++ からも使える。include ガードは `AMARK_<NAME>_H`、宣言は `AMARK_BEGIN_DECLS` / `AMARK_END_DECLS` で囲む
- 公開関数には `AMARK_API` を付け、静的・動的の両方のビルドで同じヘッダを使う

## Code Organization Principles

- **横断的な約束事は一か所に置く**: エラーコード（一覧表は foundation の設計が唯一の正本）、アロケータ、文字列の所有権規約は基盤モジュールが持ち、各モジュールが独自に定義しない
- **公開 API を最小にする**: 公開ヘッダに出すのは利用者か拡張の作者が必要とするものに限る。複数のモジュールが内部で共有するだけのもの（Unicode の分類、振り分け走査 `node_walk`）は非公開ヘッダに置く
- **本体と拡張で同じ API を使う**: ライブラリ同梱の構文拡張だけが使える特別な入口を作らない
- **spec の境界はコードの境界ではない**: モジュールの切り方が spec の切り方と食い違ったら、あるべきモジュール構成を優先する

---
_Document patterns, not file trees. New files following patterns shouldn't require updates_

_updated_at: 2026-11-28 — 規則（`src/syntax/`、`amark/syntax.h`、方言の接頭辞なしの拡張名）と方言プリセット（`src/cmark/`）を分ける構成に変更。2026-11-27 — foundation の実装に同期: テストからの include の書き方を追記、例の末尾コメントを clang-format の整形どおり 1 スペースに。CommonMark モジュールの名前を `cmark` に。`src/` をサブディレクトリに分割。各 spec の設計に合わせて公開ヘッダの配置、テスト補助の命名、テストデータ・生成ツールの置き場所、共有の骨組みファイルの追記規則を反映。レンダラ共通層（旧 `src/render/`）を廃し、フック表と走査駆動部を node-tree の非公開モジュール `src/tree/node_walk` へ移した。`version.h` を生成から手書き（バージョンの唯一の出所）に変更_
