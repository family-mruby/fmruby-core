# 報告 S2c: アプリ終了時の二重解放を直し、estalloc の統計だけを残す

> 状態: 完了 (親の検収待ち) | 更新: 2026-10-02 | T1 (bfa8aeb4) で mruby アプリ終了時の `mrc_irep_free` をやめ、不正な解放は
> 0 回になった。T2 (a40c7fa9) で S2b の変更 (統計を `ESTALLOC_DEBUG` の外へ、`ESTALLOC_DEBUG` を通常ビルドから外す) を入れた。
> P4-Nano で起動・終了のくり返しと負荷時の打鍵を測り、S2b の B2 と同じ程度の効果が出た。静的な D/IRAM は 125,604 B のまま。
> 1 件だけ、Spinel のエディタを 2 つ同時に開いて閉じた後の 3 回目の起動で abort が出た (下の「残件」)

## 結論

- 二重解放の正体は「VM に渡した irep を、コンパイラ用の解放関数で解放していた」こと。組み込みアプリ (Shell、Monitor など
  バイトコードで持つもの) では、mruby の読み込み器が作った irep の内側の番地を解放しており、検査がそれを黙って捨てていた。
- `mrc_irep_free` を呼ぶのをやめた (T1)。上流の picoruby の同じ経路 (r2p2、picoruby コマンド) も `mrc_create_task` の後に
  irep を解放しない。irep はアプリのメモリプールごと消える。
- 不正な解放はアプリ終了 1 回あたり Shell 200 前後・Monitor 61 → **全対象で 0**。プールの使用量も起動・終了をくり返して戻る。
- T1 の上で T2 を入れ、負荷時の Shell の打鍵の 50 ms 以上は S2b の B0 (develop) 52% / 98% / 70% → 2% / 10% / 6% になった。
  S2b で落ちた「Shell を閉じる」も落ちない。

## T1: 二重解放の原因と直し方

`main/app/fmrb_app.c` の `execute_mruby_script` の末尾は、アプリのスクリプトが終わった後に次を呼んでいた。

```c
mrb_vm_ci_env_clear(ctx->mrb, ctx->mrb->c->cibase);
mrc_irep_free(cc, irep_obj);
mrc_ccontext_free(cc);
```

irep の出どころは 2 つある。

| 読み込み方 | irep を作る関数 | 中身の並び | `mrc_irep_free` で起きること |
|---|---|---|---|
| バイトコード (組み込みアプリ: Shell、Monitor、Services など) | `mrb_read_irep` (mruby の読み込み器) | irep の構造体・pool・reps・syms を **1 つの確保の中に詰める** (`MRB_IREP_CONSOLIDATED`) | pool・reps・syms の番地 (確保の先頭ではない) を `est_free` に渡す。子の irep ごとに同じことをくり返す |
| ソース (ユーザのアプリ: MML、BlockGame など) | `mrc_load_string_cxt` (コンパイラ) | 部品ごとに別々に確保 | 番地としては正しいが、VM の proc がまだ参照している irep を消す |

`mrc_irep_free` はコンパイラの並びしか知らず、`MRB_IREP_CONSOLIDATED` を見ない。前者は「確保の先頭ではない番地の解放」で、
estalloc の検査は `est_free(): Illegal address.` として黙って捨てていた。検査を外すと、その番地を空きブロックとして
つなぎ込むので、空きリストが壊れて次の解放で落ちる (S2b の Guru)。

計測で確かめたこと (下の「不正な解放の数」): 不正な解放は全部 `mrc_irep_free` の中で出ており、ソースから作った irep
(mode=1) では 0 回、バイトコードの irep (mode=0) でだけ出た。`mrc_ccontext_free` と `mrb_vm_ci_env_clear` では 0 回。

直し方は `mrc_irep_free` の呼び出しを消すだけ。

- irep の持ち主: `mrc_create_task` は `mrb_proc_new` に irep を渡し、proc が参照を 1 つ持つ。上流の r2p2 (`mrb_read_irep` →
  `mrc_create_task` → `mrb_task_run` → `mrb_close`) も picoruby コマンドも、自分では irep を解放しない。
- `mrb_irep_decref` で作り手の参照を返す形 (mruby の `mrb_load_exec` の作法) も考えたが、採らなかった。proc が回収された時点で
  `mrb_irep_free` がコンパイラ製の irep に走ることになり、実行中に別の経路を通すことになる。`mrb_close` を呼ばない今の作りでは
  得るものがなく、上流の同じ経路とも違う。
