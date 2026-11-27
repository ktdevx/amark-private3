# Research & Design Decisions

## Summary
- **Feature**: `foundation`
- **Discovery Scope**: New Feature（グリーンフィールド。既存のソースコード、ビルド定義、CI はない）
- **Key Findings**:
  - Naos v0.4.0 は `Naos::naos` ターゲットと `naos_add_executable()` を提供し、ビルド直後にテスト実行ファイルを `--naos-list` で起動してテストを列挙する。そのため動的ライブラリ構成では、ビルド時点で Amark の DLL / 共有ライブラリが実行ファイルから見つかる必要がある。FetchContent で取り込んだ Naos は常に静的ライブラリになる
  - Naos 自身の export ヘッダ（`NAOS_SHARED` / `NAOS_BUILDING` による `__declspec(dllexport/dllimport)` と `visibility("default")` の切り替え、`C_VISIBILITY_PRESET hidden`）は、Amark の静的・動的両対応の要件にそのまま適合する実績ある形である
  - エラー文字列化を `default` なしの `switch` で書けば、GCC / Clang の `-Wswitch`（`-Wall` に含まれる）と警告のエラー化により、列挙の追加時に文字列の定義漏れをビルド時に検出できる

## Research Log

### Naos v0.4.0 の CMake 連携
- **Context**: 要件 3.1〜3.3（タグ固定の取り込み、CTest 連携、動的ライブラリ構成でのテスト）
- **Sources Consulted**: Naos リポジトリ v0.4.0 タグの `README.md`、`docs/reference/cmake.md`、`docs/guides/shared-library.md`、`include/naos/export.h`、`CMakeLists.txt`
- **Findings**:
  - 取り込み: `FetchContent_Declare(Naos GIT_REPOSITORY https://github.com/vabelorg/naos.git GIT_TAG v0.4.0)` と `FetchContent_MakeAvailable(Naos)`
  - `naos_add_executable(<target> SOURCES ... [LIBRARIES ...] [PREFIX ...] [LABELS ...])` が実行ファイルを作り、`Naos::naos` と `LIBRARIES` を PRIVATE でリンクし、各テストを CTest の `<prefix><suite>.<test>` として登録する（既定の prefix は `<target>.`）
  - テストは `NAOS_TEST` / `NAOS_SUITE` / `NAOS_ADD_TEST` / `NAOS_MAIN` / `NAOS_ADD_SUITE` で書く。検査マクロは `NAOS_CHECK_INT_EQ`、`NAOS_CHECK_UINT_EQ`、`NAOS_CHECK_STR_EQ`、`NAOS_CHECK_MEM_EQ`、`NAOS_CHECK_NULL`、`NAOS_CHECK_NOT_NULL`、`NAOS_CHECK_TRUE`、`NAOS_CHECK_FALSE`、`NAOS_REQUIRE_*` など
  - アサーションを 1 つも実行しないテストは失敗になる（`ALLOW_NO_ASSERTIONS` で緩和可能）
  - サブプロジェクトとしての Naos は `BUILD_SHARED_LIBS` によらず静的ライブラリ。`NAOS_SELFTEST` と `NAOS_INSTALL` はトップレベルでないとき既定で OFF
  - 列挙はビルド後に実行されるため、DLL が見つからないとビルドが失敗する（Windows では `0xc0000135`）。対策は実行ファイルと同じディレクトリに置く、`PATH` に追加、rpath のいずれか
  - Naos は C++11 のテストにも対応する
- **Implications**: Amark はトップレベル時に `CMAKE_RUNTIME_OUTPUT_DIRECTORY` を `${PROJECT_BINARY_DIR}/bin` に揃え、DLL とテスト実行ファイルを同じディレクトリに置く。Linux / macOS はビルドツリーの rpath で解決される。Naos の取得はテストを有効にしたときだけ行う

