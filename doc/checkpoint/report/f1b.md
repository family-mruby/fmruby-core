# F1b: 基準の表を小さく、回帰の通しに入れる、止める入口を増やす

> 状態: 実装済・検収待ち | 更新: 2026-11-03 | 基準の常駐の費用の内訳を測り (ネイティブ版 64 bit の 1 VM で 1,018,640 B のうち、クラスの ivar の写しが 241 KB、名前の文字列が 152 KB、ポインタ → 名前のハッシュ表が 147 KB …)、gem の基準を平らな表に作り直して 354,784 B (−65%) にした (新しい hook で名前が 5 個増えて、最後は 355,216 B)。32 bit の見積もりは約 0.8 MB → 336 KB。`rake regress:checkpoint` を `rake regress:full` に入れた。止める入口に、開発の道具 (`fmrb_native.rb app checkpoint` / `restore`、MCP の `native_app`)、Shell の `checkpoint` / `restore`、アプリ自身の `FmrbApp#request_checkpoint` を足した。どれもカーネル → アプリの `_spin` → セーフポイントの同じ道を通る

計画は `doc/checkpoint/plan.md`、指示は `doc/checkpoint/instruction_f1b.md`。F1 は `report/f1.md`。

## 1. 基準の表 (mruby-checkpoint の gem)

### 1.1 内訳 (直す前)

F1 の 1,018,640 B (ネイティブ版 64 bit、`ckpt_counter`、名前 4,041 個) を、gem に一時的な計測を入れて分けた
(コミットしていない)。量は estalloc の 1 ブロックの大きさ (頭 8 B、8 B 揃え、最小 32 B) で数え、合計は
プールの使用量の差と 7 KB 以内で合う。

| もの | 中身 | 量 (B) |
|---|---|---|
| クラスの ivar の写し | snapshot ごとに 1 つの配列 (567 個)、最初から 16 項目分。使っているのは 1,498 項目 = 35,952 B | 241,464 |
| 名前の文字列 | 1 個ずつ割り当て (4,041 個、文字は 98,200 B) | 152,312 |
| ポインタ → 名前のハッシュ表 | オブジェクト用 (容量 8,192) と C の関数用 (容量 4,096)。半分を空けて持つ | 147,456 |
| メソッド表の写し | snapshot ごとに 1 つの配列 (246 個)、最初から 16 項目分。使っているのは 3,140 項目 = 50,240 B | 99,504 |
| 基準のときのオブジェクトの集合 | ハッシュ表 (3,453 個、容量 8,192) | 98,304 |
| irep の表 | 配列 (容量 2,048) と、ポインタ → 番号のハッシュ表 (容量 4,096) | 81,920 |
| 名前の配列 | 文字列へのポインタ・対象のポインタ・関数の印 (容量 4,096) | 69,632 |
| snapshot | 850 個 (容量 1,024)、1 個 64 B | 65,536 |
| 生かしておく Array | 3,453 個 × 16 B | 55,264 |
| ほか | グローバル変数の写し、構造体、割り当ての頭 | 約 7,000 |

効いていたのは、小さな配列を大量に作っていたこと (最小の容量と割り当ての頭)、ハッシュ表を半分空けて
持つこと、同じものを 2 通りに持つこと (オブジェクトの集合と生かしておく Array) だった。

### 1.2 作り直し

基準を作る間は、伸びる表と、名前が付いたかを引くハッシュ表で作る。終わりにハッシュ表を放し、表を
要る大きさに切り詰める (realloc で縮めるので、その場で後ろが返る)。残るのは平らな配列だけ。

| 残るもの | 中身 |
|---|---|
| `names` + `name_off` | 名前を NUL 区切りで並べた 1 つの塊と、各名前の始まり。作る順は前と同じ。**同じビルドなら同じ中身で、VM に依らない**ので、後でファームごとの固定の表 (flash) に置ける (D12 の (c)) |
| `ptrs` + `is_func` | 名前 i の対象のポインタと、C の関数かの 1 bit |
| `obj_idx` / `func_idx` | オブジェクトの名前と C の関数の名前を、ポインタの順に並べた番号。ポインタ → 名前は二分探索 (ハッシュ表をやめた) |
| `keep` | 生かしておく Array を**アドレスの順に並べて**、基準のときのオブジェクトの集合にも使う (別のハッシュ表をやめた) |
| `ireps` + `irep_idx` | irep と中身のハッシュ (見つけた順のまま) と、irep のポインタの順の番号 |
| `snaps` / `mt` / `iv` | snapshot は 32 B (classes の super と中身の digest を共用)。メソッドと ivar は 2 つの共有の配列の範囲。**メソッドは Proc・C の関数をポインタでなく名前の番号で持つ** (これも VM に依らない) |