- `mrc_ccontext_free(cc)` は残した。cc は自分で作ったコンパイラの文脈で、`mrc_create_task` の中の `mrc_resolve_intern` が
  シンボルを VM のシンボルに置き換えた後は、VM 側は cc を参照しない。計測でもここでの不正な解放は 0 回。
- `destroy_vm` と強制終了の経路の「`mrc_irep_free` の後に `mrb_close` を呼ぶと落ちる」という注記は、事実でなくなったので
  書き換えた (`mrb_close` を呼ばないのは変えていない)。

副次の効果: develop では Shell を閉じると、検査に弾かれる解放 (1 回ごとにプールを先頭から走査) が `mrc_irep_free` の中で
200 回前後続き、終了待ちの時間切れで**強制終了**になっていた (`Killed by force; system may be degraded`)。T1 の後は
Shell も通常の終了で閉じる。

## 同じ形の箇所の洗い出し

`mrc_irep_free` と `mrb_irep_decref` の呼び出しを、main/、components/fmrb*、lib/add、lib/patch、lib/replace、wasm/、
それに picoruby の submodule (ビルドに入る gem) で grep した。

| 場所 | 中身 | 判断 |
|---|---|---|
| `main/app/fmrb_app.c` (mruby アプリの起動) | 本件 | T1 で直した |
| Spinel のアプリ (エディタ、Spinel の gem) | mruby の irep を使わない | 該当なし |
| BASIC・Lua・MicroPython | mruby の irep を使わない | 該当なし |
| picoruby-eval (`eval`、mruby 版) | `mrc_irep_free` は上流でコメントアウト済み。irep は proc に任せる | 問題なし (irep はプールごと消える) |
| picoruby-sandbox (`Sandbox`、Shell の irb もこれ) | 解放は `mrb_irep_decref` (上流のコメントに「`mrc_irep_free` は proc が生きていても消すので使わない」) | 問題なし |
| picoruby-require | sandbox 経由 | 問題なし |
| mruby-compiler の `mrb_generate_code` | 一度ダンプして `mrb_read_irep_buf` で読み直し、`mrb_irep_decref` + 元のコンパイラ製 irep を `mrc_irep_free` | 自分で作った物を自分で消しているだけで問題なし |
| picoruby-wasm (js.c)、picoruby-picorubyvm、mrubyc 版の各 gem | family-mruby のビルドに入っていない (gembox に無い、VM が mrubyc) | 対象外 |

family-mruby のコードで同じ形はこの 1 か所だけだった。

## 不正な解放の数 (前後)

測り方: S2b の数え方 (`s2b_est_rejects.diff`) を submodule の estalloc.c に一時的に当て、`fmrb_app.c` に一時の行
(`S2CREJ`) を足して、`mrc_irep_free` の前後と `destroy_vm` で累計を出した。`ESTALLOC_DEBUG` あり (develop と同じ検査)。
P4-Nano、NARYAv4。1 周 = Shell (irb に入って `42`、`exit`) → MML → BlockGame → Monitor → FM-Editor (1 打鍵相当) →
Spinel Hello → 試験用のユーザのアプリ (`eval` と `Sandbox` を呼ぶ mruby のアプリ。終了後に端末から消した) を、起動して
5 秒後に `/app/kill` で閉じる。

| 対象 | 読み込み | 前 (develop + 数え方、2 周) | 後 (T1 + 数え方、3 周) |
|---|---|---|---|
| Shell (+ irb = Sandbox) | バイトコード | 1 周目 220 回、2 周目 168 回 (どちらも途中で強制終了) | 0 回 (3 周とも通常終了) |
| MML | ソース | 0 回 | 0 回 |
| BlockGame (ゲーム) | ソース | 0 回 | 0 回 |
| Monitor | バイトコード | 61 回 / 周 | 0 回 |
| FM-Editor (Spinel) | (mruby の irep なし) | 0 回 | 0 回 |
| Spinel Hello (Spinel の gem を呼ぶ mruby のアプリ) | ソース | 0 回 | 0 回 |
| 試験用のユーザのアプリ (`eval` + `Sandbox`) | ソース | 0 回 | 0 回 (画面に `eval=12 sb=42`) |
| 10 秒ごとの累計 | | 510 回 (2 周後) | **0 回** (3 周後) |

