# Phase B1 報告書: 言語コア再構成 (互換レベル L1)

実施: 2026-07-27 - 28 / ブランチ `fmrb-basic` (fmruby-core)
対象タスク: `phase_b1.md` T1-1 - T1-7

画面なしで動く Family BASIC の言語コアを実装し、旧仮実装
(`components/basic/basic/`) を撤去した。ゴールデンテストは 62 ケース green。

## 0. 成果物一覧

| 追加/変更 | 内容 |
|---|---|
| `core/basic_tokens.hpp` | トークン定義とキーワード表 (X マクロで enum と表を同期) |
| `core/basic_charset.hpp/.cpp` | Family BASIC コード <-> UTF-8 変換 (カナ含む) |
| `core/basic_core.hpp/.cpp` | ホスト I/F、値・変数・行テーブルの型、ロード、出力、実行ループ |
| `core/basic_internal.hpp` | 演算子優先順位など TU 間共有ヘルパ |
| `core/basic_tokenizer.cpp` | クランチ (ソース -> トークン列) / デクランチ (LIST 用逆変換) |
| `core/basic_vars.cpp` | 変数・配列・セルアクセス |
| `core/basic_expr.cpp` | 式評価 (反復・明示スタック) と組み込み関数 |
| `core/basic_exec.cpp` | ステートメント実行 |
| `fmrb_basic.cpp` (旧 `.c`) | アプリタスク側アダプタ (プール、コンソール、終了シグナル、gfx 拡張) |
| `include/fmrb_basic.h` | `basic_gfx_ops_t` と `fmrb_basic_set_gfx_ops()` を追加 (既存 API は不変) |
| `basic/lexer.c` `parser.c` `runtime.c` `basic_internal.h` | **削除** |
| `test/golden/` | 62 ケース (機構確認 4 + 言語機能 55 + 自作サンプル昇格 3) |
| `flash/app/demo/basic.app.bas` | Family BASIC 記法へ書き換え |

## 1. トークン形式 (T1-1)

プログラムはロード時に 1 行ずつクランチし、行テーブル + トークンアリーナに
格納する。実行はトークン列上で行い、ソーステキストは保持しない。

### 1.1 バイト割り当て

| バイト値 | 意味 |
|---|---|
| `0x00` | EOL (行末。各行の末尾に 1 バイト付く) |
| `0x01` | 数値リテラル + 2 バイト (リトルエンディアン、**16bit の生パターン**) |
| `0x02` | 文字列リテラル + 長さ 1 バイト + 文字コード列 |
| `0x03` | 数値変数 + 名前 2 バイト (有効文字。1 文字名は 2 バイト目 0) |
| `0x04` | 文字列変数 + 名前 2 バイト |
| `0x05` | 生バイト列 + 長さ 1 バイト + バイト列 (REM / DATA の中身) |
| `0x10` `0x11` `0x12` | `<=` `>=` `<>` |
| `0x20`-`0x7E` | その文字自身 (`+ - * / ( ) , ; : = < >` など) |
| `0x81`-`0xE8` | キーワード (`basic_tokens.hpp` の X マクロ順) |

設計上の要点:

- **数値リテラルは 16bit 生パターンで持つ**。式では `int16_t` として読むので
  `&HFFFF` = -1 (core_spec sec 2) になり、GOTO/GOSUB などの行番号オペランドは
  同じ 2 バイトを `uint16_t` として読むだけで 65535 まで扱える。
  文脈依存のクランチが要らない。
- **REM と DATA の中身は式ではない**のでクランチせず生のまま持つ
  (`DATA HELLO,10` の HELLO を変数と誤解しない)。DATA は引用符の外の `:` で
  終わる (core_spec sec 6 が「`,` `:` を含む文字定数は引用符必須」と規定)。
- キーワードは**単語境界を要求せずに最長一致**でマッチする。これにより
  `FORI=1TO9` が正しく解釈でき、core_spec sec 3 が
  「変数と論理演算子の間にスペースが必要」と書いている理由とも一致する。
- 省略形 (`P.` = PRINT) は表に「`.` の前の文字数」を持たせて照合する
  (spec の省略形をそのまま表現できる)。
