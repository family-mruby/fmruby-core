# SD カードの動画 (MJPEG) を窓の中で再生する — 実装計画

Modern (Tab5 / ESP32-P4) 専用機能。SD カードに置いた連続 JPEG を読み、
P4 のハードウェア復号器で解いて、アプリの指定した位置に表示する。

最終更新: 2026-08-19

実装の記録 (計画から変えた点・確定した事実・実機で出た不具合と実測値) は
`report/p1_p4.md`。**P1〜P4 は Tab5 実機で動作確認まで済んでいる**
(SD の動画が窓の中で 14.77 コマ/秒、落としたコマはほぼ無し)。残るのは P5 の
一部 (コマ毎の内訳、表示できる大きさの上限) と Spinel 側の結び付けの確認。

## 0. 目標と対象外

| 項目 | 内容 |
|---|---|
| 目標 | Ruby アプリが数行で「この動画をここに出す」と書けること |
| 置き場 | SD カード (`/sd/...`)。flash も同じ経路で読める |
| 表示 | アプリが指定した矩形。全画面は当面考えない |
| 速度 | 320x240 前後で 30 コマ/秒。合成が 33ms 間隔なのでこれが上限 |
| 対象 | Tab5 (P4) のみ |

対象外 (今回やらない):

- 音声。動画に音を付ける話は別課題 (同期の設計が要る)
- MP4 や AVI の解析。容れ物は「JPEG をそのまま並べた」形だけを見る
- H.264。P4 は符号化器しか持たない (復号器は無い)
- Retro (S3)。描画が別チップで、間が UART 921600bps。原理的に無理

## 1. 現状 — 既にあるもの / 無いもの

### 使える部品

- **画像を窓の位置に貼る経路**: `main/drivers/display_p4/display_p4_task.cpp`
  の `CREATE_IMAGE_FROM_FILE` (:1839) がファイルを読んで RGB565
  (`rgb565_nonswapped` = 画面拡大器に直結する並び) のスプライトを作り、
  `DRAW_IMAGE` (:1950) が canvas の (x,y) へ貼る。Ruby 側は
  `FmrbGfx#create_image_from_file` / `#draw_image`
  (`lib/add/picoruby-fmrb-app/mrblib/fmrb-gfx.rb:263,269`)。
- **JPEG のハードウェア処理**: `esp_driver_jpeg` は既に依存に入っている
  (`main/CMakeLists.txt:215`)。符号化側の実績が
  `main/drivers/remote_desktop/rd_encoder_jpeg.c` (遠隔画面)。復号は
  対称の API (`jpeg_new_decoder_engine` / `jpeg_decoder_get_info` /
  `jpeg_decoder_process`、出力に RGB565 を選べる) を使うだけ。
  1080p で毎秒 48 コマという処理能力なので、320x240 なら誤差の範囲。
- **`/sd` の道は既に通っている**:
  `components/fmrb_hal/platform/esp32/fmrb_hal_file_esp32.c` が `/sd` と
  `/mnt/sd` を持ち、Ruby の File もこの経路。`fmrb_hal` は fatfs / sdmmc /
  vfs を既に要求している (`components/fmrb_hal/CMakeLists.txt:51`)。

### 無いもの

- **Tab5 で SD が繋がっていない**。`fmrb_pin_assign.h:57-61` の Modern 欄は
  `FMRB_PIN_SD_*` が全て `GPIO_NUM_NC`。既存の実装は SPI 接続 (Retro 用) で、
  Tab5 の microSD は SDMMC 接続なので差し替えが要る。
- 動画を時間どおりに送り出す仕組み (再生器)。
- Linux シミュレーションでの検証手段。**この機能は sim に載らない**
  (sim の描画は graphics-audio 側で、Modern の描画層は Linux では
  ビルドされない)。検証は実機の遠隔操作で行う。

## 2. 設計

### 2.1 容れ物 (ファイル形式)

**JPEG を SOI(FFD8) から EOI(FFD9) まで、そのまま連続させただけの列**を
`.mjpg` として扱う。索引も見出しも持たない。理由は、解析器を書かずに済み、
PC 側で `ffmpeg -c:v mjpeg -f mjpeg` の一行で作れるから。

コマ数/秒はファイルに書かず、**再生を始めるときに Ruby から渡す**。
繰り返し再生の可否も引数。

### 2.2 誰が描くか — 専用の canvas を持たせる

描画の面 (canvas) は「作業中」と「確定」の二枚組で、`PRESENT` が作業中を
確定へ写し、合成はいつも確定側だけを見る (`display_p4_task.cpp:118,273`)。
ここへ再生器が横から書くと、アプリの描画と衝突する。

