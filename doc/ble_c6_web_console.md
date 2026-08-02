# Tab5 (ESP32-P4) BLE有効化 — Web コンソールの Modern 対応

2026-07-05 着手。Web Bluetooth ベースの「Family mruby Console」(tool/web/) を
Tab5 (Modern) でも使えるようにするための、ESP32-C6 コプロセッサ経由 BLE の
設計・実装記録。

## 背景

- 既存資産: tool/web/ (Web Bluetooth クライアント、ファイル管理+ログ閲覧) と
  main/drivers/ble/ble_task.c (NimBLE GATT サーバ、FMRB File Service、
  COBS フレーミング)。Retro (ESP32-S3) では動作済み
- ESP32-P4 は無線非搭載のため、P4 ビルドでは bt / ble_task.c を除外していた
- Tab5 の無線は ESP32-C6 コプロセッサが担い、P4 とは SDIO で接続される。
  esp_hosted はホスト側 NimBLE (host-only) に BT HCI を転送する vHCI 構成を
  サポートしており、これを使うと ble_task.c をほぼ無変更で流用できる

## 方針

ストック C6 ファームウェア (ESP32C6-WiFi-SDIO-Interface-V1.4.1、esp-hosted
slave ビルド) のままで BT HCI が通るかをまず検証し、通らない場合のみ C6
スレーブファームの書き換え (BT 有効の esp-hosted slave ビルド) を検討する。

## 確定済みの事実 (2026-07-05 調査)

- ble_task.c の初期化は nvs_flash_init -> nimble_port_init -> host 設定のみで
  esp_bt_controller_init を呼ばない → host-only 構成にそのまま流用可能
- esp-hosted 公式サンプル (host_nimble_bleprph_host_only_vhci) の手順:
  esp_hosted_connect_to_slave -> esp_hosted_bt_controller_init ->
  esp_hosted_bt_controller_enable -> nimble_port_init
- Tab5 の SDIO 結線 (M5Tab5-UserDemo の sdkconfig より):
  CMD=GPIO13, CLK=12, D0=11, D1=10, D2=9, D3=8,
  C6 リセット=GPIO15 (active low), 4-bit, 40MHz
- ピン競合なし: fmruby-core P4 ビルドで GPIO8-13,15 は未使用。
  Tab5 の microSD は GPIO39-44 の別スロット
- C6 の電源は PI4IO #2 (0x44) reg 0x05 bit0。display_p4 の tab5_power_on()
  が既に ON にしている → BLE 初期化は display ready 後に行う必要がある
- esp_hosted の BT 用 Kconfig: ESP_HOSTED_ENABLE_BT_NIMBLE /
  ESP_HOSTED_NIMBLE_HCI_VHCI (前提: BT_ENABLED && BT_NIMBLE_ENABLED &&
  !BT_CONTROLLER_ENABLED)
- esp-hosted slave の C6 デフォルト構成は CONFIG_BT_ENABLED=y +
  BT_CONTROLLER_ONLY=y → ストック fw でも vHCI が生きている可能性が高い
  (名称は "WiFi-SDIO" なので実機判定が必要)

## 実装内容

1. main/idf_component.yml: espressif/esp_hosted 1.4.0 +
   espressif/esp_wifi_remote 0.8.5 を target==esp32p4 限定で追加
   (UserDemo と同版に固定。C6 ストック fw V1.4.1 とのプロトコル互換を優先)
2. config/sdkconfig.defaults.p4: host-only NimBLE + esp_hosted SDIO/vHCI の
   設定ブロックを追加 (詳細は同ファイル参照)
3. main/drivers/ble/ble_task.c: CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE ガードで
   nimble_port_init の前に esp_hosted_connect_to_slave +
   esp_hosted_bt_controller_init/enable を挿入。fw バージョンをログ出力
4. main/CMakeLists.txt: P4 でも ble_task.c と bt/esp_hosted をビルド
5. main/boot/boot.c: FMRB_HW_MODERN ブランチで display_p4_is_ready() を
   待ってから ble_task_init() を呼ぶ (C6 電源が display task で入るため)

GATT サービス定義・COBS プロトコル・Web クライアント (tool/web/) は無変更。

## 検証手順

1. ビルド: managed_components に esp_hosted/esp_wifi_remote が入ること、
   リンクエラー (ble_transport_to_ll_* 未解決等) がないこと
2. ストック C6 ハンドシェイク: ブートログで esp_hosted の INIT event /
   coprocessor fw version / capabilities、esp_hosted_bt_controller_init の
   成否、NimBLE host sync 到達を確認
