# Research & Design Decisions: markdown-renderer

## Summary
- **Feature**: `markdown-renderer`
- **Discovery Scope**: New Feature（greenfield。上流 foundation / node-tree / parser-core / html-renderer / commonmark は設計承認済み・未実装）
- **Key Findings**:
  - cmark の `commonmark.c` は再パース可能な出力の基本形（ASCII 句読点のバックスラッシュエスケープ、行頭の `- + =` と数字＋`.` `)` のエスケープ、フェンス長 = 最長の連続 + 1、コードスパンは最短の未使用のバッククォート数、info にバッククォートがあればチルダ）を示すが、強調の隣接（`EMPH(EMPH)` だけを `_` にする）、隣接するリスト（`<!-- end list -->` の HTML ブロックを挿入し HTML が変わる）、折り返しの行頭（数字だけを避ける）は不完全である。往復で HTML の一致を求める本仕様では、これらを規則として設計し直す必要がある
  - 区切り文字の並び（delimiter run）は「開き専用（left-flanking かつ right-flanking でない）」または「閉じ専用」であれば、木の入れ子が括弧の対応になっているため、パーサの「最も近い同じ文字の開きと対にする」処理で必ず意図どおりに対になる。両用（both-flanking）の並びは、同じ文字・同じ長さの開いている祖先がなければ無害（長さの異なる祖先とは長さの和の規則で対にならない）
  - 数値文字参照（`&#N;`）は前後とも ASCII 句読点であり、区切りの隣の文字・行頭行末の空白・制御文字・リンク先と title の改行を、意味を変えずに「句読点」に変える万能の逃げ道になる。flanking の判定には Unicode の句読点・空白の分類が必要で、commonmark が内部に持つ表をレンダラ層から使えるように移す必要がある
  - 走査駆動部（当時は html-renderer 所有。現在は node-tree の `node_walk`）は、子の省略で `amark_node_iter_reset(node, EXIT)` を呼ぶが、node-tree の `reset` は「その位置の続き」から報告するため、省略したノードの EXIT が訪問されない。html-renderer 要件 11.3（省略後に EXIT を呼ぶ）と食い違っており、Markdown のフレーム管理（EXIT で確実に後片付けする）にも必要なため修正を依頼した（修正済み。現在は node-tree 要件 14.7）

## Research Log

### cmark の CommonMark 出力（commonmark.c）
- **Context**: 再パース可能な Markdown 出力の既存の到達点を確かめるため
- **Sources Consulted**: commonmark/cmark `src/commonmark.c`、`src/render.c`（master）
- **Findings**:
  - `outc` の `needs_escaping`: NORMAL では制御文字、`* _ [ ] # < > \ ` ~ !`、英字が続く `&`、行頭（`begin_content`）の `- + =`（数字の直後でない）、行頭の数字の直後の `.` `)`（空白か終わりが続く）。句読点は `\` を前置、それ以外は `&#N;`。URL では `` ` < > \ ( ) `` と空白、TITLE では `` ` < > " \ ``
  - `begin_content` は行頭のプレフィックスの後、数字の間は真のまま保たれる（番号付きリストの判定のため）
  - 強調: 親が強調で兄弟がないときだけ `_`、それ以外 `*`。強い強調は常に `**`
  - リスト: 箇条書きは常に `- `。リストの直後にリストが続くと `<!-- end list -->` を出す（HTML ブロックが増えるため HTML は一致しない）
  - フェンス: info にバッククォートがあれば `~`。長さは内容中の最長の連続 + 1、最小 3
  - コードスパン: 最短の未使用の並び。内容が空・バッククォートで始まるか終わる・前後が空白で空白以外を含むなら両側に空白
  - オートリンク: スキームがあり title がなく、子の text が URL（`mailto:` を除いたもの）と一致するとき `<...>`
  - 区切り線は `-----`。折り返しは空白で行い、次が数字の位置では折り返さない
- **Implications**: エスケープ表・フェンス長・コードスパンは cmark の規則を基礎にする。強調・隣接リスト・折り返しの行頭・行端の空白は本仕様で規則を作る

