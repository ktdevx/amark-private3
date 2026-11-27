# Requirements Document

## Project Description (Input)
Amark は木を編集できるが、編集した結果を Markdown として書き戻す手段がなく、Markdown 文書の機械的な変換・整形という用途が成立しない。現在は `node-tree` が木を、`html-renderer` がレンダラフックの形を、`node-tree` がレンダラ共通の走査駆動部を、`commonmark` がパーサを提供する想定で、Markdown 出力に関するコードは存在しない。

文書を木として読み込み機械的に書き換えるライブラリ利用者、API で木を組み立てて Markdown を得たい利用者、および拡張の作者（将来の gfm を含む）のために、次の状態を実現する。

- 標準ノード種別からなる木を CommonMark として有効な Markdown テキストにレンダリングできる
- 出力した Markdown を再度パースすると、元の木と意味的に等価な木（同じ HTML）になる
- 拡張がノード種別ごとの Markdown 出力を追加・差し替えできる
- 折り返し幅などの整形オプションがある

`node-tree` のイテレータで木を反復走査し、ノード種別ごとのレンダリング関数を表引きで呼び出す。ブロック引用やリスト項目の行頭プレフィックスをスタックで管理し、文脈に応じたエスケープ（行頭の記号、強調記号、リンク文字列内など）を行う。フック機構は html-renderer と同じ形にする。ロスレスは対象外で、原文の記法は保持せず正規化した記法で出力する。往復テスト（Markdown → 木 → Markdown → 木で HTML が一致すること）を CommonMark 仕様の例で行う。失敗は `foundation` の共通エラーコードで返し、往復テストのためだけのコードをライブラリ本体に入れない。原文と一字一句同じ出力、パース、GFM ノードの出力（次フェーズ）、表の桁揃えや参照リンクの再構成などの高度な整形は対象外とする。

## Introduction
本仕様は Amark の Markdown レンダラを定義する。対象は、木（または部分木）から CommonMark 方言の Markdown 文字列へのレンダリング、18 の標準ノード種別それぞれの Markdown 出力（入れ子の引用・リストの行頭プレフィックス、tight / loose、フェンスの長さ、コードスパンのバッククォート数、リンク先と title の書き方を含む）、文脈依存のエスケープ、強調の区切り文字の選択、整形オプション（折り返し幅、リスト記号、強調記号、見出し形式、フェンス文字など）、ノード種別ごとのレンダラフック（html-renderer と同じ形）、および CommonMark 仕様の全例による往復テストと冪等性テストである。出力は原文の記法を保持しない正規化した記法とし、再パースしたときに元の木と同じ HTML になることを正しさの基準とする。

## Boundary Context
- **In scope**: 任意のノードを起点とする部分木の Markdown 出力、標準ノード種別すべての出力規則、行頭・インライン・リンク先・title・コードスパン・コードブロックでの文脈依存のエスケープ、強調の区切り文字の選択と隣接する区切りの扱い、整形オプションとその検証、段落の折り返し、ノード種別ごとのフックの登録・解除と呼び出し規則、フックから使える出力手段（行頭プレフィックスの追加と削除、文脈依存のエスケープを含む）、CommonMark の記法で表現できない木の扱い、往復テストと冪等性テスト
- **Out of scope**: テキストのパース（parser-core / commonmark）、原文と一字一句同じ出力（ロスレス）、原文の記法をノードに記憶させること、GFM のノード種別の出力（次フェーズ。フックで追加できる形にだけする）、表の桁揃え・参照リンクの再構成などのフォーマッタとしての高度な機能、東アジアの全角文字の表示幅を考慮した折り返し、HTML の出力
- **Adjacent expectations**: foundation の共通エラー列挙・グローバルアロケータ・可変長バッファ・文字列の所有権規約に従う。node-tree の標準種別、種別ハンドル、値のアクセサ、enter / exit イテレータを使い、木を変更しない。html-renderer が定めるフックの形（登録の仕方、呼び出しのイベント、子の省略と省略後の EXIT の呼び出し、既定出力への委譲、エラーの伝播と出力の巻き戻し）を同じ形で提供し、node-tree が所有する走査の駆動部を html-renderer と共有する。往復テストは commonmark の CommonMark 方言のパーサ、仕様 0.31.2 の固定された例と抽出器、html-renderer の「生 HTML と危険な URL を許可した設定」を使う。往復テストで見つかったパーサ側の不具合は commonmark の作業として扱う

