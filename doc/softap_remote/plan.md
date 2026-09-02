# WiFi AP モードと携帯端末からの遠隔画面 (SoftAP + 認証 + iPhone ビューア)

> 状態: 計画済 | 更新: 2026-08-31 | 機体が自分で WiFi を張り、PC も家の WiFi も無い場所で iPhone のブラウザから遠隔画面を使えるようにする。設定だけで切替、共通鍵で守る

2026-08-31 起票。発端は doc/ruby_asterism/node_variants.md 3.5-3.6
(ヘッドレス機の「スマホが画面のマイコン」) だが、**Modern 全機種
(Tab5 / NARYA v4 / ヘッドレス P4) に効く独立した機能**なので別テーマに
する。Asterism の進捗には依存しない。

## 目的

- 機体 (Modern) が **自分で WiFi (SoftAP) を張り**、iPhone の Safari から
  遠隔画面 (remote desktop) で見て・触って・書ける。PC も家の WiFi も
  要らない。
- 切替は **設定ファイルだけ**で決める。タイムアウトやボタンでの自動切替は
  しない (ユーザ指定)。
- 出先で張る前提なので、**遠隔画面と開発用リモート制御を認証つき**にする。

## 方針

- **表示は既存の MJPEG 経路のまま**。平文 http では WebCodecs が使えず
  (doc/remote_desktop/design.md「WebCodecs と Secure Context」)、
  SoftAP の `http://192.168.4.1/` も平文なので、iPhone では最初から MJPEG
  一本。H.264 は対象外。https (自己署名) も本計画では扱わない。
- **機体側の追加は AP モードと共通鍵の 2 つだけ**。作業の本体はビューア
  (tool/web/remote/、EMBED_FILES で機体に埋込) の携帯端末対応。
- 入力の規約 (/ws のバイナリ: 絶対座標のマウス、HID scancode のキー) は
  変えない。機体はどの経路でも「USB キーボードが打った」のと同じものを
  受けるので、アプリは無改修。
- 既存の wifi_task (STA 専用、資格情報は /etc/wifi.toml) と rd_http
  (esp_http_server 1 本に remote desktop と dev_remote_ctl が相乗り) を
  土台にする。新しいサーバは立てない。

## 設定の形

モード (秘密でない) は system_conf、鍵 (秘密) は wifi.toml。

```toml
# config/system_conf_p4.toml (naryav4 / ヘッドレスも同じ)
[network]
wifi_mode = "sta"        # "sta" | "ap" | "apsta"
```

```toml
# /etc/wifi.toml (config/wifi_p4.toml から配置。git 管理外)
[wifi]                   # STA (既存)
enable = true
ssid = "..."
password = "..."
hostname = "fmruby"

[ap]                     # 新規
ssid = "fmruby-ap"
password = "8 文字以上"    # WPA2-PSK 以上。無ければ AP を起動しない
channel = 6
max_connection = 1

[remote]                 # 新規
token = "..."            # 遠隔画面と dev_remote_ctl の共通鍵
```

規則:

- `wifi_mode` の言うとおりに起動する。必要な鍵 (`[ap].password`) が無ければ
  **AP を起動せずログに出す**。既定の鍵で勝手に開けない。
- `apsta` は家用 (STA で家の網に居ながら自分の AP も張る)。出先は `ap`
  (知らない WiFi に加わる経路が構造的に無い)。
- `wifi_mode = "ap"` のとき `[remote].token` が無ければ遠隔画面と
  dev_remote_ctl を起動しない。`sta` / `apsta` でも token があれば要求する
  (家の LAN でも有効にしてよい。無効化は「token を書かない」で明示)。
- 切替はファイルを書いて再起動。書く手段は遠隔画面のネットワーク窓
  (network_dialog.rb)、tab5_fs put、ヘッドレスの初期設定は USB シリアル。

## スコープ

### 段階 A: AP モード (機体側)

1. system_conf の `[network] wifi_mode` と wifi.toml の `[ap]` を読む。
2. wifi_task に AP / APSTA の起動経路: `esp_netif_create_default_wifi_ap`、
   `WIFI_MODE_AP` / `WIFI_MODE_APSTA`、`wifi_ap_config_t` (authmode は
   WPA2-PSK 以上。C6 が AP で WPA3-SAE を張れるなら WPA3)。DHCP サーバは
   esp_netif の AP 既定。DNS は張らない。
3. mDNS を AP の netif でも広告する (`http://fmruby.local/`)。
4. `wifi_get_ip_str` 等の既存 API が AP の IP も返す (ネットワーク窓の表示、
   tab5_ip の解決に効く)。
5. APSTA のチャネル制約 (STA が繋いだチャネルに AP が追従する) を把握して
   `channel` の扱いを決める。

### 段階 B: 共通鍵 (機体側)

