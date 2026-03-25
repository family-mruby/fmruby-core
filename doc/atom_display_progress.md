# ATOM Display (HDMI) 対応 進捗と計画

## 完了した作業

### 1. HW ターゲット切り替え基盤
- `FMRB_HW_TARGET` CMake 変数でビルド切り替え (WROVER / ATOM_DISPLAY)
- `config/sdkconfig.defaults.n16r8` / `n8r8` で sdkconfig 分離
- `config/system_conf_n16r8.toml` / `n8r8.toml` で設定分離
- `config/partitions_n16r8.csv` / `n8r8.csv` でパーティション分離
- Rakefile で `FMRB_HW_TARGET=ATOM_DISPLAY rake build:esp32` に対応
- `.env` の `FMRB_HW_TARGET` も反映
- `fmrb_pin_assign.h` で HW 別ピンアサイン

### 2. M5GFX / M5Unified 導入
- `idf_component.yml` に m5stack/m5gfx, m5stack/m5unified 追加
- `m5gfx_task.cpp` (C++) で M5Unified ベースの描画タスク実装
- M5AtomDisplay.h を M5Unified.h より前に include (atom_display config 有効化)
- M5Unified の `-Wno-error=maybe-uninitialized` 対応

### 3. BLE ファイルサービス
- `ble_task.c` に RX/TX characteristic 追加 (Web Bluetooth 対応)
- COBS + CRC32 プロトコルで file CD/LS/RM/GET/PUT
- `tool/web/index.html` Web Bluetooth ファイルマネージャ
- `tool/web_server.rb` HTTP サーバ (gem 不要)

### 4. HAL Link Local (Message Buffer)
- `fmrb_hal_link_local.c` - FreeRTOS Message Buffer ベースの HAL link 実装
- TX/RX 双方向 Message Buffer (コマンド送信 + ACK 応答)
- `fmrb_hal_link_local_receive_cmd()` / `send_response()` 拡張 API

### 5. GFX コマンド処理
- `m5gfx_task.cpp` で全 GFX コマンド対応 (描画、テキスト、Canvas 管理、カーソル)
- msgpack デコード + コマンドディスパッチ
- VERSION CHECK / INIT_DISPLAY への ACK 応答
- CREATE_CANVAS で canvas_id を ACK に含めて返却

### 6. I2C キーボードドライバ
- `i2c_keyboard.c` - I2C スレーブキーボード (0x5F) ポーリング
- 矢印キー → マウス移動変換
- `fmrb_host_send_key_down/up()` / `fmrb_host_send_mouse_move()` でホストに送信

### 7. boot.c リファクタ
- `init_hardware()` を Linux 版と ESP32 版に分離
- `FMRB_HW_ATOM_DISPLAY` で USB/WROVER 関連を条件分岐

## 現在の状態

### 直接描画モード (安定動作確認済み)
- `get_target()` が常に `g_display` を返す
- `startWrite()` で SPI バスを保持し続ける
- 各描画コマンドが FPGA に直接送信 (小パケット SPI、安定)
- **1 Window (system_gui) の描画は正常に動作**

### HDMI スケーリング設定 (検証中)
- `cfg.atom_display` で output_width/height, scale_w/scale_h を設定
- 320x240 論理解像度 + 1280x720 出力 + scale 4x3 を設定済み
- **表示の繰り返し・切れの問題が未解決**

## 未解決の課題

### 1. HDMI スケーリング表示問題 (優先度: 高)
- output/scale 設定が FPGA に正しく反映されていない可能性
- 表示が繰り返し・縦に潰れる症状
- **まず論理解像度 320x240 + scale 設定のみ (オフスクリーンなし) で正常表示を確認する必要あり**
- M5HDMI の DEBUG ログ有効化で内部設定値を確認すべき

