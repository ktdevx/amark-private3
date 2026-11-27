# Brief: markdown-renderer

## Problem

Amark は木を編集できる。編集した結果を Markdown として書き戻せなければ、Markdown 文書の機械的な変換・整形という用途が成立しない。

## Current State

`node-tree` が木を、`html-renderer` がレンダラフックの形を、`commonmark` がパーサを提供する想定。Markdown 出力に関するコードは存在しない。

## Desired Outcome

- 標準ノード種別からなる木を CommonMark として有効な Markdown テキストにレンダリングできる
- 出力した Markdown を再度パースすると、元の木と意味的に等価な木（同じ HTML）になる
- 拡張がノード種別ごとの Markdown 出力を追加・差し替えできる
- 折り返し幅などの整形オプションがある

## Approach

`node-tree` のイテレータで木を走査し、ノード種別ごとのレンダリング関数を表引きで呼び出す。ブロック引用やリスト項目の行頭プレフィックスをスタックで管理し、文脈に応じたエスケープ（行頭の記号、強調記号、リンク文字列内など）を行う。フック機構は html-renderer と同じ形にする。ロスレスは対象外なので、原文の記法（`*` と `_` の別、見出しの形式など）は保持せず、正規化した記法で出力する。

## Scope

- **In**:
  - 木から Markdown 文字列へのレンダリング API
  - 標準ノード種別すべての Markdown 出力（ネストしたリスト・引用のプレフィックス、tight / loose、コードブロックのフェンス長の決定、コードスパンのバッククォート数の決定など）
  - 文脈依存のエスケープ
  - オプション（折り返し幅、リストマーカー・強調記号・見出し形式などの選択）
  - ノード種別ごとのレンダラフック（追加と上書き）
  - 往復テスト（Markdown → 木 → Markdown → 木 で HTML が一致することを CommonMark 仕様の例で確認）
- **Out**:
  - 原文と一字一句同じ出力（ロスレス）
  - パース
  - GFM ノードの出力（次フェーズ。フックで追加できる形にする）

## Boundary Candidates

- レンダリング駆動部（走査、プレフィックス管理、折り返し）
- 標準ノード種別の Markdown 出力
- エスケープ処理
- フック登録機構
- 往復テスト基盤

## Out of Boundary

- 文書のフォーマッタとしての高度な機能（表の桁揃え、参照リンクの再構成など）
- 原文の記法の記憶をノードに持たせること

## Upstream / Downstream

- **Upstream**: foundation、node-tree、commonmark と html-renderer（往復テストで使用）
- **Downstream**: 将来の gfm

## Existing Spec Touchpoints

- **Extends**: なし
- **Adjacent**: html-renderer（フック機構を共通の形にする。共通化できる駆動部があれば spec 横断で切り出す）、commonmark（往復テストで見つかったパーサ側の不具合は commonmark を修正する）

## Constraints

- 出力は CommonMark 方言で再パース可能であること
- 往復テストのためだけのコードをライブラリ本体に入れない
- 失敗は `foundation` の共通エラーコードで返す
- コーディング規約は `.kiro/steering/roadmap.md` の Constraints を正とする
