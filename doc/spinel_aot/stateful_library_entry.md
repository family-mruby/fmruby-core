# 検討: Spinel を「状態を保つライブラリ」として呼べるようにする(B案)

> **B-lite は実装済み (2026-08-14)**。手順は `impl_plan_stateful_library_entry.md`、
> 結果は `doc/mic_spectrum/report/track_a.md` の E6 節。本書の見立て
> (「civ_ は既に mark 済みなので reset を飛ばすだけでよい」) は当たっていた。
> 見落としていた点が 1 つ: **reset は整数グローバルをクリアしない**ので、
> キャッシュのキーはキャッシュしたオブジェクト自身にする必要がある。

## 実装者が気にすること(要点。内部の仕組みは後述)

**(a) gem を使うだけの普通のアプリ実装者 → 何も気にしなくてよい。**
`Fmrb::Fft.new(backend: :spinel)` して `forward` するだけ。キャッシュも reset も
gem/entry が隠す。以下は関係ない。

**(b) Spinel ライブラリ(entry `.rb`)を新しく書く人 → 気にするのはこの 5 つだけ:**

1. **前処理が重いルーチンのときだけ**永続化を考える(安いなら毎回作り直しで十分)。
2. 重い状態は**永続グローバル(`$x`)にオブジェクトを入れて再利用**。作り直しは初回だけ。
3. キャッシュ判定は**そのオブジェクト自身**で(`$c.nil? || $c.size != n`)。
   **別の Integer 変数をキーにしない**(← ハマる唯一の落とし穴。理由は下の「内部」)。
4. **1 変数に 2 クラスを入れない**。クラスごとに別グローバル(型が untyped に広がる)。
5. キャッシュは **entry ラッパ側だけ**に書く。共有アルゴリズム(`:ruby` と同一ソース)は
   触らない。

生成配線(`--persistent-statics` を付ける)は `rakelib/spinel.rake` 側の一度きりの
作業で、entry を書くたびに気にする話ではない。gv_/reset/GC などの C 内部は**理解の
ためだけ**で、コードでは触らない(全部自動生成)。

---

**当初は設計検討の記録**(2026-08-14)。動機: Spinel を gem として繰り返し
呼ぶ用途では、繰り返しでも速くあってほしい(単発なら app ごと Spinel にすればよい)。
現状は entry 呼び出しごとに状態が消え、前処理を毎回払う(E5 の知見:
spinel_q15 の所要の 88% が前処理)。→ [[project_fft_engine_bench]]、
`doc/mic_spectrum/report/track_a.md` E5、`embedded_constraints.md` 7.1。

## 何が状態を消しているか(調査結果)

- 生成 entry の先頭で **`sp_reset_tu_statics()`**(SP_MULTI_CTX 時)が、クラスの
  ivar キャッシュ(`civ_FftCore_window/cos/sin/rev/re/im/samples` など)と
  オブジェクトプール(`sp_*_pool_head/count`)を**全部 NULL に戻す**。
  = 1 entry 呼び出し = プログラム 1 回実行、という契約。
- **重要**: `civ_` は **既に GC ルートとして mark されている**
  (`fft_spinel.c` の TU mark 関数に `if (civ_...) sp_gc_mark(...)`)。非 NULL の間は
  生き残る。インスタンス(GC ヒープ)は run() 間で生存している。
- つまり **civ_ を消しているのは reset だけ**。reset しなければ、テーブルは次の
  entry でも生きている。**追加の GC 配線は不要**。
- マルチインスタンス設計(`sp_ctx.h`)は「共有 lib 側グローバル → ctx フィールド」で
  解決済み。per-TU static(civ_)は relocate せず reset で処理する方針だった。理由は
  「同一 TU を複数インスタンスが使うと file-scope static が混線するから毎回 clean に
  する」。**FFT ライブラリは 1 インスタンス・1 タスクのシングルトンなので、この混線は
  起きない**。

## B-lite(推奨): reset を per-entry → per-instance にする

