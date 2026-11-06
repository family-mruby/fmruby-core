# ブラウザ版の時間の待ちが何十倍にも伸びる (Worker ごとに時計がずれる)

> 状態: 解決 | 更新: 2026-11-04 | この PC (WSL2) の Linux の Chromium で、ブラウザ版の `sleep_ms(100)` が 1.7〜4.4 秒かかった。原因はヘッドレスでもタイマーの間引きでもなく、port の時計: `emscripten_get_now()` (Worker ごとの `performance.timeOrigin + performance.now()`) が Worker ごとに違う時刻を返し、後から作られたタスクほど先へ進んでいた。tick はそのとき走っているタスクの時計で進めるので、先を行くタスクが tick を押し上げると、遅れた時計の idle タスクは追いつくまで tick を進められない。時計を `Date.now()` (全 Worker で 1 つ) に替えて直した。直した後は 10 x 100 ms が 1,000〜1,054 ms

## 1. 症状

- F5 (doc/checkpoint/report/f5.md 7 章): ヘッドレスの Chromium で、ブラウザ版の
  `task_sleep` の 100 ms の待ちが 1.7〜3.4 秒、`ckpt_counter` の 250 ms の 1 歩が
  3.4 秒、Asterism の 1 回の呼び出しが 3〜5 秒。
- F4 (report/f4.md 4 章): ときどき Task の待ちが 20 倍ほどに延びた。
- ブラウザ版の配置の作業: Shell の 2 個目以降のコマンドとして起動した
  `task_sleep` が 6.5〜12.5 秒。
- どれも「ヘッドレスのブラウザの性質」と見られていた。

## 2. 調べたこと

### 2.1 再現

同じコミットのブラウザ版を自分の port (8031、8032) で出し、Playwright の
Chromium (Linux 版) で開いて、Shell から `task_sleep`・`task_turns` と、
`on_update` の間隔を 20 回ずつ書き出すだけのアプリ (`_spin(50)` の長さを測る)
を起動した。

| 条件 | `task_sleep` (10 x 100 ms) | `task_turns` (10 歩、1 歩 120 ms) | `_spin(50)` の間隔 |
|---|---|---|---|
| 起動直後に最初のアプリとして | 1,024 ms | - | - |
| 起動から 40〜70 秒後に Shell から | 1,031 / 1,831 / 1,491 ms | - | - |
| 起動から 4.5 分後に Shell から | - | - | 325 ms と 0 ms が交互 |
| 起動から 3 分待ってから Shell から (ヘッドレス) | 3,866〜4,402 ms | - | 平均 335 ms |
| 同上、タイマーの間引きを止める引数を全部付けて | 3,775〜3,795 ms | 3,773 ms | 平均 243 ms |
| 起動から 2 分待ってから Shell から (Window あり、Xvfb) | 2,733〜3,278 ms | 3,266 ms | 平均 232 ms |

- 起動から時間が経ってから起動したアプリほど遅い。最初のアプリは遅くない。
- ヘッドレスか Window ありか、`--disable-background-timer-throttling`・
  `--disable-renderer-backgrounding`・`--disable-backgrounding-occluded-windows`・
  `--disable-features=IntensiveWakeUpThrottling` の有無では変わらない
  (Playwright は最初の 1 つを自分で付けている)。
- ページの主スレッドは詰まっていない: `setTimeout(0)` の間隔は中央値 4.1 ms・
  最大 49 ms。主スレッドへ回る呼び出し (ログの `fd_write`、ファイル) を JS で
  計ると、どれも 1 ms 以下で戻っている。
- 遅いときの `_spin(50)` は「長い・0 ms」の交互になる。`_spin` は FreeRTOS の
  tick で終わりを決めるので、tick が止まっては跳ぶ形をしている。

### 2.2 一時的な計装 (コミットしていない)

`hosted/port/port.c` に、5 秒ごとに次を出す計装を入れた。

- idle フックが 1 ms 眠ったあと tick を 1 つも進められなかった回数。
- タスクごとの `emscripten_get_now() - Date.now()` (そのタスクの時計が
  壁時計からどれだけずれているか)。

起動から 3 分待ってアプリを起動した回の値:

