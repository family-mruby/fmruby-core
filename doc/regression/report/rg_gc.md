# rake regress:run の控えの前に GC をかける

> 状態: 完了 | 更新: 2026-11-03 | debug server に `gc` (各アプリの task が次のメッセージ待ちで自分のエンジンの完全な GC をかける) を足し、`rake regress:run` の 3 つの控えの前に流すようにした。Modern のカーネルの揺れは 68,504 B から 632 B に、閉じた直後のカーネルも 1 KB 未満になって比べるようにした。幅は「4 KB か 3%」に戻した

## 1. 何が困っていたか

`rake regress:run` の控え (debug server の ps) は、その瞬間に各 VM の GC がどこまで進んでいたかで動いた。Modern の 5 回の calibrate で、動作中のカーネルの使用量が 68,504 B 動き、カーネルとサービスホストの幅を 137 KB にしていた (367a44c7)。閉じた直後のカーネルは、閉じたアプリのごみを集め終えたかどうかで 40 KB 動くので比べていなかった (rg3b.md 4 章)。137 KB の幅では、カーネルの側の増加はほぼ見えない。

## 2. 作り

| 場所 | 中身 |
|---|---|
| `fmrb_app.h` の文脈 | `gc_req`・`gc_done` (uint32 2 つ)。頼む側が gc_req を 1 つ進め、アプリの task が GC の後に gc_done へ写す。書き手はそれぞれ 1 つ。文脈の表は PSRAM (FMRB_EXT_RAM_BSS_ATTR) |
| `fmrb_app.c` | `fmrb_app_gc_request` (動作中の mruby・Spinel のアプリだけ頼む。ほかは skipped)、`fmrb_app_gc_poll` (done・pending・gone。gen で枠の使い回しを見分ける) |
| mruby のアプリ (`_spin`、lib/add/picoruby-fmrb-app) | 待ちの輪の頭、idle GC の前で `mrb_full_gc`。GC.start が走るのと同じ所 |
| mruby のカーネル (`_poll_message`、lib/add/picoruby-fmrb-kernel) | 受信の前で `mrb_full_gc` (互換の構成用) |
| Spinel のアプリ・カーネル (`fmrb_spx_app_recv_message`・`fmrb_spx_recv_message`) | 受信の前で `fmrb_spinel_gc_full` |
| `fmrb_spinel_host.c` | `fmrb_spinel_gc_full`: Spinel の GC は世代別で、`sp_gc_collect` だけでは予定どおり (多くは minor) の 1 回になり、古い世代のごみが残る。周期の数 (gc_cycle) を次の full の境目まで進めてから sp_gc_collect を呼ぶと、その回が full (両方のヒープ、古い文字列も) になり、予定はそこから続く。Spinel の取り込み元 (spinel_rt/) には手を入れていない |
| debug server (`fmrb_debugd.c`) | `gc {pid?, max?}`。頼んで、全部が終わるか max ms (既定 3000) まで 10 ms おきに見て、`{apps: [{pid, name, status}]}` を返す。debugger が attach 中のアプリは頼まない (skipped) |
| `tool/debug/fmrb_dbg_client.py` | `gc()` と、1 回きりの `gc` の待ち時間 (max + 2 秒) |
| `tools/regress/sim_run.rb` | 控え = `gc max=5000` → ps。done・skipped 以外 (timeout・gone) があれば落ちる |

- **GC はアプリ自身の task でだけ回す**。debug server の task から VM に触ると、走っている VM と取り合いになる。アプリのメッセージ待ちは、どのエンジンでも GC してよい所 (mruby は C のメソッドの中で GC.start と同じ、Spinel は FFI の呼び出しの中で、生成された C を見ると `_poll_message` の最初の文。その時点で生きている値は枠に登録された局所変数と self だけ。ランチャーが既に呼んでいる `FmrbApp.gc` (fmrb_spx_app_gc) と同じ条件)。
- 頼まれていなければ、待ちの輪で増えるのは比較 1 つ (gc_req と gc_done)。カーネルの受信は文脈を TLS (fmrb_current) で取るので、錠は取らない。
- 答えないアプリ: Lua・BASIC・MicroPython (skipped)、メッセージ待ちに来ない mruby のスクリプト (timeout)。台本の 4 つのアプリ、カーネル、デスクトップ、サービスホストは全部 done になる。GC にかかる時間は sim で全アプリ合わせて 20〜60 ms ほど (debug server の client up / down の時刻の差)。
- debug server は ESP32 にも (BLE で) あるので、`gc` もそこで使える。ファームに残るのは関数とコマンド表の 1 行で、内蔵 RAM の静的な増分は無い (5 章)。

