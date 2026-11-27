# Requirements Document

## Project Description (Input)
パーサの出力先であり、レンダラの入力であり、利用者が直接編集する対象でもある「木」のデータ構造と API が必要である。ここが定まらないとパーサもレンダラも着手できない。現在は `foundation` がアロケータ、エラーコード、バッファ・文字列ユーティリティを提供する想定で、ノードに関するコードは存在しない。

ライブラリの利用者、パーサ（parser-core / commonmark）とレンダラ（html-renderer / markdown-renderer）の実装者、および拡張の作者のために、次の状態を実現する。

- Markdown 文書を表す木を、パーサを介さず API だけで組み立てられる
- ノードの値（見出しレベル、リンク先、コードの中身など）は木が所有し、アクセサで読み書きできる（cmark 方式）
- 木を編集（挿入、取り外し、置換、削除）でき、不正な親子関係はエラーとして拒否される
- 木を走査できる（親・子・兄弟の取得、enter / exit イベント型のイテレータ）。編集中のイテレータの扱い（どの操作が安全か）が明確に定義されている
- 拡張が独自のノード種別（名前、ブロック/インラインの別、子に取れる種別、付随データの後片付け）を登録して同じ木に載せられる
- 隣接テキストノードの結合などの木の正規化ができる

値の文字列はノードが複製して所有し、確保はすべて `amark_*` アロケータを経由する。失敗は `foundation` の共通エラーコードで返し、必要なコードはそこへ追加する。テキストから木を作ること、木から文字列を出力すること、ソース位置を前提としたロスレス表現、GFM のノード種別そのものは対象外とする。

## Introduction
本仕様は Amark の文書木（ノード木）を定義する。対象は、opaque なノードの生成・破棄と値の所有、CommonMark に必要な標準ノード種別とその値のアクセサ、拡張ノード種別の登録と付随データ、親子関係の規則に基づく編集と検証、親・子・兄弟の参照と enter / exit 型の走査、走査中の編集の安全性、テキストノードの正規化、およびレンダラが共有する非公開の振り分け走査（種別ごとの処理の表と走査の駆動部）である。node-tree はパーサとレンダラの共通の土台であり、レンダラはパーサなしに手組みの木だけで、パーサはレンダラなしに木だけで検証できるようにする。

## Boundary Context
- **In scope**: ノードの生成・破棄と所有の規則、標準ノード種別（document、block quote、list、item、code block、HTML block、paragraph、heading、thematic break、text、soft break、line break、code span、emphasis、strong、link、image、HTML inline）とその値、拡張ノード種別の登録場所（レジストリ）と付随データ、親子関係の規則と受け入れ可否の問い合わせ、編集（子の追加、前後への挿入、取り外し、置換、部分木の破棄）、参照と走査、走査中の編集の安全性の定義、テキストノードの正規化、種別ハンドルをキーとする処理の表とそれを引きながら走査する非公開の駆動部（レンダラの内部で共有する）
- **Out of scope**: テキストから木を作ること（parser-core / commonmark）、木から文字列を出力すること、レンダラの公開のフック機構（フックの型、コンテキスト、登録の API）、Markdown の行の書き出し（html-renderer / markdown-renderer）、振り分け走査の公開、ソース位置の保持とロスレス表現、構文に固有の解析中状態（開いているブロック、デリミタスタックなど）をノードに持たせること、原文の記法（強調記号の種類、見出しの形式、フェンスの文字など）の記憶、CommonMark の構文上の制約のうち構造の妥当性に関わらないもの（リンクの入れ子禁止など。パーサが守る）、GFM のノード種別そのもの、木の複製・比較
- **Adjacent expectations**: foundation の共通エラーコード列挙、グローバルアロケータ、文字列の所有権規約（借用は長さ付きスライス、所有は NUL 終端付き）に従う。parser-core は本仕様の種別登録を、構文登録と一体の「拡張」として包んで使えること。html-renderer / markdown-renderer は、標準種別は識別値で、拡張種別は登録で得た種別の同一性でノード種別ごとの処理を選べること

## Requirements

