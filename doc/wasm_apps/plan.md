# wasm のアプリ (WAMR で TinyGo・Rust・Zig) 計画

> 状態: 計画済 | 更新: 2026-11-03 | WAMR (インタプリタ) を本体に入れ、TinyGo・Rust・Zig などで作った wasm のアプリを P4・sim・ブラウザ版で動かす。API の bindings は `sig/*.rbs` から各言語へ自動で生成する。着手は VM の checkpoint (doc/checkpoint) が落ち着いてから

## 目的

- Ruby 以外の言語 (TinyGo・Rust・Zig、ほかに C・AssemblyScript など wasm を出せる言語) で書いた
  アプリを、Family mruby のアプリとして動かす。言語ごとにエンジンを足す今の形 (BASIC・Lua・
  MicroPython) と違い、WAMR 1 つで wasm を出せる言語がまとめて入る。
- **API は型定義 (`sig/*.rbs`) から各言語の bindings を自動で生成する。** 言語ごとに API を設計しない
  ためであり、「rbs をインタフェース定義言語として使う」こと自体が Ruby の関係者に見せられる技術に
  なる (2026-11-03 ユーザ)。
- 他人が作ったアプリを安全に配れる: wasm のアプリは自分のメモリと、渡した関数にしか触れない。

## 方針 (2026-11-03 ユーザ)

- **主戦場はブラウザ版と P4**。sim でも動かす。ネイティブ版 (Linux の PC のコンポジタ) は優先度を
  下げる (使うのは主に作者だけのため。Cardputer Zero は別に考える)。
- **ブラウザ版でも、ブラウザの wasm エンジンは使わず、WAMR の C を wasm (emscripten) にして動かす。**
  本体の C を wasm にしているブラウザ版の作りにそのまま乗り、ブラウザ・P4・sim で同じインタプリタが
  動く (動きの違いを追わずに済む。将来 wasm のアプリを checkpoint で運ぶときも実装が 1 つで済む)。
- **速さはほどほどでよい**。WAMR はインタプリタで使い、AOT (事前に機械語にする方式) は使わない。
  AOT は実行できるメモリが要り、ESP32 では内蔵 RAM (IRAM) を食いやすく、内蔵 RAM を増やさない決まり
  とぶつかる。
- **内蔵 RAM は +0**。WAMR の本体のコードは flash、ヒープ・スタック・wasm のメモリは PSRAM (アプリ
  ごとのプール) に置く。
- アプリのコンパイルは PC で行う (TinyGo・Rust・Zig のコンパイラは端末では動かない)。端末の上で書いて
  動かす Ruby のアプリとは別の、「配る側」の道として位置づける。

## 作るもの

### 1. WAMR を本体に入れる

- sim (Linux x86-64)、P4 (RISC-V 32bit、ESP-IDF)、ブラウザ版 (emscripten) の 3 つで同じ WAMR を
  インタプリタで組み込む。どのインタプリタ (classic / fast-interp) を使うかは W1・W2 の計測で決める。
- 新しいアプリの種類として、今のアプリの一生 (起動・イベント・終わり)・VM のプール・ランチャーに乗せる。
  メモリの上限はアプリごとのプールで縛る。

### 2. 型定義から bindings を生成する

- 元は `sig/*.rbs` (今はエディタの型支援のために書かれている: `fmrb_app`・`fmrb_gfx`・`fmrb_audio`・
  `fmrb_midi`・`fmrb_sprite`・`fmrb_tile`・`fmrb_ui`・`fmrb_p5`・`fmrb_misc` ほか)。
- 生成するもの: 本体の C の側のつなぎ (wasm の import 関数の実装の雛形と登録の表) と、各言語の
  bindings (TinyGo・Rust・Zig。C のヘッダも)。
- rbs は Ruby の形の型なので、ABI に落とす決まりが要る:
  - 生成に使える範囲: 整数 (i32 / i64)・浮動小数点 (f64)・真偽・文字列 (ポインタと長さ)・配列
    (ポインタと長さ)・オブジェクトはハンドル (整数)、など。
  - rbs で書けないもの (文字列や配列の持ち主・寿命、コールバック、単位) を補う注釈の書き方。
  - 型が混ざる値 (union)・ブロック・キーワード引数・中身の決まらない Hash の扱い (使わない、
    または形を決める)。
  - イベントとアプリの一生の渡し方: アプリが `on_event` などの関数を wasm から公開し、本体が呼ぶ形を
    基本にする。
  - **ABI に版を付ける**。型定義から生成するので、版は型定義の内容から決める。
- まず今の `sig/*.rbs` が API のどこまでを覆っているかを調べる (W0)。

### 3. アプリの形と配り方

