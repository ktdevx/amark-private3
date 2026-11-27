# Requirements Document

## Project Description (Input)
パースした、あるいは API で組み立てた木を HTML として出力する手段が必要である。Markdown ライブラリの最も基本的な用途であり、CommonMark 仕様適合の検証にも HTML 出力が必須となる。現在は `foundation` と `node-tree` が基盤と木を提供する想定で、レンダラに関するコードは存在しない。

ライブラリの利用者、CommonMark 仕様適合テストを行う commonmark の実装者、Markdown レンダラ（markdown-renderer）の実装者、および拡張の作者のために、次の状態を実現する。

- 標準ノード種別からなる木を、CommonMark 仕様の期待出力と一致する HTML にレンダリングできる
- 生 HTML や危険な URL を無害化する安全側のオプションがある
- 拡張がノード種別ごとの HTML 出力を追加・差し替えできる
- パーサなしで、手組みの木だけを使ってテストできる

`node-tree` のイテレータで木を enter / exit 走査し、ノード種別ごとのレンダリング関数を表引きで呼び出す。標準ノード種別の関数は組み込みで持ち、拡張ノード種別や上書きは登録されたフックを使う。出力は `foundation` の可変長バッファに書き出す。フックの形は markdown-renderer と揃える。失敗は `foundation` の共通エラーコードで返し、確保は `amark_*` アロケータを経由する。パース、CommonMark 仕様適合テストの実行（commonmark が所有）、HTML 以外の形式への出力、GFM の tagfilter、HTML のサニタイズ全般、構文ハイライトなどの出力の加工は対象外とする。

## Introduction
本仕様は Amark の HTML レンダラを定義する。対象は、木（または部分木）から HTML 文字列へのレンダリング、18 の標準ノード種別それぞれの HTML 出力（tight / loose リスト、コードブロックの info string、画像の alt テキストの平坦化を含む）、HTML のエスケープと URL のパーセントエンコード、安全性と soft break の出力方法のオプション、ノード種別ごとのレンダラフック（拡張種別の出力の追加と標準種別の出力の上書き）である。既定の書式は CommonMark 仕様のテストスイートの期待出力に一致させる。レンダラはパーサに依存せず、API で手組みした木だけで検証できる。

## Boundary Context
- **In scope**: 任意のノードを起点とする部分木の HTML 出力、標準ノード種別すべての出力規則、テキスト・属性値のエスケープ、URL のパーセントエンコード、危険な URL スキームの判定、生 HTML と危険な URL の抑止の切り替え、soft break の出力方法の切り替え、ノード種別ごとのフックの登録・解除と呼び出し規則、フックから使える出力手段、フックの形を他のレンダラと共通にするための約束
- **Out of scope**: テキストのパース（parser-core / commonmark）、CommonMark 仕様の例の抽出と適合テストの実行（commonmark）、HTML 以外の形式（XML、LaTeX、man、Markdown）への出力、GFM の tagfilter（次フェーズ）、生 HTML の内容の検査・除去を行う一般的なサニタイズ、構文ハイライトなど出力の加工、ソース位置の属性の出力、拡張（構文＋ノード種別＋フック）をひとまとめに登録する単位（parser-core と拡張の側が結び付ける）
- **Adjacent expectations**: foundation の共通エラー列挙・グローバルアロケータ・可変長バッファ・文字列の所有権規約に従う。node-tree の標準種別、種別ハンドル、値のアクセサ、enter / exit イテレータをそのまま使い、木を変更しない。commonmark は本レンダラの「安全性の抑止を無効にした設定」で仕様の例の期待出力と一致することを前提に適合テストを行う。markdown-renderer は本仕様が定めるフックの形（登録の仕方、呼び出しのイベント、子の省略、既定出力への委譲、エラーの伝播）を同じ形で提供する

## Requirements

### Requirement 1: レンダリングの入口と出力先
**Objective:** As a ライブラリ利用者, I want 手元の木をパーサなしに HTML 文字列へ変換したい, so that API で組み立てた木やパース結果を表示や検証に使える

