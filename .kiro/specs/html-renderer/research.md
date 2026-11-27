# Research & Design Decisions: html-renderer

## Summary
- **Feature**: `html-renderer`
- **Discovery Scope**: New Feature（greenfield。上流 foundation / node-tree は設計承認済み・未実装）
- **Key Findings**:
  - CommonMark 仕様の期待 HTML は cmark の `html.c` の出力と一致する。cmark 自身の仕様テストは HTML 出力時に必ず `CMARK_OPT_UNSAFE` を付けて実行している（`test/cmark.py`）。したがって「安全側を既定にする」ことと「仕様の期待出力と一致する」ことは、適合テストで unsafe 相当の設定を明示すれば両立する
  - cmark の属性・テキストのエスケープは `& < > "` の 4 文字のみ（`'` と `/` は secure モードでだけ実体参照にする）。URL の符号化は `houdini_escape_href` の安全文字表に従い、`&` → `&amp;`、`'` → `&#x27;`、その他の非安全バイト → `%XX`（大文字 16 進）、既存の `%` は再符号化しない
  - 画像の alt は「平坦化モード」で子孫を走査し、text・code・html_inline の内容をエスケープ、soft / line break を空白、それ以外（強調、リンク、入れ子の画像）のタグは出さない。node-tree の設計は、画像を起点にした別イテレータで平坦化し外側は `reset(image, EXIT)` で子を飛ばす方法を想定している

## Research Log

### cmark の HTML 出力規則（html.c）
- **Context**: 要件 2・3・4・5 の出力をバイト単位で一致させるため
- **Sources Consulted**: commonmark/cmark `src/html.c`（master）
- **Findings**:
  - `cr()`: 出力が空でなく末尾が `\n` でなければ `\n` を足す。block quote / list / item / heading / code block / html block / thematic break / 段落（tight でない）の開始前に呼ぶ。block quote の終了前、html block の後にも呼ぶ
  - list: 箇条書き `<ul>\n`、番号付き `start == 1` で `<ol>\n`、それ以外 `<ol start="N">\n`。終了は `</ul>\n` / `</ol>\n`（cr なし）
  - item: `<li>`（改行なし）、`</li>\n`（cr なし）
  - heading: `<hN>` / `</hN>\n`。thematic break: `<hr />\n`
  - code block: info が空なら `<pre><code>`。空でなければ最初の `cmark_isspace`（空白、`\t`、`\n`、`\v`、`\f`、`\r`）までを語とし `<pre><code class="language-語">`。info 全体が `language-` で始まる場合は接頭辞を付けない。内容はエスケープし `</code></pre>\n`
  - html block: cr、unsafe なら内容そのまま、そうでなければ `<!-- raw HTML omitted -->`、cr
  - paragraph: 祖父母が LIST で tight なら開始・終了とも何も出さない
  - softbreak: HARDBREAKS → `<br />\n`、NOBREAKS → ` `、既定 → `\n`。linebreak → `<br />\n`
  - link: `<a href="URL"`、title が空でなければ ` title="..."`、`>`、終了 `</a>`。危険な URL は（unsafe でない限り）href を空にする
  - image: `<img src="URL" alt="平坦化" title="..." />`（title は空でなければ）
  - custom block / inline（cmark の拡張ノード）は on_enter / on_exit 文字列を出す。文字列がなければ何も出さず子は通常どおり出る
- **Implications**: 組み込み出力の表をこの規則どおりに定める。`cr` を「このレンダリングで出力した範囲」に限定することで、利用者の既存バッファの内容に左右されないようにする（要件 1.7。cmark はバッファ全体を見るが、cmark は常に空バッファに出すため差はない）