- opt-in の「永続 entry」を用意し、`sp_reset_tu_statics()` を**インスタンス生成時 1 回**
  だけ実行(以降の entry では飛ばす)。ctx に `statics_initialized` フラグ 1 本で足りる。
- civ_ は既に mark されているので、**reset を飛ばすだけで @window/@cos などが次回も
  生存** → **前処理(Math.cos/sin ×1024)が初回のみ**になる。
- 効果(E5 の数字から): spinel_q15 は前処理が所要の 88% なので、**~2.34ms → 実 FFT
  分の ~0.4ms 前後**まで落ちる見込み(c_q15 436us に肉薄)。繰り返し呼ぶ gem 用途で
  桁が変わる。
- 安全性: **シングルトン(同一 TU を 1 インスタンスだけが使う)前提でのみ有効**。
  複数インスタンスが同一 TU を使う構成では file-scope static が混線するので使わない。
  run-once プログラム(kernel/desktop/editor)は entry を 1 回しか呼ばないので影響なし
  (むしろ意味が変わらない)。
- 変更範囲: **vendor/spinel(コンパイラ)の codegen 変更**。entry プロローグの
  reset を「初回だけ」に変える + ctx にフラグ。runtime 側は既存の civ_ mark をそのまま
  使えるので小さい。プール永続化も同時に効く(再 alloc が減る利点)。
- Ruby 意味論: FftCore オブジェクトが呼び出しをまたいで生き続ける = **普通の Ruby
  オブジェクトと同じ**挙動になる(gem として自然)。

## 実例(最小形) — 繰り返し呼ぶ entry の書き方

実装は `lib/add/picoruby-fmrb-fft/spinel/fft_spinel.rb`。核はこれだけ:

```ruby
# entry の top-level = fmrb_fft_spinel_run() 1 回で 1 度実行される本体。
# 入出力は FFI 経由(entry は引数を取れない)。
n     = FftSpx.fmrb_fft_spx_size
iters = FftSpx.fmrb_fft_spx_iters
bytes = FftSpx.fmrb_fft_spx_samples

core = $fft_core                 # 1) 永続グローバルから取り出す
if core.nil? || core.size != n   # 2) キャッシュキーは “オブジェクト自身”
  core = FftCore.new(n)          #    初回だけ重い前処理(window/twiddle/bit反転)
  $fft_core = core
end
core.load(bytes)

t0 = FftSpx.fmrb_fft_spx_micros
core.run(iters)                  # 2 回目以降は変換だけ(前処理を払わない)
us = FftSpx.fmrb_fft_spx_micros - t0

mag = core.magnitudes_bytes
FftSpx.fmrb_fft_spx_output(mag, mag.bytesize, us)
0
```

効かせる 3 つの要点:

1. **永続グローバルにキャッシュ**(`$fft_core`)。`--persistent-statics` で reset が
   インスタンス初回だけになり、グローバルが呼び出しをまたいで生きる。フラグ無しでも
   このコードは正しく動く(毎回 nil → 毎回再構築 = 従来どおり、ただ遅い)。
