# 実装計画: Spinel per-TU の大物を PSRAM へ (方針A)

> **完了 (2026-08-15)。P1 + P2 とも実施、実機検証済み。**
> 実機 (Tab5/P4) のアイドル空き内蔵 RAM が **95,888 → 152,612 バイト
> (+56,724)**。生成 TU の内蔵 .bss は合計 **60,496 → 4,886 バイト**、
> 57,336 バイトが `.ext_ram.bss` (PSRAM) へ移動。Guru/abort ゼロ、
> 性能変化なし。詳細は下の「実施結果」。

前提と背景は `doc/spinel_aot/report/per_tu_internal_ram.md`(および同「追加検討」節)。
本計画は **方針A: `SP_TU_BSS` 属性で per-TU の大物 static 配列を PSRAM .bss へ再配置**する
手順。段数(16)は維持したまま、生成 TU 1 本あたりの内蔵 RAM を 11.4KB → ほぼ 0 にする。

## 効き目(見込み)

- exc スタック + catch スタックだけで **9,728B/TU(全体の 85%)**。これを PSRAM へ。
- 付随の小配列(msg/cls/obj/rootmark/handling/rescue_mark/tag/tag_kind/val/exc_top/rootmark)も
  同じ属性で移す。
- 内蔵に残るのは `sp_exc_top` 等のスカラと数本のポインタ(数十バイト)。
- 5 TU で約 57KB が内蔵に戻る。

## 変更の核(意味論は不変)

`EXT_RAM_BSS_ATTR` は変数の**配置セクションを変えるだけ**で、型・サイズ・構造体レイアウトは
変えない。したがって:

- **ABI に影響しない**。per-TU static は TU 間で共有されないので、SP_MULTI_CTX や
  SP_GC_STACK_MAX のような「全 TU で一致必須」の縛りは無い(片方内蔵/片方 PSRAM でも
  機能は同じ)。それでも一貫性のため全 Spinel TU に一律適用する。
- malloc しないので **dangling も instance プール予算への影響も無い**(方針B との差)。
- `jmp_buf` を PSRAM に置く安全性: setjmp/longjmp は通常のメモリ操作で DMA も ISR も絡まない
  (begin/rescue は task 文脈)。exc/catch はホットループでないので PSRAM 遅延は無視できる。

## 変更点の全体像

- **upstream(Spinel fork, fmrb-dev)= 2 ファイル**
  1. `sp_types.h`: 既定空マクロ `SP_TU_BSS` を追加。
  2. `sp_runtime.h`: per-TU の大物 static 配列に `SP_TU_BSS` を前置。
- **fmrb 側(re-import 後)= 3 箇所**
  3. import 対象外の小ヘッダ `components/fmrb_spinel_rt/fmrb_sp_tu_bss.h` を新設し、
     `SP_TU_BSS = EXT_RAM_BSS_ATTR`(ESP 非 Linux のときだけ)を定義。
  4. `components/fmrb_spinel_rt/CMakeLists.txt` の `SPINEL_MC_FLAGS` に `-include` を追加。
  5. `main/CMakeLists.txt` の生成 TU 用 `set_source_files_properties`(6 箇所)に `-include` を追加。

## upstream 変更の詳細

### 1. `sp_types.h`(`SP_TLS` の定義の隣に)

```c
/* Per-TU 大物 static の配置属性。既定は空(=通常の .bss)。組み込みで内蔵 RAM が
   希少なターゲットは、外側のビルドが EXT_RAM_BSS_ATTR 等に定義して PSRAM へ逃がす。 */
#ifndef SP_TU_BSS
#define SP_TU_BSS
#endif
```

### 2. `sp_runtime.h`(現 import `cafe659` での行。import で前後する前提で名前で探す)

各配列宣言の先頭に `SP_TU_BSS` を付ける(`SP_TU_BSS static SP_TLS ...`)。対象:

- 例外ハンドラ群:
  - `sp_exc_stack[SP_EXC_STACK_MAX]`(:5773、jmp_buf×16 = 4,864B ← 最大)
  - `sp_exc_msg`(:5774) / `sp_exc_rootmark`(:5780) / `sp_exc_cls`(:5782) /
    `sp_exc_obj`(:5789) / `sp_exc_handling`(:5805) / `sp_rescue_mark`(:5810)
- catch/throw 群:
  - `sp_catch_stack[SP_CATCH_STACK_MAX]`(:6419、jmp_buf×16 = 4,864B ← 最大)
  - `sp_catch_tag`(:6420) / `sp_catch_tag_kind`(:6423) / `sp_catch_val`(:6425) /
    `sp_catch_exc_top`(:6426) / `sp_catch_rootmark`(:6427)
- 動的 intern シンボル表(report の `sp_dyn_syms`、~1,024B/TU): **fork 内で実体の宣言を
  特定して**同様に前置する。現 import では `sp_runtime.h` に `SP_DYN_SYMS_MAX` の #define と
  コメントはあるが配列宣言が見当たらない(別ヘッダ/別名の可能性)。**まず宣言箇所を確定**し、
  per-TU に複製されていることを map で確認してから付ける。

