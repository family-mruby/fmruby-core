# 実装計画書: Spinel 永続ライブラリ entry(B-lite)

> **完了 (2026-08-14)。結果は `doc/mic_spectrum/report/track_a.md` の E6 節。**
> Stage 0-4 全て実施。`--persistent-statics` を vendor/spinel に追加し、
> 前処理 13.7ms / 16.4ms が **ほぼ 0** に。マイクアプリの 1 フレームは
> **18-20ms → 5ms** (`spinel_q15`)、**35ms → 16ms** (`spinel`)。
> 実機でメモリ安定・正しさ不変・kernel/editor 回帰なしを確認済み。
> vendor/spinel は `cafe6595` (fmrb-dev) としてコミットし、`SPINEL_PIN` /
> `IMPORT_INFO` も更新済み。**残: fork の push だけ**
> (未 push なので新規 clone からの `rake spinel:setup` はまだ通らない)。

別セッションが実装するための手順書。検討と根拠は同ディレクトリ
`stateful_library_entry.md`。目的: Spinel を gem として**繰り返し呼んでも速い**
(状態=前処理テーブルを呼び出しをまたいで保持)ようにする。まず FFT で実証。

- 勝手に git 操作しない(commit/push は明示指示)。コメント英語・会話日本語・絵文字なし。
- **比較は同一コミットのビルド同士**、report に環境とコミットを明記。
- vendor/spinel(自前コンパイラ)に触る作業を含む。**実装者の spinel セッションで行い**、
  fmruby-core 側は import + 配線 + 計測。

## 何を変えるか(2つ揃って初めて効く)

1. **reset を per-entry → per-instance に**(コンパイラ)。生成 entry 冒頭の
   `sp_reset_tu_statics()` を「インスタンス生成後の初回 1 回だけ」にする。civ_
   (クラス ivar キャッシュ)は**既に GC mark 済み**なので、reset を飛ばせば次回も生存。
2. **entry が core オブジェクトを永続グローバルで再利用**(fft_spinel.rb)。現状は
   毎回 `FftCore.new(n)` して initialize でテーブルを作り直す。reset を飛ばしても
   **新しいオブジェクトを作れば意味がない**。→ 永続グローバルにキャッシュし、初回だけ
   生成する。reset を飛ばしたのでグローバルが生存する。

**この 1+2 で前処理が初回のみになる**(spinel_q15 の所要 88% が前処理 → ~2.34ms が
実 FFT 分 ~0.4ms 前後へ、c_q15 に肉薄の見込み)。fft_core.rb(:ruby と共有)は
**触らない**(キャッシュは entry ラッパ fft_spinel.rb 側)ので公平性の前提は保たれる。

## 前提事実(調査済)

- reset の実体: `main/prebuild_scripts/spinel/gen/fft_spinel.c` の
  `sp_reset_tu_statics()`(civ_* と `sp_*_pool_head/count` を NULL/0 に)。entry 冒頭で
  `sp_tu_ctx_init(); sp_reset_tu_statics();` の順に呼ばれる(SP_MULTI_CTX)。
- civ_ は TU の mark 関数で `if (civ_...) sp_gc_mark(...)` 済み(非 NULL の間生存)。
- インスタンスは run() 間で生きている(`fmrb_fft_spinel.c` の begin 一度 / run 多数 /
  end)。gem 側 `fmrb-fft.rb` の spinel_open は参照カウントで begin/end。
- マルチインスタンス設計は `components/fmrb_spinel_rt/spinel_rt/sp_ctx.h`(共有 lib
  グローバル→ctx フィールド)。per-TU static はそこに含めず reset で処理してきた。
- Spinel コンパイラ = `vendor/spinel`(`rake spinel:setup`、SPINEL_PIN 固定)。runtime
  スナップショットは `components/fmrb_spinel_rt/import_from_fork.rb` で取り込み、
  `SPINEL_PIN` と `spinel_rt/IMPORT_INFO` を揃える。

## Stage 0 — スパイク(コンパイラに触らず de-risk)★最初にこれ

目的: 「reset を飛ばし + 永続グローバルで再利用」で本当にテーブルが持続し、前処理が
消え、**メモリが安定**するかを、生成物の手編集だけで確認する。

1. `main/prebuild_scripts/spinel/fft_spinel.rb` を編集: core を永続グローバルで再利用。
   ```ruby
   # 例(mode/n をキーに初回だけ生成)
   if $fft_core_n != n || $fft_core_mode != mode
     $fft_core = (mode == 1 ? FftCoreQ15.new(n) : FftCore.new(n))
     $fft_core_n = n; $fft_core_mode = mode
   end
   $fft_core.load(bytes)
   ...
   ```
   ※ Spinel のグローバル変数(`$x`)が civ_ 同様に GC mark され reset 対象かは要確認。
   もしグローバルが使えなければ、定数/クラス変数、あるいは「FftCore を毎回 new するが
   initialize が civ_ を『既にあれば作らない』」形にする(civ_ は class 単位 static なので
   `civ_FftCore_window` が非 NULL なら skip、という初期化ガードでも同じ効果)。
   → **どの持ち方が reset を飛ばした時に持続するか**をスパイクで見極める。
2. `rake spinel:gen` で生成 → **生成された `gen/fft_spinel.c` の
   `sp_reset_tu_statics();` 呼び出しを手でコメントアウト**(gen は gitignore。次の
   spinel:gen で戻るので一時的)。初回の zero 化が要るなら「初回だけ呼ぶ」に手編集。
