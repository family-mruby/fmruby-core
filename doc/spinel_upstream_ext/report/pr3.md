# 報告 PR3: 提出前の最終準備 (最新の上流へ載せ替え)

> 状態: 完了 | 更新: 2026-09-26 | 6 本すべてを新しい基点 `9fb2cf02` へ衝突なしで載せ替えた (中身は旧と同一)。最小再現は 6 件とも新しい基点でまだ再現し、上流で解消済みのものは無い。`make gate` は全ブランチで基点から退行なし。`pr/U-6` は修正と検査の 2 コミットにし、検査の型名は `emit_class_struct` と同じ関数 `emit_ivar_field_ctype` で作るように直した。PR の題と本文は下にそのまま置いた

## 前提

| 項目 | 値 |
|---|---|
| clone | `/home/kishima/fmrb/wt/spinel-pr/` (以下 `$W`) |
| **新しい基点** | `9fb2cf0245e587f318c550c430d558867aa6dd76` (origin/master、2026-09-26 14:43:53 +0900、"Array#flatten splices in an element that answers #to_ary")。旧基点 `01521b1e` から 49 commit 進んでいる |
| ツールチェーン | `cc 11.4.0`、`ruby 3.2.6`、WSL2 x86_64、24 コア |
| 作業ファイル | `$S = /tmp/claude-1000/-home-kishima-fmrb-family-mruby/a0ea00a0-6754-4693-ae2d-f05782a9784d/scratchpad/pr3/` (`gate.sh` `queue.sh` `cdiff.sh` `gen.sh` `m32.sh` と各ログ、`sd/<ID>/` に `spinel diff` の出力) |

push・PR・Issue はしていない。`~/dev/spinel`、`wt/spinel-rebase`、fmruby の
本体 checkout・build/・sim・実機には触れていない。fmruby-core で書いたのは
このファイルだけ (台帳は編集していない)。

`make gate` の各項目は `$W` 本体を detached で切り替えて 1 本ずつ順に回した
(並べると 10 秒の timeout で偽の失敗が出るため)。生成 C の比較と 32bit の
確認には `$W/.worktrees/` に worktree を作り、最後にすべて消した。

## 結果の一覧

出す順番は指示書どおり **U-11 → P-9 → U-10 → P-3 → U-6 → U-12** (変える理由は無かった)。

| 順 | ID | ブランチ | 最終 commit (旧) | 変更 | `make test` | bench / optcarrot / rubyspec / props | 生成 C が変わったテスト |
|---|---|---|---|---|---|---|---|
| 1 | U-11 | `pr/U-11` | `7744003a` (`2b14a9c3`) | `src/codegen_expr.c` +2/-2、`src/codegen_stmt.c` +2/-2、テスト +29 / +6 | 4,076 / 1 / 0 | 62/0 / OK / pass / pass | 新テスト 1 本 |
| 2 | P-9 | `pr/P-9` | `c4e7917b` (`848d6389`) | `src/codegen_call.c` +8/-3、`test/ffi_variadic.rb` +3/-3 | 4,075 / 1 / 0 | 62/0 / OK / pass / pass | 3 本 (`ffi_variadic` `ffi_io_buffer_arg` `ffi_header_declared_extern`。キャストの綴りだけ) |
| 3 | U-10 | `pr/U-10` | `0070e0a6` (`45adc6c9`) | `src/codegen_call.c` +12/-7、テスト +23 / +5 | 4,076 / 1 / 0 | 62/0 / OK / pass / pass | 3 本 (新テスト、`format_splat_array_variable`、`io_instance_read_surface`) |
| 4 | P-3 | `pr/P-3` | `4676f6ac` (`3080c1f7`) | `lib/sp_gc.c` +6/-1、`lib/spinel_rt.h` +15 | 4,075 / 1 / 0 | 62/0 / OK / pass / pass | 0 本 (`-D` 無しの `cc -E -P` も基点と一致) |
| 5 | U-6 (修正) | `pr/U-6` の 1 つ目 | `bd194b0a` (`b49540e4`) | `src/analyze.c` +46/-28、テスト +32 / +2 | 4,076 / 1 / 0 | 62/0 / OK / pass / pass | 2 本 (新テスト、`super_attr_mid_redeclare`) |
| 5 | U-6 (検査) | `pr/U-6` の 2 つ目 (= 先端) | **`b0808eb4`** (`4af0a94c` を作り直し) | `src/codegen.c` +64/-11 | 4,076 / 1 / 0 | 62/0 / OK / pass / pass | 修正コミットと同じ 2 本 (検査コミットは生成 C を 1 バイトも変えない) |
| 6 | U-12 | `pr/U-12` | `31e750ad` (`f44d4b06`) | `src/codegen_util.c` +36、`src/codegen_internal.h` +1、`src/codegen_stmt.c` +11/-7、`src/codegen_call_recv.c` +10/-30、`src/codegen_call.c` +1/-2、テスト +119 / +48 | 4,076 / 1 / 0 | 62/0 / OK / pass / pass | 新テスト 1 本 |