### エスケープと URL 符号化（houdini）
- **Sources Consulted**: cmark `src/houdini_html_e.c`、`src/houdini_href_e.c`
- **Findings**:
  - HTML: `"`→`&quot;`、`&`→`&amp;`、`<`→`&lt;`、`>`→`&gt;`。`'` と `/` は非 secure では素通し（cmark の HTML 出力は非 secure）
  - href 安全文字: 英数字と `! # $ % ( ) * + , - . / : ; = ? @ _ ~`。非安全: 空白、`" & ' < > [ \ ] ^` バッククォート `{ | }`、制御文字、DEL、0x80 以上。`&`→`&amp;`、`'`→`&#x27;`、その他 → `%XX`
- **Implications**: 要件 6.1〜6.3 の規則を表として設計に固定する。0x80 以上のバイトはバイトごとに `%XX`（例: `ä` → `%C3%A4`、仕様の例と一致）

### 危険な URL（scanners.re）
- **Findings**: `_scan_dangerous_url` は大文字小文字を区別しない接頭辞一致で `javascript:`、`vbscript:`、`file:`、`data:` を危険とし、`data:image/` に続く `png|gif|jpeg|webp` は安全とする。先頭の空白の除去は行わない
- **Implications**: cmark と同じ判定にする。先頭が空白・制御文字の URL（手組みの木で起こりうる）は危険と判定されないが、URL 符号化で `%20` / `%09` になるためブラウザはスキームとして解釈しない。追加の正規化は行わない（cmark との出力差を作らない）

### 仕様テストの実行設定
- **Sources Consulted**: cmark `test/cmark.py`
- **Findings**: プログラム経由では `--unsafe`、ライブラリ経由では `1 << 17`（`CMARK_OPT_UNSAFE`）を常に付ける
- **Implications**: 既定を安全側にしても、commonmark の適合テストは「生 HTML と危険な URL を許可した設定」を明示すれば期待出力と一致する。要件 9.1 / 9.2 をこの形で定義した

## Architecture Pattern Evaluation

| Option | Description | Strengths | Risks / Limitations | Notes |
|--------|-------------|-----------|---------------------|-------|
| 反復走査＋種別ごとの表引き（採用） | node-tree イテレータの ENTER / EXIT ごとに、フック表を引き、なければ組み込みの `switch` | 再帰なし、cmark と同じ構造、フックの差し込み点が 1 か所 | フック数に比例する探索（少数前提） | steering「レンダラは表引き」 |
| 再帰下降 | ノードごとに子を再帰的に描画 | 実装が直感的 | 深い木でスタックあふれ（要件 13.1 違反） | 不採用 |
| 汎用コンテキスト 1 型を両レンダラで共有 | `amark_render_context_t` とフック型 1 つを HTML / Markdown で共用 | 拡張が 1 つの関数型で両方を書ける | HTML 用フックを Markdown に渡す誤りをコンパイル時に検出できない。Markdown 固有の操作（プレフィックス）を同じ型に混ぜる | 不採用。形を揃え、型は分ける |

## Design Decisions

### Decision: 既定を安全側にする
- **Context**: brief は「既定の出力を仕様の期待出力と一致させる」ことと「安全側のオプション」の両方を求める。呼び出し元は「cmark のように既定を安全にするか、決めて理由を示す」ことを求めた
- **Alternatives Considered**:
  1. 既定で生 HTML・危険な URL を出す（commonmark.js、cmark 0.28 以前）
  2. 既定で抑止する（cmark 0.29 以降、`CMARK_OPT_UNSAFE` で解除）
- **Selected Approach**: 2。`allow_raw_html` と `allow_dangerous_urls` の 2 つの真偽値（既定 偽）を独立に持つ
- **Rationale**: 信頼できない入力を前提とするライブラリでは、何も設定しない利用者が XSS を作り込まないことを優先する。cmark の現行の振る舞いと同じで、cmark 自身も仕様テストでは unsafe を明示している。書式（タグ、改行、エスケープ）は既定のままで仕様と一致し、差は 2 つの安全設定に限られる（要件 9.1、9.2）
- **Trade-offs**: commonmark の適合テストは 2 つの許可を明示する必要がある。brief の「既定の出力」の文言を「安全設定を除く既定の書式」と解釈することになるため、Cross-Spec Contract に明記する
- **Follow-up**: commonmark spec の設計で、適合テストの設定が本設計の 9.1 の形であることを確認する