### export マクロとシンボル可視性
- **Context**: 要件 1.4、1.5、2.2
- **Sources Consulted**: Naos `include/naos/export.h`、`src/CMakeLists.txt`（`C_VISIBILITY_PRESET hidden`、`VISIBILITY_INLINES_HIDDEN ON`）、CMake の `GenerateExportHeader` モジュールの仕様
- **Findings**:
  - 手書きの export ヘッダは、生成物に依存せずソースツリーだけで完結し、静的・動的で同一のヘッダを使える
  - `GenerateExportHeader` は生成ヘッダをビルドツリーに置くため、インストール時の扱いと、静的ビルドで `*_STATIC_DEFINE` を利用者に伝播させる設定が必要になる
- **Implications**: `include/amark/export.h` を手書きし、`AMARK_BUILDING`（ライブラリのビルド時のみ PRIVATE）と `AMARK_SHARED`（動的ライブラリのとき PUBLIC で利用者へ伝播）で切り替える

### UTF-8 の不正バイト列の扱い
- **Context**: 要件 10.2
- **Sources Consulted**: The Unicode Standard, Chapter 3「U+FFFD Substitution of Maximal Subparts」、RFC 3629、WHATWG Encoding Standard の UTF-8 decoder
- **Findings**:
  - 妥当な先頭バイトは `00–7F`、`C2–F4`。2 バイト目の許容範囲は先頭バイトに依存する（`E0` → `A0–BF`、`ED` → `80–9F`、`F0` → `90–BF`、`F4` → `80–8F`、他は `80–BF`）。この表で過長表現、サロゲート、U+10FFFF 超を同時に排除できる
  - 「最大部分列」方式では、妥当な並びの接頭辞として成立する最長のバイト列（1〜3 バイト）を 1 つの不正単位とし、それ以外の不正バイトは 1 バイトずつ不正単位とする。WHATWG の decoder も同じ結果になる
- **Implications**: `amark_utf8_decode` は不正時に `consumed` へ最大部分列の長さ（1 以上）を返し、呼び出し側が U+FFFD に置き換えて先へ進めるようにする

### CI ランナーとツール
- **Context**: 要件 4.1〜4.7、12.3
- **Sources Consulted**: GitHub Actions のホスト型ランナーの提供イメージ（`ubuntu-24.04`、`macos-latest`、`windows-latest`）、`gh release create` の仕様
- **Findings**:
  - `ubuntu-24.04` には GCC と Clang、および `clang-format-18` が用意されている。`windows-latest` は Visual Studio（MSVC）と CMake を持ち、既定のジェネレータはマルチコンフィグの Visual Studio
  - `$<TARGET_RUNTIME_DLLS:tgt>` は CMake 3.21 から使え、利用者プロジェクトの Windows 動的リンク確認で DLL のコピーに使える
  - `gh release create <tag> --verify-tag --generate-notes` は `GITHUB_TOKEN`（`contents: write`）だけでリリースを作れる
  - clang-format は版によって整形結果が変わりうる
- **Implications**: 整形検査は Linux の 1 ジョブで `clang-format-18` に固定して行う。マルチコンフィグ対応のため `--config` / `-C` を常に指定する

## Architecture Pattern Evaluation

| Option | Description | Strengths | Risks / Limitations | Notes |
|--------|-------------|-----------|---------------------|-------|
| 単一ターゲット + `BUILD_SHARED_LIBS` 切り替え | 1 回の構成で静的か動的のどちらかを作る | CMake の標準慣習。steering の Common Commands と一致。インストール後の利用者側の扱いが単純 | 両方を得るには 2 回ビルドする | 採用。CI は行列で両方を検証する |
| 静的・動的の 2 ターゲットを同時に作る | `amark_static` と `amark_shared` を常に両方作る | 1 回のビルドで両方得られる | Windows で `amark.lib`（静的）とインポートライブラリの名前衝突、ソースの二重コンパイル、パッケージ設定での選択ロジックが必要 | 不採用 |
| エラー: 単一の列挙 + `switch` の文字列化 | 列挙は明示的な数値付き、文字列は `src/error.c` の `switch` | 追加箇所が 2 か所で明確。`-Wswitch` で漏れを検出 | MSVC では既定で漏れ検出の警告が出ない | 採用。GCC / Clang の CI ジョブで検出する |
| エラー: X マクロで列挙と文字列を同時生成 | 公開ヘッダに一覧マクロを置く | 1 か所の追加で済む | 公開ヘッダが読みにくく、IDE の補完や文書化と相性が悪い | 不採用 |
| アロケータ: オブジェクトごとに渡す | 各 API にアロケータを渡す | 細粒度の制御 | steering で明示的に却下済み | 不採用 |

