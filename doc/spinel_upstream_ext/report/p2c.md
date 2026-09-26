# 報告 P2c: 内蔵 RAM の増分をゼロにする

> 状態: 完了 | 更新: 2026-09-26 | **静的な DIRAM は基準 (develop `ac860133` + `vendor/spinel`) に対して S3 -6,736 / P4 -6,812 バイト**。P2b-1 の +54,048 / +53,844 から、S3 で 60,784、P4 で 60,656 バイト減った。凍結リテラルは const にして flash へ (ハッシュ値は変換時に計算)、1 文字文字列の表も const、slab の表は SP_NO_SLAB で消え、nil で始まる定数・クラス ivar は PSRAM へ。P4 のアプリ区画は 7M にして 13% 空き。fmrb-next の `make test` / `make bench` / `make test-multi-ctx` は基準と同数

## 前提

| 項目 | 値 |
|---|---|
| fmruby-core | 作業ブランチ `feature/spinel-upstream` (P2b-1 の続き)。push していない |
| Spinel | `/home/kishima/fmrb/wt/spinel-rebase` の `fmrb-next`。開始 `88465f2a`、終了 **`0b350247`** (2 本足した。push していない) |
| 基準 | P2b-1 の基準の map / size をそのまま使った (develop `ac860133` + `vendor/spinel` `622750c`、同じ手順 `rake clean_all` → `rake build:esp32`)。`$S/../p2b1/base_{s3,p4}/` |
| 新 | 2 本を取り込んだ後、S3 (`FMRB_HW_TARGET=NARYAv3`) と P4 (`TAB5`) をそれぞれ `rake clean_all` → `rake build:esp32` → `idf.py size` / `size-components` / `size-files` |
| 生成物 | `$S = /tmp/claude-1000/-home-kishima-fmrb-family-mruby/a0ea00a0-6754-4693-ae2d-f05782a9784d/scratchpad/p2c/`。`new_s3/` `new_p4/` (size と map)、`t1_*.txt` (処置前の棚卸し)、`t6_*.txt` (処置後)、`symdiff.rb` (map の入力節を 1 つずつ突き合わせる道具)、`linux/` (sim のログ)、fork の試験ログ `base_*.log` `new_*.log` |

## 変化点の要約 (先に読む)

1. **両機種とも基準より減った** (S3 -6,736、P4 -6,812)。増分を 0 にしたうえで、Spinel 由来の既存分
   (定数・クラス ivar の .data 約 5.6 KB、1 文字文字列表 768 B) も外へ出した。
2. **凍結リテラル 30 KB は flash へ**。変換時に FNV-1a のハッシュ値と ASCII7 の印をヘッダに
   焼き込み、`static const` にした。ホストでは .rodata が読み取り専用なので、書き込みが残って
   いれば `make test` で SIGSEGV になる。コーパス 4,054 本は基準と同数で通った。
3. **`sp_hdr_char_cache` 16 KB は PSRAM ではなく flash へ**。起動時に決まった値で埋めるだけの
   表なので、定数の初期化子にできた。ランタイムが書き込むのは ASCII7 の印 1 か所だけで、
   全項目が必ずその印を得るので、最初から立てておける。
4. **SP_NO_SLAB で `sp_slab_on` を定数 0 にした**。`sp_slab_wk` 2 KB が消え、S3 で IRAM に
   入っていた `__atomic_exchange_4` (93 B) も参照が無くなって消えた。
5. **nil で始まる定数・クラス ivar・大域 (`SP_INT_NIL` など) を `SP_TU_NIL_SLOT` で宣言**。
   多重インスタンス構成ではエントリの頭 (`sp_reset_tu_statics`) で nil を書くので、静的な
   初期値が要らず、`SP_TU_BSS` で PSRAM に置ける。副作用として、同じプログラムを 2 回目に
   起動したとき整数・浮動小数の枠も nil から始まるようになった (これまでは前回の値が残っていた。
   8 節)。
6. **P4 は 7M の区画で空き 13%** (0xe41e0 バイト)。storage は 0x610000 から 0x710000 に動く。
   既存の Tab5 は最初の 1 回を `rake flash` (全体) にする必要がある (5 節)。

## 1. T1: 処置前の棚卸し (基準 → P2b-1 の新)

map の DIRAM に載る入力節 (S3: `.dram0.data` `.dram0.bss` `.noinit` `.iram0.text` `.iram0.vectors`、
P4: 加えて `.dram1.*` `.tcm.*`) を (オブジェクト, 節名) で突き合わせた。凍結リテラルは 1 本ずつ
節が分かれているので生成プログラムごとにまとめ、個数を添えた。**合計は P2b-1 の差と一致した**
(S3 +54,048 = .data +30,000 / .bss +23,952 / IRAM +96、P4 +53,844 = .data +30,016 / .bss +23,828)。

