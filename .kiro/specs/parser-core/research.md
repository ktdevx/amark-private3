# Research & Design Decisions: parser-core

## Summary
- **Feature**: `parser-core`
- **Discovery Scope**: New Feature（greenfield。ソースコードは未作成で、foundation と node-tree の承認済み設計の公開 API の上に積む）
- **Key Findings**:
  - CommonMark 仕様の付録「A parsing strategy」と cmark の実装は、ブロック段階（開いているブロックの継続判定 → 開始判定 → 遅延継続 → 内容の追加）とインライン段階（デリミタスタック・ブラケットスタック・openers_bottom による線形時間の強調処理）からなる。このうち構文に依存しない骨組みはすべてエンジンに置け、構文固有の判定（flanking、rule of 3、リンク先の構文、ラベルの正規化）はコールバックに押し出せる
  - 段落を後から別のブロックに置き換える構文（Setext 見出し、GFM テーブル）と、段落の先頭を消費する構文（リンク参照定義）は、「中断される段落の行への読み取りアクセス」「末尾の行の引き取り」「段落の内容の処理器」の 3 つの汎用機構で表現できる
  - node-tree のレジストリには種別の削除 API がない。方言単位のレジストリに拡張ごとに複数種別を登録すると、途中失敗時に強い保証を守れない。拡張ごとにレジストリを持てば、拡張の生成が原子的になり、方言の組み立て前から種別ハンドルが得られる

## Research Log

### CommonMark の解析戦略（付録）
- **Context**: エンジンが提供すべき汎用機構を CommonMark の全構文を基準に決めるため
- **Sources Consulted**: CommonMark Spec 0.31.2 本文と付録 A parsing strategy（https://spec.commonmark.org/0.31.2/）、cmark の `blocks.c` / `inlines.c` の構成（既知の知識）
- **Findings**:
  - タブは展開せず、構造の判定でだけ 4 桁のタブ位置として扱う。ブロック引用の `>` の後のタブは一部だけ消費され、残りの桁はコードブロックの内容などで空白として現れる（例 6 ほか）
  - U+0000 は U+FFFD に置き換える（2.3）
  - openers_bottom は「デリミタの種類 × 閉じ側の長さ mod 3 × 閉じ側が開き側にもなれるか」で索引付けされ、一致しなかったときに更新される。これにより強調処理はデリミタ数に対して線形になる
  - リンク（画像でない）が一致すると、それより前の `[` の開きはすべて無効になる（リンクの入れ子禁止）
  - リンク内の強調はブラケットの開きを stack_bottom として処理する
  - リンク参照定義は段落を閉じるときに先頭の行から解析される。Setext 見出しの下線は段落を見出しに変える。cmark は下線を判定する時点で参照定義を先に解決し、段落が定義だけだった場合は下線を段落の続きとして扱う
  - 遅延継続行は「すべての開いているブロックが継続したわけではないが、新しいブロックが始まらず、最も内側の開いているブロックが段落で、行が空行でない」ときに段落へ加わる
  - 字下げコードブロックは段落を中断できず、遅延継続の可能性がある位置でも始まらない（cmark の `maybe_lazy`）。順序付きリストは開始番号 1 でなければ段落を中断できない。これらは構文の判定だが、エンジンは「段落を中断する位置か」「遅延継続の可能性があるか」の 2 つの事実を提供する必要がある
- **Implications**: エンジンの行処理アルゴリズムは cmark の `S_process_line` と同じ順序に固定し、構文には位置・桁・字下げ・空行・中断・遅延の可能性を問い合わせる手段を与える

### リストの tight / loose と空行の記録
- **Context**: 解析中の状態をノードに持たせない（tech.md）制約のもとで、リストの tight 判定に必要な「最後の行が空行だったか」をどこに持つか
- **Findings**: cmark はノードに `last_line_blank` を持ち、ブロック引用・フェンスコード・空で始まる項目を例外とし、空行で記録した後に祖先の記録を消す。リストの finalize は項目と子について記録を調べる
- **Implications**: エンジンが「ブロック記録」（ノードをキーとする解析中の付随表）に空行の記録を持ち、構文は問い合わせ関数で読む。例外規則は構文のコールバック（空行の記録の規則）で宣言する

### GFM テーブルの表現可能性
- **Context**: brief が「段落を後から別ブロックに置き換える構文を拡張 API で表現できること」の確認を求めている
- **Findings**: GFM のテーブルは、段落の最後の行をヘッダ行、現在の行を区切り行として判定し、段落の残りの先頭側の行はそのまま段落として残す。セルはテーブルのブロックが閉じたときに作られ、各セルの内容がインライン解析される
- **Implications**: 段落の行単位の読み取り、ある行から末尾までの引き取り、閉じるときに任意の子孫ノードをインライン解析の対象として登録する操作（インライン対象の登録）が必要

