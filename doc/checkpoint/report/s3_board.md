# S3 (NARYAv3) の実機: アプリ終了で止まる件と、アプリのプールの割り当て

> 状態: 実装済・検収待ち | 更新: 2026-11-07 | NARYAv3 で `regress:checkpoint` の実機の場面がアプリの終了で全体ごと止まった原因は、枠を使い回すときに前のアプリの `parked` が残っていたこと (kill が片付けの途中のタスクを消し、ログの錠を持ったまま消えた)。枠の始めに `parked` を戻して直し、実機の場面は全部通った。S3 のユーザアプリのプールを「通常 1 (500 KB) + 大きい方 2 (1 MB)、すべて静的、同時に 3 つまで」にした。転送のアプリが大きい方を取ること、3 つ動くこと、4 つ目が断られることを実機のログで確認。内蔵 RAM は全端末で増えていない

関連: 計画は `doc/checkpoint/plan.md`、S3 に gem を入れた経緯は `report/f6_s3.md`・`report/s3_next.md`、プールの説明は `doc/reference/app_memory.md` 1 章。

## 1. 止まる件 (B)

### 1.1 症状

`rake regress:checkpoint BOARD=narya_v3 BOARD_ONLY=1 NO_BUILD=1` が 2 回とも、
チェックポイントに関わったアプリ (1 回目は `Ckpt Self gen=2`、2 回目は
`Ckpt Counter gen=6`) を `/app/kill` で止めたところで止まった。画面も固まり、
その後ログが 1 行も出ず、Guru・abort・watchdog も無い。最後の行は

```
I fmrb_app: [Ckpt Counter] mruby VM closed in 12 ms
I fmrb_app: [Ckpt Counter gen=6] Exited on request, reaping
I fmrb_app: [Ckpt Counter gen=6] State: STOPPING -> FREE
```

### 1.2 読み

- `Exited on request, reaping` は `fmrb_app_kill` の行。kill は 10 ms ごとに
  枠の state と `parked` を見て、`STOPPING` かつ `parked` なら
  `fmrb_app_reap` でタスクを消す。
- アプリの側は `app_task_main` の片付けで、`g_ctx_lock` を持ったまま
  `STOPPING` にし、VM を閉じ、資源を返し、錠を放してから
  `Resources cleaned up` を出し、カーネルへ exit を送り、**最後に** `parked = true`
  にして眠る。
- 止まった回は `Resources cleaned up` が出ていない。つまり kill は、アプリが
  `parked = true` にする前に `parked` を真と読んでいる。kill は `g_ctx_lock` で
  待っていたので、アプリが錠を放した瞬間 (`Resources cleaned up` を書き
  始める所) に `STOPPING` を見る。そこで `parked` が真ならタスクを消す。
- `parked` を偽に戻す所がどこにも無かった。枠をゼロにするのは
  `free_ctx_index` (起動の失敗の巻き戻しだけが呼ぶ) で、普通の終わり方
  (`fmrb_app_reap`) は state を FREE にするだけ。`reset_ctx_run_state` は
  `started_notified` しか戻していなかった。だから **同じ枠の 2 本目からは、
  前のアプリの `parked = true` が最初から立っている**。
- 消されたタスクは `Resources cleaned up` のログを書いている途中 (ログの錠を
  持っている) だった。錠は戻らず、ログを書くタスク (カーネル、kill の続きの
  `Reaped` も) が全部止まる。`fmrb_app.h` の `parked` の注釈が書いている
  「途中で消すと stdout などの錠が残る」そのもの。

### 1.3 証拠

一時的な計装 (`reset_ctx_run_state` で、枠を渡すときの `parked` の値をログに
出す) を入れて焼き、同じ場面を流した (計装は外した)。

```
I (10:32:19.430) fmrb_app: DIAG slot 4 reuse: stale parked=1
I (10:32:24.420) fmrb_app: DIAG slot 5 reuse: stale parked=1
I (10:32:29.415) fmrb_app: DIAG slot 5 reuse: stale parked=1
...
```

使い回した枠はすべて `stale parked=1` (初めて使う枠だけ 0)。直したあとは、
kill で止めたアプリが全部 `Resources cleaned up` → `Exited on request, reaping`
→ `Reaped` の順になる (前は `Resources cleaned up` の前に reaping が来ていた)。

