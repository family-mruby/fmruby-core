# 報告 S2b: estalloc の統計を残して解放ごとの全走査を止める (止まる条件で停止)

> 状態: 計測済・停止 (止まる条件) | 更新: 2026-10-02 | 統計は `ESTALLOC_DEBUG` 無しで取れる形に分けられ、P4-Nano で
> 負荷時の Shell の打鍵の 50 ms 以上が 98% → 11%、全 VM の GC の総時間が 1/7-1/20、Spinel のカーネルの GC も 1/4 になった。
> ただし検査を外すと、アプリの終了時に Shell が落ちる (Guru)。`fmrb_app.c` の `mrc_irep_free` が VM の解放済みのメモリを
> もう一度解放しており、これまでは `ESTALLOC_DEBUG` の検査が黙って捨てていた。指示書の止まる条件に当たるので、変更は
> コミットせず、場所と差分を残して止めた

## 結論

- 分け方は小さく済む。`est_take_statistics` の定義を `#if defined(ESTALLOC_DEBUG)` の外に出すだけで、統計は
  `ESTALLOC_DEBUG` 無しで取れる (estalloc の README は元々「どのビルドでも使える」と書いている)。そのうえで
  family-mruby の全ビルドから `ESTALLOC_DEBUG` を外す。新しいマクロは要らない (理由は下の「分け方」)。
- 効果は大きい (下の表)。統計 (VM Pools の Used / Free / Frag、`FmrbApp.pool_usage`) は従来どおり出る。静的な D/IRAM は
  増えない (125,604 B のまま)。
- **しかし入れられない**。検査を外したビルド (走査だけ外したもの・統計だけにしたもの、どちらも) で、Shell を終了させた
  瞬間に `Store access fault` で落ちた。場所は `main/app/fmrb_app.c:950` の `mrc_irep_free(cc, irep_obj)` →
  `mrb_basic_alloc_func` → `est_free` → `add_free_block`。develop (検査あり) では同じ所で 180 回の不正な解放が
  検査に弾かれていた (下の「見つかった不正な解放」)。
- 次の手は 2 つを順に: (1) `mrc_irep_free` の二重解放を直す (estalloc とは別の不具合。直し方は親とユーザの判断)、
  (2) そのうえで本書の変更 (scratchpad の `s2b/change.diff`) を入れる。(1) の前に (2) を入れると、mruby のアプリを
  閉じるたびに落ちうる。

## `ESTALLOC_DEBUG` の中身

estalloc (picoruby の submodule `mrbgems/picoruby-mruby/lib/estalloc`、上流 picoruby/estalloc 971b793) の
`ESTALLOC_DEBUG` が入れるもの。

| 中身 | どこで | 費用 | 使い手 (family-mruby) |
|---|---|---|---|
| 統計 `est_take_statistics` | 呼んだとき | 呼んだときだけプールを先頭から 1 回たどる。確保・解放の経路には何も足さない | VM Pools の周期ダンプ、`FmrbApp.pool_usage` / `pool_used`、`ps`、Spinel アプリの pool 使用率 (いずれも `mrb_get_estalloc_stats`)、`mrb_alloc_statistics` |
| 解放時の検査 | `est_free` のたび | **プールの先頭から対象のブロックまで物理順にたどる (解放 1 回がブロック数に比例)**。範囲外・二重解放・permalloc・不正な番地を見つけると `error_message` を書いて**何もせずに戻る** | なし (`error_message` を読む所が無い)。結果として、不正な解放を黙って捨てる働きをしていた |
| 解放時の埋め 0xff | `est_free` のたび (上の検査の中) | 解放する大きさに比例 | なし |
| 確保時の埋め 0xaa | `est_malloc` / `est_permalloc` のたび | 確保する大きさに比例 (`est_calloc` はこの後さらに 0 で埋める) | なし |
| プール全体の 0 埋め | `est_cleanup` | プールの破棄 1 回につきプールの大きさに比例 | Spinel のインスタンスの終了 (`fmrb_spinel_instance_end`)。mruby は呼ばない |
| profiling (`est_start_profiling` / `est_stop_profiling`、`PROFILE()`) | 確保・解放のたび | profiling 中でなければ分岐 1 つ。中ならプール全走査 | なし (しかも `take_profile` は構造体のコピーに書くので結果が残らない。上流の不具合) |
| `est_sanity_check` | 呼んだとき | プール全走査 | なし |
| `ESTALLOC` 構造体に `prof` を足す | 型 | 構造体が 16 B ほど大きくなる | なし。`stat` は先頭なので、外から読む位置は `ESTALLOC_DEBUG` の有無で変わらない |