**基点 `9fb2cf02`**: `make test` 4,075 pass / 1 fail / 0 error、`make bench`
62 / 0、optcarrot OK、rubyspec-gate pass、gate-props pass。1 fail は
全ブランチ共通の `systemcallerror_hierarchy` (`TCPSocket.new("127.0.0.1", 1)`
がこのホストでは固まって timeout になる。PR1 と同じ環境の問題)。各ブランチの
+1 は新しく足したテスト。`make gate` の他の項目は全部通った。

生成 C の比較は、基点のコンパイラと各ブランチのコンパイラで、そのブランチの
`test/*.rb` (4,010 本、新テストがあれば 4,011 本) を `-c --no-line-map` で
C にして比べた (`$S/cdiff.sh`、結果は `$S/cd/<ID>/diffs.txt`)。生成に失敗した
ものは全ブランチで両側とも 0 本。

## 載せ替え

- 旧基点 `01521b1e` から新基点 `9fb2cf02` へ `git rebase --onto` で載せ替えた。
  **6 本とも衝突なし**。`git range-diff` で全部 `=` (パッチの中身が旧と同一)
  なので、差分に無関係な変更は入っていない。直し方を変える必要があったものも無い。
- 新基点の 49 commit のうち、関係しそうな場所に触れたのは
  `f483bedf` (codegen のクラス到達判定) や `ebf7a82e` (共有可変 String の
  poly 読み) などだが、どれも今回の 6 本の不具合を直していない (下の再現確認)。

### 新しい基点での最小再現

`fmruby-core/doc/spinel_upstream_ext/repro/` の各再現から先頭のコメントを
除いたものを `repro.rb` として、新基点の `bin/spinel diff` にかけた。

| ID | 新基点での `spinel diff` | ブランチでの `spinel diff` |
|---|---|---|
| U-11 | `link-error` (生成 C が通らない) | `same` |
| U-10 | `link-error` | `same` |
| U-6 | `exception-diff` (TypeError) | `same` |
| U-12 | `output-diff` | `same` |
| P-9 | 64bit では再現しない。32bit の `spinel` で SIGSEGV (exit 139) | 32bit で `1-22-hi` |
| P-3 | Ruby の再現ではない。新基点で `-D` を渡すと `redefined` の警告 4 件 | 0 件 |

**上流で既に直っていて出さないものは無い**。

`spinel diff` は CRuby と比べる道具なので、`ffi_func` を持たない CRuby では
P-9 は比べられない (CRuby 側が NoMethodError になる)。P-9 の本文には
32bit でのそのままの実行結果を載せた。P-3 は動作ではなくビルドの口の話なので、
`cc` の警告を載せた。

出力中の作業場所の絶対パスは `lib/...` に、一時ファイル名は
`/tmp/spinel_out_XXXX_0.c` に置き換えてある (内容は変えていない)。

## `pr/U-6` の 2 コミット化と関数の共通化

- 1 つ目 `bd194b0a` は旧 `pr/U-6` (`b49540e4`) を載せ替えたもの。
- 2 つ目 `b0808eb4` は旧 `pr/U-6-check` (`4af0a94c`) を載せ、型名の作り方を
  直したもの。
  - 旧の検査は `ivar_field_ctype()` の中で `emit_class_struct` の型の決め方
    (void / nil を poly に広げ、unknown を sp_int で宣言し、`emit_ctype` で綴る)
    を別に写していた。
  - これを `emit_ivar_field_ctype(Compiler *, TyKind, Buf *)` 1 つにまとめ、
    `emit_class_struct` の 2 つのループ (例外の子クラス用と通常のクラス用) も
    検査もこの関数を通るようにした。`ivar_field_ctype()` は Buf に書かせて
    文字列を返すだけの薄い包みになった。
  - 通常クラスのループにあった何もしない行
    (`if (!is_scalar_ret(t) && t != TY_UNKNOWN) { /* ok */ }`) は、`t` が
    関数の中へ移ったので消した。
  - **既定の出力は変わらない**: 1 つ目のコミットのコンパイラと 2 つ目の
    コンパイラで、コーパス全部の生成 C が (テストのパスの長さを除いて) 同一。
