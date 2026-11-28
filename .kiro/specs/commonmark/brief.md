# Brief: commonmark

## Problem

`parser-core` は構文を知らないエンジンであり、それだけでは Markdown を解釈できない。利用者が最初に必要とする方言である CommonMark を、拡張 API の上に実装する必要がある。これは拡張 API が実用に足ることの証明でもある。

## Current State

`parser-core` がエンジンと拡張登録 API を、`node-tree` が標準ノード種別を、`html-renderer` が HTML 出力を提供する想定。具体的な構文の実装は存在しない。

## Desired Outcome

- CommonMark の各構文が方言に属さない個別の規則（拡張）として提供され、それらをまとめた CommonMark 方言プリセットでパースできる
- CommonMark 仕様（spec.txt）の例がすべて通る
- 個々の構文を外した方言（例: 生 HTML なし、インデントコードブロックなし）を組み立てられる
- 病的な入力（深いネスト、大量のデリミタやブラケット）で処理時間が爆発しない

## Approach

構文ごとにブロック拡張・インライン拡張を「規則」として実装し（`src/syntax/`、`amark/syntax.h`）、`parser-core` の公開拡張 API だけを使う。方言プリセット（`src/cmark/`、`amark/cmark.h`）は規則の公開 API だけで規則を集める。エンジンの内部に手を入れたくなった場合は、裏口を作らず `parser-core` の API を改訂する（spec 横断の変更として扱う）。仕様の spec.txt から例を抽出してテストデータとし、パース → HTML レンダリングの結果を期待値と比較する。

## Scope

- **In**:
  - ブロック構文: thematic break、ATX / Setext 見出し、インデント / フェンスコードブロック、HTML ブロック、リンク参照定義、段落、空行、ブロック引用、リストとリスト項目
  - インライン構文: バックスラッシュエスケープ、エンティティ・数値文字参照、コードスパン、強調・強い強調、リンク、画像、オートリンク、生 HTML、ハード / ソフト改行、テキスト
  - CommonMark 方言プリセット（拡張セット）と、それを使う簡易パース API
  - HTML エンティティ表など CommonMark が必要とするデータ
  - CommonMark 仕様適合テスト（対象バージョンを固定）と病的入力テスト
- **Out**:
  - エンジン本体（parser-core）
  - HTML / Markdown の出力（各レンダラ）
  - GFM 拡張（次フェーズ）

## Boundary Candidates

- ブロック構文群
- インライン構文群
- 方言プリセットと簡易 API
- 仕様適合テスト基盤（spec.txt の例の抽出と実行）

## Out of Boundary

- CommonMark 仕様にない構文や独自の挙動
- 仕様で実装依存とされる部分以外でのレンダラ側の調整（HTML 出力の差異は html-renderer を修正する）

## Upstream / Downstream

- **Upstream**: parser-core、html-renderer、node-tree、foundation
- **Downstream**: markdown-renderer（往復テスト）、将来の gfm（GFM の規則を規則の層に足し、CommonMark の規則と合わせて GFM 方言プリセットを作る）

## Existing Spec Touchpoints

- **Extends**: なし（ただし実装中に parser-core の拡張 API、node-tree のノード種別、foundation のエラーコードを必要に応じて改訂する）
- **Adjacent**: parser-core（エンジンと構文の責務境界）、html-renderer（期待 HTML との一致）

## Constraints

- 構文の実装は `parser-core` の公開拡張 API だけを使い、エンジンの非公開部分に依存しない
- 仕様適合テストのためだけのコードをライブラリ本体に入れない（例の抽出や比較はテスト側に置く）
- 対象とする CommonMark 仕様のバージョンを明記し、spec.txt をリポジトリに固定して持つ（ライセンス表記を確認する）
- コーディング規約は `.kiro/steering/roadmap.md` の Constraints を正とする
