# 調査: 生成 Spinel TU 1 本あたりの内蔵 RAM

計測日: 2026-08-15 / 対象: `8241ec5` (develop) / ターゲット: TAB5 (ESP32-P4)

`doc/internal_ram_budget.md` の **M-3 (静的確保 .bss/.data の内訳)** に相当する。
きっかけは「raycast gem を足したら内蔵 RAM は増えたか」という問い。

## 結論

**Spinel プログラムを 1 本足すと、内蔵 RAM が約 11.4KB 静的に減る。**
一度も呼ばなくてもリンク時から確保されたままになる。中身は**書いた Ruby とは
無関係**で、ほぼ全部が Spinel ランタイムの「1 TU あたりの固定装備」である。

現状 5 本で **60,496 バイト = ファーム全体の内蔵 .bss/.data (163,246 バイト) の 37%**。

## 生成 TU ごとの内蔵 .bss/.data

| 生成 TU | バイト |
|---|---:|
| `editor_combined.c.obj` | 13,536 |
| `fmrb_kernel_combined.c.obj` | 12,688 |
| `fft_spinel.c.obj` | 11,424 |
| `spinel_hello_entry.c.obj` | 11,424 |
| `raycast_entry.c.obj` | 11,424 |
| **合計** | **60,496** |

参考までに、これ以外の上位は `display_p4_task.cpp` 10,280 /
`display_p4_vm.cpp` 8,832 / `fmrb_hal_link_local.c` 8,220 /
`sp_time.c` 8,192 (ランタイム共有、TU ごとではない)。

## 1 本の内訳 (`raycast_entry.c.obj`、`.bss` 11,424 バイト)

| バイト | シンボル | 何か |
|---:|---|---|
| 4,864 | `sp_catch_stack` | catch ハンドラスタック (16 段 x 304B) |
| 4,864 | `sp_exc_stack` | 例外ハンドラスタック (16 段 x 304B) |
| 1,024 | `sp_dyn_syms` | 実行時 intern シンボル表 (256 エントリ) |
| 256 | `sp_catch_val` | |
| 416 | 残り 42 個 | `sp_exc_obj` / `sp_exc_cls` / `sp_exc_msg` など各 64B |

**例外/catch スタックだけで 9,728 バイト、85%**。1 段 304 バイトは RV32 の
`jmp_buf` のサイズ。

**アプリのコードは実質ゼロ**: `RaycastCore` のインスタンス変数は `civ_*` が
4 バイトのポインタ数本、キャッシュ用の `gv_raycast` も 4 バイト、定数も
`mrb_int` 数本 (`.sdata`/`.sbss` 合わせて 133 バイト)。

## なぜ TU ごとに増えるのか

`sp_runtime.h` がこれらをファイルスコープの配列として宣言しており、生成
プログラムは必ずこのヘッダを include する。したがって**生成 TU が 1 本増える
たびに 1 セットまるごと複製される**。マルチインスタンス化 (`sp_ctx.h`) が
移したのは「共有 lib 側のグローバル」であって、per-TU static はそこに含まれない
(`embedded_constraints.md` 7 章)。

## 既に縮めた後の値である

`components/fmrb_spinel_rt/CMakeLists.txt` で Spinel の既定値から下げてある。

| 設定 | 現在 | Spinel 既定 |
|---|---:|---:|
| `SPINEL_RT_EXC_STACK_MAX` (= `SP_EXC_STACK_MAX` / `SP_CATCH_STACK_MAX`) | 16 | 64 |
| `SPINEL_RT_DYN_SYMS_MAX` (= `SP_DYN_SYMS_MAX`) | 256 | 8192 |

既定のままなら 1 TU あたり例外/catch だけで約 39KB になっていた計算になる。

## 削減余地: 16 → 8 で約 24KB 戻る

例外/catch スタックを 8 段にすると **1 TU あたり約 4.9KB、5 本で約 24KB** が
内蔵 RAM に戻る。現在のアイドル空きが約 95KB なので小さくない。

**ただし実測の裏づけが片方しか無い。** 深さの実測は周期ダンプの `ExcHW` 列に出る:

```
Name               VM     Used     Free    Total  Frag  ExcHW
fmrb_kernel       spx    91400   420208   512000    3%    3/0
system_desktop    mrb   520744   298064   819200   21%      -
Raycaster         mrb   298656   749528  1048576  119%      -
```

