# PicoRubyのrequireとSandboxの仕組み

本ドキュメントは、PicoRubyにおける`require`機能と`Sandbox`の実装について、調査結果をまとめたものです。

## 目次

1. [概要](#概要)
2. [PicoRubyのrequire機能](#picorubyのrequire機能)
3. [Sandboxの仕組み](#sandboxの仕組み)
4. [mrb_stateとmrb_contextの違い](#mrb_stateとmrb_contextの違い)
5. [Sandbox内でのrequire](#sandbox内でのrequire)
6. [参考情報](#参考情報)

---

## 概要

### requireの役割

- Rubyライブラリの動的ロード
- 依存関係の解決
- 重複ロードの防止（`$LOADED_FEATURES`による管理）

### Sandboxの役割

- Rubyコードの安全な独立実行環境
- Task単位での実行隔離
- 動的コンパイル・実行制御
- タイムアウト・例外処理

---

## PicoRubyのrequire機能

PicoRubyのrequire機能は、**ビルド時**と**実行時**の2層構造で実現されています。

### 1. ビルド時の処理

#### gem収集とバイトコード化

**処理場所**: `components/picoruby-esp32/picoruby/mrbgems/picoruby-require/mrbgem.rake`

```ruby
# picogems（PicoRubyのgem）の収集
build.gems.each do |gem|
  if gem.name.start_with?("picoruby-") && !gem.name.start_with?("picoruby-bin-")
    gem_name = gem.name.sub(/\Apicoruby-?/,'')
    mrbfile = "#{mrbgems_dir}/#{gem.name}/mrblib/#{gem_name}.c"

    # mrblib/**/*.rb をバイトコードにコンパイル
    file mrbfile => rbfiles do |t|
      File.open(t.name, 'w') do |f|
        name = File.basename(t.name, ".c").gsub('-','_')
        mrbc.run(f, t.prerequisites, name, cdump: false)
      end
    end

    picogems[gem.require_name || File.basename(mrbfile, ".c")] = {
      mrbfile: mrbfile,
      initializer: initializer
    }
  end
end
```

#### picogem_init.cの生成

**生成ファイル**: `build/mrbgems/picogem_init.c`

```c
const char *prebuilt_gems[] = {
  "mruby",
  "env",
  "io/console",
  "machine",
  "sandbox",
  "require",
  "yaml",
  // ... その他のgem
  NULL
};
```

この配列により、ビルド時に埋め込まれたgemのリストが管理されます。

### 2. 実行時の処理

#### requireメソッドの実装

**ファイル**: `picoruby-require/mrblib/require.rb:5-13`

```ruby
def require(name)
  return false if required?(name)     # 既にロード済みかチェック
  result = extern(name)               # prebuilt_gemsをチェック
  if result != nil
    $LOADED_FEATURES << name          # ロード済みリストに追加
    return !!result
  end
  require_file(name)                  # ファイルシステムから検索
end
```

#### externメソッド（Cで実装）

**ファイル**: `picoruby-require/src/mruby/require.rb:12-24`

```c
static mrb_value
mrb_extern(mrb_state *mrb, mrb_value self)
{
  char *name;
  mrb_bool force;
  mrb_get_args(mrb, "z|b", &name, &force);

  // prebuilt_gems配列を検索
  for (int i = 0; prebuilt_gems[i]; i++) {
    if (strcmp(prebuilt_gems[i], name) == 0) {
      return mrb_false_value();  // 見つかった
    }
  }
  return mrb_nil_value();  // 見つからない（ファイルから検索）
}
```

#### ファイルからのロード

**ファイル**: `picoruby-require/mrblib/require.rb:38-54`

```ruby
def load_file(name_with_ext)
  sandbox = Sandbox.new('require')
  load_paths(name_with_ext).each do |load_path|
    path = File.expand_path(name_with_ext, load_path)
    if File.file?(path)
      begin
        sandbox.load_file(path)
        $LOADED_FEATURES << name_with_ext unless required?(name_with_ext)
        return true
      rescue => e
        sandbox.terminate
        raise e
      end
    end
  end
  raise LoadError, "cannot load such file -- #{name_with_ext}"
end
```

### 3. requireの実行フロー

```
require 'yaml'
    ↓
1. $LOADED_FEATURES に 'yaml' が含まれるか？
    ↓ NO
2. prebuilt_gems[] に 'yaml' が含まれるか？
    ↓ YES → 既にメモリに埋め込み済み
3. $LOADED_FEATURES << 'yaml'
    ↓
4. return true

（prebuilt_gemsにない場合）
    ↓
5. $LOAD_PATH から yaml.mrb / yaml.rb を検索
    ↓
6. 新しいSandboxを作成
    ↓
7. Sandboxでファイルを実行
    ↓
8. $LOADED_FEATURES << 'yaml'
```

---

## Sandboxの仕組み

Sandboxは、Rubyコードを安全に独立した環境で実行するための仕組みです。

### 1. Sandboxの構造

#### SandboxStateデータ構造

**ファイル**: `picoruby-sandbox/src/sandbox.c:7-17`

```c
typedef struct sandbox_state {
  mrc_ccontext *cc;      // コンパイラコンテキスト
  mrc_irep *irep;        // 中間表現（バイトコード）
  mrb_value task;        // Taskオブジェクト
  uint8_t *vm_code;      // VMコード
  pm_options_t *options; // パーサーオプション
} SandboxState;
```

### 2. Sandboxの初期化

**ファイル**: `picoruby-sandbox/src/mruby/sandbox.c:30-56`

```ruby
sandbox = Sandbox.new('my_sandbox')
```

内部処理:
1. SandboxState構造体の確保
2. コンパイラコンテキストの作成 (`mrc_ccontext_new`)
3. Taskの作成
   - 初期コード: `"Task.current.suspend"` をコンパイル
   - Taskオブジェクトを作成し、SandboxStateに保存
4. GC登録（タスクがGCされないように保護）

### 3. コードのコンパイル

#### 文字列からコンパイル

**ファイル**: `picoruby-sandbox/src/mruby/sandbox.c:81-100`

```ruby
sandbox.compile("puts 'Hello'")
```

内部処理:
1. コンパイラコンテキストのクリーンアップ
2. 新しいコンパイラコンテキストの作成
3. Rubyコードをパース & コンパイル (`mrc_load_string_cxt`)
4. 中間表現（irep）を生成
5. irep参照カウントの増加

#### メモリから直接コンパイル

**ファイル**: `picoruby-sandbox/src/mruby/sandbox.c:103-128`

```ruby
sandbox.compile_from_memory(physical_address, size)
```

フラッシュメモリ上のコードを直接コンパイル可能。SDカードやフラッシュからのファイル読み込みを最適化。

### 4. コードの実行

#### バイトコードの実行

**ファイル**: `picoruby-sandbox/src/mruby/sandbox.c:243-256`

```ruby
sandbox.exec_mrb(bytecode_string)
sandbox.exec_mrb_from_memory(physical_address)
```

内部処理:
1. バイトコードの読み込み (`mrb_read_irep`)
2. RProc（手続きオブジェクト）の作成
3. Taskコンテキストの初期化
4. Taskの再開 (`mrb_resume_task`)

#### コンパイル済みコードの実行

**ファイル**: `picoruby-sandbox/src/mruby/sandbox.c:139-150`

```ruby
sandbox.compile("puts 'Hello'")
sandbox.execute
```

内部処理:
1. インターン文字列の解決 (`mrc_resolve_intern`)
2. RProc作成（irepからRProcを生成）
3. Taskへのプロシージャ設定 (`mrb_task_proc_set`)
4. Taskコンテキストのリセット
5. Taskの再開 (`mrb_resume_task`)

### 5. ファイルの読み込みと実行

**ファイル**: `picoruby-sandbox/mrblib/sandbox.rb:14-50`

```ruby
sandbox.load_file("/path/to/file.rb", join: true)
```

処理フロー:
1. ファイルを開く
2. 形式判定
   - `RITE0300` で始まる → mrubyバイトコード
   - それ以外 → Rubyスクリプト
3. 実行
   - バイトコード: `exec_mrb` / `exec_mrb_from_memory`
   - スクリプト: `compile` → `execute`
4. 待機（`join: true` の場合、完了まで待機）

### 6. 実行制御

#### 状態の取得

```ruby
sandbox.state  # => :DORMANT, :READY, :RUNNING, :WAITING, :SUSPENDED
```

タスクの状態:
- `:DORMANT` - 休止中（実行完了）
- `:READY` - 実行可能
- `:RUNNING` - 実行中
- `:WAITING` - 待機中（sleep等）
- `:SUSPENDED` - 一時停止

#### タスクの制御

```ruby
sandbox.suspend   # タスクを一時停止
sandbox.resume    # タスクを再開
sandbox.stop      # タスクを停止
sandbox.terminate # タスクを終了
```

#### 実行結果の取得

```ruby
result = sandbox.result  # タスクの戻り値
error = sandbox.error    # 例外が発生した場合のエラーオブジェクト
```

### 7. 待機とタイムアウト

**ファイル**: `picoruby-sandbox/mrblib/sandbox.rb:8-12, 54-84`

```ruby
sandbox.wait(timeout: 5000)  # 5秒タイムアウト
```

内部ループ:
1. 状態監視ループ（5msごとにタスクの状態をチェック）
2. 入力処理（`STDIN.read_nonblock(1)` で割り込み可能）
3. タイムアウト処理
4. 割り込みハンドリング
   - `Ctrl+C` → `Signal.raise(:INT)` → タスク停止
   - `Ctrl+Z` → `Signal.raise(:TSTP)` → タスク一時停止

---

## mrb_stateとmrb_contextの違い

PicoRubyにおける `mrb_state` と `mrb_context` の違いは、**VM全体の状態**と**個別の実行コンテキスト（スタック状態）**の違いです。

### 1. mrb_state（mruby VM全体の状態）

**定義**: `picoruby/mrbgems/picoruby-mruby/lib/mruby/include/mruby.h:246-316`

```c
typedef struct mrb_state {
  struct mrb_jmpbuf *jmp;

  struct mrb_context *c;         // 現在実行中のコンテキスト（ポインタ）
  struct mrb_context *root_c;    // ルートコンテキスト
  struct iv_tbl *globals;        // グローバル変数テーブル

  struct RObject *exc;           // 現在の例外
  struct RObject *top_self;      // トップレベルのself

  // 組み込みクラス
  struct RClass *object_class;   // Object
  struct RClass *string_class;   // String
  struct RClass *array_class;    // Array
  // ... その他のクラス

  mrb_gc gc;                     // ガベージコレクタ

  // メソッドキャッシュ
  struct mrb_cache_entry cache[MRB_METHOD_CACHE_SIZE];

  // シンボルテーブル
  mrb_sym symidx;
  const char **symtbl;

  // ... その他
} mrb_state;
```

#### 役割

**VM全体で1つだけ存在する大域的な状態**

- クラス定義: すべての組み込みクラス（Object, String, Array等）
- グローバル変数: `$LOADED_FEATURES`, `$LOAD_PATH` 等
- シンボルテーブル: 全シンボルの管理
- GC: ヒープメモリの管理
- 例外: 現在発生している例外
- メソッドキャッシュ: メソッド呼び出しの高速化

#### 特徴

- 全Task間で共有される
- 1プロセスに1つ
- グローバルな状態を保持

### 2. mrb_context（実行コンテキスト）

**定義**: `picoruby/mrbgems/picoruby-mruby/lib/mruby/include/mruby.h:192-203`

```c
struct mrb_context {
  struct mrb_context *prev;       // 前のコンテキスト（リンクリスト）

  // スタック
  mrb_value *stbase, *stend;      // スタック領域（開始・終了）

  // コールスタック
  mrb_callinfo *ci;               // 現在の呼び出し情報
  mrb_callinfo *cibase, *ciend;   // 呼び出しスタック領域

  // Fiber/Task状態
  enum mrb_fiber_state status : 4;
  mrb_bool vmexec : 1;
  struct RFiber *fib;
};
```

#### callinfo（呼び出し情報）

```c
typedef struct {
  uint8_t n:4;                  // 引数の数
  mrb_sym mid;                  // メソッド名（シンボル）
  const struct RProc *proc;     // 実行中のプロシージャ
  mrb_value *stack;             // スタックポインタ
  const mrb_code *pc;           // プログラムカウンタ（現在の命令）
  // ... その他
} mrb_callinfo;
```

#### 役割

**Task/Fiber毎に独立した実行状態**

- スタック: ローカル変数、引数、中間値を保存
- コールスタック: メソッド呼び出しの履歴
- 実行位置: 現在実行中の命令アドレス（PC）
- 実行状態: CREATED, RUNNING, STOPPED

#### 特徴

- 各Task毎に独立して存在
- スタックオーバーフローを隔離
- 並行実行を可能にする

### 3. Taskにおけるmrb_contextの使われ方

#### Task制御ブロック（TCB）

**定義**: `picoruby/mrbgems/picoruby-mruby/include/task.h:60-84`

```c
typedef struct RTcb {
  struct RTcb *next;            // タスクキューのリンク
  uint8_t priority;             // 優先度
  uint8_t status;               // DORMANT/READY/RUNNING/WAITING/SUSPENDED
  mrb_value task;               // Taskオブジェクト
  mrb_value value;              // タスクの戻り値
  struct mrb_context c;         // 【重要】各タスク専用のコンテキスト
} mrb_tcb;
```

#### コンテキストの初期化

**ファイル**: `picoruby/mrbgems/picoruby-mruby/src/task.c:240-283`

```c
void mrb_task_init_context(mrb_state *mrb, mrb_value task, struct RProc *proc)
{
  mrb_tcb *tcb = (mrb_tcb *)mrb_data_get_ptr(mrb, task, &mrb_task_tcb_type);
  struct mrb_context *c = &tcb->c;  // TCB内のコンテキストを取得

  // スタック領域の確保（Task専用）
  c->stbase = (mrb_value*)mrb_malloc(mrb, slen*sizeof(mrb_value));
  c->stend = c->stbase + slen;

  // コールスタックの確保（Task専用）
  c->cibase = (mrb_callinfo *)mrb_malloc(mrb, TASK_CI_INIT_SIZE * sizeof(mrb_callinfo));
  c->ciend = c->cibase + TASK_CI_INIT_SIZE;

  // 初期化
  c->ci = c->cibase;
  c->status = MRB_TASK_CREATED;
  c->ci->proc = proc;
  c->ci->stack = c->stbase;
}
```

#### タスク切り替え

**ファイル**: `picoruby/mrbgems/picoruby-mruby/src/task.c:419-434`

```c
mrb_value mrb_tasks_run(mrb_state *mrb) {
  while (1) {
    mrb_tcb *tcb = q_ready_;  // Ready queueから次のTaskを取得

    // 【重要】現在のコンテキストを切り替え
    mrb->c = &tcb->c;  // mrb_state の c を Task のコンテキストに変更

    // VMを実行
    tcb->value = mrb_vm_exec(mrb, mrb->c->ci->proc, mrb->c->ci->pc);

    // ... タスク完了チェック、次のTaskへ
  }
}
```

### 4. アーキテクチャ図

```
┌─────────────────────────────────────────────────────────┐
│                     mrb_state (VM全体)                   │
├─────────────────────────────────────────────────────────┤
│  ● globals:         $LOADED_FEATURES, $LOAD_PATH, ...   │
│  ● object_class:    Object クラス                        │
│  ● string_class:    String クラス                        │
│  ● gc:              ガベージコレクタ                      │
│  ● symtbl:          シンボルテーブル                     │
│  ● cache:           メソッドキャッシュ                   │
│                                                           │
│  ● c: ───────────┐  現在実行中のコンテキスト（ポインタ）│
└───────────────│──────────────────────────────────────────┘
                 │
                 │  【タスク切り替えで変わる】
                 │
    ┌────────────┴──────────┬───────────────┐
    │                       │               │
    ▼                       ▼               ▼
┌─────────────┐      ┌─────────────┐  ┌─────────────┐
│   Task 1    │      │   Task 2    │  │   Task 3    │
│   (TCB 1)   │      │   (TCB 2)   │  │   (TCB 3)   │
├─────────────┤      ├─────────────┤  ├─────────────┤
│ priority: 10│      │ priority: 5 │  │ priority: 8 │
│ status:READY│      │ status:SUSP │  │ status:WAIT │
│             │      │             │  │             │
│ ┌─────────┐ │      │ ┌─────────┐ │  │ ┌─────────┐ │
│ │context c│ │      │ │context c│ │  │ │context c│ │
│ ├─────────┤ │      │ ├─────────┤ │  │ ├─────────┤ │
│ │stbase   │←┼──┐   │ │stbase   │ │  │ │stbase   │ │
│ │stend    │ │  │   │ │stend    │ │  │ │stend    │ │
│ │ci       │ │  │   │ │ci       │ │  │ │ci       │ │
│ │status   │ │  │   │ │status   │ │  │ │status   │ │
│ └─────────┘ │  │   │ └─────────┘ │  │ └─────────┘ │
│             │  │   │             │  │             │
│ スタック領域 │  │   │ スタック領域 │  │ スタック領域 │
│ [値1, 値2]  │  │   │ [値A, 値B]  │  │ [値X, 値Y]  │
└─────────────┘  │   └─────────────┘  └─────────────┘
                 │
                 └─── mrb->c が現在指しているコンテキスト
```

### 5. 違いの一覧

| 項目 | mrb_state | mrb_context |
|------|-----------|-------------|
| **スコープ** | VM全体（グローバル） | Task/Fiber毎（ローカル） |
| **存在数** | 1つ | Task数だけ存在 |
| **保存内容** | クラス、グローバル変数、GC、シンボル | スタック、コールスタック、実行位置 |
| **共有** | 全Task間で共有 | Task毎に独立 |
| **切り替え** | 切り替わらない | タスクスイッチで切り替わる |
| **ライフタイム** | VMの起動から終了まで | Taskの作成から削除まで |
| **例** | `$LOADED_FEATURES`, `Object`クラス | ローカル変数、メソッド引数 |

---

## Sandbox内でのrequire

Sandbox内でrequireが実行される場合、グローバル変数は全Task間で共有されるため、問題なく動作します。

### 1. グローバル変数の保存場所

#### mrb_stateのglobalsフィールド

**ファイル**: `picoruby/mrbgems/picoruby-mruby/lib/mruby/include/mruby.h:246-251`

```c
typedef struct mrb_state {
  struct jmp_buf *jmp;

  struct mrb_context *c;           // 現在のコンテキスト（Task固有）
  struct mrb_context *root_c;      // ルートコンテキスト
  struct iv_tbl *globals;          // グローバル変数テーブル（全Task共有）

  // ...
} mrb_state;
```

#### グローバル変数アクセス

**ファイル**: `picoruby/mrbgems/picoruby-mruby/lib/mruby/src/variable.c:1222-1251`

```c
mrb_value mrb_gv_get(mrb_state *mrb, mrb_sym sym) {
  mrb_value v;
  if (iv_get(mrb, mrb->globals, &v))  // mrb->globals から取得
    return v;
  return mrb_nil_value();
}

void mrb_gv_set(mrb_state *mrb, mrb_sym sym, mrb_value v) {
  if (!mrb->globals) {
    mrb->globals = iv_new(mrb);
  }
  iv_put(mrb, mrb->globals, sym, v);  // mrb->globals に保存
}
```

**重要**: グローバル変数は `mrb_state->globals` に保存され、これは**全Taskで共有**されます。

### 2. 実行フロー例

#### シナリオ

```ruby
# メインプログラム（Task 0）
$LOADED_FEATURES  # => ['require', 'sandbox', ...]

# Sandbox 1を作成（Task 1）
sandbox = Sandbox.new('task1')
sandbox.compile("require 'yaml'; puts YAML")
sandbox.execute
```

#### 内部動作

1. **Task 0（メイン）実行中**
   ```c
   mrb->c = &task0_tcb->c  // メインのコンテキスト
   mrb->c->ci->pc = [メインの実行位置]
   mrb->c->stbase = [メインのスタック]
   ```

2. **Task 1（Sandbox）を作成**
   ```c
   // 新しいTaskを作成
   mrb_value task = mrc_create_task(cc, irep, name, priority, top_self);

   // Task 1専用のコンテキストが確保される
   mrb_tcb *task1_tcb = ...;
   task1_tcb->c.stbase = [Task 1専用スタック];
   task1_tcb->c.cibase = [Task 1専用コールスタック];
   ```

3. **Task 1実行開始**
   ```c
   mrb_tcb *tcb = q_ready_;  // Task 1を取得
   mrb->c = &tcb->c;         // コンテキストを切り替え

   // この時点で：
   // - mrb->c->stbase は Task 1 のスタック
   // - mrb->globals は共有（全Task共通）

   mrb_vm_exec(mrb, mrb->c->ci->proc, mrb->c->ci->pc);
   ```

4. **Task 1内でrequire 'yaml'を実行**
   ```ruby
   def require(name)
     return false if required?(name)  # $LOADED_FEATURES をチェック
     # ...
   end
   ```

   ```c
   // C側（variable.c:1222-1229）
   mrb_value mrb_gv_get(mrb_state *mrb, mrb_sym sym) {
     // mrb->globals から取得（全Task共有）
     iv_get(mrb, mrb->globals, sym, &v);
     return v;
   }
   ```

5. **新しいSandbox（Task 2）を作成**
   ```ruby
   sandbox = Sandbox.new('require')  # さらにネストしたTask
   sandbox.load_file('/path/to/yaml.rb')
   ```

   - Task 2専用のコンテキストが作成される
   - `mrb->globals` は引き続き共有

6. **`$LOADED_FEATURES`に追加**
   ```ruby
   $LOADED_FEATURES << 'yaml'
   ```

   ```c
   // C側（variable.c:1242-1251）
   void mrb_gv_set(mrb_state *mrb, mrb_sym sym, mrb_value v) {
     // mrb->globals に保存（全Task共有）
     iv_put(mrb, mrb->globals, sym, v);
   }
   ```

### 3. ネストしたSandboxの例

```ruby
# メインプログラム
$LOADED_FEATURES  # => ['require', 'sandbox', ...]

# Sandbox 1
sandbox1 = Sandbox.new('task1')
sandbox1.compile("require 'yaml'; puts YAML")
sandbox1.execute
# $LOADED_FEATURES に 'yaml' が追加される

# Sandbox 2
sandbox2 = Sandbox.new('task2')
sandbox2.compile("require 'yaml'; puts 'Already loaded'")
sandbox2.execute
# 'yaml' は既に $LOADED_FEATURES にあるのでスキップ
```

### 4. 実例: shell_executablesでのrequire

**ファイル**: `picoruby-shell/shell_executables/wifi_connect.rb:16-20`

```ruby
require 'env'
require 'yaml'
require "mbedtls"
require "base64"
require 'net'
```

#### 実行の流れ

**ファイル**: `picoruby-shell/mrblib/job.rb:39-49`

1. **Shellがコマンドを実行**
   ```ruby
   job = Shell::Job.new('wifi_connect', 'MySSID', 'password')
   job.exec
   ```

2. **Jobが専用のSandboxを作成**
   ```ruby
   @sandbox = Sandbox.new('wifi_connect')
   ```

3. **実行可能ファイルをロード**
   ```ruby
   @sandbox.load_file('/bin/wifi_connect')
   ```

4. **Sandbox内でrequireが複数実行される**
   - 各requireで `$LOADED_FEATURES` を確認
   - 未ロードなら新しいSandboxでロード
   - `$LOADED_FEATURES` に追加

5. **すべてのTaskが `mrb->globals` を共有**
   - 一度requireされたgemは他のTaskでもスキップされる

### 5. まとめ

#### Sandbox内でrequireが実行される場合

1. **グローバル変数（`$LOADED_FEATURES`）は全Task間で共有される**
   - `mrb_state->globals` に保存
   - `mrb_gv_get` / `mrb_gv_set` でアクセス

2. **requireは正しく動作する**
   - 一度requireされたgemは他のSandboxでも認識される
   - 重複ロードが防止される

3. **ネストしたSandboxも問題なく動作**
   - require内部でさらにSandboxが作成される
   - すべてのSandboxが同じ `mrb_state` を共有

4. **スタックと実行コンテキストは独立**
   - 各Taskは専用の `mrb_context` を持つ
   - スタックオーバーフローなどは隔離される

5. **クラス定義も共有される**
   - `mrb->object_class` など
   - requireでロードされたクラス/モジュールは全Taskで利用可能

#### 設計の利点

- **効率的**: 一度ロードされたコードは再利用
- **安全**: スタックは隔離、例外も隔離
- **柔軟**: 動的にコードをロード・実行可能
- **一貫性**: グローバル状態は統一

---

## 参考情報

### 重要なファイル一覧

#### require関連

- `components/picoruby-esp32/picoruby/mrbgems/picoruby-require/mrbgem.rake`
  - ビルド時のgem収集とバイトコード化

- `components/picoruby-esp32/picoruby/mrbgems/picoruby-require/mrblib/require.rb`
  - requireメソッドの実装

- `components/picoruby-esp32/picoruby/mrbgems/picoruby-require/src/mruby/require.c`
  - externメソッドの実装
  - `$LOADED_FEATURES`の初期化

- `components/picoruby-esp32/picoruby/build/host/mrbgems/picogem_init.c`
  - 生成されるprebuilt_gems配列

#### Sandbox関連

- `components/picoruby-esp32/picoruby/mrbgems/picoruby-sandbox/src/sandbox.c`
  - SandboxStateデータ構造

- `components/picoruby-esp32/picoruby/mrbgems/picoruby-sandbox/src/mruby/sandbox.c`
  - Sandboxメソッドの実装（compile, execute, exec_mrb等）

- `components/picoruby-esp32/picoruby/mrbgems/picoruby-sandbox/mrblib/sandbox.rb`
  - load_file, waitメソッドの実装

#### Task関連

- `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/include/task.h`
  - mrb_tcb（Task制御ブロック）の定義

- `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/src/task.c`
  - タスクスケジューラの実装
  - mrb_task_init_context, mrb_tasks_run等

#### mruby内部

- `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby/include/mruby.h`
  - mrb_state, mrb_context, mrb_callinfoの定義

- `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby/src/variable.c`
  - グローバル変数のアクセス（mrb_gv_get, mrb_gv_set）

### 主要な型定義

```c
// VM全体の状態
typedef struct mrb_state;

// 実行コンテキスト（Task毎）
struct mrb_context;

// 呼び出し情報
typedef struct mrb_callinfo;

// Task制御ブロック
typedef struct RTcb mrb_tcb;

// Sandbox状態
typedef struct sandbox_state SandboxState;
```

### 主要な関数

```c
// Taskの作成・制御
mrb_value mrb_create_task(mrb_state *mrb, struct RProc *proc, ...);
void mrb_resume_task(mrb_state *mrb, mrb_value task);
void mrb_suspend_task(mrb_state *mrb, mrb_value task);
void mrb_terminate_task(mrb_state *mrb, mrb_value task);
mrb_value mrb_tasks_run(mrb_state *mrb);

// コンテキストの初期化
void mrb_task_init_context(mrb_state *mrb, mrb_value task, struct RProc *proc);

// グローバル変数アクセス
mrb_value mrb_gv_get(mrb_state *mrb, mrb_sym sym);
void mrb_gv_set(mrb_state *mrb, mrb_sym sym, mrb_value v);
```

---

本ドキュメントは、PicoRubyのrequire機能とSandboxの実装について、ソースコード解析に基づいてまとめたものです。
