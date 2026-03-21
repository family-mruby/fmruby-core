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

3. **`[]=` (IndexAndWriteNode) がpicorbcで未実装** - 原因特定済み
   - `a[0] = 1` 形式のインデックス代入がコンパイルエラー
   - **原因**: family-mrubyの `family_mruby_linux.rb` が `MRuby::Build.new` + `conf.microruby` を使用
   - `conf.microruby` が `PICORB_VM_MRUBY` を定義 -> picorbcも **mrubyターゲットモード** でビルドされる
   - R2P2-ESP32ではpicorbcは `picorbc.rb` で独立ビルドされ、`PICORB_VM_MRUBY` なし (スタンドアロンモード)
   - R2P2-ESP32ではESP32ターゲットに `MRuby::CrossBuild.new` を使用 -> picorbcとターゲットが分離
   - **対応策**: family-mrubyのビルド設定をCrossBuild方式に変更するか、picorbcが正しくスタンドアロンでビルドされるよう設定を見直す

### Linux ビルド - リンクエラー残り3件 (大部分解消)

#### 解消済みの問題
- picorbcの `[]=` コンパイルエラー -> CrossBuild方式に移行で解消
- 多重定義 (`mrb_task_enable/disable_irq`) -> ports/posix/hal.cから削除
- `Machine_get_config_int` -> posix/machine.c, esp32/machine.cにスタブ追加
- `mrc_*` / `pm_*` 未定義 -> gemboxに mruby-compiler2 追加で解消
- `global_mrb` / prism allocator問題 -> prism専用estallocプール方式に変更
- `io_raw_bang` / `io_cooked_bang` -> `FMRB_NO_IO_CONSOLE` ガードで解消
- `hal_register_vm` 等 -> POSIX用スタブ追加

#### 残りのリンクエラー (3件)

##### 1. `mrb_io_fileno` (file_ext.c)
- picoruby-mruby/src/file_ext.c が `mrb_io_fileno()` を呼ぶ
- これは mruby-io gem が提供する関数
- family-mrubyでは mruby-io を使わない (fmrb-io使用)
- 対策案: file_ext.c をパッチするか、fmrb-io側にスタブを追加

##### 2. `mrb_task_is_switching` (app.c, kernel.c)
- 旧task.cにあったAPI。本流task.cでは削除されている
- family-mruby独自コードの app.c (L340,348) と kernel.c (L108,120) で使用
- 用途: HIDイベントディスパッチ前にタスク切り替え中かチェックする
  - `switching_` フラグが立っている間はmrb_funcallを避ける (CI stackリーク防止)
- **本流の scheduler_lock で代替可能** だが、呼び出し方が異なる
- 対策案:
  - A) 本流task.cに `mrb_task_is_switching()` 互換関数を追加 (パッチ)
  - B) app.c/kernel.c を `mrb_execute_proc_synchronously()` 方式に書き換え
  - C) 当面スタブ (常にfalse返却) で通し、後で本対応

##### 3. `mrb_io_fileno` の補足
- file_ext.c は picoruby-mruby gem の一部 (submodule内)
- 直接編集不可。パッチで対応する必要がある

### Linux ビルド - 旧リンクフェーズ到達記録

CrossBuild方式への移行で picorbc `[]=` 問題は解消。Cソースのコンパイルも全て通過。
リンクフェーズで以下のエラー:

#### 1. 多重定義
- `mrb_task_enable_irq` / `mrb_task_disable_irq` が `task_hal.o` と `hal.o` の両方に定義
- 原因: hal-posix-task (task_hal.c) と picoruby-machine (ports/posix/hal.c) の両方で実装
- 対策: picoruby-machine/ports/posix/hal.c から irq関数を削除 (hal-posix-taskに委譲)

#### 2. undefined reference (family-mruby独自)
- `hal_register_vm` - POSIX用のhal.cに実装がない (ESP32用のみ)
- `Machine_get_config_int` - 未実装関数
- 対策: POSIX用のスタブ実装が必要

#### 3. undefined reference (本流API)
- `mrc_ccontext_new`, `mrc_ccontext_free`, `mrc_load_string_cxt`, `mrc_irep_free`
  - mruby-compiler2のAPI。libmruby.aにリンクされていない可能性
- `pm_constant_pool_id_to_constant`, `pm_options_free`
  - prismライブラリのAPI。リンク順序またはライブラリ不足
- `io_raw_bang`, `io_cooked_bang`
  - picoruby-io-consoleのAPI。fmrb-ioでは提供されていない

#### 4. io_raw_bang / io_cooked_bang 問題
- 本流のsrc/mruby/machine.cが `io_raw_bang` / `io_cooked_bang` を呼ぶ
- これらは `picoruby-io-console` が提供する関数
- family-mrubyでは `picoruby-io-console` を使わない (fmrb-io使用)
- 対策: src/mruby/machine.cにスタブを追加するか、条件分岐でガード

### ESP32 ビルド (`rake build:esp32`)
未実施 (Linuxビルドが先に通る必要がある)

## 追加発見: ビルド設定の追従不足

### 本流のビルド設定構成変更

本流では gembox とビルド設定が大幅にリファクタリングされている:

#### gembox構成 (本流)
- `minimum.gembox` - compiler2, mruby/mrubyc VM選択
- `core.gembox` - require, machine, picorubyvm, time + POSIX/組込み分岐
- `mruby-posix.gembox` - POSIX HAL群 (hal-posix-task, hal-posix-dir, hal-posix-io, mruby-io等)
- `stdlib.gembox`, `shell.gembox`, `networking.gembox` 等

#### マクロリネーム
- `MRB_INT64` -> `PICORB_INT64` (ただしmruby内部は `MRB_INT64` のまま)
- `MRB_64BIT` は本流設定に存在しない
- `PICORUBY_*` -> `PICORB_*` 全般

#### conf.microruby
本流では `conf.microruby` メソッドが使用されており、内部で `PICORB_VM_MRUBY` 等の定義を設定する。
family_mruby_linux.rb でも使用済みだが、定義の整合性確認が必要。

### family_mruby_linux.rb で必要な変更

現在の定義:
```ruby
conf.cc.defines << "MRB_64BIT"       # 本流にない
conf.cc.defines << "MRB_INT64"       # 本流では不要? (mrbgem.rakeで設定)
```

本流の microruby.rb との差分:
- `MRB_UTF8_STRING` が不足
- `PICORB_INT64` の使用を検討

### family_mruby.gembox で必要な変更

- `picoruby-io-console` のコメントアウトは正しい (fmrb-io使用)
- `picoruby-picorubyvm` が本流core.gemboxにあるが未追加 (存在するか要確認)
- `picoruby-time` が本流core.gemboxにあるが未追加 (既存gemか要確認)

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