- 書き出しのときに取るメソッド表・ivar・グローバル変数の写しも、作業の領域 (埋め込む側のアロケータ、
  Family mruby ではプールの外) から取るようにした (`checkpoint_collect_free`)。
- イメージの中身は変わらない (名前・snapshot・項目の順を保った)。mruby-checkpoint の試料 24 個のイメージを
  前の gem と比べ、全部同じバイト列だった。
- 一度、基準を作る間の表も作業の領域から取る形を試したが、ネイティブ版のシステムのプールが足りず
  (`fmrb_sys_realloc FAILED: size=49152`)、アプリのプールに戻した。作る間だけ一時的にハッシュ表の分
  (64 bit で 200 KB ほど) が多く要る。

### 1.3 前後

| どこで | 名前 | 前 | 後 |
|---|---|---|---|
| Family mruby のネイティブ版 (64 bit、estalloc、実測) | 4,041 | 1,018,640 | 354,784 (−65%) |
| 同、この段の hook を足した後 (`regress:checkpoint` のログ) | 4,046 | | 355,216 |
| 同じ gem の組み合わせの 32 bit (ESP32 と同じ値の大きさ、見積もり) | 4,041 | 約 0.8 MB | 約 336 KB |
| mruby-checkpoint の既定の gembox、64 bit (glibc、実測) | 1,611 | 404,784 | 156,416 (−61%) |
| 同、32 bit (`-m32`、glibc、実測) | 1,611 | 279,456 | 130,288 (−53%) |

- 32 bit の見積もりは、同じ数 (名前 4,041、オブジェクト 3,454、irep 1,641、snapshot 850、メソッド 3,140、
  ivar 1,498) に 32 bit の大きさを掛けたもの。`MRB_NO_BOXING` と 64 bit の整数で値は 32 bit でも 16 B
  (riscv32・xtensa は double を 8 B に揃える) なので、ivar の写しと生かしておく Array は 64 bit と同じ大きさ。
  前の見積もりは F1 の report の「およそ半分」より大きい (ivar の配列の最小の容量が 32 bit でも効く)。
- 後の内訳 (64 bit): 名前の塊 98,200、生かしておく Array 55,264、メソッド 37,680 (1 項目 12 B)、ivar 35,952、
  名前の始まり 16,168 + ポインタ 32,328 + 印 507、二分探索の番号 16,164、irep 26,256 + 6,564、snapshot 27,200。
- 残りで大きいのは名前の塊 (と始まりの表) で、合わせて 114 KB。D12 の (c) で flash に移すと、32 bit で
  約 220 KB になる見込み。

### 1.4 確認

| 何を | 結果 |
|---|---|
| mruby-checkpoint の `rake test` | 通る (試料 461、狙った点、インスペクタ 3,267、短い fuzz) |
| `rake test:asan`・`test:stress` | 通る (461 / 461) |

`test:asan` で一度、縮めた後の古いポインタを読む誤りが見つかった (並べ替えの鍵が縮める前の配列を読んで
いた)。直した。

## 2. `rake regress:full` に `regress:checkpoint`

- `rakelib/regress.rake` の `regress:full` の最後に入れた。所要時間の案内は 25 分 → 32 分 (usage.md の表は
  27 分 → 32 分)。
- `doc/regression/usage.md` の 7 章と 12 章を直した。

## 3. 止める入口

どの入口も、F1 と同じ道 (カーネル → アプリの `_spin` → セーフポイント) に乗る。C の側の入口を 1 つに
まとめ (`fmrb_app_checkpoint.c` の `ask_kernel`)、デバッグサーバとアプリの両方がここを通る。

