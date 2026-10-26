# RG4 報告: 実機の起動ログを基準と比べる (コマンド 1 つで)

> 状態: 完了 | 更新: 2026-10-26 | `rake regress:boot BOARD=<端末>` (ファームを作り、app の区画だけ焼き、リセットして起動ログを取り、crash マーカー・M1| の段・IRAM free・タスクの stack の空きを基準と比べる) を作った。端末のつなぎ方は targets.rb の `serial:`。基準は P4-Nano (NARYAv4) で 802b16a3 のファームから取った。確認できた実機は P4-Nano だけ

## 1. 作り

| ファイル | 役目 |
|---|---|
| `tools/regress/boot.rb` | 本体。端末の選択 → ポートを VID:PID で探す → ポートを使っている物がいないか確認 (いれば触らずに止まる) → (焼くとき) build/ を退避して固定の設定でビルドし `rake flash:app` で app の区画だけ焼き、build/ を戻す → `tools/fmrb_serial_capture.py` でリセットして起動ログを取る → 解析 → 基準と比べて表。`--no-flash`・`--table`・`--update`・`--dumps` |
| `tools/regress/boot_log.rb` | 解析部 (ログ → 数) と比べ方と表。ホストだけで動く。伏せ字 (mDNS の名前・IP・MAC・アクセスポイントの名前) もここ |
| `tools/regress/targets.rb` | 各端末に `serial:` (USB の VID:PID、リセットのしかた `rts` / `open`、コンソールのボーレート、app を焼く速さ) を足した。5 端末とも値を書いた |
| `rakelib/regress.rake` | `regress:boot` (BOARD=・FLASH=0・TABLE=1・DUMPS=・PORT=)、`regress:boot:update`、`regress:boot:test`。`regress:full` には入れていない |
| `rakelib/test.rake` | `rake test` に `regress:boot:test` を足した (よって `rake regress` と CI の test-host にも入る) |
| `rakelib/device.rake` | `rake flash:app` が `PORT=` を受けるようにした (無ければ従来どおり `.serial_port`)。regress:boot が VID:PID で見つけたポートを渡すため |
| `test/regress/boot_log_test.rb` | 解析部の試験 13 本 (作り物のログ: 健全な起動、crash マーカー 7 種、リブート、内蔵 RAM・IRAM free・stack の悪化、段・タスク・ダンプの欠け、基準なし、伏せ字、古い VM プールの表) |
| `test/regress/boot_baseline.json` | P4-Nano の基準と幅 (236 行、9 KB ほど) |

- **起動ログの取り方**: ルートの `tools/fmrb_serial_capture.py` (読むだけ) を子で起こし、それが書くファイルを 0.1 秒ごとに読んで、各行に届いた時刻を付ける (`[  9.714] I (...) ...`)。周期ダンプの `IRAM free:` が `DUMPS` 回 (既定 3) 出て、その後の VM プールの表が閉じたら止める。crash マーカーを見たら 5 秒後に止める。最長 90 秒。キャプチャの道具は 4096 バイトか 0.5 秒で区切って書くので、時刻は 0.5 秒刻み。
- **リセットのしかた**: `rts` の端末 (NARYAv3・P4-Nano、外付けの USB-UART) は esptool と同じ RTS のパルス (キャプチャの道具の既定)。`open` の端末 (Tab5・StickS3・AtomS3R、チップの USB-Serial-JTAG) は開くだけでリブートするので `--no-reset` で開く (二重のリセットを避ける。ルートの CLAUDE.md の Tab5 の記述どおり)。
- **ポートの取り合い**: MCP サーバはポートごとに `~/.fmrb_mcp/<port>.lock` を flock する (`tools/mcp/lib/serial_manager.rb`、`FMRB_MCP_STATE_DIR` も見る)。regress:boot は同じ錠を待たずに取りに行き、取れなければ止まる。取れたら、ほかのプロセスがポートを開いていないかを /proc の fd で確認し、開いていれば止まる (ユーザのモニタ、MCP の死んだサーバが残したキャプチャ)。流している間は錠を持ち続けるので、MCP の serial_start・flash は断られる。終わったら錠の中の文字列を消して放す。
- **焼く**: `rake build:esp32` と `rake flash:app` に `firmware_env` (rake regress:size と同じ固定の設定) を環境変数で渡す。`idf.py app-flash` は 0x10000 の app の区画だけを書く (storage と /home は書かない)。速さは targets.rb の `flash_baud` (P4-Nano は 460800)、落ちたら 115200 で 1 回やり直す。全体の書き込み (`rake flash`)・erase は道具のどこからも呼ばない。
- **基準の更新**: `rake regress:boot:update` は、前回の結果が crash マーカー 0・起動 1 回・Boot complete あり・ダンプあり、のときだけ書く。基準に残すのは数と名前だけ (版の文字列、リセット原因、M1|、IRAM free、タスク、VM プール、起動の秒数)。

## 2. P4-Nano の今の表

802b16a3 (develop) のファームを固定の設定で作って app の区画だけ焼き、その後の起動で取った。