## Requirements

### Requirement 1: レンダリングの入口と出力先
**Objective:** As a ライブラリ利用者, I want 手元の木をパーサなしに Markdown 文字列へ変換したい, so that 編集した木やAPI で組み立てた木を Markdown 文書として書き戻せる

#### Acceptance Criteria
1. When 利用者が木の根ノードと出力先を指定してレンダリングを要求する, the Markdown Renderer shall その根を起点とする部分木全体の Markdown を出力先の既存の内容の後ろに追記する
2. When 利用者が document 以外のノード（リスト項目、段落、強調など）を起点としてレンダリングを要求する, the Markdown Renderer shall そのノードとその子孫だけを、木の中の同じノードと同じ規則（親のリストの種類や tight / loose を含む）で出力する
3. The Markdown Renderer shall パーサを必要とせず、API だけで組み立てた木を、パース結果の木と同じ規則で出力する
4. The Markdown Renderer shall レンダリングの前後で木の構造と値を変更しない
5. If 根ノードまたは出力先が指定されていない, the Markdown Renderer shall 何も出力せず不正な引数のエラーを返す
6. When 利用者が同じ木と同じ設定とフックで複数回レンダリングを要求する, the Markdown Renderer shall 毎回同一のバイト列を出力する
7. When 起点がブロックの種別である, the Markdown Renderer shall 出力の最後の行を改行 1 つで終え、末尾に空行を出力しない
8. When 起点がインラインの種別である, the Markdown Renderer shall 出力の末尾に改行を付け加えない
9. The Markdown Renderer shall 出力の行の区切りと行頭の判定を、このレンダリングで出力した内容だけを基準に行い、出力先に既にあった内容によって出力を変えない

### Requirement 2: ブロック種別の出力
**Objective:** As a 文書を機械的に書き換えるライブラリ利用者, I want 標準ブロック種別が CommonMark の正規化した記法で出力されてほしい, so that 出力を人が読め、他の CommonMark 実装でも同じ構造として解釈される

#### Acceptance Criteria
1. When document をレンダリングする, the Markdown Renderer shall document 自身の記法を出力せず、子のブロックだけを出力する
2. When 同じ親の中で前の兄弟を持つブロックをレンダリングする, the Markdown Renderer shall tight なリストの項目の中にある場合を除き、前のブロックとの間に空行を 1 つ出力する
3. When paragraph をレンダリングする, the Markdown Renderer shall インラインの内容を、soft break の位置で改行した行として出力する
4. While 見出し形式が ATX（既定）である, when heading をレンダリングする, the Markdown Renderer shall 見出しレベルと同じ数の `#`、空白 1 つ、内容を 1 行で出力し、内容が空なら `#` の並びだけを出力する
5. Where 見出し形式が Setext である, when レベル 1 または 2 で内容が空でない heading をレンダリングする, the Markdown Renderer shall 内容の行の後に、レベル 1 なら `=`、レベル 2 なら `-` の並びの下線の行を出力する
6. When heading の内容が soft break または line break を含み、そのレベルが 1 または 2 である, the Markdown Renderer shall 見出し形式の設定にかかわらず Setext の形で出力する
7. When Setext の形では解釈が変わる位置（tight なリストの項目の中で段落の直後など）または形（レベル 3 以上、内容が空）の heading をレンダリングする, the Markdown Renderer shall 改行を含まない限り ATX の形で出力する
8. When thematic break をレンダリングする, the Markdown Renderer shall 設定した区切り線の文字を 3 つ並べた行を出力する
9. When 設定した区切り線の文字では別の構文として解釈される位置（同じ行のリスト記号と同じ文字で行全体が区切り線になる、空行を挟まない段落の直後で Setext の下線になる）に thematic break がある, the Markdown Renderer shall そう解釈されない別の文字で区切り線を出力する
10. When code block をレンダリングする, the Markdown Renderer shall 開きのフェンス（info string があれば続けて出力）、内容の各行、閉じのフェンスをこの順で出力し、インデントによるコードブロックの形を使わない
11. When code block のフェンスを決める, the Markdown Renderer shall 内容に現れるフェンス文字の連続の最大長より長く、かつ 3 以上の長さのフェンスを使う
12. When 設定したフェンス文字がバッククォートで、info string がバッククォートを含む, the Markdown Renderer shall チルダのフェンスを使う
13. When HTML block をレンダリングする, the Markdown Renderer shall 内容を変換せずにそのまま、各行に現在の行頭プレフィックスを付けて出力する
14. When block quote をレンダリングする, the Markdown Renderer shall 子のブロックのすべての行の先頭に `> ` を付け、内容のない行には `>` だけを付けて出力する
15. The Markdown Renderer shall すべての行の末尾から、行頭プレフィックスやリスト記号に由来する空白を取り除き、内容に由来する空白（コードブロックや HTML block の中の空白）は保つ

