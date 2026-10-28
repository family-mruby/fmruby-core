# 実装指示書 NET1: Net::HTTP と WebSocket の 3 つの欠点

Linux 版から Chrome の DevTools Protocol (CDP) を PicoRuby で動かす確認
(2026-10-28、親) で見つけた gem の欠点を直す。経緯と症状は
doc/reference/picoruby_upstream_pr_candidates.md の「2026-10-28 追加」の 2 節。

作業ブランチは fmruby-core の `feature/linux-native` (本体 checkout、直列)。

## 直すもの

1. **Net::HTTP が、接続を切らないサーバで止まる**
   (`lib/patch/picoruby-net-http/mrblib/http_client.rb`、既にある patch に足す)
   - `read_response` のヘッダのループとチャンクのループの `@socket.read(8192)` を
     `readpartial(8192)` にする。
   - 本文の長さが分かっている `read(remaining)` と、接続が切れるまで読む枝は
     変えない。
   - 先頭の "Family mruby patch" のコメントに理由を 1 段落足す (英語)。
2. **WebSocket の握手が 101 の文言を決め打ちしている**
   (`lib/patch/picoruby-net-websocket/mrblib/client.rb` を新しく置き、Rakefile の
   patch の仕組みで上書きされることを確かめる)
   - 応答の 1 行目が `HTTP/1.1 101` で始まるかで判定する。
   - `Host` ヘッダに、既定 (ws 80 / wss 443) 以外のときはポートを付ける。
   - `Sec-WebSocket-Accept` の検証は今回は入れない。
3. **受信の見直しの間隔** (2 と同じ client.rb の `receive`)
   - `sleep_ms 100` を `sleep_ms 30` にする (ユーザ決定: 30 ms までは許容、それ
     より短くするのは実機の確認が要るのでしない)。
   - server.rb は変えない。

## 確認

- lib/ を変えたので、ビルドの前に `rake clean`。
- `rake regress` (落ちたものだけ見る)。`rake regress:size` (内蔵 RAM +0 のこと。
  mrblib の変更だけなので大きさの差は小さい見込み。差を report に書く)。
- ネイティブ版 (`rake native:core`、標準と `NATIVE_ENGINE=mruby` の両方) で:
  - `flash/app/test/net_test.app.rb` の HTTP・HTTPS・WS echo が今までどおり通る
    (L2n の確認の仕方: doc/linux_native/report/l2n.md)。
  - CDP の通し: Playwright の headless のブラウザ
    (`~/.cache/ms-playwright/chromium_headless_shell-*/*/chrome-headless-shell`)
    を**専用の `--user-data-dir` と空いたポート**で `--remote-debugging-port` 付きで
    起動し、`Net::HTTP.get_response` で `/json/list` を取り、
    `Net::WebSocket::Client` で `Runtime.evaluate`・`Input.insertText`・
    `Page.captureScreenshot` が返ること。**アプリの中での上書きなしで**通ること。
    親が試したときの検査アプリ (上書きあり) は親の作業場所にある。検査アプリは flash/ に入れず、`fmrb_native.rb up --user-dir <一時の場所>`
    の /home に置く。1 往復の時間 (前は約 100 ms) を report に書く。
  - 起動したブラウザは自分の pid だけ止める。
- sim (rake build:linux) でも net_test の WS echo を 1 回通す。

## 触ってよい範囲

- `lib/patch/picoruby-net-http/`、`lib/patch/picoruby-net-websocket/`、必要なら
  Rakefile の patch の対象の列挙。
- submodule (components/picoruby-esp32/picoruby) は直接編集しない。

## 止まる条件

- 直したことで既存の利用者 (net_test、天気アプリ、アプリの配布の取得、
  Asterism 以外の HTTP の利用) の動きが変わると分かったとき。
- 30 ms より短くしないと受け入れ条件が満たせないと分かったとき。
- 見立てと違う事実 (例: readpartial が SSLSocket で使えない) が出たとき。

## 守ること

- `rake clean_all` はしない。sudo しない。本当の HOME・/etc・/usr に書かない。
- tools/fmrb_web.rb が開いたブラウザ (pid 551298 前後) は閉じない。
- 終わったら作業ブランチにコミット (`picoruby` の領域名は履歴に合わせる。英文、
  Co-Authored-By を付ける)。push はしない。
- report は `doc/linux_native/report/net1.md`: 計測値 (1 往復の時間、大きさの差)、
  見立てと違った点、撤回した仮説、残件。
