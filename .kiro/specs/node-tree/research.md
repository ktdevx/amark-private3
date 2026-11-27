# Research & Design Decisions

## Summary
- **Feature**: `node-tree`
- **Discovery Scope**: New Feature（グリーンフィールド。上流は承認済みの foundation 設計のみで、ソースコードは未作成）
- **Key Findings**:
  - cmark の木（親・兄弟・子のリンクを持つ opaque なノード、値はノードが所有、enter / exit 型イテレータ、テキストの結合）はそのまま採用できる。ただし cmark-gfm の拡張ノード種別はプロセス全体のカウンタで識別値を払い出しており、foundation の「可変のグローバル状態はアロケータだけ」という約束に反する
  - 拡張ノード種別を「レジストリオブジェクトが所有する種別オブジェクト」とし、種別の同一性をポインタの一致で表すと、グローバル状態なしに parser-core（拡張単位でまとめて登録）とレンダラ（種別ごとのフック）の両方が安定したキーを得られる
  - 深い木（信頼できない入力から 10 万段など）でスタックあふれを起こさないため、破棄・走査・正規化はすべて反復で書く必要がある。挿入時の循環検査は「子を持たないノードは祖先になり得ない」ことを使えば、上から下へ木を組むときに定数時間になる

## Research Log

### cmark のノード API とイテレータ
- **Context**: brief が「cmark 方式」を指定しているため、利用者に馴染みのある形と既知の落とし穴を確認する
- **Sources Consulted**: cmark の `cmark.h`（`cmark_node_new`、`cmark_node_free`、`cmark_node_append_child`、`cmark_node_insert_before`、`cmark_node_replace`、`cmark_iter_*`、`cmark_consolidate_text_nodes`）、`node.c` の `S_can_contain`、cmark-gfm の `cmark_syntax_extension_add_node`
- **Findings**:
  - イテレータは `NONE / DONE / ENTER / EXIT` の 4 状態で、葉ノードは ENTER のみ報告する。次の位置は `next()` の呼び出し時に現在ノードのリンクから遅延計算される
  - cmark の文書は「EXIT の後、または葉の ENTER の後にだけ現在ノードを変更してよい」と述べるが、現在ノードを取り外した場合の挙動は未定義で、利用者が誤用しやすい
  - `cmark_node_free` は取り外してから子孫ごと再帰的に解放する。cmark は再帰を避ける実装に改修されているが、API 上の保証は書かれていない
  - `S_can_contain` は document を子にできない、list は item だけ、など本仕様の標準規則とほぼ一致する。リンクの入れ子は木の規則では禁止していない（パーサが守る）
  - cmark-gfm は拡張ノード種別の識別値をグローバルなカウンタで払い出し、拡張を登録する順序に依存する
- **Implications**: 標準種別の規則・イベントモデルは cmark に揃える。拡張種別はグローバルカウンタを使わずレジストリに持たせる。走査中の編集の安全性を「現在ノードとその祖先に触れなければ安全」という一文の規則で定義し、現在ノードを取り外した場合の挙動も定義する（早期終了、位置の再設定で再開）

### foundation の約束事との整合
- **Context**: 上流の公開 API を再定義せず、その上に積む必要がある
- **Sources Consulted**: `.kiro/specs/foundation/design.md`（承認済み）
- **Findings**:
  - 失敗しうる関数は `amark_error_t` を返し、結果は出力引数で返す。必須ポインタが `NULL` なら `AMARK_ERROR_INVALID_ARGUMENT`。失敗時は呼び出し前の状態を保つ（強い保証）
  - 文字列は借用が `amark_slice_t`、所有が NUL 終端付きの長さ付き文字列。UTF-8 の検査は入力を受け取るモジュールが `amark_utf8_validate` で行う
  - エラー列挙は末尾追加のみ。追加時は `error.h`、`src/base/error.c`、`tests/error_test.c` を同時に更新する
  - 可変のグローバル状態はアロケータだけ。テストはモジュールごとの実行ファイルで、公開 API だけを使い、失敗注入は `tests/support/` の差し替えアロケータで行う
  - steering の `tech.md` の例 `amark_node_t *amark_node_create(amark_node_type_t type)` は、foundation 確定後の規約（エラー戻り値＋出力引数）と食い違っている