- デクランチ (`decrunch_line`) を実装済み。B4 の LIST / 直接モードで使う。

### 1.2 メモリレイアウト

`interpreter::init()` がホスト (= per-task プール) から確保するブロック:

| ブロック | 既定容量 | バイト数 | 用途 |
|---|---|---|---|
| 行テーブル | 512 行 | 6144 | `{行番号, アリーナ offset, 長さ}` をソート順に保持 (二分探索) |
| トークンアリーナ | 8192 B | 8192 | クランチ済みトークン列 |
| 変数テーブル | 96 個 | 1152 | `{名前 2, 型, 次元数, 各次元上限, データ offset}` |
| 変数データ | 8192 B | 8192 | スカラ・配列の実体 |
| FOR スタック | 16 段 | 160 | 変数名・終値・STEP・本体位置 |
| GOSUB スタック | 24 段 | 96 | 戻り位置 |
| 式オペランドスタック | 24 段 | 864 | `basic_value` (36 B) |
| 作業バッファ x2 | 288 B x2 | 576 | 1 行分のソース / クランチ結果 / INPUT 行 |
| **合計** | | **25376** | + `sizeof(interpreter)` = 240 B |

- 値 (`basic_value`) は 36 バイト: 型フラグ + 16bit 数値 + 長さ + 31 バイトの
  文字列インライン領域。**文字列ヒープと compaction は作らなかった**。
  Family BASIC は文字列を 31 文字に制限している (core_spec sec 1 / ST エラー)
  ので、固定セルにすると断片化も compaction バグも構造的に起きず、
  使用量が容量から一意に決まる。compat_plan sec 4.1-3 が求めた
  「固定長プール」を、値そのものに分散させた形。
- **ファイルスコープの static 配列は 1 つも無い**。1 行分の作業バッファも
  プールから取る (00_common メモリ設計原則 2)。
- キーワード表・エラー表・文字コード表は `constexpr` (rodata、原則 3)。

## 2. 実行エンジン (T1-2)

- 実行位置は `(行インデックス, 行内オフセット)`。GOTO/GOSUB/RETURN/ON は
  行番号 -> 行インデックスを二分探索で解決し、`jumped_` を立てて
  実行ループに制御を返す。**ジャンプは C の呼び出しに写像しない**。
- `IF ... THEN <文>` が真のときも同じ仕組みを使う: `jumped_` を立てて
  「続きは実行ループが実行する」と伝える。したがって
  `IF A THEN PRINT X:PRINT Y` の実行に C 再帰が発生しない。
  偽のときは行末までスキップする (裁可済みの MS 系挙動)。
- FOR は「開始時点で条件を満たしていなければ本体を実行しない」
  (core_spec sec 6) ため、その場合は対応する NEXT までトークンを
  読み飛ばす (ネスト深さをカウント)。NEXT にループ変数名を書くのは
  spec どおり SN エラー。
- `on_tick` は 32 ステートメントごとに呼ぶ (`tick_interval`)。
  false が返るとプログラムを止める。アプリの終了要求 (kill) への応答と、
  B3 の 1/60 tick 統合の土台になる。

## 3. 式評価 (T1-4)

- **再帰下降ではなく反復**。オペランドスタック (プール) と
  オペレータスタック (16 段、`op_entry` 8 B = 128 B のローカル) を持ち、
  括弧・関数呼び出し・配列添字を「マーカー」としてオペレータスタックに
  積む。`)` でマーカーまで畳み、関数なら引数をまとめて適用する。
- ネスト上限 (16) 超過は FT エラー (spec sec 15 の「式が複雑すぎる」)。
- 優先順位は core_spec sec 3 のとおり:
  `* / MOD` > `+ -` > 関係 > NOT > AND > OR > XOR。単項マイナスは
  すべての二項演算子より強い。関係演算の結果は -1 / 0。
- 実装済み関数: ABS SGN RND FRE PEEK ASC CHR$ VAL STR$ HEX$ LEFT$ RIGHT$
  MID$ LEN INSTR POS。RND は固定シードの xorshift32
  (ゴールデンテストの再現性のため。RND(1) は常に 0)。PEEK は常に 0 (B4)。
  CSRLIN / SCR$ / INKEY$ / STICK / STRIG / XPOS / YPOS / VCT / CRASH /
  MOVE(n) は中立値を返す (B2/B3 で実装)。

