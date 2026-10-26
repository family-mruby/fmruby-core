# RG6 報告: 揺れの幅を道具が自分で測る (calibrate)

> 状態: 完了 | 更新: 2026-10-26 | `rake regress:boot:calibrate` (実機を N 回起動) と `rake regress:run:calibrate` (sim の台本を N 回) を作った。数ごとの揺れから幅の案と基準の案 (中央値) を出し、数行の要約にする。`APPLY=1` で基準の json に書く。幅は端末ごと (sim は種類ごと) に持てるようにした。P4-Nano で RUNS=3 を確認、案は今の幅と同じだった。試しに書いた基準は戻した

## 1. 作り

| ファイル | 役目 |
|---|---|
| `tools/regress/calibrate.rb` (新規) | 計算だけの部品。中央値 (偶数回なら小さい方)、揺れ (最大 - 最小) を種類ごとに集める、幅の案、数ごとの中央値で作る基準の案、要約の表 |
| `tools/regress/boot_log.rb` | 起動ログの幅の案の決め方 `CALIBRATE`、比べる数を平らにする `flat`、基準にしてよい起動か `clean?` |
| `tools/regress/boot.rb` | `--calibrate N` (焼かずに N 回リセットして取る。`--flash` のときだけ最初に 1 回焼く)・`--apply`。端末ごとの幅 (`boards.<端末>.tolerance`) を既定の幅の上に重ねて比べる。`--update` は端末の幅を残す |
| `tools/regress/sim_run.rb` | `--calibrate N` (種類ごとに 1 回作り、台本を N 回。回の間は 3 つのコンテナを落として起こし直す)・`--apply`。`scenario` を「作る」と「起こして台本を流す」に分けた。種類ごとの幅 (`families.<種類>.tolerance`)。比べるときに基準の json の `tolerance` も見るようにした (今までは定数だけを見ていた。値は同じなので挙動は変わらない) |
| `rakelib/regress.rake` | `regress:boot:calibrate` (BOARD=・RUNS=・FLASH=1・DUMPS=・APPLY=1)・`regress:boot:calibrate:apply`・`regress:run:calibrate` (ONLY=・RUNS=・APPLY=1)・`regress:run:calibrate:apply`。`regress:boot:test` が calibrate の試験も流す |
| `test/regress/calibrate_test.rb` (新規) | 10 本 46 アサーション。中央値、案の丸め、RG4 の揺れから RG4 の幅が出ること、RG3b の揺れからの案、作り物の起動ログ 3 回を本物の解析部に通して揺れ・中央値を出し、3 回とも案の幅で基準と比べて通ること、crash の回、要約の `*`、sim の種類ごとの幅 |
| `doc/regression/usage.md` | 3 章に 1 行、7.2 に calibrate のコマンドと幅の重ね方、8 章にコマンドと幅の重ね方、8.1「端末を足したとき (calibrate)」(要約の読み方と案の決め方) |

- **APPLY=1 は測り直さない**: 前回の `<名前>.calibrate.json` をそのまま基準に書く。人が読んだ要約と書かれる値が同じになるようにするため。実機・sim は使わない。crash・リブートのあった回を含む calibrate は書かない。
- 基準に書くもの: 基準の案 (数ごとの中央値を最後の回の記録にはめたもの。中身の形は `--update` が書くものと同じ)、端末 (種類) の幅、`calibration` (回数・日時・種類ごとの揺れ)。ファイル全体の既定の幅 (`tolerance`) は残る。

## 2. 幅の案の決め方

案 = max(下限, 揺れ × 係数 を単位に切り上げ)。揺れはその種類の全部の数のうち一番大きい最大 - 最小。

| 種類 | 係数 | 単位 | 下限 | 理由 |
|---|---|---|---|---|
| 起動 M1| internal | 2 | 256 | 512 | RG4 と同じく揺れの 2 倍ほど |
| 起動 M1| largest | 1 | 4096 | 8192 | 4 KB 刻みで動く。RG4 は 2 刻み分 (見えた揺れそのもの) にした |
| 起動 M1| psram | 2 | 4096 | 16384 | RG4 では動かなかったが WiFi の確保の順で動きうる |
| 起動 IRAM free | 2 | 256 | 512 | |
| 起動 stack の空き | 2 | 128 | 256 | |
| sim app_used | 2 | 256 | 512 | RG3b は揺れ 0〜88 B で 512 B にした |
| sim gc_used (カーネル・サービスホスト) | 2 | 512 | 4096 | 数回では GC の当たり外れが見えないことがある。割合の部分 (3%) は今の値のまま |
| sim stack_free | 2 | 256 | 512 | |
| sim compile | 2 | 256 | 256 | |

