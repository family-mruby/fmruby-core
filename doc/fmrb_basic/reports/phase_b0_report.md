# Phase B0 報告書: 検証基盤とコーパス整備

実施: 2026-07-27 / ブランチ `fmrb-basic` (fmruby-core)
対象タスク: `phase_b0.md` T0-1 - T0-6

Phase B0 は検証基盤づくりのフェーズであり、**言語機能の実装は行っていない**。
成果物は「C++ コアの器」「ホストゴールデンテスト機構」「2 つの技術判断」
「コーパスの雛形」「既存デモの回帰基準」。

## 0. 成果物一覧

| 追加/変更 | 内容 |
|---|---|
| `components/basic/core/basic_core.hpp` | ホスト非依存の純粋 C++ コア API (ホスト I/F・エラーモデル・行テーブル) |
| `components/basic/core/basic_core.cpp` | 同実装 (B0 スケルトン) |
| `components/basic/CMakeLists.txt` | `core/basic_core.cpp` を SRCS に、`core` を INCLUDE_DIRS に追加 |
| `components/basic/test/runner/main.cpp` | ホスト g++ 用ランナー (stdout 出力・malloc・tick no-op) |
| `components/basic/test/golden/` | 機構確認ケース 4 本 (.bas + .expected) |
| `components/basic/test/run_golden.sh` | 全ケース実行・バイト一致判定・合否集計 |
| `components/basic/test/samples/` | 自作サンプル 3 本 + README |
| `Rakefile` | `rake basic:test` / `basic:runner` / `basic:run` / `basic:clean` を追加 |
| `doc/fmrb_basic/reports/corpus_inventory.md` | 作品棚卸し表の雛形 |

## 1. T0-1: C++ ビルド経路の開通

### 実施内容

`components/basic/core/` を新設し、以下を含む純粋 C++ スケルトンを置いた。

- `basic_host_t`: ホストインタフェース (関数ポインタ構造体)。
  メモリ確保 `alloc` / 解放 `dealloc`、1 文字出力 `put_char`、
  行入力 `read_line`、時刻 `ticks_ms`、協調 tick `on_tick`、
  エラー通知 `on_error`、および `user` ポインタ。
- `error_code`: V3 エラー表 22 種の enum class。値はエラー番号
  (ERR が返す番号) と一致させてある。`error_mnemonic()` / `error_message()`
  は constexpr テーブル (rodata) 引き。
- `interpreter`: コンストラクタは確保を行わない (placement new 可能)。
  `init(line_capacity, text_capacity)` でホスト経由に行テーブルとテキスト
  アリーナを確保、`load()` で行番号付きソースを取り込み、`run()` は B0 では
  行テーブルを走査して `on_tick` を回すだけのスタブ。
  `raise()` が Family BASIC 形式のエラー表示を行う。

規約適合:

- IDF / fmruby ヘッダの include はゼロ (`<cstddef>` / `<cstdint>` のみ)。
- 例外・RTTI なし、heap を使う STL なし、グローバルコンストラクタなし。
- 00_common の「メモリ設計原則」(2026-07-27 追記) にも適合:
  行テーブル・テキストアリーナはすべてホスト (= per-task プール) から確保し、
  ファイルスコープの static バッファは持たない。エラー表は constexpr で
  rodata に置く。再帰呼び出しは使っていない (行検索も二分探索の反復)。

### ビルド結果

| ターゲット | コマンド | 結果 |
|---|---|---|
| ESP32-S3 (NARYAv3, spinel kernel/desktop) | `rake build:esp32` | 成功。`build/esp-idf/basic/CMakeFiles/__idf_basic.dir/core/basic_core.cpp.obj` を確認 |
| Linux (IDF linux target, Debug) | `rake clean_all && rake build:linux` | 成功。`build/esp-idf/basic/CMakeFiles/__idf_basic.dir/core/basic_core.cpp.o` を確認 |
| ホスト g++ (ゴールデンテスト) | `rake basic:test` | 成功 (`-std=c++20 -fno-exceptions -fno-rtti -Wall -Wextra -Werror`) |