```
I (10:32:55.533) fmrb_app: [Ckpt Counter gen=6] State: RUNNING -> STOPPING
I (10:32:55.562) fmrb_app: [Ckpt Counter gen=6] Resources cleaned up
I (10:32:55.582) fmrb_app: [Ckpt Counter gen=6] Exited on request, reaping
I (10:32:55.582) fmrb_app: [Ckpt Counter gen=6] State: STOPPING -> FREE
I (10:32:55.586) fmrb_app: [Ckpt Counter gen=6] Reaped
```

### 1.4 直し方

`reset_ctx_run_state` (枠を渡すたびに通る) で `parked = false` にする。
タスクを消す所を飛ばしたり、kill の待ちを変えたりはしていない。

### 1.5 チェックポイントと S3 に見えた理由

- チェックポイントの場面は同じアプリを何度も起こして kill で止めるので、
  枠 5 が何度も使い回される。kill で止めるのも場面の決まり。
- 古い `parked` があると、kill は毎回アプリが眠る前に消しにいく。全体が
  止まるのは、そのときアプリが錠 (ログの錠など) を持っているときだけで、
  時間の並び次第。kill はアプリの片付けの間 `g_ctx_lock` で待っていて、
  アプリが錠を放した直後 (`Resources cleaned up` を書き始める所) に動き出す
  ので、当たりやすい。同じ回の `Ckpt Calls` は `Resources cleaned up` を
  書き終えてから消されたので無事だった (これも眠る前の削除で、たまたま
  錠を持っていなかった)。S3 はアプリが core 1、kill (devctl) が別の core で
  同時に走るので、ログの錠の取り合いの中で消す形になりやすい。P4 で
  出なかったのは時間の並びの違いと見ている (穴は共通の C にあり、直しは
  P4 にも効く)。

### 1.6 撤回した仮説

- チェックポイントの道がタスクに何か (mutex・セーフポイント用のタスク・
  tick の hook) を持たせたまま: 違う。止まる前に `Resources cleaned up` が
  出ていないこと、計装で古い `parked` が見えたこと、直したあと全場面が通る
  ことで説明がつく。
- タスクが本当に眠る前に消されている: これはその通りで、原因が `parked` の
  残りだった。
- S3 の 2 core の問題: 2 core は当たりやすさに効くだけで、原因ではない。

### 1.7 残件 (判断は親)

- `expected_stop` も同じく枠の使い回しで残る (どこでも偽に戻していない)。
  前のアプリが頼まれて閉じた枠で、次のアプリが例外で落ちると「頼まれた
  終わり」と扱われうる。直すと落ちたときの知らせが出るようになる
  (利用者に見える動きが変わる) ので、今回は触っていない。

## 2. S3 のアプリのプール (A)

### 2.1 決めたこと (ユーザ決定)

S3 の端末 (NARYAv3・StickS3・AtomS3R。`fmrb_mem_config.h` の Modern でも
Linux でもない枝) のユーザアプリのプールを、通常 1 (500 KB) + 大きい方 2
(1 MB)、すべて静的にする。同時に動くユーザアプリは 3 つまで。

### 2.2 実装

- `fmrb_mem_config.h`: プール ID は全ビルドで同じ並び (mrbgem の port は
  機種の define なしでこの header を読むので、並びを機種で変えない)。
  `POOL_ID_USER_APP_LARGE` の次に `POOL_ID_USER_APP_LARGE1` を足した。
  機種で変わるのは `FMRB_USER_APP_POOL_COUNT` (通常のプールの数。S3 は 1、
  ほかは枠の数)、`FMRB_USER_APP_STATIC_POOL_COUNT` (S3 は 1、ほかは 3)、
  `FMRB_USER_APP_LARGE_POOL_COUNT` (S3 は 2、ほかは 1)。
- `fmrb_mempool.c`: 静的なプールは配列 (`g_mempool_user_app[n]`、
  `g_mempool_user_app_large[n]`)。プールの番地の表 (`g_mempool_list`、
  プール ID ごとに 1 語、内蔵 RAM の .data) をやめ、静的なプールは
  `fmrb_get_mempool_ptr` のコードで名指しし、初めて使うときに取る通常の
  プールだけ小さな表 (`g_mempool_dyn`、S3 では無い) に持つ。ID を 1 つ足しても
  内蔵 RAM が増えないようにするため (最初の形は NARYAv3 の DIRAM が +16 で
  `regress:size` が落ちた)。大きさの無い ID (S3 の通常 1〜4) は
  `fmrb_mempool_reserve` が断る。`fmrb_mempool_is_user_large()` を足した。