- **Implications**: `amark_node_create(type, &node)` の形を採り、tech.md の例は実装時に更新する。新しいエラーコードは 2 つだけ foundation の列挙へ追加する。値の設定時に UTF-8 を検査する

### 下流 spec の要求
- **Context**: parser-core / html-renderer / markdown-renderer が本仕様に何を期待するかを確認する
- **Sources Consulted**: `.kiro/specs/parser-core/brief.md`、`html-renderer/brief.md`、`markdown-renderer/brief.md`、`.kiro/steering/roadmap.md`
- **Findings**:
  - parser-core は「構文＋ノード種別」を 1 つの拡張として登録する。方言は拡張の集合
  - 両レンダラはノード種別ごとの出力を表引きで差し替え・追加する。GFM 拡張は「構文＋ノード種別＋ HTML 出力＋ Markdown 出力」を 1 単位で提供したい
  - markdown-renderer は原文の記法を保持しない（フェンスの文字、強調記号の種類など）。木は正規化した値だけを持てばよい
  - html-renderer は画像の alt テキストを子の平坦化で作るため、部分木の走査と「子を飛ばす」操作が必要
- **Implications**: レンダラのフックは種別ハンドル（`const amark_node_kind_t *`）をキーにすればよい。標準種別にもハンドルを与え、標準・拡張を同じキーで扱えるようにする。イテレータに位置の再設定（`reset`）を設ける。記法の記憶（フェンス長、区切り記号など）は持たない

## Architecture Pattern Evaluation

| Option | Description | Strengths | Risks / Limitations | Notes |
|--------|-------------|-----------|---------------------|-------|
| グローバルな種別表（cmark-gfm 方式） | 拡張種別の識別値をプロセス全体のカウンタで払い出す | 識別値が整数で表引きしやすい | グローバル可変状態。登録順で値が変わる。複数の方言を同時に使うと衝突しやすい | foundation の約束に反するため不採用 |
| レジストリオブジェクト＋種別ハンドル | 利用者（parser-core の方言など）が生成するレジストリに種別を登録し、種別オブジェクトのポインタを識別子とする | グローバル状態なし。名前の一意性をレジストリ単位で保証。ハンドルが安定 | レジストリはその種別のノードより長く生きる必要がある（利用者の義務） | 採用 |
| 自立した種別オブジェクト（レジストリなし） | 種別を個別に生成・破棄する | 最小 | 名前の一意性と名前による検索ができない。方言が自前で集合を持つ必要がある | parser-core に重複実装を強いるため不採用 |
| 種別ごとの参照カウント | ノードが種別（レジストリ）を参照カウントで保持する | 寿命の誤用が起きない | C99 に原子操作がなく、複数スレッドで同じレジストリを使うと計数が競合する | 不採用。寿命は利用者の義務として文書化する |

## Design Decisions

### Decision: 種別の表現は「識別値の列挙＋種別ハンドル」の二本立て
- **Context**: 2.3〜2.6、4.7。標準種別は `switch` で速く分岐したい。拡張種別は実行時に増える
- **Alternatives Considered**:
  1. 列挙だけ（拡張は整数を払い出す）
  2. ハンドルだけ（標準もハンドルで比較）