| 区分 | 所属 | シンボル | 個数 | S3 | P4 |
|---|---|---|---:|---:|---:|
| .data | 生成 (editor_combined) | `_fzl_*` | 485 | +19192 | +19184 |
| .bss | ランタイム sp_str.c.obj | `sp_hdr_char_cache` | 1 | +16384 | +16384 |
| .data | 生成 (fmrb_kernel_combined) | `_fzl_*` | 275 | +10760 | +10752 |
| .bss | ランタイム sp_slab.c.obj | `sp_slab_wk` | 1 | +2072 | +2072 |
| .bss | 生成 (fmrb_kernel_combined) | `sp_bt_buf` | 1 | +1024 | +1024 |
| .bss | 生成 (editor_combined) | `sp_bt_buf` | 1 | +1024 | +1024 |
| .bss | 生成 (fft_spinel) | `sp_bt_buf` | 1 | +1024 | +1024 |
| .bss | 生成 (spinel_hello_entry) | `sp_bt_buf` | 1 | +1024 | +1024 |
| .bss | 生成 (raycast_entry) | `sp_bt_buf` | 1 | +1024 | +1024 |
| .bss | ランタイム sp_alloc.c.obj | `sp_str_lcache` | 1 | -384 | -384 |
| .bss | その他 | `libespressif__mdns.a:mdns_receive.c.obj .n.2` | 1 | -263 | -263 |
| .bss | その他 | `libespressif__mdns.a:mdns_receive.c.obj .n.5` | 1 | +263 | +263 |
| .bss | ランタイム re_compile.c.obj | `buf.0` | 1 | +256 | +256 |
| .bss | 生成 (editor_combined) | `cst_*` | 107 | -188 | -188 |
| .data | 生成 (editor_combined) | `cst_*` | 155 | -160 | -160 |
| .bss | その他 | `libc.a:libc_a-__atexit.o .__atexit0` | 1 | +140 | - |
| .data | 生成 (editor_combined) | `civ_*` | 7 | +112 | +112 |
| .data | 生成 (fmrb_kernel_combined) | `civ_*` | 9 | -80 | -80 |
| IRAM | その他 | `libnewlib.a:stdatomic.c.obj .text.__atomic_exchange_4` | 1 | +66 | - |
| .bss | ランタイム sp_alloc.c.obj | `buf.0` | 1 | +64 | +64 |
| .data | 生成 (fft_spinel) | `_fzl_*` | 2 | +64 | +64 |
| .data | 生成 (raycast_entry) | `_fzl_*` | 1 | +32 | +32 |
| .data | その他 | `*fill*` | 1 | - | +32 |
| .bss | 生成 (raycast_entry) | `cst_*` | 7 | -28 | -28 |
| .data | 生成 (raycast_entry) | `cst_*` | 7 | +28 | +28 |
| IRAM | その他 | `libnewlib.a:stdatomic_s32c1i.c.obj .text.__atomic_s32c1i_exchange_4` | 1 | +27 | - |
| .bss | 生成 (fmrb_kernel_combined) | `civ_*` | 5 | +20 | +20 |
| .bss | 生成 (fmrb_kernel_combined) | `sp_pending_exc_recv` | 1 | -16 | -16 |
| .bss | 生成 (editor_combined) | `sp_pending_exc_recv` | 1 | -16 | -16 |
| .bss | 生成 (fft_spinel) | `sp_pending_exc_recv` | 1 | -16 | -16 |
| .bss | 生成 (spinel_hello_entry) | `sp_pending_exc_recv` | 1 | -16 | -16 |
| .bss | 生成 (raycast_entry) | `sp_pending_exc_recv` | 1 | -16 | -16 |
| .bss | その他 | `*fill*` | 1 | -3 | +13 |
| .data | 生成 (fmrb_kernel_combined) | `cst_*` | 33 | +12 | +12 |
| .bss | 生成 (fmrb_kernel_combined) | `cst_*` | 33 | -12 | -12 |
| .data | ランタイム sp_gc.c.obj | `trim_every.1` | 1 | +8 | +8 |
| .bss | ランタイム sp_alloc.c.obj | `first.1` | 1 | +8 | +8 |
| .bss | ランタイム sp_alloc.c.obj | `last.2` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `last_trim.0` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_trim_inline_t` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_trim_inline` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_trim_req` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_mk_scan` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_mk_globals` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_mk_fibers` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_mk_roots` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_conc_waits` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_apply_release` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_apply_str` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_apply_obj` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_park` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_barrier` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_conc_wall` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_conc_wait` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_mk_drains` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_mk_helpers` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_task_syoung` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_task_sold` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_task_obj` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_task_sum` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_slot_max` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_trim` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_strsweep` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_rembclear` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_slotsweep` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_oldsweep` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_mark` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_park_sweeping_n` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_park_sweeping` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_wait_top` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_mk_idle` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_mk_join` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_mk_drain` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_mk_takes` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_mk_spills` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_mk_by_helpers` | 1 | +8 | +8 |
| .bss | ランタイム sp_slab.c.obj | `sp_slab_rel_sort_t` | 1 | +8 | +8 |
| .bss | ランタイム sp_slab.c.obj | `sp_slab_rel_madv_t` | 1 | +8 | +8 |
| .bss | ランタイム sp_slab.c.obj | `sp_slab_rel_madv` | 1 | +8 | +8 |
| .bss | ランタイム sp_slab.c.obj | `sp_slab_rel_walked` | 1 | +8 | +8 |
| .bss | ランタイム sp_slab.c.obj | `sp_slab_rel_calls` | 1 | +8 | +8 |
| .bss | 生成 (fmrb_kernel_combined) | `gv_*` | 1 | -4 | -4 |
| .bss | 生成 (fmrb_kernel_combined) | `sp_user_exc_parent_fn` | 1 | -4 | -4 |
| .bss | その他 | `libmain.a:fmrb_spx_common.c.obj .sp_net_bin_len` | 1 | -4 | -4 |
| .bss | 生成 (editor_combined) | `sp_user_exc_parent_fn` | 1 | -4 | -4 |
| .data | 生成 (fmrb_kernel_combined) | `gv_*` | 1 | +4 | +4 |
| .data | ランタイム sp_alloc.c.obj | `on.3` | 1 | +4 | +4 |
| .data | ランタイム sp_alloc.c.obj | `sp_gc_str_major_sched` | 1 | +4 | +4 |
| .data | ランタイム sp_alloc.c.obj | `sp_gc_obj_budget_mode` | 1 | +4 | +4 |
| .data | ランタイム sp_gc.c.obj | `sp_gc_minor_on` | 1 | +4 | +4 |
| .data | ランタイム sp_gc.c.obj | `sp_gc_full_interval_start` | 1 | +4 | +4 |
| .data | ランタイム sp_gc.c.obj | `sp_gc_par_mark_on` | 1 | +4 | +4 |
| .data | ランタイム sp_gc.c.obj | `sp_gc_conc_on` | 1 | +4 | +4 |
| .bss | 生成 (fmrb_kernel_combined) | `sp_bigl` | 1 | +4 | +4 |
| .bss | 生成 (fmrb_kernel_combined) | `sp_at_exit_count` | 1 | +4 | +4 |
| .bss | 生成 (fmrb_kernel_combined) | `sp_explicit_cause_set` | 1 | +4 | +4 |
| .bss | 生成 (fmrb_kernel_combined) | `sp_reraise_current` | 1 | +4 | +4 |
| .bss | 生成 (fmrb_kernel_combined) | `sp_inflight_cause` | 1 | +4 | +4 |
| .bss | 生成 (fmrb_kernel_combined) | `sp_bt_n` | 1 | +4 | +4 |
| .bss | 生成 (editor_combined) | `sp_bigl` | 1 | +4 | +4 |
| .bss | 生成 (editor_combined) | `sp_at_exit_count` | 1 | +4 | +4 |
| .bss | 生成 (editor_combined) | `sp_explicit_cause_set` | 1 | +4 | +4 |
| .bss | 生成 (editor_combined) | `sp_reraise_current` | 1 | +4 | +4 |
| .bss | 生成 (editor_combined) | `sp_inflight_cause` | 1 | +4 | +4 |
| .bss | 生成 (editor_combined) | `sp_bt_n` | 1 | +4 | +4 |
| .bss | 生成 (fft_spinel) | `sp_at_exit_count` | 1 | +4 | +4 |
| .bss | 生成 (fft_spinel) | `sp_explicit_cause_set` | 1 | +4 | +4 |
| .bss | 生成 (fft_spinel) | `sp_reraise_current` | 1 | +4 | +4 |
| .bss | 生成 (fft_spinel) | `sp_inflight_cause` | 1 | +4 | +4 |
| .bss | 生成 (fft_spinel) | `sp_bt_n` | 1 | +4 | +4 |
| .bss | 生成 (spinel_hello_entry) | `sp_at_exit_count` | 1 | +4 | +4 |
| .bss | 生成 (spinel_hello_entry) | `sp_explicit_cause_set` | 1 | +4 | +4 |
| .bss | 生成 (spinel_hello_entry) | `sp_reraise_current` | 1 | +4 | +4 |
| .bss | 生成 (spinel_hello_entry) | `sp_inflight_cause` | 1 | +4 | +4 |
| .bss | 生成 (spinel_hello_entry) | `sp_bt_n` | 1 | +4 | +4 |
| .bss | 生成 (raycast_entry) | `sp_at_exit_count` | 1 | +4 | +4 |
| .bss | 生成 (raycast_entry) | `sp_explicit_cause_set` | 1 | +4 | +4 |
| .bss | 生成 (raycast_entry) | `sp_reraise_current` | 1 | +4 | +4 |
| .bss | 生成 (raycast_entry) | `sp_inflight_cause` | 1 | +4 | +4 |
| .bss | 生成 (raycast_entry) | `sp_bt_n` | 1 | +4 | +4 |
| .bss | ランタイム re_compile.c.obj | `rot.1` | 1 | +4 | +4 |
| .bss | ランタイム sp_alloc.c.obj | `prev_swept.4` | 1 | +4 | +4 |
| .bss | ランタイム sp_alloc.c.obj | `prev_marked.5` | 1 | +4 | +4 |
| .bss | ランタイム sp_alloc.c.obj | `sp_alloc_report_on` | 1 | +4 | +4 |
| .bss | ランタイム sp_alloc.c.obj | `sp_gc_str_major_fixed` | 1 | +4 | +4 |
| .bss | ランタイム sp_alloc.c.obj | `sp_gc_str_budget_fixed` | 1 | +4 | +4 |
| .bss | ランタイム sp_alloc.c.obj | `sp_gc_obj_budget_fixed` | 1 | +4 | +4 |
| .bss | ランタイム sp_core.c.obj | `sp_c_loc.0` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_on` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_slab_freed_str` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_slab_freed_obj` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_obj_retune_hook` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_age_on` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_verify_gen` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_trimmer_on` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_full_interval_fixed` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_in_sweeper` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_conc_promote` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_conc_wait_hook` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_conc_sweep_hook` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_str_major_due_hook` | 1 | +4 | +4 |
| .bss | ランタイム sp_io.c.obj | `sentinel.1` | 1 | +4 | +4 |
| .bss | ランタイム sp_slab.c.obj | `tick.0` | 1 | +4 | +4 |
| .bss | ランタイム sp_slab.c.obj | `sp_slab_sortcap` | 1 | +4 | +4 |
| .bss | ランタイム sp_slab.c.obj | `sp_slab_sortbuf` | 1 | +4 | +4 |
| .bss | ランタイム sp_slab.c.obj | `sp_slab_frees` | 1 | +4 | +4 |
| .bss | ランタイム sp_slab.c.obj | `sp_slab_verify_on` | 1 | +4 | +4 |
| .bss | ランタイム sp_slab.c.obj | `sp_gc_alloc_fast_ok` | 1 | +4 | +4 |
| .bss | ランタイム sp_slab.c.obj | `sp_slab_epoch` | 1 | +4 | +4 |
| .bss | ランタイム sp_slab.c.obj | `sp_slab_on` | 1 | +4 | +4 |
| .bss | ランタイム sp_slab.c.obj | `sp_slab_empty` | 1 | +4 | +4 |
| .bss | ランタイム sp_slab.c.obj | `sp_slab_cap` | 1 | +4 | +4 |
| .bss | ランタイム sp_slab.c.obj | `sp_slab_brk` | 1 | +4 | +4 |
| .bss | ランタイム sp_slab.c.obj | `sp_slab_base` | 1 | +4 | +4 |
| .bss | ランタイム sp_str.c.obj | `sp_hdr_char_cache_init` | 1 | +4 | +4 |
| .bss | その他 | `libespressif__mdns.a:mdns_cache.c.obj .s_cache` | 1 | +4 | +4 |
| IRAM | その他 | `*fill*` | 1 | +3 | - |
| .bss | 生成 (fmrb_kernel_combined) | `sp_pending_exc_flags` | 1 | -1 | -1 |
| .bss | 生成 (editor_combined) | `sp_pending_exc_flags` | 1 | -1 | -1 |
| .bss | 生成 (fft_spinel) | `sp_pending_exc_flags` | 1 | -1 | -1 |
| .bss | 生成 (spinel_hello_entry) | `sp_pending_exc_flags` | 1 | -1 | -1 |
| .bss | 生成 (raycast_entry) | `sp_pending_exc_flags` | 1 | -1 | -1 |
| | | **合計** | | **+54048** | **+53844** |