## 4. fmruby 統合 (T1-6)

- `fmrb_basic.c` -> `fmrb_basic.cpp`。`include/fmrb_basic.h` の
  既存 C API はシグネチャを変えていない (`fmrb_app.c` は無改造)。
  追加したのは `basic_gfx_ops_t` と `fmrb_basic_set_gfx_ops()` のみで、
  これは旧 `basic_internal.h` を直接触っていた
  `basic_console_register_gfx_ops()` の受け皿。
- コアはプール上に placement new する (グローバル operator new 不使用)。
- ホストコールバックの対応:

  | コア側 | アダプタの実装 |
  |---|---|
  | `alloc` / `dealloc` | `fmrb_malloc` / `fmrb_free` (per-task プール) |
  | `put_char` | 1 行分バッファし、改行で `basic_output_cb_t` へ (コンソールは行単位 API のため) |
  | `read_line` | プロンプトを flush してから `basic_input_cb_t` |
  | `ticks_ms` / `sleep_ms` | FreeRTOS tick / `fmrb_task_delay_ms` |
  | `on_tick` | `fmrb_app_poll_exit_signal()` (false で停止) |
  | `on_error` | `FMRB_LOGE` |
  | `ext_statement` | CLS / CIRCLE / PRESENT を gfx ops へ。それ以外は false -> コアが IL |
- 画面・スプライト・サウンド系ステートメントは現状すべて
  `ext_statement` に落ちて **IL エラー**になる (ゴールデン
  `151_unsupported_statement` で固定)。B2/B3 がコア側に実装して置き換える。

## 5. デモの書き換え (T1-6 / B0 裁可 4)

`flash/app/demo/basic.app.bas` を Family BASIC 記法へ変更した:

| 旧 | 新 | 理由 |
|---|---|---|
| `LET A = 10` | `A=10` | LET という命令語は存在しない |
| `NEXT I` | `NEXT` | NEXT にループ変数名は付けられない |
| `WAIT 1000` / `500` / `5000` | `PAUSE 60` / `30` / `300` | WAIT は無い。PAUSE の単位は 1/60 秒 |
| `PRINT "  ", I` | `PRINT "  ";I` | `,` はゾーンタブなので見た目が変わる |
| 小文字混じりの文字列 | 大文字 | 文字コード表に小文字が無い (ロード時に大文字化される) |
| `PRINT ""` | `PRINT` | 単独 PRINT = 改行のみ |

linux sim (headless ハーネス) で確認した挙動 (B0 report sec 6.3 の維持基準):

1. ランチャから起動でき、200x150 のウィンドウ (位置 10,15) が開く: OK
2. 行が溢れるとスクロールする: OK
3. 最後まで走り `PROGRAM COMPLETED!` の後に自動終了・資源解放: OK
   (`BASIC program executed successfully` -> `Task exiting normally`)
4. PRINT 出力 (**新しい回帰基準**、空行含め 37 行):

```
HELLO FROM BASIC!
FMRUBY BASIC INTERPRETER

A =  10 
B =  20 
C = A + B =  30 

COUNTING FROM 1 TO 5:
   1 
   2 
   3 
   4 
   5 

COUNTING DOWN FROM 10 TO 0 BY 2:
   10 
   8 
   6 
   4 
   2 
   0 

X =  15 
X IS GREATER THAN 10
X EQUALS 15

CALLING SUBROUTINE...
  INSIDE SUBROUTINE
  Y =  100 
RETURNED FROM SUBROUTINE

TESTING ARITHMETIC:
  5 * 3 =  15 
  20 / 4 =  5 
  (2 + 3) * 4 =  20 

PROGRAM COMPLETED!
```

B0 との差分は「英字が大文字」と「数値の後ろにスペース 1 個が付く」の 2 点。
後者は core_spec sec 6 の PRINT 規定 (数値は前に符号 1 桁分、後にスペース 1 つ)
に合わせた結果で、意図した変更。

