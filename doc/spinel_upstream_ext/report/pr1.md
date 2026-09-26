# 報告 PR1: 上流 PR の下準備 (第 1 陣)

> 状態: 完了 | 更新: 2026-09-26 | 6 件すべてブランチを作り、`make test` / `make bench` を通した (U-6 は解析の修正と検査の 2 本)。止めた候補は無い。U-12 は文の位置だけを直し、値の位置と別名は Issue 向きの残りとして書いた。検査だけのブランチ (`pr/U-6-check`) は既存テスト 1 本を止める (そのテスト自体が同じ食い違いを持っていた) ので、`pr/U-6` の後に出す

## 前提

| 項目 | 値 |
|---|---|
| clone | `/home/kishima/fmrb/wt/spinel-pr/` (`https://github.com/matz/spinel.git`)。以下 `$W` |
| 基点 | `01521b1e` (全ブランチ共通) |
| 作業場所 | ブランチごとに `$W/.worktrees/<ID>/` の worktree で編集し、`make test` / `make bench` は `$W` 本体を detached で切り替えて順に回した (並べると 10 秒の timeout で偽の失敗が出るため)。`.worktrees/` は `.git/info/exclude` に入れてある |
| 基点のコンパイラ | `$W/.worktrees/base/` (`01521b1e` をビルドしたもの。修正前との比較用) |
| フォーク | `fmruby-core/vendor/spinel` `622750c`。読むだけ (fetch は不要だった) |
| ツールチェーン | `cc (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0`、`ruby 3.2.6`、WSL2 x86_64、24 コア |
| 作業ファイル | `$S = /tmp/claude-1000/-home-kishima-fmrb-family-mruby/a0ea00a0-6754-4693-ae2d-f05782a9784d/scratchpad/pr1/` (`gate.sh` `cdiff.sh` `queue.sh` `extra.sh` と各ログ) |

push・PR・Issue はしていない。fmruby-core / graphics-audio の作業ツリー、
`~/dev/spinel`、sim、実機には触れていない。

## 上流の作法 (README の Contributing 節) と今回の対応

| 作法 | 対応 |
|---|---|
| 焦点を絞った小さい PR | 1 ブランチ 1 コミット、1 問題 |
| `make gate` を通す (`make test`、`make bench`、`make optcarrot`。実体は加えて `gate-props` と `rubyspec-gate`) | 指示の 2 つに加え、残りの 3 つも全ブランチで回した (下の表) |
| `test/` に回帰テスト (`<name>.rb` + `.rb.expected`、CRuby の出力と比べる) | P-3 以外は追加。P-9 は既存テストの 32bit 除外を外した |
| Issue 参照は `Closes #N` / `Fixes #N` / `Refs #N` の trailer | **対応する Issue は無い** (作っていない)。trailer は付けていない。出すときに Issue を先に立てるなら足す |
| AI が関わったら `Co-Authored-By:` trailer | 付けた (上流の直近のコミットと同じ `Claude Opus 5.5 (1M context)` の形) |
| コミットメッセージの形 | 上流の直近の履歴に合わせた: 接頭辞 (`codegen:` 等) の無い英文の件名、本文に症状・原因・直し方・影響範囲、生成 C の抜粋 |
| 自己ホストの一致 | **今の上流には無い**。README の History 節によれば master は C で書き直した実装で、自己ホストの Ruby 実装は `self-host` ブランチに退いた。gate も自己ホストを検査しない。よって指示書の「`make` の自己ホスト一致」は該当なしとし、代わりに下の「生成 C の差分」で既定の出力が変わらないことを確かめた |

### 生成 C の差分 (既定の出力を変えないことの確認)

`$S/cdiff.sh` が `test/*.rb` 全部 (4,056 本) を基点のコンパイラとブランチの
コンパイラで `-c --no-line-map` し、生成 C を比べる。上流のコミットにも
「across the test corpus only the new test's C changes」という書き方があるので、
PR 本文にもこの数字を書ける。

| ブランチ | 生成 C が変わったテスト |
|---|---|
| `pr/U-11` | 新しいテストだけ (1 本) |
| `pr/U-6` | 新しいテストと `super_attr_mid_redeclare` (2 本)。後者は基点で既に親子の食い違いを持っていた (U-6 の節) |
| `pr/U-6-check` | 生成 C の中身は変わらない。`super_attr_mid_redeclare` だけが生成を拒否される |
| `pr/U-6` + 検査 (`check/U-6+check`) | `pr/U-6` と同じ 2 本。検査で止まるものは 0 本 |
| `pr/U-12` | 新しいテストだけ (1 本) |
| `pr/U-10` | 3 本: 新しいテスト、`format_splat_array_variable`、`io_instance_read_surface`。後の 2 本は `IO#printf` の書式にルートが 1 つ増えただけ (`SP_GC_ROOT_STR(_tN)` と GC フレームの枠 1 つ)。`Kernel#format` の側は、書式がルートの文を積まない限り 1 バイトも変わらない |
| `pr/P-9` | 3 本: 可変長引数を使う `ffi_variadic` `ffi_io_buffer_arg` `ffi_header_declared_extern`。キャストの綴りが `(long long)(` から `(sp_int)(` に変わるだけで、LP64 では同じ 8 バイトの渡し方 |
| `pr/P-3` | 生成 C は変わらない (ランタイムのヘッダだけ)。`cc -E -P` の結果が基点と一致 |