- RG4 の揺れ (internal 436・largest 8,192・psram 0・IRAM free 440・stack 144) にこの決め方を当てると、今の幅 (1,024・8,192・16,384・1,024・384) がそのまま出る (試験に入れた)。
- RG3b の揺れでは、sim の stack だけ 276 B → 768 B と今 (512 B) より広い案になる (RG3b は 2 倍を取らずに 512 B にしていた)。
- 下限は「数回では見えないが起こりうる揺れ」を受ける。下限より小さい揺れしか見なくても、案は下限より小さくならない。

## 3. 確認

### 3.1 P4-Nano (`rake regress:boot:calibrate BOARD=naryav4 RUNS=3`)

```
  kind         items moved  spread (where)                         now            proposal
  m1_internal     24     5  436 (M1|ble_nimble_port internal)      1,024          1,024
  m1_largest      24     2  4,096 (M1|ble_nimble_port largest)     8,192          8,192
  m1_psram        24     0  0                                      16,384         16,384
  iram_free        1     1  456 (IRAM free)                        1,024          1,024
  stack_free      11     6  144 (stack fmrb_host)                  384            384
  boot 8.9-8.9s
  candidate vs the baseline now: 7 of 84 numbers differ (most: IRAM free +36)
calibrate: naryav4 3 boots (1.8 min: capture 36-36s x3); tmp/regress/boot/naryav4.calibrate.json
  to make it the baseline, after reading the above: rake regress:boot:calibrate:apply BOARD=naryav4
```

- 案は 5 種類とも今の幅と同じ。揺れも RG4 の 5 回とほぼ同じ (internal 436 は同じ数、同じ段)。
- その前の 1 回目 (下の 5 章のとおり焼いてしまった回) は、IRAM free の揺れが 972 B (209,040 / 209,032 / 208,068) で、案が 2,048 (`*` 付き) になった。3 回目だけ 1 KB 低い。焼いた直後の 1 回目は高い側なので焼いたせいではなく、IRAM free はときどき 1 KB 近く動くことがある、と読める。今の幅 1,024 はその揺れとほぼ同じで余裕が無い。幅を 2 KB に広げるかは人が決める (基準は変えていない)。RUNS=5 で何度か流すとはっきりする。
- `APPLY=1` → `rake regress:boot BOARD=naryav4 FLASH=0` は通った (`M1 last internal 212,628 -32`、`IRAM free 208,604 -48`、ok、37 秒)。`rake regress:boot:update` が端末の幅と `calibration` を残すことも確認した。そのあと基準は元のファイルに戻した (`git status` で差分なし)。

### 3.2 sim (`rake regress:run:calibrate ONLY=modern RUNS=3`)

```
build/: TAB5 (esp32p4)
modern (TAB5, 3 runs):
  kind         items moved  spread (where)                         now            proposal
  app_used         7     2  104 (running system_desktop used)      512            512
  gc_used          5     2  2,688 (running fmrb_kernel used)       4,096 or 3%    5,632 or 3%  *
  stack_free      13     2  32 (running system_desktop stack_free) 512            512
  compile          8     0  0                                      256            256
  not compared: closed fmrb_kernel used moved 6,432
  candidate vs the baseline now: 4 of 33 numbers differ (most: running fmrb_kernel used +1,960)
calibrate: 1 families, 3 runs each (3.1 min: modern build 76s + sim 36-36s x3); tmp/regress/sim/<family>.calibrate.json
  to make it the baseline, after reading the above: rake regress:run:calibrate:apply ONLY=modern
```

- 案は 3 種類が今と同じ。gc_used だけ 5,632 B (カーネルの used が 3 回で 2,688 B 動いた。RG3b の最大 1,928 B より大きい)。ただし今の幅は「4 KB か 3% の大きい方」で、カーネル (165 KB ほど) の 3% は 5 KB ほど、サービスホスト (558 KB) の 3% は 16 KB なので、実際に効く幅は今でも案とほぼ同じ。取り込む必要は薄い。
- 閉じた直後のカーネル (比べない数) は 3 回で 6,432 B 動いた (RG3b では 40 KB)。
- 1 回の sim は 36 秒 (RG3b の 53 秒は種類を切り替えた直後の起こし直しを含んでいた。同じ種類の 2 回目からは起こし直しが無い)。
- `APPLY=1` → `rake regress:run` (retro・modern) は通った (4.3 分)。`rake regress:run:update ONLY=modern` が種類の幅を残すことも確認した。そのあと基準は元のファイルに戻した。

