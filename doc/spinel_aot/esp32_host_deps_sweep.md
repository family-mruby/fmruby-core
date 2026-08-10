# Spinel runtime のホスト/POSIX 依存 棚卸し (ESP32 移植前チェックリスト)

Spinel runtime は開発ホスト (Linux/POSIX) 前提の facility を随所で使う。これらは
ESP-IDF / newlib / Xtensa では存在しない・動かないため、Phase 5 前に**一括で洗い出し**、
各々を次のいずれかで対処する:

- **backend/ガード追加 (fork)**: 常時リンクされるコア機能。ESP32 用バックエンドを
  差し込む (例: File/Dir の VFS フック) か、include をガードする。
- **リンク対象外 (SRCS 除外)**: 使わないオプション機能。ESP-IDF コンポーネントの
  SRCS に**そもそも入れない** (phase5.md T5-2 の方針)。参照が漏れないことを確認する。

一件ずつ実行時に踏むのを避けるための**網羅版チェックリスト**。per-item の具体的な
対処手順は `phase5.md` の「移植ハザード一覧」/ T5-1・T5-2、fork 修正候補は
`reports/fork_pr_candidates.md` を正とし、本表はその索引 + 網羅確認用。

status: **DONE** (fork/MC で解決済) / **PENDING** (方針あり未着手) /
**SWEEP** (grep で網羅・非リンクを確認する)

## コア (常時リンク — backend/ガードが必要)

| 依存 | 箇所 | ESP32 での問題 | 対処方針 | status |
|---|---|---|---|---|
| File/Dir 生 POSIX (`fopen`/`opendir`/`stat`/`fdopen`) | `sp_io.c` | ESP32 に POSIX FS 無し。FS 依存機能 (launcher scan / icon / file manager / editor 起動) が全滅 | `sp_io` に VFS バックエンドフック追加 (`sp_mem_*` と同型)→ fmruby が `fmrb_hal_file_*` に配線。default は POSIX 直で byte 同一 | **DONE** (2026-08-10 確認: sp_io.h の backend スロット + fmrb_spinel_host.c の hal_open 群で両側実装済み。P4 Spinel desktop の実機 /app スキャン動作が実証。本行の PENDING は更新漏れだった) |
| `isatty` / `ioctl(TIOCGWINSZ)` (`File#tty?` / `#winsize`) | `sp_io.c` `sp_io.h` | ESP32 に tty/ioctl 無し | VFS フックに含めるか、`winsize`→[0,0] / `tty?`→false 固定 | PENDING |
| `__int128` | `sp_time.c` | 32bit (Xtensa) で不可 | `__SIZEOF_INT128__` ガード + double フォールバック | DONE `d9e363ed` |
| `clock_gettime` | `sp_time.c` `sp_runtime.h` | ESP-IDF は提供するが CLOCK 種別/精度を要確認 | newlib/ESP-IDF の clock_gettime を使用、要動作確認 | SWEEP |
| `time()` (乱数 seed) | `sp_random.c` | newlib で通るが RTC 未設定時の値に注意 | seed 源を HAL/ハードRNG へ寄せるか要確認 | SWEEP |
| `<malloc.h>` / `malloc_trim` | `sp_gc.c` | newlib で include が通るか | MC で `malloc_trim` no-op 済。include ガードを fork へ | DONE(no-op)/SWEEP(include) |
| `getenv` (SP_GC_* 等のデバッグ env) | `sp_gc.c` `sp_alloc.c` `sp_ctx.c` 他 | ESP32 は環境変数無し (getenv→NULL) | NULL で無害に既定動作へ落ちることを確認 | SWEEP |
| `sysconf(_SC_PAGESIZE)` (fiber guard/GC) | `sp_gc.c` `sp_fiber.c` | ESP-IDF に sysconf 無し/限定 | fiber 非リンク時は不要。GC 経路で使うなら定数化 | SWEEP |
| 静的 BSS (`SP_GC_STACK_MAX` default 配列 / 生成 TU の static) | `sp_gc.c` / 生成 TU | 32bit で 256KB 等が BSS に残るリスク | MC で root は heap 化 (`root_stack_entries`)。map で BSS 不在を確認 | SWEEP (phase5.md 2,7) |
| `__attribute__((constructor))` | lib 各所 | ESP32 の constructor タイミング問題 | MC で解決済 (`sp_tu_ctx_init`/`sp_instance_create`)。MC でも残る ctor が無いか grep | DONE/SWEEP |

## オプション (未使用 — リンク対象外にして参照漏れを確認)

phase5.md T5-2 の方針: これらのモジュールは ESP-IDF コンポーネントの SRCS に入れない。
入れなければ下記プリミティブは参照されない。**入っていない/参照が漏れていない**ことを
`nm -u` で確認する (SWEEP)。

| モジュール | 使うプリミティブ | 備考 |
|---|---|---|
| fiber | `mmap`/`<sys/mman.h>`、`ucontext`/asm ctx switch、`pthread`、`sysconf` | Xtensa backend 無し。使わない構成が前提。`<sys/mman.h>` は無条件 include のためガード要 (phase5.md 4)。使うなら Xtensa backend 移植 ([[spinel-stack-model]] 参照) |
| net | `socket`/`poll` | `sp_net.c` を除外 |
| system | `system()`/`fork()`/`getenv` | `sp_system.c` を除外 |
| sched/thread | `pthread`/`signal`/`poll`/`clock_gettime` | `sp_sched.c` を除外 (SP_THREADS 無効) |
| crypto | `/dev/urandom`/`getrandom`/`arc4random` | `sp_crypto` を除外、または HAL RNG へ |

## 運用

- grep 種 (再走査用):
  `fopen|opendir|stat|fdopen|isatty|ioctl|mmap|ucontext|pthread|clock_gettime|`
  `gettimeofday|/dev/urandom|getrandom|arc4random|getenv|system\(|fork\(|socket\(|`
  `poll\(|signal\(|sysconf|malloc_trim|__int128|__attribute__\(\(constructor`
- 新しいヒットは上表に登録して status を付ける。fork 修正が要るものは
  `reports/fork_pr_candidates.md` へ起案。
- ESP32 ビルドの `nm -u` ゲート (phase5.md T5-2) で、除外したモジュールのプリミティブが
  未定義参照として残っていないことを機械確認する。
- 関連: `phase5.md` (per-item 対処), `reports/fork_pr_candidates.md`,
  `ruby_writing_constraints.md`。
