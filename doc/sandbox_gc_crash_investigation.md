# Sandbox/IRB GC クラッシュ調査

## 概要

Shell の `irb` コマンド実行時、GC 処理中の `mrb_task_mark_all` で SEGV が発生する。
2026-03-21 の picoruby サブモジュール更新 (`0f47b6b` -> `c14aa44`) 以降に発生。

## 現象

- Shell アプリで `irb` コマンドを実行
- `Sandbox.new` -> `compile("_ = nil")` -> `execute` は成功
- `wait(timeout: nil)` (内部で `mrb_task_run` を呼ぶ) の最中に GC が走りクラッシュ
- `mrb_gc_mark` に不正なポインタ `0x2c0` が渡されて SEGV

## クラッシュのバックトレース (全再現で一貫)

```
#0  mrb_gc_mark ()                    -- obj=0x2c0 のデリファレンスで SEGV
#1  mrb_task_mark_all ()              -- VM スタック値のマーク処理
#2  incremental_gc ()
#3  incremental_gc_finish ()
#4  mrb_incremental_gc ()
#5  mrb_obj_alloc ()
#6  (各種: mrb_str_new_static / mrb_env_new / ary_new_capa)
#7  mrb_vm_exec ()                    -- sandbox タスクが "_ = nil" を実行中
#8  execute_task ()
#9  mrb_task_run ()
#10 execute_mruby_script () at fmrb_app.c:446
```

## 原因分析

### 0x2c0 の正体

- 有効なヒープポインタではない (0x10000 未満の範囲)
- sandbox タスクの VM スタック (`c->stbase[]`) 上に存在
- `mrb_task_mark_all` が `stbase[0..e]` を反復し、各スロットに対して
  `mrb_gc_mark_value` を呼ぶ。あるスロットが GC にオブジェクトポインタと
  解釈される `0x2c0` を含んでいる

### mrb_task_mark_all 内のクラッシュ箇所

クラッシュはスタックマーキングループ内 (callinfo マーキングではない):

```c
/* Mark task's stack */
if (c->stbase) {
    // ... ci->stack と mrb_ci_nregs から 'e' を計算 ...
    for (i = 0; i < e; i++) {
        mrb_gc_mark_value(mrb, c->stbase[i]);  // <-- ここでクラッシュ
    }
}
```

ELF バイナリの逆アセンブル解析で確認済み。

### 除外済み: tick タスクの干渉

`hal_freertos.c` の `mrb_tick()` 呼び出しを完全に無効化 (コメントアウト) しても
同一のクラッシュが再現する。**tick はクラッシュの原因ではない。**

### picoruby サブモジュール更新

- 旧コミット: `0f47b6b` (2025-10-19) -- 動作していた
- 新コミット: `c14aa44` (2026-03-21) -- クラッシュする
- この間に task.c に多数の変更あり
- task.c に影響する主なコミット:
  - `054bd1e9` (2025-07-12) feat(task): Implement complete Ruby Task class
  - `591e07b7` (2025-08-14) Fix memory leak in Sandbox and refactor Task
  - `fbb6e5e0` (2025-11-28) Fix memory leak of tcb and context
  - `081ffe11` (2026-01-01) MicroRuby on ESP32
  - `5ad43757` (2026-01-20) remove deprecated source

## 疑わしい箇所

### 1. task_init_context のスタック初期化

`task.c` の `task_init_context()` でスタックを確保・初期化:

```c
c->stbase = mrb_malloc(mrb, slen * sizeof(mrb_value));
c->stend = c->stbase + slen;
/* stack[1..end] を nil に初期化、stack[0] = top_self */
```

`slen` が `proc->body.irep->nregs` から計算されるが、実際の実行で
より多くのスタックスロットを使う場合 (例: `mrb_ci_nregs` の返す値)、
GC が初期化されていないスロットをマークしようとする可能性がある。

### 2. mrb_ci_nregs が過大な値を返す

`mrb_task_mark_all` でマーク範囲 `e` を以下で計算:

```c
e = (ci->stack - c->stbase) + mrb_ci_nregs(ci);
if (c->stbase + e > c->stend) e = c->stend - c->stbase;
```

