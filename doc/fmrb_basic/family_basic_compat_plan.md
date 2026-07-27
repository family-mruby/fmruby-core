# FMRuby BASIC 本格実装検討: Family BASIC 互換化計画

作成: 2026-07-23
ステータス: 着手決定 (2026-07-27)。実装言語は C++ に決定 (規約は
`00_common.md`)。フェーズ別指示書 `phase_b0.md` - `phase_b5.md` を整備済み。
実装担当はまず `00_common.md` を読むこと。

## 1. 目的

現在の仮実装 BASIC (components/basic) を本格実装に置き換え、
**Family BASIC (ファミリーベーシック) の文法を移植**する。
互換性の目標は「Family BASIC のサンプルコードや、ベーマガ (マイコン
BASIC マガジン) に掲載された Family BASIC 用ゲームが、軽微な修正なしで
動く」程度とする。

### 互換性レベルの定義 (受け入れ基準として使う)

| レベル | 内容 | 目標 |
|---|---|---|
| L1 | 言語コア互換: 整数演算・文字列・制御構造・DATA/READ・配列が Family BASIC と同一挙動 | 必須 |
| L2 | 画面互換: 28x24 テキスト画面、LOCATE/COLOR/CLS/SCR$、キャラクタ表示 | 必須 |
| L3 | ゲーム機能互換: SPRITE / DEF MOVE 系、STICK/STRIG、PLAY/BEEP、PALET | 必須 (ベーマガゲームの中核) |
| L4 | 周辺互換: POKE/PEEK の代表的用法、CGSET/CGEN、BGGET/BGPUT、ON ERROR | 可能な範囲 (個別判断) |
| L5 | 完全互換: 実機タイミング、全メモリマップ、マシン語 CALL | 対象外 |

## 2. 現状の仮実装 (調査結果、2026-07-23)

- 場所: `components/basic/` (計 約 2,170 行)
  - `basic/lexer.c` (199) / `basic/parser.c` (572) / `basic/runtime.c` (256) /
    `basic_internal.h` (180): 手書きの行指向インタプリタ
  - `fmrb_basic.c` (309): アプリタスクへの組み込みラッパ
    (fmrb_malloc ベースの per-task state、行テーブル・変数テーブル)
  - `extension/fmrb_basic_gfx.c` (401): CLS/CIRCLE/PRESENT 等の描画拡張 +
    BASIC コンソール (basic_console)
- 起動経路: `main/app/fmrb_app_spawner.c:214` が拡張子 `.bas` を判定し、
  `fmrb_app.c` の `execute_basic_script()` がロード・実行する。
  アプリ定義は `<name>.app.bas` + `.app.toml` (例: flash/app/demo/basic.app.bas)。
- 対応済み構文 (トークンから確認): PRINT / LET / IF-THEN / FOR-NEXT-STEP /
  GOTO / GOSUB-RETURN / INPUT / REM / END / WAIT / CLS / CIRCLE / PRESENT。
- 制約: 変数は単純テーブル (配列・文字列変数・DATA 系なし)、行は逐次
  再パース方式、エラー処理・STOP/CONT なし。**Family BASIC 互換には
  言語コアから作り直しに近い拡張が必要**。
- 資産として残るもの: アプリタスク統合 (spawner / console / per-task
  メモリプール)、gfx 拡張の канва (canvas への文字・図形描画経路)。

## 3. Family BASIC 仕様の整理

対象バージョン: **V3 準拠を基本**とする (V2 は V3 のサブセットに近く、
ベーマガ掲載作品は V2/V3 混在。V3 準拠なら両方を受けられる。
V2 専用動作との差異は個別対応)。

主要仕様 (詳細は spec/ 配下の仕様書を正とする):

- 数値は**整数のみ** (16bit 符号付き、-32768..32767 で確認予定)。
  浮動小数点なし。これが互換実装を大幅に簡単にする。
- 変数: 数値変数 (初期値 0)、文字列変数 A$ (初期値空)。DIM で配列
  (最大 2 次元)。SWAP あり。
- 画面: **28 桁 x 24 行** のテキスト画面 (BG)。LOCATE (0-27, 0-23)、
  COLOR (位置ごとの色 0-3)、CGEN/CGSET (キャラクタセット)、PALET、
  SCR$(x,y) (画面文字の読み取り)、POS/CSRLIN。
- PRINT のゾーン: 画面を幅 8 文字 x 4 ブロックに分割し、`,` 区切りは
  次ゾーンへ飛ぶ (カンマゾーン幅 8)。

### 3.1 ステートメント一覧 (実装対象)

- コマンド系: NEW / LIST / RUN / CONT / CLEAR / AUTO / DELETE / RENUM /
  FIND / TRON / TROFF (V3)。LOAD/SAVE/LOAD? はファイルシステムに読替え。