3. Web コンソール接続: Chrome で tool/web/ を開き "FamilyMruby" に接続、
   Files タブ (ls/get/put) と Logs タブ (subscribe) を確認
4. 判定: HCI が通らなければストック fw は BT 非対応 → フォールバックへ

## フォールバック (ストック C6 が BT 非対応の場合): C6 ファーム更新手順

esp_hosted 1.4.0 の実装確認済み (2026-07-05):

1. **SDIO 経由 slave OTA (第一候補、分解不要)**
   - `esp_hosted_slave_ota(path)` は P4 のローカルファイルを読み、RPC
     (ota_begin/write/end) で C6 の OTA パーティションへ書き込む実装
     (host/drivers/rpc/wrap/rpc_wrap.c:857、OTA_FROM_WEB_URL 無効時)。
     完了後 C6 は新ファームで再起動する
   - slave ソースは取得済みコンポーネントに同梱:
     managed_components/espressif__esp_hosted/slave/ を
     `idf.py set-target esp32c6 && idf.py build`
     (C6 デフォルトで BT_ENABLED=y + BT_CONTROLLER_ONLY=y)
   - app イメージを P4 の /flash に配置し、一時トリガー (起動時の
     ファイルチェック等) から esp_hosted_slave_ota を呼ぶ
   - 前提: ストック fw のパーティションテーブルに ota スロットがあること。
     ota_begin が失敗する場合は物理書き込みへ
2. **物理書き込み (最終手段)**
   - `esptool.py write_flash 0x0 <full image>.bin`
     (tmp/M5Tab5-UserDemo/platforms/tab5/wifi_c6_fw/ にストックイメージと
     flash.sh がある)
   - Tab5 の C6 は USB 非接続のため、基板上のパッドに USB-TTL を接続して
     ダウンロードモードに入れる必要がある。パッド位置・手順は M5Stack の
     復元ガイド "M5Tab5 C6 WiFi firmware restore" を参照

## リスク / 注意

- 内部 RAM: NimBLE host + esp_hosted で +80-100KB 程度の見込み。ブート時の
  heap ログで確認する
- nimble_host タスク (優先度ブースト済み) と display/audio タスクの競合。
  症状が出たらコア割り当て/優先度を調整
- Retro への回帰: 変更はすべて P4 ガード内。sdkconfig.defaults.n16r8 無変更

## 実装時に判明した事項 (2026-07-05)

- esp_hosted 1.4.0 の API は main ブランチのサンプルと異なる:
  esp_hosted_connect_to_slave / esp_hosted_bt_controller_init/enable は
  存在しない。1.4.0 では esp_hosted_init() がトランスポート初期化と
  hci_drv_init() を行い、vHCI (ble_transport_to_ll_* / ble_transport_ll_init)
  は host/drivers/bt/vhci_drv.c が提供する。BT コントローラの明示的な
  init/enable RPC は不要で、NimBLE ホスト起動時に vHCI 経由で HCI が流れる
- esp_hosted_get_coprocessor_fwversion(&ver) (構造体版) を疎通確認を兼ねて
  呼んでいる
- Kconfig 実名の注意:
  - ESP_HOSTED_ENABLE_BT_NIMBLE は BT_NIMBLE_TRANSPORT_UART=n が前提条件。
    IDF デフォルトが y のため、sdkconfig.defaults.p4 に明示的に n を設定
  - SDIO D1 ピンの設定シンボルは ESP_HOSTED_SDIO_PRIV_PIN_D1_4BIT_BUS。
    ESP_HOSTED_SDIO_PIN_D1 は派生シンボルで直接設定しても無視される
    (放置すると P4 デフォルトの 15 になり、リセット線 GPIO15 と衝突する)
- esp_hosted 1.4.0 は ble_transport_ll_deinit() を実装していないが、
  IDF 5.5 の nimble_port_deinit() が参照するため、ble_task.c に空スタブを
  追加した (本 FW では BLE は常駐なので deinit は実質未使用)
- sdkconfig.defaults の変更を反映するには生成済み sdkconfig (gitignore済み)
  を削除して再ビルドする

## 実測結果の記録