### linux ターゲットで .cpp をビルドした結果 (指示書の懸念点)

**問題は起きなかった**。必要だったのは CMakeLists への
`core/basic_core.cpp` 追加と `core` の INCLUDE_DIRS 追加のみで、
コンパイラフラグ・リンクフラグの追加は不要だった。理由:

- IDF の linux ターゲットはホストの gcc/g++ ツールチェインをそのまま使い、
  コンポーネントビルドは拡張子で C/C++ を切り替える (`.cpp` は自動的に
  g++ 経由)。libstdc++ のリンクも IDF 側が面倒を見る。
- コアが STL・例外・RTTI・グローバル new を使わないため、ランタイム依存が
  実質 rodata のみで、リンク時に追加ライブラリを要求しない。

なお、`main/drivers/display_p4/*.cpp` が既に P4 ターゲットで .cpp を
ビルドしているため、esp32 側は元から前例があった。B0 で新しく通ったのは
**linux ターゲットでの .cpp** と **ホスト g++ 単体ビルド**の 2 経路。

## 2. T0-2: ホスト・ゴールデンテストランナー

`rake basic:test` でホストの g++ がコア + ランナーをビルドし、
`components/basic/test/run_golden.sh` が全ケースを回す。docker も IDF も
不要 (`CXX` で任意のコンパイラを指定可)。

- ランナー: `basic_runner program.bas [input.txt]`。
  出力はプログラム出力 + 正常終了時の `OK`。エラー時はコアが
  `?SN ERROR IN 10` 形式で出力し、終了コードは 1。
  `ticks_ms` は「呼ぶたびに +1」の決定的な擬似時計にしてある
  (ゴールデン出力が実時間に依存しないようにするため)。
- 判定: `.expected` とのバイト単位完全一致 (`cmp`)。差分は `diff -u` 表示。
- 実行フィルタ: `rake basic:test FILTER=003` で部分実行。
- 単発実行: `rake basic:run BAS=path/to/x.bas [IN=input.txt]`。

現時点のケース (機構確認用、B1 で本格的なケースに置き換わる):

| ケース | 内容 | 期待出力 |
|---|---|---|
| `001_empty_program` | 空プログラム | `OK` |
| `002_line_table` | 行番号が降順で並んだ 3 行を読み込む | `OK` |
| `003_missing_line_number` | 行番号のない行 | `?SN ERROR` |
| `004_line_number_range` | 行番号 70000 (範囲外) | `?SN ERROR` |

実行結果: `golden: 4 passed, 0 failed`。

エラー表示形式は Family BASIC の画面表示に合わせて
「`?` + 2 文字コード + ` ERROR` + (行が特定できる場合) ` IN <行番号>`」と
した。ロード時 (行番号が確定しないケース) は `IN` を付けない。
**この表示形式は spec に明文がないため疑義リスト (sec 8) に記録した**。

## 3. T0-3: テキスト画面ダンプの debug 経路 (設計・要判断)

B2 以降、28x24 シャドウバッファのテキストダンプを取得して diff する。
取得手段を評価した。

### 3.1 候補と評価

| 案 | 内容 | headless での扱いやすさ | 実機で同経路か | 実装コスト |
|---|---|---|---|---|
| (a) ファイル出力 | BASIC 側のデバッグ命令が `/home` 以下にダンプを書く | 良: linux では `flash/` がホスト側の実ディレクトリなので即読める | 一応可。ただし実機は littlefs 上のファイルを吸い出す手段が別途必要 | 小 |
| (b) 既存 debug 経路 | debugd (TCP 5555 / BLE, msgpack) に問い合わせコマンドを追加 | 中: クライアントを書く必要あり | 可。ただし debugd は mruby VM 向け設計で、コマンドは VM アタッチ前提 | 大 (プロトコル + 両端実装) |
| (c) linux 限定 stdout | linux sim のみ標準出力に吐く | 良 | **不可** (実機で使えない) | 小 |
| (d) ログ経路 + マーカー | `fmrb_log.h` 経由でマーカー付きのダンプを出す | 良: ハーネスは既に `docker logs fmruby_core` を grep している (dev_run_check.sh のブートマーカー待ちと同じ経路) | 可: 実機では UART シリアル (`rake monitor`) に同じ文字列が出る | 小 |

