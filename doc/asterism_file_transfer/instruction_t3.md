# T3 指示書: 速さと大きなファイル

> 状態: 実装中 | 更新: 2026-11-08 | 正は plan.md (方針・決まったこと・決定事項の各節)。ここには範囲と進め方だけを書く

## 範囲

1. **種類 file の途中のファイルを置き場所の隣に** (決定事項、T2 の後): `/tmp` でなく
   `/home/inbox/` の隠しファイル (`.` で始まる名前) に直接書く。受けられる大きさは
   storage の空き (`FmrbApp.free_bytes`) まで。commit で `/tmp` から写さない
   (送り手の期限 10 秒を ESP32 で越えるおそれを消す)。控え (`.xfer`) の置き場所と、
   期限切れ・取りやめ・照合が合わないときの片付けを、置き場所に合わせて直す。
   checkpoint は今のまま `/tmp`。受け手のアプリの起動し直しからの再開も今どおり通ること。
2. **片の大きさと同時に送る片の数を受け手が決める** (plan.md の方針 6): `begin` の答えで
   受け手が片の大きさ (呼び出しの上限約 4 KB の内側) と `window` を返し、送り手はそれに従う。
   値は受け手が自分の空きや種類から決める (決め方を report に)。
3. **1 MiB を超えるファイル**: ネイティブ版どうし・ネイティブ版 ⇔ ブラウザ版で数 MB
   (例: 4 MB・16 MB) を運び、速さを片の大きさ・同時に送る片の数ごとに測る。
4. ESP32 で 4 MB を運ぶ確認は実機がつながったとき。手順を report に書く
   (S3 の storage の空き、P4 の SD など、端末ごとに置ける大きさの見込みも)。

## 確認

- `rake regress`、`rake regress:transfer`、`rake regress:transfer:quick` (要点の試験に
  大きい file を 1 つ足すなら 1 分ほどに収める)、`rake regress:checkpoint`
  (実機の `BOARD=` は除く)。main/ の C を変えたら `rake regress:size`。
- 種類 file を 4 MB 以上でネイティブ版どうし運び、途中で接続を切っても続きから届き、
  SHA-256 が合い、`/tmp` を使っていないことをログか試験で示す。

## 触ってよい範囲

fmruby-core (転送のアプリ・試験のアプリ・`FmrbTransfer`・Shell の `asend`・道具・試験・rakelib、
`doc/asterism_file_transfer/`)、asterism gem の作業用 checkout (コミットまで)。
それ以外が要るなら止まって返す。作業は本体 checkout で、終わったら build/ を残さない。
push はしない (gem → fmruby-core の順で push するのは、ユーザの指示の後で親が行う)。

## 止まる条件

- plan.md の方針・決定事項と違う動きが要る。利用者から見た動き (仕様) が変わる直し方を
  選ぶ必要が出た。
- 実機・sudo・公開リポジトリへの push が要る所まで来た。
- 測った値が方針を揺るがす。

## report

`doc/asterism_file_transfer/report/t3.md` (作り、受け手の決め方、速さの表、ESP32 の手順、残件)。