費用の大きさは S2 で測ったとおり、2 段目の解放時の検査がほぼ全部 (GC の掃除が死んだ中身を 1 個ずつ解放するので、
全体 GC 1 回が「解放の数 × プールのブロック数」になる)。埋めの分は、下の表の B1 (走査だけ外して埋めは残す) と
B2 (統計だけ) の差で見た。

## 分け方と理由

当てた差分 (コミットしていない。scratchpad の `s2b/change.diff`、estalloc.c の全体は `s2b/estalloc.patch.c`):

1. `lib/patch/picoruby-mruby/lib/estalloc/estalloc.c` (新規、submodule の estalloc.c の全体の写し) —
   `est_take_statistics` の前の `#if defined(ESTALLOC_DEBUG)` を、その次の `take_profile` の前へ移すだけ (3 行)。
   rakelib/setup.rake の既存の `cp -rf lib/patch/picoruby-mruby` で submodule に入るので、setup.rake の変更は要らない。
2. `lib/patch/picoruby-mruby/src/alloc.c` — `mrb_alloc_statistics` の `#if defined(ESTALLOC_DEBUG)` を外し、
   常に `est_take_statistics` を呼ぶ (今は `ESTALLOC_DEBUG` 無しだと古い値を返す)。
3. `lib/patch/picoruby-mruby/mrbgem.rake` — family-mruby が足していた「常に `ESTALLOC_DEBUG=1`」をやめ、上流と同じ
   「`PICORB_DEBUG` のときだけ」に戻す。
4. `lib/add/family_mruby_linux.rb` / `family_mruby_wasm.rb` — 通常ビルドの `ESTALLOC_DEBUG=1` を消す
   (`PICORB_DEBUG` のときの `ESTALLOC_DEBUG` は残す)。esp32 / esp32p4 は元々 `PICORB_DEBUG` のときだけ。

指示書の案 (検査を `ESTALLOC_DEBUG_CHECKS` に分け、`ESTALLOC_DEBUG` を統計だけにする) にしなかった理由:

- 統計は元々 `ESTALLOC_DEBUG` を要らない作りで、README も「In any build」と書いている。定義の位置がずれているだけ
  なので、直すのは 1 か所の移動で済み、上流にも出しやすい。
- `ESTALLOC_DEBUG` の意味を変えると、上流の既存の使い手 (picoruby の `PICORB_DEBUG` ビルド、estalloc 自身の debug
  テスト) が黙って二重解放の検出を失う。今回の形なら `ESTALLOC_DEBUG` の意味は変わらない。
- 埋めはユーザの方針 (統計だけでよい) どおり外れる。測ると、埋めを残した B1 と外した B2 で mruby の GC はほぼ同じで、
  差は Spinel の側に出た (カーネルの GC 平均 2.1 → 1.4 ms、エディタ 14 → 3.8 ms。回数が少ないので幅はある)。
  `PICORB_DEBUG` のビルドでは検査も埋めも従来どおり入る。

両経路の定義: estalloc.c は rake の libmruby (picoruby-mruby の gem の cc) でだけ compile され、Spinel 側
(`components/fmrb_spinel_rt/fmrb_spinel_host.c`) は `void*` で呼ぶだけで estalloc.h の構造体を見ない。CMake 側で
estalloc.c を compile する所も `ESTALLOC_*` を定義する所も無い。定義の場所は mrbgem.rake と lib/add の build config
だけで、どちらも全ビルド (esp32 / esp32p4 / linux / wasm) を通る。

## estalloc の使い手

