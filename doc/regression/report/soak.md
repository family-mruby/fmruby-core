# アプリを何回も開いて閉じる道具 (rake regress:soak) と 17 KB の正体

> 状態: 完了 | 更新: 2026-11-16 | `rake regress:soak` を作った (sim の Retro・Modern、実機は WiFi の devctl と MCP の capture のファイル)。regress:run の代表の組を閉じた後のカーネルの 17 KB は**漏れではない**: Spinel のカーネルの文字列が自分の周期でしか掃除されず、閉じた後の値が数十 KB の幅で上下する。その周期のどこで控えを取ったかの差。30 回流しても床は上がらない。使い方の正は usage.md 14 章。指示書は instruction_soak.md

## 1. 道具の作り

| 場所 | 中身 |
|---|---|
| `tools/regress/soak.rb` | 本体。sim は regress:run と同じビルドと起動 (`sim_run.rb` の関数をそのまま使う: clean_all、固定の設定での build:linux、build/ の退避と戻し、決まった組のランチャー)、1 回の起動の中で ROUNDS 回。実機は `Board` (1.1) |
| `tools/regress/soak/wasm_probe.app.rb` | `FmrbApp.wasm_stats` を 1 行ログに出して終わる試験のアプリ (Modern の sim だけ。sim の `/tmp` に置いて起動) |
| `test/regress/soak_test.rb` | 判定と周期ダンプの読み取りの試験 (作り物の数とログ。`rake regress:boot:test` に足したので `rake test`・`rake regress` に入る) |
| `rakelib/regress.rake` | `regress:soak` (ROUNDS・WARMUP・ONLY・GROUP・SPLIT・NO_BUILD・SPINEL・TABLE・BOARD・DUMPS) |

- **1 回**: regress:run の組ごとに、アプリを順に起動 (3 秒ずつ) → 全部閉じる、を全部の組について行ってから控え。控えは regress:run と同じ (debug server の gc で全アプリに完全な GC → ps)。記録はカーネル・デスクトップ・(Modern) サービスホストの VM プールとスタックの空き、Modern は wasm の共通のプール (`running`・`apps`・`used`・`high`)。
- **判定**: 最初の 3 回は慣らし。その後を前半・後半に分け、最小値 (床) を比べる。後半の床が 1 KB 以上かつ回ごとのふだんの動き (段差の中央値) 以上高く、傾きが 128 B/回以上なら GROWS (落ちる)。Spinel の VM (カーネル) は、大きく下がる回 (振れ幅の 1/4 以上) が 2 回未満なら UNDECIDED (落ちない。回数を増やせと出す)。理由は 3 章。最初は傾きと前後の差で判定していたが、2.1 の数ではどの組も「増える」側に出たので変えた。
- **SPLIT=1**: 全部の組の後に、同じビルドで組ごとにも流す (増えたときの切り分け)。**GROUP=** は組の名前かアプリのパス。**NO_BUILD=1** は build/ の Linux ビルドをそのまま使う (種類が違えば止まる)。
- **SPINEL=1**: 控えのたびに gdb を別コンテナから当て (ルートの CLAUDE.md の手順と同じ。止めずに続く)、Spinel の各インスタンスの数 (`gc_bytes`・`gc_old_bytes`・`gc_threshold`・`str_heap_bytes`・`str_threshold`・`str_old_bytes`・`str_old_threshold`・`gc_cycle`) を読む。3 章の切り分けはこれで行った。
- sim の `/tmp` は平ら (サブディレクトリを作れない) なので、試験のアプリは `/tmp/regress_soak_wasm_probe.app.rb` (checkout の `flash/tmp/`、gitignore 済み)。最初は `/tmp/regress_soak/` に置いて `-7` (NOT_FOUND) で落ちた。
- ファームのコードは変えていない。

### 1.1 実機 (BOARD=) の作り

