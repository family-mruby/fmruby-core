# WS0〜WS3 報告: ESP32 の Wi-Fi の画面と、覚えたネットワークへ自分でつなぐ

> 状態: 完了 | 更新: 2026-11-12 | ネイティブ版の設定アプリの Wi-Fi のページを WifiPageMixin に出し、ESP32 では内蔵アプリ default/wifi として Network の画面から開く。wifi_task に低い優先度の管理タスクを足し、探す・つなぐ・切る・忘れるの口と、見えている中でいちばん強い覚えたネットワークへ自分でつなぐ動きを入れた。wifi.toml は [[networks]] で 5 つまで (前の [wifi] の 1 組も読む)。NARYAv4 の実機で、起動時の自動接続・画面・選んだものが失敗したときの戻り・忘れるまで確認。実在の 2 つの AP の間の切り替えはユーザの確認待ち

## 1. WS0: 調べたこと

### 今の作り (変更前)

- `main/drivers/wifi/wifi_task.c`: `wifi_task_init` が `/home/wifi.toml` (無ければ `/etc/wifi.toml`) の `[wifi]` から
  `ssid`・`password`・`hostname` を 1 組だけ読み、`esp_wifi_set_config` してから `esp_wifi_start`。`STA_START` で
  `esp_wifi_connect`、切れたら esp_timer で 1 秒から 30 秒まで倍にしながらつなぎ直す。`enable = false`・ファイル無し・
  ssid 無しなら始めない (`FMRB_ERR_NOT_FOUND`)。
- 起動の呼び出し: P4 (Tab5・NARYAv4) は `boot.c` の `modern_radio_init_task` で BLE (C6 の SDIO を上げる) の後、
  `wifi_auto_start` (P4 の既定は true) なら `wifi_task_init`、続けて rd_task。S3 は `wifi_auto_start` (既定 false) で、
  BLE と同時には動かない (BLE が先に動いていれば始めない)。
- 秘密の扱い: `fmrb_secret_path.c` が `/home/wifi.toml`・`/etc/wifi.toml` と `wifi.toml` で始まる名前をパスワードの
  ファイルとして扱い、devctl の `/fs/get` は断り、BLE は受け付ける時間だけ読める。`wifi.toml.tmp` もこの規則に入る。
- ネイティブ版: `fmrb_platform_sys_t` に `wifi_list` (状態と一覧のテキスト)・`wifi_connect`・`wifi_disconnect` があり、
  Linux の表 (`platform_sys_linux.c`) だけが埋める。既定の表 (端末・sim・ブラウザ版) は空で、`FmrbApp.wifi_networks`
  は ""。デスクトップの Network の画面は `wifi_networks != ""` のときだけ「Wi-Fi...」のボタンを出し、
  `FmrbApp.linux_settings("wifi")` で設定アプリ (`native/apps/linux_settings.app.rb`、ネイティブ版だけに入る
  バイトコード) を開く。
- `FmrbApp.linux_settings` はネイティブ版にしか無い。チェックポイントの回帰 (checkpoint_sim/board/web) は
  「この口が無い」ことを前提にしているので、端末に足してはいけない。

### P4 の esp_hosted での探索

- esp_hosted 1.4.0 の `esp_wifi_scan_start`・`esp_wifi_scan_get_ap_records`・`esp_wifi_scan_stop` は RPC で C6 に渡る
  (`host/api/src/esp_wifi_weak.c` → `rpc_wrap.c`)。`WIFI_EVENT_SCAN_DONE` も C6 から P4 の既定のイベントループへ
  届く (`rpc_wrap.c` の `_h_event_wifi_post`)。記録の取り出しは 1 回の RPC で全部 (応答の領域は esp_hosted が取る)。
- 費用: P4 側は記録の受け皿 (`wifi_ap_record_t` 約 80 B × 48、後述) と、見せる一覧 (SSID・強さ・鍵の有無 × 24) を
  PSRAM に置くだけ (static 約 7 KB、すべて PSRAM)。電波の仕事は C6。BLE と同時に探せる (C6 は共存)。
- 探すのは非同期 (`esp_wifi_scan_start(NULL, false)`) にし、SCAN_DONE で記録を読む。待つのは管理タスクだけ。

### S3・Tab5・P4 の違い