### Requirement 1: ノードの生成と破棄
**Objective:** As a ライブラリ利用者, I want パーサを介さずに任意の種別のノードを生成し、不要になったら破棄したい, so that API だけで文書の木を組み立てられる

#### Acceptance Criteria
1. When 利用者が標準ノード種別を指定してノードの生成を要求する, the Node Tree Module shall 親・子・兄弟を持たない、その種別の新しいノードを返す
2. When 利用者が登録済みの拡張ノード種別を指定してノードの生成を要求する, the Node Tree Module shall 親・子・兄弟を持たない、その種別の新しいノードを返す
3. If 生成を要求された種別が標準ノード種別でも登録済みの拡張ノード種別でもない, the Node Tree Module shall ノードを生成せず不正な引数のエラーを返す
4. When 利用者がノードの破棄を要求する, the Node Tree Module shall そのノードと子孫をすべて破棄し、それらが所有する値と付随データをすべて解放する
5. When 親を持つノードの破棄を要求される, the Node Tree Module shall 先にそのノードを親から取り外し、残りの木を有効な状態に保つ
6. The Node Tree Module shall 親を持たないノード（根）を呼び出し側の所有とし、親を持つノードをその親の所有とする
7. When 空ポインタの破棄を要求される, the Node Tree Module shall 何もしない

### Requirement 2: 標準ノード種別
**Objective:** As a パーサ・レンダラの実装者, I want CommonMark の文書を表すのに必要なノード種別が標準で定義されていてほしい, so that パーサとレンダラが同じ種別の集合を前提に独立して実装できる

#### Acceptance Criteria
1. The Node Tree Module shall 標準ブロック種別として document、block quote、list、item、code block、HTML block、paragraph、heading、thematic break を提供する
2. The Node Tree Module shall 標準インライン種別として text、soft break、line break、code span、emphasis、strong、link、image、HTML inline を提供する
3. The Node Tree Module shall 各標準種別に、ライブラリの版を通じて変わらない識別値と、種別間で重複しない名前を与える
4. When 利用者がノードの種別を問い合わせる, the Node Tree Module shall 標準種別のノードにはその種別の識別値を、拡張種別のノードには拡張種別であることを示す共通の識別値を返す
5. When 利用者がノードの種別の名前、ブロック/インラインの別を問い合わせる, the Node Tree Module shall 標準種別と拡張種別のいずれにも同じ方法で答える
6. When 利用者が標準種別の識別値から種別そのものを問い合わせる, the Node Tree Module shall どのレジストリにも依存せずに、プロセス内で常に同一の標準種別を返す
7. When 利用者が種別が子を取れない種別（葉）かどうかを問い合わせる, the Node Tree Module shall 標準種別と拡張種別のいずれにも同じ方法で、走査で enter の 1 回だけ報告される種別に限り真を、それ以外と空ポインタには偽を、公開 API として返す

### Requirement 3: ノードの値とアクセサ
**Objective:** As a ライブラリ利用者, I want ノードの値を種別ごとのアクセサで読み書きしたい, so that 値は木が所有し、利用者は文字列の寿命を管理せずに済む

#### Acceptance Criteria
1. The Node Tree Module shall heading の見出しレベル、list のリスト種別（箇条書き/番号付き）・開始番号・tight か否か、code block の info string と内容、HTML block・text・code span・HTML inline の内容、link と image のリンク先とタイトルを、種別ごとに保持し読み書きできるようにする
2. When 利用者が文字列の値を設定する, the Node Tree Module shall 渡された文字列を複製して所有し、設定後に呼び出し側の文字列が変更・解放されても値が変わらないようにする
3. When 利用者が文字列の値を読み出す, the Node Tree Module shall ノードが所有する値を、長さ付きで末尾が NUL の借用文字列として返し、その値が次に変更されるかノードが破棄されるまで有効とする
4. The Node Tree Module shall 生成直後のノードの値を、見出しレベル 1、箇条書きのリスト、開始番号 1、tight でない、文字列の値はすべて空文字列とする
5. If 利用者が見出しレベルに 1〜6 以外を、リストの開始番号に 0〜999999999 以外を、リスト種別に未定義の値を設定しようとする, the Node Tree Module shall 値を変更せず不正な引数のエラーを返す
6. If 設定しようとする文字列が UTF-8 として不正である, the Node Tree Module shall 値を変更せず不正な UTF-8 のエラーを返す
7. If 利用者がその値を持たない種別のノードに値を設定しようとする, the Node Tree Module shall ノードを変更せず不正な引数のエラーを返す
8. When 利用者がその値を持たない種別のノードから値を読み出す, the Node Tree Module shall 値がないことを示す結果（文字列なら参照先を持たない空の借用文字列、数値と真偽値なら 0 と偽）を返す
9. The Node Tree Module shall NUL バイトを含む文字列の値を、長さと内容を保ったまま保持する

