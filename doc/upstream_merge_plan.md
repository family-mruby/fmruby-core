# 本流PicoRuby統合計画

## 概要

components/picoruby-esp32/picoruby サブモジュールを最新版に更新し、family-mruby独自の変更を新しいpatchとして再適用する。

## 背景

### 現在の問題
- family-mrubyは古いmrubyc由来のtask.c実装を使用(967行)
- 本流PicoRubyは新しいtask.c実装(1573行)に移行済み
- 本流には`scheduler_lock`や`mrb_execute_proc_synchronously()`等、レースコンディション対策機能が実装済み
- [fix_shell_crash_race_condition.md](fix_shell_crash_race_condition.md)で指摘されたCI stackリーク問題は、本流の機能を使えば解決可能

### 本流実装の優位性
1. **scheduler_lock機能**: 同期実行中はタスク切り替えを完全ブロック
2. **mrb_execute_proc_synchronously()**: イベントハンドラ等を安全に実行
3. **C function境界チェック**: sleepがC関数内で呼ばれた場合のフォールバック
4. **保守性**: 上流の改善を継続的に取り込める

参照: `/home/kishima/fmrb/family-mruby/fmruby-core/tmp/picoruby/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-task/src/task.c`

## 実行計画

### Step 1: 現在のパッチ内容の把握 ✅完了

#### パッチ適用箇所 (Rakefile より)

以下のファイル/ディレクトリが`lib/patch`または`lib/replace`から上書きコピーされている:

##### 1. picoruby-mruby (タスクシステム)
```bash
cp -rf lib/patch/picoruby-mruby components/picoruby-esp32/picoruby/mrbgems/
```

含まれるファイル:
- **src/task.c** - 古いmrubyc実装(967行) ★最重要★
- **include/hal.h** - HAL抽象化インターフェース
- src/alloc.c - メモリアロケータ
- vm_cipush_debug.c - CI stackデバッグ機能

##### 2. picoruby-machine (HAL実装)
```bash
cp -rf lib/replace/picoruby-machine components/picoruby-esp32/picoruby/mrbgems/
cp -f lib/replace/picoruby-machine/include/hal.h components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/include/
```

含まれるファイル:
- **ports/esp32/hal.c** - マルチVM対応tick実装 ★重要★
- ports/posix/hal.c - POSIX用HAL
- ports/rp2040/machine.c - RaspberryPi Pico用

##### 3. mruby-compiler2
```bash
cp -f lib/patch/compiler/prism_xallocator.h components/picoruby-esp32/picoruby/mrbgems/mruby-compiler2/include/
cp -f lib/patch/compiler/prism_alloc.c components/picoruby-esp32/picoruby/mrbgems/mruby-compiler2/lib/
cp -f lib/patch/compiler/prism_tlsf_wrapper.c components/picoruby-esp32/picoruby/mrbgems/mruby-compiler2/lib/
cp -f lib/patch/compiler/mruby-compiler2-compile.c components/picoruby-esp32/picoruby/mrbgems/mruby-compiler2/src/compile.c
```

##### 4. その他
- picoruby-env/ports/posix/env.c
- esp_littlefs/CMakeLists.txt
- mrbgem.rake ファイル群 (picoruby-require, picoruby-yaml, picoruby-sandbox)

#### family-mruby独自機能 (要保持)

以下の独自実装は本流統合後も維持する必要がある:

##### hal.c (ESP32) - マルチVM対応
```c
// lib/replace/picoruby-machine/ports/esp32/hal.c

typedef struct {
    mrb_state *mrb;
    int active;        // 1=in use, 0=unused
    int in_c_funcall;  // 0=MRB_C_FUNCALL_EXIT  1=MRB_C_FUNCALL_ENTER
    int irq;           // 0=MRB_ENABLE_IRQ  1=MRB_DISABLE_IRQ
} mrb_vm_entry_t;

static struct {
    mrb_vm_entry_t vms[MAX_MRB_VMS];  // 最大16VM
    SemaphoreHandle_t mutex;
    TaskHandle_t tick_task_handle;
} g_tick_manager;

// FreeRTOSタスクとして定期的に全VMにtickを配信
static void mruby_tick_task(void* arg) {
    while (1) {
        vTaskDelay(tick_interval);

        for (int i = 0; i < MAX_MRB_VMS; i++) {
            if (g_tick_manager.vms[i].active && g_tick_manager.vms[i].mrb) {
                if (MRB_C_FUNCALL_EXIT == g_tick_manager.vms[i].in_c_funcall &&
                    MRB_ENABLE_IRQ == g_tick_manager.vms[i].irq) {
                    mrb_tick(g_tick_manager.vms[i].mrb);  // ★外部から状態変更★
                }
            }
        }
    }
}
```

**問題点**: これが[fix_shell_crash_race_condition.md](fix_shell_crash_race_condition.md)で指摘されたレースコンディションの原因

**本流での対応**: 本流では各VMが自分のコンテキストで`mrb_tick`を呼ぶ想定(要確認)

### Step 2: サブモジュール更新前の準備

#### 2.1 現在のサブモジュール状態確認

```bash
cd /home/kishima/fmrb/family-mruby/fmruby-core
git submodule status
```

現在のコミットハッシュを記録:
```bash
cd components/picoruby-esp32/picoruby
git log --oneline -1 > /tmp/current_picoruby_version.txt
```

#### 2.2 本流の最新状態確認

```bash
cd /home/kishima/fmrb/investigate/picoruby
git log --oneline -20 > /tmp/upstream_recent_commits.txt
git branch -a > /tmp/upstream_branches.txt
```

#### 2.3 重要な変更履歴を調査

特に以下の機能がいつ追加されたか:
- `scheduler_lock`メカニズム
- `mrb_execute_proc_synchronously()`
- C function境界チェック(`ci->cci`チェック)

```bash
cd /home/kishima/fmrb/investigate/picoruby
git log --all --grep="scheduler_lock\|execute_proc_synchronously\|cci" --oneline -20
```

### Step 3: サブモジュール更新実行

#### 3.1 バックアップ作成

```bash
cd /home/kishima/fmrb/family-mruby/fmruby-core
# 現在のlib/patchをバックアップ
cp -r lib/patch lib/patch.backup.$(date +%Y%m%d)
cp -r lib/replace lib/replace.backup.$(date +%Y%m%d)

# 現在のcomponentsをバックアップ
tar czf ../backup_components_$(date +%Y%m%d_%H%M%S).tar.gz components/
```

#### 3.2 サブモジュール更新

```bash
cd components/picoruby-esp32/picoruby
git fetch origin
git log --oneline HEAD..origin/main  # 差分確認

# 最新版にチェックアウト
git checkout origin/main
# または特定のタグ/コミット
# git checkout <commit-hash>

cd ../../..
git add components/picoruby-esp32/picoruby
git commit -m "Update picoruby submodule to latest version"
```

### Step 4: 新しいpatch作成と適用

#### 4.1 本流との差分分析

更新後の本流task.cとfamily-mrubyで必要な機能の差分を確認:

```bash
# 本流のtask.cを確認
cat components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-task/src/task.c

# 必要な機能:
# 1. scheduler_lock - あるか?
# 2. mrb_execute_proc_synchronously() - あるか?
# 3. HALインターフェース - hal_register_vm() が必要
```

#### 4.2 HAL拡張の実装方針

**方針A: 本流のHALを拡張**
- 本流の`task_hal.h`を確認
- マルチVM登録機能を追加
- `mrb_tick`を外部から呼ぶ代わりに、メッセージングを検討

**方針B: 独自HAL実装を維持**
- `lib/replace/picoruby-machine/ports/esp32/hal.c`を継続使用
- ただし`mrb_set_in_c_funcall()`の呼び出しを`scheduler_lock`に置き換え