→ **動画は自分専用の canvas を 1 枚持つ**。位置と大きさはその canvas の
指定 (既存の create/push canvas) がそのまま動画の表示位置になり、重なり順も
既存の窓の仕組みに乗る。アプリは自分の canvas に普段どおり描き、動画の面には
触らない。移動したければ canvas を動かせばよい。

### 2.3 動く筋道

再生器は専用の仕事 (task) を 1 本持ち、次を繰り返す:

1. 次のコマの位置まで待つ (`board_millis` 基準の絶対時刻。ずれを溜めない)
2. SD から 1 コマ分を読む (SOI から EOI まで。読み口は開いたまま)
3. ハードウェア復号器で RGB565 へ解く
4. 動画 canvas の作業中の面へ行ごとに写す (幅は 16 の倍数へ切り上げられて
   出てくるので、右端を捨てながら写す)
5. 確定へ写して再描画を頼む

読み・復号・書き込みは全て C 側で完結し、**Ruby は再生の開始と停止しか
呼ばない**。毎コマ Ruby を経由すると命令の往復とブロック呼び出しの費用が
30 回/秒ぶん乗るため。

コマ落ち: 期限に間に合わなければそのコマは描かずに読み飛ばす (時刻が正)。

### 2.4 追加する命令と Ruby の見え方

命令は 3 つ。番号は `FMRB_LINK_GFX_*` の空き (0x90 付近) を使う。

| 命令 | 中身 |
|---|---|
| `VIDEO_OPEN` | canvas_id, path, fps, flags(繰り返し) → 応答: 幅・高さ・コマ数不明可 |
| `VIDEO_CONTROL` | 再生 / 一時停止 / 停止 / 先頭へ |
| `VIDEO_STATUS` | 再生中か、今何コマ目か、落としたコマ数 |

Ruby:

```ruby
@video = @gfx.video_open("/sd/movie/demo.mjpg", canvas: @vcanvas, fps: 15, loop: true)
@video.play
@video.pause
@video.stop            # 資源を返す
@video.playing?        # 状態の問い合わせ
```

Modern 以外では `video_open` は失敗を返す (アプリ側で分岐できるよう、
例外ではなく nil を返し、理由をログに出す)。

### 2.5 触るファイル

一本の命令を通すのに要る場所 (既存の DRAW_IMAGE と同じ並び):

- `components/fmrb_common/include/fmrb_link_protocol.h` — 命令番号と構造体
- `components/fmrb_gfx/fmrb_gfx_cmd.{h,c}` — 命令を組み立てる関数
- `main/drivers/display_p4/display_p4_task.cpp` — 受け口
- `main/drivers/display_p4/display_p4_video.{h,cpp}` — **新規**。再生器本体
- 結び付け 2 種 (どちらも要る):
  - mruby: `lib/add/picoruby-fmrb-app/ports/esp32/gfx.c` + `mrblib/fmrb-gfx.rb`
  - Spinel: `main/app/fmrb_spx_gfx.c` + `main/prebuild_scripts/spinel/fmrb_app_ffi.rb`
- SD: `components/fmrb_common/include/fmrb_pin_assign.h` と
  `components/fmrb_hal/platform/esp32/fmrb_hal_file_esp32.c`

## 3. 作業段階

### P1: Tab5 で SD を読めるようにする (ここが唯一の未踏)

やること:

- `fmrb_pin_assign.h` の Modern 欄に SDMMC の足を定義する。
  **実機資料での確認値** (要実測): CLK=43, CMD=44, D0=39, D1=40, D2=41, D3=42。
  挿抜検出の足は無い前提で組む。
- `fmrb_hal_file_esp32.c` の SD の入り口を接続方式で分ける。Retro は今の
  SPI のまま、Modern は `esp_vfs_fat_sdmmc_mount` (4 本線、失敗したら 1 本線へ
  落とす)。挿抜検出の足が無い場合は「検出できない = 挿さっているつもりで
  試す」に変える。
- 今の実装は挿抜検出の足を無条件に設定していて、Modern では足が `NC` の
  まま `gpio_config` を通っている。ここも整理する。

完了の条件: Tab5 実機で `/sd` の一覧が取れ、Ruby から
`File.open("/sd/test.txt")` が読める。ブートログに mount 成功が出る。
`M1|` の記録で内蔵メモリの増分を見ておく。

→ **完了** (2026-08-19)。増分は 3KB。一覧はデスクトップのファイル管理から
`/mnt/sd` を辿って確認できる (仮想の根には `/sd` が並ばない)。

### P2: 静止画 1 枚をハードウェア復号で出す

やること:

- 復号器の初期化・破棄と「JPEG のかたまり → RGB565 の面」を 1 本の関数にする
  (`display_p4_video.cpp` の下半分になる)。