| 使い手 | 経路 | 効くか |
|---|---|---|
| mruby の VM (デスクトップ、Services、ユーザの mruby アプリ全部) | `mrb_basic_alloc_func` → `est_realloc` / `est_free` (タスクごとの est) | 効く |
| コンパイラ (prism) | VM の est (`fmrb_mrb_current` 経由、lib/patch/compiler) | 効く |
| Spinel のカーネル・エディタ・Spinel アプリ | `fmrb_spinel_host.c` の hook → `est_calloc` / `est_realloc` / `est_free`、終了時 `est_cleanup` | 効く (同じ estalloc.o) |
| Spinel の gem (FFT、raycast、spinel_hello) | `fmrb_spinel_instance_begin` で自分の est | 効く |
| MicroPython | 自前の GC ヒープを `fmrb_malloc` で確保 (`components/micropython/fmrb_mp.c`) | 使っていない |
| Lua / BASIC | estalloc を呼ばない | 使っていない |

## 測った値 (P4-Nano、NARYAv4、同じコミット 337286c0 = develop 9c6f83f1 + 文書、`FMRB_GC_PROFILE=1`)

計測の方法は S2 と同じ (SHLAT の計装、遠隔の打鍵 100 ms ごと、10 秒ごとの VM ごとの GC)。今回は Spinel のインスタンス
ごとの GC の回数と累計 (`sp_ctx` の `gc_stat_collections` / `gc_stat_seconds`) と、検査に弾かれた解放の数も 10 秒ごとに
出した (一時的な計装。scratchpad の `s2b/instrumentation.diff`、`s2b_est_rejects.diff`)。

- (a) デスクトップ + Services + Shell。192 打鍵。
- (b) (a) + MML・Breakout.py・Monitor。192 打鍵。
- (c) (b) の Breakout.py を BlockGame (mruby) に替え、5 分間、30 秒ごとに 32 打鍵。
- (d) ユーザのアプリを全部閉じ、FM-Editor (Spinel) を開いて 192 打鍵。

3 つのビルド:

| | estalloc |
|---|---|
| B0 | develop と同じ (`ESTALLOC_DEBUG` あり、検査・埋めあり) + 弾いた解放を数える計装 |
| B1 | 解放時の走査と範囲の検査だけを外す (埋めは確保・解放とも残す)。S2 の構成 4 に近い |
| B2 | 本書の変更 (`ESTALLOC_DEBUG` 無し、統計だけ) |

### Shell の打鍵 (打鍵から present まで)

| | B0 | B1 | B2 |
|---|---|---|---|
| (a) 50 ms 以上 | 99/192 (52%) | 1/192 (1%) | 4/192 (2%) |
| (a) 最大 | 418 ms | 59 ms | 196 ms |
| (a) 50 ms 未満の平均 | 25.3 ms | 17.8 ms | 18.9 ms |
| (a) Shell の全体 GC | 10 秒で 18 回、平均 213 ms、最大 380 ms | 20 秒で 33 回、平均 12.8 ms、最大 36 ms | 10 秒で 18 回、平均 16.2 ms、最大 60 ms |
| (b) 50 ms 以上 | 183/186 (98%) | 24/192 (13%) | 21/192 (11%) |
| (b) 最大 | 5,144 ms | 257 ms | 217 ms |
| (b) Shell の全体 GC | 28 回、平均 589 ms、最大 1,220 ms | 18 回、平均 23.5 ms、最大 63 ms | 25 回、平均 24.1 ms、最大 52 ms |
| (c) 50 ms 以上 | 225/320 (70%) | 18/320 (6%) | 18/320 (6%) |
| (c) 最大 | 1,821 ms | 268 ms | 248 ms |

### 全 VM の GC ((c) の 5 分間)

| VM | B0 | B1 | B2 |
|---|---|---|---|
| FM-Shell (mruby) | 60 回、計 16.6 秒、最大 582 ms | 55 回、計 0.84 秒、最大 83 ms | 55 回、計 0.81 秒、最大 112 ms |
| Monitor (mruby) | 383 回、計 25.7 秒、最大 625 ms | 426 回、計 4.5 秒、最大 103 ms | 426 回、計 4.2 秒、最大 59 ms |
| BlockGame (mruby) | 46 回、計 6.1 秒、最大 741 ms | 38 回、計 0.44 秒、最大 40 ms | 48 回、計 0.60 秒、最大 43 ms |
| MML (mruby) | 0 回 (待機中) | 0 回 | 0 回 |
| Services / system_desktop (mruby) | 0 回 (デスクトップは idle_gc で割り当て経路の回収が無い) | 0 回 | 0 回 |
| fmrb_kernel (Spinel) | 20 回、計 107 ms、平均 5.4 ms | 21 回、計 45 ms、平均 2.1 ms | 24 回、計 34 ms、平均 1.4 ms |