- `fmrb_app.c`: `g_large_pool_in_use` をやめ、プールは起動のとき、枠と同じ
  `g_ctx_lock` の中で選ぶ (`pick_user_pool`)。使用中かどうかは「FREE でない
  ユーザの枠がその `mempool_id` を持っているか」で決める (枠が FREE に
  なるまで、つまりタスクが消えるまで持つ。終わり方ごとに旗を戻す必要が
  なくなった)。`large_memory = 1` は空いている大きい方。それ以外は空いて
  いる通常のプール、無ければ空いている大きい方。
  - どれも空いていない: `No free app memory pool (all 3 in use)` を出して
    `FMRB_ERR_NO_RESOURCE` (枠が無いときと同じ。カーネルの文は
    「No free app slot. Close a running app and try again.」)。
  - `large_memory = 1` で通常のプールだけ空いている: `No free LARGE memory
    pool` を出して `FMRB_ERR_FAILED` (前の「LARGE memory pool already in use」
    と同じ返り値)。
- Modern・sim・ブラウザ版・ネイティブ版の割り当ては変えていない (大きさ・
  静的の数・大きい方 1 つ)。通常のプールは「枠の番号のもの」から「空いて
  いる一番若いもの」になったが、通常のプールが枠と同じ数あるので、空いた
  枠があれば必ず空いた通常のプールがある。
- `fmrb_app_checkpoint.c`: イメージの `.toml` の `large_memory` は、VM が
  走っていたプールが大きい方かどうか (`fmrb_mempool_is_user_large`)。
  S3 では普通のアプリが大きい方に入ることがあり、ヒープが 500 KB を超えて
  いるかもしれないので、写す側も大きい方に入れる。
- `flash/usr/share/transfer/transfer.app.toml` の `large_memory = 1` (親の変更)。
- 文書: `doc/reference/app_memory.md` 1 章・6 章、`doc/app-distribution/spec.md`
  の `large_memory` の行。

### 2.3 PSRAM の静的な量 (S3)

| | 前 | 後 |
|---|---|---|
| 通常 | 500 KB x 3 (静的) + 最大 2 (使うときに取る) | 500 KB x 1 |
| 大きい方 | 1 MB x 1 | 1 MB x 2 |
| 静的の計 | 2,524 KB | 2,548 KB (+24 KB) |
| 同時に動くユーザアプリ | 5 (PSRAM が許せば) | 3 |

静的な量は 24 KB 増え、使うときに取る 1 MB (500 KB x 2) は無くなる。

### 2.4 内蔵 RAM (`rake regress:size ALL=1`)

基準 4e7e7e61 との比較 (`ok`、13.0 分)。内蔵 RAM はどの端末も増えていない
(減ったのは `g_mempool_list` をやめた分。S3 は 52 B の表が無くなり、P4 は
初めて使うときに取るプールの 2 語だけが残る。ほかの差はこの作業の前からの
基準とのずれ)。

| 端末 | DIRAM | IRAM | ほかの内蔵 | アプリのイメージ |
|---|---|---|---|---|
| NARYAv3 | 125,411 -48 | 16,384 +0 | 104 +0 | 5,214,080 +208 |
| TAB5 | 129,338 -360 | - | 948 +0 | 7,214,448 +525,520 |
| NARYAv4 | 124,756 -440 | - | 3,052 +0 | 7,191,536 +525,472 |
| STICKS3 | 114,455 -72 | 16,384 +0 | 104 +0 | 5,023,568 +160 |
| ATOMS3R | 114,099 -64 | 16,384 +0 | 104 +0 | 5,008,768 +128 |

- 最初の形 (表 `g_mempool_list` に ID を 1 つ足しただけ) では NARYAv3 の
  DIRAM が +16 で落ちた (`libfmrb_mem.a` internal +4、残りは並びの詰め物)。
  2.2 の形に直して通した。
- S3 の `libfmrb_mem.a` の flash の列の +24 KB は PSRAM の .bss (静的なプール
  が 24 KB 増えた分、2.3)。