#### 4.3 app.cの修正

```c
// lib/add/picoruby-fmrb-app/ports/esp32/app.c
// dispatch_hid_event_to_ruby() を修正

// 旧実装
mrb_callinfo *ci_before = mrb->c->ci;
mrb_funcall(mrb, self, "on_event", 1, event_hash);

// 新実装 (本流のAPIを使用)
mrb_value on_event_proc = mrb_iv_get(mrb, self, MRB_IVSYM(on_event));
mrb_execute_proc_synchronously(mrb, on_event_proc, 1, &event_hash);
```

#### 4.4 patch/replaceファイルの更新

必要最小限のpatchのみ残す方針:

**削除候補**:
- `lib/patch/picoruby-mruby/src/task.c` - 本流を使用
- `lib/patch/picoruby-mruby/vm_cipush_debug.c` - 本流で不要か確認

**保持候補**:
- `lib/replace/picoruby-machine/ports/esp32/hal.c` - マルチVM対応のため
  - ただし`scheduler_lock`を考慮した修正が必要

**新規作成候補**:
- アプリケーション層での`mrb_execute_proc_synchronously()`呼び出し

### Step 5: ビルドとテスト

#### 5.1 ビルドテスト

```bash
cd /home/kishima/fmrb/family-mruby/fmruby-core
rake clean
rake build:core
```

エラーが出た場合:
- include pathの確認
- HAL関数の呼び出し変更が必要か確認
- データ構造の変更(mrb_task vs mrb_tcb)に対応

#### 5.2 動作確認項目

- [ ] system_guiアプリが起動する
- [ ] shellアプリが起動する
- [ ] shellアプリ起動時にクラッシュしない (CI stackリーク解消)
- [ ] マウス/キーボードイベントが正常に処理される
- [ ] 複数アプリの同時実行
- [ ] sleep/sleep_ms の動作確認

#### 5.3 レグレッションテスト

既存の動作が壊れていないか確認:
- 既存のRubyスクリプトが動作するか
- グラフィックス/オーディオAPIが動作するか
- プロセス間メッセージング(fmrb_msg)が動作するか

## リスクと軽減策

### リスク1: task.c APIの非互換性
**影響**: コンパイルエラー、実行時エラー
**軽減策**:
- 段階的移行(まず本流でビルド、次に独自機能追加)
- 型名変更(mrb_tcb → mrb_task)への対応

### リスク2: HAL抽象化の違い
**影響**: hal.c の大幅な書き換えが必要
**軽減策**:
- 本流の`task_hal.h`を詳細確認
- 必要なら`hal_register_vm()`等の独自拡張を最小限追加

### リスク3: scheduler_lockの前提条件
**影響**: 本流のscheduler_lockが単一VM前提の可能性
**軽減策**:
- マルチVM環境でのscheduler_lockの動作を確認
- 必要なら各VM毎にlockを管理

### リスク4: メモリ使用量増加
**影響**: task構造体のサイズ増加によるメモリ不足
**軽減策**:
- 新task.cでのメモリ使用量を測定
- MAX_TASKSやスタックサイズを調整

## 参考資料

### ソースコード
- 本流task.c: `/home/kishima/fmrb/investigate/picoruby/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-task/src/task.c`
- family-mruby task.c: `lib/patch/picoruby-mruby/src/task.c`
- family-mruby hal.c: `lib/replace/picoruby-machine/ports/esp32/hal.c`

### ドキュメント
- [fix_shell_crash_race_condition.md](fix_shell_crash_race_condition.md) - レースコンディション分析
- Rakefile - パッチ適用ロジック

### コミット履歴
```bash
# family-mrubyでのパッチ適用履歴
git log --oneline --all -- lib/patch/picoruby-mruby/src/task.c
git log --oneline --all -- lib/replace/picoruby-machine/ports/esp32/hal.c
```

## 次のアクション