3. `rake build:esp32 && FLASH_BAUD=115200 rake flash`。fft_bench / mic_spectrum で
   spinel / spinel_q15 の μs を計測。**前処理が消えて大幅短縮**するか。
4. **メモリ安定性**: 1000 回程度 run して内蔵/プールが増え続けないか(`M1|`/`ps` ログ、
   `fmrb_task`/`fmrb_app` の周期ダンプ)。transient(毎回の bytes/mag String)は GC で
   回収され、キャッシュした core だけ残ること。
5. 正しさ: ピーク bin 一致 + 振幅が従来と同じ(キャッシュしても結果は不変)。

**Stage 0 が通れば本命確定**。ダメなら(グローバルが持続しない/メモリ増加)原因を
report に残し、B-full かコンパイラ側の別手を検討。

## Stage 1 — コンパイラ: reset を per-instance に(vendor/spinel)

1. opt-in フラグを追加(例 `--persistent-statics` / `--stateful-entry`)。指定時、entry
   プロローグの `sp_reset_tu_statics()` を**インスタンス初回だけ**にする:
   ```c
   sp_tu_ctx_init();
   if (!SP_CTX_STATICS_INITED()) { sp_reset_tu_statics(); SP_CTX_MARK_STATICS_INITED(); }
   ```
   - フラグは**per-instance**(ctx)に置く。`sp_ctx` に `int statics_inited` を 1 本追加し、
     `sp_ctx.h` のマクロ(既存の ctx-field パターン)で公開。インスタンス生成
     (`sp_instance_create`)で 0 初期化。
   - `sp_tu_ctx_init()` は従来どおり毎 entry 呼ぶ(現インスタンスの ctx を TU に束ねる)。
2. runtime を fmrb_fft へ取り込む: `components/fmrb_spinel_rt/import_from_fork.rb` で
   再スナップショット、`SPINEL_PIN`(repo/branch/commit)と `spinel_rt/IMPORT_INFO` を
   更新(両者一致必須、`rake spinel:gen` が乖離を警告)。
3. **run-once プログラム(kernel/desktop/editor)には付けない** → 従来どおり毎 entry
   reset(entry を 1 回しか呼ばないので無影響)。**FFT だけ opt-in**。

## Stage 2 — entry の永続キャッシュ(fft_spinel.rb)

Stage 0 で確定した持ち方(グローバル/定数/初期化ガード)を正式に実装。n/mode 変更時は
作り直す。fft_core.rb は不変(共有ソースの公平性を維持)。

## Stage 3 — 配線(rakelib)

`rakelib/spinel.rake` の fft_spinel 生成コマンドに Stage 1 のフラグを付ける
(`spinel --no-main --entry fmrb_fft_spinel_entry --persistent-statics ...`)。
kernel/desktop/editor の生成には付けない。

## Stage 4 — 計測・検証・記録

- **before/after**: 同一コミット近傍で spinel / spinel_q15 の μs(前処理込み total は
  受け皿 C の `fmrb_fft_spinel_last_total_us` で計る。内側だけだと見落とす=E5 の教訓)。
  期待: spinel_q15 が c_q15(436us)に肉薄。
- **メモリ安定**(Stage 0 の再確認、実機で長時間)。
- **公平性の維持**: 非永続(毎回 reset)版でも計れるようにしておく(showcase の
  「ライブラリ呼び出しは前処理を払う」知見を残す)。フラグ有無の 2 ビルド、または
  entry を 2 種。
- **回帰**: kernel/desktop/editor が従来どおり起動・動作(reset セマンティクス不変)。
- 記録: `doc/mic_spectrum/report/track_a.md`(E6 として)+ `stateful_library_entry.md` に
  結果、`embedded_constraints.md` 7.1 に「永続 entry で前処理税を消せる」を追記。

## 留意 / リスク

- **シングルトン(1 TU=1 インスタンス)前提でのみ安全**。同一 TU を複数インスタンスが
  使う構成では file-scope civ_ が混線する。B-lite はその構成に使わない(将来必要なら
  B-full=civ_ を ctx 化)。FFT は 1 インスタンス・1 タスク([[project_fft_engine_bench]]
  のシングルトン制約)なので該当。
- transient の GC 回収が効かないとプールが増え続ける → Stage 0/4 で必ず長時間確認。
- vendor/spinel 変更 → SPINEL_PIN と IMPORT_INFO の整合を忘れると `rake spinel:gen` が
  古い runtime とズレる。
- グローバル/定数が reset を飛ばした時に本当に持続するか(Spinel の mark 対象か)を
  Stage 0 で必ず確定してから Stage 1 へ。

## 参考(コード位置)
- entry/reset: `main/prebuild_scripts/spinel/gen/fft_spinel.c`(`sp_reset_tu_statics`、
  entry プロローグ)、ソース `main/prebuild_scripts/spinel/fft_spinel.rb`
- 受け皿(インスタンス lifecycle): `main/kernel/fmrb_fft_spinel.c`
  (begin/run/end、`fmrb_fft_spinel_last_total_us`)
- ctx: `components/fmrb_spinel_rt/spinel_rt/sp_ctx.h`、import
  `components/fmrb_spinel_rt/import_from_fork.rb` / `SPINEL_PIN`
- 生成配線: `rakelib/spinel.rake`、`main/CMakeLists.txt`(FMRB_FFT_SPINEL)
- gem: `lib/add/picoruby-fmrb-fft/mrblib/fmrb-fft.rb`(spinel_open/close 参照カウント)
- 共有アルゴリズム(**触らない**): `lib/add/picoruby-fmrb-fft/mrblib/fft_core.rb`