| | S3 (NARYAv3) | Tab5 | NARYAv4 (P4-Nano) |
|---|---|---|---|
| 電波 | 自前 (esp_wifi) | C6 (esp_hosted) | C6 (esp_hosted) |
| BLE と同時 | 不可 (どちらか 1 つ) | 可 | 可 |
| `wifi_auto_start` の既定 | false | true | true |
| 設定の保存 | 自前の NVS に書かないよう RAM にした (下) | C6 の側 | C6 の側 |

### ページを共通にできるか

- `linux_settings.app.rb` の Wi-Fi のページは、`FmrbApp.wifi_networks` のテキストを読んで描くだけで、NetworkManager に
  直接触れていない。同じテキストを端末が返せば、そのまま使える。Bluetooth・画面のページと共通の小道具 (選択・
  スクロールの印・文字) もあるので、それも含めて mixin に出した。
- 作り: ESP32 の内蔵アプリの組み立て (`compile_ruby_to_bytecode.cmake`) は `default_app/<名前>/` の .rb を前に
  つなぐ。ネイティブ版の設定アプリの組み立て (`rake native:mrb`) にも、hosted と同じ `# combine:` の行を読ませた。

## 2. WS1: ESP32 の口と、複数を覚える wifi.toml

### wifi.toml の形 (`main/drivers/wifi/wifi_conf.c`・`.h`)

```toml
[wifi]
enable = true
hostname = "..."        # 任意
[[networks]]            # 新しいものが先、5 つまで
ssid = "..."
password = "..."        # "" か省略で鍵なし
hidden = true           # 任意: 隠れた SSID。名前で試す
```

- 前の形 (`[wifi]` の中の `ssid`・`password` 1 組) も読み、1 つ目のネットワークにする。`[wifi]` があるときは
  今までどおり `enable = true` が要る (無ければ Wi-Fi は始めない)。`[wifi]` の無い、`[[networks]]` だけのファイルは有効。
- 6 つ目以降・同じ名前の 2 度目・長すぎる SSID (32 B 超)・パスワード (64 B 超) は落とす。
- 書くと必ず新しい形 (コメントは残らない)。`/home/wifi.toml.tmp` に書いて名前を変える (途中で落ちても古いか新しいか
  どちらか)。書く先はいつも `/home/wifi.toml`。`/etc/wifi.toml` から読んでいたときは、その中身も一緒に `/home` へ
  移る (`/home` があると `/etc` は読まれないため、移さないと失う)。
- 純粋な C で、ホストの試験 `rake wificonf:test` (`test/wifi_conf/main.c`、48 件。前の形・並び・上限・忘れる・
  書いて読み戻す・エスケープ) を `rake test` に入れた。

### 管理タスク (`main/drivers/wifi/wifi_task.c`)

- `wifi_mgr` タスク: 優先度 3、スタック 6 KB を PSRAM (`FMRB_WIFI_MGR_TASK_*`、漢字の引きと同じ扱い)。
  ファイルの読み書きは hw_proxy に回る (PSRAM のスタックから)。S3 では `esp_wifi_set_storage(WIFI_STORAGE_RAM)` に
  して、このタスクから NVS (flash) に書かないようにした (設定は毎回 wifi.toml から入れるので失うものは無い)。
- イベントの受け手 (イベントループのタスク) はフラグを立ててタスクを起こすだけ。記録の読み出し・ファイル・
  次の試行はすべて管理タスク。前の esp_timer によるつなぎ直しは無くした (管理タスクが受け持つ)。
- 新しい static はすべて `EXT_RAM_BSS_ATTR` (PSRAM)。管理タスクを起こすのは、もとからある event group
  (`WIFI_EV_GOT_IP` と同じもの) のビット。初めはタスク通知 (`xTaskNotifyWait`) と static の錠
  (`xSemaphoreCreateMutexStatic`) で書いたが、S3 では FreeRTOS が内蔵 RAM に置かれるため、まだ使っていなかった
  この 2 つが入るだけで NARYAv3 の DIRAM が +192 B 増えた (regress:size で FAIL)。すでに使われている
  event group と `xSemaphoreCreateMutex` (動的。実行時に内蔵 RAM の heap を約 80 B) に替えた。