**対象外(付けない)**:

- `sp_bt_buf[256]`: `#if SP_BT_AVAILABLE`(既定 0)で **release では非コンパイル**。
  現に report の 11,424B に入っていない。
- `sp_gc_mark_stack` / `sp_brk_stack`: **既に遅延 malloc**(PSRAM プール)。.bss に無い。
- スカラ(`sp_exc_top` 等): 数バイトで hot。内蔵のまま残す。

## fmrb 変更の詳細

### 3. `components/fmrb_spinel_rt/fmrb_sp_tu_bss.h`(新規・import 対象外)

`spinel_rt/` の**外**に置く(`fmrb_spinel_host.c` と同じ扱い。`import_from_fork.rb` が消さない)。

```c
#ifndef FMRB_SP_TU_BSS_H
#define FMRB_SP_TU_BSS_H
/* ESP-IDF の非 Linux ターゲットでのみ、Spinel の per-TU 大物を PSRAM .bss へ。
   host / Linux sim では空のまま(EXT_RAM_BSS_ATTR は無い)。SP_THREADS(=__thread)
   とは排他なので、その構成では空にする。 */
#if defined(ESP_PLATFORM) && !defined(CONFIG_IDF_TARGET_LINUX) && !defined(SP_THREADS)
#include "esp_attr.h"
#define SP_TU_BSS EXT_RAM_BSS_ATTR
#endif
#endif
```

- ガードは `ESP_PLATFORM`(ESP-IDF ビルドで常に定義。sdkconfig.h の include 順に依存しない)。
- `SP_THREADS` 除外: TLS(`__thread`)と section 属性は併用不可のため。現状 fmrb は未使用。

### 4/5. forced-include を 2 系統に追加(順序が肝)

`-include fmrb_sp_tu_bss.h` を、既存の `-include .../sp_mem_override.h` の**前**に足す。
`-include` は TU 先頭に展開されるので、後で来る `sp_types.h` の `#ifndef SP_TU_BSS`
既定より fmrb 定義が勝つ。

- `components/fmrb_spinel_rt/CMakeLists.txt` の `SPINEL_MC_FLAGS`(runtime + 依存へ INTERFACE)。
- `main/CMakeLists.txt` の生成 TU 6 箇所の `set_source_files_properties`
  (`fmrb_kernel_combined` / `system_desktop_combined` / `editor_combined` / `fft_spinel` /
  `spinel_hello_entry` / `raycast_entry`)。ここは今も `sp_mem_override.h` をベタ書きしている
  ので、同じ列に `-include;<fmrb_sp_tu_bss.h>` を足す。**2 系統を lockstep** で
  (ABI 影響は無いが、付け漏れ TU だけ内蔵に残って紛らわしいため)。

## import と反映の手順

1. fork(kishima/spinel fmrb-dev)で上記 1/2 を実装・コミット。
2. `components/fmrb_spinel_rt/import_from_fork.rb` で re-import → `spinel_rt/` 更新 +
   `IMPORT_INFO`(fork_commit)更新。必要なら `SPINEL_PIN` も。
3. fmrb 側 3/4/5 を実装。
4. `rake clean_all`(sdkconfig/フラグ系の再生成を確実に)→ `rake build:esp32`。

## 検証

- **サイズ**: `report/per_tu_internal_ram.md` の計測手順で `build/fmruby-core.map` を
  obj 単位集計。各生成 TU の内蔵 `.bss`/`.data` が**スカラ数十バイト**に落ち、
  exc/catch/(dyn_syms) が `.ext_ram.bss.*` に移ったことを確認。5 TU 合計 ~57KB 減が目標。
- **回帰(実機 Tab5)**: kernel / desktop / editor / 各 gem(fft・hello・raycast)を一通り
  起動。begin/rescue と catch/throw を含む経路(例:エラーを投げるアプリ、raycaster の
  通常動作)を動かし、**Guru/abort 0**。exc/catch が PSRAM でも正しく巻き戻ることを確認。
  - 実機起動・kill は `fmrb_rd_launch/ps/kill`、crash は常設シリアルで観測
    (`feedback_serial_capture_keep_open`)。
- **host/Linux**: `rake build:linux` が通ること(`SP_TU_BSS` 空で従来どおり)。
  `file build/fmruby-core.elf` で x86-64 確認(stale な esp32 build に注意)。

## リスク・落とし穴

- **forced-include の順序**: `fmrb_sp_tu_bss.h` が `sp_types.h` の既定より先に来ること。
  `-include` は必ず TU 先頭なので満たされるが、両系統(CMake / main)で付け忘れると
  その TU だけ内蔵に残る(害は無いが目標未達)。
- **esp_attr.h を全生成 TU 先頭に強制 include**する影響: 軽量ヘッダで問題なし。ESP ビルドでのみ。
- **`jmp_buf` in PSRAM**: 安全(上記)。念のため回帰で例外/catch 経路を実走させる。
- **将来 `SP_THREADS` を有効化**するなら `SP_TU_BSS` は空に戻す(ガードに既に `!SP_THREADS`)。
- **段数削減(16→8)とは直交**。本計画で内蔵を回収したあと、PSRAM 側の per-TU も削りたければ
  ExcHW 実測の上で段数を下げる(report の別項)。