```
board    reset              boot    crash M1 steps  M1 last internal    IRAM free           least stack free             dumps
NARYAv4  POWERON            8.9s    0     24        212,636 +0          208,592 -24         status_led 1,508 +0          3
  pools (used/peak): fmrb_kernel 100,824/126,208  system_desktop 649,096/708,888  Services 391,040/475,648
boot: naryav4 vs baseline 2.1.0-847-g802b16a3-dirty, ok (0.6 min: capture 36s; not flashed; log tmp/regress/boot/naryav4.log)
```

- M1| は 24 段 (boot_start 505,228 → wifi_rd 212,636)。タスクは 11 (hw_proxy・status_led・usb_host_lib・hid_host・audio_p4・fmrb_host・fmrb_kernel・debugd・system_desktop・Services・ble_fs)。一番空きの少ないのは status_led の 1,508 B (4 KB のうち)。
- リセット原因は RTS で EN を引いても `POWERON` と出る (P4 の ROM の出し方)。
- 版の文字列の `-dirty` は、手元の submodule (components/esp_littlefs・picoruby) に変更があるため (ファームのコードの変更ではない。RG3b と同じ)。
- 気づき (直していない): P4-Nano でも起動ログに `Running on ESP32-P4 (M5Stack Tab5)` と出る (main/boot/boot.c が P4 を全部 Tab5 と書く)。表と比べ方には使っていない。

## 3. 揺れの幅

同じファーム (802b16a3、焼いた直後の 1 回 + 焼かずに 4 回) で 5 回起動し、数ごとに最大 - 最小を見た。

| 数 | 揺れ (5 回) | 幅 |
|---|---|---|
| M1| の internal | 0〜436 B (BLE・WiFi の後の 5 段だけが動く。それより前の 19 段は 0) | 1 KB |
| M1| の largest | 0〜8,192 B (4 KB 刻み。ble_nimble_port が 200,704〜208,896、wifi_rd が 1 回だけ +4 KB) | 8 KB |
| M1| の psram | 0 B | 16 KB |
| IRAM free (最後のダンプ) | 440 B (208,600〜209,040) | 1 KB |
| タスクの stack の空き | 0〜144 B (fmrb_host 144、hid_host 96、status_led 80、usb_host_lib 32、ほかは 0) | 384 B |
| 起動の秒数 (ROM の表示から Boot complete) | 8.9〜9.0 秒 | 比べない (表に出すだけ) |
| VM プールの used | カーネル 296 B、Services 696 B、デスクトップ 0 B | 比べない (表に出すだけ) |

- 幅は観測した揺れの 2 倍ほどにした。largest は 4 KB 刻みで動くので 2 刻み分。psram は 0 だったが、WiFi の確保の順で動きうるので 16 KB にした。
- 5 回とも基準 (2 回目の起動の結果) と比べて通る (幅の中) ことを確認した。基準を取った後に焼かずに起動した 1 回と、焼き直した 1 回 (M1| の最後の段 -8、IRAM free -32) も通った。
- 揺れが大きすぎて比べられない、には当たらなかった (止まる条件に当たらず)。M1| の前半 19 段と多くのタスクは決まった値になる。
- 判定は悪い向き (空き・largest が減る) に幅を超えたら落とす。よい向きに超えたものは `moved` に出すだけ。

## 4. わざと壊した確認

ファームのコードには触らずに 3 つの形で確認した。

1. **基準の値を一時的に書き換える** (M1|host_ready の internal を +4096、IRAM free を +3000、fmrb_host の stack を +600、無い段 `fake_step` を足す)。`rake regress:boot BOARD=naryav4 TABLE=1`:

```
NARYAv4  POWERON            8.9s    0     24        212,636 +0          208,592 -3,024      status_led 1,508 +0          3
  pools (used/peak): fmrb_kernel 100,824/126,208  system_desktop 649,096/708,888  Services 391,040/475,648
  FAIL M1|host_ready internal 391,808 (-4,096, tolerance 1,024)
  FAIL M1|fake_step missing
  FAIL IRAM free 208,592 (-3,024, tolerance 1,024)
  FAIL stack fmrb_host 10,040 (-600, tolerance 384)
boot: naryav4 vs baseline 2.1.0-847-g802b16a3-dirty, FAILED (table only)
```

   4 つとも落ち、基準を戻すと通った。

2. **作り物のログを解析部に食わせる** (`test/regress/boot_log_test.rb`、13 本 54 アサーション): crash マーカー (`Guru Meditation ... panic'ed`・`abort() was called`・`Backtrace:`・`assert failed`・`A stack overflow`・`CORRUPT HEAP`・`Stack protection fault`) で落ちる、2 回目の `rst:` (リブート) で落ちる、内蔵 RAM が幅の中なら通り幅を超えると落ちる、増えたときは moved、段・タスク・ダンプが無いと落ちる、伏せ字が効く。
3. **ポートを使っている物がいるとき**: (a) 別のプロセスで `~/.fmrb_mcp/dev-ttyACM0.lock` を flock した状態、(b) 別のプロセスが `/dev/ttyACM0` を開いた状態、の両方で `port busy: ...` と出て、ポートに触らずに止まった (終了コード 2)。