- 制御: GOTO / GOSUB / RETURN [行番号] / IF..THEN {行 | 文} /
  FOR..TO..STEP..NEXT / ON n GOTO|GOSUB|RETURN|RESTORE / STOP / END /
  PAUSE / ON ERROR GOTO / RESUME / ERROR (V3)
- 変数・データ: LET / DIM / SWAP / READ / DATA / RESTORE / REM
- 入出力: PRINT / INPUT / LINPUT / INKEY$ / KEY / KEYLIST / CLICK
- 画面: CLS / LOCATE / COLOR / CGEN / CGSET / PALET / SCREEN / VIEW /
  FILTER / BGGET / BGPUT / BACKUP (V3。BGTOOL は対象外候補)
- スプライト: DEF SPRITE n,(...)=文字列 / SPRITE n[,x,y] / SPRITE ON|OFF
- 自動移動: DEF MOVE(n)=... / MOVE n / CUT n / ERA n / CAN n (V3) /
  POSITION n,x,y
- サウンド: PLAY 文字列 (MML、複数チャンネル) / BEEP
- メモリ: POKE / PEEK (対応方針は sec 5.6)

### 3.2 関数一覧

- 数値: ABS / SGN / RND(x) (0..x-1) / FRE / PEEK
- 文字列: ASC / CHR$ / VAL / STR$ / HEX$ / LEFT$ / RIGHT$ / MID$ / LEN /
  INSTR (V3)
- 画面・入力: POS / CSRLIN / SCR$(x,y) / INKEY$ / STICK(n) / STRIG(n) /
  XPOS(n) / YPOS(n) / CRASH(n) (V3) / VCT(n) (V3)
- エラー: ERL / ERR (V3)

### 3.3 入力値の仕様 (確認済み)

- STICK(n): 十字キー。0=中立, 1=右, 2=左, 4=下, 8=上 (同時押しは加算)
- STRIG(n): 0=中立, 1=START, 2=SELECT, 4=B, 8=A (同時押しは加算)

## 4. アーキテクチャ検討

### 4.1 実装言語・方式の選択肢

| 案 | 内容 | 評価 |
|---|---|---|
| A: 現行 C 実装を拡張 | components/basic を土台に言語コアを再構成 | 実行速度が出る (ベーマガのアクションゲームは BASIC ながらスプライト自動移動で成立しており、インタプリタ速度は実機並みで十分)。タスク統合・メモリプール・gfx 経路を流用可。**推奨** |
| B: Ruby (PicoRuby) で実装 | BASIC インタプリタを .rb アプリとして書く | 開発は楽だが二重インタプリタで速度が苦しい (mruby VM 上でトークン処理)。ゲーム互換目標に対しリスク |
| C: 既存 OSS BASIC 移植 | uBASIC / my_basic 等 | Family BASIC 固有命令 (SPRITE/MOVE/PLAY) が結局全部自前。行エディタ・整数型・ゾーン等の互換も合わず、利得が薄い |

**案 A を推奨** (2026-07-27 決定)。実装言語は **C++** とする
(例外/RTTI なし、heap を使う STL 禁止、C API 境界維持、コアは
IDF 非依存の純粋 C++ でホストテスト可能にする。詳細規約は
`00_common.md`)。現行コードは「逐次再パース・機能最小」なので、
以下の再構成を行う (実質は書き直しに近いが、構成・統合部は維持):

1. **トークナイズ格納方式**: プログラムはロード時に中間トークン列
   (クランチ済みキーワード + 数値リテラル + 文字列) に変換して行テーブルに
   保持し、実行はトークン列上で行う。実行速度・メモリの両面で必須。
2. **整数 16bit セマンティクス**: 演算・オーバーフロー挙動を Family BASIC に
   合わせる (オーバーフロー時の挙動は未定義扱い。spec 参照)。
3. **文字列ヒープ**: 固定長プール + 単純 compaction (整数のみなので
   値はスタック不要、式評価はテンポラリ文字列の管理だけ注意)。
4. **実行エンジン**: 行番号 -> 行インデックスのソート済みテーブル +
   二分探索。FOR/GOSUB スタック、DATA ポインタ、ON ERROR ハンドラ。
5. **エラーモデル**: Family BASIC 形式のエラー (`?SYNTAX ERROR IN 10` 系。
   文言・コードは spec のエラー表参照) + ERL/ERR/RESUME。
6. **tick 統合**: DEF MOVE の自動移動・PLAY の演奏継続・キー/パッド
   ポーリングは、インタプリタのメインループに 1/60 秒相当の tick 処理を
   組み込み、アプリタスクの on_update 相当から駆動する。