## 結果の一覧

`make test` の基点は **4,054 pass / 1 fail / 0 error**、`make bench` は
**62 pass / 0 fail**。基点の 1 fail は `systemcallerror_hierarchy` で、
`TCPSocket.new("127.0.0.1", 1)` がこの環境では拒否されずに固まり 10 秒の
timeout に当たる (単体で 30 秒待っても返らない)。環境の問題で、全ブランチで
同じく落ちる。

| ID | ブランチ | commit | 変更 | `make test` | `make bench` | optcarrot / gate-props / rubyspec-gate |
|---|---|---|---|---|---|---|
| P-3 | `pr/P-3` | `2b618595` | `lib/sp_gc.c` +6/-1、`lib/spinel_rt.h` +14 | 4,054 / 1 / 0 (基点と同じ) | 62 / 0 | OK / pass / pass |
| U-10 | `pr/U-10` | `45adc6c9` | `src/codegen_call.c` +12/-7、テスト 2 ファイル | 4,055 / 1 / 0 (+1 は新テスト) | 62 / 0 | OK / pass / pass |
| U-11 | `pr/U-11` | `2b14a9c3` | `src/codegen_expr.c` 2 行、`src/codegen_stmt.c` 2 行、テスト 2 ファイル | 4,055 / 1 / 0 | 62 / 0 | OK / pass / pass |
| U-6 | `pr/U-6` | `b49540e4` | `src/analyze.c` +46/-28 (関数への切り出し + 呼び出し 1 箇所)、テスト 2 ファイル | 4,055 / 1 / 0 | 62 / 0 | OK / pass / pass |
| U-6 検査 | `pr/U-6-check` | `4af0a94c` | `src/codegen.c` +54 | **4,053 / 1 / 1** (ERR は `super_attr_mid_redeclare`。検査が正しく止めたもの) | 62 / 0 | OK / pass / pass |
| (確認用) | `check/U-6+check` | `7dd035a4` | `pr/U-6` の上に検査を載せたもの | 4,055 / 1 / 0 | 62 / 0 | OK / pass / pass |
| U-12 | `pr/U-12` | `e0c71c4b` | `src/codegen_stmt.c` +17/-7、テスト 2 ファイル | 4,055 / 1 / 0 | 62 / 0 | OK / pass / pass |
| P-9 | `pr/P-9` | `848d6389` | `src/codegen_call.c` +8/-3、`test/ffi_variadic.rb` 3 行 | 4,054 / 1 / 0 (テスト追加なし) | 62 / 0 | OK / pass / pass |

32bit (P-9 の節で詳述): `make test-corpus CC='cc -m32'` を i386 の libcrypt の
代わりに空の `crypt` を入れた静的ライブラリで回した。基点 3,993 pass / 4 fail、
`pr/P-9` 3,994 pass / 4 fail (+1 が `ffi_variadic`)。4 fail は全ブランチ共通で、
3 本が `String#crypt` (空の代用品のせい)、1 本が上の環境の件。

## P-3: 例外・catch・GC mark のスタック段数を `#ifndef` に

- ブランチ `pr/P-3`、commit `2b618595`。`lib/sp_gc.c` (+6/-1)、`lib/spinel_rt.h` (+14)。
- 回帰テスト: 無し。既定の出力を変えないのが要件で、`-D` を付けたときの
  動作はコーパスの形 (CRuby と出力を比べる) に乗らないため。代わりに次を確かめた。
  - **`-D` 無しで 1 バイトも変わらない**: 基点と修正後の `lib/` で、`lib/sp_gc.c`
    と生成 C (`test/catch_depth_bounded.rb`) の `cc -E -P` の md5 が一致。
  - **`-D` が効く**: `-DSP_EXC_STACK_MAX=8 -DSP_CATCH_STACK_MAX=8
    -DSP_GC_MARK_STACK_MAX=64` で、入れ子の rescue・catch/throw・2 万要素の連結
    リストを GC.start で回す模型 (`$S/p3/k.rb`) が CRuby と一致。生成 TU の
    `.bss` は 100,928 → 74,016 バイト (段数 64 → 8)。
- フォークとの違い:
  - 上流は `sp_runtime.h` が `spinel_rt.h` に改名され、定義は 4 箇所
    (`sp_gc.c` 1、`spinel_rt.h` 3)。書き直した。
  - **上流の mark stack は倍々に伸びる** (フォークの頃は溢れたら再帰だった)。
    `SP_GC_MARK_STACK_MAX` は今は「最初に確保する容量」で、名前は MAX のまま。
    コメントをそれに合わせた。最初の 1 回の確保 (LP64 で 512KB) が組み込みで
    効くという動機は変わらない。
  - `spinel_rt.h` の `SP_GC_MARK_STACK_MAX` は生成 TU からは参照されていない
    (残骸)。2 か所の値を揃えるため同じく `#ifndef` にした。
  - 例外・catch の push は上流で境界検査が入った (`sp_stack_too_deep`)。
    段数を下げても溢れれば SystemStackError になり、メモリは壊れない。フォーク
    の本文にあった「push が検査されない」の注意は不要になった。