## 5. 流した時間と出力

| 流し方 | 時間 | 内訳 |
|---|---|---|
| `rake regress:boot BOARD=naryav4` (1 回目) | 4.8 分 | clean_all からのビルド 150 秒、app の区画の書き込み 103 秒 (460800 bps、6.67 MB を圧縮 3.93 MB で 91 秒)、起動ログ 36 秒 |
| 同 (2 回目、基準と比べて通った) | 4.6 分 | ビルド 139 秒、書き込み 102 秒、起動ログ 36 秒 |
| `FLASH=0` | 37〜38 秒 | 起動ログ 36 秒 (リセットから 3 回目のダンプまで) |
| `TABLE=1` | 0.1 秒 | |
| `rake regress:boot:test` | 0.01 秒 (ruby の起動を除く) | |

- 出力は、通ったとき 3 行 (表の見出し・端末の行・VM プールの行) + 要約 1 行。焼くときは `build/: TAB5 (esp32p4)` の 1 行が先に付く。落ちたときは落ちた項目を 1 行ずつ (最大 10 行、残りは数だけ)、よい向きに動いたものを 1 行。
- 全文のログは 690 行ほど (`tmp/regress/boot/naryav4.log`)。ビルドと焼いたときのログ (`.build.log`・`.flash.log`) と結果の json も同じ場所。場所だけ出す。
- `rake regress` は 18 スイート (boot の解析部を含む) と描画の記録 205 本で 19 秒、全部通る。

## 6. 終わったときの状態

- P4-Nano には 802b16a3 (develop) のファームが入っている (焼いたのは app の区画だけ: `idf.py app-flash`、flash のログは `Wrote 6671792 bytes ... at 0x00010000` の 1 区画だけ)。storage と /home は書いていないので消えていない。焼く前に入っていたのは 2.1.0-603 (1ff4c43f) のファームだった。
- build/ は始める前と同じ TAB5 (esp32p4) のビルドに戻した (`build/: TAB5 (esp32p4)`、退避の残り `build.regress*` は無い)。
- MCP の錠 (`~/.fmrb_mcp/dev-ttyACM0.lock`) は放してある。

## 7. 端末を足すとき

- targets.rb の端末の行に `serial: { usb: "vid:pid", reset: "rts" か "open", baud: 115_200, flash_baud: ... }` を書く。USB-UART の橋渡しの石が外付けなら `rts`、チップの USB-Serial-JTAG なら `open`。
- つないで `rake regress:boot BOARD=<端末>` を 1 回流し (基準が無いので落ちる)、表と全文のログを人が見てから `rake regress:boot:update BOARD=<端末>` する。幅を見直すなら焼かずに数回 (`FLASH=0`) 流して揺れを見る。
- 書いた値のうち確認できたのは P4-Nano (1a86:55d3、rts、115200、焼く速さ 460800) だけ。ほかは以前の報告から: NARYAv3 は CH340 (1a86:7523、rts)、Tab5・AtomS3R・StickS3 は 303a:1001 で開くとリブート (`open`)。焼く速さは MCP の flash と同じ 115200 にし、StickS3 だけ m3s の報告で通った 460800 にした。StickS3 は m3s で「開いたときのリセットで書き込み待ちに戻った」ことがあり、regress:boot でも同じことが起きうる (つないだときに確認する)。
- Tab5・StickS3・AtomS3R は VID:PID が同じなので、2 台以上つながっていると `PORT=` が要る (道具が止まって案内する)。

## 8. 残件

- P4-Nano 以外の 4 端末の基準 (つないだときに取る)。
- 起動の秒数は表に出すだけで比べていない。0.5 秒刻みなので、比べるならキャプチャの側の読み方を細かくする (ルートの tools/ を直すことになる) か、ログの中の時刻 (ブートの ms) を使う。
- VM プールは比べていない。カーネルとサービスホストは自分の GC で動く。デスクトップは 5 回とも同じだったので、比べる対象に足せる (幅は sim の 512 B にならう)。
- MCP の flash ツールは端末・ポートを選べない (`.env` の端末を焼く) ことは変わっていない。regress:boot は `rake flash:app PORT=` で自分で焼く。
- `Running on ESP32-P4 (M5Stack Tab5)` の表示 (2 章)。
- `doc/README.md` の索引の更新 (`rake docs:index`) は doc/regression の文書をコミットするときに親が行う。

## 9. コミット (fmruby-core develop、push していない)

- d2d1bf01 tools: rake regress:boot, a board's boot log against its baseline (RG4)
- d8807380 test: P4-Nano boot baseline for rake regress:boot (develop 802b16a3)
- fe2eb4ec doc: rake regress:boot, the boot log check, in the regression usage

指示書 (instruction_rg4.md) とこの report はコミットしていない。