### CommonMark 0.31.2 の関連規則
- **Sources Consulted**: `tests/data/commonmark/spec-0.31.2.txt`（commonmark spec が固定）、commonmark の設計
- **Findings**:
  - 段落の各行の先頭と末尾のスペース・タブは除去される（ASCII のみ）。行末の 2 つ以上のスペースは hard break
  - left-flanking: 直後が空白でなく、かつ（直後が句読点でない、または直前が空白か句読点）。right-flanking は対称。`*` は left で開き right で閉じ、`_` は加えて単語内を禁じる。両用の並びが関わる対は、長さの和が 3 の倍数なら（両方が 3 の倍数でない限り）対にならない
  - 句読点は ASCII 句読点と Unicode の一般カテゴリ P・S、空白は Zs とタブ・LF・FF・CR。行頭と行末は空白として扱う
  - 文字参照は text、リンク先、title、info string で復号される（コードスパン・コードブロック・HTML では復号されない）。`&#0;` は U+FFFD になる
  - 箇条書き記号や番号の区切り文字が変わると新しいリストになる。空行を挟んでも同じ記号なら同じリスト（loose）になる
  - 空の項目と開始番号 1 以外の番号付きの項目は段落を中断できない。`=`/`-` だけの行は段落の後で Setext の下線になる。`- ---` は行全体が区切り線になる
  - 項目は空行の継続行で最初の空白でない位置まで読み飛ばすため、項目内のコードブロックの「空白だけの行」は保てない
- **Implications**: 行頭行末の空白は最初と最後の 1 文字だけを `&#32;` / `&#9;` にすれば除去を防げる（残りは内側になる）。flanking の判定はこの定義を ASCII 表と Unicode 表で行う

### 上流設計との整合
- **Sources Consulted**: `.kiro/specs/html-renderer/design.md`（Cross-Spec Contract）、`.kiro/specs/node-tree/design.md`、`.kiro/specs/commonmark/design.md`、`.kiro/specs/parser-core/design.md`、`.kiro/specs/foundation/design.md`
- **Findings**:
  - html-renderer は `amark_markdown_*` の型・関数・コンテキスト操作の名前と規則を指定し、フック表と走査駆動部を複製せずに使うことを求める。これは現在 node-tree の非公開モジュール `src/tree/node_walk.h`（表 `amark_node_walk_table_*`、走査 `amark_node_walk_run`、`amark_node_walk_skip_children`。葉の判定は `amark_node_kind_is_leaf`）が提供する。Markdown 固有の処理を駆動部に入れないことも求める
  - node-tree の `amark_node_iter_reset` は「次の next はその続きを報告する」。当初の駆動部の手順 3 は EXIT の訪問を行う記述がなかったが、横断レビューの結果修正され、リセットの直後に同じノードの EXIT を訪問する（html-renderer 要件 11.3。その後、駆動部は同じ機能のまま node-tree へ移り node-tree 要件 14.7 となった）
  - node-tree はリストの区切り文字・箇条書き記号・フェンスの別・見出しの形式を保持しない（Non-Goals）。レンダラは常に正規化した記法を選ぶ
  - parser-core は解析後に `amark_node_normalize` を呼ぶため、隣接する text は結合されている。文字参照で書いた文字も復号後に結合される
  - Unicode の句読点・空白の分類は、commonmark が最初から非公開モジュール `src/base/unicode_class.{h,c}`（生成物 `src/base/unicode_class_data.c`、`amark_unicode_is_punctuation` / `amark_unicode_is_whitespace`）として作る。レンダラはパーサの非公開ヘッダに依存せずに同じ分類を使える
  - foundation は後方復号 `amark_utf8_decode_previous` を公開する。出力の最後の文字の復号にはこれを使う
  - エラーコードは foundation の `AMARK_ERROR_LIST` と設計のエラーコード一覧表が正本で、`AMARK_ERROR_INVALID_STATE` を含む。本仕様は新しいコードを追加せず、意味の合う既存のコードを使う
- **Implications**: 型・関数の形は html-renderer に合わせる。NodeWalk は変更せず（省略後の EXIT の訪問は node-tree の規則）、フレームと行の管理は Markdown 側に置く。Unicode 分類は commonmark の `unicode_class` をそのまま使う。新しいエラーコードは作らず、プレフィックスの不正な操作は `AMARK_ERROR_INVALID_STATE` を再利用する

## Architecture Pattern Evaluation