## 6. ゴールデンテスト (T1-7)

`rake basic:test` -> **62 passed, 0 failed** (docker 不要、ホスト g++)。

| 番号帯 | 内容 |
|---|---|
| 001-004 | 機構確認 (空プログラム、行テーブル、ロード時 SN) |
| 101-108 | 整数演算、優先順位、負数の除算、オーバーフロー境界、16 進、関係演算、論理演算、単項、DZ |
| 110-117 | 文字列関数、連結、ST エラー、ASC/CHR$/VAL/STR$/HEX$、文字列比較、INSTR、カナ、TM |
| 120-130 | FOR/NEXT (STEP・0 回・ネスト)、GOSUB ネスト、IF の 4 形態、ON GOTO/GOSUB、UL/NF/RG |
| 131-142 | DATA/READ/RESTORE、OD、1 次元/2 次元/文字列配列、SO、DD、SWAP、PRINT ゾーン・区切り・数値書式 |
| 143-158 | INPUT (数値・文字列・引用符付き・非数値)、LINPUT、SN、REM 2 形態、CLEAR、STOP、未実装文 (IL)、省略形、マルチステートメント、変数名 2 文字識別、RND 範囲、数値関数、入れ子式 |
| 160-162 | 自作サンプル 3 本 (B0 の `test/samples/` から昇格) |

- ランナーには入力供給機能を追加済み: `NNN_name.input` があれば
  INPUT/LINPUT の行入力として渡す。
- 期待値は**まず spec から手で導き、実行結果と 1 件ずつ突き合わせて**確定した
  (実装の出力をそのまま正としていない)。差異が出た 1 件
  (`PRINT )` が MO を返していた) は評価器を修正し SN に揃えた。

## 7. 計測 (受け入れ基準 5)

linux sim でデモアプリ (55 行) を実行した直後の値:

```
fmrb_basic: Loaded 55 program lines
fmrb_basic: BASIC usage: pool used=34104 free=471208 of 505312 bytes,
            stack headroom=123224 bytes
```

| 項目 | 値 | 内訳 |
|---|---|---|
| per-task プール使用量 | 34,104 B | インタプリタ 25,376 + `interpreter` 240 + アダプタ状態 ~250 + BASIC コンソール ~8,400 (64 行 x 128 B のテキストバッファ) |
| プール残 | 471,208 B / 505,312 B | 使用率 6.7 % |
| BASIC タスクのスタック残 | 123,224 B | linux ビルドのタスクスタックは大きいため**この数値は S3 の参考にならない** |

C スタック深さの上限 (設計値、再帰なし):

```
run() -> exec_statement() -> st_xxx() -> parse_var_target() -> eval_number() -> eval() -> call_builtin()
```

の 7 フレームが最深で、**BASIC 側のネスト (括弧・FOR・GOSUB・IF) は
1 フレームも増やさない** (すべてプール上の明示スタック)。
機械的な確認結果:

- `core/` の include は `<cstddef>` `<cstdint>` と自身のヘッダのみ
  (受け入れ基準 4、`grep -rn "#include" components/basic/core/` で確認)。
- `core/` に自己再帰は無い (`eval()` は `eval()` を呼ばず、
  `exec_statement()` はステートメントハンドラから呼ばれない)。
- `core/` にファイルスコープの static 配列は無い (作業バッファもプール)。

**未計測**: ESP32-S3 実機でのスタック high-water。linux の値は代用にならない
ため、S3 実機での計測は B5 (または実機を触る最初の機会) に回す。
esp32s3 ビルド自体は通っている。

## 8. 受け入れ基準セルフチェック

| # | 基準 | 結果 |
|---|---|---|
| 1 | `rake basic:test` green (30 ケース以上) | 達成: 62 passed, 0 failed |
| 2 | `rake build:linux` / `rake build:esp32` 通過、旧 lexer/parser/runtime は削除済み | 達成 (両ビルド成功、4 ファイル削除、CMakeLists から撤去) |
| 3 | linux sim でデモが従来どおり動く | 達成 (sec 5。起動・スクロール・自動終了、出力は記法変更分のみ差分) |
| 4 | コアに IDF / fmruby ヘッダの include が無い | 達成 (sec 7 の grep 結果) |
| 5 | 再帰・static 配列が無いこと + スタック/プール計測 | 達成 (sec 7)。ただし**実機スタック計測は未実施** |
| 6 | `reports/phase_b1_report.md` 完成 (疑義リスト含む) | 本書 |