```
デバッグサーバの checkpoint ─┐
FmrbApp.request_checkpoint ──┴→ ask_kernel (D16 の断り、表に PENDING)
  → カーネル: {"cmd" => "checkpoint", "pid", "restart", "from"} → request_checkpoint
  → アプリ: {"cmd" => "checkpoint"} → _spin → セーフポイントで書く (F1 のまま)
  → カーネル: checkpoint_done / checkpoint_refused
     → "from" のアプリに {"cmd" => "checkpoint_result", "pid", "ok", "path" | "reason"}
```

### 3.1 開発の道具

- `tools/fmrb_native.rb app checkpoint PID [--restart] [--max MS]`: デバッグサーバの `checkpoint`。答えは
  `{"status": "written", "path"}`・`{"status": "refused", "reason"}`・`{"status": "timeout"}`。
- `tools/fmrb_native.rb app restore PATH`: イメージ (`.ckpt`) を起動する (spawn と同じ。`.ckpt` でなければ断る)。
- MCP の `native_app` に `action: "checkpoint"` (`pid`・`restart`・`max`) と `action: "restore"` (`path`)。
  description に使い方と、`/tmp` はプログラムの中のものでホストからは見えないことを書いた。
  `tools/mcp/README.md` の表と、`selftest_native.rb` に引数の受け渡しの確認を足した。
- `rake regress:checkpoint` も Python の debug client を直接呼ぶのをやめ、この道具を使う。

### 3.2 Shell のコマンド

```
> checkpoint -r 5
checkpoint: asked PID 5 for its image
checkpoint: PID 5 wrote /tmp/ckpt_counter.ckpt; starting it again
> checkpoint 5
checkpoint: asked PID 5 for its image
checkpoint: PID 5 wrote /tmp/ckpt_counter.ckpt and closed (restore /tmp/ckpt_counter.ckpt)
> restore /tmp/ckpt_counter.ckpt
Spawned: /tmp/ckpt_counter.ckpt
> checkpoint 4
checkpoint: asked PID 4 for its image
checkpoint: PID 4 refused (the app does not declare movable = true in its .app.toml)
> checkpoint 0
checkpoint: PID 0: only an app on the mruby VM can be written
```

- `checkpoint [-r] <pid>`: `kill` と同じ形 (pid は `ps` のもの)。`-r` で書いたらすぐ起動し直す。答えは
  カーネルの `checkpoint_result` で後から出る (`kill_result` と同じ流れ)。Shell 自身は断る。
- `restore <image>`: `.ckpt` を起動する (相対の道は今のディレクトリから)。
- `help` に 2 行足した。

### 3.3 アプリが自分で頼む API

| API | 中身 |
|---|---|
| `request_checkpoint(restart = false)` | 自分のイメージを頼む。頼めたら nil、頼めないときは理由の文字列 (この構成では書けない、など)。カーネルがアプリに頼み返すので、書くのはメッセージ待ちの後のセーフポイント。**`on_event`・`on_update` の中から呼んでよい** |
| `FmrbApp.request_checkpoint(pid, restart = false)` | 別のアプリのイメージを頼む (Shell が使う)。答えは `on_control` に `checkpoint_result` |
| `on_checkpoint` / `on_restore` | F1 の hook。基底クラスに空の定義を置いた |
| `on_checkpoint_refused(reason)` | 自分で頼んだイメージが断られたとき。既定はログに出すだけ。アプリは動き続ける |

- 自分で頼んで書けたときは答えを送らない (アプリは閉じる)。断られたときの答えは `_spin` が
  `on_checkpoint_refused` に回す (ほかのアプリについての答えは `on_control`)。
- gem を持たない構成 (ESP32 の端末、ブラウザ版) では、`FmrbApp.request_checkpoint` は理由の文字列
  「this build cannot write checkpoint images」を返す。Spinel の基底クラスの `request_checkpoint` は、いつも
  「only an app on the mruby VM can be written」を返す (D16)。
- `sig/fmrb_app.rbs` に 5 つ足した (`on_checkpoint`・`on_restore`・`on_checkpoint_refused`・
  `request_checkpoint`・`self.request_checkpoint`)。要約は 112 バイト以内。

### 3.4 試験 (`rake regress:checkpoint` の新しい場面)