## 3. 揺れ (同じビルドで 5 回、`rake regress:run:calibrate`)

| 数 | Retro 前 | Retro 後 | Modern 前 | Modern 後 |
|---|---|---|---|---|
| アプリ・デスクトップの used | 80 | 0 | 0 | 56 |
| カーネル・サービスホストの used | 40 | 648 | 68,504 | 632 |
| 閉じた直後のカーネル (前は比べない数) | 1,472 | 952 | 1,440 | 824 |
| スタックの空き | 39 | 0 | 32 | 24 |
| コンパイル直後 | 0 | 0 | 0 | 0 |

- 前は 367a44c7 の calibrate (GC なし)。Retro は前もたまたま GC の時機が揃っていた (40 B) が、閉じた直後は前の rg3b の測定で 40 KB 動いていた。
- 後のカーネルの 600〜950 B は、その瞬間にカーネルが持っているもの (処理中のメッセージ、タイマーなど) の違いで、GC の進み具合ではない。サービスホストは 5 回とも同じ値。
- 幅の案は全部 floor のまま (アプリ 512、カーネル・サービスホスト 4,096 か 3%、スタック 512、コンパイル 256)。Modern の gc_used を 137,216 から 4,096 か 3% に戻した。閉じた直後のカーネルは比べる数に入れた (UNCOMPARED を空に)。

## 4. 値の変わり方 (基準 367a44c7 → 今)

GC の後の数は「生きているもの」の量なので小さくなる。コンパイル直後の数は変わらない (GC と関係ない時点)。

| 控え | アプリ | Retro | Modern |
|---|---|---|---|
| idle | カーネル | 168,584 → 165,312 | 175,960 → 175,216 |
| idle | デスクトップ | 799,040 → 780,536 | 780,632 → 780,688 |
| idle | サービスホスト | - | 572,328 → 515,104 |
| running | カーネル | 216,200 → 203,320 | 230,280 → 167,728 |
| running | Kamon | 482,464 → 456,584 | 同じ |
| running | MML | 451,248 → 443,624 | 同じ |
| running | StackChan Remote | 446,312 → 427,120 | 同じ |
| running | Spinel Hello | 432,856 → 413,936 | 同じ |
| closed | カーネル | 202,104 → 173,208 | 199,728 → 182,520 |
| closed | デスクトップ | 815,560 → 780,536 | 787,792 → 780,576 |

- デスクトップは閉じた後も起動前と同じ値に戻る (Retro 780,536 → 780,536)。前は閉じた直後に 16 KB 多く見えていた。
- Modern の動作中のカーネルが起動前より小さいのは、起動前の控えでもカーネルが起動の後始末の何かを持っているため (5 回とも同じ向き)。追っていない。

## 5. 確認

- `rake regress:run` (Retro・Modern、新しい基準): 通る、moved も 0 件 (4.7 分。GC を挟んでも 1 回の sim は 55〜60 秒で前と同じ)。
- `rake regress` (ホスト): 20 suites と 205 runs が通る。
- `rake regress:screens ONLY=retro,modern,native,native-keys`: 27 / 27 通る。ブラウザ版の profile は流していない (ユーザのブラウザが開いている)。
- `rake regress:size` (S3・P4): DIRAM・IRAM・その他の内蔵が NARYAv3・TAB5 とも +0。flash の増分 (+68 KB / +79 KB) は基準 6f7b5848 からの全部の変更の分で、この変更だけの分ではない。
- 実機では流していない (debug server の `gc` は BLE 経由でも同じ処理だが、試していない)。

## 6. 残件

- Spinel の full を強いるやり方 (周期の数を進める) は取り込み元の作りに頼っている。取り込み元に「full を 1 回」の口ができたらそれに替える。
- 実機の BLE で `gc` を試していない。