### 拡張とレンダラの結び付け
- **Context**: html-renderer が並行して設計中であり、parser-core はレンダラに依存しない。structure.md は「結び付けるのは拡張の側」と定める
- **Findings**: node-tree の下流ガイダンスは、レンダラのフックを種別ハンドル `const amark_node_kind_t *` で引くことを定めている
- **Implications**: parser-core は (1) 拡張の生成直後から安定した種別ハンドルを返し、(2) 型を知らない「鍵付きの付随データ（attachment）」を拡張に結び付けられるようにする。レンダラ側の型はレンダラの spec と拡張パッケージが持つ

## Architecture Pattern Evaluation

| Option | Description | Strengths | Risks / Limitations | Notes |
|--------|-------------|-----------|---------------------|-------|
| 全構文を vtable として登録（採用） | エンジンは行処理・インライン走査・デリミタ／ブラケット処理の骨組みだけを持つ | roadmap の決定どおり。CommonMark が API の最初の利用者になる | API の不足が commonmark 実装時に露見する（前提として受け入れ済み） | brief と tech.md の方針 |
| cmark-gfm 型（コア固定＋フック） | CommonMark をエンジンに埋め込み、拡張はフックで追加 | 実績がある | 構文の取り外し・差し替えができない | roadmap で却下済み |
| micromark 型（イベント列） | トークナイザとイベント列から木を作る | 拡張性が高い | C99 で複雑、ロスレスを対象外とする今回には過剰 | roadmap で却下済み |

## Design Decisions

### Decision: 拡張ごとにノード種別のレジストリを持つ
- **Context**: node-tree は「方言（またはパーサ）ごとに 1 つ」を提案し、決定を parser-core に委ねた
- **Alternatives Considered**:
  1. 方言ごとに 1 つのレジストリ — 拡張の追加中に 2 つ目の種別の登録が失敗すると、1 つ目を取り消せない（node-tree に削除 API がない）
  2. パーサごとに 1 つ — 文書の寿命がパーサに縛られ、パーサを先に破棄できない
  3. 拡張ごとに 1 つ（採用）
- **Selected Approach**: `amark_extension_create` が自分のレジストリを作り、宣言された種別をすべて登録する。失敗時はレジストリごと破棄する。方言は拡張の追加時に、他の拡張の種別名との重複を `amark_node_registry_find` で検査する
- **Rationale**: 強い保証を node-tree の変更なしで守れる。種別ハンドルが方言の組み立て前から得られ、レンダラのフックを拡張単位で結び付けられる
- **Trade-offs**: 名前の一意性は方言単位での検査になる（レジストリ単位ではない）。`amark_dialect_find_kind` は拡張数に対して線形
- **Follow-up**: node-tree の Downstream Integration Guidance と NodeRegistry の記述は、横断レビューで本決定（拡張ごとのレジストリ、parser-core の API には出さない、方言が拡張間の名前の重複を検査、拡張または方言がその種別のノードとフックを保持するレンダラより長く生きる）に合わせて更新済み（文書のみ、API 変更なし）

### Decision: flanking はエンジンでなく構文が計算する
- **Context**: left/right flanking の判定には Unicode の空白・句読点の分類が必要で、brief はそれを「特定構文のためのデータ」として範囲外にしている
- **Selected Approach**: 構文がデリミタの並びを積むときに「開けるか」「閉じられるか」を渡す。エンジンは直前・直後の文字を調べる手段（内容全体と位置）を提供する
- **Rationale**: エンジンに Unicode 表を持たせず、`_` と `*` の違いや GFM の `~` の規則も構文側で表現できる

### Decision: rule of 3 と openers_bottom の一般化
- **Context**: 強調の照合規則（rule of 3）は CommonMark 固有だが、openers_bottom による線形化はエンジンにしかできない
- **Selected Approach**: デリミタの処理器（handler）が「照合（使う個数、0 なら不一致）」「閉じ側の分類（0〜7）」「ノードの生成」の 3 つを提供する。エンジンは openers_bottom を「処理器 × 文字 × 閉じ側の分類」で持つ。CommonMark の強調は分類を `(can_open ? 3 : 0) + original_length % 3` とする
- **Rationale**: 照合規則が分類の中で単調（ある分類の閉じ側が不一致だった開き側は、同じ分類の後続の閉じ側とも不一致）である限り線形になる。この条件を処理器の契約として文書化する

### Decision: ブラケットの無効化を水位線で行う
- **Context**: 「前のすべての `[` を無効にする」を素朴に行うとスタックの深さに比例する
- **Selected Approach**: フラグのビットごとに「この深さより下は無効」という水位線を持ち、無効化はスタックの現在の深さを記録するだけにする
- **Rationale**: 一定時間。積み直された開きは水位線より上なので有効

### Decision: 段落はエンジンのフォールバックとして持つ
- **Context**: brief は「段落とテキストという最小のフォールバックだけを持つ」と定める
- **Selected Approach**: エンジンが段落の振る舞い（空行で閉じる、遅延継続、行頭の空白除去、末尾の空白除去、段落の内容の処理器の呼び出し）を持つ。CommonMark の段落の規則はこれと一致するため、commonmark は段落の構文を実装せず、リンク参照定義を段落の内容の処理器として登録する
- **Trade-offs**: 段落の規則を差し替えたい方言はない前提。必要になれば段落の振る舞いを構文として差し替える API を追加する（Revalidation Trigger）