### 3.2 推奨: (d) を主経路、(a) を大量ダンプ時の補助

推奨理由:

1. **同一経路が linux・実機の両方で成立する**。linux は
   `docker logs fmruby_core`、実機は `rake monitor`。ハーネス側に新しい
   通信路を足さない。
2. **既存の検証フローに乗る**。`tools/dev_run_check.sh` は既にコンテナログを
   grep してブートマーカーを待つ実装なので、同じやり方でダンプを抜ける。
   ダンプの全行に固有プレフィクスを付け (sec 3.3)、**範囲抽出ではなく
   行フィルタ**で取れるようにする。BASIC タスクのログと他タスクのログは
   同じストリームに混ざるため、BEGIN/END の間を切り出す方式では他タスクの
   行が割り込んだ時点で壊れる。
3. **コアの設計と相性が良い**。ダンプ整形をコア (`core/`) に置き、出力を
   `basic_host_t` のコールバックへ 1 文字ずつ流す構造にすると、
   **ホストゴールデンランナーでも同じダンプが得られる**。つまり B2 の画面
   仕様の大半は docker を起動せず `rake basic:test` だけで回帰できる
   (linux sim は実際の描画・入力の確認に限定できる)。整形バッファを
   持たずストリーム出力にできるので、メモリ設計原則にも沿う。
4. 実機のファイル吸い出し手段に依存しない。

(a) は「1 画面より大きいダンプ (BG 面 2 面分、CGSET 前後の比較等) を
取りたい」場合の補助として残す。(b) は debugd が VM 前提の設計であり、
BASIC (C++ タスク) 用に別コマンド系を足すコストに対して得るものが薄い。
(c) は実機で使えないため単独では不採用。

### 3.3 ダンプ形式 (提案)

命令名は `_SCRDUMP [tag]` (先頭アンダースコアは Family BASIC の変数名・
命令名として出現しないため、既存プログラムと衝突しない)。デバッグ出力が
無効なときは no-op。

**全行が `SCRD|<tag>|` プレフィクスで始まる**。ログの行頭にはタイムスタンプと
タスク名が付き、他タスクの行がいつでも割り込むため、範囲抽出 (BEGIN と END の
間を切り出す) は使わない。行フィルタだけで 1 ダンプ分が揃う形式にする。

```
SCRD|1|BEGIN 28x24 bg=0
SCRD|1|T00 20 20 46 41 4D 49 4C 59 ... (28 バイトを 16 進で)
...
SCRD|1|T23 ...
SCRD|1|A00 |  FAMILY BASIC             |
...
SCRD|1|A23 |                           |
SCRD|1|C00 00001111222233330000111122222222
...
SCRD|1|END 50
```

- 命令名は `_SCRDUMP [tag]` (先頭アンダースコアは Family BASIC の変数名・
  命令名として出現しないため、既存プログラムと衝突しない)。デバッグ出力が
  無効なときは no-op。
- `<tag>`: `_SCRDUMP` の引数 (省略時 0)。同じ実行で複数回ダンプするとき、
  タグで抽出対象を選べるようにする。
  抽出は `docker logs fmruby_core | grep -o 'SCRD|1|.*'`
  (実機は `rake monitor` の出力に対して同じフィルタ)。
