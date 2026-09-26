# 報告 PR2: PR 準備の更新 (U-12 の拡張と P-3 の書き直し)

> 状態: 完了 | 更新: 2026-09-26 | `pr/U-12` を U-16 (値の位置) と U-18 (定数) まで広げて 1 コミットに作り直した。`make test` 4,055 / 1 / 0、`make bench` 62 / 0 で基点から退行なし、生成 C が変わるのはコーパスで新テストだけ。`pr/P-3` はメッセージとコメントを「上流の流儀に揃える」理由で書き直した (`3080c1f7`)。U-17 (別名) は直していない

## 前提

| 項目 | 値 |
|---|---|
| clone | `/home/kishima/fmrb/wt/spinel-pr/` (以下 `$W`)。基点 `01521b1e` |
| 作業場所 | `$W/.worktrees/{base,U-12,P-3}` を作って使い、最後に 3 つとも `git worktree remove` で消した (ブランチは残した)。worktree には `vendor/prism` `vendor/rbs` が無いので `$W/vendor/` から写してビルドした |
| `make test` / `make bench` | PR1 と同じく `$W` 本体を detached で切り替えて回した (`$S/gate.sh`) |
| 作業ファイル | `$S = /tmp/claude-1000/-home-kishima-fmrb-family-mruby/a0ea00a0-6754-4693-ae2d-f05782a9784d/scratchpad/pr2/` (`gen.rb` `run.sh` と各出力) |

push・PR・Issue はしていない。`~/dev/spinel`、`wt/spinel-rebase`、fmruby の
build/・sim・実機には触れていない。fmruby-core で書いたのはこのファイルだけ。

`$W` 本体は最後に `pr/P-3` (新しい commit) を detached で checkout した状態に
戻した。本体の `build/` と `bin/` は直前の試験で作った `pr/U-12` のもの。

## 結果の一覧

| ID | ブランチ | 新しい commit | 旧 commit | 変更 | `make test` | `make bench` |
|---|---|---|---|---|---|---|
| U-12 (+U-16, U-18) | `pr/U-12` | `f44d4b06` | `e0c71c4b` | 下の表 (7 ファイル、+226 / -39) | 4,055 pass / 1 fail / 0 error | 62 / 0 |
| P-3 | `pr/P-3` | `3080c1f7` (追記参照。途中で `34e02d82`) | `2b618595` | メッセージとコメントだけ。`lib/sp_gc.c` +6/-1、`lib/spinel_rt.h` +15 | PR1 の結果がそのまま当てはまる (4,054 / 1 / 0。コメント以外は同一) | 62 / 0 |

基点は PR1 と同じ commit で 4,054 / 1 / 0、62 / 0。1 fail は全ブランチ共通の
`systemcallerror_hierarchy` (環境の問題。PR1 参照)。`pr/U-12` の +1 は新テスト。
`pr/U-12` では加えて `make optcarrot` OK、`gate-props` 全 pass、`rubyspec-gate`
全 pass (671 / 421 / 571 / 192 / 171 / 83 の expected-PASS が全部残る)。

`pr/U-12` の変更ファイル:

| ファイル | 行数 | 内容 |
|---|---|---|
| `src/codegen_util.c` | +36 | 受け手の判定 `str_mut_var_recv()` を新設 |
| `src/codegen_internal.h` | +1 | その宣言 |
| `src/codegen_stmt.c` | +11 / -7 | 文の位置の 4 つの腕。PR1 の `str_mut_recv_assignable()` を「self または `str_mut_var_recv()`」に書き換え |
| `src/codegen_call_recv.c` | +10 / -30 | 値の位置の 10 箇所の「局所変数か ivar か」の並びを `str_mut_var_recv()` に置き換え、使われなくなった変数を削除 |
| `src/codegen_call.c` | +1 / -2 | 値の位置の `clear` |
| `test/str_mutate_gvar_cvar_const.rb` (+ `.expected`) | +119 / +48 | PR1 の `test/str_mutate_gvar_cvar.rb` を改名して広げた |

