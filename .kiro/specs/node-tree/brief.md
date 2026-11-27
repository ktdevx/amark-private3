# Brief: node-tree

## Problem

パーサの出力先であり、レンダラの入力であり、利用者が直接編集する対象でもある「木」のデータ構造と API が必要である。ここが定まらないとパーサもレンダラも着手できない。

## Current State

`foundation` がアロケータ、エラーコード、バッファ・文字列ユーティリティを提供する想定。ノードに関するコードは存在しない。

## Desired Outcome

- Markdown 文書を表す木を、パーサを介さず API だけで組み立てられる
- ノードの値（見出しレベル、リンク先、コードの中身など）は木が所有し、アクセサで読み書きできる（cmark 方式）
- 木を編集（挿入、取り外し、置換、削除）でき、不正な親子関係はエラーとして拒否される
- 木を走査できる
- 拡張が独自のノード種別を定義して同じ木に載せられる

## Approach

cmark と同様に、親・兄弟・子へのリンクを持つ opaque なノードを採用し、値はノード自身が所有する。標準ノード種別（CommonMark が必要とする範囲）は本 spec が定義する。加えて、拡張がノード種別（名前、ブロック/インラインの別、子に取れる種別、付随データの後片付け）を登録できる機構を設け、将来の GFM などが木を拡張できるようにする。

## Scope

- **In**:
  - opaque なノード型と `_create` / `_destroy`
  - 標準ノード種別（document、paragraph、heading、block quote、list、item、code block、HTML block、thematic break、text、soft break、line break、code span、emphasis、strong、link、image、HTML inline など）と種別ごとのアクセサ
  - 拡張ノード種別の登録機構と付随データの保持
  - 編集 API（子の追加、前後への挿入、取り外し、置換、部分木の破棄）と親子関係の検証
  - 走査（親・子・兄弟の取得、enter / exit イベント型のイテレータ）
  - 隣接テキストノードの結合などの木の正規化
- **Out**:
  - テキストから木を作ること（parser-core / commonmark）
  - 木から文字列を出力すること（html-renderer / markdown-renderer）
  - ソース位置の保持を前提としたロスレス表現

## Boundary Candidates

- ノードの生成・破棄と値の所有
- 種別定義（標準種別と拡張種別の登録）
- 編集と構造検証
- 走査

## Out of Boundary

- 構文に固有の解析中状態（開いているブロックの情報、デリミタスタックなど）をノードに持たせること。解析中の状態は parser-core が別途管理する
- GFM のノード種別（次フェーズ。登録機構だけ用意する）

## Upstream / Downstream

- **Upstream**: foundation
- **Downstream**: parser-core、html-renderer、markdown-renderer、commonmark、将来の gfm

## Existing Spec Touchpoints

- **Extends**: なし
- **Adjacent**: foundation（文字列所有権・エラーコード）、parser-core（拡張ノード種別の登録が構文拡張の登録と一体で使えること）

## Constraints

- 値の文字列はノードが複製して所有する。確保はすべて `amark_*` アロケータを経由する
- 失敗は `foundation` の共通エラーコードで返し、必要なコードはそこへ追加する
- 編集中のイテレータの扱い（どの操作が安全か）を明確に定義する
- コーディング規約は `.kiro/steering/roadmap.md` の Constraints を正とする
