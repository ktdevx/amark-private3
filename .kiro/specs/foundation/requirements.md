# Requirements Document

## Project Description (Input)
Amark はゼロから作る C99 の Markdown ライブラリであり、ビルド・テスト・CI の土台と、全モジュールが共有する約束事（メモリ確保、エラーコード、命名、コーディング規約）がまだ存在しない。リポジトリには kiro の設定と CLAUDE.md しかなく、ソースコード、ビルド定義、CI、ライセンスファイルはいずれも未作成である。これらを各 spec がばらばらに決めると、エラーコードや API の流儀が分裂する。

ライブラリの開発者（後続 spec の実装者）と、Amark を組み込む利用者のために、次の状態を実現する。

- CMake で静的ライブラリと動的ライブラリの両方をビルドでき、インストールして `find_package` で利用できる
- Windows / Linux / macOS の CI でビルドとテストが通る
- Naos（v0.4.0、FetchContent でタグ固定）によるテストが CTest から実行できる
- 全モジュールが使う差し替え可能なグローバルアロケータ（`amark_malloc`, `amark_realloc`, `amark_calloc`, `amark_free` と差し替え API）、単一の列挙による共通エラーコードとその文字列化、共通ユーティリティ（可変長バッファ、文字列スライス、UTF-8 基本操作）が揃っている
- MIT ライセンス（copyright: The Amark Authors）とソースヘッダの表記が揃っている
- コーディング規約が文書化され、可能な範囲で機械的に検査できる

後続 spec が共通して依存するものだけを最小限で先に作る。エラーコードは単一の列挙として定義し、後続 spec が追加していく前提にする。ノード、パーサ、レンダラ、CLI ツール、他言語バインディングは対象外とする。

## Introduction
本仕様は Amark ライブラリの基盤を定義する。対象は、ビルドとパッケージング、テスト基盤、CI/CD、ライセンス表記、全モジュールが共有する横断的な約束事（グローバルアロケータ、単一のエラーコード列挙、公開 API の命名とヘッダの規約）、および後続 spec が必ず必要とする共通ユーティリティ（可変長バッファ、文字列スライス、UTF-8 基本操作）である。`foundation` はこれらの約束事の正本であり、後続 spec（node-tree、parser-core、html-renderer、commonmark、markdown-renderer）はここで定めたものに従い、必要なエラーコードやユーティリティはここへ追加する。

## Boundary Context
- **In scope**: リポジトリ構成、静的・動的ライブラリのビルド、インストールとパッケージ利用、バージョン情報、テスト基盤と CTest 連携、3 OS の CI とリリース手順、MIT ライセンスとソースヘッダ表記、グローバルアロケータ、共通エラーコード、可変長バッファ、文字列スライス、UTF-8 の基本操作（前方・逆方向の復号、符号化、検証）、公開ヘッダと命名の規約、整形設定とその検査
- **Out of scope**: ノード、パーサ、レンダラに関する一切。CLI ツール、他言語バインディング。特定の構文やノード種別に依存するユーティリティ。Unicode の文字分類・大文字小文字の畳み込み・正規化（必要になった spec が自分の非公開モジュールとして持つ。句読点・空白の分類は commonmark が提供する）。テスト専用のフックやテスト用アロケータをライブラリ本体に入れること
- **Adjacent expectations**: 後続 spec は本仕様のエラーコード列挙と、本仕様の設計にあるエラーコード一覧表（全コードの唯一の正本）に定数を追加し、独自のエラー体系を作らない。node-tree は本仕様が定める文字列の所有権規約（借用は長さ付きスライス、所有はアロケータで確保した NUL 終端付きの長さ付き文字列）に従う。後続 spec が追加するソースとテストは、本仕様のビルド定義・テスト登録・CI にそのまま載る

## Requirements

### Requirement 1: ライブラリのビルド
**Objective:** As a ライブラリ利用者, I want Amark を静的ライブラリと動的ライブラリのどちらでもビルドしたい, so that 自分のアプリケーションに合った形態で組み込める