- `BEGIN` 行: 画面サイズと BG 面番号。`END` 行: 本文の行数
  (欠落・切り詰めの検出用。ログのドロップに気付けるようにする)。
- `T<行>`: 文字コード 28 バイトを 16 進 2 桁で。**これが比較の正**
  (カナ・背景パターンはコードでしか正しく表せないため)。
- `A<行>`: 人間が差分を読むための ASCII 透過表記。32-126 はそのまま、
  それ以外は `.`。桁は必ず 28 文字固定 (末尾空白をトリムしない)。
- `C<行>`: 配色番号 (0-3) を 1 桁ずつ 28 文字。**既定では出力しない**
  (`_SCRDUMP` の引数か debug フラグで有効化)。COLOR / CGSET の検証時のみ
  使う。
- ゴールデンの `.expected` にはプレフィクスを含めた行をそのまま格納する
  (ホストランナーとログ経路で同じ文字列になり、比較器を 1 つで済ませる)。

**ユーザ判断待ち**: 上記 (d) 案 + この形式で確定してよいか。

## 4. T0-4: PLAY 実装深度の技術判断 (調査結果・要判断)

### 4.1 音源経路の現状 (調査結果)

- **共通のコマンド系**: `main/drivers/audio_p4/audio_commands.h` と
  fmruby-graphics-audio 側 `main/common/audio_commands.h` が同一定義。
  LOAD_BINARY(0x01) / PLAY(0x02) / STOP / PAUSE / RESUME / SET_VOLUME /
  PLAY_SLOT(0x08) / NOTE_ON(0x09) / NOTE_OFF(0x0A) / LOAD_FMSQ_FILE(0x0B)。
- **経路**: アプリ -> (kernel の audio_handler.rb がバイナリ化) -> host_task
  -> 音源。ただし host_task はこのメッセージを **中身を見ずに転送する**
  だけなので、C/C++ アプリからは `FMRB_MSG_TYPE_APP_AUDIO` の生バイナリを
  `PROC_ID_HOST` へ直接送れる (gfx 拡張が gfx コマンドで既にやっている
  やり方と同じ)。BASIC タスクから Ruby を経由する必要はない。
- **3 ターゲットすべてで同じコマンドが通る**: Retro=WROVER の audio_task、
  Modern=ローカル `main/drivers/audio_p4/`、linux=SDL2 側プロセスの
  apu_emu。
- **FMSQ の表現力**: フレーム (1/60 秒) 単位のコマンド列。
  チャンネルは pulse1 / pulse2 / triangle / noise の 4 系統
  (Family BASIC の「方形波 2 + 三角波 1 + 効果音 1」と一致)。
  NOTE_ON は pulse で timer(2) + vol_env(1) + sweep(1) を、triangle で
  timer(2) + linear(1) を持ち、**デューティ・音量・エンベロープ有無は
  vol_env バイトにそのまま入る**。PARAM で途中変更、WAIT は 1-128 フレーム、
  LOOP / END あり。さらに任意の APU レジスタ直書き (REG_WRITE) も定義済み。
  -> **spec の PLAY (O/T/V/M/Y/R/音長/3 重音) は FMSQ で完全に表現できる**。
- **リアルタイム経路も既にある**: NOTE_ON(ch, freq, vol, duty, sweep) /
  NOTE_OFF(ch)。ただし実装 (`apu_helper.c`) が
  「constant volume (0x30)」を固定で書くため、**M1 (エンベロープあり) は
  現状のままでは表現できない**。デューティ・音量・スイープは指定できる。

### 4.2 判明した制約 (これが判断を変える)

アプリ -> host_task のメッセージ payload は
`FMRB_MAX_MSG_PAYLOAD_SIZE = 176` バイト。したがって
**LOAD_BINARY によるインライン FMSQ 転送は 1 回あたり実質 167 バイト**しか
運べない。PLAY 1 回分の MML は、3 チャンネル x 数十音で数百バイト -
数 KB になるため、compat_plan sec 5.4 の a 案 (MML -> FMSQ 事前変換 +
既存 load_fmsq) は **そのままでは成立しない**。

