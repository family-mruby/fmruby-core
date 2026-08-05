# GC の観測と調整 (mruby アプリ VM)

「時々止まる」「なぜか遅い」「NoMemoryError で死ぬ」の大半は GC で説明が付く。
**推測せずに数えるための道具**をここにまとめる。実例と実測値は
[midi/report/p6.md](midi/report/p6.md) にある (読み込み 7.9 秒の 5/6 が GC
だった件)。

対象は **mruby VM** である。Spinel エンジンで動く VM (`FMRB_KERNEL_ENGINE=spinel`
の kernel など) は GC の実装自体が別物なので、この文書は当てはまらない。

## 0. 先に知っておくこと

- **GC は VM ごと**である。`GC.stat` は**それを呼んだ VM のもの**しか返さない。
  アプリ・desktop・kernel はそれぞれ別の mrb_state を持つ。
- **ヒープはプール固定**である。アプリは `FMRB_MEM_POOL_SIZE_USER_APP` = 500 KB、
  `large_memory = 1` を .toml に書いたアプリだけ 1 MB
  (`components/fmrb_common/include/fmrb_mem_config.h`)。**足りなくなっても
  伸びない**。
- **そのプールは実機では PSRAM にある** (`EXT_RAM_BSS_ATTR`)。マーク・スイープは
  ヒープを舐める処理なので、**GC のコストだけが実機で不釣り合いに高くなる**。
  sim で軽い GC が実機で重いのはこれが理由で、CPU クロック比では説明が付かない。
- **アイドル時に GC を進める仕組みは、現状どの VM でも有効になっていない**
  (3.2 の `GC.scheduler_driven` 参照)。つまり **GC は割り当ての最中にしか
  走らない = 止まるのは必ず何かを割り当てた瞬間**である。

## 1. 再ビルド無しでできること

### 1.1 `GC.stat` の常設キー

どのビルドでも読める。

| キー | 意味 | 読み方 |
|---|---|---|
| `:live` | 生きているオブジェクト数 | 増え続けるならリーク、上下するなら正常な churn |
| `:debt` | 次の GC までの負債。負なら余裕 | 常に負で沈んでいれば割り当て駆動の GC は起きていない |
| `:state` | 0=ROOT 1=MARK 2=SWEEP | サイクルの途中かどうか |
| `:generational` / `:full` | 世代別モードか / 次が major か | 5.2 参照 |
| `:step_limit` | 1 ステップの上限 (0=無制限) | 3.1 |
| `:malloc_increase` / `:malloc_threshold` | malloc 側のバイト増分としきい値 (0=無効) | 文字列など GC 対象外のバイトで GC を起こしたいとき |
| `:symbol_count` / `:dynamic_symbol_count` | シンボル総数 / 動的生成分 | 動的シンボルが増え続けるのは事故 (回収されない) |

### 1.2 ログに 1 行入れる (これが一番使う)

周期ログに次の行を混ぜておくと、**「その詰まりは GC か」がログだけで判定できる**。
実例は `flash/app/demo/midi_apu.app.rb` の `gc_line`:

```ruby
def gc_line
  st = GC.stat
  "[gc live=#{st[:live]} n=#{st[:total] || 0} major=#{st[:major] || 0} " \
    "pause=#{st[:prof_sync_count] || 0}x tot=#{(st[:prof_sync_total_us] || 0) / 1000}ms " \
    "max=#{(st[:prof_sync_max_us] || 0) / 1000}ms]"
end
```

`|| 0` を付けてあるので**計測ビルドでなくても動く** (その場合 `n` 以降は 0)。
`n` / `major` は `MRB_GC_STATS`、`pause` 以降は `MRB_GC_PROFILE` が要る (2 章)。

### 1.3 1 回の呼び出しが何オブジェクト残すかを数える

**GC を止めれば `:live` は減らないので、差分がそのまま割り当て数**になる。
再ビルドも計測ビルドも要らない、一番効く道具である。

```ruby
def measure_alloc(label, times = 200)
  GC.start
  GC.disable
  before = GC.stat[:live]
  i = 0
  while i < times
    yield
    i += 1
  end
  after = GC.stat[:live]
  GC.enable
  GC.start
  Log.info("alloc #{label}: #{(after - before) * 100 / times} objects/100 calls")
end
```

注意:

- **測る間はプールが減り続ける**。`times` を大きくしすぎると測定中に
  NoMemoryError になる。1 回あたり十数オブジェクトなら 50〜200 回で十分。
- **必ず `GC.enable` に戻す**。例外が飛ぶ可能性がある処理を測るなら
  `begin/ensure` で囲む。