#### Acceptance Criteria
1. When 利用者が既定の設定で CMake 3.21 以上による構成とビルドを行う, the Amark Build System shall Amark を静的ライブラリとしてビルドする
2. When 利用者が動的ライブラリを選択して構成とビルドを行う, the Amark Build System shall Amark を動的ライブラリ（Windows では DLL とインポートライブラリ）としてビルドする
3. The Amark Build System shall ライブラリ本体を C99 として、コンパイラ拡張を無効にしてコンパイルする
4. The Amark Build System shall 静的ライブラリと動的ライブラリの両方で同一の公開ヘッダを使えるようにし、利用者がヘッダを使い分ける必要をなくす
5. Where 動的ライブラリとしてビルドする, the Amark Build System shall 公開 API として宣言された関数だけを外部に公開し、翻訳単位をまたぐ非公開シンボルを公開しない
6. When CMake 3.21 未満で構成しようとする, the Amark Build System shall 構成を失敗させ、必要な CMake のバージョンを示す
7. Where 警告をエラーとして扱う設定が有効である, the Amark Build System shall ライブラリ本体とテストのコンパイル警告をエラーとして扱う

### Requirement 2: インストールとパッケージ利用
**Objective:** As a ライブラリ利用者, I want ビルドした Amark をインストールし、自分の CMake プロジェクトから見つけて使いたい, so that ソースを取り込まずに Amark へリンクできる

#### Acceptance Criteria
1. When 利用者がインストールを実行する, the Amark Build System shall ライブラリ本体、公開ヘッダ、CMake パッケージ設定、ライセンスファイルをインストール先へ配置する
2. When 利用者のプロジェクトがインストール済みの Amark を `find_package` で要求する, the Amark Build System shall 名前空間付きのターゲットを提供し、そのターゲットへのリンクだけで公開ヘッダの参照と必要なコンパイル定義の付与が行われるようにする
3. When 利用者のプロジェクトが互換性のないバージョンを `find_package` で要求する, the Amark Build System shall パッケージを見つからなかったものとして扱う
4. When 利用者のプロジェクトが Amark をサブディレクトリまたは FetchContent で取り込む, the Amark Build System shall インストール時と同じ名前空間付きのターゲット名で利用できるようにし、Amark 自身のテストとテスト用依存の取得を既定では行わない
5. The Amark Library shall ライブラリのバージョン（メジャー・マイナー・パッチ）を、コンパイル時に参照できる定数と、実行時に問い合わせられる関数の両方で提供する
6. The Amark Build System shall 公開ヘッダに記したバージョンの定数を唯一の出所とし、実行時に問い合わせられる値とパッケージ設定のバージョンをその定数に一致させる

### Requirement 3: テスト基盤
**Objective:** As a ライブラリ開発者, I want 共通のテスト基盤でテストを書き CTest から実行したい, so that すべての spec が同じ方法でテストを追加・実行できる

#### Acceptance Criteria
1. When 開発者が Amark をトップレベルプロジェクトとしてテスト有効で構成する, the Amark Build System shall Naos v0.4.0 をタグ固定で取得し、テスト実行ファイルをビルドする
2. When 開発者が CTest を実行する, the Amark Build System shall 各テスト実行ファイルに含まれる個々のテストを CTest のテストとして実行し、結果を報告する
3. Where 動的ライブラリとしてビルドする, the Amark Build System shall すべての OS でテスト実行ファイルが Amark の動的ライブラリを読み込んでテストの列挙と実行ができるようにする
4. The Amark Library shall テストのためだけに存在するコード（テスト用のフック、テスト専用の関数、テスト用の条件コンパイル、テスト用アロケータ）をライブラリ本体に含めない
5. The Amark Test Suite shall ライブラリの振る舞いを公開 API を通して検証する
6. The Amark Test Suite shall メモリ確保の失敗、確保・解放の釣り合い、確保バイト数（現在量・累計・最大）を、テスト側でグローバルアロケータを差し替えることによって検証できる補助機能を提供する
7. The Amark Test Suite shall 各公開ヘッダが単独で include でき、C++ からも利用できることを検証する

### Requirement 4: 継続的インテグレーション
**Objective:** As a ライブラリ開発者, I want 変更のたびに 3 OS で静的・動的の両形態がビルドとテストを通るか確認したい, so that プラットフォーム固有の不具合を早期に見つけられる