- 検査の働きの確認:
  - 新基点に検査コミットだけを当てたコンパイラ (確認用の worktree) では、
    コーパスで止まるのは `super_attr_mid_redeclare` の 1 本だけ
    (`@x is sp_RbVal in Parent but sp_int in its ancestor Grand`)。他の生成 C は
    基点と同一。最小再現 `repro/U-6.rb` も
    `@items is sp_RbVal in Child but sp_PolyArray * in its ancestor Base` で止まる。
  - 2 コミットそろった `pr/U-6` では止まるものは 0 本。1 つ目だけでも
    `make gate` は通る (上の表)。よって保守者が検査コミットだけを落としても
    成り立つ。
- 旧ブランチ `pr/U-6-check` (`4af0a94c`) と確認用の `check/U-6+check`
  (`7dd035a4`) は旧基点のまま残してある。もう使わないので、親の判断で消してよい。

## P-9 の 32bit 確認

PR1 と同じく、空の `crypt()` だけを持つ 32bit の `libcrypt.a`
(`scratchpad/pr1/stub32/`) を `LIBRARY_PATH` で見せ、新基点と `pr/P-9` を
丸ごと `make CC='cc -m32'` でビルドしてから
`make test-corpus CC='cc -m32' OPT=-O1` を回した。

| | pass | fail | error |
|---|---|---|---|
| 新基点 | 4,014 | 4 | 0 |
| `pr/P-9` | **4,015** | 4 | 0 |

+1 は `ffi_variadic` (基点では `# spinel: int64` で 32bit から外れていて走らない)。
4 fail は両方で同じ: `string_crypt` `poly_string_surface`
`string_nil_float_batch` (どれも `String#crypt` を呼ぶ。空の代用品のせい) と
`systemcallerror_hierarchy` (環境)。

作業上の注意: 最初は `make` を省いて `make test-corpus CC='cc -m32'` だけを
回し、スレッドを使う 119 本が ERR になった (`packages/*/sp_*_mt.o` が作られず、
TLS の食い違いでリンクが落ちる)。`make` を先に回せば出ない。基点・P-9 で
同じ 119 本だったので P-9 とは無関係 (下の PR 候補 N-9)。

---

## PR の題と本文

各節の「題」を `--title`、「本文」を `--body` にそのまま渡せる。試験結果の
数字は新基点 `9fb2cf02` でのもの。

### 1. U-11 (`pr/U-11`、`7744003a`)

題:

```
Coerce a poly right side of += on a String global or class variable
```

本文:

````markdown
`$g += h[:a]` and `@@v += h[:a]`, with `$g` / `@@v` a String and `h[:a]` a poly (a Symbol-keyed hash value), emit C that does not compile.

```ruby
h = { a: "ab", n: 1 }
$g = +"g"
$g += h[:a]
puts $g
class C
  @@v = +"v"
  def self.f(h)
    @@v += h[:a]
    @@v
  end
end
puts C.f(h)
```

`spinel diff` on master (9fb2cf02):

```
spinel diff: link-error
  program: repro.rb
  ruby:    exit 0
  spinel:  the C did not build

repro.rb: In function 'sp_C_s_f':
repro.rb:8: error: incompatible type for argument 2 of 'sp_str_concat'
In file included from lib/sp_array.h:374,
                 from lib/spinel_rt.h:14,
                 from /tmp/spinel_out_XXXX_0.c:2:
lib/sp_str.h:74: note: expected 'const char *' but argument is of type 'sp_RbVal'
repro.rb: In function '_sp_main_body':
repro.rb:3: error: incompatible type for argument 2 of 'sp_str_concat'
In file included from lib/sp_array.h:374,
                 from lib/spinel_rt.h:14,
                 from /tmp/spinel_out_XXXX_0.c:2:
lib/sp_str.h:74: note: expected 'const char *' but argument is of type 'sp_RbVal'
spinel: C compilation failed
```