## T1: `pr/U-12` を広げる

### 直し方

String は `const char *` の値なので、破壊的メソッドは「受け手への代入」に
下ろされる。文の位置 (`codegen_stmt.c`) も値の位置 (`codegen_call_recv.c`、
`clear` だけ `codegen_call.c`) も、代入してよい受け手を「局所変数・ivar
(文の位置では self も)」の並びで判定していて、その並びが約 15 箇所に
写してあった。

判定を 1 つの関数 `str_mut_var_recv(Compiler *c, int recv)` にまとめ、
大域変数・クラス変数・定数を加えた。

- 大域変数 (`gv_g`)、クラス変数 (`cvar_C_v`) は局所変数と同じく C の左辺値
  なので、そのまま真。
- 定数は `comp_const()` で引いて、型が決まっていて、`Class.new` で初期化
  される定数 (`init_guarded`。読みが条件式になり左辺値でない) でなければ真。
  読みは `cst_S` という素の変数になる。
- `K::S` の形 (`ConstantPathNode`) は読みの解決に表が何段もある (ffi 定数、
  組み込みクラス、受け手が実行時の値のときの switch) ので、同じ条件を写す
  代わりに、読みの C を一度出力させて `cst_<識別子>` だけのときに真とした
  (`g_tmp` は元に戻す)。受け手が実行時の値の形 (`klass::CODE`) は switch に
  なるので偽になり、従来の形のまま。

文の位置の判定は「self または `str_mut_var_recv()`」になった。

### 直した形

受け手 4 種 (大域変数、クラス変数、トップレベルの定数、クラス内の定数) ×
破壊メソッド 17 種 (`<< "x"`、`<< "x" << "y"`、`concat`、`upcase!`
`swapcase!` `squeeze!` `reverse!`、`gsub!` `sub!` `tr!` `delete!` `slice!`、
`replace` `prepend` `clear` `delete_prefix!` `delete_suffix!`) × 位置 4 種
(文、`x = (...)`、`p(...)`、メソッドの戻り値) の 272 通りを自動で作って
CRuby と比べた (`$S/gen.rb` `$S/run.sh`)。

| | 基点 | `pr/U-12` 旧 (`e0c71c4b`) | `pr/U-12` 新 (`f44d4b06`) |
|---|---|---|---|
| 272 通りで CRuby と一致 | 16 (`squeeze!` が何も変えない 16 通りだけ) | 48 (大域変数とクラス変数の文の位置 + 上の 16) | **272** |

表の外で手で書いて確かめた形 (`$S/m2/`、すべて新で CRuby と一致):

| 受け手 | 文の位置 | 値の位置 |
|---|---|---|
| 大域変数 | `insert` `setbyte` `bytesplice` `concat` の複数引数、`<< 33` (Integer)、`<< "#{...}"` (式展開)、メソッド内・ブロック内の `$log << s << "\n"` | `slice!(0, 2)` `slice!("c1")` `chomp!` `insert`、`def m = ($g.replace(...))`、`String.new` への `<<` |
| クラス変数 | `insert` | 連鎖、`slice!(0)`、`prepend`、`delete_suffix!` |
| 定数 | `insert` `setbyte` `concat` の複数引数、`<< 33`、式展開、`String.new` の定数、`K::U << "w"` (外から)、`M::S.upcase!` | 連鎖、`slice!` `chomp!` `succ!`、`K::U.sub!`、`P::Q.sub!` |
| 大域変数 (nil と String の両方を持つ) | `<<` `upcase!` | `<<` `sub!` (基点では失われていた。今回の直しで一致) |

凍結の確認 (新で CRuby と一致): `S = "abc".freeze`、`# frozen_string_literal: true`
の下の定数と大域変数、`$g = "gg".freeze`、`@@v = "vv".freeze` に対して、
文の位置と値の位置の `<<` `upcase!` `gsub!` `replace` `clear` と連鎖が
すべて FrozenError になり、値は変わらない。

最小再現:

- U-12 `repro/U-12.rb`: 新で `.expected` と一致。
- U-16 `$g = +"z"; x = ($g << "q"); puts $g`: 基点 `z`、新 `zq` (CRuby `zq`)。
- U-18 `S = +"c"; S << "d"; puts S`: 基点 `c`、新 `cd` (CRuby `cd`)。

### 外した形とその理由

| 形 | 理由 |
|---|---|
| **U-17 別名** (`t = $g; $g << "x"; puts t`) | 指示どおり直さない。テストにも入れていない。新でも `g` のまま (CRuby `gab`)。PR 本文で範囲外と一言だけ書いた |
| `S = "abc"` (`+` 無しのリテラル) への破壊的変更 | spinel は文字列リテラルを凍結するのが仕様 (docs/limitations.md。局所変数でも同じ FrozenError と、コンパイル時の警告が出る)。CRuby 3.2 とは違うが、今回の問題ではない |
| 型が混ざって poly になった受け手の値の位置 (`$g = 1; $g = +"a"; p($g << "c")`) | **局所変数・ivar でも同じく失われる** (下の PR 候補 N-7)。受け手の種類ではなく poly の `sp_poly_shl` の問題なので、この PR の外 |

codegen の設計に踏み込む必要があった形は無かった。

### 回帰テスト

`test/str_mutate_gvar_cvar_const.rb` (+ `.expected`、CRuby 3.2.6 の出力)。
PR1 の `test/str_mutate_gvar_cvar.rb` を改名して、次を足した。

- 大域変数の値の位置: `x = (...)`、`puts(...)`、連鎖、引数なしの bang
  (変化あり・なしの nil)、`sub!` `tr!` `delete_suffix!` `slice!` `concat`
  `insert` `clear` `replace`、メソッドの戻り値としての `<<`。
- クラス変数の値の位置: 連鎖の値、`prepend` `delete!` の値。
- 定数: トップレベル・クラス内・`K::U` の外からの参照、`String.new` の定数。
  文の位置と値の位置。
- 凍結: 凍結された定数と大域変数への `<<` `upcase!` `replace` `gsub!` が
  FrozenError になり、値が変わらないこと。

基点では 1 行目から出力が食い違い (`gab` のはずが `g`)、`$g.clear` の
NoMethodError で止まる。

テストを書いていて、値の位置の `replace` に**リテラルを渡すと受け手が凍結
される**別の不具合を見つけた (局所変数でも起きる。N-6)。テストはそれに
当たらない順序 (`replace` の後で同じ受け手を変えない) にした。

### 生成 C の比較

`test/*.rb` 全部を基点と新しいコンパイラで `-c --no-line-map` し、生成 C を
比べた (PR1 の `cdiff.sh`、`$S/cd_U-12/`)。**変わったのは新テスト
`str_mutate_gvar_cvar_const` の 1 本だけ**。生成に失敗したものは両方とも 0 本。

### PR 本文の下書き

```
Apply String mutators on a global, class variable or constant receiver

A String is a const char * value, so an in-place mutator is lowered to a
reassignment of its receiver: `s << x` -> `s = sp_str_append_grow(s, x)`,
`s.upcase!` -> `s = sp_str_upcase(s)`, and so on. Both the statement arms
(codegen_stmt.c) and the value arms (codegen_call_recv.c, and clear in
codegen_call.c) write back only to a local or an ivar (and, in statement
position, self). A global, a class variable or a constant fell through to
the form that builds the new string and drops it:

  $g = +"g"
  $g << "ab"
  puts $g            # g    (CRuby: gab)
  x = ($g << "q")
  puts $g            # g    (CRuby: gabq)
  $g.clear           # NoMethodError: undefined method 'clear' for String
  S = +"c"
  S << "d"
  puts S             # c    (CRuby: cd)

gv_g, cvar_C_v and cst_S are plain C lvalues holding the string, the same
as a local, so they take the same reassignment. The receiver checks, which
were copied into some fifteen places, now go through one predicate,
str_mut_var_recv. It adds GlobalVariableReadNode, ClassVariableReadNode and
a constant whose read is its cst_ slot; a Class.new-guarded constant, or a
`klass::NAME` path whose owner is a run-time value, reads through an
expression that is not an lvalue and keeps the old form. The statement arms
add self on top of it.

That covers <<, its chains, concat/prepend/insert, the bang methods with and
without arguments, slice!, replace/clear, setbyte and bytesplice, in
statement and value position (assigned, passed as an argument, returned
from a method). A frozen receiver still raises FrozenError. A generated
matrix of 4 receivers x 17 mutators x 4 positions now matches CRuby in all
272 cases (16 before, all of them squeeze! calls that change nothing).

Across the test corpus only the new test's C changes. The test replaces
test/str_mutate_gvar_cvar.rb, which covered the statement forms on globals
and class variables only.

Not covered here: aliasing. `t = $g; $g << "x"` still does not reach t
unless the append happens in place; locals and ivars get that from the
shared-mutable String (TY_STRBUF) promotion, which does not track globals
or constants.

make test: 4055 pass / 1 fail (systemcallerror_hierarchy, a local network
timeout that also fails on master); make bench 62/0; optcarrot OK;
gate-props and rubyspec-gate pass.
```