#### Acceptance Criteria
1. When 利用者が木の根ノードと出力先を指定してレンダリングを要求する, the HTML Renderer shall その根を起点とする部分木全体の HTML を出力先の既存の内容の後ろに追記する
2. When 利用者が document 以外のノード（段落、強調など）を起点としてレンダリングを要求する, the HTML Renderer shall そのノードとその子孫だけを、木の中の同じノードと同じ規則で出力する
3. The HTML Renderer shall パーサを必要とせず、API だけで組み立てた木を、パース結果の木と同じ規則で出力する
4. The HTML Renderer shall レンダリングの前後で木の構造と値を変更しない
5. If 根ノードまたは出力先が指定されていない, the HTML Renderer shall 何も出力せず不正な引数のエラーを返す
6. When 利用者が同じ木と同じ設定で複数回レンダリングを要求する, the HTML Renderer shall 毎回同一のバイト列を出力する
7. The HTML Renderer shall 出力の区切りの改行を、このレンダリングで出力した内容だけを基準に決め、出力先に既にあった内容によって出力を変えない

### Requirement 2: ブロック種別の出力
**Objective:** As a commonmark の実装者, I want 標準ブロック種別が CommonMark 仕様の期待出力と同じ HTML になってほしい, so that 仕様の例をバイト単位の比較で検証できる

#### Acceptance Criteria
1. When document をレンダリングする, the HTML Renderer shall document 自身のタグを出力せず、子だけを出力する
2. When block quote をレンダリングする, the HTML Renderer shall 子を `<blockquote>` 開始タグ（直後に改行）と `</blockquote>` 終了タグ（直後に改行）で囲む
3. When 箇条書きの list をレンダリングする, the HTML Renderer shall 子を `<ul>` と `</ul>` で囲み、各タグの直後に改行を出力する
4. When 番号付きの list をレンダリングする, the HTML Renderer shall 開始番号が 1 なら `<ol>`、それ以外なら開始番号を 10 進で持つ `<ol start="N">` を開始タグとし、`</ol>` で閉じ、各タグの直後に改行を出力する
5. When item をレンダリングする, the HTML Renderer shall 子を `<li>` と `</li>` で囲み、終了タグの直後に改行を出力する
6. When heading をレンダリングする, the HTML Renderer shall 見出しレベル N に対して子を `<hN>` と `</hN>` で囲み、終了タグの直後に改行を出力する
7. When thematic break をレンダリングする, the HTML Renderer shall `<hr />` と改行を出力する
8. When info string が空の code block をレンダリングする, the HTML Renderer shall エスケープした内容を `<pre><code>` と `</code></pre>` で囲み、改行を続ける
9. When info string が空でない code block をレンダリングする, the HTML Renderer shall info string の最初の空白文字より前の部分を言語名とし、`<pre><code class="language-言語名">` の形の開始タグ（言語名はエスケープし、info string が `language-` で始まる場合は接頭辞を重ねない）で内容を囲む
10. When 段落が tight でない文脈にある, the HTML Renderer shall 子を `<p>` と `</p>` で囲み、終了タグの直後に改行を出力する
11. When ブロックの開始タグを出力する時点で、このレンダリングの出力が空でなく末尾が改行でない, the HTML Renderer shall 開始タグの前に改行を 1 つ出力する
12. When 子を持たないコンテナ種別（空の段落、空の item など）をレンダリングする, the HTML Renderer shall 開始タグと終了タグを内容なしで出力する

### Requirement 3: tight / loose リスト
**Objective:** As a commonmark の実装者, I want リストの tight / loose が仕様どおりに出力に反映されてほしい, so that リストの例が期待出力と一致する

#### Acceptance Criteria
1. While 段落の親が item で、その item の親が tight と設定された list である, the HTML Renderer shall その段落の `<p>` と `</p>` を出力せず、段落の内容だけを出力する
2. While 段落の親が item で、その item の親が tight でない list である, the HTML Renderer shall その段落を `<p>` と `</p>` で囲んで出力する
3. The HTML Renderer shall tight の判定を段落の親と祖父母のノードの値だけで行い、段落以外の子（コードブロック、入れ子のリストなど）の出力を tight によって変えない
4. When 段落を起点としてレンダリングし、その段落が木の中で tight な list の item の子である, the HTML Renderer shall 木の中にある場合と同じく段落のタグを出力しない

