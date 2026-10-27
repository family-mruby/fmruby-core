# L10 指示書: Shell から Linux のコマンドを使う

> 計画: doc/linux_native/plan.md (「Linux のプログラムとの連携」の 2、段取りの L10)。
> 前段の結果: report/l2n.md (待たずに確かめる形、blocking-call gate)、
> report/l6.md (OS のサービスの受け口の作り)、report/l7.md (/mnt の素通し)

## ユーザの決定 (2026-10-27、変えない)

- **呼び方**: 頭に `!` を付けた行を Linux で実行する (`!ls -l`、`!git status`)。
  `!` だけの行は Linux の対話のシェル (bash、無ければ sh) に入り、そのシェルが
  終わる (`exit`・Ctrl+D) と Family mruby の Shell に戻る。組み込みのコマンド
  (ls・cat・cd など) は今のまま。
- **作業ディレクトリ**: Shell の今の場所に合わせる。`/mnt/host/src` にいれば
  実際の `~/src`、`/mnt/root/etc` なら `/etc`。`/mnt` の外 (`/home` など) に
  いるときは、Family mruby の振り分けで決まる実体の場所 (利用者の場所など)。
- **端末の制御の文字**: 行の出力と色だけ。改行・行頭への戻り (進み具合の表示で
  同じ行を書き直す形)・バックスペース・タブ、SGR の色と太字と元に戻すまで。
  カーソルの移動や画面の消去は解釈しない (捨てる)。全画面のプログラム
  (vim・top) は対象外で、`TERM=dumb` で起動する。
- **権限**: 全部の Family mruby のアプリに許す (L7 の決定)。Cardputer では
  root で動く。

## 作業の内容

### 1. Linux のプログラムを動かす口 (C)

- 擬似端末 (pty) の下で 1 つのプログラムを起こし、出力を読み、入力を書き、
  シグナル (Ctrl+C の SIGINT) を送り、終わりと終了コードを知る口を作る。
  置き場所と形は L6 の `fmrb_platform_sys` (関数の表を機種ごとに差し込む形) に
  合わせ、native だけが実装を持つ。端末・sim・ブラウザ版は「使えない」と
  答える (動きと内蔵 RAM を変えない)。
- 協調の作りなので、読み書きは待たない (poll で確かめる、L2n と同じ)。
  子のプロセスの後始末 (waitpid) も待たずに行い、ゾンビを残さない。
  アプリ (Shell) が終わったり kill されたりしたときは、子のプロセスの組
  (プロセスグループ) を終わらせる。
- 環境: `TERM=dumb`、`COLUMNS`・`LINES` は Shell の窓の桁数・行数 (pty の
  大きさも合わせる)、`HOME` などは Family mruby の起動時の環境を受け継ぐ。
- 作業ディレクトリは、Shell から渡す仮想の場所を file HAL の振り分けで実体に
  直して使う (表の外の場所なら断る)。

### 2. Ruby の口

- Shell から使う最小の API を足す (例: 起こす・読む・書く・シグナル・終わったか)。
  名前の付け方は既存の FmrbApp / FmrbSys 系に合わせる。Spinel のカーネルと
  全部 mruby の両方で動くこと (Shell の実行のエンジンを確かめ、型が絞れない
  呼び出しで黙って 0 にならないこと)。

### 3. Shell

- `!<コマンド>` は `/bin/sh -c` で実行し、出力を Shell の画面に流す。終わるまで
  Shell のキー入力はそのプログラムの入力になる (行単位でなく打鍵ごとに送るか、
  エコーをどうするかは pty の作りに合わせて選び、report に理由を書く)。
- `!` だけは対話のシェル。プロンプトは Linux のシェルのものを出す。
- Ctrl+C は SIGINT、Ctrl+D は入力の終わり。終わったら終了コードが 0 以外の
  ときだけ短く示す。