補足:
- `mdns_receive.c` の `n.2` / `n.5` は関数内 static の番号が変わっただけで、差は 0。
- `sp_str_lcache` -384 は上流で消えたもの (長さキャッシュはインスタンスのプールへ移った)。
- `cst_*` / `civ_*` の小さな増減は、上流の生成器が定数・クラス ivar の型を変えた結果
  (`.data` と `.bss` の間の移動が主)。
- IRAM の 2 行 (S3 のみ) は newlib の不可分操作。IDF が IRAM に置く。呼び出し元は `sp_slab.c`。
- `__atexit0` (S3 のみ、P4 では基準から DIRAM の外にある) は libc の atexit 表。
  `sp_alloc.c` (GC 統計の出力) と `sp_cold.c` (`trap("EXIT")`) の `atexit` 呼び出しで引き込まれた。

### 分類 (処置の方針)

| 増分 | 分類 | 処置 |
|---|---|---|
| 凍結リテラル `_fzl_*` 30,048 (S3) | flash | 2 節 T2 |
| `sp_hdr_char_cache` 16,384 | flash (定数表) | 3 節 |
| `sp_bt_buf` 1,024 × 5 | 外す (execinfo の無い構成では 1 枠) + PSRAM (`SP_TU_BSS`) | 3 節 |
| `sp_slab_wk` 2,072 | 外す (SP_NO_SLAB で不要) | 3 節 |
| `re_compile.c buf` 256 / `sp_alloc.c buf` 64 / `sp_gc_ph_*` 約 300 / `sp_slab_rel_*` 40 | PSRAM (`SP_RT_COLD`) | 3 節 |
| `__atomic_exchange_4` 93 (S3 の IRAM) | 外す (slab 経路と一緒に消えた) | 3 節 |
| ランタイムの 4-8 B の旗 (`sp_gc_*_on` `sp_gc_*_hook` `sp_slab_epoch` など) 計約 100 | 内蔵に残す | GC とアロケータの経路で毎回読む旗。合計 100 B 程度で、PSRAM に移す得より遅くなる危険が勝つ |
| 生成プログラムごとの 4 B の小物 (`sp_bt_n` `sp_inflight_cause` など) 計 15 (差し引き) | 内蔵に残す | 例外の経路で読み書きする。差し引きで 15 B |
| `__atexit0` 140 (S3) | 残した (外せる) | 下の「外していないもの」 |

## 2. T2: 凍結リテラルを flash に置く

### 方式 (fmrb-next `fc5870d6` codegen: emit frozen string literals as const objects)

- 凍結リテラルの静的オブジェクトを `static const struct { sp_str_hdr h; unsigned char m; char d[N]; }`
  で出す。式の型は従来どおり `char *` (キャストで合わせた。呼び出し側は変えていない)。
- ヘッダの `hash` を変換時に計算する: **FNV-1a 64 を最初の NUL まで** (`sp_str_hash_bytes` が
  C 文字列として歩くため)、0 なら 1 (`sp_str_hash_miss` と同じ)。リテラルには BINARY の印が
  付かないので、`sp_str_hash_compute` のバイナリ用の攪拌は掛からない。
- ヘッダの `size` に `SP_STR_SIZE_ASCII7` を、**`sp_str_count_units` の歩き方で文字数とバイト数が
  一致するとき**に立てる。ランタイムは最初の `length` でこの印を書き込んでいた
  (`sp_str_length` → `sp_str_mark_ascii7`)。7 ビットの文字列に加えて、UTF-8 として壊れた
  バイト列 (`count_units` がバイト数を返す) も印が付く。これはランタイムが最初の `length`
  の後に置く状態と同じ。
- 隣接リテラルの連結 (`"a" "b"`) は、エスケープ済みの文字列しか持っていなかったので、
  `interp_plan` のエスケープを逆にたどって生のバイトに戻し、同じヘッダを作る。以前はこの経路が
  `ascii7=0` を渡していた (印は実行時に書かれていた)。
- 名前に NUL を含むシンボルの `_sym_N` も同じ const オブジェクトにした (同じヘッダの書き込みを受けるため)。

### 実行時にリテラルのヘッダへ書く箇所の洗い出し

