# 実装指示書 NET2: WebSocket の受信の見直しを 5 → 10 → 30 ms に

NET1 (report/net1.md) で `Net::WebSocket::Client#receive` の見直しの間隔を
100 ms から 30 ms にした。返事がすぐ届いていても最初の見直しまで 30 ms 待つので、
待ちの始めは短く見て、だんだん伸ばす。

作業ブランチは fmruby-core の `feature/linux-native` (本体 checkout、直列)。

## ユーザ決定 (2026-10-28)

- 刻みは **5 → 10 → 30 ms、その待ちで何も届かないまま 1 秒たったら 100 ms**
  (次の待ちが始まると 5 ms に戻る)。1 秒で 100 ms にする所は、同じ日のユーザの
  追加決定。
- **Linux 版 (ネイティブ版と sim) だけに入れる**。実機 (ESP32-S3・P4) は
  30 ms より短い見直しの負担を実機で確認するまで、今の一定の 30 ms のまま。
- ブラウザ版はこの gem を持たない (lib/add/family_mruby_wasm.rb) ので対象外。

## 直すもの

- `lib/patch/picoruby-net-websocket/mrblib/client.rb` の `receive`。
  - 待ちが始まるたびに (receive の呼び出しごと、またはフレームを 1 つ受けた後)
    刻みを最初の 5 ms に戻す。
  - Linux 版かどうかの見分け方は、既にある仕組み (定数・ビルド時の定義・
    実行時の問い合わせ) から選び、report にどれをなぜ選んだか書く。新しい C の
    関数が要るなら止まって返す。実機のビルドで刻みが一定の 30 ms のままである
    ことを、コードの経路で示す (実機では動かさない)。
  - 先頭の "Family mruby patch" のコメントを更新する (英語)。

## 確認

- lib/ を変えたので、ビルドの前に `rake clean`。
- `rake regress`。`rake regress:size` (内蔵 RAM +0)。
- ネイティブ版 (標準と `NATIVE_ENGINE=mruby` の両方):
  - `l2n_check.rb --only http` が通る。
  - CDP の通し (NET1 と同じやり方、専用の user-data-dir と空いたポートの
    chrome-headless-shell) で、1 往復の時間を 20 回の平均で測る。NET1 の
    29-30 ms と比べて report に書く。
  - 何も届かない待ち (例: 3 秒の receive(timeout: 3)) の間の見直しの回数が、
    最初の 2 回を除いて最初の 1 秒は 30 ms 刻み、その後は 100 ms 刻みである
    ことを確かめる (ログや数え上げで)。
- sim (rake build:linux) で net_test の WS echo を 1 回通す。NET1 と同じく
  build/ は tools/regress/build_tree.rb で退避して戻す。

## 触ってよい範囲

- `lib/patch/picoruby-net-websocket/`、必要なら `rakelib/setup.rake`。
- submodule は直接編集しない。

## 止まる条件

- Linux 版だけを見分ける手段が既存に無く、新しい C の関数や設定が要るとき。
- 実機のビルドの動きが変わってしまうと分かったとき。
- 見立てと違う事実が出たとき。

## 守ること

- `rake clean_all` はしない。sudo しない。本当の HOME・/etc・/usr に書かない。
- tools/fmrb_web.rb が開いたブラウザは閉じない。自分で起動したブラウザ・サーバ
  だけ止める。
- 終わったら作業ブランチにコミット (英文、Co-Authored-By を付ける)。push はしない。
  文書に手元の絶対パスやユーザ名を書かない。
- report は `doc/linux_native/report/net2.md`: 計測値、見分け方の選択と理由、
  見立てと違った点、残件。