#### Acceptance Criteria
1. When プルリクエストが作成または更新される、あるいは既定ブランチへ push される, the Amark CI shall Windows、Linux、macOS のそれぞれで静的ライブラリと動的ライブラリの両方をビルドし、全テストを実行する
2. The Amark CI shall Linux では GCC と Clang の両方、macOS では Clang、Windows では MSVC でビルドする
3. The Amark CI shall 警告をエラーとして扱う設定でビルドする
4. When CI のジョブでビルドとテストが成功する, the Amark CI shall Amark をインストールし、インストール先を `find_package` で利用する最小の利用者プロジェクトをビルド・実行して成功を確認する
5. If いずれかのジョブでビルド、テスト、インストール確認、または規約検査が失敗する, the Amark CI shall その実行を失敗として報告する
6. When 開発者がバージョンを表すタグを push する, the Amark CI shall タグのバージョンと公開ヘッダに記したバージョンが一致することを確認し、全プラットフォームのビルドとテストが成功した場合に限りリリースを公開する
7. If push されたタグのバージョンが公開ヘッダに記したバージョンと一致しない, the Amark CI shall リリースを公開せずに失敗を報告する

### Requirement 5: ライセンスと著作権表記
**Objective:** As a ライブラリ利用者, I want ライセンスと著作権者が明確であってほしい, so that 安心して組み込み・再配布できる

#### Acceptance Criteria
1. The Amark Repository shall MIT ライセンスの全文を、著作権者 `The Amark Authors` の表記付きでリポジトリ直下に置く
2. The Amark Repository shall ライブラリ本体、公開ヘッダ、テストの各ソースファイルの先頭に、著作権者 `The Amark Authors` と MIT ライセンスを示す定型のヘッダ表記を置く
3. If ソースヘッダ表記を欠くソースファイルが存在する, the Amark Test Suite shall そのファイル名を示して検査を失敗させる

### Requirement 6: グローバルアロケータ
**Objective:** As a ライブラリ利用者, I want Amark が行うすべての動的確保を自分のアロケータへ差し替えたい, so that 組み込み先のメモリ管理方針に従わせられる

#### Acceptance Criteria
1. The Amark Library shall メモリの確保・再確保・ゼロ初期化確保・解放を行う `amark_malloc`, `amark_realloc`, `amark_calloc`, `amark_free` を公開し、ライブラリ内の動的確保と解放をすべてこれらを経由して行う
2. While 利用者がアロケータを差し替えていない, the Amark Library shall C 標準ライブラリの確保関数を用いて確保と解放を行う
3. When 利用者が確保・再確保・ゼロ初期化確保・解放の各関数を指定してアロケータを差し替える, the Amark Library shall 以降のすべての確保と解放を指定された関数へ委ねる
4. If 差し替えの指定に必要な関数が欠けている, the Amark Library shall 差し替えを行わず、不正な引数を示すエラーコードを返す
5. When 利用者が既定のアロケータへ戻すよう指定する, the Amark Library shall 以降の確保と解放を C 標準ライブラリの確保関数で行う
6. When 利用者が現在のアロケータを問い合わせる, the Amark Library shall 現在使われている確保・再確保・ゼロ初期化確保・解放の各関数を返す
7. If ゼロ初期化確保で要素数と要素サイズの積が表現可能なサイズを超える, the Amark Library shall アロケータを呼ばずに確保失敗を返す
8. The Amark Library shall サイズ 0 の確保要求ではアロケータを呼ばずに空ポインタを返し、サイズ 0 への再確保では元の領域を解放して空ポインタを返し、空ポインタの解放では何もしないという振る舞いを、全プラットフォームで同じに提供する
9. The Amark Library shall ある時点のアロケータで確保されたメモリは同じアロケータで解放されなければならないこと、およびアロケータの差し替えはスレッドセーフでないことを公開ヘッダに明記する

### Requirement 7: 共通エラーコード
**Objective:** As a ライブラリ開発者, I want ライブラリ全体で単一のエラーコード体系を使いたい, so that モジュールごとに流儀が分裂せず、利用者が一貫した方法でエラーを扱える