## 9. 仕様の疑義リスト (spec は変更していない)

B0 report sec 8.1 の 3 件 (エラー表示書式、ロード時エラーの行情報、
文字列 `+` の記載漏れ) は未決のまま。B1 で新たに判明したもの:

| # | 内容 | 採用した挙動 | 影響 |
|---|---|---|---|
| 6 | 負数の整数除算・剰余の丸め方向。core_spec は「小数点以下切り捨て」とだけ書く | 0 方向への切り捨て (`-7/2 = -3`, `-7 MOD 2 = -1`。C と同じ) | 座標計算で符号付きの値を割る作品で結果が変わる。床方向 (`-4`) の可能性あり |
| 7 | オーバーフロー時の挙動 (core_spec sec 1 は「保証されない」、エラー表には OV がある) | 16bit の折り返し (`32767+1 = -32768`) | 実機が OV エラーを出すなら差が出る |
| 8 | 省略形の重複: `D.` が DIM と DATA、`SC.` が SCREEN と SCR$ | 表順で先勝ち (`D.`=DATA、`SC.`=SCREEN) | 省略形を使ったリストの解釈 |
| 9 | FOR 終了後のループ変数の値 (spec に記載なし) | 終値 + STEP (`FOR I=1 TO 5` 後は 6)。MS 系の挙動 | ループ後に変数を使う作品 |
| 10 | 未宣言配列の暗黙 DIM (spec に記載なし) | 各次元 0-10 で自動生成 | DIM を書かない作品 |
| 11 | 文字列の 31 文字制限をテンポラリにも適用するか | 適用する (超過は ST)。ただし **PRINT の裸の文字列リテラルだけは制限外**にして直接出力する | 32 文字以上のメッセージを PRINT する作品が動くかどうか |
| 12 | 文字コード表に無い文字 (長音符「ー」、コード 176/183 の未確定分) | ロード時に `?` (0x3F) へ置換 | カナ文字列を多用する作品の表示 |
| 13 | INPUT のエコー。実機は入力文字が画面に出る | B1 はホストの行入力に委ねエコーしない | B2 のコンソール実装で解決予定 |

## 10. Phase B2 への引き継ぎ

- **画面系ステートメントの受け皿は用意済み**: LOCATE / COLOR / CLS / CGEN /
  CGSET / PALET / SCREEN / VIEW / FILTER はトークン定義済みで、現在は
  `ext_statement` -> IL に落ちている。B2 はコア側に 28x24 シャドウバッファを
  持たせ、`exec_statement()` の switch にハンドラを足す。
- **カーソル桁管理は既にコアにある** (`cursor_col_`)。PRINT のゾーン計算は
  `screen_columns = 28` / `print_zone_width = 8` を参照しているので、
  B2 で行方向 (`CSRLIN`) を足せば揃う。
- **出力経路の切り替え点**: 現在 `put_fb_char()` が Family BASIC コードを
  UTF-8 に変換してホストの `put_char` へ流している。B2 では
  「シャドウバッファへ書く + 変更セルのみ canvas へ描く」に置き換える。
  UTF-8 変換 (`basic_charset.cpp`) はホスト境界に残す。
- **`_SCRDUMP` (B0 sec 3.3)**: トークンを 1 つ足し、`SCRD|<tag>|` 付きの
  行をホストのログチャネルへ流す。ホスト側コールバックを
  `basic_host_t` に 1 本追加する想定 (put_char とは別系統にして、
  ゴールデンランナーでは stdout、実機ではログに出す)。
- **SCR$ / INKEY$ / CSRLIN / POS**: 関数側の枠は `call_builtin()` にあり、
  中立値を返している。B2 で中身を入れる。
- 文字コード表 (`basic_charset.cpp`) は B2 のフォント制作時に
  「コード -> 絵柄」の対応表としてそのまま使える。