`mrb_ci_nregs` が未初期化のスロットを含む範囲を返す場合、
それらのゴミ値 (0x2c0 等) が GC に渡される。

### 3. Sandbox タスクの再利用時にスタックがクリアされない

`mrb_sandbox_execute()` は以下を呼ぶ:
```c
mrb_task_proc_set(mrb, ss->task, proc);
mrb_task_reset_context(mrb, ss->task);
```

`mrb_task_reset_context` は ci と status のみリセットし、**スタック内容は
再初期化しない**。前回の実行で残った非 nil 値がスタック上にあり、
新しい proc の nregs が異なる場合、GC が古いスタックスロット上の
不正な値をスキャンする可能性がある。

## 再現手順

1. `docker compose up`
2. GUI の "Shell" ボタンをクリック
3. シェルウィンドウ内をクリック
4. `irb` + Enter を入力
5. 即座に SEGV 発生

## 関連ファイル

- `picoruby/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-task/src/task.c`
  - `mrb_task_mark_all()` -- クラッシュ箇所
  - `task_init_context()` -- スタック初期化
  - `execute_task()` -- タスクコンテキスト切り替え
  - `mrb_create_task()` -- タスク作成
- `picoruby/mrbgems/picoruby-sandbox/src/mruby/sandbox.c`
  - `mrb_sandbox_initialize()` -- sandbox タスク作成
  - `mrb_sandbox_execute()` -- コンテキストリセットとタスク再開
- `picoruby/mrbgems/picoruby-mruby/src/mrc_utils.c`
  - `mrc_create_task()` -- タスク作成のラッパー
- `picoruby/mrbgems/picoruby-mruby/lib/mruby/src/gc.c`
  - `mrb_gc_mark()` -- SEGV 対象
  - fiber マーキングコード (task マーキングとの比較用)
- `main/prebuild_scripts/default_app/shell.app.rb`
  - `cmd_irb` -- クラッシュのトリガー

## 追加したデバッグ計装

### task.c

- `task_gc_mark_safe()` ヘルパー: callinfo の proc/target_class に対して
  `(uintptr_t) < 0x10000` ガード付き `mrb_gc_mark` (callinfo は原因でないことを確認)
- `mrb_tick()` 内で `switching_ = TRUE` 設定時のログ (`[TICK]`)
- 不正ポインタ検出ログ (`[TASK_GC] BAD`)

### shell.app.rb

- `cmd_irb` 内のステップごとのログ: クラッシュが `execute` 後の
  `wait(timeout: nil)` 中に発生することを確認

### hal_freertos.c

- `mrb_tick()` 呼び出しをコメントアウトしてテスト (tick が原因でないことを確認)

## 修正方針

最も有力な修正箇所は `mrb_task_reset_context()` または `task_init_context()`:

1. **方針 A**: `mrb_task_reset_context()` で、ci/status のリセットに加えて
   スタック全スロットを nil に再初期化する (最も安全で的確)

2. **方針 B**: `mrb_task_mark_all()` で、初期化済み範囲のみマークする
   (gc.c が TERMINATED fiber をスキップするのと同様)

3. **方針 C**: `mrb_ci_nregs()` の過大カウントを修正するか、
   スタック確保時に全 `stend` 範囲をゼロクリアする

方針 A が最も安全。

## 試行した修正と結果

### 修正1: mrb_task_reset_context でスタッククリア (効果なし)

`mrb_task_reset_context` にスタック全体の nil クリアと `ci->stack` リセットを追加。
逆アセンブルで ELF への反映を確認済み。**しかしクラッシュは解消しなかった。**

```c
/* 追加した修正 */
if (c->ci) {
    c->ci->stack = c->stbase;
}
if (c->stbase) {
    mrb_value *s = c->stbase + 1;
    mrb_value *send = c->stend;
    while (s < send) { SET_NIL_VALUE(*s); s++; }
}
```

この結果から、`mrb_task_reset_context` が呼ばれる前 (= 初回 execute 中) に
既にスタックが壊れている可能性が高い。

### 修正2: mrb_tick 無効化 (効果なし)

`hal_freertos.c` の `mrb_tick()` 呼び出しをコメントアウト。
クラッシュは同一パターンで再現。**tick タスクは原因ではない。**

