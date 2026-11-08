# T5 指示書: アプリの移動 (checkpoint) を新しい受け口の作りに揃える

> 状態: 実装中 | 更新: 2026-11-08 | 正は plan.md の決定 (T5 は checkpoint の載せ替えだけ。.app.wasm は wasm のアプリの対応のとき)

## 範囲

- アプリの移動 (種類 `checkpoint`) を、T1〜T3b で作った受け口の作りに揃える:
  受け手が片の大きさと同時に送る数を決める (T3)、全体の SHA-256 の照合 (T1)、
  種類ごとの許可 (`receive.toml` の `[checkpoint]`、古い書き方も読む。T2)、
  空きを見て `begin` で断る (T2)、取りやめと期限。checkpoint の片は今のまま `/tmp`
  (端末の再起動で最初から、の決定どおり)。
- カーネルの送り手 (移動を頼まれた側) が、`FmrbTransfer` / `Asterism::Transfer::Sender` の
  同じ部品を使う形にする。残っている古い経路 (1 片ずつ・照合なし等) があれば消す。
- 移動の速さ (ネイティブ版どうし・sim・ブラウザ版) を前後で測る (T1 の report の値と比べる)。

## 確認

- `rake regress`、`rake regress:checkpoint SIM=1` (ネイティブ版・sim・ブラウザ版の場面が全部)、
  `rake regress:transfer:quick`、`rake regress:transfer`。main/ の C を変えたら `rake regress:size`。
- ESP32 (`BOARD=`) は実機がつながったとき。手順を report に。

## 触ってよい範囲

fmruby-core (転送のアプリ、カーネルの移動の経路、試験・道具・rakelib、`doc/asterism_file_transfer/`)、
asterism gem の作業用 checkout (コミットまで。0.12.0 は公開済み、変えるなら次の版の Unreleased に)。
asterism-console は触らない。push しない。build/ を残さない。

## 止まる条件

- 移動の動き (利用者から見た仕様) が変わる直し方を選ぶ必要が出た。
- 実機・sudo・push が要る所まで来た。

## report

`doc/asterism_file_transfer/report/t5.md` (作り、消した古い経路、速さの前後、試験の結果、ESP32 の手順、残件)。