- Spinel だけで書かれたアプリはエディタしか無い (デスクトップは標準構成で mruby)。「Spinel のアプリ 1 本」は Spinel の
  gem を呼ぶ Spinel Hello で代えた。
- 前の Shell の数が周ごとに違うのは、検査つきの解放が遅く、全部終わる前に強制終了で打ち切られたため。

## プールの戻り

アプリを起動するときの `M1|spawn:<名前>` 行 (その時点の内蔵 RAM と PSRAM の空き) を、周ごとに比べた。

| | 1 周目 | 2 周目 | 3 周目 |
|---|---|---|---|
| T1 + 数え方: PSRAM の空き (Shell の起動時) | 11,228,464 | 11,226,544 | 11,226,544 |
| 同: 各アプリの起動時 (2 周目以降) | | 全部 11,226,544 | 全部 11,226,544 |
| T2 の最終ビルド: 各アプリの起動時 | Shell 11,228,468、Monitor 以降 11,226,548 | 全部 11,226,548 | |

1 周目の最初の数個だけ 1,920 B 少ないのは、Monitor が最初に開いたときに常駐側に残す分 (以後は変わらない)。内蔵 RAM の
空きも同じアプリなら周ごとに数百バイトの幅で揃っている。irep を解放しなくなったことで漏れは出ていない (irep はアプリの
プールの中にあり、プールはアプリごと消える)。

## `mrb_close` を試した結果 (コミットしていない)

T1 + 数え方の上で、`destroy_vm` の通常終了の経路 (強制終了でない方) で `mrb_close(ctx->mrb)` を呼んだ。1 周 (7 本)。

| 対象 | 落ちたか | `mrb_close` 中の不正な解放 | `mrb_close` にかかった時間 (検査つき) |
|---|---|---|---|
| Shell | 落ちない | 0 回 | 1,528 ms |
| MML | 落ちない | 0 回 | 265 ms |
| BlockGame | 落ちない | 0 回 | 569 ms |
| Monitor | 落ちない | 0 回 | 273 ms |
| Spinel Hello | 落ちない | 0 回 | 149 ms |
| 試験用のユーザのアプリ | 落ちない | 0 回 | 152 ms |

- 「`mrc_irep_free` の後に `mrb_close` を呼ぶと落ちる」という注記どおりで、`mrc_irep_free` が無くなれば `mrb_close` は
  通る。バイトコードの irep も、コンパイラ製の irep も、`mrb_close` の掃除で問題なく消えた。
- 時間はこの計測ビルドでは `ESTALLOC_DEBUG` の検査 (解放ごとの走査) 込みなので、T2 の後ならずっと短いはず (測っていない)。
  一方で、プールごと捨てる今の作りなら終了時の手間は 0 なので、呼ぶ利点は「終了処理 (ファイナライザ相当) が走る」ことに
  限られる。指示書どおり、この段階では変えていない。

## T2: 統計だけを残す

T1 をコミットしてから S2b の `change.diff` と `estalloc.patch.c` を当てた (S2b の報告の「分け方と理由」と同じ。差分なし)。

### 打鍵と GC (P4-Nano、計測ビルド = T2 + S2b の計装、`FMRB_GC_PROFILE=1`)

計測の台本は S2b と同じ ((a) Shell だけ 192 打鍵、(b) MML・Breakout.py・Monitor を足して 192 打鍵、(c) BlockGame に替えて
5 分間、(d) FM-Editor で 192 打鍵)。B0・B2 は S2b の値 (B0 = develop、B2 = S2b の変更、ただし T1 なし)。

| | B0 (develop) | B2 (S2b) | T2 (本書) |
|---|---|---|---|
| (a) 50 ms 以上 | 99/192 (52%) | 4/192 (2%) | 4/192 (2%) |
| (a) 最大 | 418 ms | 196 ms | 193 ms |
| (a) 50 ms 未満の平均 | 25.3 ms | 18.9 ms | 16.1 ms |
| (a) Shell の全体 GC | 18 回、平均 213 ms | 18 回、平均 16.2 ms | 16 回、平均 9.9 ms、最大 13.5 ms |
| (b) 50 ms 以上 | 183/186 (98%) | 21/192 (11%) | 20/192 (10%) |
| (b) 最大 | 5,144 ms | 217 ms | 323 ms |
| (b) Shell の全体 GC | 28 回、平均 589 ms | 25 回、平均 24.1 ms | 26 回、平均 21.3 ms、最大 51 ms |
| (c) 50 ms 以上 | 225/320 (70%) | 18/320 (6%) | 20/320 (6%) |
| (c) 最大 | 1,821 ms | 248 ms | 273 ms |

