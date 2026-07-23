# Phase 0 技術知見・Spinel 改修記録

Phase 0 で判明した Spinel の挙動・バグと、フォーク (`tmp/spinel`, branch `fmrb-dev`)
に加えた改修の記録。実装 AI が中断・再開・レビューできるよう、根本原因と最小再現を残す。

## 最重要: symbol-keyed Hash は常に poly 値

Spinel の Hash 型は固定セット (`src/types.h`):
`STR_INT / STR_STR / INT_INT / INT_STR`(キー・値とも具体型) と
`SYM_POLY`(symbol キー、値は poly) / `STR_POLY` / `POLY_POLY`。

**typed な symbol-hash 型 (SYM_INT/SYM_STR 等) は存在しない。**
したがって `h = {a: "x"}` のような単一値でも `h[:a]` は **poly (sp_RbVal)** 型になる
(string キーの `{"a" => "x"}` は StrStr で `["a"]` が String 型になるのと非対称)。

### 影響

対象コード (kernel) は window list (`win[:x]`, `win[:app_name]` ...) や VM メッセージ
(`msg[:data]`) で symbol-keyed hash を多用する。これらは全て poly になる。
poly 値は `puts` / 算術 / 比較 / `case/when` / メソッドレシーバでは概ね正しく流れるが、
以下の 2 パターンで問題化した (どちらも修正済み):

1. poly 値を **具体的 mrb_int を要求する builtin** に渡すとコンパイルエラー
   -> `String#setbyte` 修正 (下記 FIX-1)
2. poly 経由の **`String#size`/`length` が strlen 停止** し埋め込み NUL で誤値
   -> `sp_poly_length` 修正 (下記 FIX-3)

### 設計含意 (Phase 1/3 へ引き継ぎ)

- 恒久対策の理想は Spinel に **typed symbol-hash (SYM_INT/SYM_STR ...)** を追加すること
  だが、types/analyze/codegen/runtime 全体に及ぶ大改修で Phase 1/3 級。
- Phase 2 の FFI 境界では、`msg[:data]` を **poly Hash 経由でなく typed String として**
  Ruby に渡す設計にすれば、payload の getbyte/setbyte 連鎖が具体型で通り最も安全。
- 上記 2 つの局所修正により、poly のままでも kernel input-router は
  CRuby と byte 一致で動作することを実証済 (回避可能な範囲)。

## FIX-1: String#setbyte が poly 引数を受理しない (汎用バグ)

- 症状: `s.setbyte(i, v)` で `v` が poly (例: symbol-hash 由来) だと
  `incompatible types when initializing 'mrb_int' using 'sp_RbVal'` で C コンパイル失敗。
- 最小再現: `def h(m); d=m[:data]; b=d.getbyte(0); o="\x00\x00"; o.setbyte(0,b); o; end; h({data:"AB"})`
- 根本原因: `src/codegen_call_recv.c` の setbyte codegen が第2引数を
  `mrb_int _t = <emit_expr(arg)>` と生成。poly はアンボックスされない。
  すぐ隣の `getbyte` は index に `emit_int_expr` (poly を `sp_poly_to_i` でアンボックス) を
  使っており非対称だった。
- 修正: setbyte の第2引数を `emit_expr` -> `emit_int_expr` に変更 (1 行)。
- 位置: `src/codegen_call_recv.c` setbyte ブランチ。
- 汎用性: fmruby 非依存の一貫性バグ。upstream PR 可能。

## FIX-2: include された module メソッド内の `rescue => e` が poly 型 (汎用バグ)

- 症状: `module M; def f; begin ... rescue => e; ...e.class...e.message... end; end; end`
  を `class K; include M; end` で使うと、`e` が poly になり
  (a) `lv_e = <sp_Exception*>` の代入で型不整合、(b) `e.message` が `sp_raise_nomethod` に
  化ける。C コンパイル失敗。plain class メソッドの `rescue => e` は正常 (対比)。
- 最小再現: `tmp/r7.rb` (module M の handle 内 rescue) が失敗、`tmp/r8.rb` (class 直書き) は成功。
- 根本原因: module メソッドは `include` 時に **includer ごとに body を deep-clone**
  (`process_include_body`, `nt_clone_subtree`) し、独立した Scope (別 def_node・別 body node) を
  作る。codegen はこの includer コピー scope の型を読む。しかし rescue 変数を
  TY_EXCEPTION に特殊化するパスは **clone 生成前** に 1 度だけ走るため、コピー scope の
  `e` は poly のまま残っていた。