## 現在の調査状況

### 確認済み事項

1. クラッシュは VM スタックのマーキング (callinfo ではない) - 逆アセンブルで確認
2. 不正値 0x2c0 は毎回同じ
3. tick タスクは無関係
4. `mrb_task_reset_context` のスタッククリアでは解決しない
5. picoruby の shell は Task.new を使わず直接 Sandbox を操作する (fmruby と異なる)

### 残る疑い

1. **task_init_context 自体の問題**: `Sandbox.new` -> `mrc_create_task` ->
   `task_init_context` で初期化される際のスタックサイズやレジスタ数の計算が不正。
   初期スクリプト `"Task.current.suspend"` の irep の nregs とスタック初期化サイズの
   不一致がある可能性

2. **mrb_proc_new のコンテキスト継承**: `Sandbox.new` が shell_task 内から呼ばれるため、
   `mrb_proc_new` が shell_task の closure を `proc->upper` に設定する。
   この proc chain が GC マーク時にスタック範囲の過大計算を引き起こす可能性

3. **mrb_ci_nregs の過大評価**: `mrb_task_mark_all` のスタックマーク範囲を
   `mrb_ci_nregs(ci)` で計算するが、この値が実際に初期化されたスタック範囲を
   超えている可能性

4. **0x2c0 の意味**: 704 = 毎回同じ値。スタック上のどのスロットにこの値が入るかを
   特定する必要がある (stbase からのオフセット、ci の nregs 値等)

### 修正3: スタック値デバッグガード (重要な発見)

`mrb_task_mark_all` のスタックマーキングループに不正ポインタ検出を追加。
**しかしログが出ずにクラッシュ** -> スタック値ではなく別の箇所。

逆アセンブル解析の結果、クラッシュは **`t->name` のマーキング** であることが判明:
- `0x67b99: mov 0x10(%r15), %rsi` -- r15=task pointer, offset 0x10 = t->name
- `0x67bad: call mrb_gc_mark` -- name の value.p = 0x2c0 で SEGV

**VM スタックの問題ではなく、mrb_task 構造体の `name` フィールドが壊れている。**

### 逆アセンブル解析の詳細

ELF シンボルアドレス:
- `mrb_gc_mark` = 0x2874a
- `mrb_task_mark_all` = 0x6799d

クラッシュの `#1 mrb_task_mark_all` のオフセット = 0x20C (関数先頭からの距離)

該当命令:
```asm
67b99: mov 0x10(%r15), %rsi    ; r15=task, offset 0x10 = t->name (mrb_value)
67b9d: test $0x7, %esi         ; mrb_immediate_p チェック (boxing_no)
67ba3: jne 67bb2               ; immediate なら skip
67ba5: test %rsi, %rsi         ; NULL チェック
67ba8: je 67bb2                ; NULL なら skip
67baa: mov %rbx, %rdi          ; mrb を第1引数に
67bad: call mrb_gc_mark         ; mrb_gc_mark(mrb, name.value.p) <- SEGV
```

`mov 0x10(%r15), %rsi` は `t->name` の `value.p` フィールドを読んでいる。
boxing_no モードでは mrb_value は `{value(8bytes), tt(4bytes)}` 構造体で、
offset 0x10 = mrb_task 構造体の name フィールドの先頭 = value.p。

### mrb_task 構造体レイアウト (64bit, boxing_no)

```
0x00: next (8 bytes, pointer)
0x08: priority (1), status (1), reason (1), padding (5)
0x10: name.value.p (8 bytes) <- ここが 0x2c0
0x18: name.tt (4 bytes) + padding
0x20: wait union (8 bytes)
0x28: self.value.p (8 bytes)
0x30: self.tt (4 bytes) + padding
0x38: state union (timeslice/result)
0x48: c (struct mrb_context)
```

### 新しい仮説: GC arena 問題

`mrb_create_task` の処理順序:
1. `name_val = mrb_str_new_lit(mrb, "(noname)")` -- arena に保護
2. `task_alloc(mrb)` -- malloc (GC なし)
3. `t->name = name_val` -- task に格納
4. `mrb_data_object_alloc(mrb, ...)` -- **GC トリガーの可能性**