| 箇所 | 書くもの | リテラルに届くか | 処置 |
|---|---|---|---|
| `sp_str_hash_miss` | `hash` | 届く (0xf1) | 変換時に計算して入れた。以後 `sp_str_hash` の速い経路 (キャッシュ非 0) で止まる |
| `sp_str_length` → `sp_str_mark_ascii7` | `size` の ASCII7 | 印の無いリテラルに届く | 変換時に同じ判定で立てた。立っていれば `sp_str_fixed_width` が先に返る |
| `sp_str_set_len` / `sp_str_mark_binary` / `sp_str_as_binary` / `sp_str_as_text` / `sp_str_ascii7_clear` | `len` `hash` `size` | 破壊的な操作の経路。凍結の検査が先に FrozenError を出す | 変更なし (検出器で確かめた) |
| `sp_str_freeze_val` | マーカー | 0xf1 はそのまま返す (書かない) | 変更なし |
| GC の印付け | 0xf1 は `sp_gc_mark` 等が最初に返す。リテラルは `next=NULL` で掃除の一覧に入らない | 変更なし |

### 検出器の結果 (T2 の検出器)

ホストでは `static const` のオブジェクトは .rodata (読み取り専用の頁) に入る
(`nm` で `r _fzl_0.12` など、`objdump -h` で .rodata を確認)。書き込みが残っていれば SIGSEGV になる。

| | 基準 `88465f2a` | 処置後 `0b350247` |
|---|---|---|
| `make test` (コーパス) | 4054 pass / 1 fail | **4054 pass / 1 fail** (同じ 1 本 `systemcallerror_hierarchy`、ホストでの TCP の時間切れ。P2a と同じ) |
| `make test` の C 側の脚 (rbs / reject / backtrace / gc-* / ext / ext-cruby ほか 16 本) | 全 pass | 全 pass |
| `make bench` | 62 pass / 0 fail | **62 pass / 0 fail** |
| `make test-multi-ctx` | 全 PASS (大域 203 個) | **全 PASS (大域 192 個)** |
| `make test-lib-mode` | (回していない) | PASS |

基準は同じ clone に一時の worktree (`$S/spinel_base`、`88465f2a`) を作って回した。

小さな確認 (`$S/t/h.rb`): 凍結リテラルをキーにした Hash を、実行時に組み立てた同じ内容の
文字列で引く (ASCII と UTF-8 の両方)、NUL を含む連結、壊れた UTF-8 の添字、`"lit" << "x"` の
FrozenError、`:"a\0b"`。基準のコンパイラと出力が一致した。

### 既定の動作

上流の既定 (ホスト) でも有効にした。`-D` の口は作っていない。変わるのは置き場所 (.data → .rodata)
と、ヘッダの 2 項目が最初から埋まっていることだけ。後者は、ランタイムが最初の使用で書き込んでいた
値と同じ。

## 3. T3: 残りの .data / .bss

fmrb-next `0b350247` (runtime, codegen: keep what an embedded port never writes out of its RAM)。

| 対象 | 処置 | 既定の動作 |
|---|---|---|
| `sp_char_cache` (768) / `sp_hdr_char_cache` (16,384) | プリプロセッサで 256 項目を展開した**定数の初期化子**にした。初回の埋め込みと `_init` の旗は消えた。平の表は ASCII7 を最初から持つ (1 バイトの文字列は `count_units` が必ず 1 を返すので、ランタイムが全項目に立てていた印) | 変わらない (表の値は同じ。印は最初の `length` で立っていたもの) |
| 多重インスタンスでの共有 | 定数の表なのでインスタンス間で共有してよい。以前は遅延初期化の旗の書き込みに競合の余地があった (同じ値を書くので実害は無かった)。`globals_allow.txt` から 4 行を削った | |
| `sp_slab_on` (SP_NO_SLAB) | `sp_gc.h` で SP_NO_SLAB を導き (`SP_MULTI_CTX` か `SP_NO_MMAN` で自動)、**`sp_slab_on` を定数 0、`sp_slab_owns()` を常に偽**にした。`sp_gc_alloc` は slab の bump を通らず `sp_gc_alloc_full` へ (もともと `sp_gc_alloc_fast_ok` が常に 0 だった)。chunk の処理が畳まれ、`sp_slab_wk` と不可分操作の参照が消えた | SP_NO_SLAB でない構成は何も変わらない |
| `sp_bt_buf` | execinfo.h の無い構成 (`backtrace()` が 0 を返す置き換え) では 256 枠を 1 枠にし、`SP_TU_BSS` を付けた。未捕捉例外の経路のスタック上の `_bt_keep[256]` (1 KB) も同じく 1 枠になる | execinfo のある構成は同じ |
| `SP_RT_COLD` (新しい口) | `sp_types.h` に既定で空の属性。`SP_GC_PHASES` の計測値 (`sp_gc_ph_*`)、slab の解放統計、`re_group_name` と `sp_str_major_label` の作業領域に付けた。どれも診断の旗が立たない限り触られない | 空 |
| `SP_TU_NIL_SLOT` (新しい口) | 生成器が、nil の番兵 (`SP_INT_NIL`、浮動小数の NaN、`{SP_TAG_NIL,...}`、シンボルの -1) で始まる file-scope の枠 (定数・クラス ivar・大域・クラス変数・特異アクセサ) をこのマクロで宣言する。`SP_MULTI_CTX` では `SP_TU_BSS static T name` (0 初期化) とし、`sp_reset_tu_statics` の先頭で番兵を書く。既定の構成では `static T name = 初期値` のまま | 既定の構成は同じ C になる |

fmrb 側 (`components/fmrb_spinel_rt/fmrb_sp_tu_bss.h`): `SP_RT_COLD` を `EXT_RAM_BSS_ATTR` にした
(`SP_TU_BSS` と同じ条件: ESP 実機かつ `SP_THREADS` でない)。ヘッダのコメントに `SP_TU_NIL_SLOT` が
`SP_TU_BSS` に乗ることを足した。runtime にも生成 C にも同じヘッダが `-include` される (既存の配線)。

### `SP_TU_NIL_SLOT` が正しい理由

- `SP_MULTI_CTX` のエントリは全形式とも `sp_tu_ctx_init()` の直後に `sp_reset_tu_statics()` を
  呼ぶ (`emit_tu_ctx_init`)。`--persistent-statics` ではインスタンスごとに 1 回。どちらも
  プログラムの本体が走る前。
- 番兵を書く前にこれらの枠を読むのは GC の印付けだけで、印付けするのはポインタと poly の枠
  (poly の 0 は tag 0 = 整数として読まれ、印付けされない)。整数・浮動小数は印付けしない。
- fmrb の 5 本では、kernel / editor / spinel_hello は 1 インスタンス 1 回、fft / raycast は
  `--persistent-statics` で 1 回。

### 外していないもの

- `__atexit0` 140 B (S3): `atexit` の呼び出しを組み込み構成で外せば消える (`SP_NO_PROCESS` などの
  旗で包む)。多重インスタンスでは `trap("EXIT")` がプロセス終了に結び付くこと自体が意味を持たない。
  合計が負になっているので今回は触っていない。PR 候補に挙げた。
- ランタイムの 4 B の旗 (上表)。

## 4. T4: 速度の確認

sim では PSRAM の遅さは出ない (x86 の普通のメモリ)。sim で見えた数字は参考値:
raycaster `cast:11us` (P2b-1 は 6 us。sim の負荷の揺れの範囲)、fft_bench の spinel `avg=48.5us`、
spinel_q15 `avg=46.3us`。`edit_lat` / `hid_lat` は今回の操作量では出なかった (1,000 イベントごと)。

PSRAM に移したものと、実機 (P2b-2) で見るべき項目:

| 移したもの | 置き場所 | 通る頻度 | 実機で見ること |
|---|---|---|---|
| 1 文字文字列の表 (`sp_hdr_char_cache` `sp_char_cache`) | **flash** (.rodata、キャッシュ経由) | `s[i]` と `Integer#chr` のたび | エディタの打鍵遅延 (`edit_lat` / `spx: hid_lat`)、raycaster の `cast` |
| 凍結リテラル | **flash** | リテラルを読むたび | 同上 (ヘッダと本体の読み込み) |
| クラス ivar / 定数 (`civ_*` `cst_*`、editor 241 本・kernel 73 本など) | PSRAM (`SP_TU_BSS`) | **エディタとカーネルの状態の読み書きのたび (最も熱い)** | `edit_lat`、カーネルの `hid_lat`、GFX の `render_ms`。差が出るならこれが第一の容疑 |
| `sp_bt_buf` (1 枠) | PSRAM | 例外のたび (実質使わない) | なし |
| `SP_RT_COLD` の計測値 | PSRAM | 診断の旗が立つときだけ | なし |

flash と PSRAM はどちらもキャッシュを通る。熱い `civ_` は数 KB でキャッシュに収まる見込みだが、
キャッシュ外れのときの遅さは実機でしか測れない。遅ければ `civ_` だけ `SP_TU_BSS` から外す
(マクロを分ける) のが戻し方。

## 5. T5: パーティション

`config/partitions_p4.csv` の factory を 6M → 7M にした。storage は 8M のまま。

| | offset | size |
|---|---|---|
| nvs | 0x9000 | 24K |
| phy_init | 0xF000 | 4K |
| factory | 0x10000 | **7M** (0x700000) |
| storage | **0x710000** (旧 0x610000) | 8M |
| 終端 | 0xF10000 | (16MB = 0x1000000 の内側、残り 960K) |

P4 のイメージ `fmruby-core.bin` は 6,405,664 バイト、区画 0x700000 に対して **空き 13%**
(`check_sizes.py`: 0xe41e0 バイト)。NARYAv4 (`sdkconfig.defaults.naryav4`) も同じ csv を使い、flash は 16MB。
S3 (`partitions_n16r8.csv`) は変えていない (4,504,672 バイト、空き 28%)。

storage が動くことの影響:

- **既存の Tab5 の最初の更新は `rake flash` (全体) でなければならない**。`rake flash:app`
  (`idf.py app-flash`) はパーティション表を書かない。端末の表は古いまま (factory 6M) で、
  6M を超えるイメージは起動時の検査で弾かれ、しかも 0x610000 からの旧 storage の頭を上書きする。
  全体の書き込みは表・アプリ・storage を書くので /home は消える (csv の注記と同じ扱い)。
  一度全体を書いた後は、`rake flash:app` が再び使える。
- ファームは storage をラベル `"storage"` で探す (`fmrb_hal_file_esp32.c`)。offset を直書きしている所は無い。
- インストーラ: `release-installer.yml` が `flasher_args.json` を渡し、インストーラ側の
  `scripts/stage-firmware.sh` が offset をそこから導く (定数を持たない、とスクリプトの注記にある)。
  追従するはず。インストーラの書き込みは全体なので /home は消える (従来どおり)。
- `tools/` と Rakefile には storage の offset の直書きは無かった (grep)。

## 6. T6: 仕上げの計測

### 全体 (`idf.py size`)

| | S3 基準 | S3 P2b-1 | **S3 今回** | 基準との差 | P4 基準 | P4 P2b-1 | **P4 今回** | 基準との差 |
|---|---|---|---|---|---|---|---|---|
| DIRAM 使用 | 154,123 | 208,171 | **147,387** | **-6,736** | 190,260 | 244,104 | **183,448** | **-6,812** |
| うち `.data` | 34,668 | 64,668 | 29,052 | -5,616 | 26,436 | 56,452 | 20,868 | -5,568 |
| うち `.bss` | 32,464 | 56,416 | 31,344 | -1,120 | 75,888 | 99,716 | 74,644 | -1,244 |
| うち `.text` (S3 の DIRAM) | 86,991 | 87,087 | 86,991 | 0 | | | | |
| PSRAM `.bss` | 6,787,056 | 6,788,400 | 6,794,656 | +7,600 | 18,100,392 | 18,101,736 | 18,107,984 | +7,592 |
| イメージ | 4,165,216 | 4,503,936 | 4,504,672 | +339,456 | 5,978,912 | 6,405,712 | 6,405,664 | +426,752 |
| アプリ区画 | 6M 空き 34% | 6M 空き 28% | **6M 空き 28%** | | 6M 空き 5% | 6M 超過 | **7M 空き 13%** | |

凍結リテラルと表を flash に移しても、イメージはほぼ同じ (S3 +736、P4 -48)。.data の初期値は
もともと flash にも載っていたため。

### 処置後の棚卸し (基準との差、T1 と同じ道具)