- 測定の枠自体が 100 回あたり 2 個ほど乗る。**空のブロックを 1 つ測って
  ベースラインを取る**と読みやすい (`{ [1, 2] }` なら 102/100 = 1 個/回)。

### 1.4 「GC のせいか」を切り分ける A/B

**同じ処理を `GC.disable` して測り、速くなるなら GC が原因**である。
これ以上に直接的な証拠は無い。

```ruby
GC.start
t0 = Machine.uptime_us
result = target_work          # GC 有効
t1 = Machine.uptime_us
GC.start
GC.disable
t2 = Machine.uptime_us
target_work                   # GC 無効、同じ処理
t3 = Machine.uptime_us
GC.enable
GC.start
Log.info("with gc #{t1 - t0}us, without #{t3 - t2}us")
```

実例 (P6): 走査が 22.7 ms 対 2.8 ms で **8 倍**。つまり時間の 7/8 は集めていた。

**`GC.start` を `ensure` の中でやると計測区間に入ってしまう**ので、
時間を測る区間の外に出すこと (P6 で一度これを踏んだ)。

### 1.5 プール側の数字と併せて読む

`GC.stat` はオブジェクト数の話で、バイト数は `FmrbApp.ps` が返す
(`:mem_total` `:mem_used` `:mem_free` `:mem_frag`)。**`:mem_frag` は
NoMemoryError の犯人探しに効く**: 空きバイトは十分なのに大きな String が
確保できない、という形の失敗がある。

```ruby
FmrbApp.ps.each do |e|
  Log.info("#{e[:name]} #{e[:mem_used]}/#{e[:mem_total]} frag=#{e[:mem_frag]}")
end
```

debugd 経由でも外から見える (`fmrb_dbg_client.py localhost:5555 ps`。
ただし `mem_frag` は出ない)。

### 1.6 時間分解能

`Machine.board_millis` は ms、`Machine.uptime_us` は us。**GC の停止は ms 未満の
こともある**ので、計測は `uptime_us` を使う。

## 2. 計測ビルド (`FMRB_GC_PROFILE=1`)

停止時間そのものを知りたいときだけ使う、**既定で無効**のビルドオプション。

```
rake clean_all && FMRB_GC_PROFILE=1 rake build:linux
rake clean_all && FMRB_GC_PROFILE=1 rake build:esp32
```

- `MRB_GC_PROFILE` と `MRB_GC_STATS` の両方が入る。
- **切り替えたら `rake clean` が要る**。cmake は configure 時に環境変数を
  焼き込むので、環境変数を変えただけでは再 configure されない。
- 仕組み: `Rakefile` がコンテナに `-e FMRB_GC_PROFILE` を渡し、rake 側
  (`lib/add/family_mruby_{linux,esp32,esp32p4}.rb`) と CMake 側
  (`components/picoruby-esp32/mruby_abi_defines.cmake`) が**同じ変数を
  コンテナ内で読む**。**これは ABI に効く** (`mrb_gc` が大きくなる =
  `mrb_state` が大きくなる) ので、片側だけ有効だと起動時の ABI ガードで
  abort する。だから両側が 1 つの変数を読む形にしてある。
- コストは GC 1 回につき `clock_gettime` 2 回。常時有効にする理由は無い。

### 2.1 増えるキー

`MRB_GC_STATS` の分:

| キー | 意味 |
|---|---|
| `:total` | GC 実行回数 |
| `:minor` / `:major` | うち minor / major サイクル |

`MRB_GC_PROFILE` の分。`prof_sync` / `prof_step` / `prof_step_jitter` の 3 系統に
それぞれ `_count` `_total_us` `_max_us` `_hist` が付く:

| キー | 意味 |
|---|---|
| `:prof_sync_*` | **同期停止**。割り当ての最中に走った GC = 実際にアプリを止めた分。**まず見るのはこれ** |
| `:prof_step_*` | スケジューラのアイドル時ステップに逃がした分 (現状は常に 0。3.2 参照) |
| `:prof_step_jitter_*` | 上記ステップが、実行可能になったタスクを待たせた時間 |
| `:prof_final_mark_max_us` / `_max_live` | final marking の最長。**インクリメンタルにできない、原理的に残る停止** |
| `:prof_mark_work_total` / `:prof_sweep_work_total` | マークしたオブジェクト数 / スイープしたスロット数。**機種に依らない仕事量**なので、sim と実機で直接比較できる |
| `:prof_emergency_count` | アロケータが空振りして緊急 GC に落ちた回数。**0 でないならプールが逼迫している** |

`_hist` は 20 要素の配列で、**バケット i は 2^(i-1) 〜 2^i マイクロ秒**
(バケット 0 は 0us)。平均ではなく分布を見たいとき用。