- P4 のイメージ +525 KB は基準が古いため (この作業の前からの差。最初の回も
  同じ値)。
- 基準は更新していない (親が判断)。

### 2.5 実機 (NARYAv3) のログ

起動のときにカーネルが起こす転送のアプリが大きい方を取る:

```
I (00:00:00.337) MEMPOOL:    9 USER_LARGE   0x3c6ac9f0 - 0x3c7ac9f0 (1048576 bytes)
I (00:00:00.344) MEMPOOL:   10 USER_LARGE   0x3c7ac9f0 - 0x3c8ac9f0 (1048576 bytes)
I (10:32:04.430) fmrb_app: USER_APP pool: idx=4 mempool_id=9 (large, 1024 KB)
I (10:32:07.262) fmrb_app: [Transfer] VM pool after compile: total=1048576 used=434704 free=613480 largest_free=401488 peak=638720 frag=203
```

転送のアプリ + 小さいアプリ 2 つで 3 つ、4 つ目は断られる:

```
I (10:33:15.812) fmrb_app: USER_APP pool: idx=5 mempool_id=4 (ordinary, 500 KB)        <- ckpt_counter
I (10:33:19.256) fmrb_app: USER_APP pool: idx=6 mempool_id=10 (large, 1024 KB, the ordinary ones are busy)  <- ckpt_calls
E (10:33:22.697) fmrb_app: [Ckpt Self Refused] No free app memory pool (all 3 in use)
```

転送のアプリを止めて小さいアプリを 3 つ (`/app/list` に Ckpt Self Refused・
Ckpt Counter・Ckpt Calls)。通常のプールを空けても、大きい方が 2 つとも
ふさがっていれば `large_memory = 1` は断られる:

```
I (10:33:32.420) fmrb_app: USER_APP pool: idx=4 mempool_id=9 (large, 1024 KB, the ordinary ones are busy)  <- ckpt_self_refused
E (10:33:38.224) fmrb_app: [Transfer] No free LARGE memory pool
I (10:33:40.510) fmrb_app: USER_APP pool: idx=5 mempool_id=4 (ordinary, 500 KB)        <- ckpt_counter をまた
```

`/app/launch` の答えは順に `{"ok":true,"pid":4}`・`5`・`6`・`{"ok":false,"err":-9}`、
後半は `{"ok":false,"err":-6}` (転送のアプリ)。

## 3. 試験の結果

最後の形 (計装なし、cae98733 と同じ中身) を NARYAv3 の app 区画にだけ焼いて流した。

- `rake regress`: host 21/21、描画 205/205。
- `rake regress:size ALL=1`: ok (2.4 の表)。
- `rake regress:checkpoint` (ホスト): standard・mruby・web とも ok (6.3 分)。
- `rake regress:checkpoint BOARD=narya_v3 BOARD_ONLY=1`: ok (1.7 分)。

```
board narya_v3 (standard): ok (self on the board: a 4->5, b 2->3, main 4->5, t step 341 ms, image 23539 B; in: a 6->7, t step 449 ms, image 23072 B (3316 ms); out: a 9->10, t step 712 ms, image 23152 B (moved to rg-ckpt-a as PID 6); window: to the board and back; calls: moved, missing linux_settings, platform esp32 there, no @platform, associations read again; requires: refused before it stopped; no answer: refused (no answer from rg-ckpt-nobody/transfer/inbox check (nobody a); 41s)
```

  board self・in・out・window・calls・requires・no answer の全部が通った
  (window は NARYAv3 の小さい画面でも通る。画素は比べない場面)。
  そのときの実機のログに Guru・abort・強制終了は 0 件、kill で止めた 4 本は
  すべて `Resources cleaned up` の後に `Exited on request, reaping` が来る。
  計装つきの版でも同じ場面を 1 回通した (1.3)。
- `rake regress:run` (sim の VM プール) は流していない。Linux の割り当ては
  大きさも数も変えていない (通常のプールの選び方だけが「枠の番号」から
  「空いている一番若いもの」になった)。ネイティブ版の checkpoint の場面は
  この選び方の上で通っている。

## 4. 残件

- 1.7 の `expected_stop` (親の判断)。
- `test/regress/boot_baseline.json` に作業の前からの未コミットの差分
  (narya_v3 の起動ログの基準) があり、触っていない。