```
idle_nofeed=4518 (5 秒間の idle フック約 4,700 回のうち)
IDLE=-459.6  system_desktop=-459.5  fmrb_kernel=-459.7  ...  (起動時に作られたタスク)
Services=-404.1  FM-Shell=-105.9  Task Sleep=-54.8  Spin Probe=-54.8
```

idle タスクの時計は、後から作られた `Task Sleep` の時計より 405 ms 遅れている。
そのとき `task_sleep` の 1 回の待ちは 387 ms (100 ms のはず)、idle フックは
ほぼ毎回 tick を進められていない。

### 2.3 Worker の時計

素のページで確かめた。Worker を作り、`performance.timeOrigin +
performance.now()` を主スレッドの値と比べる (往復の真ん中との差)。

| ブラウザ | 10 秒後に作った Worker | 40 秒後に作った Worker |
|---|---|---|
| Linux の Chromium、ヘッドレス | +34.9 ms | +70.9 ms |
| Linux の Chromium、Window あり (Xvfb) | +55.6 ms | - |
| Windows の Chrome、ヘッドレス | +1.7 ms (作った直後だけ、すぐ 0) | 0 |

ずれが決まるのは Worker を作ったときではなく、**その Worker が初めて
`performance` に触れたとき**だった。Worker を 5 つ同時に作り、i 番目に
初めて時刻を聞くのを 10 x i 秒後にすると、30 秒後と 40 秒後に初めて聞いた
Worker だけ +78 ms ずれた。作った直後に全部の Worker で `performance.now()`
を 1 回呼んでおくと、40 秒後に初めて `timeOrigin` を聞いても 0.2 ms 以内に
そろう。

この PC の Linux (WSL2) は壁時計を 30 秒ほどごとに 50〜70 ms 前へ進めている
(2 分で 4 回、合わせて 241 ms。単調な時計はそのぶん遅れている)。Linux の
Chromium は Worker が初めて `performance` に触れたときの壁時計を起点にし、
あとは単調な時計で進めるので、Worker ごとに「そのときまでに壁時計が
進められた分」だけ時刻が違う。ブラウザ版の Worker は起動時にまとめて
作られる (pthread の pool、56 個) が、時計に初めて触れるのはそのタスクが
初めて走ったとき (`dPortClockNowMs()`) なので、後から起動したアプリの
タスクほど先へ進んで見える。Windows の Chrome では起きない (人のブラウザ、
`tools/fmrb_web.rb` がこの PC で使う Chrome は Windows のもの)。

### 2.4 仕組み

ブラウザ版の FreeRTOS の port (hosted/port/port.c) は、タスクごとに pthread
(= Worker) を作り、tick を壁時計からの経過で進める (`prvCatchUpTicks()`: 
「今の時刻 - 起点」までの tick をまだ渡していない分だけ渡す。戻ることはない)。
時計 (`dPortClockNowMs()`) は `emscripten_get_now()` で、呼んだタスクの
Worker の時刻になる。

1. 後から作られたアプリのタスク (時計が D ms 先) が CPU を手放すとき、
   `vPortYield()` が tick をそのタスクの時刻まで進める。
2. 待ちに入ったあと、tick を進めるのは idle フックだけになる。idle タスクの
   時計は D ms 遅れているので、その時計が追いつくまで「渡す tick が無い」。
   tick は D ms 止まる。
3. VM のスケジューラの待ち (`ulTaskNotifyTake` 5 ms)、`_spin` の待ち
   (`fmrb_msg_receive`)、`vTaskDelay` は、どれも D ms 余計にかかる。アプリの
   `sleep_ms` は VM の自給の tick (`fmrb_hal_time_get_us()` = 壁時計) で
   数えるので正しく 100 ms で期限が来るが、VM が起きるのが D ms 遅れる。

D は「起動からアプリのタスクが初めて時計を読むまでに WSL2 が壁時計を進めた分」なので、
起動から長く経ったセッション (F5) ほど大きく (1.7〜3.4 秒 = 起動から十数分
〜数十分)、起動直後の最初のアプリでは 0 に近い。F4 の「ときどき」は、壁時計の
進め方が飛び飛びなことによる。

Linux の sim・ネイティブ版は `clock_gettime(CLOCK_MONOTONIC)`
(port_clock_posix.c) で、プロセスの中の全スレッドに同じ時計なので起きない。
実機には関係しない。

## 3. 直し方