### Requirement 4: インライン種別の出力
**Objective:** As a commonmark の実装者, I want 標準インライン種別が仕様の期待出力と同じ HTML になってほしい, so that インラインの例が期待出力と一致する

#### Acceptance Criteria
1. When text をレンダリングする, the HTML Renderer shall 内容をエスケープして出力する
2. When code span をレンダリングする, the HTML Renderer shall エスケープした内容を `<code>` と `</code>` で囲む
3. When emphasis をレンダリングする, the HTML Renderer shall 子を `<em>` と `</em>` で囲む
4. When strong をレンダリングする, the HTML Renderer shall 子を `<strong>` と `</strong>` で囲む
5. When line break をレンダリングする, the HTML Renderer shall `<br />` と改行を出力する
6. When link をレンダリングする, the HTML Renderer shall 子を、リンク先を URL として符号化した `href` 属性を持つ `<a>` 開始タグと `</a>` で囲む
7. When タイトルが空でない link をレンダリングする, the HTML Renderer shall 開始タグの `href` 属性の後に、エスケープしたタイトルを値とする `title` 属性を出力する
8. When タイトルが空の link をレンダリングする, the HTML Renderer shall `title` 属性を出力しない

### Requirement 5: 画像と alt テキスト
**Objective:** As a commonmark の実装者, I want 画像の子が平坦化されて alt 属性になってほしい, so that 画像の例が期待出力と一致する

#### Acceptance Criteria
1. When image をレンダリングする, the HTML Renderer shall リンク先を URL として符号化した `src` 属性、子から作った `alt` 属性、タイトルが空でなければ `title` 属性を、この順で持つ `<img ... />` を出力する
2. When image の子孫に text、code span、HTML inline がある, the HTML Renderer shall それらの内容を文書順に連結し、エスケープして alt 属性の値とする（HTML inline も生 HTML の設定にかかわらずエスケープする）
3. When image の子孫に soft break または line break がある, the HTML Renderer shall その位置に空白 1 つを alt 属性の値に入れる
4. The HTML Renderer shall image の子孫の強調・リンク・入れ子の画像などのタグを alt 属性に出力せず、それらの子孫の文字列だけを平坦化の対象とする
5. The HTML Renderer shall image の子孫について、登録されたフックを呼び出さない

### Requirement 6: エスケープと URL の符号化
**Objective:** As a ライブラリ利用者, I want 木の文字列が HTML として安全かつ仕様どおりに書き出されてほしい, so that 文字列の内容が意図せずマークアップとして解釈されない

#### Acceptance Criteria
1. When テキストや属性値をエスケープする, the HTML Renderer shall `&`、`<`、`>`、`"` をそれぞれ `&amp;`、`&lt;`、`&gt;`、`&quot;` に置き換え、他のバイトはそのまま出力する
2. When URL を符号化する, the HTML Renderer shall 英数字と `-_.!~*()#$%+,/:;=?@` をそのまま出力し、`&` を `&amp;`、`'` を `&#x27;` に置き換え、それ以外のバイト（空白、制御文字、`"<>[\]^` と バッククォート、`{|}`、および 0x80 以上のバイト）を大文字 16 進の `%XX` に置き換える
3. The HTML Renderer shall URL 中の既存の `%` を再度符号化せず、そのまま出力する
4. The HTML Renderer shall エスケープと符号化の処理を、フックの作者と利用者が木の外の文字列にも使えるように提供する
5. The HTML Renderer shall NUL バイトを含む値も長さどおりに処理し、途中で打ち切らない

### Requirement 7: 安全性のオプション
**Objective:** As a 信頼できない Markdown を表示するライブラリ利用者, I want 既定で生 HTML と危険な URL が無害化されてほしい, so that 何も設定しなくてもスクリプトの埋め込みを防げる