### Requirement 4: 拡張ノード種別の登録
**Objective:** As a 拡張の作者, I want 独自のノード種別を名前、ブロック/インラインの別、子の受け入れ規則、付随データの後片付け方法とともに登録したい, so that GFM などの拡張が標準種別と同じ木に独自のノードを載せられる

#### Acceptance Criteria
1. The Node Tree Module shall 拡張ノード種別を登録する場（レジストリ）を、利用者が生成・破棄する独立したオブジェクトとして提供し、プロセス全体で共有される可変の登録状態を持たない
2. When 拡張の作者がレジストリに名前、ブロック/インラインの別、子の受け入れ規則、付随データの後片付け方法を指定して種別を登録する, the Node Tree Module shall 新しい拡張ノード種別を作り、その種別を指すハンドルを返す
3. If 登録しようとする名前が、同じレジストリの既存の拡張種別または標準種別の名前と一致する, the Node Tree Module shall 登録せず既に存在することを示すエラーを返す
4. If 名前が空である、64 バイトを超える、または英小文字で始まり英小文字・数字・下線だけからなる形式でない, the Node Tree Module shall 登録せず不正な引数のエラーを返す
5. If 子の受け入れ規則の指定が不完全または矛盾している（独自判定を宣言して判定処理がない、など）, the Node Tree Module shall 登録せず不正な引数のエラーを返す
6. When 利用者がレジストリに名前で種別を問い合わせる, the Node Tree Module shall 一致する拡張種別または標準種別を返し、一致するものがなければ見つからないことを示す空ポインタを返す
7. The Node Tree Module shall 登録された種別のハンドルを、レジストリが破棄されるまで同一のまま有効に保ち、ハンドルの一致によって種別を識別できるようにする
8. The Node Tree Module shall 登録時に渡された名前などの値を複製して保持し、呼び出し側の領域の寿命に依存しない
9. When レジストリが破棄される, the Node Tree Module shall 登録された拡張種別をすべて解放する
10. The Node Tree Module shall レジストリをそのレジストリの種別のノードがすべて破棄されるまで破棄してはならないという利用者の義務を、利用者向けの文書に明記する

### Requirement 5: 拡張ノードの付随データ
**Objective:** As a 拡張の作者, I want 拡張種別のノードに独自のデータを持たせ、ノードの寿命に合わせて後片付けさせたい, so that 拡張固有の値（表の列揃えなど）を木の一部として扱える

#### Acceptance Criteria
1. When 利用者が拡張種別のノードに付随データを設定する, the Node Tree Module shall その付随データの所有権を引き取り、ノードから読み出せるようにする
2. When 付随データを持つノードが破棄される、または付随データが別のデータで置き換えられる, the Node Tree Module shall その種別の登録時に指定された後片付けの処理を古い付随データに対して 1 回だけ呼ぶ
3. When 現在と同じ付随データが再び設定される, the Node Tree Module shall 後片付けの処理を呼ばずに成功を返す
4. Where 種別の登録時に後片付けの処理が指定されていない, the Node Tree Module shall 付随データを解放せずに手放す
5. If 利用者が標準種別のノードに付随データを設定しようとする, the Node Tree Module shall ノードを変更せず不正な引数のエラーを返す
6. When 付随データが設定されていないノードから付随データを読み出す, the Node Tree Module shall データがないことを示す空ポインタを返す

