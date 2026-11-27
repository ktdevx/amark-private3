# Research & Design Decisions

## Summary
- **Feature**: `foundation`
- **Discovery Scope**: New Feature（グリーンフィールド。既存のソースコード、ビルド定義、CI はない）
- **Key Findings**:
  - Naos v0.4.0 は `Naos::naos` ターゲットと `naos_add_executable()` を提供し、ビルド直後にテスト実行ファイルを `--naos-list` で起動してテストを列挙する。そのため動的ライブラリ構成では、ビルド時点で Amark の DLL / 共有ライブラリが実行ファイルから見つかる必要がある。FetchContent で取り込んだ Naos は常に静的ライブラリになる
  - Naos 自身の export ヘッダ（`NAOS_SHARED` / `NAOS_BUILDING` による `__declspec(dllexport/dllimport)` と `visibility("default")` の切り替え、`C_VISIBILITY_PRESET hidden`）は、Amark の静的・動的両対応の要件にそのまま適合する実績ある形である
  - エラーコードの名前と文字列を X マクロの 1 つの一覧に組で書き、列挙と文字列化の両方をそこから生成すれば、文字列のないコードはそもそも書けない。コンパイラの警告に頼る検出（MSVC では効かない）が不要になる
  - 実績のある C ライブラリの多く（zlib、curl、libuv、libgit2、expat）はバージョンをソースツリーのヘッダに手書きし、ビルドがそこから読む。ヘッダを CMake で生成する例（cmark）もあるが、生成物がないとヘッダが使えない

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

### バージョン番号の置き場所
- **Context**: 要件 2.5、2.6、4.6、4.7（バージョンの唯一の出所、ヘッダ・実行時・パッケージ設定・タグの一致）
- **Sources Consulted**: zlib の `zlib.h`（`ZLIB_VERSION`、`ZLIB_VER_MAJOR` ほか。CMake は `file(READ)` と正規表現で読む）、curl の `include/curl/curlver.h`（`LIBCURL_VERSION_MAJOR` ほか。CMake とリリーススクリプトが読む）、libuv の `include/uv/version.h`（`UV_VERSION_MAJOR` ほか。CMake が `file(STRINGS)` で読む）、libgit2 の `include/git2/version.h`（`LIBGIT2_VER_MAJOR` ほか。CMake が読む）、expat の `lib/expat.h`（`XML_MAJOR_VERSION` ほか）、cmark（`project(VERSION)` から `cmark_version.h` を CMake の構成時にビルドツリーへ生成）
- **Findings**:
  - 主流の確立した C ライブラリは、バージョンをソースツリーの公開ヘッダに数値マクロとして手書きし、ビルドシステム（CMake、Autotools、リリーススクリプト）がそのヘッダを読む
  - 生成方式（cmark）は CMake 側に数値を書けるが、構成前のソースツリーでは公開ヘッダが揃わず、IDE の索引、CMake 以外のビルドシステム、ソースを直接取り込む利用者がビルドツリーの include ディレクトリを必要とする。インストール規則やヘッダ検査も生成ヘッダを特別扱いする必要がある
  - CMake の `file(STRINGS ... REGEX)` と `string(REGEX REPLACE ...)` は `project()` の前に実行でき、`project(VERSION)` に読み取った値を渡せる。文字列のマクロはプリプロセッサの文字列化（2 段のマクロでマクロを展開してから `#`）で数値から組み立てられる