- アプリの包み: `.app.wasm` と `.app.toml` (言語と ABI の版を書く)。
- 起動の前に ABI の版を確かめ、端末と合わないアプリは理由を出して断る。
- 入れる道:
  - **店** (`doc/app-distribution/`、完了済み): 正式な配布。`.app.wasm` も同じ店で配る。店がバイナリを
    正しく扱えるかを確かめる (picoruby の文字列でバイト列を扱うときの罠)。一覧に言語と ABI の版を出す。
  - **Console のファイル転送**: 開発中に PC で作ったものを端末へ送る。Console の許可表と記録に乗る。
  - **Asterism でのインストール**: 下の 4。
  - 開発の手早い道: Tab5 は今の `tab5_fs` の put → launch でも足りる。

### 4. Asterism の受け口 (インストールと checkpoint のイメージで共用)

- 受け口のサービスが Asterism で `begin` / `chunk` / `commit` を公開し、`caller:` で送り手を絞る。
  Zenoh の 1 回の上限 (4 KB) に合わせて分けて送る。端末の証明書による相手の確認がそのまま効く。
- 受け取ったものは種類に応じて処理する:
  - `.app.wasm` と `.app.toml` → 置いて、ランチャーの一覧に足す (インストール)。
  - checkpoint のイメージ → 着いた先で続きから起動する (doc/checkpoint の F5、決定 D13 と同じもの)。
- 店は「使う人が自分で選んで入れる」、Asterism は「管理する側 (Console・別の端末) が配る・移す」と
  向きで分ける。
- 受け口を最初に作るのは、W (この計画) と checkpoint の F5 のうち先に必要になった方。
  (2026-11-04) F5 で作った (実装済・検収待ち、doc/checkpoint/report/f5.md の 2 と 11): 転送のアプリ
  `/usr/share/transfer/transfer.app.rb` が `<ノード>/transfer/inbox` に `check` / `begin` / `chunk` /
  `commit` / `status` / `cancel` を出す。W5 は種類の表に `.app.wasm` の種類を足し、`commit` で置き場所へ
  移してランチャーの一覧に足す処理を書く形になる。

### 5. 将来: wasm のアプリを checkpoint で運ぶ

- wasm のアプリの状態は linear memory・グローバル変数・スタックで、mruby のヒープより単純。Ghost
  (動いているアプリを網の中で移す構想) を Ruby 以外のアプリへ広げる道になりうる。
- WAMR が実行中のスタック (インタプリタのフレーム) をどこまで取り出して戻せるかは未調査 (W0 で調べる)。

## 段

| 段 | 内容 | 状態 |
|---|---|---|
| W0 | 調べて設計する (文書だけ): `sig/*.rbs` の覆う範囲、rbs の使える範囲と ABI の決まり・注釈・版、イベントとアプリの一生の渡し方、WAMR の載せ方 (sim・P4・ブラウザ版) と費用の見積もり、店・Console・Asterism での入れ方、WAMR の checkpoint の可否 | 未着手 |
| W1 | sim に WAMR (インタプリタ) を載せ、手で書いた import 関数 2〜3 個 (ログ・点を打つ・待つ) で、TinyGo・Rust・Zig の「点を打つ」アプリを動かす | 未着手 |
| W2 | ブラウザ版 (WAMR を emscripten で) と P4 に載せ、内蔵 RAM・flash・速さを測る | 未着手 |
| W3 | rbs から生成する道具: C の側のつなぎと 3 言語の bindings を、まず窓・描画・入力・アプリの一生の範囲で | 未着手 |
| W4 | アプリの包み (`.app.wasm`・ABI の版の確認)、店での配布、デモのアプリ | 未着手 |
| W5 | Asterism の受け口でのインストール (checkpoint の F5 と共用) | 未着手 |

**着手は VM の checkpoint (doc/checkpoint、F1〜) が落ち着いてから** (2026-11-03 ユーザ)。

## 受け入れ条件

- P4・sim・ブラウザ版で、TinyGo・Rust・Zig のアプリが同じ `.app.wasm` のまま動く。
- その 3 言語の bindings は、`sig/*.rbs` から生成したものだけを使っている (手書きの bindings が無い)。
- 内蔵 RAM が増えない (`rake regress:size`)。今の回帰の道具 (`rake regress`・`regress:run`・
  `regress:screens`) が通る。
- 店から `.app.wasm` のアプリを入れて動かせる。ABI の版が合わないアプリは理由つきで断られる。

## 未確定事項

- rbs の使える範囲と、補う注釈の書き方 (W0 で案、ユーザが決める)。
- 最初に対応する言語の順と、各言語の SDK の配り方 (crate・Go の module・Zig の package・置き場所)。
- WAMR の版と設定 (classic / fast-interp、WASI をどこまで入れるか)。
- 速さの目安 (ほどほどの基準。W2 の計測を見て決める)。
- ネイティブ版・Cardputer Zero での扱い。
- WAMR での checkpoint の可否と、やるならいつか。
- 受け口 (W5 と checkpoint の F5) をどちらで先に作るか。