回避策は 3 つ:

- a-1) FMSQ をファイルに書き、`LOAD_FMSQ_FILE` で読ませる。
  プロトコル変更ゼロだが、Retro/linux では音源側のファイルシステムに
  ファイルを転送する必要があり (file transfer 経路)、PLAY のたびに
  ファイル書き込み + 転送が走る。効果音を連打するゲームでは不適。
- a-2) **チャンク分割ロードコマンドを 1 つ足す** (例:
  `LOAD_BINARY_CHUNK`: music_id + offset + total_size + 最大 160 バイト)。
  1KB の FMSQ でも 7 メッセージ程度。実装は 3 バックエンド
  (graphics-audio の audio_handler_shm / esp32、core の audio_p4_handler) に
  各 20-30 行 + 組み立てバッファ。
- b) MML シーケンサをインタプリタの tick 内に実装し、毎フレーム
  NOTE_ON/NOTE_OFF を送る。プロトコル変更ゼロ。ただし
  (i) M1 エンベロープが出せない、(ii) 音のタイミングがリンク越しの
  ジッタを受ける、(iii) BASIC プログラム終了後も鳴り続ける挙動を
  作りにくい (tick が止まる)。

### 4.3 推奨: a-2 (チャンク転送を足して MML -> FMSQ) + BEEP は b

- **PLAY**: MML を BASIC 側で FMSQ に変換し、チャンク分割ロード ->
  `PLAY_SLOT` で再生する。理由:
  - 再生は音源側が 1/60 フレームで駆動するので **タイミングが正確**で、
    リンクのジッタを受けない。Retro (S3 <-> WROVER) で特に効く。
  - Family BASIC の PLAY は非同期で、プログラムの実行と独立に鳴り続ける。
    FMSQ ならインタプリタ側の tick が止まっても再生が続く。
  - M/Y/V (エンベロープ・デューティ・音量) を含む spec の全パラメータが
    そのまま表現できる。
  - 追加コストはコマンド 1 つ (3 バックエンド)。a-1 のような毎回の
    ファイル I/O が要らない。
- **BEEP**: 固定の短音なので NOTE_ON + 数フレーム後の NOTE_OFF で足りる
  (プロトコル変更なし、即時性が高い)。FMSQ スロットを使わないので
  BGM 用スロットを潰さない。

**b 単独は非推奨**: エンベロープ非対応とタイミングジッタが、ベーマガ作品の
BGM 再現性に直接効く。逆に a-2 を採っても、効果音の即時性は BEEP と同じ
NOTE_ON 経路で確保できる。

**最終決定はユーザ**。a-2 を採る場合、fmruby-graphics-audio 側の変更が
1 件発生する (別リポジトリのため、B3 着手時に合わせて依頼が必要)。

未検証事項 (B5 で実機確認): Retro の UART/SPI リンクで PLAY 更新
(数メッセージ x 数百バイト) がゲームループの描画コマンドと競合しないか。

## 5. T0-5: ターゲット作品の棚卸し準備

- `doc/fmrb_basic/reports/corpus_inventory.md` に棚卸し表の雛形を作成。
  作品別サマリ / ステートメント・関数の使用マトリクス / POKE・PEEK
  アドレス頻度表 / 集計 / 疑義欄。作品コードはリポジトリに収録しない旨を
  冒頭に明記した。
- 自作サンプル 3 本を `components/basic/test/samples/` に配置
  (`README.md` に各サンプルの守っている記法ルールと昇格予定フェーズを記載):

| サンプル | 範囲 |
|---|---|
| `sample_01_fizzbuzz.bas` | FOR/NEXT、MOD、IF-THEN (行番号形)、GOTO、PRINT |
| `sample_02_strings.bas` | 文字列変数・連結、LEN/LEFT$/RIGHT$/MID$/ASC/CHR$/VAL/STR$ |
| `sample_03_data_array.bas` | DIM、DATA/READ/RESTORE、GOSUB/RETURN、整数除算 |