- 色: SGR の 8 色と明るい 8 色を Shell の色に写す (端末の色の決まりと見やすさを
  考えて選び、report に表を書く)。今の Shell の行の持ち方 (`@history` の文字列)
  で色を持てなければ、行に色の情報を足す。
- 行頭への戻り (`\r`) は今の行の書き直し、バックスペースは 1 文字戻し。
- 端末・sim・ブラウザ版で `!` を打ったときは「このマシンでは Linux の
  コマンドは使えない」と分かる 1 行を出す。
- `help` に `!` の説明を足す (en・ja の文字列の表があればそこに)。

## 試験

- C の口の host 試験 (test/native/): `echo`・終了コード・標準入力への書き込み・
  SIGINT で止まる・出力の多いもの (`seq 1 100000`) で取りこぼさない・
  作業ディレクトリ・子が残らない (ゾンビ・孤児が無い)。
- 色の解釈の試験 (SGR の列 → 行と色の区切り)。Ruby で書くなら host で
  動く形で。

## 触ってよい範囲

- fmruby-core: `main/prebuild_scripts/default_app/` (Shell)、
  受け口のための `main/` と `components/` の該当箇所 (L6 の受け口の隣)、
  `native/`、`hosted/`、`test/`、`rakelib/`、`doc/linux_native/report/`、
  Shell の手引きがあればそれ。
- 触らないもの: fmruby-graphics-audio、sdkconfig、submodule の中、`flash/`、
  plan.md、端末・sim・ブラウザ版の動き (「使えない」の 1 行を除く)。

## 作業場所と git

- 本体の checkout、fmruby-core の `feature/linux-native` に続けてコミット
  (規約は fmruby-core/CLAUDE.md、Co-Authored-By、`git add` は明示)。
  develop へのマージと push はしない。
- `build/` は今 NARYAv4 (ESP32-P4) のビルド。自分で `rake clean_all` はしない。
- native は HOME を一時ディレクトリにして試す。**試験で利用者のホームや
  `/` の下に書かない**。Linux で走らせるコマンドは読むだけのもの、または
  一時ディレクトリの中に書くものに限る。sudo は使わない。

## 受け入れ条件

1. native (`--machine sdl`) の Shell で `!echo hello` が `hello` を出し、
   `!false` で終了コードが示される。
2. `cd /mnt/host` してから `!pwd` が一時の HOME を出す。`/mnt/root/etc` では
   `/etc`。
3. `!ls --color=always` (または `!printf` で SGR を出す) の色が Shell に出る。
   `!seq 1 2000` を取りこぼさず、最後まで出る (スクロールで見られる)。
4. `!sleep 30` を Ctrl+C で止められる。`!cat` に打った行が返り、Ctrl+D で
   終わる。`!` で bash に入り、`cd /tmp; pwd` が働き、`exit` で戻る。
5. Shell を kill したとき、起こしたプログラムが残らない (`ps` で確かめる)。
6. keys の形 (`--machine sdl_keys`) の Shell でも 1・4 が通る。
7. 1・4 を標準と全部 mruby の両方で。
8. sim で `!ls` が「使えない」の 1 行になる。`rake regress`・
   `rake regress:screens` が通り、`rake regress:size` で内蔵 RAM が増えていない。
   画面の基準が変わったら、変化を report に書き、基準の更新は別のコミットにする。

## 止まる条件

- 上の決定と食い違う作りが要るとき。
- 端末・sim・ブラウザ版の動きを変えないとできないとき、内蔵 RAM が増える
  作りしか無いとき。
- `rake clean_all` や sudo が要るとき。
- 止まったら report に状況を書いて返す。

## report (doc/linux_native/report/l10.md) に書くこと

- C の口の形と置き場所、待たない作りと子の後始末
- Ruby の API、Shell の入力の渡し方とエコーの扱い、色の表
- 受け入れ条件ごとの結果と、確かめたコマンド・数値 (内蔵 RAM・flash の差)
- 見立てと違った点、撤回した仮説、踏んだ罠、残件