| 場面 | 中身 |
|---|---|
| self | `/app/test/ckpt_self.app.rb` (Window なし、`movable = true`) が main の 4 歩目に `on_update` の中から `request_checkpoint(true)`。書かれて起動し直し、a・b・main が続き、頼んだのは 1 回だけ |
| self refused | `/app/test/ckpt_self_refused.app.rb` (`movable` なし) が同じことを頼み、`on_checkpoint_refused` に理由 (movable) が来て、動き続ける |
| shell | Shell の Window を開き、`checkpoint -r <pid>` を打つ (`fmrb_native.rb input`)。カウンタが restart と同じに続き、Shell が答えを受ける |

## 4. 確認

すべて本体の checkout で `MRUBY_CHECKPOINT_DIR=<mruby-checkpoint の f1b>` を付けて流した。

| 何を | 結果 |
|---|---|
| `rake regress:checkpoint` (標準・全部 mruby) | 通る (場面 7 つずつ。1.9 分、作るところから) |
| `rake regress` | 通る (ホスト 20 suites、描画の記録 205) |
| `rake regress:run` | 通る (retro・modern、幅の中。基準は変えていない) |
| `rake regress:screens ONLY=retro,modern,native,native-keys` | 通る (27 / 27。ブラウザ版は流していない) |
| `rake regress:size` | 通る。内蔵 RAM は NARYAv3 (S3) DIRAM 126,035 +0・IRAM +0・ほか +0、TAB5 (P4) DIRAM 129,698 +0・ほか +0。flash の差 (S3 +80 KB、P4 +94 KB) は古い基準 (`6f7b5848`) との差で、F1 の時点 (+74 KB・+88 KB) から 6 KB ほど増えた (Shell・FmrbApp・カーネルのコード) |
| `tools/mcp/selftest.rb` (family-mruby) | 通る |
| 最後の `build/` | TAB5 (esp32p4、RISC-V の ELF) |

`regress:checkpoint` の出力 (標準):

```
standard: ok (restart: a 5->6, b 3->4, main 5->6, t step 450 ms, image 23039 B; wait 2.0s: t step 448 ms;
  refused: the app does not declare movable = true in its .app.toml; window refused at the safe point: C data
  of class FmrbGfx (type 'Graphics') has no entry in the checkpoint data ta; self: a 4->5, b 2->3, main 4->5,
  t step 252 ms; self refused: told why; shell: a 17->18, b 9->10, main 17->18; 28s)
```

イメージの大きさ (23,039 B) は F1 と同じ (gem の直しはイメージを変えない)。

## 5. 気づき

- ネイティブ版のシステムのプール (`fmrb_sys_malloc`) は、基準を作る間の表 (数百 KB) を受けられなかった。
  F2 で大きいイメージを扱うとき、作業の領域の大きさも見ておく。
- 32 bit で基準が 64 bit とあまり変わらない (336 KB と 355 KB) のは、名前の塊 (ポインタの幅に依らない) と、
  値が 16 B のままの配列 (ivar の写し、生かしておく Array) が大きいため。S3 の 500 KB のプールには、
  D12 の (c) (名前を flash に) が要る。
- `checkpoint 0` (カーネル) のように Spinel で動くものは、カーネルに頼む前に C の側で断る (D16)。
  断りは道具には表、アプリには戻り値で返る。

### 5.1 flash に置く形 (`flash_table.md`) のための分け方

平らにした表を、ファームで決まるもの (flash に置ける) と VM ごとに違うものに分けると次のとおり
(数は 64 bit のネイティブ版、4,046 名前のとき)。

