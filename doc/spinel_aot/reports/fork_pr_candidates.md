# Spinel fork PR 候補・知見統合リスト

Spinel AOT 化 (Phase 0-2) で判明した Spinel 本体のバグ・機能不足・落とし穴を
1 本にまとめた索引。フォークは `tmp/spinel` branch `fmrb-dev`。
汎用 (fmruby 非依存) の修正は upstream (matz/spinel) への PR 候補。

- 詳細な根本原因・最小再現は `phase0_findings.md` / `phase1_report.md` /
  `phase2_report.md` を参照 (本表はその索引)。
- 「汎用性」= fmruby に依存しない一般バグ/機能かの判定。汎用のみ PR 対象。

---

## A. 適用済み (fmrb-dev コミット済み・回帰ゼロ) — upstream PR 候補

いずれも `make test` 1,991 pass / 1 fail (既存 cosmetic の 1 件のみ、回帰ゼロ)、
`make bench` 通過を確認済み。汎用バグ/機能なので master ベースの短命ブランチへ
cherry-pick すれば PR 化可能。

| commit | 区分 | 内容 | 位置 |
|---|---|---|---|
| `318f4a7b` | バグ | `String#setbyte` が poly 第2引数を受理せず C コンパイル失敗 (`emit_expr`→`emit_int_expr`) | `src/codegen_call_recv.c` |
| `56394f2d` | バグ | poly が保持する String の `#size`/`#length` が strlen 停止 (埋め込み NUL で誤値)。`strlen`→`sp_str_length` | `lib/sp_runtime.h` `sp_poly_length` |
| `7b820768` | バグ | include された module メソッド内の `rescue => e` が poly 型のまま (clone scope の特殊化漏れ)。`specialize_rescue_vars` を clone 後に再実行 | `src/analyze.c` |
| `a8c3c201` | バグ | `String#*` が埋め込み/先頭 NUL を strlen 切り捨て (`"\x00"*3`→0 byte)。格納長ベースに | `lib/sp_str.c` `sp_str_repeat` |
| `d9e363ed` | 移植 | `sp_time.c` の `__int128` が 32bit (i386/**Xtensa/ESP32**) で不可。`__SIZEOF_INT128__` ガード + double フォールバック | `lib/sp_time.c` |
| `d9c6dbf9` | バグ | `String#ljust/rjust/center` が poly レシーバで dispatch されない (U-1) | analyze/codegen |
| `5129af95` | バグ | 値が消費される poly return を void 化してしまう + value-type block local の reset 漏れ (U-3)。値消費構文スキャンを追加 | analyze/codegen |
| `9aa7cdd6` | 機能 | library mode `--no-main` / `--entry <fn>` / `--inject`。カーネルを C 関数として組込むために必須 (汎用機能) | `main`/codegen |

共通根本テーマ:
- **NUL 一貫性** (`56394f2d` / `a8c3c201`): poly/concrete/各 String 関数で埋め込み
  NUL の扱いが strlen ベースと格納長ベースで食い違う。他の String 関数も要点検。
- **poly ディスパッチ**の穴 (`318f4a7b` / `d9c6dbf9` / `5129af95`): poly 値を
  builtin/メソッドに渡す経路で型特殊化が非対称に漏れる。
- **32bit 移植** (`d9e363ed`): ESP32 (Phase 5) 必須。

---

## B. 未修正の PR 候補 (現状は Ruby 側回避で対処)

Phase 2 までは Ruby 側の書き換えで回避済み。汎用のものは将来 fork で直せば
回避コードを削除できる。

### B-1. バグ (Spinel が誤動作)

| 区分 | 内容 | 現状の回避 | 詳細 |
|---|---|---|---|
| ディスパッチ | poly レシーバの `.delete(int)` が `String#delete` に誤 dispatch (`Array#delete` であるべき) | 明示ループで rebuild | phase2_report |
| 名前解決 | module/class に定義した `def self.name` が呼ばれず、**組み込み `Module#name` (モジュール名文字列) に解決**される。実 Ruby は self.name で上書き可 | メソッド名を `name` 以外に (例 `read_name`) | phase2_report |
| コメント誤検出 | `source_references_set` がコメント/文字列リテラル内の `Set` も検出し `require "set"` を自動 splice (その set.rb がコンパイル不能) | コメントから `Set` を除去 | phase2_report |
| スコープ | module レベルの mutable global 配列 (`$calls = []` + module メソッド内 `<<`) が `gv_calls undeclared` の壊れた C を生成 (int/bool の global は正常) | 配列 global を使わず直接出力 | phase0_findings |

### B-2. 機能不足 (未実装)

| 内容 | 現状の回避 | 詳細 |
|---|---|---|
| `Integer#chr` / `sp_str_chr` がランタイム未実装 | `"\x00"*n` + `setbyte` | phase2_report |
| `hash[k] ||= v` (IndexOrWriteNode) 未対応 | `x = [] unless x` 形へ | phase2_report |
| FFI: `ffi_buffer` の per-byte リーダが無い (`:ptr` に getbyte 不可、`ffi_read_u8`/`u16` 相当なし)。C 構造体のバイト列読みが `ffi_read_u32/i32/ptr` に限られる | C 関数の戻りを `:binstr` (実 String) にして getbyte | phase2_report / FFI.md |
| bundled `set.rb` 自体が `Set#&` を poly `other` でコンパイル不能 (Set 型を使うと連鎖的に不可) | Set を使わない | phase2_report |

### B-3. インフラ (fmruby 側の shim で暫定対応中)

| 内容 | 現状 | 詳細 |
|---|---|---|
| `:binstr` のバイト長 global `sp_net_bin_len` が `sp_net.c` 所有で recv 系専用。任意の `ffi_func` が binary 長を publish できない | fmruby 側 (`fmrb_spx_kernel.c`) で `sp_net_bin_len` を定義して流用。**汎用化 (`sp_ffi_bin_len` 等をコア runtime に) すれば任意 ffi_func が clean に :binstr 返却可** = PR 候補 | phase2_report |

---

## C. 恒久的な設計課題 (大改修・要判断)

- **typed symbol-hash 型が無い** (`SYM_INT`/`SYM_STR` 等)。symbol キー hash は常に
  poly 値 (`win[:x]`, `msg[:data]` 等)。性能 (AOT 利点減) と互換の鍵。
  types/analyze/codegen/runtime 全体に及ぶ Phase 1/3 級の大改修。設計評価は
  `phase1_report.md` (sp_StrIntHash がテンプレ、3 層 3-5 日規模、Phase 3 実装推奨)。
  当面は **FFI 境界に poly を持ち込まない** (payload をバイト列のまま渡す) で回避。

---

## D. Spinel を使う側の回避ルール (再発防止メモ)

コード規約として実装 AI/開発者が守るべき Spinel の癖:

1. module/class に **`name` 等の組み込みメソッド名の `self.method` を定義しない**
   (`Module#name` に解決される。B-1 参照)。
2. コメント/文字列に **bareword `Set`** を書かない (auto-require が発火)。
3. FFI 境界に **poly を持ち込まない** (`.to_s` で concrete 化、payload はバイト列)。
4. **C が埋めたバイト列は `:binstr` で受ける** (`ffi_buffer` = `:ptr` は getbyte 不可)。
5. `hash[k] ||= v` / `Integer#chr` / poly `Array#delete` / module 級 mutable
   global 配列 を避ける (B 参照)。
6. 未対応構文の最小再現は `tool/spinel_poc/coverage/UNSUPPORTED.md` (U-1/U-2/U-3)。

---

## PR 化の進め方 (メモ)

- fmrb-dev は汎用変更と fmruby 固有変更を分離してコミット済み。**汎用コミットのみ**
  を master ベースの短命ブランチへ cherry-pick して PR。
- A の各コミットは単独で `make` / `make test` / `make bench` 通過を確認済み。
- `Co-Authored-By` トレーラは一部コミット (`5129af95` 等) に付与済み。upstream 提出前に
  rebase で整えられる。