### Requirement 6: 親子関係の規則
**Objective:** As a ライブラリ利用者, I want 不正な親子関係をエラーとして拒否してほしい, so that 木が常にレンダラが解釈できる構造に保たれる

#### Acceptance Criteria
1. The Node Tree Module shall 標準種別の親子関係を、document・block quote・item は任意のブロックを、list は item だけを、paragraph・heading・emphasis・strong・link・image は任意のインラインを子に取れ、code block・HTML block・thematic break・text・soft break・line break・code span・HTML inline は子を取れない、という規則で判定する
2. The Node Tree Module shall document をどのノードの子にもできないようにし、item を、item を明示的に受け入れる親（list、または独自判定で受け入れる拡張種別）以外の子にできないようにする
3. Where 拡張種別が子の規則として「任意のブロック」「任意のインライン」「子なし」のいずれかを宣言している, the Node Tree Module shall その規則で子を判定する
4. Where 拡張種別が独自の判定処理を宣言している, the Node Tree Module shall 子を受け入れるかどうかをその判定処理の結果で決める
5. Where 拡張種別が「受け入れを明示した親の下にだけ置ける」と宣言している, the Node Tree Module shall 任意のブロックまたは任意のインラインを受け入れる親の下にその種別を置くことを拒否する
6. When 利用者が親の種別と子の種別の組を指定して受け入れ可否を問い合わせる, the Node Tree Module shall 木を変更せずに、編集操作と同じ判定結果を返す
7. If 編集操作によって親が子の種別を受け入れない関係ができる, the Node Tree Module shall 操作を行わず不正な子であることを示すエラーを返す
8. If 編集操作によってノードが自分自身または自分の子孫の子になる, the Node Tree Module shall 操作を行わず不正な子であることを示すエラーを返す

### Requirement 7: 木の編集
**Objective:** As a ライブラリ利用者, I want ノードを挿入・取り外し・置換・削除して木を書き換えたい, so that 文書を機械的に変換できる

#### Acceptance Criteria
1. When 利用者が子の末尾追加または先頭追加を要求する, the Node Tree Module shall ノードを指定した親の最後または最初の子にする
2. When 利用者が兄弟の前または後への挿入を要求する, the Node Tree Module shall ノードを指定した兄弟の直前または直後に、その兄弟と同じ親の子として置く
3. When 挿入するノードが既に別の親の下にある, the Node Tree Module shall 検証に成功した後で元の位置から取り外し、新しい位置に置く（移動）
4. When 利用者が親を持つノードの取り外しを要求する, the Node Tree Module shall ノードを子孫ごと親と兄弟から切り離して根にし、所有権を呼び出し側に移し、元の前後の兄弟同士をつなぎ直す
5. When 利用者が親を持たないノードの取り外しを要求する, the Node Tree Module shall 何もしない
6. When 利用者がノードの置換を要求する, the Node Tree Module shall 置換するノードを元のノードの位置に置き、元のノードを子孫ごと取り外して呼び出し側の所有とする
7. If 兄弟への挿入または置換の基準となるノードが親を持たない、または挿入・置換するノードが基準ノードと同一である, the Node Tree Module shall 操作を行わず不正な引数のエラーを返す
8. If 編集操作が失敗する, the Node Tree Module shall 関係するすべてのノードの位置と値を呼び出し前のまま保つ

### Requirement 8: 木の参照
**Objective:** As a ライブラリ利用者, I want ノードの親・子・兄弟をたどりたい, so that 木の任意の位置を調べられる

#### Acceptance Criteria
1. When 利用者がノードの親、最初の子、最後の子、次の兄弟、前の兄弟を問い合わせる, the Node Tree Module shall 該当するノードを返し、存在しない場合または空ポインタを渡された場合は空ポインタを返す
2. The Node Tree Module shall 参照の問い合わせによって木を変更しない
3. The Node Tree Module shall 子の並びを、編集操作で指定されたとおりの順序に保つ

