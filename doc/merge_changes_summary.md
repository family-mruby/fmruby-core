# 本流PicoRuby統合 - 実施内容一覧

## 日付: 2026-03-21

## 1. サブモジュール更新

### 1.1 picorubyサブモジュール更新
- `components/picoruby-esp32/picoruby` を `0f47b6bb` -> `c14aa440` (origin/master最新) に更新
- 内部サブモジュールも全て再帰更新 (mruby-compiler2, prism, mruby, mrubyc等)
- コミット: `2a13fec`

### 1.2 バックアップ作成
- `lib/patch.backup.20260321/` - 旧パッチのバックアップ
- `lib/replace.backup.20260321/` - 旧replaceのバックアップ
- `.gitignore` に `lib/*.backup.*` を追加

## 2. lib/replace/picoruby-machine/ の再構築

本流のpicoruby-machineを丸ごとコピーし、family-mruby固有の変更のみ上書き。

### 2.1 ports/esp32/machine.c
- 本流ベース (esp_timer, USB stubs, RTC, Machine_* 関数群)
- mruby用マルチVM tick管理を追加: `g_tick_manager`, `mruby_tick_task` (FreeRTOSタスク)
- `hal_register_vm()`, `hal_deinit()`, `hal_deinit_by_pool()` を追加
- `mrb_set_in_c_funcall()` を追加
- `mrb_task_enable_irq()` / `mrb_task_disable_irq()` をVM単位のirqフラグ管理で実装
- task HAL関数を追加: `mrb_hal_task_init()`, `mrb_hal_task_final()`, `mrb_hal_task_idle_cpu()`, `mrb_hal_task_sleep_us()`
- `Machine_get_config_int()` を追加 (ブート診断用)
- prism allocator mutex を追加: `fmrb_prism_lock()` / `fmrb_prism_unlock()` (FreeRTOS semaphore)

### 2.2 ports/posix/hal.c
- 本流ベース (SIGALRM, sigprocmask)
- `mrb_task_enable_irq()` / `mrb_task_disable_irq()` を削除 (hal-posix-taskが提供するため多重定義回避)
- family-mruby独自関数のPOSIXスタブを追加: `hal_register_vm()`, `hal_deinit()`, `hal_deinit_by_pool()`, `mrb_set_in_c_funcall()`
- prism allocator mutex を追加: `fmrb_prism_lock()` / `fmrb_prism_unlock()` (pthread)

### 2.3 ports/posix/machine.c
- 本流そのまま
- `Machine_get_config_int()` を追加

### 2.4 include/hal.h
- 本流ベース (HAL_PORTING_H_, PICORB_VM_MRUBY/MRUBYC条件分岐, C++ガード)
- `hal_idle_cpu()` 宣言を修正 (mruby用に `mrb_state*` 引数)
- `hal_enable_irq()` / `hal_disable_irq()` マクロを追加
- family-mruby独自関数宣言を追加: `hal_register_vm()`, `hal_deinit()`, `hal_deinit_by_pool()`, `mrb_set_in_c_funcall()`, `MRB_C_FUNCALL_*`, `MRB_*_IRQ` 定数
- task HAL関数宣言を追加: `mrb_hal_task_init()`, `mrb_hal_task_final()`, `mrb_hal_task_idle_cpu()`, `mrb_hal_task_sleep_us()`

### 2.5 mrbgem.rake
- 本流ベース
- `picoruby-io-console` 依存を除去 (fmrb-io使用)
- `mruby-io` 依存を除去 (fmrb-ioとconflict)
- `include_paths` に mruby-task/include を追加 (task.h, task_hal.h 参照用)
- `include_paths` に picoruby-mruby/lib/mruby/include と picoruby-mruby/include を追加

### 2.6 mrblib/kernel.rb
- IO初期化 (STDIN/STDOUT/STDERR) をKernelモジュール定義の前に移動
- `require 'io/console'` を除去 (fmrb-io使用)

### 2.7 src/mruby/machine.c
- `picoruby-io-console/include/io-console.h` のincludeを `FMRB_NO_IO_CONSOLE` ガードで囲む
- `io_raw_bang()` / `io_cooked_bang()` 呼び出しを同ガードで囲む

### 2.8 その他
- 本流の新規ファイルを含む: `README.md`, `ringbuffer.h`, `machine.rb`, `ports/common/machine.c`, `src/mruby/machine.c`, `src/mrubyc/machine.c`, `CANONICAL_BUFFER_MEMO.md`

## 3. lib/patch/ の再構築

### 3.1 削除したパッチ
- `lib/patch/picoruby-mruby/src/task.c` - 本流の新task.c (scheduler_lock付き) を使用
- `lib/patch/picoruby-mruby/vm_cipush_debug.c` - scheduler_lockで不要
- `lib/patch/picoruby-mruby/README_vm_cipush_debug.md` - 同上
- `lib/patch/compiler/prism_alloc.c` (旧TLSF版) - 新estalloc版に置換
- `lib/patch/compiler/prism_tlsf_wrapper.c` - TLSF廃止