## Design Decisions

### Decision: ライブラリ形態は `BUILD_SHARED_LIBS` で切り替える単一ターゲット
- **Context**: 要件 1.1、1.2、2.2。brief の「静的/動的の両ターゲット」は両形態を提供する意味と解釈する
- **Alternatives Considered**:
  1. 単一ターゲット `amark` を `BUILD_SHARED_LIBS` で切り替える
  2. 静的・動的の 2 ターゲットを常に同時にビルドする
- **Selected Approach**: 1。既定は静的。`Amark::amark` を別名ターゲットとエクスポート名の両方で提供する
- **Rationale**: steering（tech.md の Common Commands）と一致し、Windows の名前衝突を避けられる
- **Trade-offs**: 両形態の同時配布は利用者が 2 回ビルドする必要がある
- **Follow-up**: CI 行列で静的・動的の両方をインストール確認まで検証する

### Decision: アロケータ関数には 0 サイズと空ポインタを渡さない
- **Context**: 要件 6.7、6.8。C の `malloc(0)` と `realloc(p, 0)` は処理系定義（C23 では `realloc(p, 0)` は未定義動作）
- **Alternatives Considered**:
  1. 0 サイズ要求をそのまま利用者関数へ渡す
  2. 0 サイズ要求を 1 バイトの要求として扱い、`amark_free(NULL)` と `amark_realloc(NULL, n)` はラッパ側で処理する
- **Selected Approach**: 2。`amark_malloc` / `amark_calloc` / `amark_realloc` の戻り値が `NULL` なら常に確保失敗を意味する
- **Rationale**: 全プラットフォームで同じ結果になり、利用者のアロケータ実装も単純になる。失敗の判定が曖昧にならない
- **Trade-offs**: 0 サイズ要求で 1 バイト消費する
- **Follow-up**: 利用者関数への事前条件（size ≥ 1、pointer ≠ NULL）を公開ヘッダに明記する

### Decision: エラーコードは明示値付きの単一列挙で、末尾追加のみ
- **Context**: 要件 7.1〜7.6。後続 spec が追加していく
- **Selected Approach**: `amark_error_t`。`AMARK_OK = 0`、以降 `AMARK_ERROR_*` に連番の明示値を振る。番兵（個数を表す定数）は公開しない（追加のたびに値が変わり ABI の約束になってしまうため）。文字列は `default` なしの `switch` で返し、`switch` の後で未知の値の文字列を返す
- **Rationale**: 末尾追加は ABI 互換。`-Wswitch` による漏れ検出が効く
- **Trade-offs**: MSVC 単独では漏れを検出しない。テストは既知の各コードの文字列を個別に確認する
- **Follow-up**: 後続 spec はコードを追加する際、`error.h` の列挙・`error.c` の文字列・`error_test.c` の確認を同時に更新する

### Decision: 文字列の所有権規約
- **Context**: 要件 8.7。node-tree と共有する seam
- **Selected Approach**: 借用は `amark_slice_t`（NUL 終端を仮定しない、NUL を含みうる）。所有は `amark_malloc` 系で確保した `char *` と長さの組で、常に `data[length] == '\0'`。解放は `amark_free`
- **Rationale**: 長さ付きで NUL を含むデータを扱え、C の文字列 API との相互運用も保てる
- **Follow-up**: node-tree の設計でこの規約に従っているかを確認する

### Decision: バッファの取り出しはエラーコードで結果を返す
- **Context**: 要件 9.7。空のバッファ（領域なし）からの取り出しでも所有権のある文字列を返す必要がある
- **Selected Approach**: `amark_buffer_detach(buffer, &data, &length)`。領域がなければ 1 バイトの空文字列を確保する。確保に失敗したら `AMARK_ERROR_NO_MEMORY` を返しバッファは変更しない
- **Rationale**: 呼び出し側は常に `amark_free` で解放でき、空と失敗を区別できる

