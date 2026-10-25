# RG3b 報告: RAM とフットプリントの表をコマンド 1 つで

> 状態: 完了 | 更新: 2026-10-25 | `rake regress:size` (全端末のファームの idf.py size の表、内蔵 RAM が増えたら落ちる) と `rake regress:run` (sim の決まった台本で VM プールの使用量を比べる) を作り、`rake regress:full` に入れた。基準は R6 の前の develop で取った。実機は使っていない。sim では M1| と周期ダンプの IRAM free が取れない (6 章)

## 1. 作り

| ファイル | 役目 |
|---|---|
| `tools/regress/targets.rb` | 端末の一覧に `firmware` (FMRB_HW_TARGET) と `build` (端末ごとのビルド設定) を足した。共通の設定は `FIRMWARE_ENV` (標準構成: Spinel のカーネルとエディタ、mruby のデスクトップ、FFT・日本語フォントあり)。AtomS3R・StickS3 は `MINI_BUILD` (Rakefile の MINI_BUILD_DEFAULTS と同じ)、StickS3 はさらに FMRB_IMU=1 |
| `tools/regress/size.rb` | ビルドの時点の表。端末ごとに clean_all → `rake build:esp32` → `esp_idf_size` (json2) → 結果を `tmp/regress/size/<端末>.json` に書き、基準と比べる。`--only`・`--table`・`--update`・`--no-restore`・`--restore` |
| `tools/regress/sim_run.rb` | 動かした時点の表。Retro・Modern の Linux ビルド → 3 つのコンテナを headless で起こす → 台本 → debug server の ps とコアのログから数字を取り、基準と比べる。`--only`・`--table`・`--update` |
| `tools/regress/build_tree.rb` | build/ を借りて返す。始める前の build/・sdkconfig・端末ごとの libmruby (`components/picoruby-esp32/picoruby/build/*`)・Spinel の生成物 (`main/prebuild_scripts/spinel/gen/*`)・flash/etc の生成物 (system_conf ほか。sim は flash/ を直に読むので要る) を `build.regress.saved/`・`build.regress-aside.saved/` に退避し、終わったら (落ちても) 戻す |
| `rakelib/regress.rake` | `regress:size` (ONLY=・TABLE=1・NO_RESTORE=1)・`regress:size:update`・`regress:run` (ONLY=・TABLE=1)・`regress:run:update`・`regress:full` (regress + size + run) |
| `test/regress/size_baseline.json` | 5 端末の基準 (メモリの種類、アプリのイメージ、区画、全アーカイブ、Spinel の TU)。64 KB |
| `test/regress/sim_baseline.json` | Retro・Modern の基準と幅。4 KB |

- **ビルドの設定は targets.rb で固定する**: 子の `rake build:esp32` に全部の FMRB_* を環境変数で渡すので、Rakefile の .env 読み込みはそれらを飛ばす (シェルの値が勝つ)。開発者が .env でエディタを mruby に戻していても表は動かない。AtomS3R・StickS3 の既定 (BOARD_BUILD_DEFAULTS) と targets.rb が食い違うと `rake regress:size` が始める前に止まる。
- **数字は esp_idf_size を直接呼ぶ**: `idf.py size` は先に `ninja all` を走らせ (storage の image を作り直し、ELF を作り直すこともある)、10 秒以上かかる。`ESP_IDF_SIZE_NG=1 python -m esp_idf_size --format json2` を map に当てれば、要約・アーカイブ (size-components と同じ)・ファイル (size-files と同じ) の 3 つが 1 回の docker で数秒で取れる。中身は idf.py size と同じ (要約の json は一字一句一致を確認した)。
- **出力の形は端末で違うが、集められる**: メモリの種類の名前は S3 が `DIRAM`・`IRAM`・`RTC FAST`・`RTC SLOW`・`Flash Code`・`Flash Data`、P4 が `DIRAM`・`HP core RAM`・`LP RAM`・`Flash`・`External RAM`。json2 なので名前で分け、「Flash で始まる = flash、External = PSRAM、ほか全部 = 内蔵」とした。止まる条件 (形が違って集められない) には当たらなかった。
- **アプリ区画の空き**: `build/partition_table/partition-table.bin` を読んで一番小さい app 区画を取り、`.bin` の大きさを引く (check_sizes.py と同じ数え方。TAB5 で 0x9ef60 = 651,104 が一致)。
- **Spinel のプログラムごとの静的な内蔵 RAM**: size-files の結果から、生成された TU (`fmrb_kernel_combined.c.obj`・`editor_combined.c.obj`・`fft_kernel`・`raycast_kernel`・`spinel_hello_kernel`、標準構成ではデスクトップは mruby なので無い) の内蔵の分と PSRAM の分を取った。R6 で FmrbText・HitAreas が Spinel の側に入って static な表が増えれば、この行に出る。
- 結果にはコミットと設定 (env) を入れる。doc/・test/regress/・tools/regress/ 以外に手元の変更があると `+dirty` が付く (ファームに入らない所は数えない)。
- 内蔵 RAM の判定: メモリの種類ごとに、1 つでも基準より増えたら落とす。減ったときは差を出すだけ。flash は増分を出すだけ。基準の無い端末は落とす (端末を足したら基準を取らせる)。

