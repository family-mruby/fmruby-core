# 本流PicoRuby統合 - 作業進捗と残課題

## 日付: 2026-03-21

## 完了した作業

### Step 2: サブモジュール更新前の準備 (完了)
- サブモジュール状態確認: `0f47b6bb` (3.0.1-1630)
- 本流最新確認: `c14aa440` (master)
- scheduler_lock / mrb_execute_proc_synchronously / task_hal.h の存在確認
- 本流がマルチVM (`MRB_TASK_MAX_VMS`) を公式サポートしていることを確認

### Step 3: サブモジュール更新 (完了)
- picorubyサブモジュールを `c14aa440` に更新
- 内部サブモジュールも全て再帰更新
- コミット: `2a13fec`

### Step 4: パッチ再構築 (完了)

#### 削除したパッチ
| ファイル | 理由 |
|----------|------|
| `lib/patch/picoruby-mruby/src/task.c` | 本流の新task.c (scheduler_lock付き) を使用 |
| `lib/patch/picoruby-mruby/vm_cipush_debug.c` | scheduler_lockで不要 |
| `lib/patch/picoruby-mruby/README_vm_cipush_debug.md` | 同上 |
| `lib/patch/compiler/prism_alloc.c` | 本流がmrb_malloc経由に変更 |
| `lib/patch/compiler/prism_tlsf_wrapper.c` | 同上 |

#### 本流ベースに更新したパッチ
| ファイル | 変更内容 |
|----------|----------|
| `lib/patch/picoruby-mruby/src/alloc.c` | 本流ベース + ESTALLOCセクションにマルチVM対応 (fmrb_get/set_current_est) |
| `lib/patch/picoruby-mruby/include/hal.h` | 本流そのまま (task_hal.h関数宣言含む) |
| `lib/patch/picoruby-mruby/mrbgem.rake` | 本流ベース + mruby-io依存を除去 (fmrb-ioとconflictするため) |
| `lib/patch/compiler/prism_xallocator.h` | 本流そのまま |
| `lib/patch/compiler/mruby-compiler2-compile.c` | 本流そのまま (旧mutexパッチは削除) |
| `lib/patch/compiler/mruby-compiler2-mrbgem.rake` | 本流そのまま |
| `lib/patch/picoruby-require/mrbgem.rake` | 本流ベース + picoruby-fmrb-filesystem依存追加 |
| `lib/patch/picoruby-yaml/mrbgem.rake` | 本流ベース + picoruby-fmrb-io依存 |
| `lib/patch/picoruby-sandbox/mrbgem.rake` | 本流そのまま |
| `lib/patch/picoruby-env/ports/posix/env.c` | 本流そのまま |
| `lib/patch/picoruby-mruby/mrbgems/mruby-io/mrblib/file_constants.rb` | 本流そのまま |
| `lib/patch/esp_littlefs/CMakeLists.txt` | 変更なし (差分なし) |

#### 再構築したreplace
| ディレクトリ | 変更内容 |
|-------------|----------|
| `lib/replace/picoruby-machine/` | 本流ベースで全ファイル置き換え |
| `ports/esp32/machine.c` | 本流ベース + mruby用マルチVM tick管理 (g_tick_manager, FreeRTOSタスク) |
| `include/hal.h` | 本流ベース + ESP32用: hal_register_vm, hal_deinit, hal_deinit_by_pool, mrb_set_in_c_funcall |
| `mrbgem.rake` | 本流ベース + io-console依存除去, mruby-io依存除去 |
| `mrblib/kernel.rb` | IO初期化をKernel定義より前に移動 |

#### Rakefile
- 不要になったcpコマンドを削除 (prism_alloc.c, prism_tlsf_wrapper.c, TLSF lib copy)

#### ビルドコンフィグ
| ファイル | 変更内容 |
|----------|----------|
| `lib/add/family_mruby_linux.rb` | hal-posix-task, hal-posix-dir を明示追加 |

## 現在のビルドエラー

### Linux ビルド (`rake build:linux`)