### Decision: `amark_buffer_init` は値を返さない
- **Context**: steering（tech.md）の命名例では `amark_error_t amark_buffer_init(...)` としているが、初期化は確保を伴わず失敗しない
- **Selected Approach**: `void amark_buffer_init(amark_buffer_t *buffer)`。「失敗しうる関数はエラーコードを返す」という規約（要件 7.7）を優先する
- **Follow-up**: steering 更新タスク（要件 12.5）で tech.md の例をこの形に合わせる

### Decision: ソースヘッダ表記の検査は CMake スクリプトで行い CTest に登録する
- **Context**: 要件 5.3。3 OS で同じ検査を実行したい
- **Selected Approach**: `cmake/AmarkCheckSourceHeaders.cmake` を `cmake -P` で実行する CTest テストとして登録する。シェルスクリプトは Windows で動かないため採らない
- **Rationale**: 追加の依存がなく、全 OS の CI で自動的に実行される

### Decision: 整形検査は CI の 1 ジョブで clang-format の版を固定して行う
- **Context**: 要件 12.3。版差で結果が揺れる
- **Selected Approach**: `ubuntu-24.04` で `clang-format-18 --dry-run --Werror` を対象ファイルに適用する
- **Trade-offs**: 開発者の手元の版が異なると差分が出うる。版は tech.md に明記する

### Decision: Generalization / Build vs Adopt / Simplification の結果
- **Generalization**: バッファへの追記はすべて「バイト列の追記」に集約し、1 バイト、NUL 終端文字列、スライス、コードポイントの追記はその薄い入口とする。容量確保は `reserve` に集約する
- **Build vs Adopt**: テスト基盤は Naos を採用（steering で決定済み）。export ヘッダは `GenerateExportHeader` を採らず手書き（上記）。UTF-8 処理は外部ライブラリを採らず自作（C 標準ライブラリ以外に依存しない方針、処理量が小さい）。パッケージ設定は CMake 標準の `CMakePackageConfigHelpers` を採用
- **Simplification**: pkg-config ファイル、2 ターゲット同時ビルド、エラーの X マクロ、スライスのインライン関数化、Unicode 文字分類は採らない。非公開ヘッダは現時点で不要

## Risks & Mitigations
- 動的ライブラリ構成で DLL が見つからずテストの列挙が失敗する — トップレベル時に実行ファイルと DLL の出力先を揃える。CI の Windows 動的ジョブで常に検証する
- MSVC では `switch` の列挙漏れを検出できない — GCC / Clang ジョブで検出し、テストで各コードの文字列を確認する
- clang-format の版差 — CI で版を固定し、tech.md に明記する
- アロケータ差し替えの誤用（生存中のメモリがある状態で差し替え、マルチスレッドでの差し替え）— 公開ヘッダに制約を明記する。ライブラリ側では検出しない（検出のための状態を持つとテスト専用コードに近づき、コストも増えるため）
- 後続 spec がエラーコードの値を変更・再利用する — 末尾追加のみの規約を `error.h` のコメントと steering に記し、Revalidation Trigger とする

## References
- [Naos v0.4.0](https://github.com/vabelorg/naos/tree/v0.4.0) — テストフレームワーク、CMake 関数と export ヘッダの実例
- [The Unicode Standard, Chapter 3](https://www.unicode.org/versions/latest/ch03.pdf) — Table 3-7（妥当な UTF-8 バイト列）と最大部分列による置換
- [RFC 3629](https://www.rfc-editor.org/rfc/rfc3629) — UTF-8 の定義
- [CMake CMakePackageConfigHelpers](https://cmake.org/cmake/help/latest/module/CMakePackageConfigHelpers.html) — パッケージ設定とバージョン互換の生成
- [CMake TARGET_RUNTIME_DLLS](https://cmake.org/cmake/help/latest/manual/cmake-generator-expressions.7.html) — Windows での DLL コピー