- **Implications**: `include/amark/version.h` を手書きし、数値を 1 回だけ書く。CMake とリリースワークフローはそこから読む

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
| エラー: 明示値付きの列挙 + `switch` の文字列化 | 列挙は明示的な数値付き、文字列は `src/base/error.c` の `switch` | 列挙が素直に読める | 名前・値・文字列を 2 か所に書き、文字列の漏れを GCC / Clang の `switch` の列挙漏れ警告に頼る（MSVC では検出しない）。spec ごとの区切りコメントがコードの所属を限定するように見える | 不採用（レビューで変更） |
| エラー: X マクロで列挙と文字列を同時生成 | 公開ヘッダの一覧マクロ `AMARK_ERROR_LIST` に `X(名前, 文字列)` を並べる | 追加は 1 か所。文字列のないコードを書けない。値は並び順で決まり、明示値の管理が要らない | 公開ヘッダに展開用のマクロが入る | 採用。補助マクロは使用後に `#undef` する |
| バージョン: 手書きのヘッダから CMake が読む | `include/amark/version.h` に数値を書き、`project()` の前に読む | ソースツリーだけで公開ヘッダが揃う。zlib・curl・libuv・libgit2・expat と同じ | ヘッダの書式を CMake とリリースが前提にする | 採用 |
| バージョン: `project(VERSION)` から生成 | 構成時に `version.h` をビルドツリーへ生成 | CMake 側に数値を書ける（cmark） | 生成前はヘッダが揃わない。ビルドツリーの include ディレクトリと生成ヘッダの特別扱いが要る | 不採用（レビューで変更） |
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
  3. 0 サイズ要求はアロケータを呼ばずに `NULL` を返し、`amark_realloc(p, 0)` は `p` を解放して `NULL` を返す。`amark_free(NULL)` と `amark_realloc(NULL, n)` はラッパ側で処理する
- **Selected Approach**: 3（レビューで 2 から変更）。利用者関数には常に 1 以上のサイズと非 `NULL` のポインタが届く。戻り値の `NULL` は、要求サイズが 0 でないときに限り確保失敗を意味する
- **Rationale**: 全プラットフォームで同じ結果になり、利用者のアロケータ実装も単純になる。0 サイズの要求で 1 バイトを消費せず、`realloc(p, 0)` の処理系差も吸収できる
- **Trade-offs**: サイズ 0 の要求の `NULL` と確保失敗の `NULL` が区別できない。そのためライブラリ内では、`NULL` を `AMARK_ERROR_NO_MEMORY` と解釈する箇所でサイズ 0 を要求しないという規則を置く（確保前に個数を確かめ、空の配列は `NULL` と個数 0 で表す）。foundation 自身の確保（スライスの複製は長さ + 1、バッファの予約は容量 + 1）は 0 にならない
- **Follow-up**: 利用者関数への事前条件（size ≥ 1、ptr ≠ NULL）とサイズ 0 の扱いを公開ヘッダに明記する。後続 spec の設計で、個数が 0 になりうる確保に「個数が 1 以上のときだけ確保する」ことを明記する

### Decision: グローバルアロケータは利用者データを持たない
- **Context**: 要件 6.3、6.6。アロケータはプロセス全体で 1 つ（steering の Key Technical Decisions）
- **Selected Approach**: `amark_allocator_t` は `malloc_fn` / `realloc_fn` / `calloc_fn` / `free_fn` の 4 関数だけを持ち、関数の型は C 標準ライブラリの `malloc` / `realloc` / `calloc` / `free` と同じにする。既定アロケータは標準ライブラリの 4 関数をそのまま設定する
- **Rationale**: グローバルに 1 つしかないため、利用者データは利用者側の静的変数で足りる。型が標準と一致すれば中継関数が要らず、利用者も既存の確保関数をそのまま渡せる
- **Trade-offs**: 1 つのプロセスで状態の異なる複数のアロケータを使い分けられない（グローバルアロケータの方針として受け入れる）
- **Follow-up**: テスト用の計測アロケータは状態を `tests/support/` の静的変数に持つ

### Decision: エラーコードは X マクロの単一の一覧から生成し、末尾追加のみ
- **Context**: 要件 7.1〜7.6。後続 spec が追加していく
- **Alternatives Considered**:
  1. 明示値付きの列挙と、`default` なしの `switch` による文字列化（コンパイラの列挙漏れ警告で検出）
  2. `include/amark/error.h` の X マクロ `AMARK_ERROR_LIST(X)` に `X(名前, 文字列)` を並べ、列挙と文字列化を両方そこから生成する
