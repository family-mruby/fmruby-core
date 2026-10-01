# 作業指示書 M1: ミュート

対象: 実装担当のサブエージェント。前提: plan.md。report は `report/m1.md` へ。

## T1: 調べる

- 音の命令が core から出ていく経路を全部洗い出す (host_task の音のメッセージ、fmrb_audio_*、kernel / アプリの
  gem から音を鳴らす API、BASIC・MicroPython・Lua の音、MIDI の内蔵 APU の経路、デスクトップの起動音)。
  1 か所で止められる場所を決め、表にする (経路、通るか、止め方)。
- system_conf の読み書きの仕組み (config/ から生成される system_conf.toml、実行時の保存の口) と、
  デスクトップのメニューバーの指示器 (かなモードの指示器) の作りを読む。

## T2: 作る

- plan.md の方針どおり: `audio_mute` の設定、core での止め方 (鳴らす命令を送らない・ミュートにした瞬間に止める)、
  P4 のコーデックのミュート、メニューバーの指示器とメニューの項目 (日本語・英語、i18n の既存の仕組み)、
  devctl の `/audio/mute`、MCP の fmrb サーバ (tools/mcp/、親のリポジトリ) にミュートを切り替える手段。
- MCP の変更は親のリポジトリ (family-mruby) の tools/mcp/。既存のツールの書き方と selftest に揃える。
  サーバは再起動しないと新しいツールが見えないので、動作は CLI か selftest で確かめる。
- 起動の早い段階で設定を読み、起動音より前に効くこと。

## 検証

- sim (標準・互換): ミュートの切り替え (メニューバー)、MML を起動して音が出ない / 出る
  (`ruby tools/fmrb_audio_probe.rb` で数値で確かめる)、再起動して残ること (起動音の区間も probe で)。
- P4-Nano: ミュートにしてからアプリ (MML) を起動し、無音であることを確かめる。**実機で音を出す確認はしない**
  (ユーザが通話中のことがある。ミュートを外して鳴ることの確認は sim で行い、実機は親がユーザに依頼する)。
  実機ではまず遠隔でミュートにしてから、焼き直しや再起動をする。
- ブラウザ版: ミュートの切り替えと、音の命令が止まること (ログか web の音の状態)。
- ビルド: TAB5 / NARYAv4 / S3 / Linux / wasm。静的な D/IRAM が develop を超えない。`rake test`。

## 作業の決まり

- 作業ブランチ `feature/audio-mute` (fmruby-core、親が develop から切る)。親のリポジトリの tools/mcp/ の変更は
  family-mruby の作業ブランチ `feature/audio-mute` (親が切る) に。自分が変えたファイルだけをパスで指定して
  コミット。push とマージはしない。
- `.env` は書き換えずコミットしない。最後に `git diff .env` が scratchpad の env_before_audio_mute.diff と一致する。
- sdkconfig、graphics-audio、submodule は変えない。config/ を変えたら flash/etc/ は生成物であることに注意
  (config/ を直す)。
- 親から「実機の操作を止めて」と伝えられたら、書き込み・再起動・アプリの起動と停止・遠隔の入力をすぐ止める
  (ミュートにする遠隔の操作だけは、親が許したら行う)。
- 目視・耳の確認は親がユーザに依頼する。ユーザとのやり取りはしない。
- report は日本語の常体。コードのコメントと commit メッセージは英語 (件名 `<領域>: <要約>`、
  Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>)。

## 止まる条件

- 1 か所で止められず、graphics-audio や各言語の音の API を個別に直す必要があると分かったとき。
- 設定の保存の仕組みが無く、新しく作る必要があるとき (案を返す)。

## report に書くこと

音の経路の表、止め方、設定とメニューと遠隔の口、確認の結果、見立てと違った点、残件。

## 実機について (親から)

- 今の P4-Nano には S2 の試験用のファーム (計測入り) が入っている。最初に焼くのはこの M1 の版でよい
  (develop に戻す必要はない)。ただし**焼くと再起動して起動音が鳴る**。M1 の版が入るまではミュートできない
  ので、**最初の書き込みの前に止まって親に返す** (親がユーザに鳴らしてよい時を確かめる)。それまでは sim・
  ブラウザ版・ビルドで進める。

## 設計の変更 (親から、2026-10-01)

plan.md の「方針」を読み直すこと。host タスクで命令を種類ごとに捨てる作り (`fmrb_audio_cmd_allowed`、
`fmrb_audio_play` / `resume` の止め、ミュートの瞬間に STOP を送ること) は**やめる**。代わりに出力の最後の段で
サンプルを 0 にする (P4・ブラウザ版は audio_p4、Retro・sim は graphics-audio)。

- graphics-audio の変更は許可する (fmruby-graphics-audio の作業ブランチ `feature/audio-mute`、親が切った)。
  Retro の WROVER は ESP32 のビルド (`rake build:esp32`、graphics-audio 側) まで確かめる (実機はつながっていない)。
  sim では `tools/fmrb_audio_probe.rb` で無音・有音を数値で確かめる (graphics-audio の SHM のリングの段で 0 にするなら
  probe でそのまま見える)。
- 音の経路の表は「出力の段で止まるか」の確認に使う (全部の経路が最後の段を通ること)。
- 設定・メニューバー・設定画面・devctl・debugd・MCP の口は、これまでの作業を流用してよい。