- 見込み: **出しやすい**。上流に `SP_DYN_SYMS_MAX` という同じ形の前例があり、
  既定の出力は変わらない。懸念は「組み込み向けの口を上流が持ちたいか」だけ。

### PR 本文の下書き

```
Let a port size the exception, catch and GC mark stacks

SP_EXC_STACK_MAX, SP_CATCH_STACK_MAX and SP_GC_MARK_STACK_MAX are
unconditional #defines. The first two size static arrays of jmp_bufs (and
their parallel message/class/rootmark arrays) in every generated program --
about 30 KB of .bss at the default depth of 64 on x86-64. The third is the
collector's initial mark stack, one 512 KB malloc on the first mark on LP64.
On a microcontroller with a few hundred KB of RAM and a C stack far too
shallow to nest 64 handlers, that is most of the budget, and the only way to
shrink it is to patch the header.

This guards the three with #ifndef, the shape SP_DYN_SYMS_MAX already has.
Handler pushes are bounds-checked (sp_stack_too_deep), so a lower depth
fails with SystemStackError rather than corrupting memory, and the mark stack
still grows past its initial capacity on demand.

No build here passes -D, so nothing changes by default: `cc -E` of
lib/sp_gc.c and of a generated program is byte-identical before and after.
With -DSP_EXC_STACK_MAX=8 -DSP_CATCH_STACK_MAX=8 the generated TU's .bss
goes from 100,928 to 74,016 bytes, and nested rescue / catch / a GC over a
20k-node list still match CRuby.

make test: 4054 pass / 1 fail (systemcallerror_hierarchy, a local network
timeout that also fails on master); make bench 62/0; optcarrot OK;
gate-props and rubyspec-gate pass.
```

## U-10: sprintf の書式が GC ルートの要る呼び出しのとき C が壊れる

- ブランチ `pr/U-10`、commit `45adc6c9`。`src/codegen_call.c` +12/-7、
  `test/format_string_rooting.rb` (+ `.expected`)。
- 原因: `Kernel#format` / `#sprintf` は `const char *_tN = <書式>; SP_GC_ROOT_STR(_tN);`
  を g_pre に書くが、**宣言の左辺を書いてから書式を出力していた**。書式が
  「ルートの文を g_pre に積む呼び出し」(poly 引数に Symbol を箱詰めする
  `t(:key)` など) だと、積まれた文が書きかけの初期化子の中に入る。

  ```c
  const char *_t4 =     _gcf.v[0] = sp_box_sym(((sp_sym)0));
  sp_poly_arg_str_chk(sp_I18n_s_t(_gcf.v[0])); SP_GC_ROOT_STR(_t4);
  ```

  引数の側は同じ問題を #1498 / #1508 で「先に局所バッファへ出力する」形に
  直してあり、書式だけが残っていた。
- 直し方: 書式を先に局所バッファへ出力し、それから宣言の行を丸ごと書く。
- **同じ形の `IO#printf` も直した** (同じ PR に入れた)。こちらは書式を
  `emit_expr` で出していたので、poly の書式がそのまま `const char *` に
  入る型の誤りもあった。`emit_str_expr` を通し、`Kernel#format` と同じく
  ルートも張った (後ろの引数は全部箱詰めで確保するため)。
- フォークとの違い: フォーク `8a298cb` の前半 (書式を `emit_str_expr` に通す)
  は上流で既に入っていた。残っていたのは `286de9b` の順序だけ。`8a298cb` の
  `File.open` の部分は、上流では poly のパスとモードが通る (確認済み) ので不要。
- 回帰テスト `format_string_rooting`: `sprintf` / `format` / `$stdout.printf`
  の書式に `Msgs.t(:done)` を渡す。基点では生成 C のコンパイルで 3 件の
  error、修正後は CRuby と一致。
- 見込み: **高**。#1508 の兄弟で、上流が自分で直した形そのまま。
  fmrb の system_desktop が上流でコンパイルできない原因がこれ。

### PR 本文の下書き

```
Emit a format string before opening its temp's declaration

Kernel#format / #sprintf lower to

  const char *_tN = <fmt>; SP_GC_ROOT_STR(_tN);

in g_pre, and the line was opened before <fmt> was emitted. When the format
is itself a call that roots its operands -- `sprintf(t(:key), n)` where t
takes a poly parameter, so the Symbol is boxed into a GC frame slot first --
the rooting statement lands inside the half-written initializer:

  const char *_t4 =     _gcf.v[0] = sp_box_sym(((sp_sym)0));
  sp_poly_arg_str_chk(sp_I18n_s_t(_gcf.v[0])); SP_GC_ROOT_STR(_t4);

and gcc rejects it ("incompatible types when initializing type 'const char *'
using type 'sp_RbVal'"). The arguments already avoid this by emitting into a
local buffer first (#1498 / #1508); this does the same for the format.

IO#printf had the same shape and also emitted the format with emit_expr, so a
poly format reached the const char * slot raw. It now goes through
emit_str_expr and is rooted, as Kernel#format's is.

test/format_string_rooting.rb covers sprintf, format and $stdout.printf.
make test: 4055 pass / 1 fail (systemcallerror_hierarchy, fails on master
too); make bench 62/0; optcarrot OK; gate-props and rubyspec-gate pass.
```