### Decision: フックの形は揃え、型はレンダラごとに分ける
- **Context**: markdown-renderer は同じ形のフック機構を持つ（steering）。GFM 拡張は 1 つの拡張で HTML と Markdown の出力を提供する
- **Selected Approach**: フック関数は `amark_error_t (*)(ctx, const amark_node_t *node, amark_node_iter_event_t event, void *user_data)`。context はレンダラごとの opaque 型（`amark_html_context_t` / `amark_markdown_context_t`）。登録は `<renderer>_set_hook(renderer, kind, fn, user_data)`。内部のフック表と走査駆動部は両レンダラで共有する（node-tree の非公開モジュール `node_walk`。下の「走査駆動部を node-tree へ移す」）
- **Rationale**: 拡張の作者には同じ書き方を、型検査にはレンダラの取り違えの検出を与える。駆動部の共有で ENTER / EXIT、子の省略、エラー伝播の意味が構造的に一致する
- **Trade-offs**: 拡張は HTML 用と Markdown 用で 2 つの関数を書く（出力内容が違うため本質的に必要）

### Decision: 組み込み出力への委譲は「現在のノードとイベント」に限る
- **Selected Approach**: `amark_html_context_render_default(ctx)` は引数にノードを取らず、現在の (ノード, イベント) について組み込み出力を行う
- **Rationale**: 任意ノードへの委譲を許すと、子の省略の対象や画像の平坦化の前提が崩れる。上書きフックの典型（条件によって組み込みに任せる、前後に属性やラッパを足す）はこれで足りる

### Decision: 画像は ENTER で完結させる
- **Selected Approach**: 組み込みの image は ENTER で `<img ... />` 全体を出し、画像を起点にした内側のイテレータで子孫を平坦化し、外側の走査には子の省略を要求する。EXIT では何も出さない
- **Rationale**: 平坦化中にフックを呼ばない（要件 5.5）ことと、外側の走査の状態を汚さないことを同時に満たす。入れ子の画像も内側の 1 回の走査で処理されるため、全体はノード数に線形（要件 13.2）

### Decision: フック表は単一の線形配列
- **Alternatives Considered**: 標準種別を識別値で直接引く配列＋拡張の配列 / ハッシュ表 / 単一配列
- **Selected Approach**: 単一配列（種別ハンドルのポインタ比較）。登録が 0 件のときは探索しない
- **Rationale**: 登録は通常数件（GFM でも 10 件未満）。標準種別の値の範囲（19〜254 への追加）に依存しない。Simplification
- **Trade-offs**: 1 ノードあたりの探索がフック数 h に比例。要件 13.2 は h を定数とみなす前提で設計に明記する

### Decision: 子の省略後は駆動部が EXIT を自ら訪問する
- **Context**: node-tree の `amark_node_iter_reset(iter, node, EXIT)` は「次の `next` はその位置の続きを報告する」。リセットだけでは同じノードの EXIT が報告されず、要件 11.3（子を省略しても出るときの呼び出しを行う）と markdown-renderer のフレームの釣り合いを満たさない
- **Selected Approach**: 走査駆動部はリセットの直後に、イベントを EXIT にして同じノードの EXIT で訪問関数を呼ぶ。以後の `next` は EXIT の続きを報告する（現在は node-tree の NodeWalk の手順。node-tree 要件 14.7）
- **Rationale**: イテレータの意味を変えずに、ENTER / EXIT がちょうど 1 回ずつ訪問される。HTML の組み込みで子を省略するのは image だけで、その EXIT は何も出さないため出力は変わらない

