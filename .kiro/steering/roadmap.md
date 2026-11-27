# Roadmap

## Overview

Amark は Markdown テキストをパースする C99 ライブラリである。テキストを木（AST）へパースし、木を編集し、HTML および Markdown へレンダリングできる。構文解析は拡張可能で、拡張の組み合わせによって CommonMark や GFM などの方言を表現する。

パーサエンジンは個々の構文を一切知らず、CommonMark の各構文も将来の GFM 構文も同じ登録 API を通じて載せる。方言は「拡張セットのプリセット」として定義する。今回のフェーズでは基盤から CommonMark 方言、HTML / Markdown レンダラまでを対象とし、GFM は次フェーズとする。

## Approach Decision

- **Chosen**: 全構文を拡張として登録する方式。ブロック解析→インライン解析の 2 フェーズエンジンを構文非依存に作り、CommonMark の構文一式を拡張 API の最初の利用者として実装する。ノードの値は木に持つ（cmark 方式）。
- **Why**: 「拡張の組み合わせで方言を表現する」という要求にそのまま合致する。拡張 API がライブラリ本体によって常に使われるため、API の不足が早期に露見する。CommonMark 構文の取り外し・差し替えも可能になる。
- **Rejected alternatives**:
  - コア固定＋フック（cmark-gfm 方式）: CommonMark をハードコードするため、構文の取り外し・差し替えができず方言の表現力が限られる。
  - トークナイザ→イベント列→木（micromark 方式）: 拡張性は高いが C99 では複雑で、ロスレスを対象外とする今回には過剰。

## Scope

- **In**:
  - ビルド基盤（CMake、静的/動的ライブラリ、CI/CD、Windows / Linux / macOS）
  - グローバルアロケータ、共通エラーコード、共通ユーティリティ
  - ノード木と編集 API、走査
  - 構文非依存のパーサエンジンと拡張登録 API、方言定義
  - CommonMark 構文一式と仕様適合テスト
  - HTML レンダラ、Markdown レンダラ
- **Out**:
  - ロスレス（原文の完全復元、ソース位置を保った CST）
  - GFM（テーブル、取り消し線、タスクリスト、拡張オートリンク、tagfilter）— 次フェーズ。ただし拡張 API とレンダラフックは GFM を載せられる形にしておく
  - CLI ツール、他言語バインディング

## Constraints

### 技術

- 言語: C99。ビルド: CMake。静的ライブラリと動的ライブラリの両方を提供する
- 対応プラットフォーム: Windows / Linux / macOS（CI で 3 OS すべてをビルド・テストする）
- ライセンス: MIT。copyright 表記は `The Amark Authors`
- テストフレームワーク: [Naos](https://github.com/vabelorg/naos)（v0.4.0、MIT、C99、CMake 3.21 以上）。1.0 前で互換性が壊れうるため、FetchContent でタグを固定して取り込む。CTest 連携は `naos_add_executable` を使う
- ライブラリ本体をテストするためだけのコードを、ライブラリ側のソースコードに持たない

### コーディング規約

- 波括弧は Allman 方式
- 演算の順序は括弧で明示する。`&&` / `||` のオペランドになる比較は括弧で囲む。比較の左右に算術式（`+ - * / % << >>`）があれば、それも括弧で囲む。比較でないオペランド（変数、関数呼び出し、`!x`）には括弧を付けない
- 処理のまとまり（ステップ）が変わるところに空行を 1 つ入れる。1 つのステップの文は空行を挟まずに続ける
- typedef した型名には `_t` を付ける
- 構造体の中身の初期化・後片付けを行う関数名は `_init` / `_deinit`
- opaque な構造体の生成・破棄を行う関数名は `_create` / `_destroy`
- 差し替え可能なグローバルアロケータを持つ: `amark_malloc`, `amark_realloc`, `amark_calloc`, `amark_free`。ライブラリ内の動的確保はすべてこれを経由する

### 進め方

- spec という括りに縛られすぎない。エラーコード、公開 API の命名、所有権規約など spec を横断する約束事は一貫性を優先し、後続 spec の作業中に不整合や不足が見つかった場合は、完了済みの spec（要件・設計・実装）にも遡って変更する
- 横断的な約束事の正本は `foundation` spec と steering に置き、各 spec で独自のエラーコード体系や命名規則を作らない

## Boundary Strategy

- **Why this split**: 「データ構造（node-tree）」「構文非依存の解析機構（parser-core）」「具体的な構文（commonmark）」「出力（html-renderer / markdown-renderer）」を分けることで、レンダラは手組みの木だけでテストでき、パーサはレンダラなしで木を検証できる。エンジンと構文を別 spec にすることで、拡張 API が CommonMark の都合に寄りすぎていないかを境界でレビューできる。
- **Shared seams to watch**:
  - **エラーコード**: `foundation` が単一の列挙を所有する。後続 spec はそこへ追加し、独自体系を作らない
  - **ノード種別の所有**: 標準ノード種別（CommonMark が必要とするもの）とそのアクセサは `node-tree` が所有する。拡張が定義するノード種別の登録機構も `node-tree` が提供し、`parser-core` の拡張 API と整合させる
  - **エンジンと構文の境界**: 強調のデリミタ処理、リストの tight / loose 判定、リンク参照定義、遅延継続行など、CommonMark 固有に見えるが複数構文にまたがる処理をどちらが持つか。`commonmark` 実装中に `parser-core` の API 変更が必要になる前提で進める
  - **レンダラフック**: 両レンダラはノード種別ごとの出力を差し替え・追加できる同じ形の機構を持つ。将来の GFM 拡張が「構文＋ノード種別＋ HTML 出力＋ Markdown 出力」を 1 つの拡張としてまとめて登録できること
  - **文字列の所有権と入力エンコーディング**: 木が値を所有する規約、UTF-8 の扱い、長さ付き文字列か NUL 終端かを `foundation` / `node-tree` で決め、全 spec が従う
  - **仕様適合テストの所在**: CommonMark の spec テスト（Markdown → HTML）は `commonmark` が所有し、`html-renderer` に依存する

## Specs (dependency order)

- [x] foundation -- ビルド基盤（CMake、静的/動的、CI/CD、3 OS）、ライセンス、グローバルアロケータ、共通エラーコード、共通ユーティリティ、Naos 導入、コーディング規約の整備. Dependencies: none
- [x] node-tree -- 値を木に持つノード、標準ノード種別とアクセサ、拡張ノード種別の登録、木の編集 API、走査. Dependencies: foundation
- [x] parser-core -- 構文非依存の 2 フェーズ（ブロック→インライン）パーサエンジン、構文拡張の登録 API、方言（拡張セット）定義. Dependencies: foundation, node-tree
- [x] html-renderer -- 木から HTML へのレンダリング、ノード種別ごとのレンダラフック. Dependencies: foundation, node-tree
- [x] commonmark -- CommonMark 構文一式を拡張として実装、CommonMark 方言プリセット、仕様適合テスト. Dependencies: parser-core, html-renderer
- [x] markdown-renderer -- 木から Markdown へのレンダリング、ノード種別ごとのレンダラフック、往復テスト. Dependencies: node-tree, commonmark, html-renderer

## Future Phases

- gfm -- GFM 拡張（テーブル、取り消し線、タスクリスト、拡張オートリンク、tagfilter）と GFM 方言プリセット。拡張 API とレンダラフックが固まった後に着手する. Dependencies: commonmark, html-renderer, markdown-renderer
