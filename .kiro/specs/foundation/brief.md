# Brief: foundation

## Problem

Amark はゼロから作る C99 ライブラリであり、ビルド・テスト・CI の土台と、全モジュールが共有する約束事（メモリ確保、エラーコード、命名、コーディング規約）がまだ存在しない。これらを各 spec がばらばらに決めると、エラーコードや API の流儀が分裂する。

## Current State

リポジトリには kiro の設定と CLAUDE.md しかない。ソースコード、ビルド定義、CI、ライセンスファイルはいずれも未作成。

## Desired Outcome

- CMake で静的ライブラリと動的ライブラリの両方をビルドでき、インストールして `find_package` で利用できる
- Windows / Linux / macOS の CI でビルドとテストが通る
- Naos によるテストが CTest から実行できる
- 全モジュールが使うグローバルアロケータ、エラーコード、共通ユーティリティが揃っている
- コーディング規約が文書化され、可能な範囲で機械的に検査できる

## Approach

後続 spec が共通して依存するものだけを最小限で先に作る。エラーコードは単一の列挙として定義し、後続 spec が追加していく前提にする。ユーティリティは後続 spec で確実に必要になるもの（可変長バッファ、長さ付き文字列の扱い、UTF-8 の基本操作）に絞り、不足分は必要になった spec から遡って追加する。

## Scope

- **In**:
  - リポジトリ構成（`include/amark/`、`src/`、`tests/` など）と CMake（C99、静的/動的の両ターゲット、シンボル可視性と export マクロ、install / package config、バージョン情報）
  - CI/CD（GitHub Actions、3 OS × 静的/動的、テスト実行、リリース手順）
  - MIT ライセンス（copyright: The Amark Authors）とソースヘッダの表記
  - Naos v0.4.0 の取り込み（FetchContent でタグ固定）とテスト用 CMake の雛形
  - 差し替え可能なグローバルアロケータ: `amark_malloc`, `amark_realloc`, `amark_calloc`, `amark_free` と差し替え API
  - 共通エラーコード（単一の列挙と文字列化）
  - 共通ユーティリティ（可変長バッファ、文字列スライス、UTF-8 基本操作）
  - コーディング規約の文書化（steering）と整形設定（`.clang-format` など）
- **Out**:
  - ノード、パーサ、レンダラに関する一切
  - CLI ツール、他言語バインディング

## Boundary Candidates

- ビルド・パッケージング（CMake、export マクロ、install）
- CI/CD
- メモリ（グローバルアロケータ）
- エラーコード
- 共通ユーティリティ（バッファ、文字列、UTF-8）
- 規約とツール設定

## Out of Boundary

- 特定のノード種別や構文に依存するユーティリティ（各 spec が持つ）
- テスト専用のフックやテスト用アロケータをライブラリ本体に入れること（テスト側でアロケータを差し替えて実現する）

## Upstream / Downstream

- **Upstream**: なし（Naos を外部依存としてテスト時のみ利用）
- **Downstream**: 全 spec（node-tree、parser-core、html-renderer、commonmark、markdown-renderer）

## Existing Spec Touchpoints

- **Extends**: なし
- **Adjacent**: node-tree（文字列の所有権規約を共有する）

## Constraints

- C99、CMake、MIT、Windows / Linux / macOS。詳細とコーディング規約は `.kiro/steering/roadmap.md` の Constraints を正とする
- アロケータの差し替えはグローバルで行う。ライブラリ内の動的確保はすべて `amark_*` を経由する
- 関数命名: 構造体の初期化・後片付けは `_init` / `_deinit`、opaque 構造体の生成・破棄は `_create` / `_destroy`。typedef 名は `_t`
- 動的ライブラリは Windows の DLL export / import を正しく扱う
- エラーコードは後続 spec が追加していく前提で設計し、spec ごとの独自体系を生まない