commit メッセージは上の本文から最後の段落 (試験結果) と「272 通り」と
「test/str_mutate_gvar_cvar.rb を置き換えた」の 2 段落を除いたもの
(`git show f44d4b06` を参照)。

見込み: PR1 では「部分的な修正」だったが、値の位置と定数を含めたので
単独で出せる。範囲外は別名だけで、本文ではそれが解析 (TY_STRBUF) の話で
あることを一文で断っている。

## T2: `pr/P-3` のメッセージの書き直し

最初の amend (`34e02d82`) ではコードを変えず、tree は旧 `2b618595` と同じ
`fd7f0b96` だった (コメントは後で直した。下の追記)。コメント以外は同じなので、 `make test` / `make bench` などの結果は PR1 のものが
そのまま当てはまる。

書き直しに当たって確かめたこと (基点 `01521b1e` と `pr/P-3`):

- 基点の `lib/` に `-DSP_EXC_STACK_MAX=8 -DSP_CATCH_STACK_MAX=8
  -DSP_GC_MARK_STACK_MAX=64` を渡して生成 C (`test/catch_depth_bounded.rb`) と
  `lib/sp_gc.c` を `cc -fsyntax-only` すると、`"SP_GC_MARK_STACK_MAX" redefined`
  (spinel_rt.h:604 と sp_gc.c:119)、`"SP_EXC_STACK_MAX" redefined` (9919)、
  `"SP_CATCH_STACK_MAX" redefined` (10785) の 4 件の警告。`pr/P-3` では 0 件。
- `-D` 無しの `cc -E -P` は、生成 C と `lib/sp_gc.c` の両方で基点と md5 が一致。
- `pr/P-3` のヘッダに `-DSP_CATCH_STACK_MAX=8` を渡し、catch を 12 段入れ子に
  するとその段で `stack level too deep (SystemStackError)` で止まる (6 段は通る)。

### 新しい commit メッセージ (= PR 本文の下書き)

```
Guard the exception, catch and GC mark stack sizes with #ifndef

The runtime's capacity constants mostly let the build choose them:
SP_GC_STACK_MAX (lib/sp_gc.h) and SP_DYN_SYMS_MAX (lib/spinel_rt.h) are
wrapped in #ifndef, so -DSP_GC_STACK_MAX=<n> sizes the root array without
touching the source. Three constants of the same kind are still
unconditional:

  SP_EXC_STACK_MAX      depth of the begin/rescue/ensure handler stack
  SP_CATCH_STACK_MAX    depth of the catch/throw stack
  SP_GC_MARK_STACK_MAX  initial capacity of the collector's mark stack
                        (defined in both lib/sp_gc.c and lib/spinel_rt.h)

Passing -D for any of them only draws a "redefined" warning and the
header's value wins. This wraps the four definitions in #ifndef, the same
shape the other two have, with a comment on what each one sizes.

Nothing passes -D by default, so nothing changes: cc -E -P of lib/sp_gc.c
and of a generated program (test/catch_depth_bounded.rb) is byte-identical
before and after. A lower depth fails safely, since handler pushes are
bounds-checked and raise SystemStackError (sp_stack_too_deep), and the mark
stack still grows past its initial capacity on demand.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
```

