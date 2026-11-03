# 実装指示書 F2: ネイティブ版の 2 つのプロセスの間で、窓のないアプリを移す

`doc/checkpoint/plan.md`、`report/f1.md` (§6「F2 に要るもの」、§3.4 イメージの置き場所)、`report/f1b.md`、
`instruction_f1.md`・`instruction_f1b.md`、mruby-checkpoint の `doc/plan.md` (D1〜D16) を先に読む。

## 目的

同じビルドのネイティブ版を 2 つ (A と B) 動かし、A で動いている申告済みの窓のないアプリを止めてイメージにし、
B で続きから動かす。A のアプリは、B が起動できたと返事をしてから閉じる (D15)。ヘッドレスのデモの形になる。
ネットワーク (Asterism) で送るのは F5。F2 では、同じ PC の 2 つのプロセスの間の受け渡しで仕組みを固める。

## 決まっていること

- D1: 最初の組み合わせは、同じビルドのネイティブ版 ⇔ ネイティブ版。
- D14: 止める・起動する段取りはカーネル (Ruby) に入れる。カーネルが Spinel の構成と全部 mruby の構成の両方で
  確かめる。
- D15: 移動元は、着いた側が起動できたと返事をしてから、普段の終わり方で閉じる。それまでは止めたままにする
  (`ckpt_written` で Task を終えるのを遅らせ、返事で「閉じる」か「続ける」かを決める形が素直、f1 §6)。
  着いた側が断ったり起動できなかったりしたら、移動元はそのまま続ける。
- D9: `board_millis` は書いたときの値から続く (別のプロセスでもそのまま効く)。
- F1b のアプリの API は `FmrbApp#checkpoint(restart: false)` / `FmrbApp.checkpoint(pid, restart: false)`
  (2026-11-04 改名)。
- 内蔵 RAM +0。ESP32 に入れるのは F6。

## やること

1. **イメージをプロセスの外へ出す道**: ネイティブ版の `/tmp` はプロセスの中の RAM のファイルで、外から
   見えない (f1 §3.4)。A から取り出し、B へ入れる道を作る。開発の道具 (デバッグサーバ、`tools/fmrb_native.rb`)
   から使える形にする。F5 で Asterism の受け口 (`begin` / `chunk` / `commit`) に置き換えられるよう、
   「分けて渡し、最後に確定する」形に揃えておく (wasm のアプリのインストールと共用にする予定。
   `doc/wasm_apps/plan.md` の 4)。
2. **移す段取り**: A のカーネルにアプリを止めさせて (閉じずに止めたまま) イメージを書く → イメージを B へ →
   B のカーネルがイメージから起動 → B が「起動できた」(または断りの理由) を返す → A のカーネルがアプリを
   閉じる (または続ける)。返事が一定時間来なければ A は続ける。
3. **開発の道具**: `tools/fmrb_native.rb` で 2 つのプロセスを並べて動かせること (実行のディレクトリと
   デバッグサーバのポートを分ける。今の `--run-dir` と `FMRB_DEBUG_TCP_PORT` で足りるかを見る)。
   移す操作 (A の pid → B) を 1 つのコマンドにし、MCP の `native_app` からも使えるようにする。
4. **確かめ**: 同じビルドの 2 つのプロセスで、permanent の名前 (約 4,046 個) が同じに引けること。
   別のプロセスの `board_millis` と mruby-task の tick の続き方。
5. **試験**: `rake regress:checkpoint` に「2 つのプロセスの間で移す」場面を足す (成功、B が断ったときに A が
   続ける、返事が来ないときに A が続ける)。カーネルが Spinel の構成と全部 mruby の構成の両方で。
6. **文書**: `report/f2.md` (経過・計測・サブの仮決め・F3 に要るもの)、`plan.md` の F2 の行を
   「実装済・検収待ち」に、`doc/regression/usage.md`。基準の費用が動いたら数字を report に書く。

## 確かめること

- `rake regress:checkpoint` (両方の構成)、`rake regress`、`rake regress:run`、
  `rake regress:screens ONLY=retro,modern,native,native-keys` (ブラウザ版は流さない)、
  共有のコードを変えたら `rake regress:size` (内蔵 RAM +0)。
- mruby-checkpoint に手を入れたら、そちらの `rake test`・`test:asan`・`test:stress`。

## 触ってよい範囲・止まる条件・守ること

- fmruby-core は本体の checkout の `feature/linux-native` にコミット (push しない)。mruby-checkpoint は
  worktree のブランチにコミット (push しない。開発中は `MRUBY_CHECKPOINT_DIR`、固定点は親が合わせる)。
  family-mruby のリポジトリ直下 (`tools/`) も触ってよい (コミットまで)。
- 止まる条件: 利用者から見た動きが変わる直し方、D1〜D16 と食い違う事実、スケジューラの作りを変える必要。
- 文書は常体、定着したカタカナ語を使う。コミットする文書にローカルの絶対パスを書かない
  (コミットの前に、差分をローカルのパスで検索して確かめる)。