## U-11: 大域変数・クラス変数への `+=` の右辺が poly で C が壊れる

- ブランチ `pr/U-11`、commit `2b14a9c3`。`src/codegen_expr.c` と
  `src/codegen_stmt.c` の各 2 行 (`emit_expr` → `emit_str_expr`)、
  `test/str_opassign_poly_gvar_cvar.rb` (+ `.expected`)。
- 原因: String の大域変数・クラス変数への `+=` は
  `gv_g = sp_str_concat(gv_g, <右辺>)` (値の位置では `sp_str_plus`) になるが、
  右辺を `emit_expr` で出していたので、poly の右辺 (`h[:a]`) が `sp_RbVal` の
  まま `const char *` の引数に渡る。局所変数の腕は #2875 で既に変換しており、
  ivar は別の腕を通るので、大域変数とクラス変数の 4 箇所だけが残っていた。
- 直し方: 4 箇所の右辺を `emit_str_expr` (String 引数の口) に通す。型付きの
  String はそのまま、poly は `sp_poly_arg_str_chk` になり、nil なら CRuby と
  同じ `TypeError: no implicit conversion of nil into String`。
- **生成 C が変わるのはコーパス 4,056 本のうち新テストだけ**。
- フォークとの違い: フォーク `9474d92` は定数の腕 2 箇所も変えていたが、
  上流では定数は通る (最小再現で確認) ので触っていない。フォークが使った
  `sp_poly_to_s` ではなく `emit_str_expr` にした (nil を黙って "" にせず、
  CRuby と同じ例外にするため)。
- 回帰テスト: 文と値の位置、クラスメソッド内のクラス変数、nil のときの
  TypeError。基点では生成 C のコンパイルで 5 件の error。
- 見込み: **最も出しやすい**。4 行で、既存の局所変数の腕と同じ扱いに揃えるだけ。

### PR 本文の下書き

```
Coerce a poly right side of += on a String global or class variable

`$g += h[:a]` and `@@v += h[:a]`, with $g / @@v a String and h[:a] a poly
(a Symbol-keyed hash value), emitted

  gv_g = sp_str_concat(gv_g, sp_SymPolyHash_get(lv_h, ...));

passing the sp_RbVal straight into the const char * parameter; the C did not
compile. The value-position forms (`x = ($g += v)`, sp_str_plus for a class
variable) had the same hole. The local-variable arm already coerces a poly
right side (#2875); globals and class variables did not.

The four arms now emit the right side through emit_str_expr, the String
argument slot: a typed String passes through unchanged, and a poly one
converts with sp_poly_arg_str_chk, which raises CRuby's TypeError for nil.
Across the test corpus only the new test's C changes.

make test: 4055 pass / 1 fail (systemcallerror_hierarchy, fails on master
too); make bench 62/0; optcarrot OK; gate-props and rubyspec-gate pass.
```

## U-6: ivar の型が親と子で食い違い、構造体の先頭一致が崩れる

### `pr/U-6` (解析の修正)

- commit `b49540e4`。`src/analyze.c` +46/-28、
  `test/inherited_ivar_late_widen_layout.rb` (+ `.expected`)。
- 原因: 継承したメソッドは親の型へのキャスト (`sp_Base_m((sp_Base *)self)`)
  で呼ばれるので、親の構造体は子の構造体の先頭と一致していなければならない。
  型を揃えるのは「子 → 親の持ち上げ」の不動点と `infer_inherited_ivars`
  (親 → 子) だが、持ち上げは **poly への落とし込み (未解決の局所・引数を poly
  にする段) より前に 1 回だけ**走る。その後の最終ループは、落とし込みで ivar の
  型が広がりうるからこそ `infer_ivar_types` と `infer_inherited_ivars` をやり
  直すのに、広がった子の型を親へ戻す経路が無い。親は狭い型のまま残る。
  最小再現では `@items` が親で `sp_PolyArray *`、子で `sp_RbVal` になり、
  幅の違いで後ろの `@w` `@h` の位置がずれ、親の `initialize` が親の位置に
  書いた値を子の `area` が自分の位置 (nil のまま) から読んで TypeError。
  広げる原因は一度も作られないクラスの 1 行 (`app.attach(self)`)。
- 直し方: 持ち上げのループを `propagate_ivars_up()` に切り出し、最終ループでも
  呼ぶ。`ty_unify` は広げる一方なのでループは単調で、既存の上限 (8 回) で終わる。
  上流のコード (`analyze.c` 16911 行付近) はフォークの基点と同じ形で残って
  いたので、フォーク `ca0709c` と同じ直し方がそのまま当てはまった
  (ただし上流の今の関数の並びに合わせて書き直した)。