| 表 | ファームで決まるか | 大きさ (64 bit / 32 bit) | 補足 |
|---|---|---|---|
| `names` + `name_off` | 決まる | 98,200 + 16,168 / 同じ | 作る順は記号の名前の順で、VM の番地に依らない |
| `is_func` | 決まる | 507 | 名前から分かる情報 |
| `func_idx` (C の関数の名前の、関数の番地の順) | 決まる | 約 4 KB | C の関数の番地はリンクで決まる。`ptrs` のうち C の関数の分もファームの定数 |
| `ireps` (mrblib の irep と中身のハッシュ) と `irep_idx` | 決まる (ROM の irep なら) | 26,256 + 6,564 / 約 26 KB | ネイティブ版・sim の irep はヒープにあるかもしれない。端末では rodata の見込み。要確認 |
| `mt` (メソッドの写し) | ほぼ決まる | 37,680 / 同じ | 名前の番号・記号・フラグだけで、VM の番地を持たない (この段でそうした)。記号の番号がファームで決まる (presym) なら丸ごと flash に置ける |
| `ptrs` のうちオブジェクトの分、`obj_idx` | VM ごと | 約 24 KB + 12 KB / 約 12 + 12 KB | ヒープの番地。flash の形でも残る部分 (`flash_table.md` の「番号 → ポインタ」) |
| `keep` (生かしておく Array、基準のときのオブジェクトの集合) | VM ごと | 55,264 / 同じ | 値が 16 B。番地の配列にして別の手で生かせば 32 bit で 14 KB になるが、GC に生かしてもらう手が要る |
| `snaps` | VM ごと (対象の番地、super、digest) | 27,200 / 同じ | 対象を名前の番号で持てば、super と digest だけが残る |
| `iv` (ivar の写し) | VM ごと (値がオブジェクトの番地) | 35,952 / 同じ | 値を名前の番号に置き換えれば大半は決まる形にできる |

- 今の形のまま名前・始まり・`is_func`・C の関数の番号・irep・メソッドの写しを flash に出すと、32 bit で
  VM ごとの残りは約 150 KB (`keep`・`iv`・`snaps`・オブジェクトの `ptrs` と `obj_idx`)。`keep` と `iv` を
  番号の形にできれば数十 KB まで下がる。
- 名前の塊は VM に依らない形にしてあるので、案 A でも案 B でも、今の `finish_perm` が作る塊をそのまま
  書き出せばよい (C のソースにするか、区画に保存するか)。

## 6. サブの仮決め (ユーザが見直す)

1. 基準を作る間のハッシュ表と伸びる表は、アプリのプールから取る (作業の領域にすると、ネイティブ版の
   システムのプールが足りなかった)。作る間だけ 64 bit で 200 KB ほど多く要る。
2. メソッドの写しは、Proc・C の関数を名前の番号で持つ (1 項目 16 B → 12 B。VM に依らない形)。
3. 名前 → ポインタ (復元で引く) は今までどおり端から比べる (名前の長さを先に比べるようにしただけ)。
   名前の順の索引は足していない。
4. Shell のコマンド名は `checkpoint [-r] <pid>` と `restore <image>`。`kill` に合わせ、Shell 自身は断る。
5. アプリの API は `request_checkpoint(restart = false)` (FmrbApp の `request_*` に合わせた) と
   `FmrbApp.request_checkpoint(pid, restart = false)`。既定は起動し直さない (F2 で別のプロセスへ運ぶ形に
   合わせた)。断られたときの hook `on_checkpoint_refused(reason)` を足した。
6. 基底クラス (mruby) に `on_checkpoint`・`on_restore`・`on_checkpoint_refused` の既定の定義を置いた
   (rbs は実在するメソッドだけを書く決まりのため)。そのぶん基準の名前が 5 個増える (4,041 → 4,046)。
7. Spinel の基底クラスにも `request_checkpoint` を置き、いつも理由を返す (D16)。
8. 道具の `app restore` は `.ckpt` でない道を断る。中身は spawn と同じ。

## 7. 決めてほしいこと (ユーザ)

1. flash に置く形 (Ff、`flash_table.md`) で、`keep` と `iv` (32 bit でも 90 KB) を番号の形にするところまで
   やるか。名前などを flash に出すだけなら VM ごとに約 150 KB、そこまでやれば数十 KB の見込み (§5.1)。
2. Shell のコマンドの名前 (`checkpoint` / `restore`) と、アプリの API の既定 (`restart` なし) でよいか。

> 追記 (2026-11-04、ユーザの決定): アプリの API は `checkpoint` に改名した。`FmrbApp#request_checkpoint(restart = false)` は `FmrbApp#checkpoint(restart: false)`、`FmrbApp.request_checkpoint(pid, restart = false)` は `FmrbApp.checkpoint(pid, restart: false)` (キーワード引数)。カーネルの内部の `request_checkpoint` は `start_checkpoint` になった。本文は当時の名前のまま。
