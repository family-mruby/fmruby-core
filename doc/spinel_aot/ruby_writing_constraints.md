# Spinel 向け Ruby 記述制約 (アプリ作者向け契約)

fmruby のアプリ (kernel / desktop / 各 app) を **Spinel エンジンでコンパイル**する
場合に、Ruby ソースが従うべき記述制約を集約した単一の契約ドキュメント。
知見が phase0_findings / phase1 / phase2 / fork `docs/limitations.md` に散らばるのを
防ぐため、**作者向けの制約はここを唯一の正**とし、各 phase レポートはここへリンクする
(重複記述しない)。

## 使い方 / 大原則

- **dual-build 安全が絶対条件**: すべての回避は mruby ビルドでも従来どおり同一に動く
  こと。Spinel 専用の分岐や `#ifdef` 的な書き分けは避け、両エンジンで同義な書き方に
  寄せる。
- **分類を必ず付ける**。恒久制約と暫定回避を混同しない (下記 A/B/C)。
  - **A. 原理的制約 (恒久)**: whole-program AOT の性質上、fork を直しても消えない。
    アプリは恒久的にこの書き方を守る。一次情報は fork `docs/limitations.md`
    (Fundamental / By design)。ここには fmruby アプリが実際に当たるものだけ要約する。
  - **B. 現 fork の弱点による暫定回避 (fork-fix pending)**: Ruby は正当だが今の
    コンパイラが扱えず、Ruby 側で回避中。**いずれ fork 修正で消えるべき**もの。
    作者への恒久制約ではない。fork candidate / commit を必ず紐づけ、消込みを追う。
  - **C. fmruby 固有の推奨記法**: 原理でも fork バグでもないが、推論を安定させ、
    型が確定しない値の発生を避けるための設計上の寄せ方 (dual-safe)。
- **B は「負債」として可視化する**。B が多いほど「実ソースのまま通る」の主張は
  実質後退している。B の件数と消込み状況を phase レポートに要約する。

## 用語: 「型が確定しない値」

Spinel は各値の型をコンパイル時に静的に推論する。推論しきれない値は
**「型が確定しない値」(動的型のまま。実装上は C の `sp_RbVal` 型)** として扱われる。
シンボルをキーにした Hash の値 (例 `h[:a]`) が典型。この値は表示・算術・比較などでは
概ね正しく流れるが、**整数や文字列など具体的な型を必須とする組み込みメソッドに渡すと
型不一致でコンパイルエラーになりやすい**。本ドキュメントでは以降この語で統一する
(旧称 "poly")。

## A. 原理的制約 (恒久) — fork `docs/limitations.md` 参照

アプリが実際に当たる代表のみ。詳細と網羅は fork `docs/limitations.md` の
Fundamental / By design を正とする。

| 制約 | 要点 | 出典 |
|---|---|---|
| `eval` / 文字列 `class_eval` 等 | 実行時パーサが無いので不可 (ブロック形は可) | fork limitations.md |
| `method_missing` / 動的 `define_method` / 特異メソッド | 直接 C 呼び出しのため動的ディスパッチ不可 | fork limitations.md |
| `ObjectSpace` / `TracePoint` / `binding` オブジェクト化 | 実行時メタ機構が無い | fork limitations.md |
| (随時追記) | | |

## B. 現 fork の弱点による暫定回避 (fork-fix pending)

**これらは恒久制約ではない。fork 修正で消すのが目標。** 各行に fork candidate を紐づける。

各行の「症状」は、Ruby でどう書くと何が起きるかを文で説明する。

| 症状 (Ruby でどう書くと何が起きるか) | 生成 C の問題 | dual-safe 回避 | fork candidate / status | 出典 |
|---|---|---|---|---|
| 型が確定しない値 (シンボルキー Hash の値など) を、整数を必須とする組み込みメソッドに渡している | 整数が来る前提の C が生成され、型不一致でコンパイルエラー | 渡す前に明示的に `.to_i` する / 型の確定した経路から渡す | FIX-1 系 (一部修正済) | phase0_findings |
| 戻り値の FFI 型が `:void` のメソッド (`Log.debug`/`info`/`warn`/`error`) を**メソッド末尾で呼ぶ**と、そのメソッド全体が「値を返さない (void)」と推論される | 戻り値が void と推論され、戻り値を使う側と食い違う | base の `Log.*` を各々末尾 `nil` で返す | fork 候補: B-1 推論。48eba26 で回避済 | T4-3 |
| 値の位置の分岐が noreturn (`FmrbApp.reboot`) か void leaf に到達するメソッド5つ (`on_control` / `cfg_do_save` / `handle_launcher_click` / `fmgr_paste_file` / `fmgr_delete_file`) が void と推論される | 同上 | メソッド末尾に明示的に `nil` を置く | 上と同一の fork 候補。48eba26 で回避済 | T4-3 |
| 文字列を返すメソッド呼び出しを `sprintf` 等の文字列 API の実引数に**直接**渡す (`sprintf(FmrbI18n.t(:x), n)`) | 呼び出しの GC-root 展開が `const char *` の初期化子内に落ち、壊れた C を出力 (gcc: expected expression) | 一旦ローカルに退避してから渡す (`fmt = FmrbI18n.t(:x).to_s; sprintf(fmt, n)`) | **fork codegen bug**。最小再現→修正 pending。`str_run_clear` の `expected expression before sp_RbVal` も同一根因の可能性 (repro で確認) | T4-3 |
| 型が確定しない値 (`app[:label]` 等シンボルキー Hash の値、poly-widened な method param) に対し `String#byteslice(start, len)` を呼ぶと、実行時に `undefined method 'byteslice' for an instance of String` で raise (concrete String なら静的 dispatch で動くのに poly 受信で落ちる) | poly dispatch に byteslice arm が無く NoMethod raise の C を生成。**受信側が poly なので結果への `.to_s` では直らない** (`.to_s` は戻り値に付く。教訓: レシーバの poly-dispatch gap は引数/結果の coercion では治らない) | (回避不要になった) | **fork 修正済 `d26c1f9`**。ljust/rjust/center (U-1) と同族の poly-String メソッド gap | T4-5 |
| poly 受信の `String#index/rindex/start_with?/end_with?/split` が同様に raise (byteslice と同族)。`label.rindex("/")` でアプリ起動時に desktop クラッシュ、`f.end_with?(".toml")`/`msg.split("\n")`/`line.index("=")` 等 | poly dispatch に arm 無し | (回避不要になった) | **fork 修正済 `78c7cb20`** | T4-5 |
| **空配列から index で要素を組む** (`a = []; a[idx] = v`) と、末尾超えの `[]=` が silent no-op で**何も保存されない** (`a` は空のまま)。CRuby は nil 埋めで auto-extend | `sp_PolyArray_set` が `i >= len` で no-op (typed array は拡張済、PolyArray だけ outlier だった) | (回避不要になった) | **fork 修正済 `b24b1956`** (runtime)。launcher のアイコンが文字化けしていた根因 | T4-5 |

