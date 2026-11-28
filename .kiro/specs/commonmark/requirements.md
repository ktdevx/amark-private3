# Requirements Document

## Project Description (Input)
`parser-core` は構文を知らないエンジンであり、それだけでは Markdown を解釈できない。利用者が最初に必要とする方言である CommonMark を、`parser-core` の公開拡張 API の上に実装する必要がある。これは拡張 API が実用に足ることの証明でもある。現在は `parser-core` がエンジンと拡張登録 API を、`node-tree` が標準ノード種別を、`html-renderer` が HTML 出力を提供する想定で、具体的な構文の実装は存在しない。

ライブラリの利用者、方言を組み立てる利用者、将来の gfm と markdown-renderer のために、次の状態を実現する。

- CommonMark の各構文が個別の拡張として登録され、それらをまとめた CommonMark 方言プリセットでパースできる
- CommonMark 仕様（spec.txt）の例がすべて通る（対象バージョンを固定し、spec.txt をライセンス表記とともにリポジトリに固定して持つ）
- 個々の構文を外した方言（例: 生 HTML なし、インデントコードブロックなし）を組み立てられる
- 病的な入力（深いネスト、大量のデリミタやブラケット）で処理時間が爆発しない
- HTML エンティティ表など CommonMark が必要とするデータを持つ

構文の実装は `parser-core` の公開拡張 API だけを使い、エンジンの非公開部分に依存しない。仕様適合テストのためだけのコードはライブラリ本体に入れない。GFM 拡張、エンジン本体、レンダラの出力規則は対象外とする。

## Introduction
本仕様は Amark の CommonMark 方言（commonmark）を定義する。対象は、CommonMark 仕様 0.31.2 のブロック構文とインライン構文を、特定の方言に属さない「規則」として取り外し可能な単位の拡張として提供すること、それらをまとめた方言プリセットと簡易 API、CommonMark が必要とする文字データ（HTML エンティティ表、Unicode の文字分類と大文字小文字の畳み込み）、仕様の例による適合テストと病的な入力に対する計算量の保証である。

## Boundary Context
- **In scope**: thematic break、ATX / Setext 見出し、インデント / フェンスコードブロック、HTML ブロック、リンク参照定義、ブロック引用、リストとリスト項目、バックスラッシュエスケープ、エンティティ・数値文字参照、コードスパン、強調・強い強調、リンク、画像、オートリンク、生 HTML、ハード / ソフト改行の各構文。構文の単位での方言への追加と取り外し、CommonMark 方言プリセット、Markdown テキストから木と HTML を得る簡易 API。HTML エンティティ表と Unicode データとその再生成の手段。仕様の例の適合テスト、病的入力のテスト
- **Out of scope**: パーサエンジン本体と段落・テキスト・空行の扱い（parser-core が所有し、本仕様はそれに従う）、HTML / Markdown の出力規則（html-renderer / markdown-renderer）、標準ノード種別の定義（node-tree）、GFM の構文、CommonMark 仕様にない構文や独自の挙動、smart punctuation、ソース位置
- **Adjacent expectations**: parser-core の公開拡張 API だけで全構文を実装する。API が足りない場合は parser-core の API の改訂として扱い、裏口を作らない。木は node-tree の標準ノード種別だけで構成し、HTML への変換は html-renderer に委ねる。期待 HTML との差がレンダラの規則に起因する場合は html-renderer を修正する。エラーコード・アロケータ・文字列の所有権は foundation の規約に従う

## Requirements

### Requirement 1: 構文単位の規則
**Objective:** As a 方言を組み立てる利用者, I want CommonMark の各構文を、どの方言の一部かを意識せずに「強調」「フェンスコード」のような個別の規則として方言に加えたり外したりしたい, so that 生 HTML なしやインデントコードなしのような用途に合った方言や、自作の構文と組み合わせた方言を作れる

