# Project Structure

> ソースコードはまだ存在しない。以下は初期方針であり、`foundation` spec の設計で確定した時点で実態に合わせて更新する。

## Organization Philosophy

責務ごとのモジュールを層として積む。下の層は上の層を知らない。

```
基盤（アロケータ、エラーコード、バッファ・文字列）
  └─ 木（ノード、編集、走査）
       ├─ パーサエンジン（拡張登録 API、方言）
       │    └─ 構文拡張（CommonMark など）
       └─ レンダラ（HTML、Markdown）
```

- レンダラはパーサに依存しない。パーサはレンダラに依存しない
- 構文拡張はパーサエンジンの公開拡張 API だけに依存し、エンジンの非公開部分に触れない
- 1 つの拡張が構文・ノード種別・レンダラフックをまとめて提供する場合、それらを結び付けるのは拡張の側である

## Directory Patterns

### 公開ヘッダ
**Location**: `include/amark/`
**Purpose**: 利用者と拡張の作者に公開する API。ここにあるものだけが互換性の対象になる
**Example**: `#include <amark/amark.h>`

### ライブラリ本体
**Location**: `src/`
**Purpose**: 実装と非公開ヘッダ。テストのためだけのコードは置かない
**Example**: モジュール単位で `.c` と非公開の `.h` を対にする

### テスト
**Location**: `tests/`
**Purpose**: Naos によるテスト、テストデータ、テスト用の補助コード（仕様の例の抽出、アロケータの差し替えなど）
**Example**: モジュールごとにテスト実行ファイルを分け、`naos_add_executable` で登録する

### ビルド補助
**Location**: `cmake/`
**Purpose**: package config のテンプレートなど CMake の補助ファイル

## Naming Conventions

- **Files**: 小文字のスネークケース。モジュール名をファイル名にする
- **Functions**: `amark_<モジュール>_<動作>`（例: `amark_node_append_child`）
- **Types**: `amark_<名前>_t`
- **Macros / 列挙定数**: `AMARK_<名前>`
- **生成・破棄**: opaque 型は `_create` / `_destroy`、中身を呼び出し側が持つ構造体は `_init` / `_deinit`
- **非公開シンボル**: ファイル内に閉じるものは `static` とする。翻訳単位をまたぐ非公開シンボルは動的ライブラリから export しない

## Include Organization

```c
#include <amark/node.h>   /* 公開ヘッダ: 山括弧、amark/ 付き */

#include "buffer.h"       /* 非公開ヘッダ: 二重引用符 */
```

- 公開ヘッダは非公開ヘッダを include しない
- 公開ヘッダは単体で include でき、C++ からも使える（`extern "C"`）
- 公開関数には export マクロを付け、静的・動的の両方のビルドで同じヘッダを使う

## Code Organization Principles

- **横断的な約束事は一か所に置く**: エラーコード、アロケータ、文字列の所有権規約は基盤モジュールが持ち、各モジュールが独自に定義しない
- **公開 API を最小にする**: 公開ヘッダに出すのは利用者か拡張の作者が必要とするものに限る
- **本体と拡張で同じ API を使う**: ライブラリ同梱の構文拡張だけが使える特別な入口を作らない
- **spec の境界はコードの境界ではない**: モジュールの切り方が spec の切り方と食い違ったら、あるべきモジュール構成を優先する

---
_Document patterns, not file trees. New files following patterns shouldn't require updates_