- 口 (`fmrb_platform_sys_t` の既定の表、`FMRB_HAS_WIFI` のとき):
  - `wifi_list` → `wifi_list_text`: `wifi\tok\t<使用中>\t<scanning|connecting>\t<結果>\t<名前>\tforget` と
    `ap\t<ssid>\t<強さ 0-100>\t<鍵>\t<使用中>\t<保存>\t<届く>`。強さは RSSI から (-100 dBm で 0、-50 dBm 以上で 100)。
    使用中を先頭、あとは強い順。覚えていて最後の探索で見えなかったものは末尾に `\t0` (届かない) で並べる
    (忘れられるように)。始まっていないときの状態は `disabled` (wifi_auto_start・enable)・`ble` (S3 で BLE が電波を
    使っている)・`failed`。パスワードは決して出さない。
  - `wifi_connect` / `wifi_disconnect` / 新設の `wifi_forget`: 要求を PSRAM の受け皿に置いてタスクを起こし、待たない。
  - Ruby: `FmrbApp.wifi_forget(ssid)` を足した (mruby の binding・Spinel の基底 (false)・回帰の代役)。ネイティブ版の
    表は `wifi_forget` を持たないので false。
- 探す: `esp_wifi_scan_start(NULL, false)`、SCAN_DONE で `get_ap_num` → 48 件まで読み (失敗したら 24 件で読み直す)、
  名前ごとに一番強いものへまとめ、**覚えているものを必ず残して**残りを強い順に 24 件まで (混んだ場所で覚えている
  ものが一覧から落ちて、つなぎに行けなくなるのを防ぐ。実機の部屋で 35〜41 台見えた)。15 秒で SCAN_DONE が来なければ
  失敗として扱う。
- つなぐ (画面から): 覚えていてパスワードを打っていなければ覚えたものを使う。覚えていない鍵ありで空なら
  `need_password`。つながって IP が来たら、新しいもの・パスワードを打ち直したものだけファイルに書く
  (同じなら書かない。flash の書き込みを減らす)。失敗したら結果 (`auth_failed`・`no_network`・`failed`) を残して、
  覚えたものの自動の接続へ戻る (選び間違いで端末が取り残されない)。20 秒で IP が来なければ失敗。
- 切る: その後は自動でつなぎに行かない (次にネットワークを選ぶか、起動し直すまで)。
- 忘れる: ファイルから消す (つながっている最中ならそのまま。次からは選ばない)。

### 起動の変化

- `wifi_auto_start` が真で、ファイルが無い・ネットワークが 0 のときも局 (STA) を始める (前は始めなかった)。画面から
  足せるようにするため。`enable = false` は今どおり始めない。
- 起動時は「探す → いちばん強い覚えたものへ」になったので、つながるまで 5 秒ほど遅くなった (実機: 前は STA 開始から
  約 7 秒で IP、今は約 12 秒。探索に約 5 秒)。

## 3. WS2: ページを共通にし、Network の画面から開く

- `main/prebuild_scripts/default_app/wifi/wifi_page.rb` (`WifiPageMixin`): Wi-Fi のページの文字 (英日)・テキストの
  読み取り・選択・パスワード欄・描画。Bluetooth のページも使う一覧の小道具 (選択・スクロールの印・文字・ログの 1 行)
  もここ。定数は `self.class::ROWS` で読む (mixin の裸の定数は見つからないため)。
- ネイティブ版: `native/apps/linux_settings.app.rb` から Wi-Fi の部分を抜いて `include WifiPageMixin`。
  `# combine:` の行で mixin を前につなぐ (`rakelib/native.rake` の `native_regen_apps_mrb!` に、hosted と同じ
  `# combine:` の読み取りを足した)。ネイティブ版だけが出す値は変えていないので、画面と動きは同じ (新しい値の
  `disabled`・`ble`・`failed`・`forgotten`・7 つ目の欄は端末だけが出す)。
- 端末: 内蔵アプリ `default/wifi` (`default_app/wifi.app.rb`、280x200 の Window、ネイティブ版の設定の Window と同じ
  大きさ)。開いたときに 1 回探す。ボタンは つなぐ・切る・探す・忘れる (忘れるは覚えたものを選んだときだけ)。
  上の行に Wi-Fi と IP。
- デスクトップ: `network_dialog.rb` の「Wi-Fi...」ボタンは今までどおり `wifi_networks != ""` のときだけ出る。
  押したとき、端末 (`FmrbConst::PLATFORM == "esp32"`) は `spawn_app("default/wifi")`、PC は今までどおり
  `FmrbApp.linux_settings("wifi")`。**端末に `linux_settings` を足さなかった** (チェックポイントの回帰が「無い」ことを
  前提にしている)。