- **生成 C が変わるのはコーパスで 2 本**: 新テストと `super_attr_mid_redeclare`。
  後者は基点で `Grand` の `@x` が `sp_int`、`Parent` `Mid` `Leaf` が `sp_RbVal`
  という同じ食い違いを持っていた (テストの出力には現れていなかっただけ)。
  修正後は 4 クラスとも `sp_RbVal`。PR の説得材料になる。
- fmrb の editor (p0 の combined Ruby、8,764 行) も、検査を載せた確認用の
  コンパイラ (`check/U-6+check`) で生成が通った (基点 + 検査だけだと止まる)。
- 見込み: **高**。黙って値が壊れる不具合で、再現は 27 行。変更は関数への
  切り出しと呼び出し 1 行で、差分の大半は移動。

### `pr/U-6-check` (崩れたら止める検査)

- commit `4af0a94c`。`src/codegen.c` +54。
- 内容: 構造体を出力する直前に、各クラスの祖先について「祖先の ivar が子の
  ivar の先頭と、名前・順序・**C の型の綴り** (`emit_class_struct` と同じ
  `emit_ctype` で作る) まで一致する」ことを確かめ、崩れていたら両クラス名・
  ivar・両方の C 型を出して止める。native クラスと組み込みの再オープンは除外、
  `sp_Exception` の見出しを持つかどうかが違う祖先は比べない。
  ```
  spinel: class layout: @items is `sp_RbVal` in Child but `sp_PolyArray *` in its ancestor Base; methods inherited from Base would reach Child's fields through a different layout
  ```
- フォーク `622750c` との違い: フォークは TyKind を正規化して比べていたが、
  C の型の綴りで比べるようにした。TyKind が違っても同じ C 型になる組
  (幅も意味も同じ) で誤って止めないため。
- **単独だと既存テスト 1 本 (`super_attr_mid_redeclare`) を止める**
  (`make test` 4,053 / 1 / **1 error**)。これは検査の誤りではなく、上の
  とおりそのテストが実際に食い違いを持っているから。`pr/U-6` の上に載せた
  確認用ブランチ `check/U-6+check` (`7dd035a4`) では 4,055 / 1 / 0、コーパス
  で止まるものは 0 本。
- 回帰テスト: 無し。`pr/U-6` の後では食い違いを作る Ruby が無くなるので、
  「止まること」を確かめるテストが書けない (上流の `reject-test` の形にも
  できない)。本文に「U-6 の修正を外すと editor・最小再現で止まる」ことを
  書くのが限界。
- **検査だけでも価値があるか**: ある、ただし出す順番が決まる。
  - この不具合は「生成も C のコンパイルも通り、後で別のメソッドが TypeError
    を出す」形で現れ、原因にたどり着くのに gdb が要った (フォークの経緯)。
    検査があれば生成時に原因 (クラス・ivar・型) が名指しされる。解析の穴は
    U-6 の 1 本とは限らないので、今後の同種の穴を安く見つける保険になる。
  - 既に 1 本のテストが食い違いを持っていたことを検査が見つけた。これ自体が
    「検査だけでも意味がある」証拠になる。
  - ただし単独で先に出すと上流の `make test` を 1 本落とすので、**`pr/U-6` を
    先に出し、取り込まれてから検査を出す** (または 2 コミットの 1 PR にする)。
- 見込み: 中〜高。「内部の不変条件を検査して止める」変更を上流が好むかは
  未知。コスト (クラス数 × 祖先の深さ × ivar 数の文字列比較) は小さい。

### PR 本文の下書き (`pr/U-6`)

```
Carry a late-widened subclass ivar type back to the base

An inherited method is emitted once and called through a cast to the class
that defines it, `sp_Base_m((sp_Base *)self)`, so the base struct has to stay
a common initial sequence of every subclass struct. The up-propagation
fixpoint (child -> parent) runs once, before the poly fallback of unresolved
locals and params; the final loop after that fallback re-runs
infer_ivar_types / infer_inherited_ivars because the fallback can widen what
feeds an ivar, so it can widen a subclass's copy of an inherited ivar -- and
nothing carries that back up.

  class Base
    def initialize; @items = []; @w = 640; @h = 480; end
    def attach(x); @items << x; end
  end
  class Ui                        # never instantiated
    def initialize(app); app.attach(self); end
  end
  class Child < Base
    def area; @w * @h; end
  end
  Child.new.area   # nil can't be coerced into Integer (TypeError); CRuby: 307200

@items settles as sp_PolyArray * in Base and sp_RbVal in Child; the wider
field moves @w and @h, Base#initialize writes them at Base's offsets, and
Child#area reads its own. Nothing fails where the layouts diverge.

This factors the up-propagation into propagate_ivars_up() and also runs it in
the final loop. ty_unify only widens, so the loop stays monotonic and ends on
its existing bound. Across the test corpus the C changes for the new test and
for super_attr_mid_redeclare, whose Grand held @x as sp_int while Parent, Mid
and Leaf held sp_RbVal -- the same mismatch, which that test's output did not
expose.

make test: 4055 pass / 1 fail (systemcallerror_hierarchy, fails on master
too); make bench 62/0; optcarrot OK; gate-props and rubyspec-gate pass.
```