### Decision: 走査駆動部を node-tree へ移す
- **Context**: 横断レビュー（2026-11-27）。フック表と走査駆動部は当初、本仕様が非公開の共通層として所有し、markdown-renderer が本仕様の非公開ファイルに依存していた（履歴）
- **Selected Approach**: 同じ機能のまま node-tree の非公開モジュール `src/tree/node_walk.{h,c}`（NodeWalk、node-tree 要件 14）へ移し、本仕様は使う側になる。要件 10・11 の番号と HTML 側の振る舞いは変えない
- **Rationale**: 判断と理由の正本は node-tree の `research.md`（2 つの現在の利用者、イテレータの意味に関わる契約、機能の追加なし、実装前で最も安い）
- **Trade-offs**: NodeWalk の契約の変更は node-tree の Revalidation Trigger として本仕様を再検証する

### Decision: 線形性は決定的な計数で検査する
- **Context**: 要件 13.2（ノード数と出力長に線形な時間）。壁時計の時間の比は CI の負荷・環境で揺れ、誤検出と見逃しが起こる
- **Selected Approach**: parser-core と同じく、訪問回数、出力のバイト数、計測アロケータの確保回数を n と 10n の入力で数え、期待値との一致と上限を検査する
- **Rationale**: 走査は反復で各イベントを定数回処理し、alt 平坦化も 1 回の走査であるため、計数が線形なら時間も線形になる（フック数 h は定数とみなす）

### Decision: 新しいエラーコードを追加しない
- **Selected Approach**: 引数・設定の不正は `AMARK_ERROR_INVALID_ARGUMENT`、確保は `AMARK_ERROR_NO_MEMORY`、サイズは `AMARK_ERROR_OVERFLOW`。フックのエラーはそのまま伝播
- **Rationale**: 既存コードで表現できる。foundation の規則（再利用を優先）に従う

### Generalization / Build vs Adopt / Simplification
- Generalization: 「走査＋フック表＋子の省略」は HTML / Markdown 共通の問題であり、node-tree の非公開モジュール `node_walk` として 1 つにした。エラー時の巻き戻しと公開インターフェースは各レンダラに置く
- Build vs Adopt: cmark のコードはライセンス（BSD-2）上取り込み可能だが、アロケータ・バッファ・ノード API が異なるため規則（表と振る舞い）だけを採用し、実装は自作する。外部依存は増やさない
- Simplification: ソース位置属性、`CMARK_OPT_SAFE` 以外の cmark オプション（smart、validate-utf8 など。パーサ側の責務）、平坦化中のフック呼び出し、優先順位付きの複数フックは採らない

## Risks & Mitigations
- 既定を安全側にしたことを commonmark が見落とし、生 HTML の例が失敗する — Cross-Spec Contract と要件 9.1 で設定を明記する
- フック内で木を変更して走査が壊れる — 公開ヘッダで義務として文書化（要件 11.8）。根は `const` で受け取り、フックにも `const` のノードを渡す
- 種別ハンドルの出所の破棄後にフック表がぶら下がったハンドルを持つ — 「フックを登録した種別の出所（node-tree のレジストリ、または parser-core の拡張とそれを所有する方言）はレンダラより長く生きる」ことを義務として文書化する
- 仕様の版の差（CommonMark 0.31.2 以降の期待出力の変更） — 対象版は commonmark が固定する。本設計の出力規則が変わる場合は Revalidation Trigger とする

## References
- https://github.com/commonmark/cmark/blob/master/src/html.c — HTML 出力規則の基準
- https://github.com/commonmark/cmark/blob/master/src/houdini_html_e.c — HTML エスケープ
- https://github.com/commonmark/cmark/blob/master/src/houdini_href_e.c — URL 符号化の安全文字表
- https://github.com/commonmark/cmark/blob/master/src/scanners.re — `_scan_dangerous_url`
- https://github.com/commonmark/cmark/blob/master/test/cmark.py — 仕様テストでの unsafe 指定
- https://spec.commonmark.org/0.31.2/ — CommonMark 仕様（対象版は commonmark spec が固定）
