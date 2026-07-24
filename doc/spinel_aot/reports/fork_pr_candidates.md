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
| コメント誤検出 | `source_references_set` がコメント/文字列リテラル内の `Set` も検出し `require "set"` を自動 splice (その set.rb がコンパイル不能) | **fork 修正済 `44e2d57`**: コメント/文字列をスキップする state machine (`#{...}` 補間は code 走査)。回帰 test 1991/1 bench 58/0 | phase2_report / **DONE** |
| スコープ | module レベルの mutable global 配列 (`$calls = []` + module メソッド内 `<<`) が `gv_calls undeclared` の壊れた C を生成 (int/bool の global は正常) | 配列 global を使わず直接出力 | phase0_findings |
| 推論 | 戻り値の FFI 型が `:void` のメソッド (`Log.*`) をメソッド末尾で呼ぶと、そのメソッド全体が「値を返さない (void)」と推論される。値の位置の分岐が noreturn (`FmrbApp.reboot`) か void leaf に到達する場合も同様に void 化 | `Log.*` を末尾 `nil` で返す / メソッド末尾に明示 `nil` (末尾 nil×5) | T4-3 (48eba26) |
| codegen/推論 | **poly 値が `const char*` (`:str`) 引数へ coercion 無しで流れる**のが根因。最小再現確認済 (V4/V5/V6): poly 返しメソッド (nilable hash lookup 等) を `sprintf`/`sp_str_concat`/`sp_File_open` に直渡し。2 形態=(a) `incompatible types ... const char* using sp_RbVal` (通常), (b) GC-root 圧下で GC-root 展開が `const char*` 初期化子に落ち `expected expression before sp_RbVal` の壊れた C (str_run_clear で発現)。**両者同一根因 → 1 修正 (`:str` 引数位置で poly→cstr coercion を statement で挿入) でカバー**。repro: `scratchpad/repro_poly_to_cstr.rb` | **fork 修正済** `9474d92`(string op-write concat 引数)+`8a298cb`(sprintf/format・File.open 引数)+`286de9b`(sprintf format を const-char temp を開く前に emit=GC-root 初期化子混入解消)。`emit_str_expr` 経由で :str 位置の poly を `sp_poly_to_s` に coercion。回帰 test 1992/1 bench 58/0。fmruby-core 側は pin 286de9b + `.to_s`/hoist 撤去(byteslice/File.open/sprintf の3箇所)、2-Spinel で実行実証。(launcher 三項の byteslice は別根因=下行の poly レシーバ dispatch gap、d26c1f9 で解決) | T4-3 / **DONE** |
| ディスパッチ | poly レシーバの `String#byteslice(start, len)` が dispatch されず `undefined method 'byteslice' for an instance of String` を raise。concrete String は静的 dispatch 済 (codegen_call_recv.c)、poly も `sp_poly_bytesize` は持つのに byteslice だけ欠落 = ljust/rjust/center (U-1) と同族の poly-String メソッド gap。sym-hash 値 (`app[:label]`) や poly-widened な method param (`FmrbI18n.truncate_to` の str) が該当。**`.to_s` は結果に付いていた**ため受信側 poly を直せず、launcher overlay を開くと desktop がクラッシュしていた | **fork 修正済 `d26c1f9`**: `is_strjust` と同型の SP_TAG_STR pre-arm (codegen_call.c) + TY_STRING 戻り推論 (analyze_infer.c)。args の poly-widen は `sp_poly_to_i` で unbox。非 String は従来通り NoMethodError。test/poly_str_byteslice.rb。回帰 test 1993/1 bench 58/0。headless で launcher 二行ラベル (byteslice) 実行実証 | T4-5 / **DONE** |
| ディスパッチ | poly レシーバの `String#index/rindex/start_with?/end_with?/split` が dispatch されず raise (byteslice と同族)。taskbar `label.rindex("/")` で**アプリ起動時に desktop クラッシュ**、config `line.index("=")`、launcher scan `f.end_with?(".toml")`、dialog `msg.split("\n")` 等 | **fork 修正済 `78c7cb20`**: SP_TAG_STR pre-arm (codegen_call.c) + 戻り型推論 (analyze_infer.c)。index/rindex→nullable int (`sp_str_*index_opt/_poly`)、with?→bool、split→StrArray (`sp_str_split_drop_trailing`)。test/poly_str_methods.rb。test 1994/1 bench 58/0 | T4-5 / **DONE** |
| 意味論 | **`Array#[]=` が末尾超えで auto-extend しない**。`sp_PolyArray_set` が `i >= len` で silent no-op (typed の Int/Str/FloatArray_set は while で nil 埋め拡張済=PolyArray だけ outlier)。CRuby は nil 埋めで拡張。→ 空配列からの index 構築 (`a=[]; a[idx]=v`) が全 drop。**launcher の `@icon_sprite_instances[idx]=sprite` が保存されず、アイコンが文字フォールバック表示**になっていた (非クラッシュ・警告なし) | **fork 修正済 `b24b1956`** (runtime, lib/): `while (a->len <= i) sp_PolyArray_push(a, sp_box_nil())`。StrArray_set と同型。test/poly_array_set_extend.rb。test 1995/1 bench 58/0。headless で 29 アイコンがスプライト描画を実証 | T4-5 / **DONE** |
| ディスパッチ (残・latent) | 以下は生成 desktop C に raise が残る (hot path 外・未検証)。**poly-Array `opts.index(x)`** (config enum 変更, config_dialog.rb:348 — poly-Array#index gap)、**poly `setbyte`** (msgpack/char build)、**poly `Integer#to_s(16)`** (hex 整形)、**`rtc.write_time`** (clock 設定保存, Linux で rtc nil/poly=別軸)。sweep 手法=生成C の `sp_nomethod_msg_args("X"` 全列挙 | 未対応 (次バッチ候補)。index は poly-Array dispatch、setbyte/to_s は poly-String/Int、write_time は nil-guard | T4-5 |

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
| `sp_io.c` の File/Dir が生 POSIX (`fopen`/`opendir`/`stat`/`fdopen`/`isatty`/`ioctl`) 直叩きで、I/O バックエンドの差し替えフックが無い (`sp_ctx` のフックはメモリ T3-2 のみ)。fmrb HAL の仮想パス解決を通らず、Linux は `/app` ENOENT、**ESP32 は POSIX FS 自体が無く FS 依存機能が全滅** | **汎用フック候補 (`sp_mem_*` と同型)**: `sp_ctx` config に I/O バックエンド関数ポインタ (open/read/write/close/seek/stat + opendir/readdir/closedir) を追加、`sp_io` の POSIX 呼びをフック経由に (default は POSIX 直で byte 同一)。fmruby 側は `fmrb_hal_file_*` を注入。desktop の launcher/icon/file manager が依存 | T4-5 / `esp32_host_deps_sweep.md` / `phase5.md`(8) |

---

## C. 恒久的な設計課題 (大改修・要判断)

- **typed symbol-hash 型が無い** (`SYM_INT`/`SYM_STR` 等)。symbol キー hash は常に
  poly 値 (`win[:x]`, `msg[:data]` 等)。性能 (AOT 利点減) と互換の鍵。
  types/analyze/codegen/runtime 全体に及ぶ Phase 1/3 級の大改修。設計評価は
  `phase1_report.md` (sp_StrIntHash がテンプレ、3 層 3-5 日規模、Phase 3 実装推奨)。
  当面は **FFI 境界に poly を持ち込まない** (payload をバイト列のまま渡す) で回避。

- **per-object GC ヘッダ `sp_gc_hdr` が太い (live RSS の主因)**。全 heap object が
  48B/object (64-bit) のヘッダを背負う: `next` + `finalize` + `scan` + `size` +
  flags + `recycle`。T4-5 で desktop の live が mruby 比 **1.9x** (678KB) になった主因
  (`reports/memory_model_findings.md`)。mruby は均一スロットをアリーナから切り出す
  ため object 単位の固定 overhead がほぼ無いのに対し、Spinel は個別確保 + 48B ヘッダ。
  **スリム化案 (中規模・高レバレッジ)**: (1) `finalize`+`scan` の 2 ポインタを
  **per-type ディスクリプタ 1 本**に集約 (mruby 流)。(2) `size` はアロケータから
  引いて per-object から除去 (文字列では既に vestigial)。(3) `recycle` の要否精査。
  → 48B→~24B に半減余地。全 Spinel アプリの RSS を下げる。**32-bit ESP32 では
  ヘッダが半減する (48→24B) ため、実効メリットと優先度は 32-bit 実測後に判断**。
  関連: churn の est_calloc zero-fill 寄与が大なら **raw (非 zero-fill) alloc フック**
  (既出、B-1/事前作業) も併せて。

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
7. **戻り値 `:void` の FFI メソッド (`Log.*` 等) をメソッド末尾で呼ばない**
   (メソッド全体が void 推論される)。末尾に明示 `nil` を置く。値の位置の分岐が
   noreturn/void leaf に達する場合も同様 (B-1 推論 参照)。
8. **文字列返しメソッドを `sprintf`/文字列 API の実引数に直接渡さない**
   (const char* 初期化子で壊れた C)。ローカルに hoist してから渡す (B-1 codegen 参照)。

---

## PR 化の進め方 (メモ)

- fmrb-dev は汎用変更と fmruby 固有変更を分離してコミット済み。**汎用コミットのみ**
  を master ベースの短命ブランチへ cherry-pick して PR。
- A の各コミットは単独で `make` / `make test` / `make bench` 通過を確認済み。
- `Co-Authored-By` トレーラは一部コミット (`5129af95` 等) に付与済み。upstream 提出前に
  rebase で整えられる。