#### Acceptance Criteria
1. While 生 HTML の出力が許可されていない（既定）, the HTML Renderer shall HTML block と HTML inline の内容の代わりに `<!-- raw HTML omitted -->` を出力する（HTML block はブロックとしての前後の改行規則を保つ）
2. Where 生 HTML の出力が許可されている, the HTML Renderer shall HTML block と HTML inline の内容をエスケープせずそのまま出力する
3. While 危険な URL の出力が許可されていない（既定）, when link または image のリンク先が危険な URL である, the HTML Renderer shall `href` または `src` 属性の値を空にする
4. Where 危険な URL の出力が許可されている, the HTML Renderer shall 危険な URL も他の URL と同じく符号化して出力する
5. The HTML Renderer shall 大文字小文字を区別せずに `javascript:`、`vbscript:`、`file:`、`data:` で始まる URL を危険とし、ただし `data:image/png`、`data:image/gif`、`data:image/jpeg`、`data:image/webp` で始まる URL は危険としない
6. The HTML Renderer shall 生 HTML の許可と危険な URL の許可を、互いに独立に設定できるようにする
7. The HTML Renderer shall 生 HTML の内容を検査・変形するサニタイズを行わず、出力するか置き換えるかの切り替えだけを行う

### Requirement 8: soft break の出力方法
**Objective:** As a ライブラリ利用者, I want soft break の出し方を選びたい, so that 表示先に合わせて改行を保つ・詰める・強制改行にすることができる

#### Acceptance Criteria
1. While soft break の出力方法が既定である, the HTML Renderer shall soft break を改行 1 つとして出力する
2. Where soft break を空白として出力する設定である, the HTML Renderer shall soft break を空白 1 つとして出力する
3. Where soft break を強制改行として出力する設定である, the HTML Renderer shall soft break を line break と同じ `<br />` と改行として出力する
4. If soft break の出力方法に未定義の値が指定されている, the HTML Renderer shall 何も出力せず不正な引数のエラーを返す

### Requirement 9: CommonMark 仕様との一致
**Objective:** As a commonmark の実装者, I want 既定の書式が CommonMark 仕様のテストスイートの期待出力と一致してほしい, so that 正規化なしのバイト比較で適合を判定できる

#### Acceptance Criteria
1. Where 生 HTML と危険な URL の出力が許可され、他の設定が既定である, the HTML Renderer shall CommonMark 仕様の各例の Markdown を仕様どおりに解釈した結果と同じ構造・値の木に対して、期待 HTML とバイト単位で一致する出力を生成する
2. While すべての設定が既定である, the HTML Renderer shall 生 HTML も危険な URL も含まない木に対して、前項と同一の出力を生成する
3. The HTML Renderer shall 出力の書式（タグの綴り、属性の順序、空要素の `/>` の形、改行の位置）を、設定によって変わる部分を除いて固定する

### Requirement 10: レンダラフックの登録
**Objective:** As a 拡張の作者, I want ノード種別ごとの HTML 出力を登録したい, so that 拡張ノード種別の出力を追加し、標準ノード種別の出力を差し替えられる

#### Acceptance Criteria
1. When 拡張の作者が拡張ノード種別を指定して出力関数を登録する, the HTML Renderer shall 以後のレンダリングでその種別のノードにその関数を使う
2. When 利用者が標準ノード種別を指定して出力関数を登録する, the HTML Renderer shall 以後のレンダリングでその種別のノードに組み込みの出力の代わりにその関数を使う
3. The HTML Renderer shall 出力関数を、node-tree が返す種別の同一性（標準種別・拡張種別で共通の識別手段）で結び付け、種別の名前や登録元のパーサ・拡張の単位に依存しない
4. When 既に出力関数が登録されている種別に別の関数を登録する, the HTML Renderer shall 後から登録した関数で置き換える
5. When 利用者が種別の登録の解除を要求する, the HTML Renderer shall その種別の出力を登録前の振る舞い（標準種別は組み込み、拡張種別は既定の扱い）に戻す
6. The HTML Renderer shall 出力関数ごとに、登録時に渡された利用者データを呼び出し時にそのまま渡す
7. If 種別が指定されていない, the HTML Renderer shall 登録内容を変更せず不正な引数のエラーを返す
8. The HTML Renderer shall 出力関数の登録を、パーサや拡張の登録の仕組みを使わずに行えるようにする
9. The HTML Renderer shall 出力関数の登録と設定を、別々のレンダリングで再利用できる単位として保持する

