# T0-4 coverage: Spinel で回避が必要だった構文

7 本の coverage スクリプトのうち 4 本 (cov_binary / cov_i18n / cov_launcher /
cov_shell_io) は無改変で CRuby と byte 一致。残り 3 本は以下の Spinel の癖に
当たったが、**いずれも軽微な書き換えで回避可能** (回避不能ゼロ = Go 基準充足)。
各項目に最小再現・回避策・恒久対策案を記す。

共通の根っこは「ネスト配列 (`[["a","b"], ...]`) は PolyArray になり要素が poly」
+ 「symbol/文字列キー hash 値も poly」という poly 化 (phase0_findings.md 参照)。

---

## U-1: poly レシーバの `String#ljust` / `rjust` が未 dispatch (NoMethodError)

- 最小再現:
  ```ruby
  rows = [["Help", "x"]]
  rows.each { |row| puts row[0].ljust(8) }   # => undefined method 'ljust' for an instance of String
  ```
- 事実: `poly.upcase` / `downcase` / `reverse` / `strip` / `size` 等は poly でも
  dispatch されるが、`ljust` / `rjust` は poly レシーバ経路 (codegen_call.c の
  `rt == TY_POLY && argc > 0` dispatch, line ~3072) に arm が無く、実行時に
  NoMethodError。concrete String の `ljust` は正常。
- 回避策: `row[0].to_s.ljust(8)` (`to_s` で concrete String 化してから)。出力は
  CRuby と一致。
- 恒久対策 (Phase 1 hardening): poly-arg dispatch に String#ljust/rjust/center の
  TAG_STR arm を追加 (既存の `is_strdel` / `is_strftime` と同様の 1 arm)。
  汎用バグなので upstream PR 可能。fmruby の i18n/shell 整形で頻出するため優先度中。

## U-2: 同名 `rescue => e` を複数 arm で使うとサブクラス特殊化されない

- 最小再現:
  ```ruby
  begin
    ...
  rescue AppError => e
    e.message           # OK
  rescue FatalError => e
    e.code              # => unsupported call: `code` (e は generic Exception 型)
  end
  ```
- 事実: 同一スコープで同名 `e` を複数 arm が束縛すると、Spinel は 1 つの LocalVar に
  intern し「全 arm が同じ例外クラスに一致するときのみ特殊化」する (analyze.c の
  rescue 特殊化コメント参照)。異なるクラス (AppError/FatalError) なので generic
  TY_EXCEPTION に留まり、サブクラス固有メソッド `e.code` が解決できない。
  **これは Spinel の意図的制約** (1 スロットを 2 クラスに特殊化できない)。
- 回避策: arm ごとに別名を使う (`rescue AppError => ae` / `rescue FatalError => fe`)。
  これは OS コード規約に足すべき指針 (fmruby の rescue は概ね `rescue => e` の 1 arm
  なので実害は限定的)。

## U-3: ネスト配列由来の poly が「コンストラクタ引数」「mixin 算術メソッド」で miscompile

- 最小再現 (a) block 分割代入 → コンストラクタ:
  ```ruby
  [[10, 10], [100, 50]].each do |w, h|
    win = W.new(w, h)   # => incompatible types when assigning to 'sp_W' from 'void *'
  end
  ```
  `each do |pair|; w = pair[0]` と添字なら W.new は通る。
- 最小再現 (b) poly を mixin 算術メソッドに渡すと戻り値が void 化:
  ```ruby
  module M; SCALE = 2; def scaled(v); v * SCALE; end; end
  class W; include M; def initialize(w); @w = w; end; def dbl; scaled(@w); end; end
  arr = [[10]]
  arr.each { |pair| w = pair[0]; win = W.new(w); puts win.dbl }  # => invalid use of void expression
  ```
  `@w` (poly) を `scaled` (transplant された mixin method) に渡すと `v * SCALE` の
  戻り値型が void/unknown になり、補間 (`#{win.dbl}`) で C エラー。
- 事実: 根っこは「ネスト配列 = PolyArray で要素が poly」+「その poly が
  transplant された mixin メソッドやコンストラクタの引数経路で型推論を壊す」。
- 回避策: 値を concrete に保つ (nested-array iteration を避け直接引数、または
  `.to_i` 等で具体化)。coverage では driver を concrete 引数の直接呼び出しに変更。
- 恒久対策 (Phase 1 hardening): poly が transplant mixin メソッド/コンストラクタ
  引数に流れる経路の型推論を修正。汎用バグ。優先度中。

---

## まとめ

- 回避不能な未対応構文: **ゼロ** (T0-4 Go 基準を満たす)。
- U-1 / U-3 は汎用 Spinel バグで Phase 1 hardening で修正推奨 (回避策あり)。
- U-2 は Spinel の意図的制約で、OS コード規約 (rescue arm ごとに別名) で対応。
- coverage スクリプトは回避版に更新済みで、7 本すべて CRuby と byte 一致する。
