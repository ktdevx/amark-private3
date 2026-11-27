# Brief: html-renderer

## Problem

パースした、あるいは API で組み立てた木を HTML として出力する手段が必要である。Markdown ライブラリの最も基本的な用途であり、CommonMark 仕様適合の検証にも HTML 出力が必須となる。

## Current State

`foundation` と `node-tree` が基盤と木を提供する想定。レンダラに関するコードは存在しない。

## Desired Outcome

- 標準ノード種別からなる木を、CommonMark 仕様の期待出力と一致する HTML にレンダリングできる
- 生 HTML や危険な URL を無害化する安全側のオプションがある
- 拡張がノード種別ごとの HTML 出力を追加・差し替えできる
- パーサなしで、手組みの木だけを使ってテストできる

## Approach

`node-tree` のイテレータで木を enter / exit 走査し、ノード種別ごとのレンダリング関数を表引きで呼び出す。標準ノード種別の関数は組み込みで持ち、拡張ノード種別や上書きは登録されたフックを使う。出力は `foundation` の可変長バッファに書き出す。フックの形は markdown-renderer と揃える。

## Scope

- **In**:
  - 木から HTML 文字列へのレンダリング API
  - 標準ノード種別すべての HTML 出力（tight / loose リスト、コードブロックの info string、画像の alt テキスト平坦化などを含む）
  - HTML エスケープ、URL のパーセントエンコード
  - オプション（生 HTML の抑止、危険な URL スキームの抑止、soft break の出力方法など）
  - ノード種別ごとのレンダラフック（追加と上書き）
- **Out**:
  - パース
  - CommonMark 仕様適合テストの実行（commonmark が所有し、本 spec に依存する）
  - XML、LaTeX、man など他形式への出力
  - GFM の tagfilter（次フェーズ）

## Boundary Candidates

- レンダリング駆動部（走査とディスパッチ）
- 標準ノード種別の HTML 出力
- エスケープ処理
- フック登録機構

## Out of Boundary

- HTML のサニタイズ全般（生 HTML を出すか出さないかの切り替えに留める）
- 構文ハイライトなど出力の加工

## Upstream / Downstream

- **Upstream**: foundation、node-tree
- **Downstream**: commonmark（仕様適合テスト）、markdown-renderer（往復テスト）、将来の gfm

## Existing Spec Touchpoints

- **Extends**: なし
- **Adjacent**: markdown-renderer（フック機構の形を共通にする）、parser-core（拡張が構文とレンダラフックをまとめて提供できること）

## Constraints

- 既定の出力は CommonMark 仕様のテストスイートが期待する HTML と一致させる
- 失敗は `foundation` の共通エラーコードで返す
- 出力バッファの確保は `amark_*` アロケータを経由する
- コーディング規約は `.kiro/steering/roadmap.md` の Constraints を正とする
