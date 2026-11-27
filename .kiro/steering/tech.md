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

ライブラリ内の動的確保はすべて、差し替え可能なグローバルアロケータを経由する: `amark_malloc`, `amark_realloc`, `amark_calloc`, `amark_free`。`malloc` などを直接呼ばない。

### エラー処理

エラーコードはライブラリ全体で単一の列挙とする。モジュールや spec ごとの独自体系を作らず、必要なコードは共通の列挙に追加する。

### 命名

- 公開シンボルは `amark_`、マクロと列挙定数は `AMARK_` で始める
- typedef した型名には `_t` を付ける
- 構造体の中身の初期化・後片付けを行う関数は `_init` / `_deinit`（領域は呼び出し側が持つ）
- opaque な構造体の生成・破棄を行う関数は `_create` / `_destroy`（領域はライブラリが確保する）

```c
typedef struct amark_buffer amark_buffer_t;

amark_error_t amark_buffer_init(amark_buffer_t *buffer);
void amark_buffer_deinit(amark_buffer_t *buffer);

amark_node_t *amark_node_create(amark_node_type_t type);
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
amark_error_t amark_buffer_append(amark_buffer_t *buffer, const char *data, size_t length)
{
    size_t required = buffer->length + length;

    if (!buffer->data || ((buffer->length + length) > buffer->capacity))
    {
        char *grown = amark_realloc(buffer->data, required);
        if (!grown)
        {
            return AMARK_ERROR_NO_MEMORY;
        }
        buffer->data = grown;
        buffer->capacity = required;
    }

    memcpy(buffer->data + buffer->length, data, length);
    buffer->length = required;

    return AMARK_OK;
}
```

### Testing

- Naos で書き、CTest から実行する
- **ライブラリ本体をテストするためだけのコードを、ライブラリ側のソースコードに持たない**。テスト用のフック、テスト専用の関数、テスト用の条件コンパイルを本体に入れない。確保失敗の注入などはテスト側でアロケータを差し替えて実現する
- テストは公開 API を通して行う
- CommonMark 方言は仕様の例を適合テストとして実行する

### CI/CD

GitHub Actions で 3 OS すべてについて、静的・動的の両方をビルドしテストする。

## Development Environment

### Required Tools

- C99 コンパイラ（GCC / Clang / MSVC）
- CMake 3.21 以上

### Common Commands

```bash
# Configure: cmake -S . -B build
# Build:     cmake --build build
# Test:      ctest --test-dir build
# Shared:    cmake -S . -B build -DBUILD_SHARED_LIBS=ON
```

## Key Technical Decisions

- **全構文を拡張として登録する**: 「拡張の組み合わせで方言を表現する」ことを、コア固定＋フック方式より優先した。拡張 API がライブラリ本体によって常に使われるため、API の不足が早期に露見する
- **イベント列方式を採らない**: ロスレスを目的としないため、トークナイザ→イベント列→木の構成は過剰と判断した
- **グローバルアロケータ**: オブジェクトごとにアロケータを渡す方式ではなく、プロセス全体で 1 つを差し替える方式とする
- **標準ノード種別は木のモジュールが持つ**: レンダラがパーサに依存せず、手組みの木だけでテストできるようにするため

---
_Document standards and patterns, not every dependency_