- **Selected Approach**: 2（レビューで 1 から変更）。列挙子に明示値を書かず、値は並び順で決める。`AMARK_OK` は常に先頭（C の規則で 0）。列挙の生成には `AMARK_ERROR_ENUM_ENTRY_`、文字列化には `src/base/error.c` 内の `AMARK_ERROR_STRING_CASE_` を使い、いずれも使用後に `#undef` する。一覧の中に spec ごとの区切りコメントを置かない。番兵（個数を表す定数）は公開しない（追加のたびに値が変わり ABI の約束になってしまうため）。テストは一覧をテスト内で展開して個数を数える
- **Rationale**: 名前と文字列が 1 か所で組になるため、文字列のないコードは書けず、コンパイラの警告に頼る検出（MSVC では効かない）が要らない。末尾にだけ追加し並べ替え・削除・再利用をしなければ、公開済みの値は暗黙に保たれ、ABI 互換になる。区切りコメントがないため、どのコードも特定の spec 専用に見えない
- **Trade-offs**: 公開ヘッダに展開用のマクロが入る。値が並び順に依存するため、一覧の途中への挿入は値を変えてしまう（規約とテストの連番確認で防ぐ）
- **Follow-up**: 後続 spec はコードを追加する際、design.md のエラーコード一覧表（追加した spec・文字列・意味）の末尾に行を追記し、実装タスクで `AMARK_ERROR_LIST` の末尾に `X(...)` を 1 項目追記する。`error.c` と `error_test.c` は変更しない。並行する spec の追記が衝突したら一覧表の行の順に並べる

### Decision: バージョンは手書きの公開ヘッダを唯一の出所とする
- **Context**: 要件 2.5、2.6、4.6、4.7
- **Alternatives Considered**:
  1. `project(amark VERSION x.y.z)` を出所とし、テンプレートから `version.h` を構成時にビルドツリーへ生成する（cmark の方式）
  2. `include/amark/version.h` に数値を手書きし、トップレベルの `CMakeLists.txt` が `project()` の前に `file(STRINGS ... REGEX)` と `string(REGEX REPLACE ...)` で読み、`project(amark VERSION ${AMARK_VERSION} LANGUAGES C)` に渡す（zlib、curl、libuv、libgit2、expat の方式）
- **Selected Approach**: 2（レビューで 1 から変更）。`AMARK_VERSION_MAJOR` / `MINOR` / `PATCH` を 1 回だけ書き、`AMARK_VERSION_NUMBER` は `AMARK_VERSION_ENCODE` で、`AMARK_VERSION_STRING` は内部用の `AMARK_VERSION_STRINGIFY_` / `AMARK_VERSION_XSTRINGIFY_` による文字列化で、それぞれ数値から組み立てる。パッケージ設定のバージョンと `SOVERSION` は `project()` の値から得る。リリースワークフローもヘッダから読んでタグと比べる
- **Rationale**: ヘッダがソースツリーに存在するため、IDE、CMake 以外のビルドシステム、ソースを直接取り込む利用者がビルドツリーの include ディレクトリなしに使える。生成ヘッダのインストールやヘッダ検査での特別扱いが不要になる
- **Trade-offs**: CMake とリリースワークフローが `#define AMARK_VERSION_<PART> <数値>` の書式を前提にする（Revalidation Trigger とし、読めなければ構成とリリースを失敗させる）

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
- **Follow-up**: tech.md の例はこの形に修正済み（横断レビューでの是正）。実装時は 5.1 で実際の宣言と突き合わせる