PR 本文として出すときは、`Co-Authored-By` の行の代わりに試験結果の 1 段落
(`make test: 4054 pass / 1 fail (systemcallerror_hierarchy, ...); make bench
62/0; optcarrot OK; gate-props and rubyspec-gate pass.`) を足す。

### 追記: コードのコメントも書き直した (`3080c1f7`)

親セッションの追加指示で、PR1 で書いたコメントに残っていた小メモリの話を
中立な説明に直し、amend した (1 コミットのまま。メッセージは上のまま)。

- `lib/sp_gc.c` の `SP_GC_MARK_STACK_MAX`: 「a target with a small fixed heap
  can start it lower」→「A build can set it with -DSP_GC_MARK_STACK_MAX=<n>,
  as it can SP_GC_STACK_MAX; the stack still grows from there on demand.」
- `lib/spinel_rt.h` の `SP_EXC_STACK_MAX`: 「A target with little RAM and a
  shallow C stack can lower it」→「A build can set it with
  -DSP_EXC_STACK_MAX=<n>, as it can SP_GC_STACK_MAX; ... Pushes are
  bounds-checked, so a program that nests deeper raises SystemStackError
  (sp_stack_too_deep).」
- `SP_CATCH_STACK_MAX` のコメントは「SP_EXC_STACK_MAX と同じ」とだけ言って
  いるので変えていない。

確認 (新 commit の `lib/` と基点 `01521b1e` の `lib/`):

- `-D` 無しの `cc -E -P` の md5 が、生成 C (`test/catch_depth_bounded.rb`) で
  `5343a47d…`、`lib/sp_gc.c` で `0373d86a…`。どちらも基点と一致。
- `-DSP_EXC_STACK_MAX=8 -DSP_CATCH_STACK_MAX=8 -DSP_GC_MARK_STACK_MAX=64` での
  `cc -fsyntax-only` は、生成 C と `lib/sp_gc.c` のどちらも redefined の警告 0 件。
- 作業用の worktree は消した。`$W` 本体は `pr/P-3` (`3080c1f7`) を detached で
  checkout した状態。

## PR 候補 (作業中に見つけたもの)

台帳は編集していない。PR1 の N-1〜N-5 の続きで仮 ID `N-6` から。確認はすべて
上流 `01521b1e`。

| 仮 ID | 内容 | 最小再現 | 確認した上流 | 見込み |
|---|---|---|---|---|
| N-6 | 値の位置の `String#replace` に文字列リテラルを渡すと、受け手がリテラルそのもの (凍結された static) を指すようになり、以後の破壊的変更が FrozenError になる。文の位置の `replace` は正しい。受け手が局所変数でも起きる | `s = +"v"; p(s.replace("rep")); p(s.concat("c"))` (CRuby `"rep"` `"repc"`、上流 `"rep"` の後 FrozenError) | `01521b1e` と新 `pr/U-12` | 高 (値の腕で引数を複製すればよい見込み。`codegen_call_recv.c` の `replace` の腕) |
| N-7 | 型が混ざって poly になった String の受け手で、値の位置の `<<` が受け手に残らない (`sp_poly_shl` の結果が書き戻されない)。局所変数・ivar・大域変数のどれでも起きる | `g = 1; p g; g = +"a"; g << "b"; p(g << "c"); p g` (CRuby 最後が `"abc"`、上流 `"ab"`) | `01521b1e` と新 `pr/U-12` | 中 (poly の破壊的メソッドの経路を読んでから) |
| N-8 | 作った worktree では `vendor/prism` `vendor/rbs` が無くビルドできない (`make deps` か写しが要る)。上流の問題ではなく作業手順の注意 | — | — | PR ではない。次の作業の手順に書く |
