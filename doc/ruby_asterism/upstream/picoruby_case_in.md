# Upstream issue draft: case/in in PicoRuby's compiler

> Status: draft, **not filed** (2026-10-09). Both problems are already fixed upstream, so the recommendation is not to file it but to move Family mruby's vendored PicoRuby forward (see "Where it stands upstream"). Kept as the record of what was checked, and as the text to use if a filing is still wanted.

Files next to this one:

- `case_in_repro.rb`: the minimal reproduction (plain Ruby, runs on CRuby, mruby and PicoRuby).
- `picoruby_case_in.patch`: a candidate patch for the vendored compiler (a backport of the two upstream fixes).
- `../../../flash/app/test/case_in_repro.app.rb`: the same checks as a Family mruby app (compiled by the app loader, not through `eval`).

## Where the compiler lives

- Family mruby vendors `picoruby/picoruby` at `c932f70b` (2026-07-11) in `components/picoruby-esp32/picoruby`.
- PicoRuby's compiler is the submodule `mrbgems/mruby-compiler` = `picoruby/mruby-compiler2` at `10408c3` (2026-07-11): the Prism-based mruby compiler.
- `picoruby/mruby-compiler2` is a mirror. Its README says the canonical source is `mruby/mruby`'s `mrbgems/mruby-compiler`, and that patches go to `mruby/mruby`. So **the right upstream for an issue or a patch is `mruby/mruby`**, not `picoruby/picoruby`.

## Where it stands upstream

| Problem | Fixed in mruby/mruby | Reaches PicoRuby |
|---|---|---|
| A class / range / regexp as a hash pattern's value never matches, and a literal value always matches | `0e6bac0e5a7e` "fix the operand order of a hash-value pattern" (PR #7507, merged 2026-09-05) | `picoruby/mruby-compiler2` `a2c72afb` (sync of mruby `6e0a260d`, 2026-10-02), pinned by `picoruby/picoruby` master `a90afd12` (2026-10-05) |
| A pattern inside a block does not bind a local of the enclosing scope | `680084ac1275` "bind a pattern capture to a local of an enclosing scope" (PR #7562, merged 2026-09-08) | same |

Checked by running `case_in_repro.rb` (all on x86-64 Linux):

| Build | Result |
|---|---|
| CRuby 3.4 | 4 ok |
| Family mruby sim, app VM (standard and compat builds, `flash/app/test/case_in_repro.app.rb`) | 4 NG |
| `picoruby/picoruby` `c932f70b` (the vendored commit), host build | 4 NG |
| the same with `picoruby_case_in.patch` applied | 4 ok |
| `picoruby/picoruby` master `a90afd12`, host build | 4 ok |
| `mruby/mruby` master `09dbc5f3` (2026-10-08), host build | 4 ok |

Duplicates: `gh search issues` on `mruby/mruby`, `picoruby/picoruby` and `picoruby/mruby-compiler2` ("pattern matching", "case in") found no open issue for these. The fixes came as PRs without a separate issue.

## Issue draft (for mruby/mruby, only if one is still wanted)

**Title**: mruby-compiler: hash-pattern values compared the wrong way round, and captures in a block do not reach outer locals

**Environment**

- mruby-compiler as vendored by PicoRuby: `picoruby/mruby-compiler2` `10408c3` (synced from mruby `55dcb6449278`), inside `picoruby/picoruby` `c932f70b`.
- PicoRuby host build (`rake`, default config: mruby VM), Linux x86-64. Also seen in Family mruby's Linux simulation, whose app VM is built from the same sources.

**Reproduction** (`case_in_repro.rb`)

```ruby
def check(label, got, want)
  puts "#{got == want ? 'ok' : 'NG'} #{label}: got #{got.inspect}, want #{want.inspect}"
end

r = case {x: 1}
    in {x: Integer} then :int
    else :no
    end
check("in {x: Integer}", r, :int)

r = case {x: 1}
    in {x: 0..2} then :range
    else :no
    end
check("in {x: 0..2}", r, :range)

def bind_in_block
  pre = nil
  [{a: 1}].each do |m|
    case m
    in {a: pre} then nil
    end
  end
  pre
end
check("in {a: pre} inside a block", bind_in_block, 1)

def rightward_in_block
  a = nil
  [[1]].each { |x| x => [a] }
  a
end
check("x => [a] inside a block", rightward_in_block, 1)
```

**Expected** (CRuby 3.2 to 4.0)

```
ok in {x: Integer}: got :int, want :int
ok in {x: 0..2}: got :range, want :range
ok in {a: pre} inside a block: got 1, want 1
ok x => [a] inside a block: got 1, want 1
```

**Actual**

```
NG in {x: Integer}: got :no, want :int
NG in {x: 0..2}: got :no, want :range
NG in {a: pre} inside a block: got nil, want 1
NG x => [a] inside a block: got nil, want 1
```

**Notes**

1. Hash-pattern values. In `codegen_pattern`'s `PM_HASH_PATTERN_NODE` loop the value fetched with `[]` sits at `val_reg`, and `s->sp` is left at `val_reg` when the sub-pattern is compiled. A value pattern compiles to `pattern === target` by generating the pattern at `cursp()`, which is then `val_reg` itself, so the pattern overwrites the value. A literal therefore always matches when the key is present (`{x: 4} in {x: 3}` is `true`, `case {x: 4} in {x: 3}` takes that branch), and a class or a range never matches. Fix: `push()` before compiling the sub-pattern so the value stays below `cursp()`, as the array-pattern path already does.
2. Captures in a block. Every binding site in `codegen_pattern` (`PM_LOCAL_VARIABLE_TARGET_NODE`, `PM_CAPTURE_PATTERN_NODE`, the `*rest` / `**rest` / find-pattern splats) looks the name up with `lv_idx(s, name)` in the current scope only and binds nothing when it is 0. A local of the enclosing method, seen from a block, has index 0 there. Prism records the target's `depth`; binding through `gen_assignment_lvar(s, src, name, depth + s->for_depth, 1)` (which emits `OP_SETUPVAR` for depth > 0) is what an ordinary assignment does. A new name that is local to the block works today, which hides the problem in most code.
3. Related forms that also differ from CRuby at `10408c3` and are fixed on mruby master as well: `^x` of an outer local inside a block and `^(expr)` (PR #7539, `8ea99618663d`), `Const[...]` / `Const(...)` ignoring the constant (PR #7539, `325cfaf4addd`), a later `in` clause reading a register the first one never wrote (PR #7539, `951f5a0b4983`), and `in {a:, **rest} then [a, rest]` returning `rest` alone (the exact commit not pinned down; fixed on master).

## What Family mruby should do (not done here)

- Move `components/picoruby-esp32/picoruby` to a PicoRuby that pins `mruby-compiler2` `a2c72afb` or later (PicoRuby master `a90afd12` does). That brings in every fix above, plus two found by the K1 profile that are also fixed on PicoRuby master: `protected` methods called from an instance of the same class, and `$!` in a rescue modifier.
- If moving PicoRuby is too large a step, `picoruby_case_in.patch` fixes the two problems of this draft alone (checked on a host build). It touches only `src/codegen.c`.
- Until then, the shared layer and apps keep the workarounds: no class or range values in hash patterns, and a `case` that binds goes into its own method rather than into a block (asterism README, Limits).