1. **Step 2の準備作業を実行** - サブモジュール状態確認とバックアップ
2. **本流の最新コミットを調査** - scheduler_lock等の実装状況確認
3. **テスト環境で試行** - まず別ブランチで実験
4. **段階的マージ** - 一度に全部変えず、機能単位で確認

## 作業ログ

### 2026-03-21
- パッチ適用状況の調査完了
- 本流task.cとの差分確認(1573行 vs 967行)
- HALマルチVM実装の問題点特定
- 引き継ぎドキュメント作成

### 2026-03-21 Step 2: サブモジュール更新前の準備 調査結果

#### 2.1 現在のサブモジュール状態

```
components/picoruby-esp32/picoruby: 0f47b6bb (3.0.1-1630-g0f47b6bb)
  最新コミット: "Fix RBS"
```

他のサブモジュール:
- esp_littlefs: 8964c59a (v1.20.1)
- lua: 6e22fedb (v5.4-beta-336-g6e22fedb)
- tlsf: deff9ab5 (heads/master)
- msgpack-c: 8792f42f (cpp-1.3.0-445-g8792f42f)
- microtar: 27076e1b (v0.1.0-2-g27076e1)
- tomlc99: 26b9c1ea

#### 2.2 本流の最新状態 (tmp/picoruby)

- 最新コミット: `c14aa44` "Fix PICORB_ROOt -> PICORUBY_ROOT" (master)
- ブランチ: master のみ (+ origin/master)
- リモート: https://github.com/picoruby/picoruby.git

本流の最近のトピック (サブモジュール版 0f47b6bb 以降):
- マクロリネーム: PICORUBY_hoge -> PICORB_hoge
- mruby/c サポート追加
- PSG (Programmable Sound Generator) サポート
- PIO (Programmable I/O) モジュール
- Regexp (regex_light) 実装
- ネットワーク関連 (net/ntp等)
- メモリチューニング
- ビルドリファクタリング

#### 2.3 重要機能の存在確認

本流 (tmp/picoruby) に以下の機能が **存在することを確認**:

##### scheduler_lock
- 定義: `mruby.h:259` - `mrb_task_state` 構造体内の `uint8_t scheduler_lock` フィールド
- 最大値: `MRB_TASK_SCHEDULER_LOCK_MAX = 255` (task.c:52)
- チェック関数: `task_check_scheduler_lock()` (task.c:56-61)
- 使用箇所: `mrb_task_suspend`, `mrb_task_resume`, `mrb_task_terminate`, `mrb_create_task` 等の全非同期API

##### mrb_execute_proc_synchronously()
- 定義: task.c:1130 / task.h:123
- scheduler_lockをインクリメントして一時タスクを作成し、同期的に実行後にロック解除
- 主な利用者: picoruby-wasm (js.c, debugger.c)
- **注意**: 現在のargc/argvは未使用 (将来の拡張用)

##### task_hal.h (新HALインターフェース)
- `mrb_hal_task_init(mrb_state *mrb)` - VM登録含む初期化
- `mrb_hal_task_final(mrb_state *mrb)` - VM登録解除含むクリーンアップ
- `mrb_task_enable_irq()` / `mrb_task_disable_irq()` - 割り込み制御
- `mrb_hal_task_idle_cpu(mrb_state *mrb)` - アイドル処理
- `mrb_hal_task_sleep_us(mrb_state *mrb, mrb_int usec)` - 実時間スリープ
- `MRB_TASK_MAX_VMS = 8` - マルチVM対応が **本流で公式サポート**

##### POSIX HAL参考実装 (hal-posix-task/src/task_hal.c)
- `vm_list[MRB_TASK_MAX_VMS]` で複数VM管理
- `sigalrm_handler()` で全登録VMにtickを送信
- family-mrubyのESP32 HALとアーキテクチャが類似

#### 2.4 本流と現状の差異分析