### Spinel

| | B0 | B1 | B2 |
|---|---|---|---|
| カーネル (b) | 2 回、平均 9.6 ms | 1 回、2.6 ms | 1 回、2.3 ms |
| カーネル (c) | 上の表 | 上の表 | 上の表 |
| FM-Editor (d) 192 打鍵 | 6 回、計 139 ms、平均 23.2 ms | 7 回、計 99 ms、平均 14.1 ms | 4 回、計 15 ms、平均 3.8 ms |

- Spinel の GC 時間は `sp_gc_stat_seconds` (1 回の回収の頭から終わりまで) で、Spinel の掃除も死んだ中身を
  `est_free` で返すので同じく効く。B1 と B2 の差 (埋めの分) は Spinel のほうが大きく出た。
- `hid_lat` は今回も 1,000 イベントに届かず出ていない。

### 統計が出ること

- B2 (`ESTALLOC_DEBUG` 無し) の周期ダンプで VM Pools の Used / Free / Total / Frag が全 VM 分出ている。値の範囲は B0 と同じ
  (例: (c) の終わりの FM-Shell が B0 373,768 B / B2 373,848 B、Frag 57% / 57%)。
- `FmrbApp.pool_usage` も B0 と同じ値を返した (Shell の SHLAT の行の `pool=35%`)。
- 統計の費用は変わらない (ダンプの `pools=` の中央値 B0 142 ms / B2 142 ms)。
- map で `est_take_statistics` があり `est_sanity_check` が無いことを見た (B2 に `ESTALLOC_DEBUG` が入っていない確認)。

## 見つかった不正な解放 (止まった理由)

- B0 で、弾かれた解放は**動いている間は 0 回**で、アプリの終了の時だけ出た: Shell (強制 kill)・MML・BlockGame を閉じた
  10 秒で 119 回、Monitor を閉じた 10 秒で 61 回、計 180 回。最後の種類は `est_free(): Illegal address.` (プールの中だが
  ブロックの頭ではない番地 = 既に解放されて隣と合わさったブロックをもう一度解放した形)。
- B1 と B2 では、(c) の後に Shell を閉じた瞬間に落ちた。ログは `FM-Shell: Script ended` → `[FM-Shell] No exception detected`
  の直後に `Guru Meditation Error: Core 1 panic'ed (Store access fault)`、MTVAL 0x91940464。B2 の ELF で解くと
  `add_free_block` ← `est_free` ← `mrb_basic_alloc_func` ← `mrc_irep_free` ← `execute_mruby_script` (`main/app/fmrb_app.c:950`)。
  B1 も同じ番地・同じ流れ (B1 の ELF は上書きしたが、MEPC が同じ 0x4017051e)。
- 該当のコード (`main/app/fmrb_app.c` の `execute_mruby_script` の末尾):

  ```c
  mrb_vm_ci_env_clear(ctx->mrb, ctx->mrb->c->cibase);
  mrc_irep_free(cc, irep_obj);
  mrc_ccontext_free(cc);
  ```

  `mrc_create_task` は `mrc_irep` をそのまま `mrb_irep` として `mrb_proc_new` に渡すので、irep は VM の参照数と GC の
  持ち物になる。`mrc_irep_free` は参照数を見ずに子の irep・pool・syms・iseq を全部解放するので、GC が既に返した物を
  もう一度解放する。上流の picoruby (r2p2 の main.c、picoruby.c) は `mrc_create_task` の後に `mrc_irep_free` を呼ばない。
  同じ関数の少し下 (`mrb_close` を呼ばない理由のコメント) に「`mrc_irep_free` の後の `mrb_close` は二重解放で落ちる」と
  あり、この二重解放は前から気づかれていたことになる。
- これまで落ちなかったのは、`ESTALLOC_DEBUG` の検査がこの解放を「不正な番地」として黙って捨てていたから。
- 直していない (指示書どおり)。直し方の候補は `mrc_irep_free` を呼ばない (irep は VM の GC に任せる) だが、
  終了時にプールごと捨てる今の作りとの兼ね合い (`mrb_close` を呼ばない理由、`script_buffer` の扱い) を確かめる必要がある。