### Requirement 11: フックの呼び出しと制御
**Objective:** As a 拡張の作者, I want フックがいつどのように呼ばれ、何ができるかが明確であってほしい, so that 標準種別と同じ品質の出力を拡張種別で書ける

#### Acceptance Criteria
1. When 子を取りうる種別のノードに出力関数が登録されている, the HTML Renderer shall そのノードに入るときと出るときの 2 回、どちらであるかを示して関数を呼び出す
2. When 子を取らない種別のノードに出力関数が登録されている, the HTML Renderer shall そのノードについて入るときの 1 回だけ関数を呼び出す
3. When 出力関数がノードに入るときに子の省略を要求する, the HTML Renderer shall そのノードの子孫を出力せず、続けてそのノードを出るときの呼び出しを行う
4. When 出力関数が組み込みの出力への委譲を要求する, the HTML Renderer shall 現在のノードと出入りの別について、出力関数が登録されていない場合と同じ出力を行う
5. The HTML Renderer shall 出力関数に、エスケープした文字列の出力、URL の符号化と危険な URL の判定を伴う出力、そのままの出力、行頭を保証する改行の出力、現在の設定の参照の手段を提供する
6. When 出力関数が登録されていない拡張種別のノードをレンダリングする, the HTML Renderer shall そのノード自身のマークアップを出力せず、子を通常どおり出力する
7. If 出力関数がエラーを返す, the HTML Renderer shall 直ちにレンダリングを中止し、そのエラーを呼び出し側へ返す
8. The HTML Renderer shall 出力関数の中から木を変更してはならないこと、出力関数に渡した手段をその呼び出しの外で使ってはならないことを、利用者の義務として文書化する

### Requirement 12: エラー処理と資源
**Objective:** As a ライブラリ利用者, I want 失敗しても出力先と資源が予測可能な状態であってほしい, so that 確保失敗やフックのエラーを安全に扱える

#### Acceptance Criteria
1. The HTML Renderer shall 失敗を foundation の共通エラーコードで返し、独自のエラー体系を持たない
2. The HTML Renderer shall すべての動的確保を Amark のグローバルアロケータ経由で行う
3. If レンダリングが途中で失敗する（確保失敗、サイズのあふれ、フックのエラー）, the HTML Renderer shall 出力先の内容を呼び出し前と同じに戻してエラーを返す
4. If 出力関数の登録またはレンダラの設定の作成で確保に失敗する, the HTML Renderer shall 登録内容と出力引数を変更せずメモリ不足のエラーを返す
5. When 利用者がレンダラの設定を破棄する, the HTML Renderer shall 登録内容のために確保したメモリをすべて解放する
6. The HTML Renderer shall 一度のレンダリングで、出力先の伸長以外の確保を完了時に残さない

### Requirement 13: 堅牢性と性能
**Objective:** As a 信頼できない入力を扱うライブラリ利用者, I want 病的な木でもレンダリングが破綻しないでほしい, so that 深い入れ子や大きな文書を安心して出力できる

#### Acceptance Criteria
1. When 深さ 10 万段の入れ子の木をレンダリングする, the HTML Renderer shall スタックあふれを起こさずに完了する
2. The HTML Renderer shall ノード数と出力のバイト数の合計に比例する時間でレンダリングを終える（画像の入れ子を含む）
3. The HTML Renderer shall 登録内容を変更していない同じ設定を使って、異なるスレッドから同時にレンダリングできるようにする（同じ出力先を共有しない場合）
4. The HTML Renderer shall 可変のグローバル状態を持たない