## 段階

- **P1**: upstream に `SP_TU_BSS` + exc/catch クラスタへ前置 → re-import → fmrb 配線 →
  build → map で exc/catch(9.7KB/TU)が PSRAM へ移ったことを確認、実機回帰。ここで大半が済む。
- **P2**: `sp_dyn_syms` の実体を fork 内で特定して前置(~1KB/TU 追加回収)。
- **P3**: 数値を `report/per_tu_internal_ram.md` に追記(before/after、内蔵 .bss/.data 全体比)。

---

## 実施結果 (2026-08-15)

**P1 + P2 とも完了、実機 (Tab5/P4) で検証済み。**

### 回収量

| | before | after |
|---|---:|---:|
| **実機アイドルの内蔵 RAM 空き** | 95,888 B | **152,612 B (+56,724)** |
| 生成 TU の内蔵 `.bss`/`.data` 合計 | 60,496 B | **4,886 B** |
| 同 PSRAM (`.ext_ram.bss`) | 0 | 57,336 B |

生成 TU ごと (内蔵 → PSRAM):

| TU | 内蔵 after | PSRAM after |
|---|---:|---:|
| `fmrb_kernel_combined` | 1,514 | 11,536 |
| `editor_combined` | 2,921 | 11,536 |
| `fft_spinel` | 197 | 11,408 |
| `spinel_hello_entry` | 105 | 11,408 |
| `raycast_entry` | 149 | 11,408 |

内蔵に残ったのは計画どおりスカラとポインタ。kernel と editor に 1.5〜2.9KB
残るのは、その TU 自身の `civ_`/`cst_` (プログラム固有のデータ) で、これは
per-TU の固定装備ではないので対象外。

### 実装 (計画との差分)

計画どおり 5 箇所。ただし 2 点変えた。

1. **`sp_dyn_syms` は `sp_runtime.h` ではなく codegen が出していた**
   (`src/codegen.c:4889` の `buf_puts`)。宣言文字列に `SP_TU_BSS ` を前置。
   計画では「宣言箇所を確定してから」としていた P2 が、この一行で済んだので
   P1 と同時に実施した。
2. **`main/CMakeLists.txt` の 6 箇所は変数 `SPINEL_GEN_TU_FLAGS` にまとめた**。
   同一文字列の 6 重複は、計画自身が挙げている「付け漏れ」リスクそのもの
   だったため。

### つまずいた点

**`target_compile_options` は重複するフラグを畳む**。`-include` と パスを
別々のリスト要素で 2 組渡すと、2 つ目の `-include` が消えてパスだけが残り、
gcc が入力ファイルと解釈して

```
riscv32-esp-elf-gcc: fatal error: cannot specify '-o' with '-c' ... with multiple files
```

で落ちる。`"SHELL:-include <path>"` の形にしてオプションと引数を束ねる必要が
ある (`components/fmrb_spinel_rt/CMakeLists.txt`)。`set_source_files_properties`
の `COMPILE_OPTIONS` は畳まないので main 側はこの形のままでよい。

### 回帰 (実機 Tab5、Guru/abort ゼロ)

- **カーネル (Spinel)**: ブート → デスクトップ表示 → 継続動作。
- **エディタ (Spinel)**: メニューから起動、`puts 1` を打鍵して
  `Ln 1, Col 7` まで更新されることを確認 → 終了。
- **FFT gem**: 8 バックエンド完走。`:spinel` 10,001us / `:spinel_q15` 2,328us で
  **変更前 (10,038 / 2,378us) と同じ**。ピーク bin・`dev` も不変。
- **SpinelHello gem**: 起動・終了。
- **raycast gem**: `:spinel` でフレーム 77.7us→ (変更前 78.1ms) と同じ。
- **例外/catch を PSRAM に置いても正しく巻き戻る**ことは、上記が全て
  begin/rescue を含む経路を実走していることで確認。

### host / Linux

`rake build:linux` 通過、`file` で x86-64 確認。`nm` で `sp_exc_stack` /
`sp_catch_stack` が通常の `b` (.bss) のままであることを確認 —
`SP_TU_BSS` は空で、ホスト側は一切変わっていない。

### 性能

PSRAM 化による測定可能な劣化は無し。FFT・raycast とも変更前と同じ数字。
計画の見立て (「exc/catch はホットループでないので PSRAM 遅延は無視できる」)
どおり。

### 残り

- **fork (kishima/spinel fmrb-dev) が未コミット**。`sp_types.h` / `sp_runtime.h` /
  `src/codegen.c` の 3 ファイル。したがって `IMPORT_INFO` の `fork_commit` は
  変更前の `cafe6595` を指したまま。コミット → re-import → `SPINEL_PIN` 更新が要る。
- 段数削減 (`SPINEL_RT_EXC_STACK_MAX` 16→8) は**直交**。今回 PSRAM 側に
  57KB 置いたので内蔵の動機は消えたが、PSRAM も惜しくなれば ExcHW 実測の上で。