### Cause

The String arms of `+=` on a global or class variable emit

```c
gv_g = sp_str_concat(gv_g, sp_SymPolyHash_get(lv_h, ...));
```

with the right side emitted through `emit_expr`, so a poly right side reaches the `const char *` parameter as a raw `sp_RbVal`. The value-position forms (`x = ($g += v)`, and `sp_str_plus` for a class variable) have the same hole. The local-variable arm already coerces a poly right side (#2875), and an instance variable goes through its own arm; globals and class variables were left.

### Fix

The four arms (statement and value position, global and class variable, in `codegen_stmt.c` and `codegen_expr.c`) now emit the right side through `emit_str_expr`, the String argument slot. A typed String passes through unchanged, so no program that compiled before changes; a poly one converts with `sp_poly_arg_str_chk`, which raises CRuby's `TypeError` for nil ("no implicit conversion of nil into String").

### Regression test

`test/str_opassign_poly_gvar_cvar.rb`: statement and value position, a class variable inside a class method, and the nil `TypeError`. On master its C does not compile (5 errors).

### Checked

- `make test`: 4076 pass / 1 fail / 0 error (master: 4075 / 1 / 0; +1 is the new test). The one failure is `systemcallerror_hierarchy` on both: `TCPSocket.new("127.0.0.1", 1)` hangs into the timeout on this host instead of being refused.
- `make bench` 62/0, `make optcarrot` OK, `rubyspec-gate` and `gate-props` pass.
- Across the test corpus (every `test/*.rb` through `-c --no-line-map`, master vs this branch) only the new test's C changes.
````

### 2. P-9 (`pr/P-9`、`c4e7917b`)

題:

```
Pass an FFI Integer vararg at the target's Integer width
```

本文:

````markdown
On a 32-bit target, an FFI call with a trailing `:varargs` spec shifts every vararg after an Integer by 4 bytes.

```ruby
module C
  ffi_func :printf, [:str, :varargs], :int
end
C.printf("%d-%d-%s\n", 1, 22, "hi")
```

With a 32-bit spinel built from master (9fb2cf02, `make CC='cc -m32'`):

```
$ bin/spinel repro.rb -o r && ./r
Segmentation fault (core dumped)       # exit 139; LP64 prints 1-22-hi
```

(`spinel diff` does not apply here, since CRuby has no `ffi_func`; under `spinel diff` the 32-bit build reports `crash ... SIGSEGV`.)

### Cause

The extra arguments of a `:varargs` call are promoted by their inferred type, and an Integer (or bool) is cast to `long long`. On LP64 that is harmless: a `%d` reads the low half of an 8-byte slot and the next argument starts at the next slot. On ILP32 a `%d` reads 4 bytes and the next conversion reads the high half of the `long long` instead of the following argument. Above, the second `%d` prints the high half of 1 and `%s` takes the low half of 22 as a pointer.

### Fix

Cast to `sp_int` instead, the target's Integer width (`intptr_t`). On LP64 that is 8 bytes, as `long long` was, so the call passes the same bits; on ILP32 it is a 4-byte int, which is what `%d` reads.

### Regression test

`test/ffi_variadic.rb` was kept off the 32-bit lane with `# spinel: int64`. The marker is gone (and the promotion note in the test updated), and the test now passes there.

### Checked

- 32-bit, `make test-corpus CC='cc -m32' OPT=-O1` (the linux32 lane's command; the host has no i386 libcrypt, so an empty `crypt()` stub was on `LIBRARY_PATH`): master 4014 pass / 4 fail, this branch 4015 pass / 4 fail. The +1 is `ffi_variadic`. The same 4 fail on both: three call `String#crypt` (the stub) and `systemcallerror_hierarchy` (below).
- `make test`: 4075 pass / 1 fail / 0 error, the same as master. The failure is `systemcallerror_hierarchy` on both: `TCPSocket.new("127.0.0.1", 1)` hangs into the timeout on this host instead of being refused.
- `make bench` 62/0, `make optcarrot` OK, `rubyspec-gate` and `gate-props` pass.
- Across the test corpus the C changes only for the three tests that pass varargs (`ffi_variadic`, `ffi_io_buffer_arg`, `ffi_header_declared_extern`), and there only in the cast's spelling, `(long long)(` to `(sp_int)(`, which is the same 8-byte type on LP64.
- `ffi_io_buffer_arg` keeps its `# spinel: int64` marker: it also sums past 2^31, which is 64-bit for a reason other than varargs.
````

### 3. U-10 (`pr/U-10`、`0070e0a6`)

題:

```
Emit a format string before opening its temp's declaration
```

本文:

````markdown
When the format string of `sprintf` / `format` is a call that needs a GC root, the generated C does not compile.

```ruby
module I18n
  STRINGS = { done: "done %d", fail: "fail" }
  def self.t(key)
    STRINGS[key] || key.to_s
  end
end
class App
  def initialize
    @status = "idle"
  end
  def run(res)
    if res && res[:ok]
      deleted = res[:deleted] || 0
      @status = sprintf(I18n.t(:done), deleted)
    else
      @status = I18n.t(:fail)
    end
    puts @status
  end
end
App.new.run({ ok: true, deleted: 3 })
App.new.run(nil)
I18n.t("x")
```

`spinel diff` on master (9fb2cf02):

```
spinel diff: link-error
  program: repro.rb
  ruby:    exit 0
  spinel:  the C did not build

repro.rb: In function 'sp_App_run':
repro.rb:14: error: incompatible types when initializing type 'const char *' using type 'sp_RbVal'
spinel: C compilation failed
```

### Cause

`Kernel#format` / `#sprintf` lower to

```c
const char *_tN = <fmt>; SP_GC_ROOT_STR(_tN);
```

in g_pre, and the line was opened before `<fmt>` was emitted. When the format is itself a call that roots its operands -- here `t` takes a poly parameter, so the Symbol is boxed into a GC frame slot first -- that rooting statement is pushed to g_pre while the declaration is half written, and lands inside its initializer:

```c
const char *_t4 =     _gcf.v[0] = sp_box_sym(((sp_sym)0));
sp_poly_arg_str_chk(sp_I18n_s_t(_gcf.v[0])); SP_GC_ROOT_STR(_t4);
```

The arguments already avoid this by emitting into a local buffer first (#1498 / #1508); the format was left.

### Fix

Emit the format into a local buffer first, then write the declaration line whole.

`IO#printf` has the same shape and, in addition, emitted the format with `emit_expr`, so a poly format was passed raw into the `const char *` slot. It now goes through `emit_str_expr` like `Kernel#format`'s, and is rooted the same way, since every argument after it boxes.

### Regression test

`test/format_string_rooting.rb`: `sprintf`, `format` and `$stdout.printf` with `Msgs.t(:done)` as the format. On master its C does not compile (3 errors).

### Checked

- `make test`: 4076 pass / 1 fail / 0 error (master: 4075 / 1 / 0; +1 is the new test). The one failure is `systemcallerror_hierarchy` on both: `TCPSocket.new("127.0.0.1", 1)` hangs into the timeout on this host instead of being refused.
- `make bench` 62/0, `make optcarrot` OK, `rubyspec-gate` and `gate-props` pass.
- Across the test corpus the C changes for three tests: the new one, and `format_splat_array_variable` and `io_instance_read_surface`, where an `IO#printf` format gains its root (`SP_GC_ROOT_STR(_tN)` and one GC frame slot). `Kernel#format`'s C is byte-identical unless the format pushes a rooting statement.
````

### 4. P-3 (`pr/P-3`、`4676f6ac`)

題:

```
Guard the exception, catch and GC mark stack sizes with #ifndef
```

本文:

````markdown
The runtime's capacity constants mostly let the build choose them: `SP_GC_STACK_MAX` (`lib/sp_gc.h`) and `SP_DYN_SYMS_MAX` (`lib/spinel_rt.h`) are wrapped in `#ifndef`, so `-DSP_GC_STACK_MAX=<n>` sizes the root array without touching the source. Three constants of the same kind are still unconditional:

| constant | sizes |
|---|---|
| `SP_EXC_STACK_MAX` | the begin/rescue/ensure handler stack |
| `SP_CATCH_STACK_MAX` | the catch/throw stack |
| `SP_GC_MARK_STACK_MAX` | the collector's initial mark stack (defined in both `lib/sp_gc.c` and `lib/spinel_rt.h`) |

Passing `-D` for any of them only draws a "redefined" warning, and the header's value wins. On master (9fb2cf02), with a generated program (`test/catch_depth_bounded.rb`) and `lib/sp_gc.c`:

```
$ cc -fsyntax-only -Ilib -DSP_EXC_STACK_MAX=8 -DSP_CATCH_STACK_MAX=8 -DSP_GC_MARK_STACK_MAX=64 catch_depth_bounded.c
$ cc -fsyntax-only -Ilib -DSP_GC_MARK_STACK_MAX=64 lib/sp_gc.c
```

gives four `"SP_..." redefined` warnings (`SP_GC_MARK_STACK_MAX` in `spinel_rt.h` and in `sp_gc.c`, `SP_EXC_STACK_MAX`, `SP_CATCH_STACK_MAX`). With this change there are none.

(This is a build knob, not a behavior difference, so there is no `spinel diff` output to show.)

### Change

Wrap the four definitions in `#ifndef`, the same shape `SP_GC_STACK_MAX` and `SP_DYN_SYMS_MAX` have, with a comment on what each one sizes.

### Checked

- Nothing passes `-D` by default, so nothing changes: `cc -E -P` of `lib/sp_gc.c` and of a generated program (`test/catch_depth_bounded.rb`) is byte-identical before and after, and no test's generated C changes.
- A lower depth fails safely: handler pushes are bounds-checked and raise `SystemStackError` (`sp_stack_too_deep`), and the mark stack still grows past its initial capacity on demand.
- No regression test: the default output is unchanged by design, and the `-D` path does not fit the corpus's compare-with-CRuby form.
- `make test`: 4075 pass / 1 fail / 0 error, the same as master. The failure is `systemcallerror_hierarchy` on both: `TCPSocket.new("127.0.0.1", 1)` hangs into the timeout on this host instead of being refused.
- `make bench` 62/0, `make optcarrot` OK, `rubyspec-gate` and `gate-props` pass.
````

### 5. U-6 (`pr/U-6`、`bd194b0a` + `b0808eb4`)

題:

```
Carry a late-widened subclass ivar type back to the base
```

本文:

````markdown
An inherited ivar can end up with a wider C type in the subclass struct than in the base struct. Every later field then sits at a different offset, and a method inherited from the base writes the subclass's fields through the wrong layout. Nothing fails where the layouts diverge; a wrong value or a `TypeError` shows up later, in a method that does not touch the widened ivar.

```ruby
class Base
  def initialize
    @items = []
    @w = 640
    @h = 480
  end
  def attach(x)
    @items << x
  end
  def size_text
    "#{@w}x#{@h}"
  end
end
class Ui
  def initialize(app)
    app.attach(self)
  end
end
class Child < Base
  def area
    @w * @h
  end
end
c = Child.new
puts c.size_text
puts c.area
```

`Ui` is never instantiated; its `app.attach(self)` is what widens `@items`. `spinel diff` on master (9fb2cf02):

```
spinel diff: exception-diff
  program: repro.rb
  ruby:    exit 0
  spinel:  exit 1
  exception (ruby):   (none)
  exception (spinel): TypeError: nil can't be coerced into Integer

--- stdout (ruby)
+++ stdout (spinel)
@@ -1,2 +1 @@
 640x480
-307200
```

### Cause

An inherited method is emitted once and called through a cast to the class that defines it, `sp_Base_m((sp_Base *)self)`, so the base struct has to stay a common initial sequence of every subclass struct: same ivars, same order, same C types. `inherit_members` keeps the names and the order; the types are kept in line by the up-propagation fixpoint (child -> parent) and `infer_inherited_ivars` (parent -> child).

The up-propagation runs once, before the poly fallback of unresolved locals and params. The final loop after it re-runs `infer_ivar_types` and `infer_inherited_ivars` precisely because that fallback can widen what feeds an ivar -- so it can widen a subclass's copy of an inherited ivar, and nothing carries that back up. Above, `@items` settles as `sp_PolyArray *` in `Base` and `sp_RbVal` in `Child`. The wider field moves `@w` and `@h`; `Base#initialize` writes them at `Base`'s offsets, and `Child#area` reads its own, still nil.

### Fix (first commit)

Factor the up-propagation into `propagate_ivars_up()` and run it in the final loop too. `ty_unify` only widens, so the loop stays monotonic and ends on its existing bound. Most of the diff in `analyze.c` is the move into the function.

### Check (second commit)

The second commit makes the compiler refuse to emit a class struct that is not a prefix of its subclass's: each ancestor's ivars must lead the class's, with the same names, in the same order, declared with the same C type. Native classes and builtin reopens are skipped, as is an ancestor on the other side of the `sp_Exception` header. On a mismatch it stops and names both classes, the ivar and both C types; without the first commit, the reproducer above stops with

```
spinel: class layout: @items is `sp_RbVal` in Child but `sp_PolyArray *` in its ancestor Base; methods inherited from Base would reach Child's fields through a different layout
```

The C type of an ivar field (void and nil widened to poly, unknown declared as `sp_int`) moves out of `emit_class_struct`'s two loops into `emit_ivar_field_ctype`, and the check spells both sides through that same function, so it compares exactly what the structs declare. The emitted structs are unchanged: across the test corpus the second commit changes no generated C.

**The check is a separate commit, so it can be dropped if you would rather not have it**; the fix stands on its own and passes `make gate` without it.

### Regression test

`test/inherited_ivar_late_widen_layout.rb`, the reproducer above.

It also turned out that an existing test carried the same mismatch: in `super_attr_mid_redeclare`, `Grand` held `@x` as `sp_int` while `Parent`, `Mid` and `Leaf` held `sp_RbVal`. The test's output happened not to expose it. With the fix all four hold `sp_RbVal`; with the check alone (without the fix), that test is the one corpus program the check stops.

### Checked

- `make test`: 4076 pass / 1 fail / 0 error at both commits (master: 4075 / 1 / 0; +1 is the new test). The one failure is `systemcallerror_hierarchy` on all three: `TCPSocket.new("127.0.0.1", 1)` hangs into the timeout on this host instead of being refused.
- `make bench` 62/0, `make optcarrot` OK, `rubyspec-gate` and `gate-props` pass, at both commits.
- Across the test corpus the C changes for two tests: the new one and `super_attr_mid_redeclare` (above). No corpus program trips the check.
````

### 6. U-12 (`pr/U-12`、`31e750ad`)

題:

```
Apply String mutators on a global, class variable or constant receiver
```

本文:

````markdown
An in-place String mutation is silently lost when the receiver is a global, a class variable or a constant.

```ruby
$g = +"g"
$g << "ab"
puts $g
$h = String.new
$h << "xy"
puts $h.size
$u = +"g"
$u.upcase!
puts $u
```

`spinel diff` on master (9fb2cf02):

```
spinel diff: output-diff
  program: repro.rb
  ruby:    exit 0
  spinel:  exit 0

--- stdout (ruby)
+++ stdout (spinel)
@@ -1,3 +1,3 @@
-gab
-2
-G
+g
+0
+g
```

The value position and constants lose it the same way, and `clear` does not resolve at all:

```ruby
x = ($g << "q")    # $g unchanged (CRuby: gabq)
$g.clear           # NoMethodError: undefined method 'clear' for String
S = +"c"
S << "d"
puts S             # c (CRuby: cd)
```

### Cause

A String is a `const char *` value, so an in-place mutator is lowered to a reassignment of its receiver: `s << x` -> `s = sp_str_append_grow(s, x)`, `s.upcase!` -> `s = sp_str_upcase(s)`, and so on. Both the statement arms (`codegen_stmt.c`) and the value arms (`codegen_call_recv.c`, and `clear` in `codegen_call.c`) write back only to a local or an ivar (and, in statement position, self). A global, a class variable or a constant fell through to the form that builds the new string and drops it.

### Fix

`gv_g`, `cvar_C_v` and `cst_S` are plain C lvalues holding the string, the same as a local, so they take the same reassignment. The receiver checks, which were copied into some fifteen places, now go through one predicate, `str_mut_var_recv`. It adds `GlobalVariableReadNode`, `ClassVariableReadNode` and a constant whose read is its `cst_` slot; a `Class.new`-guarded constant, or a `klass::NAME` path whose owner is a run-time value, reads through an expression that is not an lvalue and keeps the old form. The statement arms add self on top of it.

That covers `<<`, its chains, `concat`/`prepend`/`insert`, the bang methods with and without arguments, `slice!`, `replace`/`clear`, `setbyte` and `bytesplice`, in statement and value position (assigned, passed as an argument, returned from a method). A frozen receiver still raises `FrozenError`. A generated matrix of 4 receivers (global, class variable, top-level and class-level constant) x 17 mutators x 4 positions matches CRuby in all 272 cases (16 before, all of them `squeeze!` calls that change nothing).

Not covered here: aliasing. `t = $g; $g << "x"` still does not reach `t` unless the append happens in place; locals and ivars get that from the shared-mutable String (`TY_STRBUF`) promotion, which does not track globals or constants.

### Regression test

`test/str_mutate_gvar_cvar_const.rb`: globals, class variables (in class and instance methods) and constants (top level, in a class, reached as `K::U` from outside, a `String.new` constant), in statement and value position, and frozen receivers raising `FrozenError` with the value unchanged. On master the first line already differs and the run stops at `$g.clear`.

### Checked

- `make test`: 4076 pass / 1 fail / 0 error (master: 4075 / 1 / 0; +1 is the new test). The one failure is `systemcallerror_hierarchy` on both: `TCPSocket.new("127.0.0.1", 1)` hangs into the timeout on this host instead of being refused.
- `make bench` 62/0, `make optcarrot` OK, `rubyspec-gate` and `gate-props` pass.
- Across the test corpus only the new test's C changes.
````

PR2 の下書きにあった「The test replaces test/str_mutate_gvar_cvar.rb」の一文は
外した。そのファイルは PR1 で自分が足したもので、上流には存在しないため。

---

## PR 候補 (作業中に見つけたもの)

台帳は編集していない。PR2 の N-8 の続きで仮 ID `N-9` から。

既存の候補の新基点での再確認:

| ID | 新基点 `9fb2cf02` での状態 |
|---|---|
| U-19 (= PR2 の N-6、値の位置の `replace` にリテラルで受け手が凍結) | まだ再現 (`spinel diff`: `exception-diff`、FrozenError: can't modify frozen String: "rep") |
| PR2 の N-7 (poly の受け手で値の位置の `<<` が受け手に残らない) | まだ再現 (`spinel diff`: `output-diff`、最後の行が `"ab"`、CRuby `"abc"`) |

新規:

| 仮 ID | 内容 | 最小再現 | 確認した上流 | 見込み |
|---|---|---|---|---|
| N-9 | `make test-corpus` が、スレッドを使うテストがリンクする `packages/*/sp_*_mt.o` (`BUNDLED_NATIVE_MT_OBJS`) に依存していない。`make` を先に回さずに `make test-corpus CC='cc -m32'` だけを実行すると、スレッドを使う 119 本が `sp_gc_nroots: TLS reference ... mismatches non-TLS reference in packages/stringio/sp_stringio.o` でリンクに失敗し ERR になる。`make` を先に回せば 0 本 | 32bit の worktree で `make test-corpus CC='cc -m32' OPT=-O1` だけ | `9fb2cf02` | 低 (Makefile の依存を 1 つ足すだけ。上流の CI は先に `make` するので困っていない。作業手順の注意としても記録) |

## 後片付けと残したもの

- ブランチ (出すもの): `pr/U-11` `pr/P-9` `pr/U-10` `pr/P-3` `pr/U-6` `pr/U-12`。
  すべて `9fb2cf02` の上。
- 旧基点のまま残したブランチ: `pr/U-6-check` (`4af0a94c`)、`check/U-6+check`
  (`7dd035a4`)。`pr/U-6` の 2 つ目のコミットに置き換わったので不要。
  消すかは親の判断。
- 載せ替え前の控えとして作った `old/*` ブランチは消した (旧 commit は上の表と
  reflog にある)。
- `$W/.worktrees/` に作った worktree (base、各 ID、U-6fix、chk、m32base、
  m32p9) はすべて `git worktree remove` で消した。
- `$W` 本体は `pr/U-12` (`31e750ad`) を detached で checkout した状態。本体の
  `build/` と `bin/` は最後の `make gate` で作った `pr/U-12` のもの。
- `spinel diff` が `/tmp/spinel_out_*.c` に残した生成 C のうち、今回の作業で
  できたものは消した (それ以前からあるものには触れていない)。