- **Selected Approach**: `amark_node_type_t` は標準種別の固定値と、拡張種別を表す共通値 `AMARK_NODE_EXTENSION` を持つ。すべての種別（標準を含む）は `const amark_node_kind_t *` のハンドルを持ち、標準種別のハンドルはライブラリ内の定数データ（可変状態ではない）として常に同一
- **Rationale**: レンダラは標準種別を列挙で分岐し、拡張種別や上書きはハンドルで引ける。グローバル可変状態を作らない
- **Trade-offs**: API が 2 つの表現を持つ。変換関数（`amark_node_kind_get_standard`、`amark_node_kind_get_type`）で補う
- **Follow-up**: レンダラ spec でフックのキーをハンドルに統一することを確認する

### Decision: 子の受け入れ規則は内容モデル＋独自判定＋明示親限定フラグ
- **Context**: 6.1〜6.5。GFM の table は table_row だけ、table_row は table の下だけ、という規則を表せる必要がある
- **Alternatives Considered**:
  1. 子に取れる種別の一覧をハンドル配列で渡す（自己参照や後から登録する種別を表せない）
  2. 判定関数だけ（単純な「任意のインライン」にも関数が要る）
- **Selected Approach**: 内容モデル `NONE / BLOCKS / INLINES / CUSTOM`。`CUSTOM` は判定関数を必須とする。種別は `requires_explicit_parent` を持ち、真なら `BLOCKS` / `INLINES` の親には置けない（標準の item はこれに該当）
- **Rationale**: 典型的な場合はデータだけで宣言でき、特殊な場合は関数で何でも表せる。標準の list / item も同じ仕組みで表現できる（一般化）
- **Trade-offs**: GFM のタスクリストのように「標準の list が拡張の子を受け入れる」必要が出た場合は、標準種別の規則の改訂が必要（Revalidation Trigger に記載）

### Decision: 値の取得は借用スライス、設定は複製＋UTF-8 検査
- **Context**: 3.2、3.3、3.6、3.9、foundation の所有権規約
- **Selected Approach**: getter は `amark_slice_t` を返す。値を持つ種別では `data` は常に非 `NULL` で `data[length] == '\0'`（未設定時は静的な `""`）。値を持たない種別では `{NULL, 0}`。setter は `amark_utf8_validate` → 複製 → 旧値の解放、の順で強い保証を守る
- **Rationale**: 利用者は C 文字列としても長さ付きでも使える。値の有無を `data` の `NULL` で区別できる
- **Trade-offs**: 設定ごとに O(n) の UTF-8 検査がかかる。木の値が常に妥当な UTF-8 であることをレンダラが前提にできる利点を優先する

### Decision: リストの開始番号を 0〜999999999 に制限する
- **Context**: 3.5。木は構文非依存であるべきだが、markdown-renderer は木を常に有効な CommonMark として出力する必要がある
- **Selected Approach**: `AMARK_NODE_LIST_START_MAX = 999999999`。それを超える値は `AMARK_ERROR_INVALID_ARGUMENT`
- **Rationale**: API で組んだ木が必ず再パース可能な Markdown に出力できる。値の型は `uint32_t`
- **Trade-offs**: 構文の制約が木に入る。範囲は定数として公開し、将来の方言で必要になれば緩める

### Decision: 走査中の編集の安全性は「現在ノードとその祖先に触れない」で定義する
- **Context**: 10.1〜10.5、brief の「どの操作が安全か明確に定義する」
- **Selected Approach**: イテレータは次の位置を `next()` 呼び出し時に現在ノードのリンクから遅延計算する。したがって、現在ノードとその祖先（起点まで）の位置を変えない編集はすべて安全。現在ノードを取り外した場合、次の `next()` は（親がないため）終了を報告する。破棄した場合は未定義。位置の再設定 `reset` で任意の部分木内の位置から再開できる
- **Rationale**: 追加の状態や確保なしに保証でき、規則が一文で言える
- **Trade-offs**: 「現在ノードを削除して続ける」には利用者が次の位置を先に取っておき `reset` する必要がある。パターンを公開ヘッダのコメントに示す