- **Spinel カーネルは 3/0** — 16 段確保して 3 段しか使っていない。8 段で十分。
- **ライブラリとして呼ぶ gem (fft / hello / raycast) の ExcHW は出ていない**。
  これらは mruby アプリのタスク内でインスタンスを作る形なので、`fmrb_app:` の
  行は mruby VM として出ており Spinel 側の深さが見えない。
- editor / desktop を Spinel で動かす構成での実測も未取得。

`begin`/`rescue` をほとんど使わない gem なら浅いはずだが**推測である**。
下げるなら、**まず gem 側の ExcHW を採れるようにしてから**が安全。
(`embedded_constraints.md` の「見積もりでサイズを決めない」に該当する。)

## 内蔵 RAM を使っていないもの (確認済み)

同じ計測で、以下は内蔵 RAM をまったく消費していないことを確認した。

| 対象 | 内蔵 | PSRAM |
|---|---:|---:|
| `raycast_native.c` (Spinel の受け皿) | **0** | 4,652 (`EXT_RAM_BSS_ATTR`) |
| `raycast_binding.c` (mruby binding) | 0 | - |
| `picoruby_fmrb_raycast.c` (gem init) | 0 | - |
| `rd_http.c` 全体 (開発用リモート制御を含む) | 16 (既存の `s_ws_fds`) | - |

- 受け皿の static を全部 `EXT_RAM_BSS_ATTR` にする方針
  (`doc/raycast_spinel/plan.md`) は意図どおり効いている。マップバッファ 4KB も
  PSRAM。
- **開発用リモート制御 (`/app/launch` 等) の常駐コストは実質ゼロ**。増えたのは
  `max_uri_handlers` 8→16 に伴う httpd のヒープ上のハンドラ表 (ポインタ 8 本) だけ。
  一時的にはハンドラ実行中のスタック約 1.5KB (`body[768]` +
  `fmrb_app_info_t list[7]` 約 560B + クエリ用 320B) を使うが、httpd タスクの
  スタックは 8KB あり、debugd は同じ `fmrb_app_ps` を 6KB スタックで呼んでいる。
  端点を 40 回以上連打して IRAM 空きは 95,888 バイトから 1 バイトも動かず、
  Guru/abort もゼロ。

## 計測方法 (再現手順)

```
rake build:esp32
# build/fmruby-core.map を obj 単位で集計する。
```

- **`size -A <obj>` だけでは足りない**。ESP32 の PSRAM 配置は
  `.ext_ram.bss.*` という別セクションになるが、`size` の出力では内蔵と並んで
  出るため、単純に合計すると PSRAM を内蔵として数えてしまう。
  **リンクマップを見て `.ext_ram.*` を除外する**こと。
- RISC-V は小さな変数を `.sdata` / `.sbss` に置く。`.bss` / `.data` だけを
  拾うと取りこぼす。
- マップの配置行は「セクション名が単独行 → 次の行にアドレス・サイズ・オブジェクト」
  という 2 行構成になることがある。1 行で正規表現を書くと 0 件になる。

## 次にやるなら

1. **gem 側 (ライブラリ呼び出しの Spinel インスタンス) の ExcHW を出す**。
   `fmrb_spinel_instance_exc_hw()` は既にランタイムにある
   (`sp_ctx.h` の `sp_instance_exc_hw`)。受け皿から読んでログに出せばよい。
2. 実測が浅いことを確認できたら `SPINEL_RT_EXC_STACK_MAX` を 8 へ。
   **全 Spinel プログラムに一律で効く**ので、回帰確認は kernel / editor /
   desktop / 各 gem の一通りの動作。
3. `SP_DYN_SYMS_MAX` 256 (1,024B/TU) も同様に実測できれば下げられるが、
   回収量は例外スタックの 1/5 なので優先度は低い。

---

# 追加検討: PSRAM 退避で内蔵をほぼ 0 に (2026-08-15)

段数削減 (16→8、実測待ち) とは別方針。**per-TU の大物を丸ごと PSRAM へ移し、内蔵を
ほぼ 0 に**する。段数を残したまま効くうえ、ExcHW の実測を待たずに実施できる。

## 前提の確認 (この構成で成り立つ事実)

- **`SP_TLS` は空**。fmrb は `SP_MULTI_CTX` でビルドしており `SP_THREADS` は未定義
  (`sp_types.h`: `SP_THREADS` のときだけ `__thread`)。よって対象は**素の file-scope
  static (.bss)** で、TLS の「PSRAM に置けない」制約はかからない。報告本文が .bss で
  計測できているのもこれが理由。