### PR 本文の下書き (`pr/U-6-check`、U-6 の後)

```
Refuse to emit a class struct that is not a prefix of its subclass's

Inherited methods are called through a cast to the defining class, which is
sound only while each class struct is a common initial sequence of its
subclasses' structs. inherit_members and the ivar fixpoints keep that, and
nothing checks it; when an inference path breaks it (#<U-6 PR>), the compiler
emits C that compiles and silently writes through the wrong offsets, and the
failure shows up later as a wrong value or a TypeError in an unrelated method.

This checks it where the structs are emitted: each ancestor's ivars must be a
prefix of the class's, same names, same order, same C type as
emit_class_struct spells it. Native classes and builtin reopens are skipped,
as is an ancestor on the other side of the sp_Exception header. On a mismatch
the compiler stops and names both classes, the ivar and both C types:

  spinel: class layout: @items is `sp_RbVal` in Child but `sp_PolyArray *`
  in its ancestor Base; methods inherited from Base would reach Child's
  fields through a different layout

With #<U-6 PR> in, no corpus program trips it. Without it, it stops
super_attr_mid_redeclare, whose Grand declared @x as sp_int under subclasses
declaring sp_RbVal.
```

## U-12: 大域変数に入れた String の破壊的変更が失われる

- ブランチ `pr/U-12`、commit `e0c71c4b`。`src/codegen_stmt.c` +17/-7、
  `test/str_mutate_gvar_cvar.rb` (+ `.expected`)。
- 原因: String は `const char *` の値なので、文の位置の破壊的メソッドは
  「受け手への代入」に下ろされる (`s << x` → `s = sp_str_append_grow(s, x)`、
  `s.upcase!` → `s = sp_str_upcase(s)`)。この下ろし方は `codegen_stmt.c` の
  4 つの腕 (`<<` とその連鎖、引数無しの bang、`gsub!` `sub!` `tr!` `delete!`
  `slice!`、`replace` `prepend` `clear` `delete_prefix!` `delete_suffix!`) にあり、
  どれも受け手が局所変数・ivar・self のときだけ効く。大域変数とクラス変数は
  値の形に落ち、新しい文字列を作って捨てる。`$g.clear` は基点では実行時に
  `NoMethodError` になる。
- 直し方: 4 つの腕の受け手の判定を 1 つの述語 `str_mut_recv_assignable()` に
  まとめ、`GlobalVariableReadNode` と `ClassVariableReadNode` を加えた。
  `gv_g` `cvar_C_v` は局所変数と同じく文字列を持つ C の左辺値なので、同じ
  代入がそのまま使える。**生成 C が変わるのはコーパスで新テストだけ**。
- フォークにも修正は無かった。上流で新規に書いた。
- **直していない残り (Issue 向き)**:
  1. **値の位置**: `x = ($g << "q")` は x が正しくても `$g` が変わらない。
     値の位置の下ろし方は `codegen_call_recv.c` にあり、受け手の判定は
     局所変数・ivar だけの並びが約 10 箇所 (`lvw` `sb_asgn` など) に散って
     いる。機械的に足せるが、文の位置とは別の述語 (self を含まない) になり、
     差分が 2 ファイル・10 箇所超に広がるので分けた。
  2. **別名**: `t = $g; $g << "x"; puts t` は、追記がその場で済む場合を除いて
     t に反映されない。局所変数と ivar は「共有される可変 String」
     (TY_STRBUF、`analyze.c` 11199 行付近から約 1,200 行) への昇格でこれを
     正しく扱うが、この解析は大域変数を追わない。直すには解析の設計に
     踏み込むので、指示書に従い手を付けていない。
  3. **定数**: `S = +"c"; S << "d"` も失われる。定数の受け手は、リテラルが
     そのまま埋め込まれる場合に代入先にならない恐れがあるので加えなかった。
  - いずれも基点で既に壊れていて、今回の変更で悪くなったものは無い。
- 回帰テスト: `<<`、連鎖、bang (引数あり・なし)、`replace` `prepend` `clear`、
  本体が追記だけのメソッド、`String.new` への追記、クラスメソッドと
  インスタンスメソッド内のクラス変数。基点では出力が CRuby と食い違い、
  途中で NoMethodError。
- 見込み: **中**。文の位置は小さく自然な直しだが、値の位置と別名が残るので、
  「部分的な修正」として出すか、Issue (最小再現 `repro/U-12.rb` + 上の残り
  3 点) と合わせて出すのがよい。上流の保守者が「値の位置も同じ PR で」と
  言う可能性は高い。

### PR 本文の下書き