### Decision: 挿入は移動として働き、循環検査は子を持つノードに限る
- **Context**: 6.8、7.3、13.2
- **Selected Approach**: 挿入するノードが既に親を持つ場合、検証成功後に取り外してから挿入する。循環検査は「挿入するノードが子を持つ」場合だけ、新しい親から根まで祖先をたどって行う（子を持たないノードは自分以外の祖先になり得ない）
- **Rationale**: 上から下へ木を組む（新しい葉を最深部へ追加する）操作が定数時間になり、深さ 10 万段の木を線形時間で組める
- **Trade-offs**: 部分木の移動は挿入先の深さに比例する

### Decision: 新しいエラーコードは 2 つだけ
- **Context**: 12.1。foundation の列挙へ追加する
- **Alternatives Considered**: 種別の不一致（値を持たない種別へのアクセス）、循環、親なし兄弟挿入にそれぞれコードを設ける
- **Selected Approach**: `AMARK_ERROR_INVALID_CHILD`（親子関係の規則違反・循環・document を子にする）と `AMARK_ERROR_ALREADY_EXISTS`（名前の重複）。それ以外は既存の `AMARK_ERROR_INVALID_ARGUMENT` に含める
- **Rationale**: 利用者の分岐に意味がある区別だけをコードにする。`ALREADY_EXISTS` は parser-core の拡張名の重複にも再利用できる汎用名にする

### Decision: 葉の判定 `amark_node_kind_is_leaf` を公開する
- **Context**: 横断レビュー。レンダラの走査駆動部（当時は html-renderer 所有。現在は本仕様の NodeWalk）は、イテレータが ENTER だけを報告する種別（葉）を判定する必要があり、内容モデルから独自に導くと判定がずれるおそれがある
- **Selected Approach**: 当初は内部関数だった `amark_node_kind_is_leaf` を `include/amark/node_kind.h` の公開 API にする（要件 2.7）。定義は `content == AMARK_NODE_CONTENT_NONE`、`NULL` は偽
- **Rationale**: イテレータとレンダラが同じ関数で葉を判定し、ENTER / EXIT の扱いが常に一致する

### Decision: レンダラ共有の振り分け走査を node-tree の非公開モジュール `node_walk` にする
- **Context**: 横断レビュー（2026-11-27）。種別ハンドルをキーとするフック表と反復の走査駆動部は、当初 html-renderer が非公開の共通層として所有し、markdown-renderer がその非公開ファイルに依存していた
- **Alternatives Considered**: 1. html-renderer の所有のまま / 2. node-tree の非公開モジュールへ移す / 3. 公開 API にする、または共有の文字列ライターも切り出す
- **Selected Approach**: 2。`src/tree/node_walk.{h,c}`（要件 14）に、従来と同じ機能（表の設定・置き換え・解除・検索と、子の省略と省略後の EXIT の訪問を含む反復の駆動部）だけを名前の接頭辞を変えて移す。公開せず、レンダラ固有の処理を持たない。Markdown の行の書き出し（プレフィックスのスタック、折り返し、改行の保証）は markdown-renderer に残す
- **Rationale**: 現に 2 つの利用者（HTML と Markdown のレンダラ）がある。子の省略後の EXIT の訪問はイテレータ（`amark_node_iter_reset` が続きから報告する）の意味に関わる契約で、まさにその継ぎ目で不具合（省略したノードの EXIT が訪問されない）が見つかった。移しても機能は増えず、実装前の今が最も安い
- **Not the rationale**: 将来の GFM や第三者のレンダラのための汎用化は理由にしない。公開や共有ライターの切り出しは、実際に第 2 の利用者（レンダラ以外、または別の書き出し形式）が現れるまで見送る
- **Trade-offs**: node-tree にレンダラ向けの内部モジュールが入る。契約の変更は html-renderer と markdown-renderer の再検証を要する（Revalidation Trigger）