| 区分 | 所属 | シンボル | 個数 | S3 | P4 |
|---|---|---|---:|---:|---:|
| .data | 生成 (editor_combined) | `civ_*` | 241 | -3844 | -3844 |
| .data | 生成 (fmrb_kernel_combined) | `civ_*` | 73 | -1168 | -1168 |
| .bss | ランタイム sp_str.c.obj | `sp_char_cache` | 1 | -768 | -768 |
| .data | 生成 (editor_combined) | `cst_*` | 54 | -564 | -564 |
| .bss | ランタイム sp_alloc.c.obj | `sp_str_lcache` | 1 | -384 | -384 |
| .bss | その他 | `libespressif__mdns.a:mdns_receive.c.obj .n.2` | 1 | -263 | -263 |
| .bss | その他 | `libespressif__mdns.a:mdns_receive.c.obj .n.5` | 1 | +263 | +263 |
| .bss | 生成 (editor_combined) | `cst_*` | 107 | -188 | -188 |
| .bss | その他 | `libc.a:libc_a-__atexit.o .__atexit0` | 1 | +140 | - |
| .data | 生成 (fmrb_kernel_combined) | `cst_*` | 15 | -60 | -60 |
| .data | その他 | `*fill*` | 1 | -16 | +32 |
| .bss | 生成 (raycast_entry) | `cst_*` | 7 | -28 | -28 |
| .bss | その他 | `*fill*` | 1 | -23 | -7 |
| .bss | 生成 (fmrb_kernel_combined) | `civ_*` | 5 | +20 | +20 |
| .bss | 生成 (fmrb_kernel_combined) | `sp_pending_exc_recv` | 1 | -16 | -16 |
| .bss | 生成 (editor_combined) | `sp_pending_exc_recv` | 1 | -16 | -16 |
| .bss | 生成 (fft_spinel) | `sp_pending_exc_recv` | 1 | -16 | -16 |
| .bss | 生成 (spinel_hello_entry) | `sp_pending_exc_recv` | 1 | -16 | -16 |
| .bss | 生成 (raycast_entry) | `sp_pending_exc_recv` | 1 | -16 | -16 |
| .bss | 生成 (fmrb_kernel_combined) | `cst_*` | 33 | -12 | -12 |
| .data | ランタイム sp_gc.c.obj | `trim_every.1` | 1 | +8 | +8 |
| .bss | ランタイム sp_alloc.c.obj | `first.0` | 1 | +8 | +8 |
| .bss | ランタイム sp_alloc.c.obj | `last.1` | 1 | +8 | +8 |
| .bss | ランタイム sp_gc.c.obj | `last_trim.0` | 1 | +8 | +8 |
| .bss | 生成 (fmrb_kernel_combined) | `gv_*` | 1 | -4 | -4 |
| .bss | 生成 (fmrb_kernel_combined) | `sp_user_exc_parent_fn` | 1 | -4 | -4 |
| .bss | その他 | `libmain.a:fmrb_spx_common.c.obj .sp_net_bin_len` | 1 | -4 | -4 |
| .bss | 生成 (editor_combined) | `sp_user_exc_parent_fn` | 1 | -4 | -4 |
| .bss | ランタイム sp_str.c.obj | `sp_char_cache_init` | 1 | -4 | -4 |
| .data | ランタイム sp_alloc.c.obj | `on.2` | 1 | +4 | +4 |
| .data | ランタイム sp_alloc.c.obj | `sp_gc_str_major_sched` | 1 | +4 | +4 |
| .data | ランタイム sp_alloc.c.obj | `sp_gc_obj_budget_mode` | 1 | +4 | +4 |
| .data | ランタイム sp_gc.c.obj | `sp_gc_minor_on` | 1 | +4 | +4 |
| .data | ランタイム sp_gc.c.obj | `sp_gc_full_interval_start` | 1 | +4 | +4 |
| .data | ランタイム sp_gc.c.obj | `sp_gc_par_mark_on` | 1 | +4 | +4 |
| .data | ランタイム sp_gc.c.obj | `sp_gc_conc_on` | 1 | +4 | +4 |
| .bss | 生成 (fmrb_kernel_combined) | `sp_bigl` | 1 | +4 | +4 |
| .bss | 生成 (fmrb_kernel_combined) | `sp_at_exit_count` | 1 | +4 | +4 |
| .bss | 生成 (fmrb_kernel_combined) | `sp_explicit_cause_set` | 1 | +4 | +4 |
| .bss | 生成 (fmrb_kernel_combined) | `sp_reraise_current` | 1 | +4 | +4 |
| .bss | 生成 (fmrb_kernel_combined) | `sp_inflight_cause` | 1 | +4 | +4 |
| .bss | 生成 (fmrb_kernel_combined) | `sp_bt_n` | 1 | +4 | +4 |
| .bss | 生成 (editor_combined) | `sp_bigl` | 1 | +4 | +4 |
| .bss | 生成 (editor_combined) | `sp_at_exit_count` | 1 | +4 | +4 |
| .bss | 生成 (editor_combined) | `sp_explicit_cause_set` | 1 | +4 | +4 |
| .bss | 生成 (editor_combined) | `sp_reraise_current` | 1 | +4 | +4 |
| .bss | 生成 (editor_combined) | `sp_inflight_cause` | 1 | +4 | +4 |
| .bss | 生成 (editor_combined) | `sp_bt_n` | 1 | +4 | +4 |
| .bss | 生成 (fft_spinel) | `sp_at_exit_count` | 1 | +4 | +4 |
| .bss | 生成 (fft_spinel) | `sp_explicit_cause_set` | 1 | +4 | +4 |
| .bss | 生成 (fft_spinel) | `sp_reraise_current` | 1 | +4 | +4 |
| .bss | 生成 (fft_spinel) | `sp_inflight_cause` | 1 | +4 | +4 |
| .bss | 生成 (fft_spinel) | `sp_bt_n` | 1 | +4 | +4 |
| .bss | 生成 (spinel_hello_entry) | `sp_at_exit_count` | 1 | +4 | +4 |
| .bss | 生成 (spinel_hello_entry) | `sp_explicit_cause_set` | 1 | +4 | +4 |
| .bss | 生成 (spinel_hello_entry) | `sp_reraise_current` | 1 | +4 | +4 |
| .bss | 生成 (spinel_hello_entry) | `sp_inflight_cause` | 1 | +4 | +4 |
| .bss | 生成 (spinel_hello_entry) | `sp_bt_n` | 1 | +4 | +4 |
| .bss | 生成 (raycast_entry) | `sp_at_exit_count` | 1 | +4 | +4 |
| .bss | 生成 (raycast_entry) | `sp_explicit_cause_set` | 1 | +4 | +4 |
| .bss | 生成 (raycast_entry) | `sp_reraise_current` | 1 | +4 | +4 |
| .bss | 生成 (raycast_entry) | `sp_inflight_cause` | 1 | +4 | +4 |
| .bss | 生成 (raycast_entry) | `sp_bt_n` | 1 | +4 | +4 |
| .bss | ランタイム re_compile.c.obj | `rot.0` | 1 | +4 | +4 |
| .bss | ランタイム sp_alloc.c.obj | `prev_swept.3` | 1 | +4 | +4 |
| .bss | ランタイム sp_alloc.c.obj | `prev_marked.4` | 1 | +4 | +4 |
| .bss | ランタイム sp_alloc.c.obj | `sp_alloc_report_on` | 1 | +4 | +4 |
| .bss | ランタイム sp_alloc.c.obj | `sp_gc_str_major_fixed` | 1 | +4 | +4 |
| .bss | ランタイム sp_alloc.c.obj | `sp_gc_str_budget_fixed` | 1 | +4 | +4 |
| .bss | ランタイム sp_alloc.c.obj | `sp_gc_obj_budget_fixed` | 1 | +4 | +4 |
| .bss | ランタイム sp_core.c.obj | `sp_c_loc.0` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_ph_on` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_obj_retune_hook` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_age_on` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_verify_gen` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_trimmer_on` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_full_interval_fixed` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_in_sweeper` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_conc_promote` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_conc_wait_hook` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_conc_sweep_hook` | 1 | +4 | +4 |
| .bss | ランタイム sp_gc.c.obj | `sp_gc_str_major_due_hook` | 1 | +4 | +4 |
| .bss | ランタイム sp_io.c.obj | `sentinel.1` | 1 | +4 | +4 |
| .bss | ランタイム sp_slab.c.obj | `sp_slab_frees` | 1 | +4 | +4 |
| .bss | ランタイム sp_slab.c.obj | `sp_gc_alloc_fast_ok` | 1 | +4 | +4 |
| .bss | ランタイム sp_slab.c.obj | `sp_slab_epoch` | 1 | +4 | +4 |
| .bss | その他 | `libespressif__mdns.a:mdns_cache.c.obj .s_cache` | 1 | +4 | +4 |
| .bss | 生成 (fmrb_kernel_combined) | `sp_pending_exc_flags` | 1 | -1 | -1 |
| .bss | 生成 (editor_combined) | `sp_pending_exc_flags` | 1 | -1 | -1 |
| .bss | 生成 (fft_spinel) | `sp_pending_exc_flags` | 1 | -1 | -1 |
| .bss | 生成 (spinel_hello_entry) | `sp_pending_exc_flags` | 1 | -1 | -1 |
| .bss | 生成 (raycast_entry) | `sp_pending_exc_flags` | 1 | -1 | -1 |
| | | **合計** | | **-6736** | **-6812** |

残る正の項目は、`__atexit0` 140 (S3)、ランタイムの 4-8 B の旗 (約 70)、`civ_*` の .bss +20
(kernel、0 初期化の枠)、`mdns` の番号替え (差 0) だけ。IRAM は S3・P4 とも基準と同じ。

### sim (標準構成、Tab5 の 426x240)

`rake clean_all` → `SPINEL_DIR=... rake build:linux`。`file build/fmruby-core.elf` は x86-64。