- sim・ブラウザ版は `wifi_networks` が "" のままなので、ボタンは出ない (画面の基準は変わらない)。
- Spinel のデスクトップ (`FMRB_APP_ENGINE_DESKTOP=spinel`、標準ではない) は `wifi_networks` を "" で答えるので、
  ボタンは出ない。要るなら Spinel の基底にも FFI を通す (今回はしていない)。
- 案内: `flash/usr/share/doc/wifi.md`・`wifi_ja.md` (画面の使い方、自分でつなぐ順、wifi.toml の書き方と隠れた SSID)。
  `home.md`・`home_ja.md` の一覧に足した。

## 4. WS3: 自分でつなぐ

- 起動時 (STA_START) と、つながっていたのが切れたとき (利用者が切ったのでなければ) に探し、覚えたもののうち
  見えているものを強い順、次に `hidden = true` のものを名前で試す。どれもだめなら待って探し直す。待ちは 5 秒から
  倍々で 2 分まで、つながれば 5 秒に戻る。画面で「探す」を押すと待ちを打ち切って探す。
- 探索が 1 台も返さなかったときは探索を信じず、覚えたものを全部ファイルの順に試す (探索ができない電波でも
  前と同じくつながるように)。
- 鍵の無いネットワークも、覚えていれば候補に入る。
- 探索・接続は管理タスク (優先度 3) と電波の側 (S3 の Wi-Fi のタスク、P4 は C6) で行い、アプリ・デスクトップの
  タスクは要求を置くだけ。実機では、失敗から戻るまでの約 20 秒の間も Wi-Fi の Window の 1 秒ごとの読み取りが
  続き、状態 (connecting → scanning → connecting → 接続) を順に表示した (ログの `settings: wifi` の行)。入力の
  遅れの数値 (`spx: hid_lat`) は測っていない。

## 5. 確認したこと

### 回帰の道具

| 道具 | 結果 |
|---|---|
| `rake regress` | host 26 組すべて通過 (新しい `wificonf:test` を含む)、描画の記録 229 件すべて一致 |
| `rake regress:screens` | 33 件すべて一致 (modern 11・retro 9・web 6・native 3・native-keys 4)。基準は変わらない (sim・ブラウザ版はボタンを出さず、ネイティブ版の場面は設定の Window を開かない) |
| `rake regress:size` (NARYAv3・TAB5) | 内蔵 RAM の増分なし: NARYAv3 DIRAM -56 B、TAB5 DIRAM -360 B (基準 4e7e7e61 との差。flash の増分は基準からの間の他の変更を含む)。1 回目は NARYAv3 DIRAM +192 B で FAIL (下記の FreeRTOS の口)、直して 2 回目で通過 |

### NARYAv4 (P4-Nano) の実機

`.env` を NARYAv4 にして `rake clean_all` → `rake build:esp32`、MCP の flash (`app_only: true`) で 3 回焼いた (2 回目は
探索の件数の直し、3 回目は起こし方を event group に替えた版。下の表は 1・2 回目、3 回目は起動と画面を見直した)。`.env` は NARYAv3 に戻した。storage の変更 (`/usr/share/doc/wifi*.md`・`home*.md`) は
`tools/fmrb_rd_fs.rb` で put した。

| 確かめたこと | 結果 |
|---|---|
| 起動 (前の形の `/etc/wifi.toml` だけ) | `Settings from /etc/wifi.toml: 1 saved network(s)` → 探索 (35〜41 台) → `joining ... (saved)` → IP。クラッシュの印なし |
| Network の画面 | 「Wi-Fi...」ボタンが出る (Refresh・Wi-Fi...・Close) |
| Wi-Fi の画面 | 開くと探して、使用中が先頭 (`>`・saved・錠)、残りが強い順。「Scanning...」が終われば消える |
| 使用中のものを選んで「つなぐ」 | 切って同じものへつなぎ直す (理由 8 の切断 → `chosen on the page` → IP)。ファイルは書かない |
| 覚えていない鍵ありを選ぶ | パスワード欄が出て、打つと * が増える。「忘れる」は出ない |
| 届かない保存済みを足す (下記) → 再起動 | `Settings from /home/wifi.toml: 2 saved network(s)` → 見えている方へつながる |
| 一覧の末尾 | 届かない保存済みが、棒なし・薄い名前・saved で並ぶ |
| 届かないものを選んで「つなぐ」 | 理由 201 で失敗 → 「... が見つかりません」→ 探し直して元のネットワークへ戻る (約 20 秒で IP) |
| 「忘れる」 | `1 network(s) saved in /home/wifi.toml`、一覧から消える。書いたファイルで再起動してもつながる |
| 片付け | 試験で作った `/home/wifi.toml` を消し、再起動で `/etc/wifi.toml` から前と同じくつながることを確認。`/home` の一覧は試験前と同じ。試験用アプリ (`/app/test/wifitest_*`) は消した |