2. **キャッシュキーはオブジェクト自身にする**(`core.nil? || core.size != n`)。
   サイズを別の Integer グローバル(`$cached_n`)に覚えて `if $cached_n != n` で判定
   しては**いけない**。理由は「reset がいつ走り、何を消すか」にある:

   - **reset(`sp_reset_tu_statics`)が走るタイミング**:
     `--persistent-statics` **無し** = 毎 entry の冒頭(呼び出しごと)。
     **有り** = インスタンス生成後の**初回 entry だけ**。
   - **reset が消すもの**: **オブジェクト(ポインタ)のグローバルだけ**を NULL に戻す
     (`$fft_core` 等。heap を指すので、古い heap を GC が mark して壊すのを防ぐため
     消す)。**Integer のグローバル(`$cached_n`)は消さない**(heap を指さないので
     reset の対象外)。しかも file-scope static は**プロセスに 1 個**で、インスタンスを
     作り直しても値が居残る。
   - **なぜ想定外だったか**: 「reset が走ればそのインスタンスのキャッシュ状態は空に
     戻る」と考え、本体 `$fft_core` と目印 `$cached_n` を対で管理した。ところが reset は
     **本体だけ NULL にして目印(int)は残す**。すると次で両者が食い違う:

     ```
     旧インスタンス: $fft_core = <core(n=512)>,  $cached_n = 512
       └ インスタンス破棄(fmrb_fft_spinel_end)。static は居残る:
            $fft_core = <解放済み heap を指すダングリング>,  $cached_n = 512
       └ 新インスタンス生成 → 初回 entry で reset:
            $fft_core = NULL(消された)          $cached_n = 512(消えない)
     ```
     このとき `if $cached_n(512) != n(512)` は **false** →「キャッシュ有り」と誤判定して
     再構築をスキップ → NULL の `$fft_core` に `.load` → **NULL 参照でクラッシュ**。
     (`--persistent-statics` 無しなら毎 entry reset なので、**2 回目の呼び出しで即**
     同じ NULL 参照になる。)
   - **オブジェクト自身をキーにすれば起きない**: `core.nil?` は本体そのものを見るので、
     reset で本体が消えれば必ず「ミス」になり再構築する。目印と本体が同じ 1 個なので、
     食い違う余地がない(= キャッシュがアトミック)。これが「実装中に踏んだ罠」の中身。
3. **具体クラスごとに別グローバル**。double と Q15 で `$fft_core` / `$fft_q15` を分け、
   分岐の本体も 2 度書く。1 変数に 2 クラスを入れると型推論が untyped に広がり、直接
   native 呼び出しでなくなる(比較の意味が消える)。
4. **キャッシュは entry ラッパにだけ書く**。共有アルゴリズム(`fft_core.rb`、`:ruby` と
   同一ソース)には一切入れない → 公平性が保たれる。

## B-full: per-TU static をインスタンスごとに持つ

- civ_/プールを ctx(またはインスタンスごとの per-TU ブロック)へ移し、
  `ctx->tu_statics->civ_...` で参照。**同一 TU を複数インスタンスが使っても各々が独立**
  に状態を保てる、一般解。マルチインスタンス設計の「最後の 1 カテゴリ」を仕上げる形。
- コスト大: codegen が civ_/プールを ctx 経由に出す + インスタンス init で確保 +
  GC mark を ctx 経由に。gem-アクセラレータ(シングルトン)用途には過剰。
- B-lite を先に入れ、複数インスタンス需要が出たら B-full に拡張、が現実的。

## 価値評価(なぜ B か)

- **B は「Spinel を gem 化する」パターンを一級機能にする**: ホットな Ruby ルーチンを
  native 化し、**状態を保ったまま繰り返し高速に呼べる**(普通の Ruby オブジェクトの
  ように)。per-call 再初期化税が消える。汎用(FFT に限らない)。
- A(テーブルを FFI で C から渡す)は per-app のハックで、`fft_core.rb` の自己完結性
  (:ruby と同一ソース)を壊す。再利用性なし。
- 「app ごと Spinel」は重く、mruby app + ホット Spinel ルーチンの hybrid 柔軟性を失う。
- → **B-lite を推奨**。小さめの vendor/spinel 変更で、gem 用途の価値が跳ね上がる。

## 留意

- vendor/spinel(= kishima の spinel fork)への変更なので、自分たちのコンパイラを
  拡張する形。エンジン方針(Spinel=標準)とも整合([[project_engine_policy]])。
- ベンチの公平性: 永続化は opt-in にし、**同一 Ruby ソースでの mruby↔Spinel 比較**は
  従来の毎回 reset 版でも取れるようにしておく(showcase の「ライブラリ呼び出しは前処理を
  払う」という知見自体は残す)。
- 実装着手前に最小スパイク: 「reset を飛ばして 2 回目の entry で civ_ が生きているか」を
  1 プログラムで確認してから codegen に手を入れる。
