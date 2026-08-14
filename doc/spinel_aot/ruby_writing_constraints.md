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
| **文字列リテラルはデフォルト frozen (opt-out 不可)**。リテラルの直接ミューテート (`s = "x"; s << y`, `s.upcase!` 等) は FrozenError | `frozen_string_literal: true` 相当が baseline。可変文字列は `+"x"` / `String.new` / 補間 / `dup` から作る (非リテラルの alias ミューテートは動く) | fork limitations.md (By design) |
| **`defined?(@ivar)` はコンパイル時に静的解決**。「プログラム中に `@ivar=` があれば truthy」で受信オブジェクトの実行時状態は見ない | ivar は C 構造体フィールドで per-object の「代入済み」記録が無い。**falsy 値メモ化 `return @x if defined?(@x)` は黙って壊れる** → `@x ||= compute` か明示フラグ (`@x_set`) を使う | fork limitations.md (By design) |
| ユーザ定義 `#hash`/`#eql?` はハッシュキーで dispatch されない (identity 比較)。`Array#hash` も未対応 | キー毎にユーザメソッドを呼ぶ機構が無い。カスタムオブジェクト/配列をキーにすると identity 比較になる | fork limitations.md |
| ブロックを使う (`yield`) メソッドが**自分自身に再帰**すると compile error | block 使用メソッドは呼び出し毎にインライン展開されるため、自己呼び出しが無限インラインになる (loud に落ちる) | fork limitations.md |
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
| 条件式で呼び出しを単項 `!` で否定する (`if !run_path_allowed?(run_path)`)。同じ呼び出しでも `!` が無ければ (`if a && run_path_allowed?(x)`) 正常 | 呼び出しの前置き文 (引数の一時変数宣言と `SP_GC_ROOT`) が `if (...)` の括弧の中に出る壊れた C (gcc: expected expression before 'const')。前行の sprintf と同じ「前置きが式の中に落ちる」系 | 呼び出しをローカルに退避してから否定する (`allowed = run_path_allowed?(x); if !allowed`) | **fork codegen bug**。B-1 に起案登録、修正 pending | 2026-07-29 kernel-only Spinel ビルド |
| 型が確定しない値 (`app[:label]` 等シンボルキー Hash の値、poly-widened な method param) に対し `String#byteslice(start, len)` を呼ぶと、実行時に `undefined method 'byteslice' for an instance of String` で raise (concrete String なら静的 dispatch で動くのに poly 受信で落ちる) | poly dispatch に byteslice arm が無く NoMethod raise の C を生成。**受信側が poly なので結果への `.to_s` では直らない** (`.to_s` は戻り値に付く。教訓: レシーバの poly-dispatch gap は引数/結果の coercion では治らない) | (回避不要になった) | **fork 修正済 `d26c1f9`**。ljust/rjust/center (U-1) と同族の poly-String メソッド gap | T4-5 |
| poly 受信の `String#index/rindex/start_with?/end_with?/split` が同様に raise (byteslice と同族)。`label.rindex("/")` でアプリ起動時に desktop クラッシュ、`f.end_with?(".toml")`/`msg.split("\n")`/`line.index("=")` 等 | poly dispatch に arm 無し | (回避不要になった) | **fork 修正済 `78c7cb20`** | T4-5 |
| **空配列から index で要素を組む** (`a = []; a[idx] = v`) と、末尾超えの `[]=` が silent no-op で**何も保存されない** (`a` は空のまま)。CRuby は nil 埋めで auto-extend | `sp_PolyArray_set` が `i >= len` で no-op (typed array は拡張済、PolyArray だけ outlier だった) | (回避不要になった) | **fork 修正済 `b24b1956`** (runtime)。launcher のアイコンが文字化けしていた根因 | T4-5 |
| poly 受信の `Array#index/find_index(x)` が raise (`opts = s[:options]; opts.index(cur)`)。config enum 変更で desktop クラッシュ | poly dispatch に index arm 無し (include?/rindex は有) | (回避不要になった) | **fork 修正済 `dea18671`** | T4-5 |
| `ch = nil; if …; ch = "\x00"; ch.setbyte(…)` が poly `String#setbyte` で raise。**nil-init が ch を poly 化**している | poly-String setbyte 未 dispatch | **具体型ローカルを使う**: `c = "\x00"; c.setbyte(0, v); ch = c` (c は直接代入で concrete)。setbyte は mutation なので Ruby 側で固定するのが正 | (Ruby 修正が正。fork の poly-setbyte-mutation は不採用) | T4-5 |

| **同じプログラムを 2 回目に起動する**と、エントリ実行中の GC で SEGV (エディタの再オープンで発覚)。エントリで大きな割り当てをすると必ず踏む | 生成 TU の定数・クラス ivar・オブジェクトプールがプロセスグローバルな static で、前インスタンスのポインタを保持したまま。エントリは定数をソース順に再代入するが、GC の globals-mark フックはその前に登録される | (回避不要になった。回避が必要だった間は「エントリで大きな割り当てをしない」= 表を on_create 等へ遅延) | **fork 修正済み `c7de66c`**: `sp_reset_tu_statics()` をエントリ冒頭 (SP_MULTI_CTX のみ) で呼び、mark 対象の全スロットとプールを 0 クリアする。**後日 `cafe6595` (`--persistent-statics`) で、繰り返し呼ぶライブラリ用途向けにこの reset を初回だけにする opt-out を追加**(上の推奨記法・C 節を参照) | doc/spinel_aot/report/stale_statics.md |

