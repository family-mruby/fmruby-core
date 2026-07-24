# Spinel 上流情報メモ (プレゼン + 公式 limitations + ツール)

fmruby 外の一次情報を読んだ結果の集約。fmruby 実装で参照する用。
**作者向けの制約は `ruby_writing_constraints.md` が正**、fork 修正候補は
`reports/fork_pr_candidates.md` が正。本書はその背景・出典・ツール索引。

## 出典

- **プレゼン「About Spinel」** (y-yagi, Ginza.rb 98, 2026-07-23, Spinel commit `ecac633`):
  https://y-yagi.github.io/presen_spinel/ (Slidev。ソースは github.com/y-yagi/presen_spinel)
- **公式 `docs/limitations.md`** (matz/spinel): AOT の可否カタログ (Fundamental /
  Partial-relaxable / By design / Now-supported)。A 制約の一次情報。
- **公式 README** (matz/spinel): 仕組み・最適化・ツール。
- Spinel は **matz 作**。ランタイム不要のネイティブバイナリを生成 (依存 libc/libm のみ)。

## Spinel とは (要点・確認済み)

- Ruby を **AOT** で C 化 → C コンパイラ (gcc/clang、GNU C 拡張必須) でネイティブバイナリ。
  実行時に Ruby インタプリタ/パーサ/型推論は無い ("it is just C")。
- パイプライン: parse (libprism) → analyze (**全プログラム型推論**) → codegen (C) → cc。
  `require`/`require_relative` はパース時にインライン展開。C 拡張 (.so) 不可。