| 操作 | 結果 |
|---|---|
| 起動してデスクトップ | 通過 (1 回目は graphics-audio が `input_socket: Failed to bind socket: Interrupted system call` で起動せず。P2b-1 と同じ既知の揺らぎ。ログ `$S/linux/ga_bind_eintr.log`、3 コンテナを下ろして上げ直して通過) |
| エディタ起動 → 「puts 1」「abc = 2」 (構文色つき) → Ctrl+X → 保存確認 → N | 通過 |
| エディタ再起動 → 「x = [1, 2]」「p x」 → File メニューをクリックで開く → Exit | 通過 (再起動で前回の状態は残っていない。`SP_TU_NIL_SLOT` のリセットを通る経路) |
| raycaster → B で Spinel に切替 (`cast:11us`) → kill | 通過 |
| spinel_hello → kill | 通過 (「Hello world! built by Spinel」) |
| fft_bench → kill | 通過 (spinel / spinel_q15 とも `agrees=true`、dev 0 / 12) |
| メニュー → Config → 値を変えて Cancel | 通過 |
| 起動音 | graphics-audio に `audio_note_lat: n=13 on=9` |

ログ (`$S/linux/core.log`): FrozenError / IndexError / abort / `begin frames too deep` / Guru が **0 件**、
`E (` の行も 0 件。sim は sim_down 済み。

## 7. P2b-2 (実機) の指示書に入れるべきこと

1. **Tab5 の最初の書き込みは `rake flash` (全体)**。/home が消えることをユーザに先に伝える。
2. **待機時の内蔵 RAM 空きの比べ方**: 同じ手順の基準 (develop のファームを焼いた同じ板) と、
   ブート後の `M1|` 行 (最後の定常値) と 10 秒周期の `IRAM free:` を並べる。静的な差は
   S3 -6,736 / P4 -6,812 なので、待機時空きもその程度**増える**のが予測値。ずれたら実行時の確保
   (インスタンスごとの sp_ctx、remembered/pinned の 5 KB はプールから) を疑う。P4 の P2b-1 時点の
   見込み (約 98,800) は無効になった。
3. PSRAM に移した `civ_` / `cst_` の速度 (4 節の表): `edit_lat`、`spx: hid_lat`、`GFX STATS` の
   `render_ms`、raycaster の `cast`。基準と同じ操作で比べる。
4. flash に移したリテラルが本当に書かれないこと: ESP では flash への書き込みは例外
   (`LoadStoreError` / キャッシュのエラー) で落ちる。ホストの検出器で見つからなかった経路が
   あれば実機でだけ出る。Guru のログが 0 件であることを確かめる。
5. 同じアプリを閉じて開き直したときの挙動 (エディタ、FM-Shell など Spinel のもの): 整数の
   クラス ivar が nil から始まるようになった (8 節)。

## 8. 見立てと違った点・撤回した仮説

- **「`sp_hdr_char_cache` は PSRAM かプールへ」は外れ。flash に置けた**。起動時に同じ値で埋まる
  表で、書き込みは ASCII7 の印 1 か所だけ、しかも全項目が必ず印を得る。PSRAM より速度の心配が
  少ない (どちらもキャッシュ経由だが、書き込みが無い)。
- **「`sp_slab_wk` は gc-sections で落ちない理由が未調査」**: 同じ TU の `sp_gc_alloc` の速い経路が
  参照していた。`sp_gc_alloc_fast_ok` は SP_NO_SLAB では常に 0 だが、実行時の値なのでコンパイラは
  消せなかった。`sp_slab_on` を定数にし、`sp_gc_alloc` の先頭で分けて消えた。
- **「Integer 定数の SP_INT_NIL 初期化は kernel で 512 B 程度」**: 実際に効いたのは定数より
  **クラス ivar (`civ_`)** で、editor 241 本・kernel 73 本、.data で約 5 KB。
- **「多重インスタンスでは file-scope の枠がエントリの頭で全部クリアされる」は半分だけ正しかった**。
  クリアされていたのはポインタと poly の枠だけで、`SP_INT_NIL` などの整数・浮動小数の枠は
  前回のインスタンスの値のまま残っていた。定数は本体が代入し直すので害は無いが、遅れて
  代入されるクラス ivar (`@x ||= ...` の類) は 2 回目の起動で前回の値を見る。今回の
  `SP_TU_NIL_SLOT` で直った (上流でも同じ。PR 候補)。
- **ASCII7 の印は「7 ビットの証明」ではなかった**。`sp_str_count_units` は壊れた UTF-8 に
  バイト数を返すので、ランタイムは壊れた UTF-8 にも印を立てる (`sp_str_length` のコメントは
  「全バイトが 0x80 未満」と言う)。変換時の判定はランタイムの実際の挙動に合わせた。
  そのため、壊れた UTF-8 の凍結リテラルは最初から 1 バイト単位で添字が取られる (以前は最初の
  `length` の後から)。
- **不可分操作の IRAM 93 B は slab の経路から**来ていた。別の手当ては要らなかった。

## 9. PR 候補

台帳は編集していない。仮 ID は P2b-1 の続き (N-26 から)。確認は fmrb-next `0b350247` (上流 `01521b1e` 基点)。

| 仮 ID | 内容 | 最小再現 / 根拠 | 見込み |
|---|---|---|---|
| N-26 (= P-11 / N-16) | 凍結リテラルを `const` にし、ハッシュ値と ASCII7 を変換時に計算する。リテラルが .rodata に行く (上流の全構成で .data が減る) | `fc5870d6`。`make test` 全体が検出器になる | 高 |
| N-27 | 1 文字文字列の表を定数の初期化子に。遅延初期化の競合も消える | `0b350247` の sp_str.c | 高 (小さい、上流の既定でも得) |
| N-28 | SP_NO_SLAB で `sp_slab_on` / `sp_slab_owns` を定数に | `0b350247` の sp_gc.h / sp_slab.c | 中 (SP_NO_SLAB 自体がフォークの口なら、フォーク側) |
| N-29 | 多重インスタンスで、整数・浮動小数の file-scope の枠がエントリの頭で nil に戻らない (ポインタだけ戻る)。2 回目の起動で前回の値が見える | 生成 C の `sp_reset_tu_statics` を読む。`SP_MULTI_CTX` がフォークの口なのでフォーク側 | フォーク側 |
| N-30 | ASCII7 の印が壊れた UTF-8 にも立つ。`s[i]` の結果が、同じ文字列への最初の添字と 2 回目以降で変わりうる (1 回目は UTF-8 の歩き、`length` の後はバイト) | `sp_str_count_units` の `return (sp_int)bl` と `sp_str_length` の印付け。ホストの小さな再現はまだ作っていない | 中 |
| N-31 | execinfo の無い構成で `sp_bt_buf[256]` と `_bt_keep[256]` (スタック 1 KB) が無駄 | `0b350247` の spinel_rt.h | 中 |
| N-32 | `SP_RT_COLD` (ランタイムの冷たい .bss の置き場所の口) | `0b350247` | 低 (組み込み向けの口。N-20 / N-25 と一緒に) |
| N-33 | 組み込み構成で `atexit` を呼ばない (libc の atexit 表が引き込まれる。多重インスタンスではプロセス終了と結び付けること自体が合わない) | map の相互参照: `atexit` ← `sp_cold.c` `sp_alloc.c` | 低 |

## 10. 既存の内蔵 RAM の削減候補 (記録のみ)

親からの追加指示による。`doc/reference/internal_ram_budget.md` (タスクスタック、BLE 遅延起動、
キューの PSRAM 化、`g_fs_ctx` / debugd linebuf、`pm_binding_powers`、IRAM_ATTR の棚卸し) に
既にあるものは除くか、そう注記した。値は今回の新ビルドの map (`$S/new_{s3,p4}`)。
「移し先」は候補で、確認していない。

### 上位のシンボル