(c) の 5 分間の全 VM の GC:

| VM | B0 | B2 | T2 |
|---|---|---|---|
| FM-Shell | 60 回、計 16.6 秒、最大 582 ms | 55 回、計 0.81 秒、最大 112 ms | 59 回、計 0.93 秒、最大 155 ms |
| Monitor | 383 回、計 25.7 秒、最大 625 ms | 426 回、計 4.2 秒、最大 59 ms | 426 回、計 4.2 秒、最大 61 ms |
| BlockGame | 46 回、計 6.1 秒、最大 741 ms | 48 回、計 0.60 秒、最大 43 ms | 41 回、計 0.40 秒、最大 21 ms |
| fmrb_kernel (Spinel) | 20 回、計 107 ms | 24 回、計 34 ms | 22 回、計 34 ms |
| FM-Editor (Spinel、(d)) | 6 回、平均 23.2 ms | 4 回、平均 3.8 ms | 6 回、平均 4.0 ms |

B2 と同じ程度。最大値の揺れ ((b) 323 ms など) は 1 回ずつの外れで、S2b の B1/B2 間の揺れと同じ幅に入る。

### 統計が出ること

- 周期ダンプの VM Pools (Used / Free / Total / Frag) が全 VM 分出る (計測ビルドでも最終ビルドでも)。値の範囲は B0 と同じ
  (例: (b) の Shell 363-368 KB)。
- `FmrbApp.pool_usage` は Shell の SHLAT 行の `pool=` に出ている。Monitor アプリの Memory 画面も各 VM の使用量を出す
  (ブラウザ版でも確認)。
- 最終ビルドの map に `est_sanity_check` が無い (= `ESTALLOC_DEBUG` が入っていない)。

### 起動と終了のくり返し (T2)

- 計測ビルドで (a)-(d) の後に上の 1 周を 3 周、最終ビルドで 2 周。不正な解放は数えようがない (検査ごと外れた) が、
  Shell を含め全部通常の終了で閉じ、落ちたのは下の 1 件だけ。S2b で落ちた「(c) の後に Shell を閉じる」も通った
  ((d) の頭で全部閉じている)。

## 静的な D/IRAM

| ビルド | Used stat D/IRAM |
|---|---|
| NARYAv4 develop (S2b の測定、計装なし) | 125,604 |
| NARYAv4 T2 (計装なし、最終) | **125,604 (増減なし)**。Total image 6,319,916 |
| TAB5 T2 | 130,186 (audio_mute M1 の報告の値と同じ) |
| S3 (NARYAv3) T2 | 126,251 (同上) |

## 回帰の確認

| 対象 | 結果 |
|---|---|
| ビルド NARYAv4 / TAB5 / S3 (NARYAv3) | 通る |
| ビルド Linux 標準 (Spinel カーネル + Spinel エディタ) / 互換 (全部 mruby) | 通る (`file` で x86-64 を確認) |
| ビルド wasm (`wasm:mruby` → `wasm:mrb` → `wasm:core`、`wasm:web`) | 通る |
| `rake test` | 全部通過 (golden 103 passed など) |
| sim 互換構成 | 起動、Shell の打鍵・`ls`・irb (`40+2` → `=> 42`)、エディタの起動と 1 打鍵、Shell・MML・BlockGame・Monitor・エディタ・Spinel Hello の起動と終了を 3 周。落ちない |
| sim 標準構成 | 同じ内容 + エディタを 2 つ同時に開いて閉じ、3 つ目を開く を 3 回。落ちない |
| ブラウザ版 | 起動、Monitor (2 回)・エディタ (1 打鍵)・Shell (`ls`)・BlockGame の起動と終了。落ちない。Monitor の Memory 画面に各 VM の使用量が出る |
| 実機 P4-Nano | 上の T1・T2 の計測。落ちたのは下の 1 件 |
| Retro (S3) 実機 | **未** (実機なし、ビルドだけ) |