### Requirement 3: リストとリスト項目
**Objective:** As a 文書を機械的に書き換えるライブラリ利用者, I want リストの入れ子・番号・tight / loose が再パースで同じになるように出力されてほしい, so that リストを含む文書を安全に書き戻せる

#### Acceptance Criteria
1. When 箇条書きのリストの項目をレンダリングする, the Markdown Renderer shall 設定した箇条書き記号と空白 1 つを項目の最初の行に置き、項目の内容の 2 行目以降をその記号と空白の幅だけ字下げする
2. When 番号付きのリストの項目をレンダリングする, the Markdown Renderer shall 最初の項目にリストの開始番号、以後の項目に 1 ずつ増やした番号（9 桁を超える場合は開始番号）を、設定した区切り文字と空白 1 つとともに置き、2 行目以降をその幅だけ字下げする
3. When 子を持たないリスト項目をレンダリングする, the Markdown Renderer shall リスト記号だけの行を出力する
4. While リストが tight である, the Markdown Renderer shall 項目の間と、各項目の子のブロックの間に空行を出力しない
5. While リストが tight でない, the Markdown Renderer shall 項目の間と、各項目の子のブロックの間に空行を 1 つ出力する
6. When 箇条書きのリストの直前の兄弟が箇条書きのリストである, the Markdown Renderer shall 直前のリストと異なる箇条書き記号を使い、2 つのリストが 1 つにつながらないようにする
7. When 番号付きのリストの直前の兄弟が番号付きのリストである, the Markdown Renderer shall 直前のリストと異なる区切り文字（`.` と `)`）を使う
8. When リスト項目の最初の子がリスト、ブロック引用、見出し、コードブロック、HTML block、区切り線のいずれかである, the Markdown Renderer shall その子をリスト記号と同じ行から出力する
9. The Markdown Renderer shall ブロック引用とリスト項目を任意の深さに入れ子にした場合も、各行に外側から順にすべての行頭プレフィックスを付け、遅延継続行に頼らない

### Requirement 4: インライン種別の出力
**Objective:** As a 文書を機械的に書き換えるライブラリ利用者, I want 標準インライン種別が再パースで同じ種別と値になるように出力されてほしい, so that 段落の中身を編集した結果も正しく書き戻せる

