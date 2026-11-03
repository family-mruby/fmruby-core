# mruby-checkpoint の持ち込み (動いているアプリの状態を運ぶ)

> 状態: 進行中 | 更新: 2026-11-03 | F0 (patch と gem をネイティブ版と sim に入れる) 実装済・検収待ち。mruby の VM で動くアプリを止めて状態をイメージに書き、別の VM・別のプロセス・別の端末で続ける。原理検証は別リポジトリ mruby-checkpoint で済み、ここはそれを Family mruby に入れる段

## 目的

mruby の VM で動いているアプリを、アプリの協力なしに途中で止め、状態 (ヒープ・
フレーム・Task) をイメージに書き出し、別の VM で続きから動かす。最初のデモは、
1 本の mruby アプリを 2 つの Family mruby (同じビルドのネイティブ版) の間で運ぶこと。

原理・形式・試験は別リポジトリ **mruby-checkpoint** (family-mruby/mruby-checkpoint)
にある。この文書には Family mruby の側で決まったことだけを書き、向こうの内容は写さない。

- 原理検証の計画と、Family mruby に持ち込む段 F0-F6・ユーザの決定 D1-D16:
  mruby-checkpoint の `doc/plan.md` (「Family mruby に持ち込む段」と
  「決まったこと (2026-11-03、Family mruby への持ち込み)」)
- 隔たりの調査 (VM・ビルド・アプリの作り・gem・時分割・転送): 同 `doc/survey_family_mruby.md`
- イメージの形式: 同 `doc/format_v1.md`

## 方針

- gem は mruby-checkpoint を固定点で取り込む (D2)。`lib/add/MRUBY_CHECKPOINT_PIN` の
  commit を `rake checkpoint:setup` が `vendor/mruby-checkpoint` に取り、`rake setup` が
  gem (`mrbgem.rake`・`include/`・`src/`) を picoruby の木へ写す。作業中のチェックアウトは
  `MRUBY_CHECKPOINT_DIR=<dir>` で使える (取りに行かず、pin と違えば rake がそう言う)。
- mruby への patch は `lib/patch/picoruby-mruby/lib/mruby/` の丸ごとの写しに混ぜる (D3)。
  印は `fmrb: checkpoint`。中身は mruby-checkpoint の `patches/0001` (セーフポイントの
  hook、判定の遅い側を関数に、hook の後のフレームの取り直し) と `0002` (切り替えの要求を
  数える)。`0003` は Family mruby に同じ直しが既にある。
- patch は全ビルドに入る (普段の速さが変わらないことを命令数で確かめる)。hook の関数
  ポインタだけは `MRB_VM_SAFEPOINT_HOOK` のときに置く。この定義は gem を入れるビルドだけが
  持つので、ESP32 の静的な領域 (内蔵 RAM) は増えない。
- gem を入れるのは、まずネイティブ版と sim だけ (D4)。端末は F6 で内蔵 RAM と flash を
  測ってから。ブラウザ版も今は入れない。

## スコープ (段)

段の中身は mruby-checkpoint の `doc/plan.md` の表が正。ここでは Family mruby の側の状態だけを持つ。

| 段 | 内容 | 状態 |
|---|---|---|
| F0 | patch と gem を Family mruby に入れる (ネイティブ版と sim) | 実装済・検収待ち (report/f0.md) |
| F1 | ネイティブ版の 1 プロセスの中で、窓のないアプリを止めて同じ所で続ける | 指示済 (`instruction_f1.md`) |
| F2 | ネイティブ版の 2 プロセスの間で受け渡す | 未着手 |
| F3 | 窓のあるアプリ | 未着手 |
| F4 | ビルドの違う端末の間 | 未着手 |
| F5 | Asterism で送り、受け取った側で起動する | 未着手 |
| F6 | 実機 (まず Tab5、その後 S3) | 未着手 |

## 受け入れ条件 (F0)

- sim (`rake build:linux`) とネイティブ版 (`rake native:core`) が gem 入りで作れ、
  `rake test` (ホスト) が通る。
- ESP32 の設定は gem を持たず、内蔵 RAM の増分 0 (同じコミットの前後で `idf.py size-components`)。
- 普段の速さ: patch の前後で、mruby のベンチマークの命令数 (valgrind) の差が 1% 以内。
  Family mruby の定義 (`MRB_USE_DEBUG_HOOK`、自前の時分割あり / なし) で測る。
- gem は 1 プロセスに VM がいくつあっても動く (静的な表を持たない)。mruby-checkpoint の
  試験 (`rake test`・`test:asan`・`test:stress`) が通る。
- `rake regress` と sim の画面 (`regress:screens`)・VM プール (`regress:run`) が変わらない
  (本体 checkout に取り込んでから親が流す)。

## 未確定事項

- タイムスライスの残りを HAL の数ごと運ぶ (D10) 入口の形。F1 で決める (report/f0.md の「F1 が要るもの」)。
- 書き出しのバッファと表を VM のプールの外 (PSRAM) から取る (D11) 口の形。
- hook の関数ポインタを端末に入れるとき (F6) の置き場所 (今の形だと 4 バイトの .bss)。