| Option | Description | Strengths | Risks / Limitations | Notes |
|--------|-------------|-----------|---------------------|-------|
| NodeWalk の走査＋Markdown の訪問関数でフレーム管理（採用） | 駆動部は共有し、訪問関数が ENTER / EXIT ごとにノードフレームを積み降ろす | フックの有無にかかわらずプレフィックスとフレームが釣り合う。NodeWalk を Markdown 非依存に保てる | 省略後の EXIT の訪問が駆動部に必要（node-tree の NodeWalk 手順 3 が提供） | node-tree の約束どおり |
| NodeWalk にプレフィックス・行ごとのフックを追加 | 駆動部が行の管理を持つ | 他形式（man など）にも使える | Markdown 固有の処理が共通部に入る（node-tree の Out of Boundary に反する）。共有ライターの切り出しは第 2 の利用者が現れるまで見送る | 不採用 |
| 出力を一度中間表現（行のリスト）にしてから整形 | 2 パス | 折り返しが単純 | 確保と処理が倍、フックの書き込みの意味が複雑 | 不採用 |
| 原文の記法をノードに記憶 | ロスレスに近い | 出力が原文に近い | node-tree の Non-Goals、brief の Out of Boundary | 不採用 |

## Design Decisions

### Decision: 区切り文字の計画（flanking 計画器）
- **Context**: 要件 6。強調は隣接・入れ子・単語内で、記号の選び方によって再パースの構造が変わる
- **Alternatives Considered**:
  1. cmark 方式（入れ子の唯一の子だけ `_`）— 隣接・単語内・句読点の組み合わせで壊れる
  2. 常に数値文字参照で区切りを守る — 正しいが日本語の文がほぼ全文参照だらけになる
  3. 並びの前後の文字の分類（空白 W・句読点 P・その他 O）から、記号ごとに開き・閉じの可否を計算し、必要なときだけ記号の切り替えと隣接文字の数値文字参照を使う（採用）
- **Selected Approach**: 開きは ENTER で直前の出力文字（または行頭）と内容の最初の出力文字の分類を見て、優先記号・もう一方の記号の順に、置換なし→内側の置換→外側の置換→両方の順で「開ける、かつ（閉じられない、または同じ記号・同じ長さの開いている祖先がない）」組み合わせを選ぶ。閉じは EXIT で同じ記号のまま「閉じられる」ように置換だけを選ぶ。外側の直前の文字は、それが組み込みの text が書いた最後の文字であれば出力の末尾を書き換えて置換する
- **Rationale**: 開き専用・閉じ専用の並びは対応が構造的に保証され、両用でも同記号同長の祖先がなければ安全。置換が必要なのは単語内の入れ子など少数の形に限られ、日本語の `これは*強調*です` は置換なしで出力される
- **Trade-offs**: 単語内で入れ子の強調（`foo***bar***baz`）は `fo&#111;*__bar__*&#98;az` のような参照を含む。読みやすさより正しさを優先した
- **Follow-up**: 往復テストの強調の節で除外 0 件を確認する。満たせない形（偶数個の隣接する強調が親の強調の子をすべて占めるなど）は設計の「表現できない木」に記した

### Decision: 隣接するリストは記号を交互にする
- **Context**: 要件 3.6、3.7。cmark の `<!-- end list -->` は HTML ブロックを増やし HTML が一致しない
- **Selected Approach**: 直前の兄弟が同種のリストなら、箇条書きは設定の記号と異なる記号（`-` → `*` → `+` の順で最初の異なるもの）、番号付きは区切り文字 `.` と `)` を入れ替える。直前の兄弟の記号は親フレームに記録し、部分木の起点だけ木をさかのぼって求める
- **Rationale**: 記号の変化は CommonMark で新しいリストを始める正規の手段で、木に新しいノードを足さない

### Decision: インデントのコードブロックを出さない
- **Context**: 要件 2.10。インデントのコードは段落の遅延継続、リストの直後、項目の内容の桁で解釈が変わる
- **Selected Approach**: 常にフェンスで出力する（cmark も info が空でも内容が空白で始まるならフェンスにする）
- **Rationale**: 内容の先頭の空白・空行を含め常に同じ値になる。リストの直後の区切りコメントも不要になる

### Decision: 行端・制御文字・リンク先と title の改行は数値文字参照
- **Context**: 要件 5.3、5.4、5.9、9.6
- **Selected Approach**: 行の最初・最後の空白 1 文字、text の制御文字（タブを除く）、title と info の改行、リンク先の制御文字は `&#N;` で書く。空行を生む soft break（連続、段落・見出しの先頭と末尾、強調の区切りの隣）も `&#10;` にする
- **Rationale**: 文字参照は復号されて同じ文字の text になり、html-renderer は soft break も text の `\n` も同じ `\n` として出すため HTML が一致する

