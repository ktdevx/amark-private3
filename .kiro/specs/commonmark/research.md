# Research & Design Decisions

## Summary
- **Feature**: `commonmark`
- **Discovery Scope**: New Feature（greenfield。上流 4 spec の設計のみ存在し、ソースコードはない）
- **Key Findings**:
  - CommonMark 仕様の最新版は 0.31.2（2024-01-28）。spec.txt は CC BY-SA 4.0、例は 32 個の `` ` `` と ` example` の行で始まり、`.` で入力と期待 HTML を分け、32 個の `` ` `` だけの行で終わる。タブは `→` で表記される
  - parser-core の公開 API（ブロック文脈、段落の処理器、デリミタ処理器、ブラケット、定義テーブル、拡張ごとの解析状態）で CommonMark の全構文を表現できる。API の追加は不要で、文書の寿命の義務の緩和と 2 つの意味の明確化（空行の判定位置、インライン対象の同一性）が必要（C1〜C3。parser-core の文書に適用済み）
  - cmark の病的入力テストは 25 類型（`test/pathological_tests.py`、1 件 5 秒の制限）。ブロックの入れ子の上限（既定 1000）を超える 3 類型は、Amark では文書化された上限の挙動を期待値とし、上限を上げた設定で cmark と同じ構造になることを別に確かめる

## Research Log

### CommonMark 仕様の版と spec.txt
- **Context**: 対象の版の固定と、適合テストの入力形式の確認
- **Sources Consulted**: https://spec.commonmark.org/（版の一覧）、https://raw.githubusercontent.com/commonmark/commonmark-spec/0.31.2/spec.txt
- **Findings**:
  - 版 0.31.2、日付 2024-01-28、author John MacFarlane、license `CC-BY-SA 4.0`（YAML front matter）
  - 例の数は 652（0.31.2 の参照実装の報告値）。抽出器は数を数えて固定値と比べる
  - 0.31 で「Unicode punctuation」が一般カテゴリ P と S の和に変わった。HTML ブロックの種類 1 に `textarea` が加わり、コメント `<!-->`、`<!--->` が成立するようになった
- **Implications**: spec.txt を改変せずに `tests/data/commonmark/` に置き、同じ場所にライセンス表記を置く。比較は正規化なしのバイト比較（html-renderer の Cross-Spec Contract）

### cmark の病的入力テスト
- **Context**: 要件 9.2 の類型の出典
- **Sources Consulted**: https://raw.githubusercontent.com/commonmark/cmark/master/test/pathological_tests.py
- **Findings**: nested strong emph（32500）、many emph closers / openers、many link closers / openers、mismatched openers and closers、issue #389、openers and closers multiple of 3、link openers and emph closers、pattern `[ (](`、pattern `![[]()`、hard link/emph case、nested brackets（25000）、nested block quotes（25000）、deeply nested lists（500）、U+0000、backticks（2500）、unclosed links A / B、unclosed `<!--`（300000）、empty lines in deeply nested lists（30000）、同 in blockquote、emph in deep blockquote（100000）、reference collisions（ハッシュ衝突）。期待値は正規表現、制限時間は 1 件 5 秒
- **Implications**: 入力と期待値の形を Amark のテストへ移植する（コードは取り込まない）。reference collisions は cmark のハッシュ関数に依存するため、「多数の異なるラベルの定義と参照」に置き換える（parser-core の定義テーブルはパーサごとの種を持つ）

### HTML5 名前付き文字参照
- **Context**: 要件 10.1 のデータの出典とライセンス
- **Sources Consulted**: https://html.spec.whatwg.org/entities.json（WHATWG HTML Living Standard、CC BY 4.0）、cmark の `src/entities.inc` の生成方法
- **Findings**: 一覧は `;` 付きと `;` なしの名前を含む。CommonMark は `;` で終わる名前だけを認める。値は 1 または 2 個のコードポイント。最長の名前は 31 文字（`CounterClockwiseContourIntegral`）
- **Implications**: `;` 付きの名前だけを名前の昇順に並べた静的表を生成して二分探索する。Living Standard で版がないため、取得日と SHA-256 を生成物に記録する

### Unicode 文字データベース
- **Context**: flanking の判定（句読点・空白）とラベルの正規化（大文字小文字の畳み込み）
- **Sources Consulted**: https://www.unicode.org/Public/16.0.0/ucd/UnicodeData.txt、https://www.unicode.org/Public/16.0.0/ucd/CaseFolding.txt、Unicode License v3
- **Findings**: 句読点は一般カテゴリ Pc Pd Pe Pf Pi Po Ps Sc Sk Sm So。空白は Zs とタブ・LF・FF・CR。畳み込みは CaseFolding の状態 C と F（完全な畳み込み。`ẞ` → `ss`、仕様の例で必要）
- **Implications**: コードポイントの範囲の表（句読点）、小さな表（Zs）、`{元, 畳み込み先 1〜3 個}` の表を生成する。版を 16.0.0 に固定し、更新は生成の再実行で行う