### 3.2 本流ベースに更新したパッチ
- `lib/patch/picoruby-mruby/include/hal.h` - 本流そのまま (task HAL関数宣言含む)
- `lib/patch/picoruby-mruby/mrbgem.rake` - 本流ベース + mruby-io依存除去
- `lib/patch/picoruby-mruby/src/alloc.c` - 本流ベース (TLSF/O1HEAP/TINYALLOC/ESTALLOC/DEFAULT全対応) + ESTALLOCセクションにfamily-mrubyマルチVM対応 (`fmrb_get/set_current_est`, `mrb_get_estalloc_stats`)
- `lib/patch/compiler/prism_xallocator.h` - xmallocをfmrb_prism_malloc方式に変更、mrc_mallocは本流のまま
- `lib/patch/compiler/mruby-compiler2-compile.c` - 本流そのまま
- `lib/patch/compiler/mruby-compiler2-mrbgem.rake` - 本流ベース + prism_alloc.cのビルド設定追加
- `lib/patch/picoruby-require/mrbgem.rake` - 本流ベース + picoruby-fmrb-filesystem依存追加
- `lib/patch/picoruby-yaml/mrbgem.rake` - 本流ベース + picoruby-fmrb-io依存
- `lib/patch/picoruby-sandbox/mrbgem.rake` - 本流そのまま
- `lib/patch/picoruby-env/ports/posix/env.c` - 本流そのまま
- `lib/patch/picoruby-mruby/mrbgems/mruby-io/mrblib/file_constants.rb` - 本流そのまま

### 3.3 新規作成したパッチ
- `lib/patch/compiler/prism_alloc.c` - prism専用estallocプール + mutex (fmrb_prism_lock/unlock extern方式)
- `lib/patch/picoruby-mruby/src/file_ext.c` - 空スタブ (本流file_ext.cがmruby-ioに依存するため)

### 3.4 変更なしのパッチ
- `lib/patch/esp_littlefs/CMakeLists.txt` - 差分なし

## 4. ビルド設定の変更

### 4.1 lib/add/family_mruby_linux.rb
- `MRuby::Build.new` -> `MRuby::CrossBuild.new('family-mruby-linux')` に変更 (picorbcの分離)
- `cc.command`, `linker.command`, `archiver.command`, `cc.host_command` をgccに明示指定
- `MRB_64BIT` 定義を削除 (本流にない)
- `FMRB_NO_IO_CONSOLE` 定義を追加
- `conf.gembox "family_mruby"` を使用
- POSIX HAL gem (`hal-posix-task`, `hal-posix-dir`) を明示追加
- mruby拡張gem群をbuild_configに移動 (gemboxから分離)

### 4.2 lib/add/family_mruby_esp32.rb
- `ESP32_PLATFORM` 定義を追加
- `FMRB_NO_IO_CONSOLE` 定義を追加
- `MRB_INT32` を `MRB_INT64` に変更 (R2P2-ESP32に合わせて)
- `MRBC_*` 定義を削除 (不要)
- mruby拡張gem群をbuild_configに移動 (gemboxから分離)

### 4.3 lib/add/family_mruby.gembox
- `mruby-compiler2` を追加 (ランタイムコンパイル用)
- mruby拡張gem群をbuild_configに移動 (重複排除)
- `picoruby-io-console` をコメントアウト維持

## 5. components/picoruby-esp32/CMakeLists.txt の変更

### 5.1 MRUBY_CONFIG パス
- Linuxビルド: `"family_mruby_linux"` -> `"${CMAKE_SOURCE_DIR}/lib/add/family_mruby_linux.rb"` (CrossBuild用フルパス)
- ESP32ビルド: 同様にフルパス指定
- ビルドディレクトリ: `host` -> `family-mruby-linux` (CrossBuild名に合わせて)

### 5.2 PICORUBY_SRCS (Linux)
- `ports/common/machine.c` を追加

### 5.3 PICORUBY_SRCS (ESP32)
- `ports/esp32/hal.c` を削除 (machine.cに統合)
- `ports/common/machine.c` を追加
- `ports/esp32/machine.c` は `src/machine.c` も含む形で維持
- `picoruby-require/ports/esp32/platform.c` を追加

### 5.4 INCLUDE_DIRS
- `picoruby-mruby/lib/mruby/mrbgems/mruby-task/include` を追加 (task.h参照用)

## 6. family-mruby独自コード (main/, lib/add/) の変更

### 6.1 main/app/fmrb_app.c
- `mrb_tasks_run()` -> `mrb_task_run()` (本流API名変更に追従)

### 6.2 lib/add/picoruby-fmrb-app/ports/esp32/app.c
- `#include <mruby/array.h>` を追加
- `mrb_task_is_switching()` ガード + `mrb_funcall()` + CI stackリーク検出/復旧コード全体を、`mrb_execute_proc_synchronously()` に置き換え

### 6.3 lib/add/picoruby-fmrb-kernel/ports/esp32/kernel.c
- `#include <mruby/array.h>` を追加
- `#include "hal.h"` と `#include "task.h"` を追加
- `mrb_task_is_switching()` ガード + `mrb_funcall()` + CI stackリーク検出/復旧コード全体を、`mrb_execute_proc_synchronously()` に置き換え

## 7. Rakefile の変更

### 7.1 パッチ適用ロジック
- `prism_alloc.c` のコピーコマンドを追加
- `prism_tlsf_wrapper.c` のコピーコマンドを削除
- TLSFライブラリコピーコマンドを削除 (mkdir + cp tlsf.c/tlsf.h)
- `picoruby-machine/include/hal.h` -> `picoruby-mruby/include/` コピーを削除 (不要)

## 8. ドキュメント

- `doc/upstream_merge_plan.md` - Step 2調査ログ、パッチ削減分析を追記
- `doc/merge_progress.md` - 作業進捗と残課題の記録
- `doc/merge_changes_summary.md` - 本ファイル (実施内容一覧)

## ビルド結果

- `rake clean_all build:linux` - 成功 (fmruby-core.elf)
- `rake clean_all build:esp32` - 成功 (fmruby-core.bin, 1.5MB, 27%空き)
