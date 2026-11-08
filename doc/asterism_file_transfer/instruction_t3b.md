# T3b 指示書: SD の inbox とブラウザ版の空き

> 状態: 実装済・検収待ち | 更新: 2026-11-08 | 正は plan.md の「決定事項 (2026-11-08 ユーザ、T3 の後)」。結果は report/t3.md の T3b

## 範囲

1. **SD の inbox**: 種類 file の置き場所に `/sd/inbox/` を足す。送り手が選ぶ:
   Shell の `asend` に SD を選ぶ指定 (例: `--sd`。既存の `-o` と並ぶ短い形にしてよい)、
   `tools/fmrb_send.rb` に同じ指定、`FmrbTransfer` に引数。指定がなければ `/home/inbox/`。
   受け手は SD が無い・書けない端末なら `begin` で理由つきで断り、ある端末では SD の空き
   (`FmrbApp.free_bytes("/sd")`) で入らなければ断る。途中のファイルは置き場所の隣
   (`/sd/inbox/.<名前>.file.part`)。名前の決まり・同じ名前の扱い・取りやめ・期限・
   起動時の片付けは `/home/inbox/` と同じ。ネイティブ版・sim での SD の扱い (`/sd` が
   ホストのどこか、無い構成) を調べ、試験はネイティブ版で `/sd` がある構成と無い構成の両方。
2. **ブラウザ版の空き**: `FmrbApp.free_bytes` がブラウザ版の `/home` で
   `navigator.storage.estimate()` (quota − usage) から目安を返す (同期で答える必要があるなら、
   起動時と定期に取った値を持っておく形でよい。どうしたかを report に)。目安なので余裕
   (例: 1 割) を引く。`begin` で入らないものを断ることを quick の `WEB=1` で確かめる
   (T3 で外した確認を戻す)。
3. plan.md の T3 の受け入れ条件の書き方 (P4 で 4 MB、S3 は空きの範囲) は親が直す。触らない。

## 確認

- `rake regress`、`rake regress:transfer`、`rake regress:transfer:quick` (と `WEB=1`)、
  `rake regress:checkpoint` (実機の `BOARD=` は除く)。main/ や lib/add の C を変えたら
  `rake regress:size`。
- ネイティブ版で `asend --sd` (相当) が `/sd/inbox/` にだけ置き、SD の無い構成では断ることを示す。

## 触ってよい範囲

fmruby-core (転送のアプリ・`FmrbTransfer`・Shell の `asend`・`tools/fmrb_send.rb`・
`FmrbApp.free_bytes` の各ポート (`lib/add/picoruby-fmrb-app`、wasm の JS)・試験・rakelib、
`doc/asterism_file_transfer/` の report)、asterism gem の作業用 checkout (コミットまで)。
それ以外が要るなら止まって返す。push はしない。build/ を残さない。

## 止まる条件

- plan.md の決定事項と違う動きが要る。仕様が変わる直し方を選ぶ必要が出た。
- 実機・sudo・公開リポジトリへの push が要る所まで来た。

## report

`doc/asterism_file_transfer/report/t3.md` に「T3b」の節を足す (作り、ネイティブ版・sim の
`/sd`、ブラウザ版の目安の取り方、試験の結果、ESP32 の手順、残件)。