### Requirement 9: 走査
**Objective:** As a レンダラの実装者, I want 木を enter / exit のイベント列として走査したい, so that 再帰を書かずにノード種別ごとの開始処理と終了処理を書ける

#### Acceptance Criteria
1. When 利用者がノードを起点に走査を開始し次のイベントを順に要求する, the Node Tree Module shall 起点の部分木のノードを深さ優先の文書順に、子を取れる種別のノードは enter と exit の 2 回、子を取れない種別のノードは enter の 1 回だけ報告する
2. When 子を持たない「子を取れる種別」のノードに到達する, the Node Tree Module shall そのノードの enter と exit を続けて報告する
3. When 起点の部分木のすべてのイベントを報告し終える, the Node Tree Module shall 走査の終了を報告し、以後の要求にも終了を報告し続ける
4. The Node Tree Module shall 起点の部分木の外にあるノード（起点の兄弟や祖先）を報告しない
5. When 利用者が走査の現在位置を起点の部分木内のノードとイベントに設定し直す, the Node Tree Module shall 次の要求からその位置の続きを報告する
6. The Node Tree Module shall 走査の開始と進行にメモリ確保を必要とせず、走査の失敗が起こらないようにする

### Requirement 10: 走査中の編集
**Objective:** As a ライブラリ利用者, I want 走査しながら木を書き換えたい, so that 「特定の種別を見つけて置き換える」といった変換を 1 回の走査で書ける

#### Acceptance Criteria
1. While 走査中である, the Node Tree Module shall どのノードの値と付随データの変更も、走査の結果に影響させずに許す
2. While 走査中である, when 利用者が現在のノードとその祖先以外のノードを挿入・取り外し・置換・破棄する, the Node Tree Module shall 走査を継続できる状態に保ち、まだ報告していない位置を編集後の木に従って報告する
3. While 走査中で現在のイベントが enter である, when 利用者が現在のノードの子を追加・取り外しする, the Node Tree Module shall 編集後の子を報告する
4. When 利用者が現在のノードを取り外した後で走査位置を部分木内の有効なノードに設定し直す, the Node Tree Module shall その位置から走査を継続する
5. The Node Tree Module shall 走査中に安全な編集操作と、走査位置の設定し直しが必要になる編集操作（現在のノードまたはその祖先の取り外し・置換・破棄・移動）を、利用者向けの文書で区別して示す

### Requirement 11: 正規化
**Objective:** As a パーサの実装者・ライブラリ利用者, I want 隣接するテキストノードをまとめた正規の形に木を整えたい, so that 解析や編集で細切れになったテキストをレンダラと利用者が扱いやすくなる

#### Acceptance Criteria
1. When 利用者がノードを起点に正規化を要求する, the Node Tree Module shall 起点の部分木で兄弟として隣接する text ノードを、内容を順に連結した 1 つの text ノードに結合する
2. When 正規化する, the Node Tree Module shall 起点の部分木の子孫のうち内容が空の text ノードを取り除く
3. The Node Tree Module shall 正規化で text 以外のノードの種別、値、順序を変更しない
4. When 正規化済みの部分木を再度正規化する, the Node Tree Module shall 木を変更しない
5. If 正規化中にメモリ確保に失敗する, the Node Tree Module shall メモリ不足のエラーを返し、木を親子関係の規則を満たし、各兄弟並びの text の連結内容が正規化前と同じ状態に保つ

### Requirement 12: エラー処理とメモリ管理
**Objective:** As a ライブラリ利用者, I want 失敗が共通の方法で報告され、メモリが漏れないでほしい, so that Amark の他のモジュールと同じ流儀でエラーとメモリを扱える