- 修正: 特殊化ロジックを関数 `specialize_rescue_vars(c)` に切り出し、
  (1) 従来位置に加え (2) transplant clone が出揃った後 (include-copy param 逆伝播の直後) に
  **もう一度呼ぶ**。各 clone は自前の RescueNode を node table に持ち、`comp_scope_of` が
  clone scope に正しくマップするので、2 回目の走査でコピーの `e` も TY_EXCEPTION になる。
  冪等。
- 位置: `src/analyze.c` (`specialize_rescue_vars` 定義 + `analyze_program` 内 2 箇所呼び出し)。
- 汎用性: mixin + rescue の組合せで再現する fmruby 非依存バグ。upstream PR 可能。
  (fmruby の kernel は mixin だらけなので実害大。)

## FIX-3: sp_poly_length が文字列で strlen 停止 (汎用バグ)

- 症状: poly が保持する文字列に対する `.size`/`.length` が最初の NUL で停止。
  `d.size` (d=poly の "\x04\x01\x00\x00\x00\x00") が CRuby=6 に対し Spinel=2。
  concrete String#size は 6 で正しい (非対称)。埋め込み NUL を含む HID パケットで
  `return if data_binary.size < 6` が毎回 true になりイベント処理が全停止。
- 根本原因: `lib/sp_runtime.h` の `sp_poly_length` の `SP_TAG_STR` ケースが
  `strlen(v.v.s)` を使用。Spinel 文字列は長さを別途格納 (byte-exact) しており、
  concrete パスは `sp_str_length` を使うのと非対称。