`GC.reset_stat` で `prof_*` の累積をゼロに戻せる (計測ビルドのときだけ定義される)。
**区間ごとに取り直したいときはこれを挟む**。`_max_us` は累積の最大なので、
これを呼ばずに 2 か所で読むと「どちらの区間の最大か」が分からなくなる。

## 3. Ruby から触れるつまみ

### 3.1 いつでも使えるもの

| API | 既定 | 効果 |
|---|---|---|
| `GC.start` | - | フルサイクルを 1 回。計測の前に状態を揃えるのに使う |
| `GC.disable` / `GC.enable` | 有効 | 収集を止める / 戻す。**計測用**。常用すると必ずプールを食い潰す |
| `GC.interval_ratio` (=) | 200 | サイクル終了時の負債クレジット。大きいほど GC が疎になり、live のピークが上がる |
| `GC.step_ratio` (=) | 200 | 1 ステップの仕事量。大きいほど 1 回の停止が長く、回数は減る |
| `GC.step_limit` (=) | 0 (無制限) | 1 ステップの絶対上限。**停止の最大値を切りたいときはここ** |
| `GC.malloc_threshold` (=) | 0 (無効) | malloc 側のバイト増分で GC を起こす。String を大量に作る処理向け |
| `GC.generational_mode` (=) | true | 世代別。5.2 も参照 |

**「停止を短く、回数を多く」にしたいなら `step_ratio` を下げるか `step_limit` を
入れる**。ただし総コストは下がらない (むしろ増える)。**根本的に効くのは
割り当てを減らすこと**で、P6 ではそれだけで停止がゼロになった (7 章)。

### 3.2 `GC.scheduler_driven` (mruby-task。現状は使っていない)

`GC.scheduler_driven = true` にすると、**割り当て時の GC を止めて、タスク
スケジューラのアイドル点で GC を進める**モードになる
(`mrbgems/mruby-task/src/gc.c`、`mrb_gc_scheduler_pending` / `mrb_gc_step`)。
停止をアイドルに逃がす道は**既に用意されている**。

**が、Family mruby ではどの VM でも有効にしていない**。理由と注意:

- アプリの主ループは `main_loop` -> `_spin(timeout_ms)` で、**`_spin` は C で
  待つ**。mruby-task のスケジューラのアイドル点を通らないので、有効にしても
  GC を進める機会が来ない可能性が高い。**使うなら先に `_spin` 側に GC を
  進める呼び出しを足す必要がある**。
- 有効にすると `auto_step` が切れる。忙しいままだとヒープが伸び続けるので、
  安全弁として `GC.debt_limit` を設定する (超えると割り当て側で強制的に
  1 ステップ回す)。
- 世代別モードとは併用できない (minor サイクルが 1 ステップ = 分割できないため)。

**これは将来の手段として有望**である。試すときは 2 章の計測ビルドで
`prof_sync_*` (同期停止) が `prof_step_*` に移ったかを見れば効果が分かる。

## 4. ビルド時のマクロ

| マクロ | 既定 | ABI | 用途 |
|---|---|---|---|
| `MRB_GC_PROFILE` | 無効 | **効く** | 2 章。停止時間の計測 |
| `MRB_GC_STATS` | 無効 | **効く** | GC 回数 (`:total` `:minor` `:major`) |
| `MRB_GC_STRESS` | 無効 | 効かない | **割り当てのたびにフル GC**。異常に遅くなる代わりに、「解放済みオブジェクトを触っている」種の壊れ方を最短で表に出せる。C 拡張を書いた直後の検証用 |
| `MRB_GC_FIXED_ARENA` | 無効 | **効く** | アリーナを固定長にする。無効なら自動で伸びる |
| `MRB_GC_ARENA_SIZE` | 100 | 固定時のみ効く | アリーナ長。固定時に溢れると `arena overflow error` |
| `MRB_GRAY_STACK_SIZE` | 1024 | **効く** | グレースタック。溢れるとヒープ全体の再スキャンに落ちる (性能の崖) |
| `MRB_GC_TURN_OFF_GENERATIONAL` | 未定義 | 効かない | 世代別を切る (初期値だけの話。実行時に `GC.generational_mode =` でも同じ) |

**ABI に効くものは rake 側と CMake 側の両方に入れること**
(`components/picoruby-esp32/mruby_abi_defines.cmake` の先頭コメント参照)。
片方だけだと `sizeof(mrb_state)` が食い違い、起動時のガードで abort する。
追加の仕方は `FMRB_GC_PROFILE` の実装がそのまま雛形になる。

## 5. この環境固有の事情

### 5.1 プールはソースのコンパイルでかなり埋まる