- 起動と終了は devctl (`/app/launch`・`/app/kill`、`/app/list`)。IP は capture のログの `Connected, ip=` (`BOARD_IP=` で上書き)。
- **数は MCP の capture のファイル `~/.fmrb_mcp/current.log` を読む形に決めた** (`BOARD_LOG=` で別のファイル)。道具はポートを開かない: Tab5 はポートを開くだけでリブートし、親が開いている capture と取り合いにもなる。regress:checkpoint の実機の場面と同じ形。
- 各回の後 (と始めの idle)、最後の kill の後に始まった周期ダンプを DUMPS 個 (既定 2) 待ち、プールごとの最小の使用量、最大の IRAM free・PSRAM free、`fmrb_wasm: runtime` の行を取る。**実機には外から GC をかける口が無い** (debug server は BLE、devctl に gc は無い) ので、数個のダンプの最小で代わりにした。IRAM free・PSRAM free・表の全部の VM プールを判定する。
- 確かめたのは、作り物の実機 (devctl を真似る HTTP と、2 秒ごとにダンプを書き足すログ) に対する 4 回の通しと、ログ・IP・端末の名前が無いときの止まり方まで。本物の実機では流していない (親が流す)。

## 2. sim の結果

### 2.1 既定の 10 回 (SPLIT=1)

カーネルの VM プール (KB、全部閉じて GC の後)。デスクトップとサービスホストはどの回も同じ値 (デスクトップ 783.3〜783.5K、サービスホスト 516.3K)。wasm の共通のプールは全部閉じた後 runtime off・high 28.8K のまま (wasm のアプリが動いている間の used も毎回 28.8K)。

| 種類・組 | idle | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 判定 (今の決まり) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Modern 全部 | 174.5 | 219.1 | 197.8 | 215.7 | 214.6 | 227.2 | 202.8 | 223.6 | 222.5 | 236.0 | 206.7 | settled |
| Modern running | 174.6 | 192.0 | 202.0 | 210.6 | 217.8 | 223.2 | 186.2 | 189.9 | 187.0 | 192.8 | 196.3 | settled |
| Modern running_langs | 174.6 | 182.2 | 184.8 | 187.6 | 188.0 | 187.7 | 192.5 | 192.0 | 194.5 | 237.5 | 220.5 | UNDECIDED |
| Modern running_tools | 174.5 | 169.4 | 157.2 | 192.4 | 182.2 | 170.1 | 204.5 | 191.0 | 180.4 | 214.6 | 200.9 | settled |
| Retro 全部 | 152.5 | 176.7 | 176.6 | 171.4 | 212.4 | 192.9 | 190.2 | 187.3 | 186.9 | 230.2 | 206.2 | settled |
| Retro running | 152.5 | 170.1 | 171.6 | 171.2 | 170.2 | 168.0 | 208.6 | 207.1 | 202.5 | 200.3 | 183.5 | UNDECIDED (30 回で settled) |
| Retro running_langs | 151.9 | 148.8 | 174.1 | 200.8 | 179.3 | 203.1 | 183.0 | 205.1 | 184.1 | 205.8 | 183.5 | settled |
| Retro running_tools | 151.9 | 145.9 | 171.7 | 196.5 | 176.2 | 198.9 | 176.1 | 198.8 | 175.7 | 198.3 | 175.8 | settled |

- 時間: 全部の組の 10 回は Modern 7.1 分・Retro 6.5 分 (ビルドを足して両方で 17〜20 分)。組ごとは 2〜4 分。SPLIT=1 の両方で 33.6 分 (ビルド 2 回を含む)。

### 2.2 回数を増やして (NO_BUILD=1、SPINEL=1)

**Retro running、30 回** (KB、9.2 分):

```
173.2 174.9 175.1 177.2 174.9 174.0 | 218.8 216.9 212.9 196.3 193.3 190.9 190.1 187.2 186.1 188.4 188.5 |
233.2 214.2 210.9 207.5 203.0 201.0 199.0 197.0 195.8 | 176.9 174.4 170.9 | 212.2
```

- 11 回ほどの周期で、跳ね上がり (+45 KB) → ゆっくり下がる → 元の 171〜177 KB に戻る、を繰り返す。29 回目の 170.9K は 1〜6 回目 (173〜177K) より低い。判定は settled (床 -3 KB)。
- 跳ね上がりと下がりは Spinel の文字列の数と一緒に動く: 若い文字列 (`str_heap_bytes`) は 15.5K から回ごとに 2.8K ずつ減って 1.2K になり、次の回に 31.3K へ跳ぶ (閾値 `str_threshold` 32K の手前)。古い文字列 (`str_old_bytes`) は回ごとに 1.2〜1.9K 増え、10〜12K で掃除されて 1.3K に戻る (`str_old_threshold` は 1 MB のままなので、古い世代の掃除は major の予定で来る)。オブジェクトの側の `gc_threshold` (生きている量から決め直される) はプールの跳ね上がりと同じ回に 23K → 82K に上がり、プールと一緒に下がる。