| シンボル | 所属 | 区分 | S3 | P4 | 移し先の候補 | 懸念 |
|---|---|---|---:|---:|---|---|
| `g_progs` | display_p4_vm.cpp | .bss | - | 8,832 | PSRAM | 描画 VM のプログラム表。描画タスクの熱い経路。キャッシュ次第 |
| `s_files` | fmrb_tmpfs_esp32.c | .bss | 2,016 | 5,376 | PSRAM | /tmp の管理表。ファイル操作の経路で DMA は無いはず |
| `scaled` (`ppa_present_patch` 内 static) | display_backend_ppa.cpp | .bss | - | 4,608 | 外せない可能性 | PPA (DMA) に渡すなら内蔵が要る。要確認 |
| `g_recv_buf` | display_p4_task.cpp | .bss | - | 4,096 | PSRAM | 受信の作業領域。DMA でなければ移せる |
| `s_fs_buf` | devctl_http.c | .bss | 4,096 | 4,096 | PSRAM | 遠隔操作の /fs 転送の作業領域。httpd の送受信に渡すだけなら可。リリースでは機能ごと無効 |
| `s_file_write_bounce` | fmrb_hal_file_esp32.c | .bss | 4,096 | 4,096 | 残す見込み | 名前のとおり内蔵に置く跳ね返しバッファ (flash 書き込み中に PSRAM が読めない対策の可能性)。budget.md の M-3 表にある |
| `g_recv_internal_buf` | fmrb_hal_link_local.c | .bss | - | 4,096 | 要確認 | リンクの受信。DMA なら残す |
| `s_stereo_buf` / `s_mic_buf` | audio_p4_hw.c | .bss | - | 3,168 / 1,024 | 残す | I2S の DMA に渡す可能性が高い |
| `xIsrStack` | FreeRTOS port.c | .bss (S3 は .data) | 3,072 | 3,072 | 残す | 割り込みスタック |
| `g_wrap` | editor_core.c | .bss | 2,208 | 2,208 | PSRAM | エディタの折返し表。打鍵の経路 (速度) |
| `s_ring` | fmrb_midi_sched.c | .bss | 2,048 | 2,048 | PSRAM | MIDI 送出のリング。タイマ割り込みから触るなら残す |
| `esp_log_system_timestamp.str1.4` ほか `.rodata.*` が DRAM にあるもの | liblog / libphy / esp_psram / esp_mm | .data | 約 10,000 (合計) | 約 9,600 (合計) | (IDF の配置) | IRAM / flash 無効中に使う文字列として IDF が DRAM に置く。sdkconfig 側の話で、今回は触らない |
| `packet` (mdns_send) | espressif__mdns | .bss | 1,460 | 1,460 | PSRAM | mDNS のパケット組立 |
| `g_sorted` | display_p4_sprite.cpp | .bss | - | 1,280 | PSRAM | スプライトの並べ替え。描画の経路 |
| `dns_table` / `destination_cache` / `sockets` ほか | lwIP | .bss | | 約 2,400 | (sdkconfig の LWIP の PSRAM 配置) | sdkconfig の変更が要る。提案のみ |
| `g_hid_devices` | usb_task.c | .bss | 1,024 | 1,024 | 要確認 | USB ホストのコールバックから触る |
| `TxRxCxt` ほか libpp / libphy / libnet80211 の .data | WiFi / PHY のバイナリ | .data | 約 9,000 | - | 外せない | ベンダ提供の配置 |
| `g_tranport_context` | fmrb_transport.c | .bss | 776 | 776 | PSRAM | 転送層の文脈。ISR から触るか要確認 |
| `s_pins` | pin manager | .bss | - | 440 | PSRAM | 冷たい |
| `g_hover` / `g_oom` | editor_ti_bridge.c | .bss | 392 / 304 | 同 | PSRAM | 型支援の表示。冷たい |
| `trilength_lut` | nes_apu.c | .bss | - | 512 | flash (const) | 表を実行時に埋めているなら const にできる |
| `pm_binding_powers` | mruby prism | .data | 1,980 | 1,980 | flash (const) | budget.md の E に既出 |

### まとめて移せる群

| 群 | S3 | P4 | 移し先 | 懸念 |
|---|---:|---:|---|---|
| mruby の `gem_init.o` / `picogem_init.o` の `.data` (mrblib の irep 表・シンボル表、274 本) | 5,848 | 5,992 | flash (const にできれば) | picoruby の生成物。生成器が `const` を付けないため .data に入る。mruby 本体の irep は const 対応があるので、生成スクリプト側 (lib/add か lib/patch) で直せる見込み |
| Spinel 生成プログラムの 0 初期化の枠 (ポインタの `cst_` / `civ_` / `gv_` など) と、`fmrb_spx_*.c` の作業領域 (`buf` `payload` 計約 1.4 KB) | 約 2,000 | 約 2,000 | PSRAM (`SP_TU_BSS` をポインタの枠にも。`fmrb_spx_*` は `EXT_RAM_BSS_ATTR`) | 生成プログラムの枠は `SP_TU_NIL_SLOT` と同じ手で移せるが、ポインタの枠は GC の根として印付けのたびに読む。今回は nil の番兵の枠だけにとどめた |
| 描画系 (P4: `g_progs` `g_recv_buf` `g_sorted` `g_canvases` カーソル退避など) | - | 約 16,000 | PSRAM | DMA に渡すものが混ざる。描画の速度 |
| ファイル系 (`s_files` `s_fs_buf` `g_fs_ctx` ほか) | 約 6,100 | 約 9,500 | PSRAM | flash 書き込み中は PSRAM も読めない S3 の制約 (`s_file_write_bounce` の存在理由) |

今回 P2c の中で移したのは、Spinel の生成プログラムとランタイムに属するもので P2c と同じ手を
使えるものだけ: `civ_` / `cst_` / `gv_` の nil 番兵の枠 (約 5.6 KB)、1 文字文字列の表 768 B。

## 11. コミット

### fmrb-next (`/home/kishima/fmrb/wt/spinel-rebase`、push していない)

| コミット | 件名 |
|---|---|
| `fc5870d6` | codegen: emit frozen string literals as const objects |
| `0b350247` | runtime, codegen: keep what an embedded port never writes out of its RAM |

基準の試験に使った worktree `$S/spinel_base` は作業後に外した。

### fmruby-core (`feature/spinel-upstream`、push していない)

| ファイル | 変更 |
|---|---|
| `components/fmrb_spinel_rt/spinel_rt/*` | `import_from_fork.rb /home/kishima/fmrb/wt/spinel-rebase` で `0b350247` を取り直し |
| `components/fmrb_spinel_rt/fmrb_sp_tu_bss.h` | `SP_RT_COLD` を `EXT_RAM_BSS_ATTR` に。`SP_TU_NIL_SLOT` の説明 |
| `config/partitions_p4.csv` | factory 6M → 7M と注記 |
| `doc/spinel_upstream_ext/report/p2c.md` | この報告 |

fmrb の Ruby と C (main/) は変えていない。sdkconfig / sdkconfig.defaults も変えていない。

## 12. `.env` と後片付け

- `.env` は一度も書き換えていない (ターゲットはコマンドラインの `FMRB_HW_TARGET`)。最後に
  `git diff .env` を `env_before_p2b1.diff` と比べて**一致**。コミットに含めていない。
- 親の未追跡ファイル `doc/spinel_upstream_ext/instruction_pr3.md` には触っていない。
- build/ は最後に Linux (x86-64)。sim は sim_down 済み。
- `/home/kishima/fmrb/wt/spinel-pr/` と `~/dev/spinel` には触っていない。