### 4.2 実行環境の形態

- 現行どおり「.bas アプリ」(spawner 起動、ウィンドウ or フルスクリーン)
  を主形態とする。ゲーム実行はフルスクリーン (28x24 の BG 画面) を既定に。
- **直接モード (画面エディタ)** は Family BASIC の体験の核だが、初期は
  オプション扱いにする: 編集は fm-editor で .bas を編集 -> 実行、で代替
  できる。互換ゴール (サンプル/ベーマガ掲載作の実行) には必須でない。
  Phase B4 で「BASIC コンソール」(直接モード + LIST/RUN/AUTO/RENUM) を
  足すか判断する。

## 5. fmruby へのマッピング設計

### 5.1 画面 (BG)

- 28x24 文字 x 8x8 ピクセル = 224x192 ピクセル。320x240 のフレームバッファ
  中央に配置 (上下 24px / 左右 48px の余白は枠色)。等倍で十分視認できる。
- テキスト画面はインタプリタ内部に「文字コード + カラー属性」の
  28x24 シャドウバッファを持ち、変更セルのみ canvas に再描画する
  (SCR$ / COLOR / スクロール系がこのバッファから答えられる)。
- 描画は既存の fmrb_gfx C API (fmrb_basic_gfx.c の経路) を拡張して使う。

### 5.2 キャラクタセット (CGROM 相当) — 重要な検討点

- Family BASIC の CG (英数・カナ・記号 + ゲームキャラ絵柄、CGEN/CGSET で
  切替) は**任天堂の著作物なので同梱できない**。
- 方針: **自作の互換タイルセット**を用意する (8x8、文字コード配置は
  Family BASIC 準拠、絵柄はオリジナル)。マリオ等のキャラタイルは
  「同じコードに同じ意味 (人型キャラ、ブロック等) の自作絵柄」を置く。
  sprite_editor (flash/app/tool) をタイル作成に活用できる。
- 文字コード表 (カナ含む) の互換は SCR$/ASC/CHR$ の挙動に直結するため、
  コード配置は spec の文字コード表に従う (絵柄のみオリジナル)。
- ベーマガのリスティングはカナ文字列を多用するため、**カナ入力・表示は
  必須**。fmruby 側フォントとは独立に BASIC 専用 8x8 フォントを持つ。

### 5.3 スプライト / DEF MOVE

- SPRITE / DEF SPRITE: fmrb_gfx のスプライト機構 (fmrb-sprite.rb が使う
  C API) を BASIC から直接呼ぶ。8x8 / 8x16、反転・カラー (パレット) 指定を
  Family BASIC のパラメタ形式で受ける。
- DEF MOVE / MOVE / CUT / ERA / CAN / POSITION / XPOS / YPOS / VCT / CRASH:
  自動移動はインタプリタの tick 処理 (4.1-6) で座標更新・アニメ切替を行う。
  CRASH(n) はスプライト矩形の重なり判定を tick 時に記録して返す。
  自動移動パラメタの仕様は spec 参照。

### 5.4 サウンド (PLAY / BEEP)

- 音源は WROVER 側 APU エミュレータ。既存経路は FMSQ シーケンス
  (load_fmsq / play_slot、kernel/audio_handler.rb) と NSF 再生。
- 方針候補:
  - a) **MML -> FMSQ 変換をインタプリタ内で行い、既存の load_fmsq/play で
    再生** (host 側変更なし。PLAY 文字列は事前変換できる)。**本命**
  - b) APU レジスタ直接制御のリアルタイム経路を新設 (BEEP や効果音の
    即時性に有利だが host プロトコル追加が必要)
  - 判断は FMSQ の表現力 (3 チャンネル、テンポ・音長・音量) を確認して
    Phase B0 で行う。BEEP は短い固定シーケンスで代替可能。
- Family BASIC の PLAY は複数チャンネル同時演奏 (矩形波 2 + 三角波 1)。
  MML 文法 (音階 CDEFGAB、音長、オクターブ O、テンポ T、音量 V 等) は
  spec の PLAY 節に従う。

### 5.5 入力 (STICK / STRIG / INKEY$)

- ゲームパッド HID は対応済み (USB ゲームパッド)。kernel からアプリへの
  HID イベントを BASIC タスクで受け、**最新状態を保持するレジスタ**を
  実装。STICK/STRIG はそのレジスタを 3.3 のビット割当で返す。
- パッド非接続時はキーボードの矢印 + Z/X/Enter/Shift を STICK/STRIG に
  マップする (ベーマガ検証を PC キーボードだけで回せるように)。
- INKEY$ はキーイベントの 1 文字キュー。カナ入力モードも必要 (5.2)。