##### task構造体の変更
- 旧: `mrb_tcb` (family-mruby) -> 新: `mrb_task` (本流)
- 新task構造体はメモリ最適化済み (union使用で約18バイト/タスク削減)
- `mrb_context c` がインラインメンバとして含まれる

##### HALインターフェースの変更
- 旧: `hal_init()` / `hal_register_vm()` / `hal_deinit()` (family-mruby独自)
- 新: `mrb_hal_task_init()` / `mrb_hal_task_final()` + `mrb_hal_task_idle_cpu()` / `mrb_hal_task_sleep_us()`
- 本流にはESP32/FreeRTOS用HAL実装は **存在しない** -> family-mrubyで新規作成が必要

##### irq制御の変更
- `mrb_task_enable_irq()` / `mrb_task_disable_irq()` は本流でも同名
- family-mruby現状: VM単位のirqフラグ管理 (g_tick_manager内)
- 本流POSIX実装: sigprocmaskベース (プロセス全体)

##### in_c_funcall の扱い
- family-mruby現状: `mrb_set_in_c_funcall()` でVM単位のフラグ管理
- 本流: `scheduler_lock` で代替可能 -> `mrb_set_in_c_funcall()` は不要になる

#### 2.5 ESP32用HAL実装の方針 (案)

本流にはESP32用task HALがないため、`hal-posix-task/src/task_hal.c` を参考に
FreeRTOS版を新規作成する必要がある。

主要な設計判断:
1. **tick配信**: FreeRTOSタスクから全VMにmrb_tick()を呼ぶ方式を継続
   - 本流POSIX版もSIGALRMハンドラから全VMにtickを送信しており、同じアーキテクチャ
2. **レースコンディション対策**: `scheduler_lock` を活用
   - イベントディスパッチ時は `mrb_execute_proc_synchronously()` を使用
   - `mrb_set_in_c_funcall()` は廃止可能
3. **irq制御**: FreeRTOS critical section ベースに変更
   - VM単位のirqフラグ管理から、FreeRTOSのtaskENTER_CRITICAL / taskEXIT_CRITICAL へ

必要な実装ファイル:
- `hal-esp32-task/src/task_hal.c` (新規) - ESP32/FreeRTOS用task HAL実装
- `lib/replace/picoruby-machine/ports/esp32/hal.c` の修正 - 新HAL IFに対応

#### 2.6 本流マルチVM対応によるパッチ削減分析

本流が `task_hal.h` でマルチVMを公式サポートしたことにより、
family-mruby独自パッチの一部が不要になる。

##### 不要になるもの

| パッチ/実装 | 理由 |
|-------------|------|
| `g_tick_manager` のVM登録管理ロジック | 本流 `task_hal.h` が `MRB_TASK_MAX_VMS` で同等の仕組みを提供 |
| `mrb_set_in_c_funcall()` + `in_c_funcall` フラグ | `scheduler_lock` で代替可能 |
| `hal_register_vm()` / `hal_deinit()` | `mrb_hal_task_init()` / `mrb_hal_task_final()` が同等の役割 |
| VM単位の `irq` フラグ管理 | 本流の `mrb_task_enable_irq()` / `mrb_task_disable_irq()` で対応 |

##### 引き続き必要なもの

| 実装 | 理由 |
|------|------|
| FreeRTOSタスクによるtick配信 (`mruby_tick_task`) | 本流にESP32/FreeRTOS用HALがない。ESP32版task HALとして新規作成が必要 |
| `hal_deinit_by_pool()` | メモリプール単位のVM解除はfamily-mruby固有の要件 |
| `hal_write` / `hal_getchar` 等のI/O関数 | プラットフォーム固有 |

##### 結論

現在の `lib/replace/picoruby-machine/ports/esp32/hal.c` は丸ごと置き換えではなく、
本流の `task_hal.h` インターフェースに沿ったESP32用task HALとして書き直す形になる。
独自のVM管理コードの大部分は本流の設計に乗せられるため、パッチの「独自度」が大幅に下がる。