#### Acceptance Criteria
1. The Amark Library shall ライブラリ全体で単一のエラーコード列挙を提供し、成功を表す値を 0 とする
2. The Amark Library shall 少なくとも、メモリ確保の失敗、不正な引数、サイズ計算のあふれ、不正な UTF-8 を区別できるエラーコードを提供する
3. When 利用者がエラーコードの文字列化を要求する, the Amark Library shall そのコードを説明する、解放不要で変更されない文字列を返す
4. If 列挙に定義されていない値の文字列化を要求される, the Amark Library shall 空ポインタではない、未知のエラーであることを示す文字列を返す
5. The Amark Library shall 公開済みのエラーコードを後のバージョンで並べ替え・削除・再利用せず、新しいコードは一覧の末尾への追加によってのみ増やし、定義済みのすべてのコードの名前・追加した仕様・文字列を 1 つの一覧で管理する
6. The Amark Library shall 各エラーコードをその文字列と組にして単一の一覧で定義し、列挙と文字列化をその一覧から作ることで、文字列を持たないコードを定義できないようにする
7. The Amark Library shall 失敗しうる公開関数の結果を、このエラーコード列挙で返すことを規約として定める

### Requirement 8: 文字列スライス
**Objective:** As a ライブラリ開発者, I want 長さ付きで所有権を持たない文字列の参照を扱いたい, so that 入力テキストの一部を複製せずに受け渡しでき、NUL を含むデータも正しく扱える

#### Acceptance Criteria
1. The Amark Library shall 先頭位置とバイト長の組で表す、所有権を持たない文字列スライスを提供する
2. When 利用者がポインタと長さ、または NUL 終端文字列からスライスを作る, the Amark Library shall 指定された範囲を参照するスライスを返す
3. When 利用者が 2 つのスライスの比較を要求する, the Amark Library shall 長さと全バイトが一致する場合に限り等しいと判定する
4. When 利用者がスライスの部分範囲を要求する, the Amark Library shall 元のスライスの範囲内に収まる部分スライスを返し、範囲外の指定は元のスライスの範囲に切り詰める
5. When 利用者がスライスの複製を要求する, the Amark Library shall グローバルアロケータで確保した、NUL 終端付きの同じ内容の文字列を返す
6. If スライスの複製で確保に失敗する, the Amark Library shall 空ポインタを返し、確保済みの領域を残さない
7. The Amark Library shall 文字列の所有権規約として、借用する文字列はスライスで表し、所有する文字列はグローバルアロケータで確保した NUL 終端付きで長さを伴うものとし、その解放は `amark_free` で行うことを定める

### Requirement 9: 可変長バッファ
**Objective:** As a ライブラリ開発者, I want 追記によって伸長するバイト列バッファを使いたい, so that レンダリング結果や組み立て中の文字列を効率よく作れる

#### Acceptance Criteria
1. When 利用者が呼び出し側の領域にあるバッファを初期化する, the Amark Library shall メモリを確保せずに空のバッファとする
2. When 利用者がバッファにバイト列、1 バイト、NUL 終端文字列、スライス、またはコードポイントを追記する, the Amark Library shall 既存の内容の後ろへ追記し、必要に応じて容量を伸長する
3. While バッファが領域を保持している, the Amark Library shall 内容の直後に NUL を置き、内容を NUL 終端文字列としても参照できるようにする
4. If 追記または容量の予約で確保に失敗するかサイズ計算があふれる, the Amark Library shall 対応するエラーコードを返し、バッファの内容と長さを呼び出し前の状態のまま保つ
5. When 利用者がバッファの内容の消去または指定長への切り詰めを要求する, the Amark Library shall 確保済みの容量を保ったまま長さを変更する
6. If 切り詰めの長さが現在の長さを超える, the Amark Library shall 内容を変更せず、不正な引数を示すエラーコードを返す
7. When 利用者がバッファの内容の取り出しを要求する, the Amark Library shall 内容の所有権を NUL 終端付きの文字列として長さとともに呼び出し側へ移し、バッファを空の状態に戻す
8. When 利用者がバッファの後片付けを行う, the Amark Library shall 保持している領域を解放し、バッファを再び初期化済みと同じ空の状態にする
9. When 利用者が多数回の追記を行う, the Amark Library shall 追記したバイト数の合計に対して償却定数時間で追記できるよう容量を伸長する

### Requirement 10: UTF-8 基本操作
**Objective:** As a ライブラリ開発者, I want UTF-8 の復号・符号化・検証を共通の関数で行いたい, so that 各モジュールが UTF-8 を同じ規則で扱える