### Decision: 折り返しは「行頭で安全な文字」の前の単一の空白だけ
- **Context**: 要件 8。cmark は数字の前だけを避けるため `-` や `#` が行頭に来て構造が変わる
- **Selected Approach**: 折り返し候補は text の中の、前後が空白でない単一の空白で、直後の文字が行頭に置かれてもエスケープ不要の構文を始めない（`-+=#>~` と数字以外）もの。soft break は直後の内容が安全なら候補、そうでなければ改行のまま。候補で改行すると空白が soft break に変わるため、比較は soft break を空白として出す HTML で行う
- **Trade-offs**: 木は空白と soft break の違いだけ変わる（要件 9.2 でこの等価性を定義した）

### Decision: Unicode 分類は commonmark の共有モジュールを使う
- **Context**: 要件 6.5。日本語の文の強調は置換なしで出力したいが、非 ASCII の句読点（`「` `。`）の隣では分類を誤ると開きにならない
- **Alternatives Considered**:
  1. 非 ASCII を常に参照にする — 日本語が読めなくなる
  2. markdown-renderer が commonmark_internal.h を include — レンダラがパーサに依存する（structure.md 違反）
  3. commonmark が最初から作る非公開モジュール `src/base/unicode_class.{h,c}`（生成物 `src/base/unicode_class_data.c`）を include して使う（採用）
- **Rationale**: パーサとレンダラが同一の分類を使い、表の重複がない。本仕様が commonmark のファイルを移設・変更する必要がない（横断レビューで決定）
- **Follow-up**: commonmark の分類の表の版が変わったら本仕様の往復テストを再実行する（Revalidation Trigger）

### Decision: プレフィックスはノード単位で自動的に戻す
- **Context**: 要件 11.6、11.7。フックが ENTER だけ組み込みに委譲して EXIT を自前で書くと、明示の push / pop が釣り合わない
- **Selected Approach**: 駆動側のフレームに ENTER 時のプレフィックスの深さを記録し、EXIT（葉では呼び出しの後）でその深さへ戻す。そのノードで積んでいないものを pop すると `AMARK_ERROR_INVALID_STATE`
- **Rationale**: 新しいエラーコードを作らず、「現在の状態ではその操作ができない」という既存の意味に合う

### Decision: 往復テストの比較方法
- **Context**: 要件 9、14
- **Selected Approach**: 例ごとに `amark_commonmark_parse` → 本レンダラ → 再パース → 両方の木を html-renderer（`allow_raw_html`・`allow_dangerous_urls` を真、折り返しの設定では soft break を SPACE）で HTML にしてバイト比較し、さらに再レンダリングの冪等性を比べる。設定は 4 組（既定、別記法 A、別記法 B、折り返し幅 20）
- **Rationale**: 木の同一性ではなく HTML の一致を基準にすることで、正規化（文字参照で書いた文字、空白と soft break）を許しつつ意味の一致を検証できる

## Risks & Mitigations
- 強調の計画が満たせない形がパース由来の木にも現れる — 往復テストの強調の節で検出し、除外一覧に載せる前に計画の規則を見直す（除外は設計への記載が条件）
- `node_walk.h` の内部契約が変わり、フレームの釣り合いが崩れる — node-tree の Revalidation Trigger により本仕様を再検証する。`markdown_hook_test.c` で子の省略の後の EXIT を確認する
- 壁時計の時間による線形性の検査が環境で揺れる — 訪問回数・出力のバイト数・確保回数の決定的な計数で検査する
- 出力の書き換え（直前の文字の置換、折り返しの挿入）がフックの出力を壊す — 書き換えは組み込みの text が書いた最後の文字と、組み込みの text の折り返し候補に限る

## References
- [cmark src/commonmark.c](https://github.com/commonmark/cmark/blob/master/src/commonmark.c) — 正規化出力とエスケープの基準
- [CommonMark Spec 0.31.2](https://spec.commonmark.org/0.31.2/) — flanking、段落、リスト、文字参照の規則
- `.kiro/specs/html-renderer/design.md` — Cross-Spec Contract
- `.kiro/specs/node-tree/design.md` — NodeWalk（フック表と走査駆動部）
- `.kiro/specs/commonmark/design.md` — 簡易パース、spec の例の抽出器、CharClass