サンプルは Family BASIC 記法に従っている (LET を使わない、NEXT に
ループ変数名を付けない、整数のみ、文字列 31 文字以内)。B0 時点では実行
できないため golden ケースには入れず、B1 で昇格させる。

ターゲット作品リストはユーザから受領後に棚卸しを実施する。

## 6. T0-6: 既存デモの回帰基準

### 6.1 `flash/app/demo/basic.app.bas` が使用している構文

REM / PRINT (`;` と `,` の両方) / LET / FOR-TO-STEP-NEXT (NEXT に
変数名付き) / IF-THEN (文形) / GOSUB-RETURN / END / WAIT。
gfx 拡張命令 (CLS / CIRCLE / PRESENT) は**デモでは使っていない**
(現行パーサが対応しているだけ)。出力先は `basic_console`
(fmrb_basic_gfx.c) のウィンドウで、`.app.toml` の設定は
`default_window_mode = "window"` / 200x150 / 位置 (10,15)。

### 6.2 Family BASIC 仕様との差分 (B1 の判断ポイント)

デモのソースは **Family BASIC 互換ではない**:

| デモの記述 | Family BASIC (spec) |
|---|---|
| `LET A = 10` | LET という命令語は存在しない (代入は `A=10`) |
| `NEXT I` | NEXT にループ変数名を付けることは不可 |
| `WAIT 1000` (ms) | WAIT は無い。`PAUSE n` (n は 1/60 秒単位) |
| `PRINT "  ", I` | `,` は幅 8 桁ゾーンへのタブ (現行実装は単純連結) |

対応の選択肢:

- (i) デモを Family BASIC 記法に書き換える (`LET` 削除、`NEXT`、
  `WAIT 1000` -> `PAUSE 60`、ゾーン出力に合わせて整形)。
- (ii) コアに LET / `NEXT <var>` / WAIT を fmruby 拡張として残す。

**推奨は (i)**。デモは自前のファイルであり、B1 のコアを「Family BASIC
互換の正」に保つほうが以後の検証がぶれない。compat_plan sec 7-5 の
「デモの互換を維持する」は「デモが動き続けること」を指すと解釈し、
記法の書き換えで満たす。(ii) は互換テストの基準を曖昧にする。
**B1 冒頭でユーザ確認する**。

### 6.3 現状動作の記録 (B1 置換後も維持すべき挙動)

検証にあたり、fmruby-graphics-audio の build/ が ESP32 (WROVER) ビルドの
ままだったため、ユーザ確認のうえ `rake build:linux` で linux ビルドに
切り替えた (WROVER 実機に焼く際は同リポジトリで `rake clean_all &&
rake build:esp32` が必要)。

linux sim (headless ハーネス) でランチャから「BASIC app demo」を起動し、
最後まで実行されることを確認した (gen=1 - gen=5 の 5 回、いずれも
`BASIC program executed successfully` -> 正常終了 -> リソース解放まで到達)。

**維持すべき挙動 (B1 置換後の回帰基準)**:

1. ランチャのアイコンから起動でき、タイトルバー「BASIC app demo」の
   200x150 ウィンドウ (位置 10,15) が開く。
2. コンソールは行が溢れると 1 行ずつスクロールし、最新行が見える。
3. プログラムが最後まで走り、`Program completed!` の後に約 5 秒
   (`WAIT 5000`) 待って自動終了、canvas とタスクが解放される。
4. PRINT 出力の内容と書式が下記と一致する (下記は `basic_gfx: PRINT:` の
   ログから採取した実際の出力。**空行も含めて 37 行**):