**poly-dispatch gap の系統的洗い出し法** (再発防止): 生成 C を `grep -oE 'sp_nomethod_msg_args\("[a-z_?!]+"' <combined>.c | sort | uniq -c` で列挙すると、**そのアプリで実際に poly-dispatch に落ちるメソッド全部**が一覧化できる (理論上の全 String メソッドでなく実 gap)。concrete で動くのに poly で欠落しているものが判る。教訓: **レシーバの poly-dispatch gap は引数/結果の `.to_s` では治らない** (再掲)。silent 誤動作 (raise しない `[]=` no-op 等) は raise 一覧に出ないので、症状 (文字化け等) からも疑う。

## C. fmruby 固有の推奨記法 (dual-safe)

| 指針 | 理由 | 出典 |
|---|---|---|
| nil や型が確定しない値になりうる結果 (`String#byteslice`、`FmrbI18n.t`、型未確定の変数) を、文字列連結・`sprintf`・`File.open` など具体型を必須とする箇所で使う直前に `.to_s` / `.to_i` で固定する | `byteslice` は範囲外で nil を返す等、Ruby でも nilable。Spinel は具体型を要求する箇所で型不一致になる。`.to_s` 追加は mruby でも同義で dual-safe | T4-3 (48eba26, `.to_s`×4) / phase0_findings |
| FFI 境界で `msg[:data]` 等を、シンボルキー Hash 経由でなく **型の確定した String** で渡す | シンボルをキーにした Hash の値は常に型が確定しない値になり、具体型を要求する箇所で詰まるため | phase0_findings |
| `$stdout` 等のグローバル変数を単一クラス (例 ShellOut) に固定する | Spinel では静的型が付くため、複数の型が混ざって型が確定しない値になり推論が悪化するのを防ぐ | phase4.md 落とし穴 |
| (随時追記) | | |

## メンテナンス

- T4-3 で確定した各回避は、**Ruby の正当な曖昧さ (C 相当へ) か fork の推論弱点 (B) か**を
  判定して該当セクションへ移す。fork 弱点は `reports/fork_pr_candidates.md` にも起案登録。
- 恒久制約 (A/C) が増えたら CLAUDE.md / アプリ作成ドキュメントからここへ導線を張る。

### T4-3 (commit 48eba26) の分類確定結果

生成 C を gcc-clean にした 11→0 の内訳。**「実ソースのまま」の後退度 = B の件数**:

- **C (推奨記法・Ruby 正当, `.to_s`×4)**: `fmrb-i18n.rb` の `byteslice(...).to_s`、
  `launcher.rb` の `File.open(icon_file.to_s, ...)`、`storage_dialog.rb` の
  `FmrbI18n.t(:x).to_s`。nilable/型未確定を具体型に固定。dual-safe で恒常的に妥当。
- **base 層の欠落 (Ruby 制約ではない)**: 当初 B に暫定登録した
  「`draw_launcher_cells` の三項の型不一致」は Ruby の問題ではなく、**T4-2 で先送りした
  base の `FmrbGfx.rgb_to_332` / `hsv_to_rgb` 未実装**が原因 (定数 `LAUNCHER_ICON_SEL`
  が Integer に確定せず三項が不統一)。base に両メソッドを実装 (48eba26) して解決。
  → 作者制約ではないので B/C 表からは除外。
- **B (fork 推論弱点, fork-fix pending)**: void 推論 (`Log.*` 末尾 nil + 末尾 nil×5)。
  `fork_pr_candidates.md` B-1 推論 に起案登録済。
- **B (fork codegen bug, repro pending)**: sprintf への文字列返し直渡し (hoist で回避)。
  `fork_pr_candidates.md` B-1 codegen に起案登録済。`str_run_clear` も同根因疑い。