#### Acceptance Criteria
1. The CommonMark Module shall 次の 17 の単位を、それぞれ規則ごとの公開関数で、方言プリセットを介さずに独立した拡張として生成できるようにする: thematic break、ATX 見出し、Setext 見出し、インデントコードブロック、フェンスコードブロック、HTML ブロック、リンク参照定義、ブロック引用、リスト（リストとリスト項目）、バックスラッシュエスケープ、エンティティ・数値文字参照、コードスパン、強調（強調と強い強調）、リンク（リンクと画像）、オートリンク、生 HTML、改行（ハード改行とソフト改行）
2. If 規則の生成関数に出力先として空ポインタが渡される, the CommonMark Module shall 拡張を生成せず不正な引数のエラーを返す
3. Where 方言がある規則を含まない, the CommonMark Module shall その構文の記法を、その構文が存在しない場合の残りの構文の規則で解釈する（例: HTML ブロックがなければ `<div>` の行を段落のテキストとして扱う）
4. The CommonMark Module shall 新しいノード種別を定義せず、node-tree の標準ノード種別だけで木を作る
5. The CommonMark Module shall 各規則の拡張名を、方言の名前を含まない固定の名前（例: `emphasis`、`html_block`）として公開ヘッダの定数で提供し、利用者がその名前で方言から取り外せるようにする
6. The CommonMark Module shall 各構文を、ライブラリ外の拡張と同じ公開拡張 API だけで提供し、利用者が CommonMark の拡張と自作の拡張を同じ方言に混在させ、優先順位を変更できるようにする
7. The CommonMark Module shall 規則を方言プリセットに依存させず、CommonMark 方言プリセットを規則の公開 API だけで組み立てる（将来の GFM などのプリセットと利用者が、同じ規則を再利用できるようにする）

### Requirement 2: CommonMark 方言プリセット
**Objective:** As a ライブラリ利用者, I want 1 回の呼び出しで CommonMark 方言を得たい, so that 規則の一覧を知らなくても仕様どおりにパースできる

#### Acceptance Criteria
1. When 利用者が CommonMark 方言の生成を要求する, the CommonMark Module shall 17 の規則をすべて含む、まだ凍結されていない方言を返す
2. When 利用者が生成した方言から規則の拡張名を指定して取り外す, the CommonMark Module shall 残りの規則だけで解析される方言にする（例: `html_block` を外した CommonMark 方言）
3. While 生成した方言がまだ凍結されていない, the CommonMark Module shall 利用者がその方言に他の拡張を追加できるようにする
4. If 方言の生成の途中で失敗する, the CommonMark Module shall 作りかけの方言と拡張をすべて解放し、方言を返さずそのエラーを返す

### Requirement 3: 簡易 API
**Objective:** As a ライブラリ利用者, I want Markdown テキストから木または HTML を 1 回の呼び出しで得たい, so that 方言とパーサの寿命を管理せずに変換できる

#### Acceptance Criteria
1. When 利用者が入力全体を指定して CommonMark の簡易パースを要求する, the CommonMark Module shall 全構文の CommonMark 方言で解析した文書を呼び出し側の所有として返し、方言を利用者が保持しなくても文書が有効であるようにする
2. When 利用者が入力全体と HTML の出力設定を指定して HTML への簡易変換を要求する, the CommonMark Module shall 全構文の CommonMark 方言で解析し、その設定で HTML に変換した結果を出力先の末尾に追記する
3. When 解析オプションまたは HTML の出力設定が省略される, the CommonMark Module shall それぞれの既定値を使う（HTML は生 HTML と危険な URL を出力しない既定値）
4. If 必須の引数に空ポインタが渡される、または長さが 1 以上の入力に参照先がない, the CommonMark Module shall 何も変更せず不正な引数のエラーを返す
5. If 簡易 API の途中で失敗する, the CommonMark Module shall 文書を返さず、出力先を呼び出し前の内容に保ち、確保したものをすべて解放してそのエラーを返す
6. When 簡易パースと、全構文の方言を使った通常のパースに同じ入力を与える, the CommonMark Module shall 同一の木を返す

### Requirement 4: コンテナブロック
**Objective:** As a ライブラリ利用者, I want ブロック引用とリストを CommonMark 0.31.2 のとおりに解釈してほしい, so that 入れ子の文書構造が正しく木になる

#### Acceptance Criteria
1. When 行が字下げ 3 以下の `>` で始まる, the CommonMark Module shall ブロック引用を開始または継続し、`>` の直後の 1 桁の空白（タブの一部を含む）を記号の一部として読み飛ばす
2. When 行が字下げ 3 以下の箇条書きの記号（`-`、`+`、`*`）または 1〜9 桁の数字と `.` か `)` の後に空白か行末が続く, the CommonMark Module shall CommonMark 0.31.2 の規則でリスト項目を開始し、内容の字下げの桁をその規則（記号の後の空白が 5 桁以上または行末なら記号の幅 + 1）で決める
3. When リスト項目が、開いているリストと異なる種類の記号（箇条書きの文字、または番号付きの区切り文字）で始まる, the CommonMark Module shall 開いているリストを閉じて新しいリストを開始する
4. While 段落を中断する位置である, the CommonMark Module shall 内容が空のリスト項目と、開始番号が 1 以外の番号付きリストの項目を開始しない
5. When リストが閉じられる, the CommonMark Module shall CommonMark 0.31.2 の規則（項目の間、または項目の直接の子の間に空行がある場合に loose）で tight / loose を判定してリストのノードに設定する
6. The CommonMark Module shall 番号付きリストの開始番号と、箇条書きか番号付きかの種別をリストのノードに設定する
7. When 行がブロック引用またはリスト項目の継続の条件を満たさない, the CommonMark Module shall そのブロックを継続せず、段落の遅延継続行の扱いは parser-core の規則に委ねる