**poly-dispatch gap の系統的洗い出し法** (再発防止): 生成 C を `grep -oE 'sp_nomethod_msg_args\("[a-z_?!]+"' <combined>.c | sort | uniq -c` で列挙すると、**そのアプリで実際に poly-dispatch に落ちるメソッド全部**が一覧化できる (理論上の全 String メソッドでなく実 gap)。concrete で動くのに poly で欠落しているものが判る。教訓: **レシーバの poly-dispatch gap は引数/結果の `.to_s` では治らない** (再掲)。silent 誤動作 (raise しない `[]=` no-op 等) は raise 一覧に出ないので、症状 (文字化け等) からも疑う。

## C. fmruby 固有の推奨記法 (dual-safe)

| 指針 | 理由 | 出典 |
|---|---|---|
| nil や型が確定しない値になりうる結果 (`String#byteslice`、`FmrbI18n.t`、型未確定の変数) を、文字列連結・`sprintf`・`File.open` など具体型を必須とする箇所で使う直前に `.to_s` / `.to_i` で固定する | `byteslice` は範囲外で nil を返す等、Ruby でも nilable。Spinel は具体型を要求する箇所で型不一致になる。`.to_s` 追加は mruby でも同義で dual-safe | T4-3 (48eba26, `.to_s`×4) / phase0_findings |
| FFI 境界で `msg[:data]` 等を、シンボルキー Hash 経由でなく **型の確定した String** で渡す | シンボルをキーにした Hash の値は常に型が確定しない値になり、具体型を要求する箇所で詰まるため | phase0_findings |
| `$stdout` 等のグローバル変数を単一クラス (例 ShellOut) に固定する | Spinel では静的型が付くため、複数の型が混ざって型が確定しない値になり推論が悪化するのを防ぐ | phase4.md 落とし穴 |
| **ESP32 向けアプリでは `Enumerator.new { \|y\| ... }` / 外部反復 (`.next`/`.peek`) / `.lazy` を避ける** | これらは fiber-backed で、Spinel の fiber は POSIX (mmap/ucontext/asm) 前提。**ESP32 に fiber backend が無く動かない** (Linux では動くので気付きにくい)。内部反復 (`.each`/`.map`/`.select`) は fiber 不使用で可 | fork limitations.md (Partial) / `esp32_host_deps_sweep.md` |
| **entry を繰り返し呼ぶ (ライブラリ用途) で前処理結果を持ち越したいときは `--persistent-statics` (生成配線) + 重いオブジェクトを永続グローバルにキャッシュ**する。キャッシュキーは**そのオブジェクト自身** (`$c.nil? \|\| $c.size != n`) にし、別の Integer グローバル (`$cached_n`) をキーにしない | `--persistent-statics` は entry 冒頭の `sp_reset_tu_statics()` を**インスタンス初回だけ**にする (既定は毎 entry = 状態が消える)。reset が消すのは**オブジェクト(ポインタ)グローバルだけ**で **Integer グローバルは消さない**(heap を指さないため)。→ 別 int をキーにすると、インスタンス再生成時に本体 `$c` は NULL に戻るのに int キーは前回値が居残り、「キー一致なのに本体 NULL」で再構築を skip → NULL 参照(状態遷移の詳細は `stateful_library_entry.md` の実例節)。**シングルトン (1 TU=1 インスタンス) 前提でのみ安全** | E6 (`cafe6595`) / `stateful_library_entry.md` / `impl_plan_stateful_library_entry.md` |
| (随時追記) | | |

## メンテナンス

- T4-3 で確定した各回避は、**Ruby の正当な曖昧さ (C 相当へ) か fork の推論弱点 (B) か**を
  判定して該当セクションへ移す。fork 弱点は `reports/fork_pr_candidates.md` にも起案登録。
- 恒久制約 (A/C) が増えたら CLAUDE.md / アプリ作成ドキュメントからここへ導線を張る。

### 今後の方向性: RBS で poly を減らす (未着手・計画)

poly (型未確定値) は B の gap 群と live overhead の根本原因。Spinel は **RBS を型推論の
ヒントとして受け取れる** (公式 internals: `--rbs DIR` で seed、`--emit-rbs` で推論結果を
書き出し、`--emit-types` で per-position 型を JSON 出力)。強制ではなくヒントで、矛盾すれば
無視される (型安全)。

fmruby での活用案:
- **method param の poly-widen を RBS で concrete に寄せる**。呼び出し側の型ばらつきで poly
  化するパラメータに、RBS で意図した型を与えて推論を安定させる。
- `--emit-types` を使って **desktop の生成 C で poly に落ちている箇所を機械的に棚卸し**し、
  改善対象を可視化する (B の gap を減らす・live を減らす両方に効く)。
- ただし **symbol-hash 値の poly は RBS でも解けない** (typed symbol-hash が無い限り常に
  poly。`fork_pr_candidates.md` C 参照)。RBS が効くのは param/戻り値の widen 側。
- **位置づけ: Phase 5 完了後の課題 (ユーザ決定 2026-07-24)**。ESP32 実機化を優先し、
  RBS 検証ループ (spinel:gen に `--rbs`/`--emit-types` を通す配線) はその後に着手する。
  詳細な外部情報とツールは `spinel_upstream_notes.md` 参照。

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
- **A (作者制約, JA1 で発見 2026-08-11)**: **文字列キー Hash を引数で渡すときは
  波括弧を明示する**。`FmrbI18n.add("en" => {...}, "ja" => {...})` と波括弧無しで
  書くと Spinel はキーワード引数と解釈し SymPolyHash を渡す (生成 C の
  `sp_..._s_add(sp_SymPolyHash *)` で判別できる)。実害: STRINGS["en"] が引けず
  全キーが key.to_s へフォールバックし、画面に `m_file` 等の生キーが出た。
  `add({...})` で解決。**system_desktop/i18n.rb が同じ書き方**なので desktop
  Spinel 化時に同じ手当てが必要 (doc/editor_ja/report/ja1.md)。