**アプリのソースをコンパイルした時点で 500 KB プールの 300 KB 強を使う**
(11 KB のソースで実測 308 KB)。残り 200 KB が実行時に使える全部である。

- 22 KB のファイルを `File#read` すると、**バッファと String で 2 つ分**が
  同時に生き、直前のデータを持っていればさらに 1 つ分。
- 大きいデータを扱うアプリは .toml に `large_memory = 1` を書いて 1 MB プールへ。
  **ただし 1 MB プールは同時に 1 アプリしか使えない**。
- 症状は `NoMemoryError`。**これは `StandardError` ではない**ので、
  アプリ雛形の `rescue => e` を素通りして**何もログに残さずタスクが終わる**。
  落ちる可能性のあるアプリでは `rescue Exception => e` にしておくと原因が残る。

### 5.2 ほとんどの収集が major になる

major サイクルの後、次に major へ上がるしきい値は `live * 1.2` に設定される
(`MAJOR_GC_INC_RATIO`)。アプリの live は 2000〜3000 程度と小さいので、
**少し churn しただけでしきい値を越え、minor がほとんど挟まらない**。
`GC.stat` の `:major` が `:total` にほぼ等しいのはこのためで、異常ではない。

### 5.3 sim と実機の倍率

P6 で同じ処理・同じアプリ・同じプールで測った比は **実機 = sim の約 330 倍**
(GC が支配的な処理の場合)。**これは CPU クロック比ではない**。GC が PSRAM を
舐めるコストが乗っているので、**GC を出さない処理に同じ倍率を掛けると
過大に出る**。

したがって:

- **sim で意味があるのは「オブジェクト数」と「GC 回数」**。これは機種に依らない。
- 時間の外挿は上限の見積りとして扱う。`prof_mark_work_total` /
  `prof_sweep_work_total` は仕事量そのものなので、比較には時間より向く。

## 6. 症状から見るもの

| 症状 | 見るもの | 判断 |
|---|---|---|
| 時々長く止まる | `prof_sync_max_us`、`:total` の増え方 | 停止の最大と観測した詰まりが同じ桁なら GC |
| 常に遅い | 1.4 の A/B | GC 無効で速くなれば GC、変わらなければ処理そのもの |
| だんだん遅くなる | `:live` の推移 | 単調増加ならリーク。`:symbol_count` も見る |
| `NoMemoryError` | `FmrbApp.ps` の `mem_used` / `mem_frag`、`prof_emergency_count` | 空きはあるのに落ちるなら断片化 |
| `arena overflow error` | `MRB_GC_ARENA_SIZE` | C 拡張で `mrb_gc_arena_save/restore` を挟み忘れている |
| 何も出ずにアプリが消える | `rescue Exception` に変えて再現 | 5.1 の NoMemoryError であることが多い |

## 7. 割り当てを減らす定石 (実測の単価)

sim で数えた 1 回あたりのオブジェクト数。**機種に依らない**ので、そのまま
設計の材料になる。

| 書き方 | 個数 | 代わりに |
|---|---:|---|
| `[a, b]` を返す / 積む | 1 | 値を返してもう片方は ivar (`@vl_pos` 方式)、整数にパック |
| 文字列リテラル | 1 | 定数に置く |
| `foo(a, b, key: v)` (キーワード引数) | 1 | 引数で受けるメソッドを用意する |
| Hash への String キー代入 | **2** | Symbol キーにする (未凍結の String キーは複製 + 凍結される) |
| Hash への Symbol キー代入 | **0** | - |
| `MessagePack.pack(hash)` | 2 | - |
| `"\x00" * n` で毎回バッファを作る | 1 | ivar に持って使い回す (同期的に書き出すなら安全) |

**1 イベント / 1 フレームごとに呼ばれる経路では、これらが積算して GC を呼ぶ**。
P6 では合計 2.4 個/イベントを 0 にしただけで、演奏中の GC が消えた。

## 8. 参考

- 実例と実測: [midi/report/p6.md](midi/report/p6.md)
- 計測アプリの雛形: `flash/app/debug/midi_bench.app.rb` (scan / play / alloc の
  3 種類の測り方が入っている)
- 常設の 1 行計装: `flash/app/demo/midi_apu.app.rb` の `gc_line`
- GC 本体: `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby/src/gc.c`
- `GC.scheduler_driven` まわり: 同 `mrbgems/mruby-task/src/gc.c` と
  `include/mruby/gc.h` のコメント
- プールの定義: `components/fmrb_common/include/fmrb_mem_config.h`
- 誤診の記録 (GC のせいにして外した例): [boot_performance.md](boot_performance.md)