- S2 の構成 3・4 (検査を外した試験) では Shell を閉じておらず、落ちた記録も無い (構成 4 では MML と BlockGame を
  親の側で止めている)。今回の B1・B2 でも落ちたのは Shell のときで、二重解放が落ちるかどうかは、その時のプールの
  並び次第と見ている (確かめていない)。

## 静的な D/IRAM

- develop (337286c0) の NARYAv4、計装なし: **Used stat D/IRAM 125,604 B**、Flash 6,220,148 B。
- 本書の変更を入れた NARYAv4、計装なし: **125,604 B** (増減なし)、Flash 6,219,716 B (-432 B。検査のコードが消えた分)。

## 確かめたこと・確かめていないこと

- 確かめた: NARYAv4 のビルド (B0/B1/B2 の計測ビルドと、変更だけの通常ビルド)、P4-Nano での (a)-(d)、統計の出力、
  静的な D/IRAM。
- 止まる条件に当たったので確かめていない: TAB5 / S3 / Linux (標準・互換) / wasm のビルド、`rake test`、sim と
  ブラウザ版の前後、sim のエディタ 1 打鍵。再開時はここから。

## 上流への説明の下書き

estalloc (picoruby/estalloc) 向け:

> est_take_statistics() is documented as available "in any build" (README, Debug Functions), but its definition sits
> inside `#if defined(ESTALLOC_DEBUG)`, so a build that wants the statistics has to define ESTALLOC_DEBUG. That also
> turns on the release-time check in est_free(), which walks the pool from the top to the block being released, making
> every free O(number of blocks), and fills every allocated and released block. This moves est_take_statistics() out of
> the ESTALLOC_DEBUG block so that the statistics follow the README; ESTALLOC_DEBUG keeps its meaning (checks, fills,
> profiling, est_sanity_check). The header comment on ESTALLOC_STAT ("define ESTALLOC_DEBUG") and the
> `#if defined(ESTALLOC_DEBUG)` around est_take_statistics() in test/test.c can go with it.

picoruby (picoruby-mruby の src/alloc.c) 向け:

> mrb_alloc_statistics() calls est_take_statistics() only under ESTALLOC_DEBUG, so without it the returned hash holds
> whatever est->stat last contained (zeros after est_init). With est_take_statistics() available in every build
> (estalloc PR above), call it unconditionally.

別件 (estalloc の不具合、出すなら別 PR): `take_profile()` と `est_start_profiling()` は `ESTALLOC_PROF prof = est->prof;`
とコピーに書き込むので、profiling の結果が `est->prof` に残らない。

## 見立てと違った点

- 検査は「デバッグのための余分な手間」だと見ていたが、実際には family-mruby の既存の二重解放を黙って吸収していた。
  検査を外すことは、その二重解放を表に出すことと同じだった。
- 埋め (0xaa / 0xff) の費用は mruby の GC ではほぼ見えず、Spinel の GC で見えた。

## 端末と作業ツリーの状態

- 端末 (P4-Nano) は develop と同じファームウェア (337286c0、NARYAv4、計装なし) に焼き戻した。ミュートのまま
  (volume 6)。端末に置いたファイルは無い。
- build/ は develop の NARYAv4 の通常ビルド。
- 作業ツリーは計装・変更・submodule の上書き (estalloc.c) をすべて戻した。コミットはこの報告だけ。

## 残したもの (scratchpad の `s2b/`、コミットしていない)

- `change.diff` (本書の変更 4 ファイル)、`estalloc.patch.c` (lib/patch に置く estalloc.c の全体)、`estalloc.c` /
  `estalloc.orig.c` (変更後と上流の元)。
- `instrumentation.diff` (S2 の SHLAT と VM ごとの GC + Spinel の GC + 弾いた解放の数)、`../s2b_est_rejects.diff`
  (B0 の estalloc の数え方)。
- `ana.rb` (集計)、`phase_c.sh` / `phase_d.sh` / `run_all.sh` (計測の台本。(a)(b) は `../s2/phase_{a,b}.sh`)、
  `B0.log` / `B1.log` / `B2.log` (各構成のシリアルログ)、`runs_B*.txt` (行番号)、`B2.elf` (落ちた番地を解いた ELF)。