ブラウザ版の起動直後に「/app/usr/paint_pad/paint_pad.app.rb: Failed to launch」が出た。ブラウザに残っている /home の
自動起動の設定によるもので、今回の変更とは関係ない (アプリ自体がバンドルに無い)。

## 残件

### Spinel のエディタを 2 つ同時に開いた後の abort (未解決、要判断)

T2 の計測ビルドで、上の台本の (d) がエディタを閉じずに終わり、続く 1 周目でエディタがもう 1 つ開いた (2 つ同時)。
片方を閉じると両方が終わり、その次の周でエディタを開いた瞬間に落ちた。

```
No such file or directory @ rb_sysopen - /home/colors.toml (Errno::ENOENT)
abort() was called at PC 0x400085e5 on core 1
```

`FmrbColors.read` の `File.open` は `rescue` で囲んであるのに、例外が捕まらずに abort した。同じ起動のまま「2 つ開く → 両方
閉じる → 3 つ目を開く」をやると、もう一度同じ abort になった (2/2)。

| ビルド | 「2 つ開く → 閉じる → 3 つ目」の abort |
|---|---|
| T2 の計測ビルド (長く動かした後) | 2 回中 2 回 |
| T1 (検査あり) | 4 回中 0 回。ただし 3 つ目のエディタの ExcHW が 6/0 (1 つだけなら 3/0) で、前のエディタの状態が残っている |
| T2 の最終ビルド (焼いた直後) | 3 回中 0 回 |
| sim 標準構成 (T2) | 3 回中 0 回 |

見立て (確かめていない): Spinel のプログラムの「ファイルの頭に置かれる状態」(クラスの持つ値、オブジェクトの使い回しの
リスト、例外の深さの最大値など) は生成された C の翻訳単位ごとの static で、同じアプリを 2 つ同時に動かすと 2 つのインス
タンスがそれを共有する (Spinel の sp_ctx.h のコメントは、この static が翻訳単位に 1 組だけで、プログラムの入口のたびに消し直される
と書いている)。片方のプールが消えた後も、その中を指す値が static に残りうる。develop では Spinel のインスタンスの終了時に
`est_cleanup` が (`ESTALLOC_DEBUG` のときだけ) プール全体を 0 で埋めていたので、残った値が指す先が 0 になり、壊れ方が
目立たなかった可能性がある。T2 ではその埋めが無くなる。二重解放と同じく「検査が別の不具合を隠していた」形かもしれない。

- エディタを 1 つずつ開いて閉じる使い方では、計測・最終ビルドとも一度も落ちていない。
- 2 つ同時に開くことは、遠隔の `/app/launch` でも、ランチャーを 2 回押しても起きうる (同じアプリの多重起動を止める仕組みは
  Python のアプリにしか無い)。
- 直し方の候補 (どれもやっていない): Spinel のアプリの多重起動を止める、Spinel のインスタンスの終了時にプールを 0 で埋める
  (develop の振る舞いに戻すだけで、元の原因は残る)、Spinel の翻訳単位の static をインスタンスごとに持たせる (Spinel 側の
  変更)。どれにするかは親とユーザの判断。

### その他

- `mrb_close` を呼ぶかどうか (上の結果を材料に別に扱う)。
- Retro (S3) の実機の確認。

## 端末と作業ツリーの状態

- 端末 (P4-Nano) には T2 の最終ビルド (a40c7fa9 相当、NARYAv4、計装なし、`ESTALLOC_DEBUG` なし) を焼いた。ミュートのまま
  (volume 6)。試験用に置いた `/app/test/s2cprobe.app.rb` / `.app.toml` は消した。
- build/ はその NARYAv4 のビルド。wasm/build は T2 のブラウザ版。
- 計装 (fmrb_app.c の `S2CREJ`、S2b の計装、submodule の estalloc.c への数え方) はすべて戻した。`.env` は触っていない。

## 残したもの (scratchpad の `s2c/`、コミットしていない)

`cycle.sh` (起動・終了の 1 周)、`dbl_editor.sh` (エディタ 2 つの試験)、`s2cprobe.app.rb` / `.toml` (試験用のアプリ)、
`after.log` (T1 の 3 周)、`close.log` と `close_try.diff` (`mrb_close` の試行)、`T2.log` と `runs_T2.txt` (T2 の計測)、
`T2meas.elf` (abort を解いた ELF)、`final.log` (最終ビルドの確認)、`t2meas_instr.diff` (計測ビルドの計装)、各ビルドのログ。