### Decision: エラーコードは 1 つだけ追加する
- **Selected Approach**: `AMARK_ERROR_INVALID_STATE`（文字列と意味は foundation design.md のエラーコード一覧表が正本。値は `AMARK_ERROR_LIST` の並び順で決まる）。終了後の供給、失敗後の供給、凍結後の方言の変更に使う。上限の超過はエラーにせず、テキストとして扱う（CommonMark の結果を壊さず、悪意ある入力でも文書を返すため）
- **Alternatives**: `AMARK_ERROR_LIMIT_EXCEEDED` を追加して上限超過で失敗させる — 信頼できない入力で文書が得られなくなるため却下

### Decision: 開始判定の結果の 4 分類と継続判定の 3 分類
- **Selected Approach**: 開始は「開かない／コンテナ／葉（行の残りを内容へ）／葉（行を処理済み）」、継続は「継続しない／継続する／行を消費して閉じる」。行の内容を蓄えた後で閉じる必要（HTML ブロックの終了条件）は、文脈への「この行の後で閉じる」指示で表す
- **Rationale**: cmark の戻り値 0 / 1 / 2 と、`add_line` 後の終了判定をそのまま一般化したもの

### Decision: commonmark からの変更要求 C1〜C3 の取り込み
- **Context**: commonmark spec の設計で、parser-core の契約の曖昧さが 3 点見つかり、変更要求として上流へ戻された（横断レビューで適用済み）
- **Selected Approach**:
  - C1（寿命の義務の限定、13.8・14.9）: 拡張・方言が文書より長く生きる義務は、拡張が登録した種別のノードを含む文書に限る。標準種別だけの文書は方言とパーサの破棄後も有効。`amark_commonmark_parse` が内部の方言とパーサを破棄してから文書を返すために必要
  - C2（空行の定義、7.1・8.4・8.5）: 空行は「継続判定と開始判定で読み進めた後の行の残りがスペースとタブだけ」の行。`> - a` / `>` / `> - b` を CommonMark どおり loose にするために必要
  - C3（インライン対象の逐次処理、9.1）: インライン対象は 1 つずつ完了まで解析し、1 つの対象の間 `amark_inline_context_get_block` は同じノードを返し、対象ごとに異なるノードである。構文が対象ごとの状態をノードで区別するために必要
- **Rationale**: いずれも既存の設計の振る舞いを明文化・限定するもので、API の形は変えない
- **Also applied**: 一括パースの名前と引数を `amark_parser_parse(dialect, data, length, options, &document)` に変更（`amark_parse` から改名、`data, length` の並びを `feed` に揃える）。テスト補助を `tests/support/test_syntax.{h,c}`（`test_syntax_*`）と `test_tree_dump.{h,c}`（`test_tree_dump_*`）に改名

## Synthesis Outcomes
- **Generalization**: 「ブロックの内容をインライン解析する」と「テーブルのセルをインライン解析する」を、インライン対象の登録という 1 つの機構にまとめた。リンク参照定義の表と GFM 脚注の定義表を、名前空間付きの定義テーブルにまとめた
- **Build vs. Adopt**: C 標準ライブラリ以外に依存しない方針（tech.md）のため、既存のパーサ（cmark、md4c）は取り込まない。アルゴリズムは CommonMark 付録と cmark の公開された手順に倣う。ハッシュ表は自作する（定義テーブルとブロック記録で共用）
- **Simplification**: 拡張間の依存関係の宣言、構文単位の無効化、上限超過のエラー、入力サイズの上限は持たない。構文の差し替えは拡張単位の取り外しと追加で行う

## Risks & Mitigations
- 段落の内容の処理器が、テーブルの開始判定のたびに段落全体を再走査して二乗時間になる — 処理器の結果を「段落の行数が変わるまで」キャッシュする。処理器は内容の長さに線形であることを契約にする
- 照合規則が分類内で単調でない処理器は線形性を壊す — 契約として文書化し、病的入力テストで CommonMark 相当の処理器を検証する
- commonmark の実装中に API の不足が見つかる — 前提として受け入れ、本 spec を遡って改訂する（roadmap の進め方）
- 1 回の開始判定で開けるブロック数を制限しないと深さの上限を超えられる — 1 回の開始判定で開けるブロックは 4 つまでとする

## References
- [CommonMark Spec 0.31.2](https://spec.commonmark.org/0.31.2/) — タブ（2.2）、不正な文字（2.3）、付録 A parsing strategy（ブロック段階、process emphasis、ブラケットの無効化）
- cmark（https://github.com/commonmark/cmark）— `blocks.c` の行処理と `S_set_last_line_blank`、`inlines.c` の delimiter / bracket スタック
- cmark-gfm（https://github.com/github/cmark-gfm）— 構文拡張の API、テーブル拡張による段落の置き換え
- `.kiro/specs/foundation/design.md`、`.kiro/specs/node-tree/design.md` — 上流の公開 API