```
Hello from BASIC!
FMRuby BASIC Interpreter

A =  10
B =  20
C = A + B =  30

Counting from 1 to 5:
   1
   2
   3
   4
   5

Counting down from 10 to 0 by 2:
   10
   8
   6
   4
   2
   0

X =  15
X is greater than 10
X equals 15

Calling subroutine...
  Inside subroutine
  Y =  100
Returned from subroutine

Testing arithmetic:
  5 * 3 =  15
  20 / 4 =  5
  (2 + 3) * 4 =  20

Program completed!
```

書式で注目すべき点 (B1 の実装で意図的に合わせる / 変える判断が要る):

- 数値は前に符号 1 桁分のスペースが入る (`A =  10`)。これは
  core_spec sec 6 の PRINT の規定 (数値は前に符号分 1 桁) と一致しており、
  **そのまま維持でよい**。
- `PRINT "  ", I` の `,` は現行実装では単純な区切りで、ゾーンタブに
  なっていない (`   1` = 文字列 2 桁 + 数値の符号スペース + 数字)。
  Family BASIC では幅 8 桁ゾーンの頭へタブするため、**B2 でゾーンを
  実装すると出力が変わる** (デモの見た目も変わる)。sec 6.2 の
  書き換え方針と合わせて扱う。

**ハーネス運用メモ (B2 以降で有用)**: linux sim ではランチャのダブル
クリックから最初の PRINT まで 15-25 秒かかる (アプリ spawn が遅い)。
固定 sleep でスクリーンショットを撮ると起動直後の画面しか取れないため、
`docker logs fmruby_core` を条件付きでポーリングしてから撮ること。
sec 3 のログ経路によるダンプ取得は、この待ち合わせと同じ仕組みで動く。

## 7. 受け入れ基準セルフチェック

| # | 基準 | 結果 |
|---|---|---|
| 1 | linux / esp32s3 の両ビルドに components/basic の .cpp が含まれて通る | 達成 (sec 1、両ビルドでオブジェクト生成を確認) |
| 2 | `rake basic:test` がホストで走り、機構確認ケースが green | 達成 (4 passed, 0 failed) |
| 3 | テキストダンプ経路・PLAY 深度の 2 判断が選択肢 + 推奨付きで書かれている | 達成 (sec 3、sec 4) |
| 4 | 棚卸しテンプレートと自作サンプルが存在する | 達成 (sec 5) |
| 5 | `reports/phase_b0_report.md` 完成 | 本書 |

## 8. 仕様の疑義リスト (spec は変更していない)

### 8.1 未決 (資料または実機確認待ち)

| # | 内容 | 影響 | 暫定対応 |
|---|---|---|---|
| 1 | エラーの画面表示書式が spec に無い (2 文字コードとメッセージ表のみ)。実機は `?SN ERROR IN 10` 形式か、`ERROR` の語や `?` の有無が違うか | 全ゴールデンテストの期待値 | `?<2 文字> ERROR[ IN <行>]` を暫定採用。実機表示が判明したら 1 箇所 (`interpreter::raise`) の修正で追従できる |
| 2 | ロード時 (行番号が確定しない) 構文エラーの表示に行情報を付けるか | 同上 | 行情報なし (`?SN ERROR`) を暫定採用 |
| 5 | 文字列連結演算子 `+` が core_spec sec 3 の演算子表に無い (V3 spec の DEF SPRITE 例では使用されている) | 文字列式の実装 | 連結ありとして実装予定 |

### 8.2 ユーザ判断で確定 (2026-07-27)

| # | 内容 | 確定した挙動 | 根拠 |
|---|---|---|---|
| 3 | `IF 式 THEN 文` の後に `:` で続く文が、条件不成立時にスキップされるか | **スキップする (行末まで)**。MS 系・Hu-BASIC 系に共通の挙動 | ベーマガ作品はこの挙動に依存して書かれており、逆の実装では作品がまず動かない |
| 4 | PLAY が非同期か (プログラム実行と並行して鳴り続けるか) | **非同期で確定** | 「BGM を鳴らしながらゲームが進む」作品が成立している事実そのもの |