4 の時点で task はまだキューに入っていないため `mrb_task_mark_all` で
`t->name` はマークされない。`name_val` は arena で保護されるはずだが、
arena overflow や arena_save/restore で保護が外れると GC で回収される。

回収後、name.value.p は dangling pointer (0x2c0) になる。

### 修正4: mrb_create_task に mrb_gc_protect 追加 (新たな発見)

`mrb_create_task` 内で `name_val` に `mrb_gc_protect` を追加。
結果: **`mrb_gc_protect` 自体が SEGV** -- `name_val.value.p = 0x2c0`。

```
#0  mrb_gc_protect ()     <-- name_val = 0x2c0 で SEGV
#1  mrb_create_task ()
#2  mrb_sandbox_initialize ()
```

つまり `mrb_create_task` に渡される `name` 引数が**既に壊れている**。
問題は `mrb_sandbox_initialize` 内で `name` が作成された後、
`mrc_create_task` に渡される前に壊れている。

sandbox.c の流れ:
```c
mrb_get_args(mrb, "|S", &name);        // name = nil (引数なし)
name = mrb_str_new_cstr(mrb, "sandbox"); // String 作成
mrb_iv_set(mrb, self, @name, name);     // iv に保存
// この時点で name は C ローカル変数のみ。
// mrb_iv_set が GC をトリガーすると name の String が arena から外れる可能性
mrb_value task = mrc_create_task(ss->cc, ss->irep, name, ...);
// name.value.p = 0x2c0 = dangling pointer
```

**GC arena の問題が確定**: `mrb_str_new_cstr` で作った文字列は arena に入るが、
その後の `mrb_iv_set` 等で arena が進み、GC が走ると arena から外れた
文字列が回収される。`mrb_iv_set` で self に保存されているが、GC マーク中に
self が到達可能かどうかは mruby タスクスケジューラのコンテキスト依存。

### 修正5: mrb_value name の未初期化 -> 解決

`mrb_sandbox_initialize` の `mrb_value name;` が未初期化だった。

```c
// 修正前 (picoruby 側のバグ)
mrb_value name;                          // 未初期化 -> ゴミ値 0x2c0
mrb_get_args(mrb, "|S", &name);          // 引数なし -> name を書き換えない
if (mrb_nil_p(name)) { ... }            // ゴミ値の tt != MRB_TT_FALSE -> false
// name.value.p = 0x2c0 がそのまま使われる

// 修正後
mrb_value name = mrb_nil_value();        // 明示的に nil 初期化
mrb_get_args(mrb, "|S", &name);          // 引数なし -> name は nil のまま
if (mrb_nil_p(name)) { ... }            // true -> "sandbox" 文字列を作成
```

`mrb_get_args` の `|` (optional) は引数が指定されなかった場合、
変数を書き換えない仕様。C の自動変数が未初期化だとゴミ値が残る。

## 確定した根本原因

picoruby の `sandbox.c` の `mrb_sandbox_initialize` で `mrb_value name` が
未初期化のまま `mrb_get_args(mrb, "|S", &name)` に渡されていた。
`Sandbox.new` を引数なしで呼んだ場合、`name` は書き換えられず、
C スタック上のゴミ値 (0x2c0) が `mrb_nil_p` チェックをすり抜け、
そのままタスクの `t->name` に設定される。GC がこの不正な値をマークして SEGV。

## 適用したパッチ

### sandbox.c (picoruby submodule)
```diff
-  mrb_value name;
+  mrb_value name = mrb_nil_value();
```

### task.c (picoruby submodule)
- `mrb_task_reset_context`: スタック nil クリアと `ci->stack` リセットを追加 (予防的修正)

### Rakefile (fmruby-core)
- `rake clean` に `libpicoruby-esp32.a` 削除を追加

## ビルド時の注意

- `rake clean` は `picoruby/build/*` のみ削除し、`build/esp-idf/picoruby-esp32/libpicoruby-esp32.a` は削除しない
- `rake clean` に `rm -f build/esp-idf/picoruby-esp32/libpicoruby-esp32.a` を追加済み
- task.c の変更は `libmruby.a` (picoruby ビルド) に入る (`libpicoruby-esp32.a` ではない)