- 届かないネットワークの足し方: 端末の上で動く試験用アプリが、今読んでいるファイル (`/etc/wifi.toml`) の中身に
  `hidden = true` の 1 組 (`fmrb-ws-test-absent`) を足して `/home/wifi.toml` に書いた (今の設定は消さず、足すだけ)。
  遠隔から `/home/wifi.toml` は書けない (秘密の規則) ので、この形にした。
- 「切る」は試していない (押すと次の選択か再起動までつながらず、遠隔から戻せないため)。
- 内蔵 RAM (実行時): 起動の M1 (`wifi_ready`) は 209,064 (前の起動は 209,756)。10 秒周期の `IRAM free` は 12 秒の時点で
  211,4xx (前は 211,8xx)。差は 0.4〜0.7 KB で、起動直後に探索の RPC が走っているかどうかで動く。22 秒の時点では
  209,672 と下がっていない。static は下の regress:size。

### sim・ネイティブ版

- sim (Retro 320x240): Network の画面は今までどおり (ボタンなし)。`default/wifi` を debug の spawn で開くと
  280x200 の Window が画面に収まり、4 つのボタンが並ぶ (sim では wifi_networks が "" なので一覧は空)。
- ネイティブ版 (自分の `--user-dir` を scratchpad に作って起動。`~/.local/share/family-mruby-dev` には触れていない):
  Network の画面の「Wi-Fi...」で設定の Window が開き、Wi-Fi・Bluetooth のページの切り替え、探す、↓・Enter で
  エラーなし。ここの WSL には NetworkManager が無いので「NetworkManager is not running」の表示 (前と同じ)。
  NetworkManager のある GPD での動きは確かめていない。

## 6. ユーザに確かめてほしいこと

1. **実在の 2 つの AP の間の切り替え** (WS3 の本題): 2 つを覚えさせ (画面からつないで覚える)、つながっている方の
   AP を切る・離れると、もう一方へ自分でつなぐこと。戻ったときに、切れない限りはそのまま (強い方へ乗り換えは
   しない。つなぐのは起動時と切れたとき) であること。
2. **画面から新しいネットワークへパスワードを打ってつなぐ** (スマートフォンのテザリングなど): つながって
   `/home/wifi.toml` に覚えること。パスワードを間違えたときに「パスワードが違うようです」が出て、元のネットワークへ
   戻ること。
3. **Tab5 と S3 (NARYAv3)**: 焼いていない。S3 は `ble_auto_start = false`・`wifi_auto_start = true` で Wi-Fi が動く構成の
   ときだけ一覧が出る (BLE が動いていれば「電波は Bluetooth が使っています」)。S3 の管理タスクは PSRAM のスタックで
   esp_wifi を呼ぶので、S3 の実機での探索・接続・ファイルの書き込みは確かめたい。
4. **GPD (ネイティブ版、NetworkManager あり)** の Wi-Fi のページが前と同じに動くこと。
5. 「切る」を押した後、次にネットワークを選ぶまで自分でつながないこと。

## 7. 気づいたこと・残り

- BLE の Web コンソールの Settings は wifi.toml を前の形 (1 組) で書き直すので、覚えた他のネットワークは消える
  (`tool/web/js/settings.js` の `settingsWifiToml`)。案内には書いた。直すなら Web コンソールで既存の `[[networks]]` を
  残して足す形にする (今回はしていない)。
- 同じ Wi-Fi の Window を 2 つ開ける (Network の画面から 2 回押すと 2 つ)。ネイティブ版は 1 つに絞っている。
- 「忘れる」の後、選択が隣のネットワークへ移り、それが鍵ありで覚えていないとパスワード欄が出る。
- 起動時の接続は探索の分 (約 5 秒) 遅くなった。覚えているのが 1 つで隠れていなければ、探さずにつなぐ近道もありうる
  (今回は「強さで選ぶ」を全部の場合にそろえた)。
- Spinel のデスクトップではボタンが出ない (上記)。

