# アプリのメモリ (VM プール)

> 状態: 完了 | 更新: 2026-10-16 | mruby アプリの VM プールの大きさ (機種別)、`.app.toml` の `large_memory`、何が尽きると NoMemoryError になるか (総量ではなく一続きの空き)、ログでの見方、コンパイルの山の実測 (麻雀の卓)

この文書は mruby で動くユーザアプリの VM プールを扱う。GC の観測と調整は
[gc_monitoring.md](gc_monitoring.md)、内蔵 RAM (タスクのスタックなど) は
[internal_ram_budget.md](internal_ram_budget.md)。経緯と測った手順は
[ruby_asterism/report/mj5.md](../ruby_asterism/report/mj5.md) 15 章。

## 1. プールの大きさ

アプリは 1 つずつ専用のプール (estalloc。TLSF 型の割り当て器) を持つ。
mruby のヒープ、コンパイラ、`require` したファイルの中身はすべてここから取る。
**足りなくなっても伸びない**。

| 構成 | 通常 | `large_memory = 1` | 置き場所 |
|---|---|---|---|
| Retro (S3) | 500 KB | 1 MB | PSRAM |
| Modern (P4: Tab5, NARYAv4) | 1 MB | 2 MB | PSRAM |
| Linux sim (64 ビット) | 1.5 MB | 3 MB | ホストのメモリ |
| ブラウザ版 (wasm、32 ビット) | 1.5 MB | 3 MB | ホストのメモリ |

- 定義は `components/fmrb_common/include/fmrb_mem_config.h`。
- 通常のプールはアプリの枠ごとに 1 つ (先頭 3 つは静的、残りは初めて使うときに
  PSRAM から取って、そのあと持ち続ける)。
- `large_memory = 1` のプールは**1 つだけで、同時に 1 アプリしか使えない**。
  2 本目は「LARGE memory pool already in use」で起動を断られる。いま使って
  いるアプリ: shooter, raycaster, rpg_demo, othello, picoruby, picorabbit,
  smf_player, json_probe, midi_bench, mahjong。
- Linux sim は 64 ビットなので、同じスクリプトがおよそ 1.5 倍の場所を取る
  (オブジェクトの頭とポインタが倍)。プールも 1.5 倍なので、**sim は Modern
  実機とほぼ同じ厳しさになる**。ブラウザ版は 32 ビットのままプールが 1.5 倍
  あるので、**実機より緩い**。ブラウザ版で動いても実機と sim で動くとは
  限らない。

## 2. 何が尽きると NoMemoryError になるか

**割り当てが失敗するのは、空きの合計が足りないときではなく、頼んだ大きさの
一続きの空きが 1 つも無いとき**である。

- アプリの起動では、まず本体の `.app.rb` をプールの中でコンパイルする。
  その山 (一時に使う最大量) はソースの大きさの十数倍から二十数倍になる
  (3 章)。山がプールの端近くまで届くと、コンパイルのあとに残るのは、
  生き残った命令列のあいだの細かい隙間だけになる。
- そのあと `require` が次のファイルを丸ごと読む。ファイルの大きさの一続きの
  ブロックが要る。空きの合計が 900 KB あっても、一番大きい隙間が
  ファイルより小さければ失敗する。
- 本体のファイルにコメントを足しても山はほとんど増えない (命令列にならない)。
  メソッドを足すと増える。「コメントなら動くのにメソッドだと落ちる」はこれ。
- `File#read` は 2026-10-16 から、読んだ中身を直接 String に入れる。前は
  一時バッファと String の 2 つを同時に取っていて、同じ大きさの一続きの
  ブロックが 2 つ要った。

## 3. 実測 (麻雀の卓、2026-10-16)

本体 `mahjong.app.rb` 57,156 B (require するファイル 31,836 B + 23,733 B +
8,871 B)。数字は 4 章のログ行から採った。

| 本体 | 32 ビット (ブラウザ版の core) の山 | 64 ビット (Linux sim) の山 |
|---|---|---|
| 57,156 B (今) | 986,312 B | 1,457,936 B |
| 59,013 B (+1.9 KB、落ちた形) | 1,011,224 B | 1,493,296 B |
| 59,218 B (+2 KB) | 1,035,232 B | 1,526,944 B |
| 61,280 B (+4 KB) | 1,085,696 B | 1.5 MB を超える (コンパイルで尽きる) |
| 65,404 B (+8 KB) | 1,183,344 B | 1,736,184 B |