### parser-core の API との対応（構文ごとの検証）
- **Context**: 「公開 API だけで実装する」制約の確認と、不足の洗い出し
- **Sources Consulted**: `.kiro/specs/parser-core/design.md`（ブロック構文の書き方の表、文脈 API、デリミタ・ブラケット）、cmark `blocks.c` / `inlines.c` の規則
- **Findings**:
  - リスト項目の開始で「記号の後の空白の桁を数え、5 桁以上なら 1 桁だけ読む」ための巻き戻しは API にないが、`get_line`・`get_offset`・`get_column` から桁を先読みで計算してから `advance_columns` すれば巻き戻し不要
  - リスト項目の開始で、追加先のリストの記号（箇条書きの文字、区切り文字）を知る必要がある。ブロックの状態は他の構文から読めないため、リスト拡張の解析状態に「開いているリストの記録のスタック」を持つ（開くときに積み、`finalize` で除く。追加先のノードと一致する記録を上から探す。探索で通過した記録はこの開始で閉じられるため償却定数）
  - 空項目の空行の例外（cmark の「子がなくこの行で開いた項目は空行を記録しない」）は、項目の状態のフラグ（開始で真、`continue_line` で偽）と `record_blank` で表せる
  - 空行の記録・段落の継続・遅延継続で「行が空行」が「継続と開始の後の位置から行末まで空白」を意味することが必要（`> - a\n>\n> - b` を loose にするため）。parser-core の設計は BlockLine の `blank`（現在位置からの判定）を使っており整合するが、要件の文言は「行が空行」なので意味の明確化を依頼する
  - コードスパンの `` ` `` の位置の記憶、生 HTML の閉じのない構造の記憶、参照展開の量は「インライン対象ごと」にやり直す必要がある。インライン対象の切り替わりは `amark_inline_context_get_block` のノードの変化で検出できる（対象は順に 1 つずつ最後まで処理される）。この保証を `inline_syntax.h` の契約として明記するよう依頼する
  - 簡易パース `amark_cmark_parse` は内部で方言を作って破棄し、文書だけを返す。parser-core の要件 13.8・14.9 は「方言は文書より後に破棄」と定めるが、CommonMark の拡張はノード種別を定義せず、文書は方言を参照しない。拡張種別のノードを含まない文書には寿命の義務が及ばないことを契約として明記するよう依頼する
- **Implications**: parser-core への API の追加・変更は不要。3 件（契約の緩和 1、意味の明確化 2）は parser-core の要件・設計に適用済みで、本仕様は依存する振る舞いを確かめるだけ

### 参照展開の量の上限
- **Context**: `[a]: <長い URL>` と大量の `[a]` で、出力と木のメモリが入力長の二乗になる
- **Sources Consulted**: cmark 0.30 の参照展開の上限（入力長と 100000 の大きい方を超えたら参照を解決しない）
- **Findings**: cmark は入力全体の長さを上限に使う。parser-core の文脈 API は入力全体の長さを提供しない
- **Implications**: 上限を「100000 とそれまでにインライン解析した内容の合計長の大きい方」とする（リンク拡張の解析状態で数える）。仕様の例の範囲では発動しない

## Architecture Pattern Evaluation

| Option | Description | Strengths | Risks / Limitations | Notes |
|--------|-------------|-----------|---------------------|-------|
| 構文ごとに 1 拡張（17 単位） | 取り外しの単位 = 仕様の構文の単位。状態や記号を共有する構文（リストと項目、リンクと画像、強調の 2 文字、ハードとソフトの改行）だけをまとめる | 「生 HTML なし」「インデントコードなし」を名前 1 つの取り外しで表せる。構文ごとにファイルとタスクを分けられる | 拡張の数が多く、方言の生成でやや多くの確保が起きる | 採用 |
| 数個のグループ（ブロック・インライン・HTML） | 粗い拡張 | 生成が軽い | インデントコードだけを外せない。要件 1 を満たさない | 不採用 |
| 1 つの拡張＋構文ごとの有効フラグ | 拡張内で構文を選ぶ | 生成が最軽量 | parser-core の Non-Goal（構文単位の無効化は拡張の取り外しで代替）に反する。名前で外せない | 不採用 |

## Design Decisions

### Decision: 拡張の粒度と名前
- **Context**: 要件 1.1、1.4、1.6
- **Alternatives Considered**: 上表
- **Selected Approach**: 17 単位。拡張名と構文名は `cmark_` で始める（例: `cmark_html_block`）。単位はビットフラグの `#define` 定数（`uint32_t`）で指定し、集合はビットの論理和
- **Rationale**: 取り外したい単位と仕様の構文が一致する。名前の接頭辞で将来の gfm や第三者の拡張と衝突しない
- **Trade-offs**: リンクと画像は分けない（`]` の処理が共有で、画像だけを外す用途が乏しい）。リンク参照定義はリンクと別にする（定義を外すと参照リンクが成立しない方言を作れる）
- **Follow-up**: GFM が優先順位を挟めるよう、ブロックの開始判定の優先順位を 10 刻みにする

