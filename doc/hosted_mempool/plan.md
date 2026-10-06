# ESP-Hosted の転送バッファの溜め込みをやめる

> 状態: 進行中 | 更新: 2026-10-06 | P4 の WiFi の通信で内蔵 RAM が戻らなくなる件。ESP-Hosted の mempool を切り (CONFIG_ESP_HOSTED_USE_MEMPOOL=n)、速さと細切れへの影響を測ってから採用を決める

## 目的

P4 の機種 (Tab5、P4-Nano) では、WiFi のチップ (ESP32-C6) とのやり取りを ESP-Hosted が SDIO で受け持つ。
ESP-Hosted は転送用のバッファ (1 個 1,664 B、DMA で読み書きできる内蔵 RAM) を使い終わっても解放せず、
上限なしで溜めて使い回す (`CONFIG_ESP_HOSTED_USE_MEMPOOL=y` のとき、`mempool_free` が一覧に積むだけ)。
そのため、内蔵 RAM がこれまでで一番多く同時に通信した瞬間の分だけ減ったままになり、起動し直すまで戻らない。

- 実測 (ruby_asterism/report/z3.md 12 章): peer 10 台の試験のあと約 23 個 (約 38 KB)、遠隔のファイル転送
  64 KB だけで 3 個。zenoh に限らず、P4 の WiFi の通信すべてで起きる。理屈の上では約 120 個 (約 200 KB) まで。
- 内蔵 RAM は増やさない方針 (reference/internal_ram_budget.md) に反する。

## 方針 (2026-10-06 ユーザ決定)

- 案 1 を採る: `CONFIG_ESP_HOSTED_USE_MEMPOOL=n` を `config/sdkconfig.defaults.p4` と
  `config/sdkconfig.defaults.naryav4` に入れる。使い終わったバッファは毎回ヒープに返る。
- 採用は測ってから決める。心配は 2 つ: 通信ごとの確保と解放による速さの低下と、内蔵 RAM の細切れ
  (一番大きく取れる空きが小さくなること。アプリ 1 本の起動に 24 KB の連続した領域が要る)。
- 退けた案: ESP-Hosted のコードに上限を足す (版を C6 のファームに合わせて固定しているので、手を入れる
  危険と手間が大きい)、受け入れて見込む (上限が約 200 KB で大きすぎる)。

## 受け入れ条件

1. P4-Nano で、同じ通信の負荷のあと (3 分おく)、内蔵 RAM が起動直後の値に戻る。
2. 速さ: 遠隔の画面の取り込み (MJPEG `/stream`、H.264 `/ws_video`) の fps、ファイル転送の速さが、変更の前と
   比べて大きく落ちない (数字を並べてユーザが判断する)。
3. 細切れ: 負荷の後の一番大きく取れる内蔵 RAM の空き (largest free block) が、アプリの起動に足りる。
4. 起動時の内蔵 RAM (`M1|`、`IRAM free:`) が増えない。BLE (C6 経由) が今までどおり使える (起動ログ)。
5. Tab5 での確認は後日の実機確認に回す (device_check_backlog)。

## 未確定事項

- 採用するか (測った数字を見てユーザが決める)。