#### Acceptance Criteria
1. When 利用者がバイト列の先頭から 1 文字の復号を要求する, the Amark Library shall 妥当な UTF-8 であればコードポイントと消費したバイト数を返す
2. If 復号対象が不正な UTF-8（過長表現、サロゲートの符号化、U+10FFFF を超える値、途切れた並び、不正な先頭・継続バイト）である, the Amark Library shall 不正な UTF-8 を示すエラーコードと置換文字 U+FFFD を返し、Unicode 標準の推奨（不正な部分の最大部分列ごとに置換文字 1 つ）に従って読み飛ばすべき 1 以上のバイト数を示す
3. If 空のバイト列の復号を要求される, the Amark Library shall 不正な引数を示すエラーコードを返す
4. When 利用者がコードポイントの符号化を要求する, the Amark Library shall 妥当なコードポイントを 1 〜 4 バイトの UTF-8 へ符号化し、書き込んだバイト数を返す
5. If 符号化対象がサロゲートまたは U+10FFFF を超える値である, the Amark Library shall 何も書き込まず、不正な UTF-8 を示すエラーコードを返す
6. When 利用者がバイト列全体の検証を要求する, the Amark Library shall 全体が妥当な UTF-8 であれば成功を返し、不正であればエラーコードと最初の不正なバイトの位置を返す
7. The Amark Library shall NUL バイト（U+0000）を妥当な 1 バイトの文字として扱う
8. When 利用者がバイト列の指定位置の直前で終わる 1 文字の逆方向の復号を要求する, the Amark Library shall 先頭からその位置までを前方へ復号したときの最後の 1 文字（不正な部分であれば同じ規則による置換文字と最大部分列）と同じ結果を、そのバイト数とともに返し、位置が先頭または範囲外であれば不正な引数を示すエラーコードを返す

### Requirement 11: 公開ヘッダと命名の規約
**Objective:** As a ライブラリ利用者, I want 公開 API が一貫した命名とヘッダ構成を持ってほしい, so that 迷わず使え、C と C++ のどちらからも組み込める

#### Acceptance Criteria
1. The Amark Library shall 公開する関数・型・変数の名前を `amark_`、マクロと列挙定数の名前を `AMARK_` で始める
2. The Amark Library shall typedef した型名を `_t` で終え、呼び出し側が領域を持つ構造体の初期化・後片付けを `_init` / `_deinit`、ライブラリが領域を確保する opaque な型の生成・破棄を `_create` / `_destroy` と名付ける
3. The Amark Library shall 公開ヘッダを `amark/` 配下に置き、各公開ヘッダを他のヘッダの事前 include なしに単独で include できるようにする
4. The Amark Library shall 公開ヘッダを C++ の翻訳単位から include した場合にも C リンケージで宣言されるようにする
5. The Amark Library shall 本仕様が提供する公開 API のすべてを 1 つの包括ヘッダから利用できるようにする
6. The Amark Library shall 公開ヘッダから非公開ヘッダを include しない

### Requirement 12: コーディング規約と整形設定
**Objective:** As a ライブラリ開発者, I want コーディング規約が文書化され、可能な範囲で機械的に検査されてほしい, so that 誰が書いても同じ流儀のコードになる

#### Acceptance Criteria
1. The Amark Repository shall コーディング規約（Allman 方式の波括弧、演算順序の括弧付け、ステップ間の空行、命名規約、アロケータ経由の確保、単一エラーコード、文字列の所有権規約）を steering に文書化する
2. The Amark Repository shall Allman 方式の波括弧を含む整形規則を整形ツールの設定ファイルとして提供し、エディタ共通の基本設定（文字コード、改行コード、インデント）も提供する
3. When CI が実行される, the Amark CI shall ライブラリ本体、公開ヘッダ、テストのソースが整形規則に従っているかを検査し、従っていないファイルがあれば失敗させる
4. The Amark Repository shall 機械的に検査できない規約（括弧付け、空行の置き方など）を、検査できない旨とともに文書に明記する
5. The Amark Repository shall steering の構成と技術スタックの記述を、本仕様で確定したディレクトリ構成とビルド方法に合わせて更新する