- **POSIX 前提**。Linux/macOS/*BSD。Windows 非対応の理由 = `pthread` /
  `<sys/mman.h>` (regexp 実行可能バッファ) / GCC・Clang インラインアセンブリの Fiber
  コンテキストスイッチ。→ **ESP32 (非 POSIX) 移植の難度を裏付け** ([[esp32-host-deps]] /
  `esp32_host_deps_sweep.md` と一致)。matz は Windows(MinGW) 対応すら打ち切り WSL 推奨。
- パッケージは `spin` (Cargo 風、`spin.toml`、matz/spin-index)。RubyGems/Bundler 不可。
- Thread は GVL 無しの M:N グリーンスレッド。データ競合は Mutex 等で自衛。

## 我々の内部理解の裏取り (リバースエンジニアリングとの照合)

- **poly = `sp_RbVal`**: 型を絞れない値はタグ付き union。アクセス毎にタグ確認+unbox で
  具体型より遅い。推論は「できる限り poly を避けて具体型に絞る」。→ 我々の poly 理解と一致。
  **poly-String/Array の dispatch gap は limitations に載らない = 意図的制約でなく「バグ」**
  (dispatch arm 欠落)。我々が B (fork 弱点・upstream 候補) に分類したのが正しい。
- **GC**: 公式は「マークアンドスイープ + stop-the-world」の 2 行のみ。我々の調査 (世代別
  young/old、8 回に 1 回 full、write barrier 無しで毎回フルマーク、精密影スタックルート、
  文字列別ヒープ、適応閾値、48B object ヘッダ) は方式一致で**より詳細** (`memory_model_findings.md`)。
- **最適化** (README/プレゼン): 小さい不変クラスはスタック構造体化 (GC 対象外)、ループ不変
  `.length` 巻き上げ、文字列連結チェーンの単一 malloc 化、静的シンボル interning のコンパイル
  時定数化。→ 我々が見た「fib のローカルが mrb_int でスタック」等と一致。
- **性能**: CRuby 4.0.4+YJIT 比で概ね高速、計算が重いほど有利、再帰が多いと YJIT と縮む。
  → 我々の desktop 計測 (compute 支配で Spinel 有利) と一致。

## fmruby に効く limitations (要点。網羅は公式 / 詳細は ruby_writing_constraints A)

- **文字列リテラルはデフォルト frozen (opt-out 不可)**。リテラル直接ミューテートは FrozenError。
- **`defined?(@ivar)` はコンパイル時静的解決**。falsy 値メモ化が黙って壊れる。
- **`Enumerator.new{|y|}` / 外部反復 / `.lazy` は fiber-backed** → ESP32 で動かない。
- ユーザ `#hash`/`#eql?` はハッシュキーで非 dispatch (identity)、`Array#hash` 未対応。
- **埋め込み NUL / バイナリ文字列**: byte-exact core は一致、transform/search は最初の NUL で
  停止 → **バイト列コンテナとして扱う** (我々の binstr 方針と一致)。
- エンコーディングは UTF-8/ASCII-8BIT のみ (i18n の日本語 UTF-8 は可)。
- 整数オーバーフロー: `raise`(既定)/`wrap`/`promote` をコンパイル時選択 (ESP32 32-bit で留意)。
- `Exception#backtrace`/`caller` は現状 `[]` (relaxable、`--line-map` から埋め可) →
  例外時 backtrace 空。fork 起案候補 (`fork_pr_candidates.md` B-2)。

## ツール (Spinel 的 Lint / デバッグ) ★活用候補

一件ずつ compile/runtime で踏む reactive を、**proactive に前倒し**できる。

| ツール | 種別 | 内容 | fmruby での活用 |
|---|---|---|---|
| **`rubocop_spinel`** (外部: gurgeous/rubocop_spinel) | **Lint** | Spinel が未対応の Ruby を flag する RuboCop カスタム cop | **Spinel 化アプリの .rb に CI/pre-build で走らせ、未対応パターンを compile 前に検出**。frozen-literal/defined?/Enumerator.new 等の罠を源で防ぐ |
| **`spinel-doctor`** (vendor 同梱 `bin/`) | health check | `spinel-doctor app.rb`。legs = build / unsupported / unresolved / inference / requires / behavior | spinel:gen 前に desktop/アプリの health を一括診断 |
| **`spinel-reduce`** (vendor 同梱) | 最小再現 | codegen バグの最小再現生成 | fork バグ報告の repro (既に利用) |
| **`spinel-flatten`** (vendor 同梱) | - | require 展開後のフラット化 | - |
| **`spinel-dev` の value bisector** (外部: OriPekelman/spinel-dev) | silent 検出 | CRuby vs Spinel の値を bisect し **silent miscompile を特定** | 今回の `[]=` no-op (アイコン化け) のような**raise しない静かなバグ**にこそ効く。ruby-lsp 型 addon / flamegraph も |
| `--emit-types` / `--emit-rbs` (spinel 本体) | 型内省 | per-position 型を JSON / 推論結果を RBS 出力 | poly 箇所の機械棚卸し (RBS 課題で使用) |

**推奨**: Spinel 化を広げる前に **`rubocop_spinel` + `spinel-doctor` を Spinel 対象 .rb に
かける proactive Lint 工程**を入れると、poly-dispatch や limitations 由来の問題を源で減らせる
(我々の reactive whack-a-mole の緩和)。silent バグは `spinel-dev` の bisector が本命。

### spinel-doctor 実行結果 (2026-07-24, Phase 5 前ゲート)

`SPINEL_DIR=vendor/spinel spinel-doctor <combined>.rb` を生成 combined に実行:
- **kernel: clean** (unsupported/unresolved エラーなし)。
- **desktop: 実エラー 1 件のみ** = `rtc.write_time` の poly 受信 unresolved (line 2239,
  `-> NoMethodError`)。**既知の非致命 latent** (rescue catch 済 / Linux no-op) で、独立ツールが
  「新規クラッシュ源 poly gap 無し」を裏付け (系統的 sweep が完全だった証拠)。ESP32 は実 RTC の
  ため要修正 (Phase 5 前推奨)。
- inference: 多数の「widened to untyped (slow path)」= poly-widen の perf 情報 (エラーでない)。
  **RBS 課題 (Phase 5 後) の対象**。
- 注意: doctor の `build`/`behavior` leg は combined を**単体**でビルド/実行するため、fmruby の
  C シム (`fmrb_spx_*`) 未リンクの undefined reference と CRuby-diff は**偽陽性**。source レベルは
  `--skip build,behavior` か `--only unsupported,unresolved,inference` で診るのが正。
- build leg の実警告 (非偽陽性): `-Wincompatible-pointer-types` 数件 —
  `while (e = dir.read)` の変数 `e` が String↔Exception 衝突 (×2, `scan_*_dir`)、`x || []` の
  空配列型不一致 (×2)、ivar IntArray/PolyArray、SymPolyHash/StrPolyHash。非致命だが潜在型ズレ、
  任意で掃除。

## RBS の活用 (Phase 5 後の課題 — ユーザ決定 2026-07-24)

method param/戻り値の poly-widen を RBS ヒントで concrete に寄せる。symbol-hash 値の poly は
RBS では解けない (typed-symbol-hash 待ち)。**ESP32 実機化 (Phase 5) を優先し、その後に着手**。
検証ループ: `--emit-types` で poly 棚卸し → 上位 widen に `.rbs` → `--rbs` 再ビルド → B gap /
live の変化を計測。詳細は `ruby_writing_constraints.md`「今後の方向性」。