```
Apply String mutators on a global or class variable receiver

A String is a const char * value, so an in-place mutator in statement
position is lowered to a reassignment of its receiver: `s << x` ->
`s = sp_str_append_grow(s, x)`, `s.upcase!` -> `s = sp_str_upcase(s)`, and
so on. The four statement arms accept a local, an ivar or self as the
receiver; a global or class variable fell through to the value form, which
builds the new string and drops it:

  $g = +"g"
  $g << "ab"
  puts $g       # g   (CRuby: gab)
  $g.upcase!
  puts $g       # g   (CRuby: G)
  $g.clear      # NoMethodError: undefined method 'clear' for String

gv_g and cvar_C_v are plain C lvalues holding the string, as a local is, so
they take the same reassignment. The four receiver checks now share one
predicate, which adds GlobalVariableReadNode and ClassVariableReadNode.
Across the test corpus only the new test's C changes.

Not covered: the value-position forms (`x = ($g << "q")` still leaves $g
alone), constants (`S << "d"`), and aliasing (`t = $g; $g << "x"` does not
reach t unless the append happens in place) -- locals and ivars get that from
the shared-mutable String promotion, which does not track globals.

make test: 4055 pass / 1 fail (systemcallerror_hierarchy, fails on master
too); make bench 62/0; optcarrot OK; gate-props and rubyspec-gate pass.
```

## P-9: FFI `:varargs` が 32bit でずれる

- ブランチ `pr/P-9`、commit `848d6389`。`src/codegen_call.c` +8/-3、
  `test/ffi_variadic.rb` (先頭の `# spinel: int64` を外し、昇格の説明を更新)。
- 原因: 可変長引数の Integer (と bool) を `(long long)` にキャストしていた。
  LP64 では 8 バイトの枠に収まるので無害だが、ILP32 では `%d` が 4 バイト
  読み、次の変換が long long の上半分を読む。以降の引数が 4 バイトずつずれ、
  `C.printf("%d-%d-%s\n", 1, 22, "hi")` は 22 の下半分をポインタとして読んで
  SIGSEGV。
- 直し方: `(sp_int)` にキャストする。`sp_int` は `intptr_t` で、対象の
  Integer の幅そのもの。LP64 では 8 バイトで long long と同じ渡し方、ILP32
  では 4 バイトの int で `%d` と合う。フォーク `53941f6` は `mrb_int` を
  使っていたが、これはフォーク固有の名前なので上流の `sp_int` にした。
- 64bit で生成 C の文字列は変わる (`(long long)(` → `(sp_int)(`)。コーパスで
  変わったのは可変長引数を使うテスト 3 本 (`ffi_variadic` `ffi_io_buffer_arg`
  `ffi_header_declared_extern`) だけで、LP64 では呼び出しの渡し方は同じ。
- 32bit の確認:
  - 上流の `make test-corpus CC='cc -m32'` は、i386 の libcrypt が無いので
    そのままでは `bin/spinel` のリンクで落ちる (p0 と同じ)。**代わりに、
    空の `crypt()` だけを持つ 32bit の静的ライブラリ `$S/stub32/libcrypt.a` を
    作り、`LIBRARY_PATH` で見せて丸ごと 32bit でビルドした** (sudo 不要)。
    上流の CI の `linux32` レーンと同じ `make test-corpus CC='cc -m32' OPT=-O1`
    がこれで回る。
  - 基点 (`.worktrees/m32base`): 3,993 pass / 4 fail。`ffi_variadic` は
    `# spinel: int64` で除外されていて走らない。
  - `pr/P-9` (`.worktrees/m32p9`): **3,994 pass / 4 fail**。+1 が
    `ffi_variadic` で、32bit でも PASS。
  - 4 fail は両方で同じ: `string_crypt` `poly_string_surface`
    `string_nil_float_batch` (どれも `String#crypt` を呼ぶ。空の代用品のせい)
    と `systemcallerror_hierarchy` (環境)。
  - 最小再現 `repro/P-9.sh` も、基点で SIGSEGV (exit 139)、修正後
    `1-22-hi` (exit 0)。
- **`# spinel: int64` は外せる**。外したうえで 32bit のコーパスで通った。
  もう 1 本の varargs を使う `ffi_io_buffer_arg` にも同じ印がある。印を外した
  写しを 32bit で走らせると、`snprintf` の行は通るが、2^31 を超える和
  (`17112760326`) が `-67108858` になる行で食い違う。可変長引数とは別の理由で
  64bit 前提なので、印は残した。
- 見込み: **高**。上流が 32bit の CI レーンを持つ今、「32bit で落ちるので
  除外していたテストが通るようになる」は動機として分かりやすい。

### PR 本文の下書き

