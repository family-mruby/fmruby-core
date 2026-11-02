# mruby-task と Fiber を混ぜない

> 状態: 確定 | 更新: 2026-11-03 | Family mruby の gembox に mruby-fiber と mruby-enumerator を入れない。アプリは全部 Task で動くので、Fiber を使えば必ず Task と混ざり、mruby-task はその組み合わせを保証していない

## 1. 決まり

- `lib/add/family_mruby.gembox` (と、それを写す picoruby 側の gembox) に **mruby-fiber と
  mruby-enumerator を入れない**。
- mruby-enumerator を外すのは、外部イテレータ (`Enumerator#next`・`peek` など) が中で
  Fiber を使うため。使う側は Fiber を使ったと気づかないまま混ぜてしまう。
- 2026-11-03 時点で、どちらも入っていない (この決まりは今の状態を明文化したもの)。

## 2. 理由

Family mruby は、アプリの本体を最初から mruby の Task として走らせる
(`main/app/fmrb_app.c`: `mrc_create_task` のあと `mrb_task_run`)。Fiber を使えば
必ず「Task の中の Fiber」になる。

mruby-task の README (Limitations and Compatibility) は、Task と Fiber は同じ
`mrb_context` を使うが互換ではなく、同じアプリで混ぜないこと、としている。

実際に、mruby-checkpoint の原理検証 (`tmp/mruby-checkpoint`、mruby `f56d44ecc`) で次を
確かめた。どれも checkpoint を使わなくても起きる。

| 起きること | 結果 |
|---|---|
| Task の中で `Fiber.current` (`Enumerator#next` が呼ぶ) | `mrb_close` で不正な `free` |
| Task の中で Fiber が動いている最中にタイムスライスが尽きる | その Task が DORMANT にされ、黙って止まる |
| Task の中で `Fiber#transfer` | Task の残りが走らない |

上流の決まりどおりの使い方の外なので、上流には報告しない。

## 3. Fiber と関係ない mruby-task の不具合 (参考)

同じ検証で見つけた、Task だけで起きるもの。どちらも Family mruby では直してある
(`lib/patch/picoruby-mruby/lib/mruby/` の `src/vm.c` と `mrbgems/mruby-task/src/task.c`。
変えた所には `fmrb: backport` のコメントがある)。

### U2: 特異クラスを持てない値への `instance_exec` / `instance_eval` で Task が途中で終わる

- 症状: Task の中で Integer・Float・Symbol に `instance_exec` / `instance_eval` すると、
  ブロックから戻ったところで Task が終わる。`f56d44ecc` (Family mruby) では黙って終わり、
  ASan では次のタイムスライスで heap-buffer-overflow になる。上流の master (`90a5125`、
  2026-11-03) では SEGV (ASan: heap-buffer-overflow、`mrb_vm_exec` ← `execute_task_vm`)。
  nil・true・String・普通のオブジェクトでは起きない。再現:

  ```ruby
  Task.new { r = 1.instance_exec { 7 }; puts "after #{r}" }
  Task.run
  ```

- 原因: `mrb_callinfo` の共用体 `u` は、Ruby のフレームでは定義先のクラス
  (`target_class`) で、C 関数のフレームでは NULL が「Fiber の切り替えがあった」印
  (`keep_context`。`Fiber#resume`・`Fiber.yield`・`Fiber#transfer` が付け、`OP_SEND` と
  `OP_CALL` の cfunc エピローグが読む) になる。`instance_exec` はブロックのフレームの
  `target_class` に受け手の特異クラスを入れるが、Integer などは特異クラスを持てないので
  NULL になる。`OP_RETURN` には「C から駆動されているコンテキスト (`c->vmexec`) で、戻る
  フレームの `keep_context` が NULL なら `mrb_vm_exec` を抜ける」という判定があり、この
  NULL を切り替えの印と読み違えて、コンテキストの途中で VM を抜けていた。mruby-task は
  タイムスライスのあいだずっと `vmexec` を立てているので、Task では必ず起きる。