## 2. ビルドの時点の表 (今の値、基準 6f7b5848)

```
board    DIRAM              IRAM               other int.   app image (flash)     app partition free
NARYAv3  126,035 +0         16,384 +0          104 +0       4,512,624 +0          1,778,832 +0 (28%)
TAB5     129,698 +0         -                  948 +0       6,688,928 +0          651,104 +0 (9%)
NARYAv4  125,196 +0         -                  3,052 +0     6,666,064 +0          673,968 +0 (9%)
STICKS3  114,863 +0         16,384 +0          104 +0       4,530,448 +0          712,432 +0 (14%)
ATOMS3R  114,499 +0         16,384 +0          104 +0       4,517,648 +0          725,232 +0 (14%)
spinel TU static internal RAM (B) [psram]:
  NARYAv3  editor 92  fft 76  hello 76  kernel 128  raycast 76
  TAB5     editor 92 [16,965]  fft 76 [11,900]  hello 76 [11,808]  kernel 128 [13,389]  raycast 76 [11,856]
  NARYAv4  editor 92 [16,965]  fft 76 [11,900]  hello 76 [11,808]  kernel 128 [13,389]  raycast 76 [11,856]
  STICKS3  editor -  fft -  hello 76  kernel 128  raycast 76
  ATOMS3R  editor -  fft -  hello 76  kernel 128  raycast 76
size: 5 boards vs baseline 6f7b5848, ok (table only)
```

- other int. は S3 が RTC FAST 72 + RTC SLOW 32、TAB5 が HP core RAM 892 + LP RAM 56、NARYAv4 は HP core RAM が 2,996 (sdkconfig の違い)。
- internal_ram_budget.md の「静的な D/IRAM」(S3 126,251 / TAB5 130,186 / NARYAv4 125,524、2026-10-01) とは数え方 (IRAM を含むか、RTC を含むか) が少し違い、その後の変更もあるので一致はしない。比べるのはこの表の中の前後だけにする。
- Spinel の TU の内蔵の分は 76〜128 B (例外・catch スタックなどの表は P4 では SP_TU_BSS で PSRAM の .bss に、[ ] の数字)。S3 の layout には外部 RAM の節が無いので [ ] は出ない。
- StickS3・AtomS3R はエディタ (mruby) と FFT (FMRB_FFT_SPINEL=0) の TU が無いので `-`。

## 3. 動かした時点の表 (sim、基準 8d22df2c)

台本: 起動 → デスクトップの「Boot complete」から 3 秒 → 控え (idle) → kamon・MML・StackChan Remote・Spinel Hello を 3 秒おきに起動 → 控え (running) → 起動したものだけを閉じる → 3 秒 → 控え (closed)。控えは debug server の ps (sim_app ps と同じ口): アプリごとの VM プールの使用量 (mem_used) とスタックの空きの最小 (stack_hw)。ほかにコアのログから mruby のアプリの「VM pool after compile」の used と peak。

```
retro   idle->closed: kernel 155K->196K, desktop 730K->730K | running: Kamon 468K, MML 439K, StackChan Remote 435K, Spinel Hello 421K
modern  idle->closed: kernel 167K->194K, desktop 730K->736K, Services 557K->557K | running: Kamon 468K, MML 439K, StackChan Remote 435K, Spinel Hello 421K
sim: 2 families, ok (4.4 min: retro build 76s + sim 53s, modern build 77s + sim 53s; M1| is boards only)
```

- 比べる数: 3 つの控えのそれぞれのアプリの used と stack_free、4 アプリのコンパイル直後の used と peak。1 つの種類あたり 30〜40 個。
- Modern には起動時からサービスホスト (Services) がいる。台本は自分が起動したアプリだけを閉じる。

## 4. 揺れの扱い

同じビルドで 5 回 (幅を決める前の 3 回 + 基準を取った回 + 比べた回) を流し、数ごとに最大と最小の差を見た。