### 2. 複数 Window (Canvas) コンポジット (優先度: 高)
- pushSprite (CMD_WRITE_RAW) が FPGA SPI と不安定 (PSRAM/内蔵RAM 問わず)
- 直接描画モードでは Canvas の Z-order 合成ができない

#### 検討中のアプローチ: FPGA オフスクリーン + CMD_COPYRECT
- FPGA 論理解像度を拡大して表示領域外にオフスクリーン領域を確保
- 各 Canvas はオフスクリーン Y 座標に直接描画 (直接描画モードと同じ安定性)
- `copyRect` (13バイト SPI) でオフスクリーン → 表示領域にコピー
- **前提: HDMI スケーリングとの両立が必要** (上記 #1 の解決が先)

### 3. TX Message Buffer 溢れ (優先度: 中)
- 描画コマンドの送信が断続的にタイムアウト
- TX buffer サイズ増 or タイムアウト延長で緩和可能

## Panel_M5HDMI 技術メモ

### 安定する操作
- `fillRect`, `drawLine`, `drawCircle` 等の直接描画コマンド
  → FPGA の CMD_FILLRECT, CMD_DRAWPIXEL 等を使用 (小パケット)
- `copyRect` → CMD_COPYRECT (13バイト)

### 不安定な操作
- `pushSprite` → CMD_WRITE_RAW で大量ピクセルストリーム
  → FPGA の内部バッファと HDMI スキャンアウトが競合

### FPGA 仕様
- VSYNC 同期なし (`display()` は空実装)
- `_check_busy()` で 512 バイトごとに FPGA ビジーポーリング
- RGB332 をネイティブサポート (CMD_WRITE_RAW_8)
- `copyRect` (CMD_COPYRECT 0x23) で FPGA 内矩形コピー
- `setViewPort` (CMD_SCREEN_ORIGIN 0x19) で表示起点座標設定
- `setScaling` (CMD_SCREEN_SCALING 0x18) でスケーリング設定
- memory_width/height はデフォルト 1280x720 (FPGA SRAM)

### 解像度制約
- `幅 x 高さ x リフレッシュレート <= 55,296,000`
- PSRAM は不要 (フレームバッファは FPGA 側)

## ファイル一覧

| File | Status | Description |
|------|--------|-------------|
| `main/drivers/m5gfx/m5gfx_task.h` | 実装済み | 解像度・Canvas 定義 |
| `main/drivers/m5gfx/m5gfx_task.cpp` | 実装済み (直接描画) | GFX コマンド処理・描画 |
| `main/drivers/i2c_keyboard/i2c_keyboard.c` | 実装済み | I2C キーボードドライバ |
| `main/drivers/i2c_keyboard/i2c_keyboard.h` | 実装済み | 公開 API |
| `main/drivers/ble/ble_task.c` | 実装済み | BLE ファイルサービス |
| `components/fmrb_hal/platform/esp32/fmrb_hal_link_local.c` | 実装済み | Message Buffer HAL |
| `components/fmrb_hal/fmrb_hal_link.h` | 変更済み | 拡張 API 宣言 |
| `components/fmrb_common/include/fmrb_pin_assign.h` | 変更済み | HW 別ピンアサイン |
| `components/fmrb_common/include/fmrb_task_config.h` | 変更済み | タスク設定 |
| `main/CMakeLists.txt` | 変更済み | ATOM_DISPLAY 条件分岐 |
| `main/boot/boot.c` | 変更済み | HW 別 init_hardware |
| `config/sdkconfig.defaults.n8r8` | 新規 | ATOM 用 sdkconfig |
| `config/partitions_n8r8.csv` | 新規 | ATOM 用パーティション |
| `config/system_conf_n8r8.toml` | 新規 | ATOM 用システム設定 |
| `Rakefile` | 変更済み | HW ターゲット対応 |
| `tool/web/index.html` | 新規 | Web Bluetooth ファイルマネージャ |
| `tool/web_server.rb` | 新規 | HTTP サーバ |