### Decision: 簡易 API の形
- **Context**: 要件 3
- **Alternatives Considered**:
  1. プロセス全体で共有する静的な方言 — 可変のグローバル状態と初期化の競合を生むため不採用
  2. 呼び出しごとに方言を作る — 生成の費用はかかるが状態を持たない
- **Selected Approach**: 2。`amark_cmark_parse` と `amark_cmark_convert_to_html`。繰り返し使う利用者には方言の再利用を案内する
- **Rationale**: 要件 12 の並行利用とスレッド安全を単純に満たす
- **Trade-offs**: 呼び出しごとに 17 拡張の生成と凍結が起きる
- **Follow-up**: parser-core の寿命の義務の緩和（C1、適用済み）。HTML への簡易変換は `amark/cmark_html.h` に分け、`amark/cmark.h` をレンダラに依存させない

### Decision: データの生成方式
- **Context**: 要件 10
- **Alternatives Considered**:
  1. 生データ（entities.json、UnicodeData.txt）をリポジトリに置きビルド時に生成 — ビルドに生成器が必要になり要件 10.4 に反する
  2. CMake スクリプトで生成 — 2 MB の UnicodeData.txt の処理が遅く保守しにくい
  3. Python 3（標準ライブラリのみ）の生成器を `tools/` に置き、生成した C の表をコミット
- **Selected Approach**: 3。生成器は入力ファイルのパスを受け取り、出典の URL・版・SHA-256 を生成物の先頭に記録する。ビルドと CI は生成器を実行しない
- **Rationale**: cmark と同じ方式で、ビルドは C コンパイラと CMake だけで済む
- **Trade-offs**: 生成物と生成器の一致は CI で自動検査しない。表の内容のテスト（件数、代表値）で破損を検出する

### Decision: 適合テストの抽出器を C で書く
- **Context**: 要件 8.2、8.7
- **Alternatives Considered**: cmark の `spec_tests.py` 相当を Python で書く / C で書く
- **Selected Approach**: `tests/support/test_spec_examples.c`（関数 `test_spec_examples_*`、テスト補助の命名規約）が実行時に spec.txt を読み、例の配列を作る
- **Rationale**: テストの実行に C コンパイラと CMake 以外を要求しない（3 OS の CI で Python の有無に依存しない）

### Decision: 病的入力の期待値と時間の検査
- **Context**: 要件 9.2、9.3
- **Selected Approach**: 類型ごとに入力を生成する関数と、出力を検査する関数（完全一致、または接頭辞・接尾辞・タグの個数）をテストに持つ。各類型の解析と変換に `clock()` で 5 秒（CPU 時間）の上限を設け、CTest の TIMEOUT を実行ファイル全体の上限とする
- **Rationale**: 二乗の実装はこの規模で数十秒以上かかり、線形の実装は 1 秒未満で終わる。呼び出し回数の線形性は parser-core が確かめ済みで、ここでは構文側の走査を対象にする

### Decision: 句読点・空白の分類を共有の非公開モジュールにする
- **Context**: 要件 7.1、10.2。markdown-renderer も出力側の flanking で同じ分類を必要とする
- **Selected Approach**: 最初から `src/base/unicode_class.{h,c}` と生成物 `src/base/unicode_class_data.c` に `amark_unicode_is_punctuation` / `amark_unicode_is_whitespace` を置く（非公開、export しない）。畳み込みの表はラベルの正規化だけが使うため `src/cmark/cmark_unicode_data.c` に commonmark 専用として残す。生成器は 2 つのファイルを出力する。直前の文字の復号は foundation の `amark_utf8_decode_previous` を使う
- **Rationale**: レンダラがパーサの非公開ヘッダに依存せず、同一の表と判定を共有できる

## Risks & Mitigations
- 仕様の例とレンダラの規則のずれ — 失敗した例の差分を報告し、レンダラ起因なら html-renderer の組み込み出力の表を修正する（brief の約束）
- 生成した表の破損・古さ — 件数と代表値のテスト、生成物の先頭のハッシュ値
- 構文側の走査の二乗化（コードスパン、生 HTML、リンクの宛先、ラベル） — 内容ごとの記憶、ラベル長 999 の早期打ち切り、括弧の入れ子 32 段、病的入力テスト
- 参照展開による出力の爆発 — 要件 9.4 の上限

## References
- [CommonMark Spec 0.31.2](https://spec.commonmark.org/0.31.2/) — 対象の版
- [spec.txt 0.31.2](https://raw.githubusercontent.com/commonmark/commonmark-spec/0.31.2/spec.txt) — 固定するテストデータ（CC BY-SA 4.0）
- [cmark pathological_tests.py](https://github.com/commonmark/cmark/blob/master/test/pathological_tests.py) — 病的入力の類型
- [WHATWG entities.json](https://html.spec.whatwg.org/entities.json) — 名前付き文字参照（CC BY 4.0）
- [Unicode 16.0.0 UCD](https://www.unicode.org/Public/16.0.0/ucd/) — UnicodeData.txt、CaseFolding.txt（Unicode License v3）