- [x] 初回実機テスト (2026-07-05): リブートループ発生 → 原因特定・修正済み
  - esp_hosted 1.4.0 はコンストラクタで自動初期化される (esp_hosted_host_init.c、
    app_main 前に "ESP-Hosted starting" が出る)。ble_task からの esp_hosted_init()
    呼び出しは不要
  - SDIO スレーブ接続は nimble_port_init() 内の ble_transport_ll_init()
    (vhci_drv.c:136) が transport_drv_reconfigure() を呼んで開始され、
    接続完了までブロックする設計
  - トランスポート確立前に RPC (fwバージョン取得) を発行すると、rpc_core の
    失敗経路がバグっており ("Uninitialized sem id 3" + 二重解放でヒープ破壊)、
    直後の malloc でクラッシュしリブートループになる
  - 修正: RPC を nimble_port_init() 成功後 (=transport up 後) に移動
  - 良い兆候: クラッシュ前に SDIO カード列挙 ("Name:/Type: SDIO") まで到達
    しており、ストック C6 は SDIO レベルで応答している
- [x] ストック C6 での BT HCI 応答 (2026-07-05): **成功、C6 ファーム書き換え不要と確定**
  - SDIO ハンドシェイク成功: INIT event 受信、capabilities=0xd
    ("HCI over SDIO" + "BLE only")、slave chip Id[12] (=C6)
  - ストック C6 fw バージョン: **1.4.1** (esp_hosted_get_coprocessor_fwversion)
  - vhci_drv: "Host BT Support: Enabled / BT Transport Type: VHCI"
  - NimBLE host sync 到達、BLE address 取得、"Family-mruby-XXXXXX" で
    アドバタイズ開始
- [x] Web コンソール接続 (2026-07-05): Chrome から接続成功。MTU 512 交渉、
  FS TX notify 購読、ログストリーム notify 動作を確認
- 起動シーケンスメモ: SDIO 接続 (約1.3秒) は nimble_port_init 内で行われ、
  ble_init タスク上でブロックするだけなので OS 起動には影響しない

## Debug パネル (ps / kill / spawn) — 2026-08-02 追加

Web コンソールの **Debug** タブから、実機の debugd を BLE 経由で叩ける。
python + bleak + usbipd の道具立てなしに ps / kill / spawn ができるので、
実機での kill 検証 (doc/app_kill_fix/) の手順が Chrome だけで完結する。

### 仕組み

- デバイス側は無変更。GATT のデバッグサービス (base UUID の末尾
  `...0005`、RX `...0006` / TX `...0007`) にファイルサービスと同じ GATT
  接続で相乗りする。別接続は張らない。
- フレーム形式は `main/drivers/debug/fmrb_debug_transport_ble.c` と同じ
  `COBS([u16 BE 本文長][msgpack 本文][CRC32 BE]) + 0x00`。CRC は長さ接頭辞を
  含む。ファイルサービスは長さ接頭辞を持たないので、フレーム組み立ては
  `js/debug.js` に別に置いてある (COBS と CRC32 は app.js のものを流用)。
- メッセージ層は `tool/debug/fmrb_dbg_client.py` と同じ msgpack:
  要求 `[0, seq, cmd, payload]` / 応答 `[1, seq, err, payload]`
  (`err != 0` は失敗) / イベント `[2, _, name, payload]`。
- msgpack は必要範囲だけの自前実装 (`js/msgpack.js`)。同じ要求について
  `fmrb_dbg_client.py` が組むフレームと**バイト単位で一致**することを
  確認済み (spawn 1 コマンドぶんを突き合わせ)。
- 応答が 5 秒来なければエラー表示に落とす。切断時は待機中の要求をすべて
  失敗させる。

### 使い方

1. 通常どおり **Connect**。デバッグサービスが見つかれば Debug タブの
   `Debug service:` が `connected` になる (無いファームでも
   ファイルコンソールは従来どおり動く)。
2. **ps** でタスク一覧。pid / 名前 / VM 種別 / 状態が出る。
3. 各行の **kill** ボタンで終了。カーネル (pid 0) とデスクトップ (pid 2) は
   落とすと OS が止まるのでボタンを出していない。
4. **Spawn** 欄にパス (例 `/app/test/busy.app.lua`) を入れて Spawn。
5. **Raw** 欄は今後 debugd にコマンドが増えたとき、UI を待たずに叩くための
   逃げ道。payload は JSON (空なら nil)。

### 実機確認の手順 (app_kill_fix Phase 2)

1. `while true do end` するだけの Lua アプリを一時的に置く
   (例 `/app/test/busy.app.lua`。コミットしない)。
2. Connect -> Debug タブ -> **ps** で一覧が出ることを確認。
3. Spawn 欄にそのパスを入れて **Spawn**。
4. ビジーループ中に **ps** を押し、**応答が返ること**を確認 (ここが本題。
   優先度修正前はここで無応答になる)。
5. その行の **kill** を押し、一覧から消えること、同じアプリを再度 Spawn
   できることを確認。
