# 実装指示書 F1: ネイティブ版の 1 プロセスの中で、窓のないアプリを止めて同じ所で続ける

`doc/checkpoint/plan.md`、`doc/checkpoint/report/f0.md` (特に §5「F1 が要るもの」)、
mruby-checkpoint の `doc/plan.md` (D1〜D16 と親の仮決め)・`doc/survey_family_mruby.md`・
`doc/format_v1.md` を先に読む。

## 目的

Family mruby のネイティブ版 (1 プロセス) で、窓を持たない mruby のアプリを、動いている途中で
イメージに書き出して閉じ、そのイメージから起動し直して、止まった所から続ける。F2 (2 プロセスの間)
と F3 (窓のあるアプリ) の土台になる仕組みを、ユーザの決定どおりに作る。

## 決まっていること (ユーザ、2026-11-03)

- D5: 止める地点は、`_spin` を起こして C の外 (セーフポイント) で書く。
- D6・D12: 運べるのは `.app.toml` で申告したアプリだけ (印の名前は決めてよい。例 `movable = true`)。
  `mrb_checkpoint_open` と基準 (`mrb_checkpoint_baseline`) は、申告したアプリの VM でだけ行う。
  hook は既存の `on_suspend` / `on_resume` と別の名前 (例 `on_checkpoint` / `on_restore`)。
- D9: `Machine.board_millis` は VM ごとのずれを持ち、着いた先でも書いたときの値から続ける。
- D10: タイムスライスの残りは、HAL の数 (`tick_countdown` ほか) ごと運ぶ。HAL に VM ごとの読み書きの
  入口を足し、gem は埋め込む側の callback で受ける。
- D11: イメージは `/tmp`、書き出しのバッファと表は VM のプールの外の PSRAM (`fmrb_sys_malloc` 系)。
  gem にアロケータの入口を足す。
- D14: 止める・起動する段取りは**カーネル (Ruby)** に入れる。カーネルは標準の構成では Spinel、
  mruby の構成もあるので、両方で動かして確かめる。
- D15 (F1 では): 書き出したらアプリを閉じ、同じプロセスでイメージから起動し直す (2 プロセスの間の
  返事は F2)。
- D16: Spinel で動くアプリは対象外。
- P6: アプリを読み込むときに、コンパイルするソースを `mrb_checkpoint_add_source` で登録する
  (flash のバイトコードは対象外)。復元した側では登録し直される。
- 実機 (ESP32) に入れるのは F6。F1 でも共有のコードを変えたら、ESP32 の内蔵 RAM は +0 を守る。

## やること

1. 申告と準備: `.app.toml` の印を読み、申告したアプリの VM でだけ checkpoint を開き、基準を取り、
   ソースを登録する。印の無いアプリの費用は F0 のまま (VM あたり 16 バイト) であること。
2. 止める: カーネルへの要求 (入口は決めてよい。デバッグサーバの命令・シェルのコマンド・カーネルの
   API など。開発の道具から叩けること) → カーネルがアプリに `APP_CONTROL` の `checkpoint` を送る →
   アプリのタスクの中で `_spin` を抜けて、セーフポイントで `/tmp/<名前>.ckpt` に書き出す → アプリの
   `on_checkpoint` (あれば) → アプリを閉じる。`on_event` の中など書き出せない所では断り、理由を返す。
3. 続ける: カーネルがイメージから起動する (`.ckpt` を見分ける起動の入口)。新しい VM を開き、
   `mrb_checkpoint_open`、復元、アプリの Task (`mrc_create_task` の形と `mrb_checkpoint_set_host_task`
   / `mrb_checkpoint_host_task`)、`on_restore` (あれば)、続きから動く。
4. 時間: D9 と D10。
5. 試験のアプリ: 窓を持たない申告済みのアプリ (例: Task 2 つでカウントし、ログに出す)。
6. 自動の確認: ネイティブ版で「起動 → 数える → 止める → 起動し直す → 続きから数える」を道具で流し、
   ログで続きを確かめる試験を、回帰の道具 (`doc/regression/usage.md`) に組み込む (ネイティブ版の
   場面か、新しい小さな道具。どちらがよいかは判断してよい)。カーネルが Spinel の構成と mruby の構成の
   両方で。
7. 文書: `doc/checkpoint/report/f1.md` (経過・計測・サブの仮決め・F2 に要るもの)、`plan.md` の F1 の
   行を「実装済・検収待ち」に。

## 確かめること

- `rake regress` (ホスト)、`rake regress:run`、`rake regress:screens ONLY=retro,modern,native,native-keys`
  が通る (ブラウザ版は流さない。人が開いているブラウザを閉じないため)。sim の基準が動いたら理由を
  report に書き、基準の更新は別コミット (親が検収)。
- 共有のコードを変えたら `rake regress:size` で内蔵 RAM +0。
- mruby-checkpoint に手を入れたら、そちらの `rake test`・`test:asan`・`test:stress`。
- 6 の試験が両方の構成で通る。

## 触ってよい範囲・止まる条件・守ること

- fmruby-core (本体の checkout、`feature/linux-native` にコミット。push しない) と、mruby-checkpoint
  (worktree で作業し、ブランチにコミット。push しない。固定点を新しいコミットに向けるのは、親が
  push してから)。
- fmruby-core の CLAUDE.md の決まり (コメントは英語、`fmrb_*` の抽象、素の malloc を使わない、
  submodule は lib/ 経由、lib/ を変えたら `rake clean`)。
- 止まる条件: 利用者から見た動きが変わる直し方を選ぶ必要が出たとき (既存のアプリの動き、
  `on_suspend` / `on_resume` の意味など)、スケジューラの作りを変える必要が出たとき、D1〜D16 と
  食い違う事実が出たとき。止まったら report に書いて返す。
- 文書は常体、定着したカタカナ語 (イメージ・コンテキスト・フレーム・タイムスライス・セーフポイント)
  を使い、作った和語を使わない。文書にローカルの絶対パスを書かない。