#### Acceptance Criteria
1. When text をレンダリングする, the Markdown Renderer shall 内容を、Requirement 5 の文脈依存のエスケープを適用して出力する
2. When soft break をレンダリングする, the Markdown Renderer shall 改行と、次の行の行頭プレフィックスを出力する
3. While line break の形式が既定（バックスラッシュ）である, when line break をレンダリングする, the Markdown Renderer shall `\` と改行を出力する
4. Where line break の形式が空白 2 つである, when line break をレンダリングする, the Markdown Renderer shall 空白 2 つと改行を出力する
5. When code span をレンダリングする, the Markdown Renderer shall 内容に現れない長さのうち最短のバッククォートの並びで内容を囲む
6. When code span の内容がバッククォートで始まるか終わる、または空白で始まりかつ空白で終わり空白だけではない, the Markdown Renderer shall 囲みのバッククォートと内容の間の両側に空白を 1 つずつ入れる
7. When emphasis または strong をレンダリングする, the Markdown Renderer shall 子を、emphasis は区切り文字 1 つ、strong は区切り文字 2 つで囲み、区切り文字の選択を Requirement 6 に従う
8. When link をレンダリングする, the Markdown Renderer shall `[` 子 `](` リンク先、title が空でなければ空白と引用符で囲んだ title、`)` の形（インラインリンク）で出力し、参照リンクの形を使わない
9. When image をレンダリングする, the Markdown Renderer shall link と同じ形の先頭に `!` を付けて出力し、子を通常のインラインとして出力する
10. Where オートリンクの使用が有効（既定）である, when link の子がリンク先と同一の内容の text 1 つだけ（またはリンク先が `mailto:` に続く内容と同一のメールアドレス）で title が空、かつリンク先がオートリンクの構文を満たす, the Markdown Renderer shall `<` リンク先（メールアドレスの場合は `mailto:` を除いたもの）`>` の形で出力する
11. When HTML inline をレンダリングする, the Markdown Renderer shall 内容を変換せずにそのまま出力し、内容に含まれる改行の後には現在の行頭プレフィックスを付ける

### Requirement 5: 文脈依存のエスケープ
**Objective:** As a 文書を機械的に書き換えるライブラリ利用者, I want 文字列の内容が意図しない Markdown の構文として解釈されないように出力されてほしい, so that 任意の文字を含むテキストを書き戻しても構造が変わらない

#### Acceptance Criteria
1. The Markdown Renderer shall text の中で、インラインの構文を始めうる文字（`\`、`` ` ``、`*`、`[`、`]`、`<`、`[` が後続する `!`、文字参照として解釈されうる並びの `&`、英数字に挟まれていない `_`）をバックスラッシュでエスケープする
2. When 行の内容の先頭に、ブロックの構文を始めうる文字（`#`、`>`、`-`、`+`、`=`、`~`、および数字の並びに続く `.` または `)`）が来る, the Markdown Renderer shall その文字をバックスラッシュでエスケープする
3. When 行の内容の先頭または末尾に空白またはタブが来る（段落、見出し、soft break や line break の直後や直前を含む）, the Markdown Renderer shall その空白またはタブを数値文字参照で出力する
4. When text が改行、復帰、その他の制御文字を含む, the Markdown Renderer shall それらを数値文字参照で出力する
5. When ATX の見出しの内容を出力する, the Markdown Renderer shall 内容の `#` をエスケープし、閉じの `#` の並びとして解釈されないようにする
6. When text の末尾の `!` の直後に link が続く, the Markdown Renderer shall その `!` をエスケープし、画像として解釈されないようにする
7. When link または image のリンク先が空、または空白・制御文字を含む, the Markdown Renderer shall リンク先を `<` と `>` で囲む形で出力し、その中の `<`、`>`、`\` をエスケープする
8. When link または image のリンク先を `<` `>` で囲まない形で出力する, the Markdown Renderer shall リンク先の `(`、`)`、`\`、`<`、`>` と、文字参照として解釈されうる `&` をエスケープする
9. When title を出力する, the Markdown Renderer shall `"` で囲み、title の中の `"`、`\`、文字参照として解釈されうる `&` をエスケープし、改行を数値文字参照で出力する
10. When code block の info string を出力する, the Markdown Renderer shall info string の `\` と文字参照として解釈されうる `&` をエスケープし、先頭と末尾の空白を数値文字参照で出力する
11. The Markdown Renderer shall code span、code block の内容、HTML block、HTML inline の内容をエスケープしない

### Requirement 6: 強調の区切り文字
**Objective:** As a 文書を機械的に書き換えるライブラリ利用者, I want 強調が隣接・入れ子・単語の途中にあっても再パースで同じ構造になるように出力されてほしい, so that 強調を含む文を編集しても強調の範囲が変わらない

#### Acceptance Criteria
1. The Markdown Renderer shall emphasis の区切り文字に設定した文字（既定 `*`）を、strong の区切り文字に設定した文字（既定 `*`）を優先して使う
2. When emphasis または strong の開きまたは閉じの区切りが、別の emphasis または strong の区切りと間に文字を挟まずに隣接する, the Markdown Renderer shall 隣接する区切りと異なる区切り文字を使い、区切りの並びが 1 つにつながらないようにする
3. When 優先する区切り文字では CommonMark の規則（left-flanking / right-flanking と、区切りの並びの長さの和の規則）により開き・閉じとして解釈されない、または別の区切りと対になる, the Markdown Renderer shall もう一方の区切り文字に切り替える
4. When どちらの区切り文字でも開き・閉じとして解釈されない, the Markdown Renderer shall 区切りに隣接する text の文字（区切りの外側の直前の文字、または内側・外側の直後の文字）を数値文字参照で出力して、区切りが開き・閉じとして解釈されるようにする
5. The Markdown Renderer shall 区切りの判定に、CommonMark 0.31.2 が定める空白と句読点（Unicode の一般カテゴリ P と S を含む）の分類を使い、非 ASCII の文字（日本語を含む）の前後でも判定を誤らない
6. The Markdown Renderer shall 区切り文字の選択を、ノードの値と木の形と設定だけで決め、同じ入力に対して常に同じ選択をする

### Requirement 7: 整形オプション
**Objective:** As a ライブラリ利用者, I want 出力の記法を選びたい, so that プロジェクトの書式規約に合わせた Markdown を得られる

#### Acceptance Criteria
1. The Markdown Renderer shall 設定の既定値を、折り返しなし、箇条書き記号 `-`、番号の区切り文字 `.`、emphasis と strong の区切り文字 `*`、見出し形式 ATX、フェンス文字 `` ` ``、区切り線の文字 `-`、line break はバックスラッシュ、オートリンクの使用は有効とする
2. Where 利用者が箇条書き記号（`-`、`*`、`+`）、番号の区切り文字（`.`、`)`）、emphasis と strong の区切り文字（それぞれ `*`、`_`）、見出し形式（ATX、Setext）、フェンス文字（`` ` ``、`~`）、区切り線の文字（`-`、`*`、`_`）、line break の形式、オートリンクの使用、折り返し幅を設定する, the Markdown Renderer shall 他の要件が別の記法を求める場合を除き、その記法で出力する
3. If 設定の記法に定義されていない値が指定されている, the Markdown Renderer shall 何も出力せず不正な引数のエラーを返す
4. The Markdown Renderer shall 設定を、別々のレンダリングで再利用できる単位として保持する

### Requirement 8: 折り返し
**Objective:** As a ライブラリ利用者, I want 段落を指定した幅で折り返したい, so that 差分の取りやすい幅の揃った文書を書き出せる

#### Acceptance Criteria
1. While 折り返し幅が 0（既定）である, the Markdown Renderer shall 段落を折り返さず、soft break の位置だけで改行する
2. Where 折り返し幅が 1 以上である, the Markdown Renderer shall 段落の行を、行頭プレフィックスを含む桁数（コードポイントの数）が幅を超えないように、text の中の単語の間の空白 1 つの位置で改行する
3. Where 折り返し幅が 1 以上である, the Markdown Renderer shall 段落の soft break を空白と同じ折り返し候補として扱い、改行しない位置では空白 1 つとして出力する（直後の内容が次項の改行しない条件に当たる soft break は改行のまま出力する）
4. The Markdown Renderer shall 次の行の先頭がブロックの構文を始めうる文字・HTML inline・数字になる位置、連続する空白の位置、および code span・HTML inline・リンク先・title・見出し・コードブロック・HTML block の中では改行しない
5. When 改行できる位置がないまま行が幅を超える, the Markdown Renderer shall その行を幅を超えたまま出力する

### Requirement 9: 再パースの正しさと表現できない木
**Objective:** As a 文書を機械的に書き換えるライブラリ利用者, I want 出力を再度パースすると元と同じ文書になってほしい, so that 読み込み・編集・書き戻しの往復で文書の意味が変わらない

#### Acceptance Criteria
1. When CommonMark 方言のパーサが生成した木を折り返しなしの任意の有効な設定でレンダリングし、その出力を同じ方言で再パースする, the Markdown Renderer shall 再パースした木と元の木を、生 HTML と危険な URL を許可した html-renderer の設定でそれぞれ HTML にしたとき、バイト単位で一致する出力を生成する
2. Where 折り返し幅が 1 以上である, the Markdown Renderer shall 前項の比較で soft break を空白として HTML に出力した場合に一致する出力を生成する
3. When CommonMark 方言のパーサが生成した木のレンダリング結果を再パースし、同じ設定で再びレンダリングする, the Markdown Renderer shall 1 回目と同一のバイト列を出力する
4. The Markdown Renderer shall 原文の記法（強調記号の種類、見出しの形式、リンクの参照形式、インデントによるコードブロックなど）を保持せず、設定で選んだ正規化した記法で出力する
5. The Markdown Renderer shall CommonMark 方言で解釈できる構文だけを出力し、他の方言の構文を使わない
6. If 木が CommonMark の記法で表現できない形（空の emphasis・strong・code span、改行を含む code span、末尾が改行でない code block の内容、段落の末尾の line break、連続する soft break、レベル 3 以上で改行を含む見出し、tight なリストの項目の中で隣接する段落など）を含む, the Markdown Renderer shall エラーを返さず、設計文書に列挙した規則で近似した出力を行う

### Requirement 10: レンダラフックの登録
**Objective:** As a 拡張の作者, I want ノード種別ごとの Markdown 出力を登録したい, so that 拡張ノード種別の出力を追加し、標準ノード種別の出力を差し替えられる

#### Acceptance Criteria
1. When 拡張の作者が拡張ノード種別を指定して出力関数を登録する, the Markdown Renderer shall 以後のレンダリングでその種別のノードにその関数を使う
2. When 利用者が標準ノード種別を指定して出力関数を登録する, the Markdown Renderer shall 以後のレンダリングでその種別のノードに組み込みの出力の代わりにその関数を使う
3. The Markdown Renderer shall 出力関数を、node-tree が返す種別の同一性（標準種別・拡張種別で共通の識別手段）で結び付け、種別の名前や登録元のパーサ・拡張の単位に依存しない
4. When 既に出力関数が登録されている種別に別の関数を登録する, the Markdown Renderer shall 後から登録した関数で置き換える
5. When 利用者が種別の登録の解除を要求する, the Markdown Renderer shall その種別の出力を登録前の振る舞い（標準種別は組み込み、拡張種別は既定の扱い）に戻す
6. The Markdown Renderer shall 出力関数ごとに、登録時に渡された利用者データを呼び出し時にそのまま渡す
7. If 種別が指定されていない, the Markdown Renderer shall 登録内容を変更せず不正な引数のエラーを返す
8. The Markdown Renderer shall 出力関数の登録を、パーサや拡張の登録の仕組みを使わずに行えるようにし、登録の形（関数の引数、呼び出しのイベント、登録・置き換え・解除の規則）を html-renderer と同じにする
9. The Markdown Renderer shall 出力関数の登録と設定を、別々のレンダリングで再利用できる単位として保持する

### Requirement 11: フックの呼び出しと制御
**Objective:** As a 拡張の作者, I want フックがいつどのように呼ばれ、何ができるかが明確であってほしい, so that 標準種別と同じ品質の Markdown 出力を拡張種別で書ける

#### Acceptance Criteria
1. When 子を取りうる種別のノードに出力関数が登録されている, the Markdown Renderer shall そのノードに入るときと出るときの 2 回、どちらであるかを示して関数を呼び出す
2. When 子を取らない種別のノードに出力関数が登録されている, the Markdown Renderer shall そのノードについて入るときの 1 回だけ関数を呼び出す
3. When 出力関数がノードに入るときに子の省略を要求する, the Markdown Renderer shall そのノードの子孫を出力せず、続けてそのノードを出るときの呼び出しを行う
4. When 出力関数が組み込みの出力への委譲を要求する, the Markdown Renderer shall 現在のノードと出入りの別について、出力関数が登録されていない場合と同じ出力を行う
5. The Markdown Renderer shall 出力関数に、そのままの出力、text と同じ文脈依存のエスケープを伴う出力、リンク先と title の書式での出力、行頭を保証する改行、空行の保証、行頭プレフィックスの追加と削除、現在の設定の参照の手段を html-renderer と同じ意味の名前で提供する
6. When 出力関数が行頭プレフィックスを追加する, the Markdown Renderer shall 以後に始まる行の先頭にそのプレフィックスを付け、そのノードを出るとき（子を取らない種別では呼び出しの後）に、そのノードで追加したプレフィックスを自動で取り除く
7. If 出力関数が、そのノードで追加していない行頭プレフィックスを取り除こうとする, the Markdown Renderer shall プレフィックスを変更せず状態が不正であることを示すエラーを返す
8. When 出力関数が登録されていない拡張種別のノードをレンダリングする, the Markdown Renderer shall そのノード自身の記法を出力せず、子を通常どおり出力する（ブロックの種別では前後のブロックとの区切りを他のブロックと同じに保つ）
9. If 出力関数がエラーを返す, the Markdown Renderer shall 直ちにレンダリングを中止し、そのエラーを呼び出し側へ返す
10. The Markdown Renderer shall 出力関数の中から木を変更してはならないこと、出力関数に渡した手段をその呼び出しの外で使ってはならないこと、出力関数が書いた内容は再パースの正しさ（Requirement 9）の保証の対象外であることを、利用者の義務と注意として文書化する

### Requirement 12: エラー処理と資源
**Objective:** As a ライブラリ利用者, I want 失敗しても出力先と資源が予測可能な状態であってほしい, so that 確保失敗やフックのエラーを安全に扱える

#### Acceptance Criteria
1. The Markdown Renderer shall 失敗を foundation の共通エラーコードで返し、独自のエラー体系を持たない
2. The Markdown Renderer shall すべての動的確保を Amark のグローバルアロケータ経由で行う
3. If レンダリングが途中で失敗する（確保失敗、サイズのあふれ、フックのエラー、プレフィックスの不正な操作）, the Markdown Renderer shall 出力先の内容を呼び出し前と同じに戻してエラーを返す
4. If 出力関数の登録またはレンダラの設定の作成で確保に失敗する, the Markdown Renderer shall 登録内容と出力引数を変更せずメモリ不足のエラーを返す
5. When 利用者がレンダラの設定を破棄する, the Markdown Renderer shall 登録内容のために確保したメモリをすべて解放する
6. The Markdown Renderer shall 一度のレンダリングで、出力先の伸長以外の確保を、成功・失敗のどちらの場合も完了時に残さない

### Requirement 13: 堅牢性と性能
**Objective:** As a 信頼できない入力を扱うライブラリ利用者, I want 病的な木でもレンダリングが破綻しないでほしい, so that 深い入れ子や大きな文書を安心して書き戻せる

#### Acceptance Criteria
1. When 深さ 10 万段の入れ子の木（ブロック引用、リスト、強調のそれぞれ）をレンダリングする, the Markdown Renderer shall スタックあふれを起こさずに完了する
2. The Markdown Renderer shall ノード数と出力のバイト数の合計に比例する時間でレンダリングを終える（多数の隣接するリストや強調を含む）
3. The Markdown Renderer shall 登録内容を変更していない同じ設定を使って、異なるスレッドから同時にレンダリングできるようにする（同じ出力先を共有しない場合）
4. The Markdown Renderer shall 可変のグローバル状態を持たない

### Requirement 14: 往復テストと冪等性テスト
**Objective:** As a ライブラリの保守者, I want 再パースの正しさを CommonMark 仕様の全例で継続的に確かめたい, so that レンダラやパーサの変更で往復が壊れたことをすぐに検出できる

#### Acceptance Criteria
1. The Markdown Renderer test suite shall CommonMark 仕様 0.31.2 の全例について、Markdown → 木 → Markdown → 木 の往復を行い、2 つの木の HTML（生 HTML と危険な URL を許可した設定）がバイト単位で一致することを確かめる
2. The Markdown Renderer test suite shall 前項の往復を、既定の設定、既定と異なる記法を選んだ設定、折り返し幅を指定した設定（soft break を空白として HTML を比較）のそれぞれで行う
3. The Markdown Renderer test suite shall 全例について、1 回目のレンダリング結果を再パースして再びレンダリングした結果が 1 回目と同一であることを、前項の各設定で確かめる
4. If 往復または冪等性の確認に失敗した例がある, the Markdown Renderer test suite shall 例の番号、節、元の入力、出力した Markdown、比較した 2 つの出力を報告して失敗する
5. Where 設計上往復できない例がある, the Markdown Renderer test suite shall その例を、理由を設計文書に記した明示的な除外一覧に載せた場合に限り除外し、除外した例の数を報告する（目標は 0 件）
6. The Markdown Renderer test suite shall 往復テストのためのコードをライブラリ本体に含めず、テスト側だけに置く
7. The Markdown Renderer test suite shall 仕様の例に現れない難しい形（隣接・入れ子・単語内の強調、各種の行頭記号、フェンスやバッククォートを含むコード、空白や括弧を含むリンク先、連続するリスト、表現できない木）を、手組みの木から出力した Markdown のバイト比較と再パースの両方で確かめる