- 既存の `CREATE_IMAGE_FROM_FILE` を拡張し、先頭が `FFD8` なら JPEG として
  この関数で解く (PNG は今のまま)。あわせて、パスが `/sd` で始まるときに
  `/flash/` を前置しない (:1848 の扱い) よう直す。

完了の条件: SD に置いた 1 枚の JPEG が、既存の `draw_image` で窓の指定位置に
出る。色が入れ替わっていないこと (符号化側と同じ確認)。遠隔の画面取得で確認。

→ **完了** (2026-08-19)。色は入れ替わっていた (バイト順。report の 8 を参照)。
直したあとは元ファイルと 6 色すべて一致。

**この時点で「SD の絵をアプリに出す」は成立する**。以降は動かす話。

### P3: 再生器 (C 側) と命令 3 つ

やること:

- 連続 JPEG の切り出し (SOI/EOI 走査。読み口は開いたまま、先読みは 1 コマ)
- 再生の仕事 1 本、時刻管理、コマ落とし、繰り返し
- 命令 3 つを protocol → cmd → 受け口の順に通す
- 動画 canvas への書き込みと確定、再描画要求

完了の条件: 実機で 320x240 / 15 コマ秒の動画が窓の中で動き続ける。
`fmrb_task:` でスタックの残り、`GFX STATS` で描画時間、再生器のログで
落としたコマ数を採る。

→ **完了** (2026-08-19)。320x176 / 15 コマ秒で 14.77 コマ秒、21.3 秒で
落としたコマは 1。落としたコマ数はログではなくアプリの状態表示から読む。

### P4: Ruby API と作例アプリ

やること:

- mruby と Spinel の両方の結び付け、`fmrb-gfx.rb` の包み
- 作例 `flash/app/demo/video_play.app.rb` (SD の一覧から選んで再生、
  再生/一時停止/停止のボタン、Ctrl+Q で終了)
- 変換手順の覚え書き (ffmpeg の一行) を doc に置く

完了の条件: 遠隔操作 (`fmrb_rd_launch` → `fmrb_rd_snap`) で、アプリを起動して
再生・一時停止・終了まで通ることを確認。

→ **完了** (2026-08-19、mruby 側)。起動と終了を 3 回繰り返して内蔵 RAM も
PSRAM も元に戻る。**Spinel 側の結び付けはまだ通していない**。

### P5: 測って詰める

- コマ毎の内訳 (読み / 復号 / 写し) を計測して記録する
- 内蔵メモリの増分を確定させる (復号器の入出力は直接転送できる領域が要る)
- 大きさの上限を決める (426x240 まで通るか、その時のコマ数)
- 分かった数字と、やめた案を本文書へ書き戻す

## 4. 記憶の見積り

| 用途 | 大きさ | 置き場 |
|---|---|---|
| 読み込み中の 1 コマ (JPEG) | 64KB 見当 | 直接転送できる領域 |
| 復号の出力 (RGB565、幅は 16 の倍数) | 320x240 で 150KB | 直接転送できる領域 |
| 動画 canvas | 表示の大きさぶん | PSRAM (既存の canvas と同じ) |

PSRAM は 32MB あるので総量は問題にならない。注意すべきは**内蔵メモリ**で、
復号器の入出力は専用の確保関数で取る必要がある。P1/P2 の各段階で `M1|` の
記録を突き合わせ、既知の逼迫を悪化させていないか確かめる。

## 5. 危険と未確認

- ~~**Tab5 の SD の足**は実機で確かめるまで確定しない~~ → **解決**。机上の
  想定どおり (CLK=43 / CMD=44 / D0-D3=39-42、slot 0、内蔵 LDO の 4 番) で、
  4 本線 40MHz で mount できた。
- ~~**無線と足がぶつからないか**~~ → **解決**。C6 との連絡も SDIO だが足が別
  (CLK[12] CMD[13] D0[11] D1[10] D2[9] D3[8]) で、両方立ち上がったまま
  遠隔操作しながら再生できる。
- **SD の読み出しは速さが要る**。1 バイトずつ読むと 220KB/s しか出ず、
  15 コマ秒に届かない (実機で踏んだ。report の 8 を参照)。
- **fatfs の LFN が要る**。既定値のままだと 8.3 名しか見えず、PC で付けた
  名前でファイルを開けない。
- **シミュレーションで検証できない**。この機能の確認は全て実機。手順は
  遠隔操作の道具で自動化できるので、各段階の完了条件に画面取得を入れてある。
- 世代の違う Tab5 (画面が別部品) では今のファームは画面が出ない。検証は
  対応済みの世代の実機で行う。