- **`sp_runtime.h` は fork から import される** (`IMPORT_INFO`: fmrb-dev @ cafe659…、
  `import_from_fork.rb` 生成、"do not edit by hand")。`rake spinel:setup` 系が commit 一致を
  検査する。→ **修正は Spinel fork 側 (kishima/spinel fmrb-dev) が正**。ローカル編集は
  再 import で消え、IMPORT_INFO 検査も壊す。
- **生成 TU の `malloc` は per-instance の PSRAM プールへ向く** (`sp_mem_override.h` が
  `-include` で強制注入され、`malloc`→`sp_mem_malloc`→現在インスタンスの sp_ctx バックエンド)。
- **先例あり**: `sp_brk_stack` は既に「インライン配列 → 遅延 malloc ポインタ」化済み
  (`sp_runtime.h` の SP_BRK_STACK。理由は TLS レイアウト税と静的節約)。exc/catch は未対応。

## 対象と回収量 (1 TU)

| シンボル | バイト | 移せるか |
|---|---:|---|
| `sp_exc_stack` (jmp_buf×16) | 4,864 | ○ |
| `sp_catch_stack` (jmp_buf×16) | 4,864 | ○ |
| `sp_dyn_syms` (256) | 1,024 | ○ |
| `sp_bt_buf` (void*×256) | 1,024 | ○ |
| `sp_catch_val` ほか端数 | 〜672 | ○ (大半) |

内蔵に残るのは `sp_exc_top` 等のスカラと数本のポインタ (数十バイト)。**約 11.4KB/TU →
ほぼ 0**。5 TU で約 57KB が内蔵に戻る。

## 方針 A (推奨): `EXT_RAM_BSS_ATTR` で PSRAM .bss へ再配置

- 大物の static 配列に「PSRAM 配置属性」を付けるだけ。**意味論は不変**
  (確保場所が内蔵→PSRAM に変わるだけ。malloc しないので dangling も、instance プール
  予算への影響も無い)。最小リスク。
- `jmp_buf` を PSRAM に置く安全性: setjmp/longjmp は普通のメモリ読み書きで、**DMA も ISR も
  絡まない** (Ruby の begin/rescue は task 文脈)。exc/catch はホットループではないので
  PSRAM の遅延も無視できる (先例 sp_brk_stack が気にしたのは「毎回引く TLS ホット変数の
  レイアウト税」で、これは別物)。
- portable 性の保ち方: 上流 runtime に**既定空のマクロ `SP_TU_BSS`** を足し、大物配列に
  前置する (`#ifndef SP_TU_BSS #define SP_TU_BSS #endif`)。fmrb 側は
  **`sp_mem_override.h` と同じ forced-include** で `SP_TU_BSS = EXT_RAM_BSS_ATTR` を定義する
  (ESP 依存を上流に持ち込まない。host/Linux は空のまま不変)。

## 方針 B: 遅延 malloc (sp_brk_stack と同型)

- インライン配列を「ポインタ + 初回使用時 malloc」に変える。メモリは instance プール
  (PSRAM) から取り、instance 破棄でプールごと解放される。
- 長所: **未使用なら 0、破棄で返る**。短所: **per-instance でポインタを reset** しないと
  次の instance で dangling (先例の sp_brk_stack も同じ注意が要る)。instance プール予算を
  少し増やす必要。MULTI_CTX の malloc override 前提。
- 「PSRAM .bss の常時確保すら惜しい」ほど TU 本数が増えたときの選択肢。今は PSRAM が
  潤沢 (23MB 空き) なので A で十分。

## 推奨

**方針 A**。単純・低リスク・段数維持のまま内蔵ほぼ 0。B は将来 TU が大量になり
「未使用 TU の PSRAM 常駐すら削りたい」場合に。

## 実装場所と検証

- 実装: Spinel fork (fmrb-dev) の `sp_types.h` (`SP_TU_BSS` 定義) と `sp_runtime.h`
  (大物配列に前置) → fmrb へ re-import → `IMPORT_INFO` 更新。fmrb 側は forced-include に
  `SP_TU_BSS=EXT_RAM_BSS_ATTR` を追加。
- 検証: `rake build:esp32` → 本報告の計測手順で map を見て、各生成 TU の内蔵 .bss が
  スカラ数十バイトに落ち、大物が `.ext_ram.bss.*` に移ったことを確認。回帰は
  kernel / editor / desktop / 各 gem の一通り動作 + Guru/abort 0。
- 段数削減 (16→8) とは**直交**。両方やれば PSRAM 側の per-TU も半減できるが、まず A で
  内蔵を回収するのが本筋。