| 数 | 揺れ (最大 - 最小) | 幅 |
|---|---|---|
| 起動した 4 アプリの used (running) | 0 B | 512 B |
| コンパイル直後の used・peak | 0 B | 256 B |
| デスクトップ (mruby) の used | 0〜88 B | 512 B |
| カーネル (Spinel) の used (idle・running) | 0〜1,928 B | 4 KB か 3% の大きい方 |
| サービスホストの used | 776〜984 B | 同上 |
| スタックの空き | 0〜276 B | 512 B |
| 閉じた直後のカーネルの used | 161,456〜202,320 B (約 40 KB) | 比べない (記録だけ) |

- アプリとデスクトップは決まった値になる (mruby のアプリはスクリプトのコンパイルと on_create までで止まって待つので、控えの時点で使用量が動かない)。幅は 0 B でも通るが、時刻で決まる描画などに備えて 512 B を取った。R6 で増えるのは KB の単位の見込みなので、それでも見える。
- カーネルとサービスホストは自分の GC の都合で動く (控えの瞬間にごみを集め終えているかどうか)。観測した最大の 2 倍あまりの 4 KB (大きいプールでは 3%) にした。
- 閉じた直後のカーネルは、閉じたアプリの後始末のごみを集めたかどうかで 40 KB 動く。幅で吸える大きさではないので比べない。比べたいときは台本の closed を数秒延ばす (GC を待つ) か、カーネルに GC を促す口が要る (ファームに手を入れるので今回はしない)。
- 判定は「悪い向き (使用量が増える、空きが減る) に幅を超えたら落ちる」。よい向きに幅を超えたものは `moved` として出すだけ。
- 揺れが大きすぎて比べられない、には当たらなかった (止まる条件に当たらず)。

## 5. わざと壊した確認 (戻した)

`components/fmrb_mem/fmrb_alloc.c` に 256 B の static 配列を 1 つ足し、`fmrb_mem_log_boot_snapshot` から触った (消されないように)。`rake regress:size ONLY=sticks3,tab5`:

```
board    DIRAM              IRAM               other int.   app image (flash)     app partition free
TAB5     129,954 +256       -                  948 +0       6,688,944 +16         651,088 -16 (9%)  FAIL internal RAM grew: DIRAM +256
STICKS3  115,127 +264       16,384 +0          104 +0       4,530,480 +32         712,400 -32 (14%)  FAIL internal RAM grew: DIRAM +264
spinel TU static internal RAM (B) [psram]:
  TAB5     editor 92 [16,965]  fft 76 [11,900]  hello 76 [11,808]  kernel 128 [13,389]  raycast 76 [11,856]
  STICKS3  editor -  fft -  hello 76  kernel 128  raycast 76
archives that moved (size-components, top 6; full: tmp/regress/size/<board>.json):
  TAB5     esp-idf/fmrb_mem/libfmrb_mem.a internal +256     flash +28        psram +0
  STICKS3  esp-idf/fmrb_mem/libfmrb_mem.a internal +256     flash +28        psram +0
  STICKS3  esp-idf/picoruby-esp32/libpicoruby-esp32.a internal +0       flash +4         psram +0
size: 2 boards vs baseline 8d22df2c+dirty, FAILED (3.9 min: TAB5 113s, STICKS3 106s)
regress:size: failed
```

- 落ち、増えたアーカイブ (libfmrb_mem.a) が出た。S3 は配置の詰め物で +264。戻して全端末を作り直すと差 0 で通り、それを最終の基準にした (6f7b5848)。
- 同じコミットで全端末を 2 回作った結果 (基準を取った回と比べた回) は、メモリの種類・イメージ・全アーカイブが一致した (`archives that moved` の行が出ない)。
- イメージの大きさは git の状態で 16 B ほど動く (app の版の文字列に `-dirty` が付くか)。flash は落とさないので害は無い。

## 6. sim で取れなかったもの

- **M1| の行**: `fmrb_mem_log_boot_snapshot` は Linux のビルドでは何も出さない (`#ifndef CONFIG_IDF_TARGET_LINUX`)。内蔵 RAM という区別が無いため。
- **周期ダンプ (fmrb_task: の表、IRAM free、--- VM Pools ---)**: sim では 10 秒ごとのダンプが一度も出ない (debug_mode は true、起動から 3 分待っても出ない)。gdb でコアの全スレッドを見ると status_led のタスクのスレッドが無い (hw_proxy も見当たらない)。原因は追っていない (ファームのコードには手を入れない範囲なので)。代わりに debug server の ps を使った (VM プールの使用量・スタックの空きは同じ値の源)。
- よって「M1| の段の値・IRAM free を sim で比べる」は実現していない。これらは実機の起動の確認 (RG4) で見る。周期ダンプが sim で出ないのが不具合なら、直せば `sim_run.rb` の控えにダンプの値を足せる (残件)。

## 7. 流した時間と出力の長さ