### Decision: UTF-8 の逆方向復号を foundation に置く
- **Context**: 要件 10.8。commonmark の強調の flanking 判定（直前の文字の分類）と markdown-renderer の出力末尾の文字の分類が、ある位置の直前の 1 文字を必要とする（横断レビューで判明）
- **Selected Approach**: `amark_utf8_decode_previous(input, offset, &codepoint, &consumed)`。結果は「先頭から `offset` までを前方へ復号したときの最後の単位」と一致させる。継続バイトでないバイトは前方復号で必ず新しい単位を始めるので、直前 4 バイト内の最後の非継続バイトから前方の復号を再利用すれば足りる
- **Rationale**: 各 spec が独自に逆方向の走査を書くと、不正バイトの扱いが前方の復号とずれる
- **Trade-offs**: 先頭から全体を復号するのに比べ、読み取りは直前 4 バイトに限られる

### Decision: テスト補助と共有の骨組みファイルの規則
- **Context**: 横断レビューで、後続 spec が `amark.h`、`src/CMakeLists.txt`、`tests/CMakeLists.txt`、`tests/cxx_header_test.cpp`、`amark_test_support` に並行して追加すること、テスト補助の名前が spec ごとにばらつくこと、メモリ使用量の検証にバイト数が必要なことが判明した
- **Selected Approach**: これらの骨組みファイルは foundation が所有し、後続 spec は spec 名のコメント付きブロックを末尾に追記するだけとする（衝突は両ブロックを残して解消）。テスト補助は `tests/support/test_<area>.{h,c}` と `test_<area>_*`。計測アロケータはブロックごとにサイズのヘッダを付け、`bytes_live`・`bytes_total`・`bytes_peak` を数える
- **Rationale**: 追記専用にすれば並行実装の衝突が機械的に解消でき、命名を揃えればテスト補助の出所が名前から分かる

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
- **Simplification**: pkg-config ファイル、2 ターゲット同時ビルド、バージョンヘッダの生成、スライスのインライン関数化、公開 API としての Unicode 文字分類は採らない（句読点・空白の分類は commonmark の非公開モジュール `src/unicode_class` が持つ）。非公開ヘッダは現時点で不要

## Risks & Mitigations
- 動的ライブラリ構成で DLL が見つからずテストの列挙が失敗する — トップレベル時に実行ファイルと DLL の出力先を揃える。CI の Windows 動的ジョブで常に検証する
- 後続 spec が `AMARK_ERROR_LIST` の途中に挿入・並べ替えをして公開済みの値を変える — 末尾追加のみの規約を `error.h` のコメントと steering に記し、Revalidation Trigger とする。テストは各コードの値が並び順どおりの連番であることを確認する
- サイズ 0 の要求の `NULL` を確保失敗と取り違える — ライブラリ内の規則（サイズ 0 を要求しない、空は `NULL` と個数 0）を foundation の設計と steering に記し、各 spec の設計で個数が 0 になりうる確保を明記する
- clang-format の版差 — CI で版を固定し、tech.md に明記する
- アロケータ差し替えの誤用（生存中のメモリがある状態で差し替え、マルチスレッドでの差し替え）— 公開ヘッダに制約を明記する。ライブラリ側では検出しない（検出のための状態を持つとテスト専用コードに近づき、コストも増えるため）
- 後続 spec がエラーコードを削除・再利用する — 同上の規約で防ぐ

## References
- [Naos v0.4.0](https://github.com/vabelorg/naos/tree/v0.4.0) — テストフレームワーク、CMake 関数と export ヘッダの実例
- [The Unicode Standard, Chapter 3](https://www.unicode.org/versions/latest/ch03.pdf) — Table 3-7（妥当な UTF-8 バイト列）と最大部分列による置換
- [RFC 3629](https://www.rfc-editor.org/rfc/rfc3629) — UTF-8 の定義
- [CMake CMakePackageConfigHelpers](https://cmake.org/cmake/help/latest/module/CMakePackageConfigHelpers.html) — パッケージ設定とバージョン互換の生成
- [CMake TARGET_RUNTIME_DLLS](https://cmake.org/cmake/help/latest/manual/cmake-generator-expressions.7.html) — Windows での DLL コピー
