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

参照: `/home/kishima/fmrb/investigate/picoruby/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-task/src/task.c`

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