### 5.6 POKE / PEEK / CALL

- 実メモリマップ (VRAM・ワーク RAM) は存在しないため完全互換は不可。
- 方針: **仮想メモリマップ**を定義し、ベーマガで使用頻度の高い領域のみ
  エミュレートする (例: スプライト属性、画面 RAM、乱数シード等。
  頻出アドレスは Phase B0 のコーパス調査で決める)。未対応アドレスへの
  アクセスは警告ログ + 無害動作 (PEEK=0)。
- マシン語 (CALL、マシン語 DATA 列の実行) は**対象外** (L5)。6502
  エミュレーションはスコープ外とする。該当作品は非対応と明示する。

### 5.7 ファイル (LOAD / SAVE)

- カセットの代わりに littlefs (/home 以下) へ読み書き。LOAD"ファイル名" /
  SAVE"ファイル名" をファイルパスに読替え。LOAD? (ベリファイ) は
  ファイル比較として実装。

## 6. 段階実装計画 (フェーズ案)

フェーズ別の詳細指示書は `phase_b0.md` - `phase_b5.md` (spinel_aot 方式。
共通規約は `00_common.md`、報告は `reports/`)。以下は概要。

- **Phase B0: 仕様確定・コーパス整備 (検証基盤)**
  - 命令・関数・エラーの仕様表を doc/fmrb_basic/spec/ に整備する
    (MML 文法、DEF SPRITE/MOVE のパラメタ、文字コード表を含む。
    整備済み。残りは spec 末尾の要実機確認リストの解消)
  - ベーマガ・サンプルのターゲット作品リスト (5-10 本) を選定し、
    使用命令を棚卸し (POKE 依存度もここで判明する)
  - ゴールデンテスト機構: .bas + 期待出力 (テキスト画面ダンプ) の
    自動比較を Linux ビルドで CI 可能にする
- **Phase B1: 言語コア再構成 (L1)**
  - トークナイザ・実行エンジン・整数/文字列/配列・DATA・エラーモデル。
  - 画面なしで動く範囲をゴールデンテストで固める
- **Phase B2: テキスト画面 (L2)**
  - 28x24 シャドウバッファ、LOCATE/COLOR/CLS/SCR$/PRINT ゾーン、
    自作フォント (カナ含む)、INKEY$/KEY
- **Phase B3: ゲーム機能 (L3)**
  - SPRITE / DEF MOVE 系 + tick 統合、STICK/STRIG、PLAY/BEEP (MML->FMSQ)、
    PALET/CGSET/CGEN。ターゲット作品を 1 本ずつ動かして潰す
- **Phase B4: 周辺・体験 (L4)**
  - 仮想 POKE/PEEK、BGGET/BGPUT、ON ERROR、LOAD/SAVE、
    直接モードコンソール (要否判断)
- **Phase B5: 実機調整**
  - S3 実機での速度・音・入力の確認。ベーマガ作品の動作リスト公開品質へ

検証は全フェーズでルートの headless ハーネス (dev_run_check.sh +
fmrb_input.py + fmrb_screenshot.py) を使う。BASIC 画面はテキスト
シャドウバッファを持つため、スクリーンショット比較よりも「画面バッファの
テキストダンプを debug 経路で吐いて diff」する方が安定する (B0 で仕組みを
入れる)。

## 7. リスク・未決事項 (ユーザ判断が必要なもの)

1. **タイルセット/フォントの自作**: 5.2 の方針で良いか。絵柄の制作を
   誰が行うか (sprite_editor での手作業 or AI 生成の下絵)。
2. **PLAY の実装深度**: MML->FMSQ 変換で十分か、効果音の即時性のために
   リアルタイム経路を作るか (B0 で技術判断、コストが変わる)。
3. **直接モードの要否**: 初期リリースは「.bas アプリ実行」のみで良いか。
4. **対象作品リスト**: ベーマガのどの作品を互換目標にするか (権利面は
   ユーザ私的利用の範囲で、リポジトリには収録しない。テストコーパスは
   自作サンプル + 権利上問題のないコードで構成する)。
5. 現行 components/basic の扱い: 再構成で置き換える前提だが、
   basic.app.bas (デモ) の互換は維持する。

## 8. 参考

- リポジトリ内の関連: components/basic (現行実装)、
  main/app/fmrb_app_spawner.c (.bas 起動)、lib/add/picoruby-fmrb-app/mrblib/
  fmrb-sprite.rb / fmrb-tilemap.rb (gfx 機構の参考)、
  main/prebuild_scripts/kernel/fmrb_kernel/audio_handler.rb (FMSQ 経路)、
  flash/app/tool/sprite_editor.app.rb (タイル制作ツール)