### Requirement 5: 葉ブロック
**Objective:** As a ライブラリ利用者, I want 見出し・区切り線・コードブロック・HTML ブロックを CommonMark 0.31.2 のとおりに解釈してほしい, so that 文書の各ブロックが仕様どおりの種別と値を持つ

#### Acceptance Criteria
1. When 行が字下げ 3 以下で、空白を挟んでよい同じ 3 個以上の `*`、`-`、`_` だけからなる, the CommonMark Module shall 区切り線のノードを作る。ただし Setext 見出しの下線として解釈できる場合は Setext 見出しを優先する
2. When 行が字下げ 3 以下の 1〜6 個の `#` と空白または行末で始まる, the CommonMark Module shall `#` の個数を見出しレベルとする見出しを作り、前後の空白と閉じの `#` の並びを除いた残りをインライン内容とする
3. When 段落の直後の行が字下げ 3 以下の `=` または `-` の並び（前後の空白を除く）で、その行が遅延継続行でない, the CommonMark Module shall 段落の行（リンク参照定義を除いた残り）を内容とするレベル 1（`=`）またはレベル 2（`-`）の見出しにする
4. When 字下げ 4 以上の空行でない行が段落の継続でも遅延継続の候補でもない位置にある, the CommonMark Module shall インデントコードブロックを開始し、各行から 4 桁を除いた内容を蓄え、末尾の空行を除いた内容をコードブロックの literal とする
5. When 行が字下げ 3 以下の 3 個以上の `` ` `` または `~` で始まる（`` ` `` のフェンスでは info に `` ` `` を含まない）, the CommonMark Module shall フェンスコードブロックを開始し、エスケープと文字参照を解決した info をノードに設定し、同じ文字で開きの長さ以上の閉じフェンス、またはコンテナの終わりまでの行を、開きのフェンスの字下げ分を除いて literal とする
6. When 行が CommonMark 0.31.2 の HTML ブロックの 7 種類の開始条件のいずれかを満たす, the CommonMark Module shall HTML ブロックを開始し、種類ごとの終了条件を満たす行（種類 6・7 は空行の直前の行）までの行をそのまま literal とする。種類 7 は段落を中断しない
7. When 段落の先頭の行が CommonMark 0.31.2 のリンク参照定義（ラベル、宛先、省略可能な title）を構成する, the CommonMark Module shall その定義を記録して段落から取り除き、同じ正規化ラベルの定義が既にあれば最初の定義を保つ
8. If リンク参照定義のラベルが 999 文字を超える、空白だけからなる、またはエスケープされていない角括弧を含む, the CommonMark Module shall それを定義として扱わず段落の内容として残す

### Requirement 6: テキスト系のインライン構文
**Objective:** As a ライブラリ利用者, I want エスケープ・文字参照・コードスパン・改行・生 HTML を CommonMark 0.31.2 のとおりに解釈してほしい, so that 本文の文字が仕様どおりのテキストとノードになる

#### Acceptance Criteria
1. When `\` の直後が ASCII の句読点文字である, the CommonMark Module shall その句読点文字をテキストとして扱い、他の構文の記号として解釈しない
2. When `&` で始まる並びが HTML5 の名前付き文字参照（`;` で終わる名前）、10 進の数値文字参照（1〜7 桁）、16 進の数値文字参照（1〜6 桁）のいずれかである, the CommonMark Module shall それを対応する文字のテキストにし、コードポイント 0、サロゲート、U+10FFFF を超える値は U+FFFD にする
3. If `&` で始まる並びが文字参照として成立しない, the CommonMark Module shall それを元の文字のままテキストとして扱う
4. When 同じ長さの `` ` `` の並びで囲まれた範囲がある, the CommonMark Module shall 行末をスペースに置き換え、前後の両方にスペースがあり内容がスペースだけでなければ前後の 1 個ずつを除いた内容をコードスパンの literal とする
5. If 開きの `` ` `` の並びと同じ長さの閉じの並びがない, the CommonMark Module shall 開きの並びをテキストとして扱う
6. When 行末の直前に 2 個以上のスペース、または `\` がある, the CommonMark Module shall ハード改行のノードを作り、それ以外の行末はソフト改行のノードとし、行末の直前のスペースをテキストから除く
7. When `<` で始まる並びが CommonMark 0.31.2 の開きタグ、閉じタグ、コメント、処理命令、宣言、CDATA のいずれかである, the CommonMark Module shall その並びをそのまま literal とする生 HTML のノードを作る
8. When `<` と `>` で囲まれた範囲が CommonMark 0.31.2 の絶対 URI またはメールアドレスである, the CommonMark Module shall それをオートリンクとし、URI はそのまま、メールアドレスは `mailto:` を前置した宛先と、元の文字列のテキストを子に持つリンクを作る

### Requirement 7: 強調・リンク・画像
**Objective:** As a ライブラリ利用者, I want 強調・リンク・画像を CommonMark 0.31.2 の優先順位と入れ子の規則のとおりに解釈してほしい, so that 複雑な組み合わせでも仕様どおりの木になる

#### Acceptance Criteria
1. When `*` または `_` の並びがある, the CommonMark Module shall CommonMark 0.31.2 の left-flanking / right-flanking の規則（Unicode の空白文字と、一般カテゴリ P または S の文字を句読点とする）と `_` の単語内の制限で、開けるか閉じられるかを決める
2. When 開ける並びと閉じられる並びが対応する, the CommonMark Module shall rule of 3 を守り、両側に 2 個以上残っていれば強い強調、そうでなければ強調のノードを作る
3. When `[` で始まりそれに対応する `]` の直後にインラインの宛先と title が続く、または定義済みのラベルを指す完全・省略・ショートカットの参照が続く, the CommonMark Module shall 宛先と title（エスケープと文字参照を解決したもの）を持つリンクを作り、括弧の中の内容をその子とする
4. When `![` で始まる範囲が同じ条件を満たす, the CommonMark Module shall 画像のノードを作り、括弧の中の内容を代替テキストの元となる子とする
5. When リンクが作られる, the CommonMark Module shall それより前にあるリンクの開き括弧をリンクの開始として使えないようにし、リンクの中にリンクを作らない（画像の中のリンクと、リンクの中の画像は作る）
6. If 閉じ括弧に対応する宛先も定義済みの参照もない, the CommonMark Module shall 開き括弧と閉じ括弧をテキストとして扱い、括弧の中の内容は通常どおり解釈する
7. The CommonMark Module shall 参照の照合で、ラベルを Unicode の大文字小文字の畳み込みと連続する空白の 1 個のスペースへの置き換えで正規化して比べる
8. The CommonMark Module shall コードスパン、オートリンク、生 HTML を、強調とリンクの括弧より優先して左から順に解釈する

### Requirement 8: 仕様適合
**Objective:** As a Amark の開発者, I want 固定した版の CommonMark 仕様の例がすべて通ることを自動で確かめたい, so that 仕様からの逸脱を早期に検出できる

#### Acceptance Criteria
1. The CommonMark Module shall 対象とする CommonMark 仕様の版を 0.31.2 に固定し、その spec.txt を改変せず、ライセンス（CC BY-SA 4.0）と出典を示す表記とともにリポジトリのテスト側に保持する
2. When 適合テストが実行される, the CommonMark Test Suite shall spec.txt からすべての例を抽出し（例中の `→` はタブに戻す）、抽出した例の数が固定した版の例の数と一致することを確かめる
3. When 各例を検査する, the CommonMark Test Suite shall 全構文の CommonMark 方言で解析し、生 HTML と危険な URL を許可した HTML の出力設定で変換した結果を、期待 HTML とバイト単位で比較する
4. The CommonMark Module shall 固定した版のすべての例で期待 HTML と一致する出力を生成する
5. If ある例が一致しない, the CommonMark Test Suite shall 失敗とし、例の番号、節、入力、期待 HTML、実際の出力を報告する
6. When 各例の入力を 1 バイトずつ供給して解析する, the CommonMark Module shall 一括で解析した場合と同一の HTML を生成する
7. The CommonMark Module shall 例の抽出・比較・報告のためのコードをライブラリ本体に含めず、テスト側に置く

### Requirement 9: 病的な入力
**Objective:** As a 信頼できない入力を扱う利用者, I want 悪意ある入力でも解析が入力長に対して線形に近い時間とメモリで終わってほしい, so that 外部から受け取った文書を安全に変換できる

#### Acceptance Criteria
1. The CommonMark Module shall 各構文の処理時間を、その構文が読み進めた入力の長さに比例させ、解析全体を入力の長さに対して線形に近い時間で完了する
2. When cmark の病的入力テストの各類型（入れ子の強調、開きだけ・閉じだけのデリミタやブラケット、rule of 3 の組み合わせ、閉じのないリンク・コメント・CDATA・処理命令・宣言、長さの異なる `` ` `` の並びの連続、深い入れ子のブロック引用とリスト、空行の多い深いリスト、大量の参照定義と参照）に相当する入力が与えられる, the CommonMark Module shall それぞれを規定の時間内に解析し、規定の HTML を生成する
3. While ブロックの入れ子が解析オプションの上限に達している, the CommonMark Module shall parser-core の規則どおりそれより深いブロックを開かず、残りを段落のテキストとして扱った HTML を生成する
4. If 参照リンクを通じて複製される宛先と title の合計の長さが、`100000` バイトとそれまでにインライン解析した内容の合計の長さの大きい方を超える, the CommonMark Module shall それ以降の参照リンクを作らず、括弧をテキストとして扱う
5. The CommonMark Module shall インラインの宛先の中の括弧の入れ子を 32 段までとし、それを超える宛先をリンクの宛先として扱わない
6. The CommonMark Module shall 解析中に確保するメモリの総量を、入力の長さと作るノードの数に比例する範囲に収める

### Requirement 10: CommonMark が必要とするデータ
**Objective:** As a Amark の保守者, I want エンティティ表と Unicode データを固定した出典から再現可能に生成したい, so that データの更新とライセンスの確認を確実に行える

#### Acceptance Criteria
1. The CommonMark Module shall WHATWG の HTML 標準の名前付き文字参照の一覧のうち `;` で終わるすべての名前と、それぞれの 1〜2 個のコードポイントを持つ表をライブラリに含める
2. The CommonMark Module shall 句読点（一般カテゴリ P と S）、Unicode の空白（一般カテゴリ Zs とタブ・LF・FF・CR）、大文字小文字の畳み込み（CaseFolding の C と F）の表を、固定した版の Unicode 文字データベース（16.0.0）から作ってライブラリに含める
3. The CommonMark Module shall 表を出典のデータから生成する手段をライブラリ本体とは別にリポジトリに置き、生成した表に出典の URL・版・ハッシュ値を記録し、同じ出典から同じ表を再生成できるようにする
4. The CommonMark Module shall ライブラリのビルドに生成の手段も外部のデータも必要としない
5. The CommonMark Module shall エンティティ表（CC BY 4.0）と Unicode データ（Unicode License v3）の出典とライセンスの表記を、生成した表と第三者のライセンス表記の一覧に記す

### Requirement 11: エラーと資源
**Objective:** As a ライブラリ利用者, I want 失敗が共通のエラーコードで報告され、失敗してもメモリが漏れないでほしい, so that ライブラリ全体で一貫したエラー処理が書ける

#### Acceptance Criteria
1. The CommonMark Module shall すべての失敗を foundation の共通エラーコードで返し、独自のエラー体系を作らない
2. The CommonMark Module shall すべての動的確保を foundation のグローバルアロケータ経由で行う
3. If 解析中または方言・拡張の生成中にメモリの確保が失敗する, the CommonMark Module shall 確保失敗のエラーを返し、それまでに確保したものを漏らさない
4. The CommonMark Module shall 入力として妥当な UTF-8 を受け取った構文が木に置くすべての文字列を妥当な UTF-8 にする

### Requirement 12: 並行利用
**Objective:** As a マルチスレッドのアプリケーションの開発者, I want 同じ CommonMark 方言や簡易 API を複数のスレッドから使いたい, so that 変換を並列に行える

#### Acceptance Criteria
1. The CommonMark Module shall 可変のグローバル状態を持たない
2. While CommonMark の方言が凍結されている, the CommonMark Module shall その方言を使う異なるパーサを別々のスレッドから同時に使えるようにする
3. The CommonMark Module shall 簡易 API を、異なる入力と出力先に対して複数のスレッドから同時に呼べるようにする