`hosted/port/port_clock_wasm.c` の時計を `emscripten_date_now()`
(`Date.now()`) に替えた。

- 全 Worker で同じ 1 つの時計になる。端末の他の部分 (`fmrb_hal_time_get_us()`
  = `gettimeofday()` = ここでは `Date.now()`、`Machine.board_millis`、
  VM の自給の tick、`_spin` の GC の締め切り) が使っている時計とも同じになる。
- 分解能は 1 ms (tick と同じ)。
- 壁時計は戻されることがあるので、戻った分は足し込んで値が戻らないようにした
  (全 Worker で共有する 64 bit の atomic 2 つ)。前へ進められた分は tick の
  まとめ渡しになる (長く CPU を持ったタスクの後と同じ扱い)。WSL2 では
  30 秒ほどごとに 50〜70 ms のまとめ渡しになり、その間の待ちがその分早く
  終わることがある。
- 変えたのはブラウザ版だけが使う 1 ファイル (と port_clock.h の説明)。
  ネイティブ版・sim・実機のコードは変わらない。

## 4. 確認

同じコミットで直す前と後を作り、並べて流した (起動から 2〜3 分待ってから
Shell で起動。`task_turns` の期待値は 1,200 ms)。

| 条件 | 直す前 | 直した後 |
|---|---|---|
| ヘッドレス、3 分後 | `task_sleep` 3,866〜4,402 ms、`_spin(50)` 平均 335 ms | 1,001〜1,050 ms、平均 50.1 ms |
| ヘッドレス、間引きを止める引数つき、3 分後 | 3,775〜3,795 ms、`task_turns` 3,773 ms、平均 243 ms | 1,001〜1,054 ms、1,142 ms、平均 50.0 ms |
| Window あり (Xvfb)、2 分後 | 2,733〜3,278 ms、3,266 ms、平均 232 ms | 1,000〜1,026 ms、1,200 ms、平均 50.1 ms |

- `rake regress`: ok (ホストの試験 20/20、描画の記録 205/205)。
- 変わったのはブラウザ版だけのファイルなので、`regress:screens`・
  `regress:size` は流していない (sim・ネイティブ版・実機のビルドに入る
  コードは変わらない)。

## 5. ブラウザでの確認のしかた

- 直した後は、ヘッドレスか Window ありか、タイマーの間引きを止める引数の有無に
  関係なく、Linux の Chromium で時間の待ちを測ってよい。道具の起動のしかたは
  変えていない。
- 直す前のビルド (公開中の版を含む) を Linux の Chromium で測ると、起動から
  時間が経つほど遅く見える。数字を比べるなら、起動直後に起動したアプリで
  比べるか、Windows の Chrome (`tools/fmrb_web.rb up --headless` がこの PC で
  使うもの) で測る。

## 6. メモ

- F5 の Asterism の 1 回の呼び出しが 3〜5 秒かかったのも、同じ tick の
  止まりによると見ている (呼び出しの往復は FreeRTOS のキューの待ちを通る)
  が、確かめていない。直した後のブラウザ版と Asterism の通しは、この作業
  では流していない。
- zenoh-pico のブラウザ版の時計 (wasm/zenoh/zp_system_emscripten.c) も
  `emscripten_get_now()` だが、ブラウザ版の zenoh-pico は 1 スレッドで
  使う作り (`Z_FEATURE_MULTI_THREAD` が 0) で、同じ Worker の中で比べるので
  このずれを受けない。変えていない。
- F5 の「`fmrb_web.rb put` で `/home` に置いた file を端末が読めない」は、
  道具の path がページの中の MEMFS の path だから: 端末の `/home` は
  MEMFS では `/flash/home` にある (`put LOCAL /flash/home/x` なら端末の
  `/home/x` で読めた)。
- 調べる途中で、主スレッドへ回る呼び出しを JS で計る包みを `core_web.js` に
  入れた回は、直す前のビルドでも遅くならなかった (3 分後に起動しても
  `task_sleep` 1,001〜1,201 ms。包みの無い同じビルドを並べて流すと
  4,008〜4,811 ms)。包みは読み込みのときに `performance.now()` を呼ぶので、
  pool の全 Worker の時計が起動時にそろっていた。2.3 の見立てと合う。
  ただし pool を使い切ってから作られる Worker には効かないので、直し方には
  使わなかった。