#### Acceptance Criteria
1. The Node Tree Module shall 失敗を Amark の共通エラーコードで返し、独自のエラー体系を持たず、新たに必要なコード（不正な子、既に存在する）は共通のエラーコード列挙への追加として定義する
2. The Node Tree Module shall ノード、値、レジストリ、種別のメモリをすべて Amark のグローバルアロケータ経由で確保・解放する
3. If 正規化以外の操作でメモリ確保に失敗する, the Node Tree Module shall メモリ不足のエラーを返し、木・ノードの値・レジストリを呼び出し前のまま保ち、その操作で確保したメモリを残さない
4. If 必須の引数に空ポインタが渡される, the Node Tree Module shall 何も変更せず不正な引数のエラーを返す
5. If 文字列の複製または連結のサイズ計算があふれる, the Node Tree Module shall 何も変更せずサイズのあふれのエラーを返す
6. When 利用者が生成したすべての根とレジストリを破棄する, the Node Tree Module shall 木とレジストリのために確保したメモリをすべて解放済みにする

### Requirement 13: 頑健性と並行利用
**Objective:** As a ライブラリ利用者, I want 深い木や複数スレッドでも安全に使いたい, so that 信頼できない入力から作られた木やサーバ用途でも安心して組み込める

#### Acceptance Criteria
1. The Node Tree Module shall 木の深さに比例するスタックを使わずに破棄・走査・正規化を行い、深さ 10 万段の木でも完了する
2. The Node Tree Module shall 1 回の挿入・置換の計算量を、挿入先の深さに比例する範囲に収め、木全体の大きさに依存させない
3. The Node Tree Module shall 異なる木を別々のスレッドから同時に操作することを許す
4. While レジストリへの登録が行われていない, the Node Tree Module shall 同じレジストリの種別を使うノードの生成・操作（異なる木に対するもの）と名前の問い合わせを、複数スレッドから同時に行うことを許す

### Requirement 14: 種別ごとの振り分け走査（非公開）
**Objective:** As a レンダラの実装者（html-renderer / markdown-renderer）, I want 種別ハンドルをキーとする処理の表と、表を引きながら木を走査する共通の駆動部を使いたい, so that 両レンダラが ENTER / EXIT と子の省略の意味を複製せずに同じにできる

#### Acceptance Criteria
1. When 呼び出し側が種別ハンドルと処理を指定して表に設定する, the Node Tree Module shall 未登録の種別なら項目を追加し、登録済みの種別ならその項目の処理と利用者データを置き換える
2. When 呼び出し側が種別ハンドルと空の処理を指定して表に設定する, the Node Tree Module shall 登録済みの種別ならその項目を取り除き、未登録の種別なら表を変更せずに成功を返す
3. When 呼び出し側が種別ハンドルで表を検索する, the Node Tree Module shall ハンドルの同一性で一致する項目を返し、一致する項目がなければ「ない」ことを返す
4. If 表への設定で種別が指定されていない、または表の伸長で確保に失敗するかサイズ計算があふれる, the Node Tree Module shall 表を変更せず、不正な引数・メモリ不足・サイズのあふれのいずれかのエラーを返す
5. When 呼び出し側が起点のノード、表、訪問関数を指定して走査を実行する, the Node Tree Module shall 起点の部分木を、木の深さに比例するスタックや再帰を使わずに反復で走査し、各イベントについて現在のノード、イベント、表から引いた項目を訪問関数に渡し、木を変更しない
6. The Node Tree Module shall 走査の実行で、子を取りうる種別のノードについて ENTER と EXIT の 2 回、子を取らない種別（葉）のノードについて ENTER の 1 回だけ訪問関数を呼ぶ
7. When 訪問関数が子を取りうる種別のノードの ENTER で子の省略を要求する, the Node Tree Module shall そのノードの子孫を訪問せず、続けて同じノードの EXIT をちょうど 1 回訪問し、以後はその EXIT の続きから走査を進める（ENTER 以外のイベントと葉での要求は無視する）
8. If 訪問関数がエラーを返す, the Node Tree Module shall 直ちに走査を中止し、そのエラーを呼び出し側へ返す
9. The Node Tree Module shall 表の伸長以外でメモリを確保せず（走査の実行は確保しない）、表の後片付けで表のために確保したメモリをすべて解放する
10. The Node Tree Module shall 振り分け走査を非公開の内部モジュールとし、公開ヘッダと動的ライブラリの公開シンボルに含めず、レンダラ固有の処理（出力の形式、行頭プレフィックス、折り返し）を持たない