- `rake regress:size` 全 5 端末: 9.5〜9.7 分 (1 端末 104〜115 秒: clean_all からの build:esp32 が 95〜105 秒、esp_idf_size が 10 秒弱。退避と戻しは 1 秒未満)。1 端末だけ (`ONLY=`) は 2 分、2 端末で 3.9 分。`TABLE=1` は 0.1 秒。
- `rake regress:run` 2 種類: 4.4 分 (Linux ビルド 77 秒 × 2 + sim 53 秒 × 2。種類を切り替えた直後の 1 回目の起動は graphics-audio が前の解像度のまま応答せずに失敗するので、それを見つけたらすぐ起こし直す分を含む)。
- `rake regress:full`: 15.0 分 (regress 15 秒 + size 9.9 分 + run 4.4 分、実測)。通ったときの出力は全部で 21 行。
- 出力: size は通ったとき 13 行 (表 6 行 + Spinel 6 行 + 要約 1 行、1 行 110 字ほど)。落ちたときは落ちた行に理由が付き、差のある部品が最大 6 行足される (上の例で 13 行)。run は通ったとき 3 行、落ちたときは種類ごとに落ちた数を 8 行まで + 動いた数を 4 行まで。ビルドのログ・コアのログ・生の json は `tmp/regress/size/`・`tmp/regress/sim/` にあり、場所だけ出す。

## 8. R6 の確認でどう使うか

- R6 の前の基準は入っている (size: 6f7b5848、sim: 8d22df2c。どちらもファームの中身は b96325eb と同じ)。
- R6 の各段の後に `rake regress:size` を流す。**内蔵 RAM (DIRAM・IRAM・RTC など) が 1 B でも増えたら落ちる**ので、FmrbText・HitAreas を足して内蔵が増えていないかがその場で分かる。増えたら「archives that moved」でどのアーカイブか、Spinel の行でどのプログラムの TU かが分かる (Spinel の側に static な表を足すと、P4 では [ ] (PSRAM) に出るはずで、内蔵の数が動けば SP_TU_BSS から漏れている)。flash の増分も同じ表に出るので、R6 の報告にそのまま貼れる。
- `rake regress:run` は FmrbUI・FmrbButtons を使うアプリ (kamon・MML・StackChan Remote) の VM プールの使用量を見る。gem の Ruby (mrblib) にメソッドやクラスを足すと、全アプリのコンパイル直後の使用量が増える (クラスとメソッドの表は各 VM のプールに作られる) ので、R6 では幅を超えて落ちる見込みが高い。そのときは増えた量 (1 アプリあたり何 KB か) を読んで意図したものか判断し、人が確認してから `rake regress:run:update` する。減った (FmrbButtons を消した) なら `moved` に出る。
- 手でやっていた「target ごとに clean_all → build:esp32 → idf.py size を前後で比べる」は `rake regress:size` 1 つ (10 分、出力 13 行) になる。build/ は元に戻るので、作業中の Linux の build を作り直さなくてよい。
- 5 章の手順 (わざと増やして落ちることを見る) は道具の確認なので、R6 では要らない。

## 9. 残件

- sim で周期ダンプが出ない理由 (6 章)。直れば sim の控えに IRAM free と VM Pools の Peak 列を足せる。
- 閉じた直後のカーネルの使用量を比べられるようにする (GC を待つ、または促す)。
- エディタ (Spinel) を台本に入れる: debug server の spawn はパスの .app.rb を起こす口で、エディタはデスクトップから開く。sim_input でデスクトップを操作すれば入れられるが、座標に頼るので今回は入れていない (エディタの起動と 1 打鍵は R6 の指示書どおり sim で見る)。
- ブラウザ版 (wasm) の表: 計画には「sim・ブラウザ版」とあるが、ブラウザ版の debug の口 (ps に当たるもの) を調べていない。
- MCP の sim_up は「種類を切り替えた直後の 1 回目は前の解像度で上がる」だけを直すが、今回は「上がらない」(コアが INIT_DISPLAY の答えを待って止まる) も起きた。sim_up でも同じことが起きうる (MCP の側の話なので触っていない)。
- `doc/README.md` の索引の更新 (`rake docs:index`) は doc/regression の文書をコミットするときに親が行う。

## 10. コミット (fmruby-core develop、push していない)

- 8d22df2c tools: rake regress:size and regress:run, the RAM and footprint tables (RG3b)
- 6f7b5848 tools: regress:size's dirty mark ignores documents and the regression tools
- 23220ed7 test: size and sim baselines for rake regress:size and regress:run, before app_refactor R6
- 9a4168ca doc: rake regress:full, the size and sim tables in the regression usage

指示書 (instruction_rg3b.md) とこの report はコミットしていない。わざと壊した変更 (fmrb_alloc.c) は戻した。