```
Pass an FFI Integer vararg at the target's Integer width

A trailing :varargs spec passes each extra argument by its inferred type, and
an Integer (or bool) was cast to long long. On LP64 that is harmless. On an
ILP32 target a `%d` reads 4 bytes and the next conversion reads the high half
of the long long instead of the following argument, so every later vararg is
off by 4 bytes:

  module C
    ffi_func :printf, [:str, :varargs], :int
  end
  C.printf("%d-%d-%s\n", 1, 22, "hi")   # SIGSEGV under gcc -m32

This casts to sp_int instead, the target's Integer width (intptr_t): 8 bytes
on LP64, as long long was, so the call passes the same bits there; a 4-byte
int on ILP32, which is what `%d` reads.

test/ffi_variadic.rb was kept off the 32-bit lane with `# spinel: int64`;
the marker is gone and it passes under `make test-corpus CC='cc -m32'`
(3994 pass vs 3993 on master, the same 4 unrelated failures on both).
make test: 4054 pass / 1 fail (systemcallerror_hierarchy, fails on master
too); make bench 62/0; optcarrot OK; gate-props and rubyspec-gate pass.
```

## 出す順番の提案

小さく、既定の出力を変えず、上流の前例に沿うものから。

1. **U-11** (4 行。局所変数の腕 #2875 と揃えるだけ。コーパスで変わるのは新テストだけ)
2. **P-9** (32bit の CI がある今は動機が明快。除外していたテストが 1 本戻る)
3. **U-10** (#1508 の兄弟。fmrb の system_desktop を上流で通す前提)
4. **P-3** (既定の出力は完全に同じ。ただ「組み込み向けの口」の議論になりうるので、
   上の 3 本で信頼を得てから)
5. **U-6** (黙って値が壊れる。説得材料が揃っている: 27 行の再現、既存テストの潜在不具合)
6. **U-6 検査** (U-6 の取り込み後。単独で先に出すと既存テストを 1 本落とす)
7. **U-12** (部分的な修正なので、Issue を立てて残り 3 点を書いてから、
   または保守者の意向を聞いてから)

Issue 参照の trailer が作法にあるので、U-12 と U-6 は Issue を先に立てると
通りやすい (Issue は作っていない)。

## PR 候補 (作業中に見つけたもの)

台帳は編集していない。新規は仮 ID `N-x`。確認はすべて上流 `01521b1e`。

| 仮 ID | 内容 | 最小再現 | 確認した上流 | 見込み |
|---|---|---|---|---|
| N-1 | 値の位置の String 破壊的メソッドで、受け手が大域変数・クラス変数だと変更が失われる (`x = ($g << "q")` で `$g` が変わらない)。`codegen_call_recv.c` の受け手判定が局所変数・ivar だけ (約 10 箇所) | `$g = +"z"; x = ($g << "q"); puts $g` (CRuby `zq`、上流 `z`) | `01521b1e` と `pr/U-12` | 中 (U-12 の続き。機械的だが差分は広い) |
| N-2 | 大域変数の String の別名が破壊的変更を見ない (`t = $g; $g << "x"; puts t`)。局所変数・ivar は TY_STRBUF 昇格で正しい | `$g = +"g"; t = $g; $g << "ab"; puts t` (CRuby `gab`、上流 `g`) | `01521b1e` と `pr/U-12` | 低 (解析の設計。Issue 向き) |
| N-3 | 定数に入れた String の破壊的変更が失われる | `S = +"c"; S << "d"; puts S` (CRuby `cd`、上流 `c`) | `01521b1e` と `pr/U-12` | 中 (リテラル定数の扱いを確かめてから) |
| N-4 | `test/super_attr_mid_redeclare.rb` の生成 C で、親 `Grand` の `@x` が `sp_int`、子が `sp_RbVal` (構造体の先頭一致が崩れている)。出力には現れない | 既存テストの生成 C | `01521b1e` | U-6 で直る (新しい PR は不要。U-6 の本文の材料) |
| N-5 | 32bit のコーパスを sudo 無しで回す方法: 空の `crypt()` を持つ i386 の `libcrypt.a` を `LIBRARY_PATH` で見せる。失敗は `String#crypt` を使う 3 本だけ | `$S/stub32/` | `01521b1e` | 台帳 P-12 の回避策として記録 (上流への PR ではない) |

`g_pre` に宣言の左辺を先に書いてから右辺を別バッファに出力する形は、
U-10 の 2 箇所の他にも `codegen_stmt.c` などに数十箇所ある。右辺が
ルートの文を積む呼び出しになる経路があれば同じ壊れ方をするはずだが、
再現は作っていないので候補には入れていない。

## 後片付けと残したもの

- `$W` の本体は最後に `extra.sh` が `pr/P-3` を detached で checkout した状態。
  ブランチは `pr/P-3` `pr/U-10` `pr/U-11` `pr/U-6` `pr/U-6-check` `pr/U-12`
  `pr/P-9` と確認用の `check/U-6+check`。
- worktree (`$W/.worktrees/`): 各 ID、`base` (基点)、`combo`
  (`check/U-6+check`)、`m32base` `m32p9` (32bit ビルド)。検収後に
  `git worktree remove` で消してよい。
- 32bit の代用 libcrypt は `$S/stub32/`。
- 作業中に 32bit ビルドのログを誤って `/home/kishima/fmrb/wt/` 直下に書いたので、
  すぐ scratchpad へ移した。`/home/kishima/fmrb/wt/spinel-rebase/` は別の作業の
  もので触れていない。