### Decision: `node_walk` はテスト実行ファイルにソースを直接コンパイルして検証する
- **Context**: foundation の TestHarness はテストが公開 API だけを使うことを原則とする。NodeWalk は非公開で、node-tree の段階では公開 API から到達できない（html-renderer は後に実装され、`tests/support/test_tree` もそのとき作られる）
- **Alternatives Considered**: 1. html-renderer の公開 API を通してだけ検証する / 2. テスト用の export やフックを本体に置く / 3. `src/tree/node_walk.c` をテスト実行ファイルに直接コンパイルする
- **Selected Approach**: 3。`amark_node_walk_test` のソースに `src/tree/node_walk.c` を加え、`src/` を include パスに足す。木はテスト内で公開 API だけで組む。1 は node-tree の完了時点で検証できず、2 は foundation 要件 3.4（テスト専用コードを本体に置かない）に反する
- **Rationale**: `node_walk.c` は公開 API だけを呼ぶため、静的・動的のどちらの構成でもリンクでき、本体には何も足さない。例外はこの 1 実行ファイルに限り、レンダラの公開 API を通した確認（`html_hook_test.c`、`markdown_hook_test.c`）も残す

### Decision: 正規化は基本保証
- **Context**: 11.5
- **Selected Approach**: 隣接 text の並びごとに、空 text を取り除き（確保なし）、残りを 1 回の確保で連結する。確保に失敗した並びは変更しない。それまでに処理した並びは正規化済みのまま残す
- **Rationale**: 部分木全体の強い保証には全体の事前確保が必要で、複雑さに見合わない。意味（連結内容）は常に保たれる

## Synthesis Outcomes
- **Generalization**: 標準種別も拡張種別も同じ種別オブジェクトと同じ受け入れ判定で表す（標準種別はライブラリ内の定数テーブル）。list / item の特別規則も `CUSTOM` と `requires_explicit_parent` で表現し、特別扱いは document の「子にできない」だけにする
- **Build vs. Adopt**: C 標準ライブラリ以外に依存しないという steering の制約により、木のライブラリは採用せず自作する。設計は cmark の API を参考にし、利用者に馴染みのある形にする
- **Simplification**: イテレータは呼び出し側が領域を持つ構造体（確保なし、`_deinit` なし）。木の複製・比較・ソース位置・記法の記憶・ノードごとの利用者データは要求にないため持たない。レジストリの検索は線形探索（種別数は数十以下を想定）

## Risks & Mitigations
- レジストリをノードより先に破棄される誤用 — 公開ヘッダに義務を明記する。parser-core の設計で、拡張ごとに 1 つのレジストリを拡張が所有し（parser-core の API には出さない）、拡張（またはそれを所有する方言）がその種別のノードと、そのフックを保持するレンダラより長く生きる義務を利用者に約束する（横断レビューで node-tree の記述を parser-core の決定に合わせた）
- GFM のタスクリストなど、標準種別に拡張データを付けたい要求 — 現設計では付随データは拡張種別だけ。gfm フェーズで標準種別の規則か付随データの対象を改訂する（Revalidation Trigger）
- Naos の `naos_add_executable` の扱い — foundation の規約どおり 1 ソース 1 実行ファイルとし、モジュール（`src/` の 1 ファイル）ごとにテスト実行ファイルを分ける
- 判定関数・後片付け関数が木を変更する誤用 — 公開ヘッダで禁止を明記する（コールバック中に木を変更しない）

## References
- [cmark](https://github.com/commonmark/cmark) — `cmark.h` のノード・イテレータ API、`node.c` の `S_can_contain`
- [cmark-gfm](https://github.com/github/cmark-gfm) — 拡張ノード種別の登録（グローバルなカウンタ方式。不採用の根拠）
- [CommonMark Spec 0.31.2](https://spec.commonmark.org/0.31.2/) — 標準ノード種別の範囲、番号付きリストの開始番号は 9 桁まで
- `.kiro/specs/foundation/design.md` — エラー列挙、アロケータ、スライス、UTF-8、テスト基盤の正本
