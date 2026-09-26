# 報告 P2a: フォークを最新上流へ載せ直す (フォーク側)

> 状態: 完了 | 更新: 2026-09-26 | `fmrb-next` (43 本) を上流 `01521b1e` の上に載せた。`make test` / `make bench` は基点と同数、`make test-multi-ctx` 全通過。**移し漏れは nm の門で 0** (残る 203 個は全部理由つきで分類)。MC アーカイブに残っていた大域 307 個のうち、多重インスタンスで壊れる性質のものを `sp_ctx` に移し (名前マクロ 102 個)、プロセス全体の診断表などを外して 203 個にした。fmrb の 6 本は C 生成・MC コンパイル・ポート構成のリンク検査まで通る。P2b の大きな論点は **flash (+約 0.95 MB) と内蔵 RAM の `.data` (+約 47 KB)**、それと FFI の `:binstr` 長さ変数の改名

## 前提

| 項目 | 値 |
|---|---|
| 作業 clone | `/home/kishima/fmrb/wt/spinel-rebase/` (`git clone https://github.com/matz/spinel.git`)。フォークは `fmruby-core/vendor/spinel` から `git fetch` した `fmrb-dev` = `622750c` をローカルの `fork` ブランチに置いた |
| 新ブランチ | `fmrb-next` = 上流 `01521b1e` + 43 本 (最終 `b259d0a3`)。**push していない** |
| 基点の比較用 | 同じ clone の worktree に `01521b1e` を置いてビルド (`$S/base`) |
| ツールチェーン | `cc (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0`、`ruby 3.2.6`、24 CPU (作業中の負荷平均 20-35) |
| 触っていないもの | `~/dev/spinel`、`vendor/spinel`、`wt/spinel-pr/`、fmruby-core / graphics-audio の作業ツリー、build/、sim、実機 |
| 作業場所 | `$S = /tmp/claude-1000/-home-kishima-fmrb-family-mruby/a0ea00a0-6754-4693-ae2d-f05782a9784d/scratchpad/spinel_p2a/` |

clone に残した補助ブランチ: `fork` (フォークの `fmrb-dev`)、`p2a-carried` / `p2a-final` (途中の検証点。消してよい)。
worktree: `$S/base` (`01521b1e`)、`$S/wt_carried`、`$S/wt_final`。

## 変化点の要約 (先に読む)

1. **34 本の予定のうち 33 本を載せ、1 本は空になって落ちた** (`8a298cb`: 上流が同じ所を
   `emit_str_expr` / `emit_path_expr` に直していた)。その直後の `286de9b` は手で載せた。
   載せ直しで形が大きく変わったのは 4 本: 多重インスタンスの T4-0 step 2 (`e0fd181`、
   `sp_runtime.h` が `spinel_rt.h` に変わり中身の多くが `lib/*.c` へ移った)、VFS
   (`e2497db`、上流の File.open が open(2)+fdopen になったので **fopencookie で包む形に
   設計し直した**)、スタック一時領域 (`d0f0232`、行の当て直しではなく `-fstack-usage` で
   測り直した)、境界検査と高水位 (`7b1feb7`、境界検査は上流にあるので高水位だけ残した)。
2. **大域状態の棚卸しが本題だった**。上流 `01521b1e` の `libspinel_rt.a` の data/bss は
   377 個。MC アーカイブで数えると、フォークの 33 本を載せた直後で 307 個が残っていた。
   読んで分類し、**インスタンスごとであるべきものを `sp_ctx` に移した** (名前マクロで 102 個。世代別 GC の印の世代番号、minor/full の状態、
   remembered/pinned 集合、文字列の旧世代、文字列長キャッシュ、PolyArray の空きプール、
   トップレベル self、ARGV キャッシュ、正規表現のエラー処理先、Marshal の表 ほか)。
   残る 203 個は理由を付けて許可表に載せ、**許可表に無い大域が現れたら落ちる門**
   (`make check-mc-globals`) を足した。
3. **棚卸しの答え合わせとして、4 インスタンスの同時実行を試した**。同じ Ruby を別の
   入口名で 4 回コンパイルして 4 つの TU にし (1 プログラム 1 インスタンスの約束を守るため)、
   1 バイナリで同時に走らせた。`test/gc_*.rb` は実行できた 16 本中 15 本、コーパスの
   抜き取り 333 本は実行できた 288 本中 285 本が CRuby と一致。残りは試験側の事情
   (根スタック溢れを意図的に起こす試験、改行の無い print の混ざり、別 C のリンクが要る
   FFI 試験) で、共有状態の不具合は出なかった。
4. **ポートで使う口を増やした**: `SP_NO_SLAB` (slab を外す。MC と `SP_NO_MMAN` で自動)、
   `SP_NO_ALLOC_REPORT` (静的表 192 KB を外す。同上)、`SP_NO_PROCESS` (fork/exec を使う
   Process.spawn / system / バッククォートを例外に。`SP_NO_MMAN` で自動)、IO::Buffer.map の
   `SP_NO_MMAN` 対応、`lib/sp_nosched.c` (fiber / sched を外したポートで参照が解決する
   ように)、`SP_GC_MARK_STACK_LIMIT`、remembered / pinned 集合の大きさの設定。
   **既定のビルドの runtime オブジェクトは全部バイト単位で変わらない** (確認方法は 4 節)。
5. **fmrb の生成手順はそのまま通る** (オプションは今の rakelib/spinel.rake と同じ)。
   kernel / desktop / editor / fft / raycast / hello の 6 本とも C 生成と MC コンパイル
   (64bit、32bit+ポートの口) が通り、ポート構成の runtime と `--gc-sections` でリンクすると
   未解決は各プログラムの FFI 関数 (fmrb 側が定義するもの) だけになった。fmrb 側で要る変更は
   **`sp_net_bin_len` → 上流の `sp_ffi_bin_len` (MC では `*sp_ctx_ffi_bin_len()`)** の
   置き換えなど (6 節)。

## 1. 載せたコミットの対応表

「衝突」は cherry-pick (`-X find-renames=30%`) で衝突したもの。解き方は各コミットの本文に
"Rebased onto upstream 01521b1e: ..." の段落で英語で書いた (新しいコミットは元の件名のまま)。