- Task に限らない: C から resume された Fiber (`mrb_fiber_resume`、`mrb_funcall` 経由の
  `Fiber#resume`) でも起きる。resume がブロックの値で返り、Fiber は生きたまま残りが走らない。
  §2 の「Task の中の Fiber」とは別の、mruby 本体 (vm.c) の不具合なので、上流に出す。
- 直し方: `OP_RETURN` のこの判定を消した。判定は、C から resume された Fiber の本体が
  ベースの上のダミーのフレームで走っていた頃 (2015 年) のもの。2021 年の `ca3a6156b` から、
  ダミーのフレームは `mrb_vm_exec` の前に外され、本体はベースのフレームで走る。C から駆動
  されたコンテキストは、ベースのフレームの経路 (`fiber_terminate` と `c->vmexec`)、外す
  フレームの cci (`CINFO_SKIP` / `CINFO_DIRECT`)、`Fiber.yield` の cfunc エピローグ
  (`CINFO_RESUMED`) のどれかで VM を抜けるので、この判定が拾う場合は残っていない。判定を
  print に置き換えて上流のテスト全体 (full-core の gembox + mruby-task、stress と bintest
  を含む) を流しても、一度も通らなかった。印を読む cfunc エピローグはそのまま、
  `mrb_callinfo` も大きくならない。
- 比べた案:
  - 切り替えの印を NULL 以外の専用の値にする: 共用体は `CI_TARGET_CLASS()`・`CI_ENV()`・
    GC (`target_class` をマークする) からも読まれ、`exec_irep()` は自分が積むフレームの NULL
    に頼っている。読み手を全部直すことになるのに、守る先の判定には拾う場合が無い。
  - ブロックの `target_class` を NULL 以外にする: `1.instance_eval { def m; end }` の挙動
    (TypeError "no class/module to add method") が変わる。
  - mruby-task がタイムスライスのあいだ `vmexec` を立てない: C から resume した Fiber の側は
    直らない。`vmexec` は Task の未処理の例外の抜け方 (`L_RAISE`) も決めている。
- `1.instance_eval { def m; end }` は今までどおり TypeError になる。
- 状態: 上流向けの修正は mruby のブランチ `fix-task-instance-exec-immediate` (未 push、PR は
  未作成。Task と、C から resume した Fiber のテストを足した)。Family mruby は
  `lib/patch/.../src/vm.c` に同じ変更を入れた。mruby-checkpoint も patch で入れた
  (`patches/0003-vm-return-target-class.patch`)。

### U4: main の Task の名前と戻り値が GC で回収される

- 症状: Task の外で `Task.current` が作る main の Task の名前 (と戻り値) が次の GC で
  回収され、`Task.current.name` が使い回された領域の別のオブジェクトを返す (ホストの確認では
  `"main"` が `"1564"` になった)。
- 原因: main の Task はキューに入らないので、`mrb_task_mark_all()` のキューの走査から届かない。
  `mrb_gc_register()` が守るのはラッパーのオブジェクトだけで、`mrb_task` の構造体の中の値
  (self・result・name) はどこからもマークされない。
- 直し方: 上流の `628ccec60` (mruby-task: mark the main task's values during GC) を、そのまま
  `lib/patch/.../mrbgems/mruby-task/src/task.c` に移した。`mrb_task_mark_all()` の最初
  (キューが空のときに抜ける前) で main の Task の self・result・name をマークする。
- 状態: Family mruby に入れた。上流は直っている。

### 確認

- ホスト: `f56d44ecc` に `lib/patch` の `vm.c`・`task.c`・`gc.c` を重ねた mruby を ASan つきで
  作り、上流のテスト (default の gembox + mruby-task、U2 と U4 のテストを足したもの) が全部
  通る。修正前は U2 の再現で heap-buffer-overflow、U4 で名前が別の文字列になる。
- sim と `rake regress` は本体の checkout に取り込んでから行う (worktree では sim を動かせない)。