**Modern 全部、30 回** (KB、22.3 分):

```
179.3 208.4 233.5 193.7 215.7 197.7 204.3 222.9 198.4 214.8 221.7 190.8 213.3 212.3 224.2
193.0 210.8 208.8 226.5 202.1 205.4 230.1 198.3 214.6 212.1 229.4 213.0 214.4 186.3 214.0
```

- 2〜4 回ごとに上がり下がりし、床は 186〜198 KB に留まる (12 回目 190.8、16 回目 193.0、29 回目 186.3)。判定は settled (床 -4.6 KB、振れ幅 43.8 KB)。デスクトップ・サービスホスト・wasm の high は 30 回とも同じ値。
- サービスホストの gc の時間切れが 1 回 (6 回目) あり、既知の揺れとして 1 回やり直して通った。

## 3. 17 KB の正体

- regress:run の closed は「起動して代表の組を 1 回ずつ開いて閉じた直後」の 1 点で、毎回同じ手順なので値も毎回同じになる (揺れ 1 KB 未満、rg_gc.md)。組を 4 つのアプリから 9 つに増やすと、その 1 点が Spinel のカーネルの文字列の周期の別の場所に来る (Modern で 4 つのとき 190.6K、9 つで 208.0K)。
- 2.2 のとおり、閉じた後のカーネルは同じ組を流し続けても 1 回ごとに 20〜60 KB 上下する。上下の元は Spinel の文字列の掃除が自分の閾値と予定でしか来ないこと: debug server の gc (`fmrb_spinel_gc_full`) はオブジェクトの完全な GC を強制するが、若い文字列の掃除は `str_threshold` を越えたとき、古い文字列は major の予定のときだけ (spinel_rt/sp_alloc.c の string sweep の gate)。17 KB はこの幅の内側。
- **落ち着く**: Retro の 1 組の 30 回、Modern の全部の組の 30 回とも床は上がらない (Retro 床 -3 KB、Modern 床 -4.6 KB)。この幅の中では、30 回で 1 回あたり数百 B より小さい漏れは見分けられない。
- よって regress:run の基準の更新 (52b3fc07) はそのままでよく、カーネルを漏れとして直すものは無い。
- デスクトップ (mruby)・サービスホスト (mruby) は閉じた後に毎回同じ値に戻る (動きは 0〜200 B)。wasm の共通のプールも同じ (WAMR は最後の wasm のアプリと一緒に止まり、high だけ残る)。

## 4. 実機で流すとき (親)

1. 端末に今の木のファーム (devctl 入り、debug mode ON) が入っていて、WiFi につながっていること。
2. MCP の `serial_start` で capture を開いたままにする (道具は `~/.fmrb_mcp/current.log` を読むだけ。ポートは開かない)。
3. `rake regress:soak BOARD=naryav4` (既定 10 回、DUMPS=2)。1 回は起動 9 つ × 3 秒 + 閉じて 3 秒 × 3 組 + ダンプ待ち 20〜30 秒で 1 分ほど、10 回で 12〜15 分の見込み (未計測)。カーネルは 10 回では UNDECIDED になりうる (3 章の周期)。はっきりさせるなら `ROUNDS=30`。
4. 要約に IRAM free・PSRAM free・各 VM プール・wasm_high の判定が出る (`SOAK_TABLE=1` で回ごとの表も)。結果は `tmp/regress/soak/board.<FMRB_HW_TARGET>.json`。

## 5. 残り・決めていないこと

- **sim の判定の精度** (ユーザが決める): Spinel のカーネルは文字列の周期のせいで、10 回では「数 KB/回」より小さい漏れが見えず、組によっては UNDECIDED になる。debug server の gc が Spinel の文字列の掃除 (若い世代と古い世代の両方) も強制するようにすれば (`fmrb_spinel_gc_full` で文字列の major を起こす)、控えは生きているものだけになり、soak は数回で、regress:run の closed も組の数で動かなくなるはず。ファームの変更なのでこの段では入れていない。
- 予定していた Modern の running_tools・running_langs の 30 回は時間の都合で流していない (10 回では running_tools が settled、running_langs が UNDECIDED)。
- 実機に外から GC をかける口が無い (devctl に gc を足すかはユーザが決める)。今は数個のダンプの最小。