```
GEN   mrblib/*.rb -> build/host/mrblib/mrblib.c
      MRBC mrblib/10error.rb
           mrblib/compar.rb
           ...
/project/.../mrblib/compar.rb:9: Not implemented:
/project/.../mrblib/10error.rb:0:0: generator error, Not implemented:
```

**症状**: `picorbc` (ホストコンパイラ) は正常にビルドされるが、mrblib Rubyファイルのコンパイルで "Not implemented" エラー

**考えられる原因**:

1. **MRB_BASELINE_PROFILE / MRB_CONSTRAINED_BASELINE_PROFILE**
   - 本流のpicoruby-mruby mrbgem.rakeが新たに追加した定義
   - これらがコンパイラの機能を制限している可能性
   - 旧パッチにはこれらの定義がなかった

2. **prism_xallocator.hの変更**
   - 旧: 独自TLSF関数 (prism_malloc等) でメモリ管理
   - 新: mrb_malloc経由 (global_mrb使用) またはlibc malloc
   - ホストビルド (picorbc) でのメモリ管理が変わった影響の可能性

3. **本流mrblib/*.rbの構文変更**
   - サブモジュール更新で mrblib/ 内のRubyファイルが変更された可能性
   - compar.rbの `<=>` 演算子やraise文が新コンパイラで未サポートか

### ESP32 ビルド (`rake build:esp32`)
未実施 (Linuxビルドが先に通る必要がある)

## 相談事項

### 1. mruby-io vs fmrb-io の競合問題

本流の新しいgem構成では多くのgemが `mruby-io` に依存する:
- `hal-posix-io` -> `mruby-io`
- `picoruby-machine` (本流) -> `mruby-io`
- `picoruby-mruby` (本流) -> `mruby-io`

family-mrubyでは `picoruby-fmrb-io` を使用しており、`mruby-io` とconflict宣言されている。
現在のパッチでは全てのmruby-io依存を除去しているが、この方針で問題ないか確認が必要。

### 2. コンパイラエラーの調査方針

"Not implemented" エラーの根本原因を特定するために:
- compar.rb自体が変わったかの確認 (旧バージョンとの比較)
- MRB_BASELINE_PROFILEの影響調査
- picorbcの動作テスト (手動で単純なrbファイルをコンパイル)

### 3. ESP32ビルド固有の課題 (未着手)

ESP32ビルドでは追加で以下が必要と予想:
- `CMakeLists.txt` の PICORUBY_SRCS 更新 (新ファイル追加、削除されたファイルの除去)
- `hal-picoruby-task` gemの解決 (本流mrbgem.rakeがnon-POSIX時に要求)
- ESP32用 `machine.c` の `fmrb_app.h` 依存の解決

## ファイル構成 (現在)

```
lib/patch/
  picoruby-mruby/
    include/hal.h          (本流)
    mrbgem.rake            (本流 + mruby-io依存除去)
    src/alloc.c            (本流 + ESTALLOC multiVM)
    mrbgems/mruby-io/mrblib/file_constants.rb  (本流)
  compiler/
    prism_xallocator.h     (本流)
    mruby-compiler2-compile.c    (本流)
    mruby-compiler2-mrbgem.rake  (本流)
  picoruby-require/mrbgem.rake   (本流 + fmrb-filesystem依存)
  picoruby-yaml/mrbgem.rake      (本流 + fmrb-io依存)
  picoruby-sandbox/mrbgem.rake   (本流)
  picoruby-env/ports/posix/env.c (本流)
  esp_littlefs/CMakeLists.txt    (変更なし)

lib/replace/
  picoruby-machine/              (本流ベース + family-mruby拡張)
    ports/esp32/machine.c        (本流 + マルチVM tick管理)
    include/hal.h                (本流 + ESP32 VM管理関数宣言)
    mrbgem.rake                  (本流 + io-console/mruby-io除去)
    mrblib/kernel.rb             (IO初期化順序変更)
    (その他は全て本流のまま)
```