- 山は本体 1 B あたり 32 ビットで約 24 B、64 ビットで約 33 B 増える
  (メソッドを足したとき)。VM の起動分を含めた全体ではソースの 17 倍
  (32 ビット) と 25 倍 (64 ビット)。
- 今の形で Modern の通常プール (1 MB) に入れると、山は 94% になる。
  sim の通常プール (1.5 MB) では 93%。どちらも、あと 2 KB で起動できなくなる
  所にいた。
- 落ちた形の sim の様子: コンパイル直後の使用 573 KB、空き 999 KB、一番大きい
  空き 57.5 KB。`mahjong_xl.rb` の 31,837 B を 1 つ取ると一番大きい空きが
  29.9 KB になり、2 つ目 (前の `File#read` の String) が取れずに
  NoMemoryError になった。
- 動いている間の使用は 64 ビットで 0.81-0.83 MB (sim の `FmrbApp.ps`)。
  山はコンパイルのときで、そのあとは越えない。

麻雀の卓は `large_memory = 1` にした。山は Modern 実機で 2 MB の 47%
(本体 57 KB)、64 KB の上限 (5 章) まで増やしても 58%。

## 4. ログでの見方

アプリを起動するたびに、コンパイルのあとに 1 行出る:

```
fmrb_app: [Mahjong] VM pool after compile: total=1572864 used=567064 free=1005064 largest_free=93240 peak=1457936 frag=535
```

- `peak` は VM を作ってからの最大の使用量 (ブロックの頭を含む)。起動直後は
  コンパイルの山そのもの。
- `largest_free` は一番大きい一続きの空き。これがこの先の大きな割り当て
  (`require`、大きな String や Array) の上限になる。

NoMemoryError で終わったときは、その時点の様子と、失敗した割り当ての
大きさを出す:

```
fmrb_app: [Mahjong] VM pool at NoMemoryError: total=1572864 used=626856 free=945272 largest_free=29872 peak=1493296 frag=357 failed_request=31837 fails=2
```

- `failed_request` が `largest_free` より大きく、`free` は十分ある、なら
  断片化。`free` 自体が小さいなら満杯。
- `fails=2` は mruby が GC をかけて 1 回やり直した分を含む。
- コンパイルの途中で尽きたときは `VM pool at compile failure` の行と、
  画面に「Out of memory while compiling. Set large_memory = 1」が出る。

デバッグモードの 10 秒ごとの表 (`--- VM Pools ---`) にも `Peak` と
`Largest` の列がある。

これらの数は estalloc が割り当てと解放のたびに数えている
(`lib/patch/picoruby-mruby/lib/estalloc/estalloc.c` の `used_now` / `peak` /
`last_fail`。プールの頭に 16 B。一番大きい空きは聞かれたときにプールを
歩いて数える)。

## 5. ほかの上限

- **本体の `.app.rb` は 64 KB まで** (`MAX_SCRIPT_FILE_SIZE`、
  `main/app/fmrb_app.c`)。読んだソースは SYSTEM プール (500 KB、ドライバ類と
  共有) に置かれ、アプリが終わるまで持たれる。`require` するファイルに
  この上限は無い (アプリのプールに読む)。麻雀の卓の本体はあと 8.4 KB で
  ここに当たる。大きくなるアプリは、プールより先にこちらに当たるので、
  メソッドを `require` するファイルに分ける。
- タスクの C スタックは `task_stack_kb` (既定 16、`[16, 64]` に丸める)。
  内蔵 RAM から取るので、プールとは別の話 (internal_ram_budget.md)。

## 6. アプリを書く人へ

- 起動で NoMemoryError になったら、まず 4 章の 2 行を見る。
- 本体が大きいアプリ (目安: 32 ビットで山が通常プールの 7 割を超える。
  Modern なら本体 40 KB 前後から) は `large_memory = 1`。ただし同時に 1 本。
- 本体を増やすより、`require` するファイルに分ける方が山が低い
  (ファイルごとにコンパイルし、終わった分の一時領域は返る)。
- ブラウザ版だけで確かめない (1 章)。sim か実機でも起動する。