1. wifi.toml `[remote].token` を読み、rd_http の全ハンドラ (ビューアの
   静的ファイルを除く: /stream、/ws、/status、dev_remote_ctl の
   /app/* と /fs/*) で要求する。渡し方は WS は接続時の最初のフレーム、
   HTTP は `Authorization` ヘッダ または cookie (ビューアは最初に token を
   聞いて cookie に保持)。
2. 失敗 n 回で数十秒の締め出し (総当たり対策。httpd タスク 1 本なので
   単純なカウンタで足りる)。
3. tools/ の fmrb_rd_*.rb と MCP (tab5_*) が token を渡せるようにする
   (環境変数か設定ファイル)。token を要求する機体に対して従来の道具が
   黙って失敗しないこと。

### 段階 C: 携帯端末向けビューア (tool/web/remote/)

1. **触る操作**: touchstart / touchmove / touchend を mouse_button /
   mouse_move に写す。タップ = 左クリック、長押し = 右クリック、動かす =
   ドラッグ。`touch-action: none` と viewport `user-scalable=no` で
   ピンチとダブルタップ拡大を殺す。hover は無いので捨てる。
   ビューアが 426x240 の絶対座標に変換して送るので、Tab5 本体のタッチ
   (相対移動) と違って絶対位置でよい。
2. **ソフトキーボード**: 隠し textarea (autocorrect / autocapitalize /
   spellcheck を切る) の `beforeinput` で文字を取り、文字→HID+Shift の
   逆引き表で送る。iOS のソフトキーボードは KeyboardEvent.code が入らない
   ことが多いため。逆引き表は fmrb_input.rb が fmrb_keymap.c から作るのと
   同じ元表 (keyboard_layout に追従)。
3. **画面上のキー帯**: ソフトキーボードに無い Ctrl (1 回押しの粘着式:
   Ctrl → s で Ctrl+S)、Esc、矢印 4 つ、Tab、F5、Enter。
4. **BT 物理キーボード**は現行の keymap.js (code → HID) のまま効く。
   Ctrl+Space は iOS が入力ソース切替に横取りするので、かなモードの
   切替は指示器クリックか半角/全角キーで (案内に書く)。
5. 日本語は iOS の IME を使わず、機体のかなモード + 英語ソフト
   キーボードのローマ字で合成する。対象外と明記。
6. iPhone の Safari には要素の全画面が無いので、「ホーム画面に追加」
   (standalone) 用の manifest を 1 枚足す。横向きで幅 800-900 CSS px、
   426x240 の約 2 倍。
7. token の入力欄 (段階 B) と cookie 保持。

### 対象外

- https (自己署名) と H.264/WebCodecs の解禁。必要になってから。
- captive portal の検出応答を細工して自動でビューアを開く案 (その画面は
  機能制限つきの簡易ブラウザで、Safari で開かせる方が確実)。
- S3 (Retro): remote desktop が無い。
- iOS の IME を使った日本語入力。

## 受け入れ条件

1. `wifi_mode = "ap"` で起動した Tab5 / NARYA v4 に iPhone が WPA で
   接続し、Safari で `http://fmruby.local/` (だめなら IP) を開くと遠隔
   画面が MJPEG で出る (機体無改修の事前実験で Safari の multipart 対応を
   先に確かめる: 家の WiFi の Tab5 に iPhone から `http://<IP>/`)。
2. タップでメニューが開き、ランチャーからアプリが起動し、ドラッグで窓が
   動く。長押しで右クリック (ランチャー再スキャン) が効く。
3. ソフトキーボードでエディタに ASCII が打て、キー帯の Ctrl+S で保存、
   F5 で実行、Esc で戻れる。BT キーボードでは現行どおり全キーが効く。
4. token 未設定で `wifi_mode = "ap"` のとき遠隔画面が起動せず、ログに
   理由が出る。token 設定時、誤った token では /stream・/ws・/app/*・
   /fs/* の全部が拒否され、n 回失敗で締め出される。tab5_* と
   fmrb_rd_*.rb は token を渡して従来どおり動く。
5. `wifi_mode = "sta"` の既存動作 (家の WiFi、tab5_* の経路、H.264 の
   ビューア) に退行が無い。`apsta` で STA と AP の両方から届く。
6. モバイルバッテリ給電で 1 時間以上の連続使用 (数字は実測して report に)。

## 未確定事項

1. esp_wifi_remote (esp_hosted 1.4.0、C6 スレーブ) 経由で AP を張れるか。
   スレーブ FW は SoftAP を持ち、esp_wifi_remote の公開 API には
   `esp_netif_create_default_wifi_ap` / `WIFI_MODE_APSTA` /
   `WIFI_AUTH_WPA3_PSK` が写されているが、本プロジェクトでの実績はゼロ。
   **段階 A の最初に Tab5 で確定させる**。だめなら本計画は `sta` + token
   (段階 B・C) だけに縮む。
2. C6 の AP で WPA3-SAE が使えるか。使えなければ WPA2-PSK。
3. iPhone Safari で `<img>` の multipart MJPEG が 15fps で流れ続けるか
   (機体無改修の事前実験で分かる)。
4. AP の netif で mDNS が iPhone (Bonjour) から引けるか。引けなければ
   `192.168.4.1` を案内する。
5. 遠隔画面の入力遅延が「主画面」として耐えるか (Tab5 では補助だった)。
   打鍵は WebSocket で即時、見た目は MJPEG 15fps なので 70-150ms 程度の
   見込み。
6. ヘッドレス機で `wifi_mode` を初めて書く手段 (USB シリアル経由の
   ファイル書き込みが要るか、SD か)。ヘッドレス機の計画側で決める。

## 関連

- doc/remote_desktop/design.md: 遠隔画面の設計 (MJPEG / H.264、/ws の
  入力規約、Secure Context の制約)。
- doc/dev_remote_ctl/plan.md: /app/* と /fs/* (無認証で「信頼できる
  LAN 内」前提だった。段階 B で改める)。
- doc/ruby_asterism/node_variants.md 3.6: 発端の検討。
- main/drivers/wifi/wifi_task.c、main/drivers/remote_desktop/rd_http.c、
  tool/web/remote/、tools/fmrb_input.rb (逆引き表の元)。
