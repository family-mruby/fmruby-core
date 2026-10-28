# NET1 報告: Net::HTTP と WebSocket の 3 つの欠点

> 状態: 完了 | 更新: 2026-10-28 | 指示書は instruction_net1.md。ブランチは feature/linux-native (未 push)

## したこと

| 直したもの | 場所 | 中身 |
|---|---|---|
| 1. Net::HTTP が接続を切らないサーバで止まる | `lib/patch/picoruby-net-http/mrblib/http_client.rb` (既存の patch に足した) | `read_response` のヘッダのループとチャンクのループの `@socket.read(8192)` を `read_some` (`readpartial(8192)` で、EOFError を nil に読み替える) にした。`read` は EOF で nil を返すが `readpartial` は EOFError を上げるので、読み替えないとループの終わり方 (`break unless line`) が変わる。`read(remaining)` と、切れるまで読む枝はそのまま。先頭のコメントに理由を 1 段落足した |
| 2. WebSocket の握手の 101 の決め打ち | `lib/patch/picoruby-net-websocket/mrblib/client.rb` (新規。上流の写しに手を入れた) | 応答が `HTTP/1.1 101` で始まるかで判定。`Host` は既定のポート (ws 80 / wss 443) 以外ならポートを付ける。`Sec-WebSocket-Accept` の検証は入れていない |
| 3. 受信の見直しの間隔 | 同じ client.rb の `receive` | `sleep_ms 100` → `sleep_ms 30`。server.rb と `close` の待ち (100 ms × 10) は変えていない |
| patch の対象 | `rakelib/setup.rake` | client.rb を写す 1 行と説明を足した (patch の列挙は Rakefile でなく rakelib/setup.rake にある) |

`rake setup` 後、submodule 内の client.rb が patch と一致すること (diff で差なし) を確認した。

## 確認

- `rake clean` → `rake regress`: host 19/19、描画 205/205。
- `rake regress:size` (S3 = NARYAv3、P4 = TAB5)。基準 (6f7b5848) が古く、
  間の commit の差が混ざるので、**同じ HEAD で patch なし・ありを続けて 2 回**
  作って比べた。

  | 端末 | 内蔵 RAM | アプリのイメージ | libmruby.a (flash) |
  |---|---|---|---|
  | NARYAv3 | DIRAM・IRAM・RTC すべて +0 | 4,579,280 → 4,579,408 (+128 B) | +134 B |
  | TAB5 | DIRAM・HP/LP すべて +0 | 6,761,568 → 6,761,680 (+112 B) | +121 B |

- ネイティブ版 (`rake native:core`、標準と `NATIVE_ENGINE=mruby` の両方):
  - `test/native/net/l2n_check.rb --only http` (L2n の確認の仕方): 両構成とも 6/6。
    HTTP 5-6 ms、HTTPS (example.com) 89-96 ms、自己署名は拒否、名前解決、
    WS echo 133 ms (L2n のときは 147-152 ms。中身は接続+握手+往復+close で、
    close の待ちが大半)、burst_ms 2。
  - CDP の通し: Playwright の chrome-headless-shell (1243) を専用の
    `--user-data-dir` (scratchpad) とポート 9351 で起動し、検査アプリを
    `fmrb_native.rb up --user-dir <scratchpad>` の `/home` に置いて起動した。
    **アプリの中での上書きなし**で、`Net::HTTP.get_response(URI.parse(".../json/list"))`
    が `200 448B` 3 ms、`Net::WebSocket::Client` で `Runtime.evaluate` (1+2 = 3)・
    `Input.insertText` (input の value が入力した文字列になる)・
    `Page.captureScreenshot` (base64 6.6 KB) が返った。両構成とも同じ。
  - 追加で、手元の小さなサーバで Net::HTTP の 3 つの枝を見た:
    チャンク形式で接続を切らない (前なら時間切れまで止まる枝) → `"hello world"` 403 ms
    (サーバ側の 0.2 s × 2 の間を含む)、Content-Length なしで切る → 本文全部、
    Content-Length の本文が 2 回に分かれて届き切らない → 本文全部。
- sim (Modern = TAB5 の .env、`rake build:linux`): `flash/app/test/net_test.app.rb`
  の HTTP (200 577B 31 ms)・HTTPS (200 577B 371 ms)・WS ECHO (ok 143 ms) が通った。
  WS の相手は PC の 0.0.0.0:8765 に立てた Ruby の echo。サーバが受けた
  `Host` は `host.docker.internal:8765` (ポートつき)。送ってから受け取るまで 30 ms。
  ビルドは `tools/regress/build_tree.rb` の退避 (stash!) で build/ を空けてから行い、
  `ruby tools/regress/size.rb --restore` で戻した (build/ は元の NARYAv4 (esp32p4))。

## 1 往復の時間 (CDP、ネイティブ版)

| | 1 回ごと | 20 回の平均 |
|---|---|---|
| 前 (直す前のビルド + 親の検査アプリの上書き) | 70・101・98・101・108 ms | 99 ms |
| 後 (標準) | 31・31・29・31・39 ms | 29 ms |
| 後 (全 mruby) | 30・31・31・30・38 ms | 30 ms |

`Page.captureScreenshot` が他より 8 ms ほど長いのは返す量 (6.6 KB) の分。
1 往復はほぼ見直しの間隔そのもの (Chrome の答えは 1 ms 未満で着いていて、
受け取る側が次に見るまで待っている)。

## 見立てと違った点

- なし (readpartial は TCPSocket・SSLSocket の両方にあり、HTTPS でも通った)。
  ただし `readpartial` は EOF で nil でなく EOFError を上げるので、置き換えは
  1 行の差し替えでは済まず、読み替える小さなメソッドを足した。
- patch の列挙は Rakefile でなく `rakelib/setup.rake` の `setup` タスクにある。

## 撤回した仮説

- なし。

## 残件

- `client.rb` は上流のファイル丸ごとの写し。submodule (picoruby) を進めたら
  写し直すか、上流に PR が入れば patch ごと消す (候補は
  doc/reference/picoruby_upstream_pr_candidates.md の 2026-10-28 の 2 節)。
- `Sec-WebSocket-Accept` の検証は無いまま (上流に出すなら別の話)。
- `close` の待ち (100 ms 刻みで最大 1 s) は変えていない。WS の通し 1 回が
  130-140 ms かかるうちの大半はここ。指示の範囲外なので触れていない。
- 30 ms の見直しは実機 (S3・Tab5) では確かめていない。ほかのタスクへの
  負担 (受信待ちのアプリが 1 秒に 33 回起きる) は実機で見る必要がある。