| # | 元 | 新 | 件名 (略) | 衝突 | 解き方の要約 |
|---|---|---|---|---|---|
| 1 | `9aa7cdd` | `23a4ccfb` | library mode (--no-main / --entry / --inject) | あり | 上流の `_sp_main_body` + トランポリン main と `--ext-init` の隣に第 3 の入口として実装。呼び出し側のスタックで走る。`sp_tu_init` (上流の改名)、at_exit の終了状態を返す。`--ext-init` との併用を拒否 |
| 2 | `93b6981` | `d8e4ac0f` | docs: multi-instance 設計 | なし | |
| 3 | `1fea726` | `266168b8` | sp_ctx の骨組み + alloc/GC 状態 | あり | 元の対象の記号だけ守る。根スタックは上流の溢れ用区画 (`sp_gc_roots_ext`) を MC では使わない (1 区画) |
| 4 | `8465dc1` | `b900da56` | regexp + RNG 状態 | あり | 既定の Random は上流が 0xfd の見張りバイト付きの箱にした。ctx にも同じ箱 (`sp_Random_box`) を持ち、生成時に見張りを置く |
| 5 | `876c0a5` | `8f4a1af8` | 値の内省 vtable | あり | 12 個 → 26 個に増えていた。26 個全部を移した |
| 6 | `0fa1a47` | `ac588419` | TU の hook 初期化 | あり | `sp_tu_ctx_init` を 3 つの入口形式の先頭に出す。上流の sp_alloc.c のコンストラクタは 4 つを入れるので、MC では `sp_alloc_instance_init` として instance 生成時に走らせる |
| 7-8 | `3394a85` `e7682c2` | `5e75b626` `1988e341` | docs / .gitignore | なし | |
| 9 | `6307545` | `08df3ddf` | 確保の付け替え + nm の門 | あり | `<malloc.h>` の判定が上流で `__GLIBC__` になった所に MC の除外を足す |
| 10 | `8e2f553` | `31b60763` | estalloc 試験 + 根溢れで止める | あり | 止める処理を MC の遅い経路 (`sp_gc_root_push_slow`) へ移し、インラインの push は両ビルドで同じに |
| 11 | `7063b35` | `f924af0c` | docs | なし | |
| 12 | `3d9774a` | `19a36f3f` | T4-0 step 1 (TU データ) | あり | 上流が ext ホスト用に `#ifdef SPINEL_EXT_HOST extern` を置いたので、三通り (`MC` / `EXT_HOST` / 既定) の `#if` に。proc 引数の副チャネルは 64 本に増えていた |
| 13 | `e0fd181` | `985103ba` | T4-0 step 2 (TU 関数を instance 経由) | あり (大) | spinel_rt.h は上流を取り、方式を当て直した: 20 関数 (+TU 専用 2) を `SP_TU_STATIC`、`#undef` ブロックと static 前方宣言は `sp_sched.h` の include 直後、`sp_tu_ctx_init` で登録 |
| 14-15 | `fa2b38a` `184ff36` | `e06037a0` `39a176d4` | docs / コメント | なし | |
| 16 | `9474d92` | `4f400431` | 演算代入の poly 引数 | あり | 局所変数の腕は上流 (#2875) が直していた。ivar/cvar/定数/大域の腕だけ残る。P0 の再現は CRuby と一致 |
| - | `8a298cb` | (空) | sprintf / File.open の poly 引数 | あり | 上流が同じ所を直していて空になったので落とした |
| 17 | `286de9b` | `8a43fc21` | sprintf の書式式を先に出す | (手で) | 前が空になったので上流の形 (書式の一時変数を根にする) に手で当てた。P0 の再現 (01521b1e ではコンパイル不能) が通る |
| 18 | `e2497db` | `4e3b4e92` | VFS (File/Dir を ctx の io_* へ) | あり (大) | **設計を変えた**: バックエンドの handle を `fopencookie` で stdio の FILE に包み、File.open と flags 形式だけを io_open に通す。以後の handle 操作は上流の stdio のまま (gets_sep / getc / readlines も通るようになった)。Dir は元どおり。Dir#rewind/tell/seek/fileno・Dir.for_fd・開いた handle の #entries は MC で NotImplementedError。io_* の約束は不変 |
| 19 | `a03386b` | `b89ea74a` | `<sys/mman.h>` を SP_NO_MMAN で外す | あり | flatten の深さの件は上流が書き直していて不要。include の guard だけ残る |
| 20 | `73a2083` | `2e8632bf` | make test32 | あり | 上流の 32bit 対応に合わせて書き直し (2 節末の表) |
| 21 | `53941f6` | `a2ffadf5` | FFI 可変長引数の幅 | あり | `sp_int` に。上流が `test/ffi_variadic.rb` に付けた `# spinel: int64` を外した (32bit で走らせるのが目的の修正なので) |
| 22 | `4e33a00` | `792a7e2e` | MMU 無し / newlib | あり | execinfo と ctype.h は上流済みで落とした。lstat→stat、ucontext の代用、sys/ioctl.h の判定が残る (**載せた直後に backtrace-test が落ち、自動併合された sp_bt_format の `#if SP_BT_AVAILABLE` が原因と分かって除いた**。7 節) |
| 23-24 | `5221ee9` `ac03888` | `990a8e58` `2c234f8d` | TIOCGWINSZ / fiber の根 | 23 なし、24 あり | 呼び出し位置の移動のみ |
| 25 | `d0f0232` | `36911a5b` | SP_STACK_SCRATCH_MAX | あり (大) | 上流を取り、`-m32 -O2 -fstack-usage -DSP_STACK_SCRATCH_MAX=512` で測り直して当てた (4 節) |
| 26-27 | `b8e5a02` `94c2f89` | `647fdeb4` `e0e4ca2f` | 例外/catch 段数、GC mark 作業リスト | 26 なし、27 あり | 27: 上流は作業リストが満杯になると realloc で倍にする方式になった。マクロは「初期の大きさ」の意味になる |
| 28 | `7b1feb7` | `611bf7cc` | 境界検査と高水位 | あり | 境界検査は上流 (`7cc36f38`) にあるので codegen の変更は全部落とし、高水位の記録だけを上流の検査関数の中に MC 限定で入れた |
| 29 | `c7de66c` | `cb77a468` | 再起動時に TU の static を消す | あり | 上流の印付け一覧が cvar / 動的シンボル / 共有 bignum リテラルまで増えたので、消す一覧も揃えた |
| 30 | `cafe659` | `921910c5` | --persistent-statics | あり | 入口の頭の出力を 1 関数 (`emit_tu_ctx_init`) にまとめて 3 形式で共有 |
| 31 | `16333bf` | `3871fd05` | SP_TU_BSS | あり | 対象は 15 本 (上流が再帰防止の深さの配列を 2 本足した) + `sp_dyn_syms` |
| 32 | `ca0709c` | `625c94a9` | 親子の ivar 型 | あり | 同じ 2 箇所を上流の analyze.c に手で当てた。P0 の再現 (01521b1e で TypeError) が CRuby と一致 |
| 33 | `622750c` | `1fe10970` | struct 前提の検査 | なし | |

新しく足したコミット (すべて本文は英語、Co-Authored-By 付き):

| # | 新 | 件名 | 内容 |
|---|---|---|---|
| 34 | `93dac1dc` | runtime: SP_NO_SLAB | slab をコンパイル時に外す口 (4 節) |
| 35 | `e26680a1` | runtime: compile the SP_MULTI_CTX build on upstream's layout | 上流が lib へ移した先の宣言を MC で外す。`sp_mem_override.h` で `_GNU_SOURCE` |
| 36 | `31aef4fd` | runtime: move the state upstream added since the fork point into sp_ctx | 棚卸しの本体 (2 節) |
| 37 | `6428f673` | test: a leak gate for process-wide state | `check_globals.sh` と許可表。smoke / estalloc の member 一覧を Makefile から読む |
| 38 | `283cc30a` | runtime: port knobs for upstream's mmap and fork/exec users | IO::Buffer.map、SP_NO_PROCESS |
| 39 | `67c15744` | runtime: let a port bound the GC mark stack's growth | `SP_GC_MARK_STACK_LIMIT`、MC で伸長失敗を死なせない |
| 40 | `49f38bbf` | codegen, runtime: the FFI side channels under SP_MULTI_CTX | 生成 C の `extern` を MC で外す。`sp_ctx_ffi_bin_len()` |
| 41 | `f9cc21ef` | runtime: stand-ins for sp_fiber.c / sp_sched.c on an SP_NO_MMAN port | `lib/sp_nosched.c` |
| 42 | `512150c2` | docs: multi-instance -- the rebase onto upstream 01521b1e | 設計文書に今回の分を追記 |
| 43 | `b259d0a3` | test: prune skip32.txt against upstream's int64 markers | 旧 25 件のうち 16 件は上流の印と重複、7 件は 32bit で通るので外した。残すのは上流が印を付け忘れた 5 件 |

衝突の量: 載せた 33 本中、衝突は 23 本 (空になった `8a298cb` を含めると 24 本)。
P0 の試し rebase の見積もり 21 本より多いのは、試しでは `-X theirs` で先へ進めていたため。中間のコミットでは MC ビルドが通らない
(35-36 で戻る)。既定ビルドは各段で `make` が通ることを確かめた。

## 2. 大域状態の棚卸し (中心の成果物)

### 数え方

```sh
make lib/libspinel_rt_mc.a
nm lib/libspinel_rt_mc.a | awk '/:$/ {obj=...} NF==3 && $2 ~ /^[BbDdVSs]$/'   # 実体は test/multi_ctx/check_globals.sh
```

| 時点 | 既定の `libspinel_rt.a` | MC の `libspinel_rt_mc.a` |
|---|---|---|
| フォーク基点 `8c70d565` | 109 | - |
| 上流 `01521b1e` | 377 | - |
| フォーク 33 本を載せた直後 (+ MC がコンパイルできる所まで) | 377 | 307 |
| 最終 (`fmrb-next`) | 377 (既定は変えていない) | **203、全部分類済み** |

### sp_ctx へ移したもの

`sp_ctx.h` の名前マクロは 75 個 → 190 個 (今回の棚卸しで 102 個、うち TU 由来のデータ 7 個。ほかに関数経由で 2 つ)。MC アーカイブの大域は 307 → 203 (下の「移さずに外したもの」を含めた差)。

| 群 | 記号 (元の置き場) | なぜインスタンスごとか |
|---|---|---|
| 世代別 GC の周期 | `sp_gc_mark_gen` `sp_gc_minor` `sp_gc_age_survivors` `sp_gc_str_minor_only` `sp_gc_root_phase` `sp_gc_sweep_full_now` `sp_gc_young_probe_on/hit` (sp_gc.c) | **印の判定そのもの**。A の GC が世代番号を進めると、印付け中の B の印が全部無効になる |
| 印付けの計数 | `sp_gc_mk_bytes` `_young_bytes` `_str_bytes` `_str_young_bytes` `_promo_bytes`、`sp_gc_mkl_*` 6 個 (元は SP_TLS) | 予算の再計算と文字列旧世代の総量に使う。混ざると閾値が狂う |
| 印付けスタックの容量 | `sp_gc_mark_cap` | スタック本体はフォークの時点で ctx だったのに容量が大域になっていた。**A の容量で B のスタックに書く = 範囲外書き込み** |
| full の間隔の調整 | `sp_gc_minors_since_full` `sp_gc_last_per_minor` `sp_gc_fulls_at_min` `sp_gc_full_interval` `sp_gc_old_live` `sp_gc_npromoted` `sp_gc_young_kept_bytes` | ヒープごとの履歴 |
| remembered / pinned 集合 | `sp_gc_remembered[65536]` `sp_gc_pinned[16384]` と個数・溢れ・最大 | 中身は自分のヒープの物。**配列はインスタンスごとの確保にし、大きさを設定で決める** (既定 1024 / 256。溢れても次の回収が全体を印付けするので正しさは保たれる) |
| 検証用 | `sp_gc_verify_probe*` `sp_gc_verify_gen_fail` `sp_gc_vg_*` | SPINEL_GC_VERIFY_GEN の作業領域 |
| GC.stat | `sp_gc_stat_collections/fulls/seconds` `sp_gc_full_runs` `sp_gc_rem_peak` `sp_gc_parked_acc` `sp_gc_ct_swept/marked` | インスタンスの統計 |
| 文字列の世代 | `sp_str_old` `_bytes` `sp_str_old_threshold(_init)` `sp_str_major_interval` `sp_str_major_forced` `sp_str_sweep_cycle` `sp_str_old_slab_bytes` `sp_str_gate_before/old` `sp_gc_str_majors` `sp_gc_obj_alpha1024` `sp_gc_stress_pin` `sp_str_vcand*` (sp_alloc.c) | ヒープごと |
| 文字列長キャッシュ | `sp_str_lcache[]` (SP_TLS、64bit で 3 KB) | ポインタをキーにした文字数のキャッシュ。別ヒープの同じ番地で誤った長さが返る |
| PolyArray の空きプール | `sp_polyarr_pool_head/count` | **他のインスタンスのヒープの物を配る** |
| FFI・深い戻り値の副チャネル | `sp_ffi_bin_len` `_sp_ret_strbuf` | 呼び出しごとの受け渡し。別スレッドのインスタンスと競合 |
| プログラム全体の物 | `sp_main_obj` (トップレベル self) `sp_argv_array_cache` `sp_class_frozen_map[4096]` (初回の freeze で確保) `sp_convert_soft/failed` `sp_glob_dotmatch` `sp_user_to_io_hook` `sp_warn_flags` `sp_bt_enabled/srcfile` (sp_cold.c) | ヒープの物を指す、またはプログラムごとの設定 |
| inspect の再帰防止 | `sp_poly_recur_*` 7 個 (sp_inspect.c) | 呼び出しの連鎖ごと (ヒープ上の配列) |
| 例外の hook | `sp_user_exc_modules_fn` (sp_exc.c)、`sp_stack_overflow_raise_fn` (sp_fiber.c) | プログラムごとの関数 |
| 正規表現 | `sp_re_pp_span` `sp_re_startup_err` (sp_re.c)、`sp_re_error_handler` (re_compile.c) | エラー処理先は各プログラムの例外スタックへ longjmp する。**別プログラムのものが呼ばれると他人の jmp_buf へ飛ぶ**。re_compile.c は sp_ctx.h を読めないので sp_ctx.c の関数経由 |
| `$?` | `sp_last_status` (sp_system.c) | sp_system.c も sp_ctx.h を読まないので関数経由 (`sp_ctx_last_status()`) |
| Marshal | `sp_marshal_v` (vtable)、`sp_mar_active` | **フォークでは共有のまま残していた** (「同時には使われない」)。上流では vtable に per-program の関数が増えたので移した |
| TU が定義し lib が読むデータ | `sp_argv` `sp_argf_obj` `sp_pending_exc_recv/key/val/flags` `sp_user_exc_parent_fn` | 2 本の TU を 1 つにリンクすると重複定義。ctx へ |
| TU だけが読むデータ | `sp_callee_name` `sp_exc_subclass_ids/count` | MC では TU 内 static に |

### 移さずに外したもの

| 物 | 大きさ (32bit) | 扱い |
|---|---|---|
| 確保の報告 (SPINEL_ALLOC_REPORT) の表 | `sp_alloc_stats` 192 KB + 名前表 4 KB | `SP_NO_ALLOC_REPORT` (MC と `SP_NO_MMAN` で自動) で外す。プロセス全体の表はインスタンスを混ぜるうえ、ポートでは静的 RAM の丸損 |
| [gcph] の旧世代の形の記録 | `sp_str_shape` 32 KB | MC では外す (診断出力の 1 行が出なくなるだけ) |
| slab | 4 KB ほど + mmap 予約 | `SP_NO_SLAB` (MC と `SP_NO_MMAN` で自動) |
| SIGSEGV によるスタック溢れの検出 | 代替スタック 64 KB を最初のインスタンスのプールから取っていた | MC と `SP_NO_MMAN` で `sp_stack_guard_init` を何もしない関数に |
| Time#strftime の静的バッファ | `out[8192]` | MC では呼び出しごとの確保 |

### 共有のまま残したもの (203 個、許可表 `test/multi_ctx/globals_allow.txt`)

| 分類 | 個数 | 中身 |
|---|---|---|
| PORTOUT | 80 | fiber / sched / net / crypto の中の状態 (ポートでは組まない。MC のインスタンスでは未対応) |
| DIAG | 49 | `sp_gc_ph_*` (SPINEL_GC_PHASES の計時)、統計行の作業領域。判断には使わない |
| RO | 25 | const な表、一度だけ同じ値で埋める表 (1 文字文字列の表、mpz の shim 文脈、C ロケール) |
| NOSLAB | 18 | slab (MC では外してあるので書かれない) |
| CONFIG | 10 | 環境変数から起動時に 1 回だけ読む設定 (SPINEL_GC_MINOR など) |
| THREADS | 8 | SP_THREADS 専用の並列回収の状態 (単一スレッドのインスタンスでは初期値のまま) |
| PROCESS | 6 | 現在のインスタンスを指す `__thread`、標準入出力の静的 handle、ARGF の行バッファ |
| SCRATCH | 5 | 例外経路の文面用の作業領域 (同時に使うと文面が崩れうるが、ヒープは壊れない) |
| SAME | 2 | どのインスタンスも同じ関数を入れる hook |

### TU ごと (1 プログラム 1 インスタンスの約束で分離されるもの)

生成 TU の static (例外/catch スタック、`sp_bt_buf`、`sp_dyn_syms`、正規表現リテラルの
コンパイル結果、例外の途中状態 `sp_pending_exc_obj` `sp_inflight_cause` など)。
設計文書の約束どおり、**同じプログラムを 2 インスタンスで走らせると壊れる**
(下の並列試験の参考行で、1 つの TU を 4 インスタンスで共有させると 3 本が落ちた)。例外の途中状態は、インスタンスが例外の
最中に殺された後で同じプログラムを再起動すると残る (`c7de66c` の消去一覧に入っていない)。
fmrb で起きるかは P2b で見る。

### 両立しないと分かった所は無い (止まる条件には当たらない)

`SP_THREADS` の TLS やスケジューラの状態は、MC とは `#error` で排他のままで、
MC の単一スレッドのインスタンスでは初期値から動かない (THREADS / PORTOUT に分類)。
上流の設計が `sp_ctx` の考え方と衝突した所は無かった。ただし**量が多い**。上流は
大域状態を増やし続けるので、次の rebase でも同じ規模の棚卸しが要る。門
(`check-mc-globals`) はその作業を「新しい記号の分類」に縮めるためのもの。

### 並列試験 (棚卸しの答え合わせ)

`make test-multi-ctx` の smoke は同じプログラムを 4 スレッドで走らせる (単純なので通る)。
それとは別に、`$S/mcstress/` で**同じ Ruby を別の入口名で 4 回コンパイルして 4 つの TU に
し、1 バイナリで 4 インスタンスを同時に走らせる**試験を作った (出力行の多重集合を
`.expected` の 4 倍と比べる)。

| 対象 | 結果 |
|---|---|
| `test/gc_*.rb` 26 本 | 実行 16 本: 15 本一致、1 本 (`gc_roots_beyond_array`) は根スタック溢れを意図的に起こす試験で、MC の約束どおり止まる。残り 10 本はスレッド・ファイル等を使うので対象外 |
| 同じく 1 本の TU を 4 インスタンスで共有した場合 (参考) | `gc_minor_byref_lent_slot` `gc_poly_local_setjmp` `gc_str_major_interval` が落ちる。1 インスタンスなら通る。TU の static 共有による (設計の約束の外) |
| コーパス 333 本の抜き取り (12 本に 1 本) | 285 本が 4 並列で一致。45 本は対象外 (スレッド・ファイル・ARGV 等)。2 本は試験用の C を別にリンクする FFI 試験でリンクできず対象外。1 本 (`poly_io_handle_methods`) は改行の無い `print` が 4 スレッドの間で混ざっただけ (行の中身が `3one` / `one3` に入れ替わる) で、実行時の不具合ではない |
| `SPINEL_GC_STRESS=1` / `SPINEL_GC_VERIFY_GEN=1` / `SPINEL_GC_VERIFY=1` での smoke と link2 | 通過 |

## 3. ゲートの結果

基点 = 上流 `01521b1e` を同じホストで同じ手順。最終 = `fmrb-next`。

| ゲート | 基点 | 最終 | 差 |
|---|---|---|---|
| `make` | 通過 | 通過 | 上流は今は自己ホストではない (自己ホスト版は `self-host` ブランチ)。「自己ホスト一致」の検査は無く、ビルドが通ることだけ見た |
| `make test` (コーパス) | 4054 pass / 1 fail | 4054 pass / 1 fail | 差なし。落ちる 1 本 `systemcallerror_hierarchy` は両方とも同じ (`TCPSocket.new("127.0.0.1", 1)` がこのホストで 10 秒の時間切れ。基点の実行ファイルを直接走らせても止まる) |
| `make test` の C 側の脚 (rbs / reject / cli-opts / backtrace / gc-* / ext / ext-cruby ほか) | 全 pass | 全 pass | 途中で backtrace-test が落ちたのを直した (7 節) |
| `make bench` | 62 pass / 0 fail | 62 pass / 0 fail | 差なし |
| `make test-lib-mode` | (無し) | PASS | |
| `make test-multi-ctx` | (無し) | 全 PASS | nm の門 (libc 確保)、**大域の門 203 個全分類**、smoke 4 並列、link2 (2 プログラム 1 バイナリ)、estalloc (分離・枯渇・根溢れ)、ASan |
| `make check-stack` | (無し) | PASS | `SP_STACK_SCRATCH_MAX=512` で 1 KB 超の枠は許可表の 3 つだけ (4 節) |
| `make test32` | 3904 pass / 8 fail (3912 本、同じスクリプトを基点に当てた) | **3916 pass / 1 fail (3917 本)** | 内訳は下。落ちる 1 本は 64bit と同じ時間切れ |
| ポートの口を全部付けたコンパイル | - | 通過 | `-m32 -DSP_MULTI_CTX -DSP_NO_MMAN -DSP_STACK_SCRATCH_MAX=512`、`<sys/mman.h>` と `<ucontext.h>` を `#error` にした見せかけのヘッダを先に置いて、sp_fiber.c (ポートで外す) 以外の全 runtime が通る |

### make test32 の内訳

上流は `make test-corpus CC='cc -m32'` を持つが、このホストは i386 の libcrypt が無く
`bin/spinel` のリンクで落ちる (P0 と同じ)。フォークの `scripts/test32.sh` を上流の形に
書き直して使った: runtime の member・regexp・同梱パッケージを Makefile から読む、
`spinel --print-build --cc='cc -m32'` が言う材料 (`-ffp-contract=off -D_TIME_BITS=64
-D_FILE_OFFSET_BITS=64 -msse2 -mfpmath=sse`) で組む、スレッドを使うプログラムには 32bit の
スレッド版アーカイブ、`SPINEL_LINK` を守る、上流の `# spinel: int64` の印も除外、
`.expected` の無い試験は CRuby と比べる。

| 項目 | 基点 (同じスクリプト、旧 skip 表) | 最終 |
|---|---|---|
| コーパス (promote_* 除く) | 3992 | 3992 |
| 除外: `# spinel: int64` (上流の印) | 48 | 47 (`ffi_variadic` の印を外した。`53941f6` の目的) |
| 除外: `test/skip32.txt` | 25 (旧フォークの表のまま) | 5 (コミット 43 で整理) |
| 除外: crypt(3) (32bit の libcrypt 無し) | 8 | 8 |
| 実行 | 3912 | 3917 |
| 結果 | 3904 pass / 8 fail | **3916 pass / 1 fail** |

- 基点の 8 fail のうち 4 本 (`gc_threshold_per_heap` `pathname_glob_receiver_prefix`
  `str_range_endpoints_root` `string_param_retained_in_ivar`) は `.expected` の無い試験を
  空の期待値と比べていたスクリプトの不備。CRuby と比べる形に直すと最終では通る。
- 3 本 (`bigint_if_value_temp` `block_given_else_arm_overflow_modes`
  `file_utime_nanoseconds`) は 2^31 を超える値を出す試験で、上流が `# spinel: int64` を
  付け忘れている (基点でも最終でも同じ答え)。最終では skip32.txt に載せた (PR 候補 N-15)。
- 残る 1 本 `systemcallerror_hierarchy` は 64bit と同じ時間切れ (環境)。
- 旧 skip32.txt の 25 件: 16 件は上流の `# spinel: int64` と重複、7 件は 32bit で通るように
  なった (上流の 32bit 対応による)、2 件 (`interp_single_buffer` `pack_endian_modifiers`) は
  64bit の値で答えが違うのに上流の印が無い (N-15 に追加)。
- 旧 skip32.txt の [BUG] 6 件: bignum 3 件 (`bignum_modulo_bit_pow`
  `bignum_recv_int_ord_float_toi` `integer_hash_range_batch9`) と `ffi_variadic` は 32bit で
  走らせて通った。`bignum_receiver_methods` と `random_bignum_seed` は上流が int64 の印を
  付けたので走らせていない。

旧フォークの「1934 本を下回らない」との比較: 旧フォークの test32 は当時のコーパス
(約 2000 本) で 1934 本が通っていた。今回はコーパスが倍になり、3916 本が通る。
比べられるのは本数ではなく「32bit で落ちる不具合が残っていないか」で、残っているのは
上流の印の付け忘れ 5 件だけ。

## 4. 新しく足した口と、既定の出力が変わらないことの確かめ方

| 口 | 既定 | 何をするか | 自動で有効になる条件 |
|---|---|---|---|
| `SP_NO_SLAB` | 未定義 | slab をコンパイルで外す。`sp_slab_on` は 0 で始まり予約をしない。`sp_gc_alloc` などは SPINEL_GC_SLAB=0 と同じ malloc の経路。mmap / madvise / mallopt / jemalloc 検出だけを guard し、塊の処理はコンパイルされるが到達しない | `SP_MULTI_CTX`、`SP_NO_MMAN` |
| `SP_NO_ALLOC_REPORT` | 未定義 | 確保の報告の表と処理を外し、計数関数を空に | `SP_MULTI_CTX`、`SP_NO_MMAN` |
| `SP_NO_PROCESS` | 未定義 | Process.spawn / waitpid2、Kernel#system、バッククォートが NotImplementedError | `SP_NO_MMAN` |
| IO::Buffer.map (`SP_NO_MMAN`) | - | `<sys/mman.h>` を読まず、map は NotImplementedError。ヒープの IO::Buffer はそのまま | - |
| `lib/sp_nosched.c` | 空の TU | `SP_NO_MMAN` で fiber / sched の代わり: GC.start は回収、Thread.pass と記述子の通知は何もしない (他のスレッドも準備集合も無い)、Mutex / Queue のクラス名はそのまま、スレッドや fiber が要る操作は NotImplementedError | - |
| `SP_GC_MARK_STACK_LIMIT` | `1<<28` (上流の上限) | 印付け作業リストの倍々伸長の上限。MC では伸長に `sp_mem_try_realloc` (プール枯渇で NULL を返すだけ) を使い、失敗したら上流どおり再帰に落ちる | - |
| `sp_instance_config.remembered_entries / pinned_entries` | 0 = `SP_MC_REMEMBERED_DEFAULT` 1024 / `SP_MC_PINNED_DEFAULT` 256 | MC の remembered / pinned 集合の大きさ。負の値で無し | - |
| `sp_ctx_ffi_bin_len()` `sp_ctx_last_status()` | - | sp_ctx.h を読めないホスト C から、現在のインスタンスの `:binstr` 長さと `$?` に触る | - |

既定の出力が変わらないことの確かめ方:

- **runtime のオブジェクト**: フォーク 33 本を載せた時点 (`p2a-carried`) と最終で、既定ビルドの
  `build/*.o` 30 個の逆アセンブルと全節の中身を比べて**全部一致**
  (`objdump -d --no-show-raw-insn` と `objdump -s`)。新しい `sp_nosched.o` だけが増えた (空)。
- **生成 C**: コーパス 199 本 (20 本に 1 本) を両方の compiler で `-c --no-line-map` して差分を
  取ると、違いは `sp_exc_subclass_ids/count` の前に付いた `SP_TU_STATIC` (既定では空に展開)
  だけ (比較はコミット 39 の時点)。その後のコミット 40 は生成 C の `extern` 2 種を
  `#ifndef SP_MULTI_CTX` で囲むだけで、既定では前処理後に同じ。
- ゲート (3 節) の件数が基点と同じ。

`SP_STACK_SCRATCH_MAX` の当て直し (`d0f0232`) で測った値 (`-m32 -O2`、budget 512):

| 枠 (測り直し前) | 大きさ | 対処 |
|---|---|---|
| `sp_file_expand_path` | 24,672 | 省く (`SP_HAVE_PATH_HELPERS`) |
| `sp_io_copy_stream` | 8,288 | `SP_SCRATCH` |
| `sp_file_realdirpath` / `sp_file_realpath` | 8,272 / 4,176 | 省く |
| `sp_glob_walk` / `sp_dir_glob_one` / `sp_dir_glob_braces` / `sp_dir_glob_rec` | 6,336 / 4,560 / 2,160 / 1,120 | 省く (glob 一式) |
| `sp_process_open_redirect` | 4,256 | `SP_NO_PROCESS` の stub で消えた |
| `sp_file_readlines(_chomp)` | 4,256 | 省く |
| `sp_readlines` / `sp_gets` (stdin) | 4,240 / 4,176 | 省く (`SP_HAVE_LINE_READERS`) |
| `sp_sprintf` | 4,208 | `SP_SCRATCH` |
| `sp_file_readlink` (新) / `sp_dir_pwd` | 4,176 / 4,176 | 省く |
| `sp_caller_now` | 1,072 | `SP_SCRATCH` (取る段数) |
| `mpz_get_str_dc` | 1,456 | 許可 (元から) |
| `sp_str_format_polyarr` (新) | 1,184 | 許可に追加 (小さなバッファの合計) |
| `sp_raise_cls` (新) | 1,104 | 許可に追加 (例外生成のインライン展開) |

上流は File#gets と ARGF の行読みをヒープのバッファにしていたので、`SP_HAVE_LINE_READERS`
で省くのは stdin の 2 つだけになった。

## 5. 手順 6 の結果 (fmrb の生成手順)

fmruby-core `f453c33` を scratchpad に clone し (`$S/core`、作業ツリーには書いていない)、
rakelib/spinel.rake と同じオプションで生成した (`$S/gen6.sh`)。組み合わせ Ruby は
`tool/spinel/gen_kernel_combined.rb` / `gen_app_combined.rb` を `linux` で。
コンパイルは main/CMakeLists.txt と components/fmrb_spinel_rt/CMakeLists.txt の
フラグに合わせた (`-DSP_MULTI_CTX -include sp_mem_override.h -DSP_GC_STACK_MAX=8192
-DSP_DYN_SYMS_MAX=256 -DSP_EXC_STACK_MAX=16 -DSP_CATCH_STACK_MAX=16
-DSP_GC_MARK_STACK_MAX=8192`、ポートは加えて `-m32 -DSP_NO_MMAN -DSP_STACK_SCRATCH_MAX=512`)。

| 対象 | オプション | C 生成 | MC コンパイル (sim 64bit / ポート 32bit) | ポート構成でのリンク検査 | 備考 |
|---|---|---|---|---|---|
| kernel | `--no-main --entry fmrb_kernel_entry` | 通過 | 通過 / 通過 | 未解決は FFI 29 個のみ | |
| system_desktop | `--no-main --entry system_desktop_entry` | 通過 | 通過 / 通過 | FFI 59 個のみ | 01521b1e では生成 C がコンパイルできなかった (P0)。`286de9b` で直る |
| editor | `--no-main --entry editor_entry` | 通過 | 通過 / 通過 | FFI 63 個のみ | `622750c` の検査が止めない (`ca0709c` が効いている) |
| fft | `--no-main --entry fmrb_fft_spinel_entry --persistent-statics` | 通過 | 通過 / 通過 | FFI 7 個のみ | |
| raycast | `--no-main --entry raycast_entry --persistent-statics` | 通過 | 通過 / 通過 | FFI 10 個のみ | |
| spinel_hello | `--no-main --entry spinel_hello_entry` | 通過 | 通過 / 通過 | FFI 2 個のみ | |

途中で見つけて直したもの (どちらもフォーク側で直した。fmrb 側の変更なしで上の表になる):

1. **生成 C が MC でコンパイルできない**: `:binstr` の FFI を持つプログラム (6 本とも) で、
   compiler が `extern SP_TLS int sp_ffi_bin_len;` を出力し、MC ではそれが ctx のマクロに
   ぶつかる。proc の副チャネルの `extern` も同じ。→ `#ifndef SP_MULTI_CTX` で囲む (コミット 40)。
2. **ポートでリンクできない**: 生成プログラム + ポート構成の runtime (fiber / sched / net /
   crypto 抜き) を `-Wl,--gc-sections` でリンクすると、`sp_Fiber_resume/yield`
   `sp_Mutex/Queue_class_name` `sp_Queue_push` `sp_Thread_join/pass/value`
   `sp_gc_collect_request` (GC.start) `sp_sched_ev_forget` が未解決になった。フォークでは
   同じ検査で未解決は `sp_net_bin_len` (fmrb が定義) だけ。→ `sp_nosched.c` (コミット 41)。
   検査の手順: `$S/linkchk.sh` (ホストの 32bit でリンクし `--unresolved-symbols=report-all`)。

spinel-doctor (`--only unsupported,unresolved`、rake spinel:doctor と同じ脚):

| 対象 | フォーク | 最終 |
|---|---|---|
| kernel | 0 | 0 |
| editor | 0 | 0 |
| system_desktop | 3 (`init` 1、`write_time` 2) | 11 (上に加えて `hit?` 5、`fires_on_press?` 2、`on?` 1) |

増えた 8 件は P0 の N-8 / 台帳 U-13 と同じもの (FmrbUI の Widget 階層)。
`rake spinel:doctor` の許可表 (`write_time` だけ) では落ちる (フォークでも `init` で落ちる)。

## 6. P2b (fmrb 側の取り込み) の指示書に入れるべきこと

### fmrb 側で要る変更

1. **`:binstr` の長さの変数**: 上流の生成 C は `sp_net_bin_len` ではなく runtime の
   `sp_ffi_bin_len` を読む。fmrb の FFI 実装 (main/kernel/fmrb_spx_kernel.c など、
   `sp_net_bin_len = n` と書いている所すべて、gem の native も) を
   `*sp_ctx_ffi_bin_len() = n` に置き換え、`fmrb_spx_common.c` の `sp_net_bin_len` の定義を
   消す。宣言は `int *sp_ctx_ffi_bin_len(void);` (sp_ctx.h にあるが、ホスト C は sp_ctx.h を
   読めないので自前で宣言する)。
2. **`sp_instance_config` の新しい欄**: `remembered_entries` / `pinned_entries`。0 なら
   1024 / 256。fmrb_spinel_host.c で明示するなら、プールの大きさと相談して決める
   (32bit で 1 エントリ 4 バイト)。
3. **FFI の関数名は変わらない**: 生成 C は `sp_ffi_f<N>_<name>` という名前で宣言するが、
   `__asm__` で元の C 名に結びつけるので、fmrb の関数名はそのままでよい。
4. **SPINEL_PIN** を `fmrb-next` (push 後) に、`import_from_fork.rb` で spinel_rt を取り直す。
5. `-ffp-contract=off`: 上流は `--print-build` で常にこれを付ける (Float の丸めを CRuby と
   揃えるため)。fmrb の CMake は付けていない。付けるかどうかを決める (Float を使う gem の
   答え合わせに効く)。

### import_from_fork.rb の除外表

上流の新しいファイルは glob で自動的に入る。今回の口で、ポートでも全部コンパイルできる
形にしたので、除外表に足すものは無い:

| ファイル | ポートで | 理由 |
|---|---|---|
| `sp_slab.c` | 入れる | `sp_gc_alloc` がこのファイルにある。`SP_NO_SLAB` (自動) で mmap を使わない |
| `sp_iobuffer.c` | 入れる | `SP_NO_MMAN` で map を例外に |
| `sp_process.c` / `sp_process_status.c` | 入れる | `SP_NO_PROCESS` (自動) で例外に。status は WIF* マクロだけ |
| `sp_nosched.c` | **入れる (必須)** | fiber / sched を外したポートのリンクに要る |
| `sp_exc.c` `sp_hash.c` `sp_proc.c` `sp_dtoa.c` | 入れる | spinel_rt.h から移ってきた本体 |
| `sp_fiber.c` `sp_sched.c` | ESP では外す (今の CMake のまま) | 変更なし |
| `sp_net.c` `sp_crypto.c` | 外す (今の除外表のまま) | 変更なし |

`components/fmrb_spinel_rt/CMakeLists.txt` の「sp_fiber.c と sp_sched.c を ESP で外す」
処理はそのままでよい (代わりは sp_nosched.c が務める)。newlib で `fopencookie` が使えるかは
未確認 (VFS はこれに依存する。ESP-IDF の newlib には `fopencookie` / `funopen` があるはずだが、
実機ビルドで確かめる)。`sys/poll.h` (`sp_sched.h` が無条件で読む) も同じく未確認。

### RAM と flash の見込み (ポート構成、`-m32 -O2` のホスト計測。Xtensa / RV32 の実数は P2b)

生成プログラム (`size -A`、フォーク → 最終):

| 対象 | .text | .data | .rodata | .bss |
|---|---|---|---|---|
| kernel | 336,939 → 522,364 | **1,284 → 10,452** | 16,096 → 15,544 | 8,194 → 8,389 |
| system_desktop | 950,996 → 1,169,633 | **4,652 → 27,476** | 38,790 → 35,758 | 9,497 → 9,032 |
| editor | 588,480 → 723,958 | **4,572 → 20,732** | 32,119 → 31,054 | 8,669 → 8,672 |
| fft | 42,480 → 55,601 | 16 → 64 | 3,911 → 5,550 | 7,881 → 8,204 |
| raycast | 30,168 → 44,638 | 12 → 64 | 3,686 → 5,258 | 7,841 → 8,136 |
| spinel_hello | 22,898 → 36,128 | 12 → 12 | 3,227 → 4,862 | 7,793 → 8,116 |

runtime (fiber / sched / net / crypto を除く全 .c の合計、フォーク → 最終):

| .text | .data | .rodata | .bss |
|---|---|---|---|
| 340,931 → 652,308 | 56 → 552 | 12,382 → 77,793 | 9,692 → 16,092 |

- **内蔵 RAM (.data)**: kernel + desktop + editor で **約 +47 KB** (凍結リテラルの静的
  オブジェクトと、`SP_INT_NIL` で初期化される Integer 定数)。Tab5 の待機時の内蔵 RAM 空き
  (152,612 バイト) に対して大きい。下の P-11 の見立て。
- **.bss**: 生成プログラムの 8 KB 前後は `SP_TU_BSS` の対象 (PSRAM へ逃がせる)。runtime の
  16 KB のうち 12 KB は `sp_hdr_char_cache` (1 文字文字列の表、ヘッダ付き 2×256) で、
  起動時に同じ値で埋まるが、文字列ハッシュのキャッシュ欄に書かれうるので const にできない。
  残りは slab の worker 表 2 KB (SP_NO_SLAB では未使用) など。gc-sections で消える分は
  実機ビルドで確かめる。
- **flash (.text + .rodata)**: 生成 6 本で約 +580 KB、runtime で約 +377 KB、**合計約 +0.95 MB**。
  S3 (Retro) は flash の残りが 6% と記録されている (memory の project_wifi_s3_enablement)。
  S3 に Spinel kernel を載せ続けるなら入らない可能性が高い。`-Os` での再計測と、
  S3 を mruby kernel に戻すかの判断が P2b に要る。
- インスタンスごとの確保 (プールから): `sp_ctx` 2,756 バイト (フォーク 1,448)、文字列長
  キャッシュ 1,536、remembered 4 KB + pinned 1 KB (既定)、Marshal の表 40。
  class-frozen map 4 KB は Class#freeze を使ったときだけ。

### P-11 (凍結リテラルの `.data`) の見立て

直していない。`SP_TU_BSS` と同じ口を付ける案は**そのままでは効かない見込み**:

- 凍結リテラルは初期値つきの `static struct { sp_str_hdr h; ... } _fzl_N = {...}` で、
  PSRAM に置くなら初期値つきデータを PSRAM に置く仕組みが要る。ESP-IDF の
  `EXT_RAM_BSS_ATTR` は .bss 用で、初期値つき .data を PSRAM に置く属性は無い (要確認)。
- `const` にして flash に置くのが本筋だが、`sp_str_hash_miss` が凍結リテラル (tag 0xf1) の
  ヘッダにハッシュ値を**実行時に書き込む**ので、そのままでは書き込み保護違反になる。
- 案: compiler がリテラルのハッシュ値を生成時に計算してヘッダに入れておけば、実行時の
  書き込みが無くなり `const` にできる (ハッシュ関数は `sp_str_hash_compute`、BINARY の
  扱いまで compiler 側で再現する)。上流にも意味のある変更 (リテラルが .rodata に行く) なので
  PR 候補 N-16 とした。Integer 定数の `SP_INT_NIL` 初期化 (P1 の N-6 追記) は .data に
  残るが、kernel で 128 個 = 512 バイト程度。

### sim / 実機で見るべきこと

- 同じプログラムの再起動 (editor を閉じて開く) で、例外の途中状態の static が残らないか
  (2 節「TU ごと」)。
- GC.start (desktop が呼ぶ) が sp_nosched.c 経由で回収すること (ESP)。
- 凍結リテラルの FrozenError (P0 の注意)。fmrb の Ruby が文字列リテラルを破壊的に変えて
  いれば、上流では実行時に FrozenError になる。

## 7. 見立てと違った点・撤回した仮説

- **「衝突を解けば多重インスタンスは動く」は成り立たなかった**。33 本を載せた直後の MC
  アーカイブには、上流が増やした 307 個の大域が残っていた。そのうちの `sp_gc_mark_gen`
  (世代番号) や `sp_gc_mark_cap` (印付けスタックの容量) は、2 インスタンスが同時に GC すると
  片方のヒープを壊す種類のもの。衝突としては 1 行も出てこない。
- **自動併合を信じて backtrace-test を落とした**。`4e33a00` を載せたとき、フォーク側の
  `#if SP_BT_AVAILABLE` が sp_cold.c の `sp_bt_format` に衝突なしで入った。上流の
  sp_cold.c はこのマクロを定義しないので 0 と評価され、--debug の backtrace が空になった。
  衝突した所 (execinfo の検出) は上流側を取っていたので気づかなかった。全試験を回して
  見つかった。**衝突した箇所で片側を取ったときは、同じコミットの衝突しなかった箇所も
  同じ前提で読み直す必要がある**。
- 「VFS は元の分岐を上流の各関数に当て直す」つもりだったが、上流の File が open(2)+fdopen
  になり、読み書きが sp_io.c / sp_cold.c / spinel_rt.h に散ったので、fopencookie で FILE に
  包む形に変えた。結果として、フォークでは libc のままだった gets(sep) / getc / readlines
  もバックエンドを通る。
- 「MC の中間コミットもビルドできるように載せる」は諦めた。MC ビルドはコミット 35-36 で
  戻る。既定ビルドは各段で通る。
- P0 の「`73a2083` (test32) は上流の -m32 の仕組みへ寄せて落とせる」は、このホストでは
  i386 の libcrypt が無いので成り立たない (指示書どおり残して書き直した)。
- `sp_marshal_v` をフォークは「同時には使われない」として共有にしていたが、上流では
  vtable がプログラムごとの関数を持つので移した。
- `test/multi_ctx/smoke.sh` の ASan 脚は、runtime の member 一覧が古く ASan ビルドのリンクが
  落ちていたのに「ASan が無い」と表示して SKIP していた。一覧を Makefile から読むように直すと、
  今度は SIGSEGV 検出の代替スタックをインスタンスのプールから取っていた問題 (インスタンス
  破棄後に解放済みのメモリを代替スタックとして登録したまま) が ASan で見つかった
  (2 節「移さずに外したもの」)。

## 8. PR 候補

台帳は編集していない。仮 ID は P1 の続き (N-13 から)。確認はすべて上流 `01521b1e`。

| 仮 ID | 内容 | 最小再現 | 見込み |
|---|---|---|---|
| N-13 | 生成 TU が `extern SP_TLS int sp_ffi_bin_len;` / `extern SP_TLS sp_RbVal _sp_proc_poly_args[]` を毎回出す。runtime のヘッダ (sp_alloc.h / sp_proc.h) が同じ宣言を持つので冗長 | 生成 C を読む | 低 (フォーク都合。上流では害が無い) |
| N-14 | SPINEL_GC_SLAB=0 以外に slab を外す手段が無い (コンパイル時の口)。mmap の無い対象でコンパイルできない | `SP_NO_SLAB` の差分 (コミット 34) | 中 (P0 の N-5 と同じ。wasi の分岐と並べて出せる) |
| N-15 | 64bit の値を使うのに `# spinel: int64` の印が無い試験: `bigint_if_value_temp` `block_given_else_arm_overflow_modes` `file_utime_nanoseconds`。32bit の走行で答えが違う | `make test-corpus CC='cc -m32'` (libcrypt があるホストで) | 高 (印を足すだけ) |
| N-16 | 凍結リテラルのハッシュを生成時に計算してヘッダに入れれば、リテラルを `const` (.rodata) にできる。今は `sp_str_hash_miss` が tag 0xf1 のヘッダに書くので .data に置くしかなく、editor で 20 KB 超 (32bit) | 生成 C の `size -A` | 中 (N-6 の具体案。組み込み以外でも .data が減る) |
| N-17 | `sp_gc_collect_request` (GC.start) と `sp_Thread_pass` が sp_sched.c にあるので、スケジューラを外したビルドでは GC.start がリンクできない。単一スレッドの GC.start は `sp_gc_collect()` を呼ぶだけ | ポート構成のリンク検査 (5 節) | 低-中 (上流は sched を外す構成を持たない。wasi は?) |
| N-18 | `sp_stack_guard_init` (SIGSEGV の代替スタック 64 KB を malloc) が、生成 TU の初期化から無条件に呼ばれる。ライブラリとして組み込むホストでは、プロセスのシグナル処理を勝手に入れ替える | 生成 C の `sp_tu_init` | 中 (ext の init でも同じ。ホストの SIGSEGV 処理と衝突) |
| N-19 | `sp_time_strftime` が `static char out[8192]` に描いてから複製する。`SP_THREADS` では競合 (SP_TLS でない) | 読むだけ。スレッド 2 本で同時に strftime | 中 (SP_THREADS の不具合の可能性。未再現) |
| N-20 | `sp_str_shape[8192]` (64 KB) と `sp_alloc_stats` (256 KB) が常に .bss に載る。ホストでは触らなければ実メモリを食わないが、組み込みや wasm では丸ごと載る | `size -A lib/libspinel_rt.a` | 低 (口を足す提案) |

## 9. 確かめられなかったこと

- `make test-corpus CC='cc -m32'` (上流の 32bit の脚) そのもの (i386 libcrypt が無い)。
- ESP-IDF の newlib でのコンパイルとリンク (fopencookie、sys/poll.h、sys/wait.h の WIF*)。
  ホストで `<sys/mman.h>` と `<ucontext.h>` を塞いだ見せかけの検査までは通した。
- fmrb の生成プログラムを実際に走らせること (fmrb のホスト shim が要る。P2b)。
- 並列試験のコーパス全体 (抜き取りのみ)。

## 後片付け

- clone `/home/kishima/fmrb/wt/spinel-rebase/` は `fmrb-next` をチェックアウトした状態。
  補助ブランチ `fork` `p2a-carried` `p2a-final` と worktree 3 つ (`$S/base` `$S/wt_carried`
  `$S/wt_final`) が残っている。worktree は `git worktree remove` で消せる。
- fmruby-core に書いたのはこの報告だけ。