いずれも B3 の作品ブリングアップで実証的に再検証する
(作品が期待どおり動けば挙動の裏付けになる)。

## 9. ユーザ判断待ち事項

1. **T0-3**: テキストダンプ経路を (d) ログ経路 + マーカー、形式は sec 3.3 で
   確定してよいか。
2. **T0-4**: PLAY を a-2 (MML -> FMSQ + チャンク転送コマンド追加) で
   進めてよいか。採用時は fmruby-graphics-audio 側の変更が 1 件発生する。
3. **T0-5**: ターゲット作品リストの提供。
4. **T0-6/sec 6.2**: デモを Family BASIC 記法へ書き換える方針でよいか。
5. 疑義リスト (sec 8) の #3・#4 の挙動確認。

(以下 9.1 のとおり、1-5 はすべて裁可済み。残る未決は sec 8.1 の 3 件のみ。)

## 9.1 裁可結果 (2026-07-27 ユーザ決定)

sec 9 の判断待ち事項はすべて裁可された。以後のフェーズはこの決定を前提とする。

1. **ダンプ経路: (d) 案で確定。ただし形式を修正**: BEGIN/END 間の行を
   裸で出すのではなく、**ダンプの全行に固有プレフィクスを付ける**
   (`SCRD|<tag>|...`)。他タスクのログ割り込みに耐えるよう、範囲抽出
   ではなく行フィルタで取得できる形式とする。
   **sec 3.3 を修正済み** (実装は B2)。
2. **PLAY: a-2 (MML -> FMSQ + チャンク分割ロードコマンド追加) で確定**。
   コマンド定義は両リポジトリの `audio_commands.h` 同一定義を保ち、
   B3 着手時に fmruby-graphics-audio 側と同時に変更する (別リポジトリの
   作業調整はレビュー担当経由)。BEEP は NOTE_ON/OFF 経路。
3. **作品リスト**: ユーザから受領後に棚卸し (実装はブロックしない)。
4. **デモは Family BASIC 記法へ書き換える (案 i) で確定**。コアに
   LET / NEXT 変数名 / WAIT の互換拡張は入れない。
5. **疑義 #3: MS 系挙動で確定** (IF..THEN 後の `:` 連結文は条件不成立時に
   行末までスキップ)。**疑義 #4: PLAY 非同期で確定**。いずれも B3 の
   作品ブリングアップで実証的に再検証する。

## 10. Phase B1 への引き継ぎ

- `interpreter::load()` は現在「行番号 + 生テキスト」を保持する。B1 で
  クランチ済みトークン列に置き換える。行テーブル (ソート済み + 二分探索) と
  テキストアリーナの確保・再利用ロジックはそのまま使える。
- `run()` は行テーブルを走査して `on_tick` を回すだけのスタブ。ここに
  トークン実行エンジンを入れる。式評価は 00_common のメモリ設計原則に従い
  **再帰下降ではなく明示スタックの反復実装**にすること (B0 のコアには
  再帰は 1 箇所も無い)。
- `fmrb_basic.c` からコアへの接続 (placement new + `basic_host_t` の実装:
  `fmrb_malloc`、basic_console への 1 文字出力、`fmrb_app_poll_exit_signal`
  を `on_tick` に接続) は B1 の作業。既存の C API
  (`include/fmrb_basic.h`) は変更しない。
- 仮実装 (`components/basic/basic/lexer.c` / `parser.c` / `runtime.c`) は
  B1 で撤去する。撤去まで両者が同居するが、CMakeLists 上は別の SRCS 変数に
  分けてあるため、削除は 1 行の変更で済む。
- ゴールデンケースは B1 で本格拡充する (現行 4 本は機構確認用。
  `test/samples/` の 3 本を expected 付きで昇格させるところから)。
