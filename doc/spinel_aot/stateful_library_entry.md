# 検討: Spinel を「状態を保つライブラリ」として呼べるようにする(B案)

> **B-lite は実装済み (2026-08-14)**。手順は `impl_plan_stateful_library_entry.md`、
> 結果は `doc/mic_spectrum/report/track_a.md` の E6 節。本書の見立て
> (「civ_ は既に mark 済みなので reset を飛ばすだけでよい」) は当たっていた。
> 見落としていた点が 1 つ: **reset は整数グローバルをクリアしない**ので、
> キャッシュのキーはキャッシュしたオブジェクト自身にする必要がある。

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