### 3.3 試験

- `rake regress:boot:test` (boot_log_test 13 本 + calibrate_test 10 本) が通る。`rake regress` は 18 スイートと描画の記録 205 本で全部通る (14 秒 + 0.6 秒)。

## 4. 出力の行数と時間

| 流し方 | 時間 | 出力 |
|---|---|---|
| `rake regress:boot:calibrate BOARD=naryav4 RUNS=3` | 1.8 分 (起動ログ 36 秒 × 3) | 10 行 (表 6 行 + 起動秒 1 行 + 今の基準との違い 1 行 + 要約 1 行 + 取り込み方 1 行) |
| 同 FLASH あり (5 章) | 5.4 分 (ビルド 115 秒、書き込み 101 秒、起動ログ 36 秒 × 3) | 上 + `build/:` の 1 行 |
| `rake regress:run:calibrate ONLY=modern RUNS=3` | 3.1 分 (Linux ビルド 76 秒 + sim 36 秒 × 3) | 10 行 (`build/:` 1 行 + 種類の見出し 1 行 + 表 5 行 + 比べない数 1 行 + 今の基準との違い 1 行 + 要約 1 行 + 取り込み方 1 行) |
| `... APPLY=1` | 0.1 秒 | 1 行 |

- 1 回あたりの目安: 実機は 36 秒 × RUNS (RUNS=5 で 3 分)、sim は 76 秒 + 36 秒 × RUNS (種類ごと。RUNS=5 で 4.3 分)。
- 各回の全文のログ: `tmp/regress/boot/<端末>.calibrate.<n>.log`、`tmp/regress/sim/<種類>.calibrate.<n>.log`。

## 5. 指示との違い・気づき

- **1 回目の実機の calibrate で、焼いてしまった**。boot.rb の既定が「焼く」(`regress:boot` の既定) のままで、calibrate の「既定は焼かない」が効いていなかった。焼いたのは `rake flash:app` (app の区画だけ、0x10000 に 6,671,792 バイト) で、storage と /home は書いていない。焼いたのは develop の b9a35c3f を固定の設定で作ったもの (版 `2.1.0-858-gb9a35c3f-dirty`)。焼く前の版は控えていない (親の話では今の develop のファーム)。直したあと (calibrate は `--flash` を付けたときだけ焼く) の 2 回目は焼かずに流れた。
- 指示書の「端末を足す手順 (2 章)」は usage.md では 3 章 (端末の一覧) だったので、3 章に 1 行足した。
- 取り込みの口は `APPLY=1` と `:calibrate:apply` の両方を作った。どちらも測り直さずに前回の結果を書く。
- 揺れが大きすぎて案が意味を持たない、には当たらなかった (止まる条件に当たらず)。ルートの tools/ は直していない。

## 6. 終わったときの状態

- 基準 (`test/regress/boot_baseline.json`・`sim_baseline.json`) は始める前と同じ (試しに書いたものは戻した)。
- build/ は TAB5 (esp32p4) のまま (`build/: TAB5 (esp32p4)`、退避の残り `build.regress*` は無い)。sim のコンテナは動いていない。port 8006 には触っていない。
- P4-Nano には b9a35c3f (develop) のファームが入っている (app の区画だけ焼いた)。MCP の錠は放してある。

## 7. 残件

- IRAM free の幅 (1,024) は、ときどき出る 1 KB 近い揺れに対して余裕が無い (3.1)。RUNS=5 を何回か流して、2 KB にするかを人が決める。
- sim の gc_used の案は絶対値の部分だけを出し、割合の部分を考えない。割合の部分のほうが大きい VM では案の `*` が実際には意味を持たない (3.2)。案を「割合を超える分だけ」にするかは、使ってみて決める。
- P4-Nano 以外の端末の calibrate (つないだとき)。
- `doc/README.md` の索引の更新 (`rake docs:index`) は doc/regression の文書をコミットするときに親が行う。

## 8. コミット (fmruby-core develop、push していない)

- d2ef606c tools: rake regress:boot:calibrate and regress:run:calibrate measure their own tolerance (RG6)
- 2c6acb2b doc: calibrate in the regression usage, the step when a board is added

指示書 (instruction_rg6.md) とこの report はコミットしていない。