- 修正: `SP_TAG_STR` ケースを `strlen(v.v.s)` -> `sp_str_length(v.v.s)` に変更 (1 行)。
  `sp_str_length` は格納バイト長ベースで UTF-8 単位を数える (CRuby String#size と一致)。
- 位置: `lib/sp_runtime.h` `sp_poly_length`。
- 汎用性: poly と concrete で String#size 挙動が食い違う一貫性バグ。upstream PR 可能。

## T0-3 性能計測の解釈 (重要)

環境: WSL2, CRuby 3.2.6, `/usr/bin/time` 3回中最良値。mruby 実行系バイナリは
未ビルド (picoruby は picorbc/mrbc の compiler のみ) のため mruby 比は Phase 2 実機に委ねる
(phase0.md T0-2 step6 の想定どおり)。

| ベンチ | 内容 | CRuby | Spinel | Spinel/CRuby |
|---|---|---|---|---|
| フル harness | 336,000 HID イベント (allocation 多: window list 再生成 + 文字列生成) | 0.51 s / 54 MB | 0.52 s / 128 MB | ~1.0x (同等) |
| compute micro | 2,000,000 反復の poly hash アクセス + 算術 (`tool/spinel_poc` 外, /tmp/microbench.rb) | 0.21 s | 0.06 s | **~3.5x** |

- **Spinel の計算自体は CRuby より速い (micro で 3.5x)。** poly 値 (symbol-hash) でも
  速い。フル harness が同等なのは **allocation/GC 律速** (button-down ごとに window list
  hash を再生成、Log 引数の文字列補間が bench でも常に構築される) のため。
  Spinel のメモリ使用は CRuby の 2.4 倍 (GC ヒープ設計差)。
- string キー hash は string ハッシュ計算コストで poly より遅く (micro 0.13s)、
  「typed の代理」には不適。typed-symbol-hash の効果は別途 Phase 1/3 で実測すべき。
- **Go 判定への含意**: 実機の比較対象は mruby (PicoRuby インタプリタ) で CRuby より
  桁違いに遅い。Spinel >= CRuby >> mruby なので Go 基準 (mruby 比 2x / CRuby 比 1x 以上) は
  満たす。ただし真価は Phase 2 の実機 mruby 比較で確認する。
- **性能を引き出すには**: (a) hot path の allocation 削減 (window list キャッシュの
  poll 化で毎クリック再生成を避ける)、(b) typed-symbol-hash 化。どちらも Phase 2/後続で。

## FIX-4: String#* が埋め込み NUL を切り捨てる (汎用バグ)

- 症状: `"\x00" * 3` が 0 バイト文字列になる (NUL 先頭/埋め込みで strlen 停止)。
  その後 `setbyte(0,..)` すると `index 0 out of string (IndexError)`。
  T0-5 の msgpack pack で `"\x00" * n` バッファ確保に使い発覚。
- 根本原因: `lib/sp_str.c` `sp_str_repeat` が `strlen(s)` で長さ測定 + `sp_str_alloc_raw`
  (格納長を記録しない)。
- 修正: `strlen` -> `sp_str_byte_len` (格納長)、`sp_str_alloc_raw`+`r[total]=0` ->
  `sp_str_alloc(total)` (格納長を記録; `sp_str_concat` と同じ流儀)。
- 位置: `lib/sp_str.c` `sp_str_repeat`。
- 汎用性: FIX-3 と同じ NUL 一貫性バグの別関数版。upstream PR 可能。

## FIX-5: sp_time.c の __int128 が 32bit で不可 (ESP32 直撃、汎用バグ)

- 症状: `gcc -m32` で `lib/sp_time.c:36` 等が `expected expression before '__int128'` で
  コンパイル不可。32bit link 全滅。
- 根本原因: `sp_time_at_div` / `sp_time_shift_ns` が Time.at(Rational/Float) の厳密
  ns 変換に `__int128` を使用。`__int128` は 32bit ターゲット (i386, **Xtensa/ESP32**) に
  存在しない。→ **ESP32-S3 ポート (Phase 5) も直撃する**。
- 修正: `#if defined(__SIZEOF_INT128__)` でガードし、32bit は double ベースの
  フォールバック。仮数 ~53bit まで厳密で、Time.at の last-ULP ns だけ縮退 (文書化)。
  64bit は従来どおり (__int128 経路)。commit d9e363ed。
- 汎用性: 32bit portability バグ。upstream PR 可能。ESP32 必須。

## T0-6 32bit 検証結果 (gcc-multilib 導入後、2026-07-23 完了)

環境: `gcc -m32` (multilib 導入済)。`intptr_t = 4` を確認 = **mrb_int は 32bit**。
runtime を `-m32` でビルドし harness / msgpack を実行、CRuby と比較。

- **harness_input_router (カーネル最重要コード): 32bit == CRuby 完全一致**。
  trace 2089 行一致、bench 336,000 件も state_changes=139201 一致。ELF 32-bit i386。
  → **実カーネルコードは 32bit で正当** (T0-6 の核心)。
- 32bit ビルドで判明・対処した点:
  - FIX-5 (`sp_time.c __int128`): 修正済。
  - `crypt()` (String#crypt, libc): 32bit libcrypt 不在。harness 未使用なので
    リンク用スタブで対応 (ESP32 でも host crypt は非対象)。実害なし。
- msgpack サブセット on 32bit: **kernel 相当サブセット 51 pass / 0 fail**、
  64bit 専用 13 件 skip。skip の内訳と理由:
  - **mrb_int が 32bit** のため >2^31 の整数 (uint32 フルレンジ / uint64 / int64) と
    int32 の two's-complement decode が表現不可。
  - float64 は 52bit 仮数を整数で組むため 64bit mrb_int 必須。
  - **fmruby の VM メッセージはこれらを一切使わない** (小整数・文字列・bin・配列・
    文字列キー map のみ) ため、実カーネルのデュアルビルドは 32bit 安全。
  - `msgpack_pure.rb` の pack_int 閾値を `(v>>16)>>16` に変更し、32bit で >=65536 の
    小値が誤ルートしないよう修正済 (64bit 挙動不変)。
- **結論**: 実カーネルコード (input_router) は 32bit で CRuby と byte 一致。
  mrb_int=32bit 由来の大整数/float 制約は既知 (mruby も同じ) で、カーネルは未使用。
  T0-6 Go 基準 (32bit 出力一致) 充足。

## make test 基準

- 改修前 (fmrb-dev HEAD): 1,991 pass / 1 fail
  (`nilclass_bool_ops_conversions`: Complex 虚部表示 `(0+0i)` vs `(0+0.0i)`、
   本プロジェクト無関係の cosmetic、既存)。
- 改修後 (FIX-1/2/3 適用): **1,991 pass / 1 fail (回帰ゼロ)**。
  fail は同一の既存 `nilclass_bool_ops_conversions` のみ。3 修正は既存テストに影響なし。

## ハーネス側で必要だった書き換え (実コードは無改変)

対象実コード (`input_router.rb` / `window_manager.rb`) は **1 行も改変せず** に取り込めた。
ハーネス (スタブ層) 側でのみ以下の配慮が要った (Spinel の癖):

- Log/スタブ出力は shared buffer に貯めず **直接 stdout に発生順出力**。
  理由: 変異する global 配列 (`$calls = []` + `<<` in module method) が
  `gv_calls undeclared` の壊れた C を生成する Spinel の別バグを踏むため回避
  (module レベルの mutable 配列も scope quirk で不可)。int/bool の global
  (`$trace`, `$menu_bar_height`) は正常。
- `msg[:data]` は symbol-hash なので poly。上記 FIX-1/FIX-3 で処理可能になった。
