# Project Structure

> 以下は各 spec（`foundation` ほか）の設計で確定した構成を反映している。実装時に実際のファイルと食い違えば、spec の設計に合わせて直すか、ここを同期する。

## Organization Philosophy

責務ごとのモジュールを層として積む。下の層は上の層を知らない。

```
基盤（エクスポート、バージョン、エラーコード、アロケータ、スライス、UTF-8、バッファ）
  └─ 木（ノード、編集、走査）
       ├─ パーサエンジン（拡張登録 API、方言）
       │    └─ 構文拡張（CommonMark など）
       └─ レンダラ共通層（render_core、非公開）
            ├─ HTML レンダラ
            └─ Markdown レンダラ
```

- 基盤の内部の依存方向は `export → version, error → memory → slice → utf8 → buffer → amark.h`。各モジュールは自分より下位のモジュールだけを include する。逆向きの include はレビューでエラーとして扱う
- レンダラはパーサに依存しない。パーサはレンダラに依存しない
- 構文拡張はパーサエンジンの公開拡張 API だけに依存し、エンジンの非公開部分に触れない
- 1 つの拡張が構文・ノード種別・レンダラフックをまとめて提供する場合、それらを結び付けるのは拡張の側である
- **レンダラ共通層** `src/render_core.h`（非公開）は HTML と Markdown の両レンダラが共有する内部層で、html-renderer spec が所有する。変更したときは markdown-renderer への影響を再検証する

## Directory Patterns

### 公開ヘッダ
**Location**: `include/amark/`
**Purpose**: 利用者と拡張の作者に公開する API。ここにあるものだけが互換性の対象になる
**Pattern**: モジュール単位で `include/amark/<module>.h`（例: `error.h`、`memory.h`、`slice.h`、`utf8.h`、`buffer.h`、`node.h`）。`export.h` が `AMARK_API` と `AMARK_BEGIN_DECLS` / `AMARK_END_DECLS` を、`version.h` は `cmake/version.h.in` からビルドツリーに生成される。包括ヘッダ `amark.h` がすべての公開ヘッダを include する
**Example**: `#include <amark/amark.h>`

### ライブラリ本体
**Location**: `src/`
**Purpose**: 実装と非公開ヘッダ。テストのためだけのコードは置かない
**Pattern**: モジュール単位で `<module>.c` と、必要なら非公開の `<module>.h` を対にする（例: `src/render_core.{h,c}`、`src/unicode_class.{h,c}`）。生成したデータは `<module>_data.c` としてリポジトリに含める（例: `src/unicode_class_data.c`）

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
**Purpose**: package config のテンプレート、警告設定、バージョンヘッダのテンプレート、ソースヘッダ検査のスクリプトなど CMake の補助ファイル

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

## Include Organization

```c
#include <amark/node.h>   /* 公開ヘッダ: 山括弧、amark/ 付き */

#include "buffer.h"       /* 非公開ヘッダ: 二重引用符 */
```

- 公開ヘッダは非公開ヘッダを include しない
- 公開ヘッダは単体で include でき、C++ からも使える。include ガードは `AMARK_<NAME>_H`、宣言は `AMARK_BEGIN_DECLS` / `AMARK_END_DECLS` で囲む
- 公開関数には `AMARK_API` を付け、静的・動的の両方のビルドで同じヘッダを使う

## Code Organization Principles

- **横断的な約束事は一か所に置く**: エラーコード（一覧表は foundation の設計が唯一の正本）、アロケータ、文字列の所有権規約は基盤モジュールが持ち、各モジュールが独自に定義しない
- **公開 API を最小にする**: 公開ヘッダに出すのは利用者か拡張の作者が必要とするものに限る。複数のモジュールが内部で共有するだけのもの（Unicode の分類、レンダラ共通層）は非公開ヘッダに置く
- **本体と拡張で同じ API を使う**: ライブラリ同梱の構文拡張だけが使える特別な入口を作らない
- **spec の境界はコードの境界ではない**: モジュールの切り方が spec の切り方と食い違ったら、あるべきモジュール構成を優先する

---
_Document patterns, not file trees. New files following patterns shouldn't require updates_

_updated_at: 2026-11-27 — 各 spec の設計に合わせて公開ヘッダの配置、テスト補助の命名、テストデータ・生成ツールの置き場所、レンダラ共通層、共有の骨組みファイルの追記規則を反映_
