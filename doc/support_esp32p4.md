# ESP32-P4対応指針

現在のHWは、ESP32S3とESP32で構成されているが、これをESP32P4に統合する。

既存のものは、Family mruby Retro として残す。NTSCの表示も価値があるため。
ESP32P4番は、Family mruby Modern　として、HDMI出力を持たせる。

HDMI出力はP4単体ではできないので、以下の方法が考えられる。いずれもP4から搭載されたグラフィック機能を生かす。
・SPI出力＞RP2350でHDMI（DVI）変換する
・パラレルRGB出力＞HDMI変換
LovyanGFXで、P4のアクセラレーションが使えるのか気になる。使えないなら、必要な機能に限って、LovyanGFX互換の描画ライブラリを自作する。

P4からの音声をHDMIの音声にどう変換するかは要検討。

fmruby-core にP4の場合のみ有効な、描画と音声のレイヤーを追加する形として、コンパイルオプションでModernのときは、リンクレイヤーを差し替えることで、RetroとModern双方で同じRubyアプリが動くようにする。
開発対象はfmruby-core になる。
ESP32のモデル違いに関する違いはIFDEFで切り替える必要も出て来るはず。

Linuxでのシミュレーション機能は、Retroと同じように使いたい。

開発環境は、最新のESPIDFに更新したい。

## ハードウェア仕様（Modern）

会話で整理した前提仕様。実機（NARYAv4）は M5Stack Tab5 と同じSoC/無線構成を採用する予定。
https://docs.m5stack.com/ja/core/Tab5
https://github.com/m5stack/M5GFX


- **SoC**: ESP32-P4（デュアルコア RISC-V HP 最大360/400MHz + LPコア）
- **無線**: ESP32-C6 をコプロセッサとして搭載（SDIO等で接続）。P4自体は無線非搭載のため、WiFi/BLEはC6経由となる（Tab5と同じ構成）。
- **PSRAM**: 32MB
- **Flash**: 16MB
- **ディスプレイ**: Tab5本体は MIPI-DSI 1280x720 タッチIPS。本プロジェクトのModernはHDMI出力を目標とするため、表示経路（DSIパネル直結 or HDMI変換）は要検討（上記HDMI方式の候補参照）。
- USB-A(host)/USB-C、MIPI-CSIカメラ、IMU、マイク/スピーカー等

ファームウェアへの影響:

- BLEは現状 P4 の `bt` コンポーネント/NimBLE に直接依存しているが、P4単体では無線が無い。Modernでは無線をC6経由化する必要があり、それまでP4ビルドではBT/WiFiを無効化する。
- 映像/音声は現状 WROVER への SPI/UART 連携前提。Modernでは描画/音声レイヤーの差し替えが必要。

## 方針変更（重要）: 単一 IDF v5.5.4 に統一

当初 Modern を **IDF v6.0.1** でビルドしたが、IDF6 はメジャー初版で破壊的変更が多く（picolibc 移行、
driver コンポーネント分割、GCC15 厳格化、特に **linux preview の上流バグ**）、回避策が積み上がり脆い。
ESP32-P4 自体は **IDF v5.3+ で正式サポート**、M5GFX(Tab5/P4 DSI) も IDF5.x 前提のため、**P4 対応に IDF6 は不要**。

→ **Retro / Modern / Linux すべてを単一 IDF v5.5.4 コンテナでビルド**する方針へ変更。
3ターゲット全てのビルド成功を確認済み（IDF6 固有の回避策ゼロ）。

- 作業ブランチ: **`feat/fmruby-modern-p4-idf55`**（develop から分岐。IDF非依存の P4 本体作業のみ適用）
- 旧 `feat/fmruby-modern-p4`（IDF6 経緯入り）はそのまま残す。IDF6 で判明した課題は `doc/idf6_migration_notes.md` 参照。

## 作業計画

- [x] ESP32-P4(NARYAv4)向け sdkconfig / partition / system_conf 追加と build タスク配線
- [x] **単一 IDF v5.5.4 コンテナで Retro(esp32s3) / Modern(esp32p4) / Linux すべてビルド成功**
- [x] Tab5表示 Phase 1: 自前LGFXクラス(LGFX_Tab5) + PI4IO電源/リセット + ブートテストパターン（実機表示確認済み）
- [x] Phase 1 実機確認（Tab5 実機: ブートテキスト表示・カーネル/デスクトップ起動確認）
- [x] Tab5表示 Phase 2: GFX コマンドデコード + フレームバッファ合成 + 3x スケーリング表示
- [x] Tab5表示 Phase 2.5: スプライト(SpriteImage/Instance/Mask) + GfxBlock VM + PNG画像ロード
- [x] Tab5表示 Phase 3: Tab5 Keyboard(I2C 0x6D, HID mode) 入力
- [x] Tab5表示 Phase 4: 本体タッチ(GT911)→トラックパッド方式マウス
- [x] 仮想画面サイズ 426x240 (3x→1278x720, LCD中央配置)
- [ ] PPA (Pixel Processing Accelerator) によるハードウェアスケーリング（調査・試行済み、未完了）
- [ ] lgfx_tab5: パネル自動判定（ILI9881C / ST7123 / ST7121 をDSI ID読み取りで切替。NARYAv4量産対応）
- [ ] 無線(BLE/WiFi)のC6経由化（現状P4では無効）
- [ ] 実機での sdkconfig 微調整

備考: 以下の「作業記録」は IDF6 で進めた当時の経緯を含む（履歴）。現行 v5.5.4 では IDF6 固有の回避策
（picolibc specs / timespec シム / idf::esp_driver 連結 / -Wno-error / linux EXCLUDE）は **すべて不要*で、
本ブランチには入れていない。P4 本体作業（config、表示バックエンド/LGFX_Tab5、Tab5キーボード、リンク層差し替え）は
IDF 非依存でそのまま有効。

## 作業記録

### ビルド環境のRetro/Modern分離

ビルドコンテナを2系統に分離した。

- **Retro**（ESP32-S3 + WROVER, NTSC）: ESP-IDF `v5.5.4` を維持
- **Modern**（ESP32-P4, HDMI）: ESP-IDF `v6.0.1`（6.x系、pin指定。ローカル所持のベースイメージに合わせ再現性確保）

実装方針:

- Dockerfile は二重管理せず1つを共用し、`ARG IDF_VER` でバージョンを切替えて別タグの2イメージをビルドする。
  - Retro イメージ: `ghcr.io/family-mruby/fmruby-esp32-build`
  - Modern イメージ: `ghcr.io/family-mruby/fmruby-esp32-build-modern`
  - libasan のパッケージ名がIDF5/IDF6のUbuntuベースで異なるため、apt インストールはフォールバックさせて両対応とした。
- バージョンは `.env` の `ESP_IDF_VERSION_RETRO` / `ESP_IDF_VERSION_MODERN` で管理。
- 使用コンテナは `FMRB_HW_TARGET` で自動選択（`NARYAv4`=ESP32P4 → Modern、それ以外 → Retro）。Rakefile が起動時に選択結果を表示する。
- `docker/build.sh` はローカルビルド用に `./build.sh` / `./build.sh modern` で系統を選択。
- CI（`.github/workflows/docker-publish.yml`）は matrix で両イメージを publish。

変更ファイル: `.env`, `docker/Dockerfile`, `docker/build.sh`, `Rakefile`, `.github/workflows/docker-publish.yml`

注: Modern専用のビルド差分（P4用ツールチェーン等）が出てきた場合はDockerfileを分割する。

### ESP32-P4 ビルドターゲットの配線

`FMRB_HW_TARGET=NARYAv4` で esp32p4 ターゲットをビルドする経路を追加した（この段階ではコンパイルの完全通過は目標としない。BT/WROVER依存のソース修正は次段階）。

- `Rakefile`: `set_target:esp32` / `build:esp32` を Retro=esp32s3 / Modern(NARYAv4)=esp32p4 で切替。`hw_config` に `NARYAv4` を追加し P4 用 config を選択。
- 追加した config（Tab5相当: Flash 16MB / PSRAM 32MB、無線はC6前提で BT/WiFi 無効）:
  - `config/sdkconfig.defaults.p4`
  - `config/partitions_p4.csv`
  - `config/system_conf_p4.toml`
- sdkconfig は実機（PSRAM速度、パーティション等）に合わせ menuconfig での調整が必要。

既知の未対応（次段階のポーティング作業）:

- `main/CMakeLists.txt` の `bt`/`drivers/ble/ble_task.c` 等が P4 では無線非搭載のためそのままでは通らない。C6経由化 or IFDEF分離が必要。
- 映像/音声の WROVER(SPI/UART)依存を Modern 用レイヤーに差し替える必要がある。

### Modern コンテナ実ビルドと IDF6 ソースポーティング（コンパイル&リンク成功）

Modern コンテナ（`espressif/idf:v6.0.1` ベース、ローカルタグ `esp32_build_container_modern:v6.0.1`）を `docker/build.sh modern` でビルドし、`FMRB_HW_TARGET=NARYAv4` で esp32p4 ファームのコンパイル&リンクを通した（`fmruby-core.bin` 生成、app パーティション 4MB に約58%空き）。

ローカル開発時は Modern イメージを `DOCKER_IMAGE_MODERN=esp32_build_container_modern:v6.0.1 rake build:esp32` のように指定（CI publish 前のため）。`.env` の `FMRB_HW_TARGET` を `NARYAv4` にして切替。

対応した IDF6 / RISC-V 移行ポイント:

- **M5GFX/M5Unified を esp32p4 で除外**（`main/idf_component.yml` の `rules`）。旧版が IDF6 で削除された `driver/i2s.h` を含むため。Modern では未使用。
- **BLE を esp32p4 で除外**（`main/CMakeLists.txt`）。`bt` コンポーネント要求と `drivers/ble/ble_task.c` を外し、`boot.c` の `ble_task_init()` を `CONFIG_IDF_TARGET_ESP32P4` でガード。無線はC6経由化が将来課題。
- **IDF6 の driver コンポーネント分割対応**。monolithic `driver` から個別 `esp_driver_uart/gpio/spi/sdspi` 等へ REQUIRES を追加（`fmrb_hal`, `fmrb_common`, `main` の CMakeLists）。IDF5.5 でも有効で Retro 互換。
- **FreeRTOS API**: `xTaskGetAffinity`（IDF6で削除）→ `xTaskGetCoreID`（`fmrb_common/src/fmrb_task.c`）。
- **picoruby のデッドgem除外**（esp32p4のみ、`components/picoruby-esp32/CMakeLists.txt`）。gembox 未収録で未使用の `picoruby-pwm`(pwm.c) と `picoruby-mbedtls`(cipher.c) を PICORUBY_SRCS から除外。後者は IDF6 で mbedtls cipher API が PSA へ移行したため。
- **libmruby を RISC-V でビルド**。`lib/add/family_mruby_esp32p4.rb` を新規追加し、`riscv32-esp-elf-*` ツールチェーン + IDF と同じ `-march=rv32imafc_zicsr_zifencei_zaamo_zalrsc_xesploop_xespv -mabi=ilp32f` を使用。`picoruby-esp32/CMakeLists.txt` が esp32p4 では別 build dir(`build/esp32p4`) と本設定を選択。
- **picolibc 対応**。IDF6 の esp32p4 は newlib でなく picolibc をリンクするため、libmruby も `-specs=picolibc.specs` でビルド（`<ctype.h>` の `_ctype_` 未定義を回避）。
- **C11 `timespec_get` シム**。picolibc が同関数を提供せず mruby-time がリンク失敗するため、`main/compat/fmrb_libc_compat.c` に `clock_gettime` ベースの実装を esp32p4 限定で追加。

注: この段階は「コンパイル&リンクが通る」ことが目標。映像/音声/transport は WROVER 前提のままで、Modern 実機での動作は未確認（表示経路・無線C6化が今後）。

### Tab5 表示/入力（設計と Phase 0）

実機 Tab5 で画面表示まで到達する作業。設計判断:

- 表示は Modern 単体(WROVERなし)なので **fmruby-core 内の新規軽量描画層**で行う。gfxコマンド経路(`fmrb_gfx`→host_task→transport→`fmrb_hal_link`)は不変で、**`fmrb_hal_link` の差し替え**でローカル描画する（ATOM_DISPLAY と同じ `fmrb_hal_link_local.c` ＝ Message Buffer ループバックを esp32p4 でも選択）。Retro/Linux/Rubyアプリ不変。
- 描画ライブラリは **LovyanGFX を直接利用**（M5Unified のボード自動判定/`M5.begin()` は使わない）。Tab5 の DSI/ST7123/GT911 は自前 LGFX クラスで構成（将来のパラレルRGB対応や fork の土台）。表示バックエンドを薄い抽象の裏に隠す。
- マウスは Tab5 本体タッチ(GT911 @ I2C 0x55)を LGFX `getTouch()` で読む。キーボードは Tab5 Keyboard(I2C 0x6D, STM32F030, character mode)。
- 論理解像度は当面 320x240 のまま、最終 present で 1280x720 にアップスケール。

**Phase 0（ビルド配線 + M5GFX の IDF6/P4 統合）完了:**

- `main/idf_component.yml`: m5gfx を esp32p4 でも取得可に（LGFX部分のみ利用。M5Unified は除外）。
- `main/CMakeLists.txt`: esp32p4 アームに `drivers/display_p4`(表示タスク stub) と `drivers/tab5_keyboard`(stub) を追加、`FMRB_HW_MODERN` define、m5gfx を REQUIRES に。
- M5GFX の IDF6/P4 ビルド問題を解決（managed_components は gitignore のため非編集、main 側で対処）:
  - device.hpp が P4 でも legacy esp32 バス(Bus_Parallel8/Light_PWM)を強制 include し、IDF6 で分割された driver ヘッダを要求 → m5gfx ライブラリを **`idf::esp_driver_i2s` / `idf::esp_driver_ledc` に link** して推移的 include を供給（`driver/i2s_types.h`, `driver/ledc.h`+`hal/ledc_types.h`）。`driver/dac.h` 等は esp32-classic ガード内でP4非コンパイル。
  - GCC15/IDF6 で M5GFX が IDF ヘッダの `[[noreturn]]` 等で `-Werror` に当たるため、m5gfx に `-Wno-error`（upstream コードのため、既存の m5unified 対応と同方針）。
- `components/fmrb_hal/`: esp32p4 で `fmrb_hal_link_local.c` を選択、拡張API guard を `FMRB_HW_MODERN` にも拡張。
- `main/boot/boot.c`: `FMRB_HW_MODERN` アームで display/keyboard を起動。
- `components/fmrb_common/include/fmrb_pin_assign.h`: Modern(Tab5) ピン（KBD I2C SDA0/SCL1/INT50, TOUCH INT23, LCD backlight 22 ほかは NC で TODO）。

結果: M5GFX を含む P4 ファームがリンク成功（app 4MB に 59% 空き）。表示/入力本体は次フェーズ。

**Phase 1（Tab5 DSI パネル起動 + テストパターン）— ビルド成功・実機確認待ち:**

- `main/drivers/display_p4/lgfx_tab5.hpp`: M5Unified 非依存の自前 `LGFX_Tab5 : lgfx::LGFX_Device`。Bus_DSI(2レーン/1040Mbps/LDO ch3 2500mV) + Panel_ST7123(720x1280, dpi80MHz, hsync40/2/40, vsync8/2/220) + Touch_GT911(SDA31/SCL32/INT23, port1) を明示構成。M5GFX.h は LGFX ライブラリ用に include するが M5GFX/M5Unified オブジェクトは使わない。Panel_ST7123/Touch_GT911 は device.hpp が include しないので個別 include。
- `main/drivers/display_p4/display_p4_task.cpp`: PI4IO 拡張IC(0x43/0x44, IDF i2c_master で一時バス→LCD/タッチ reset 解除→削除しLGFXにI2C port1を譲る) + TP INT(GPIO23) + バックライト(GPIO22 full-on)。LGFX_Tab5.init() 後にカラーバー+文字のテストパターン描画。FreeRTOS タスク(core1)。
- `main/CMakeLists.txt`: display_p4_task.cpp が M5GFX.h を引くため main(esp32p4) にも esp_driver_i2s/ledc を REQUIRES 追加 + 当該ファイルに -Wno-error。
- Phase 1 はパネル ST7123 をハードコード（現行Tab5）。ILI9881C/ST7121 個体は Panel クラス/タイミング差し替えが必要。

**実機テスト前に必要な sdkconfig 提案（sdkconfig.defaults 編集禁止のため menuconfig 等で適用）:**

- `CONFIG_SPIRAM_SPEED_200M=y`（Tab5/P4 PSRAM は 200MHz。M5GFX も 200MHz 要求。DSI フレームバッファ帯域に必要）
- DSI PHY LDO(ch3/2500mV) と esp_lcd MIPI-DSI は IDF6/P4 で実行時取得・自動有効のため追加不要の見込み（点灯しない場合は LDO 予約系 Kconfig を確認）。

**Phase 1 実機確認（Tab5 実機）— 完了:**

実機 Tab5 で起動・表示を確認。以下の問題を発見・修正した。すべて **M5GFX の Tab5 実装（M5GFX.cpp）を参照** して解決した。

1. **チップ名誤表示**: `boot.c` の chip 名判定に `CONFIG_IDF_TARGET_ESP32P4` 分岐を追加。

2. **kernel 起動待ちタイムアウト**: `display_p4_task` がトランスポートの受信側を実装していなかった。`fmrb_hal_link_local_receive_cmd` でコマンドをポーリングし、msgpack でパースして ACK を返す実装を追加（VERSION/INIT_DISPLAY/GA_VERSION/DEFINE_PROG 対応）。

3. **PSRAM 速度**: DSI DPI フレームバッファの帯域が足りず画面がアンダーランしていた。`CONFIG_SPIRAM_SPEED_200M` は `CONFIG_IDF_EXPERIMENTAL_FEATURES=y` が前提になっていたため、`config/sdkconfig.defaults.p4` に追加して 200MHz を有効化。

4. **パネル型番の誤認識（主因）**: `lgfx_tab5.hpp` を当初 ST7123 でハードコードしていたが、実機は **ILI9881C**（Tab5 は生産時期でパネルが異なる。背面ラベルで確認）。M5GFX.cpp の Tab5 検出コードを参照し、以下に修正:
   - `Bus_DSI`: lane_mbps 1040 → **900**
   - パネル: `Panel_ST7123` → **`Panel_ILI9881C`**
   - DPI タイミング: M5GFX の ILI9881C 値（hsync_back=140, hsync_pulse=40, hsync_front=40, vsync_back=20, vsync_pulse=4, vsync_front=20）に変更
   - タッチ: `Touch_GT911` はそのまま（ILI9881C は GT911 が正しい）

5. **バックライト点灯しない**: `gpio_set_level(GPIO22, 1)` では Tab5 のバックライトドライバ IC が点灯しなかった。M5GFX に倣い `Light_PWM`（LEDC ch7 / 44100Hz / GPIO22）に変更し、LovyanGFX の init 内でバックライトを起動するよう修正。

現状: ブートテキスト表示・カーネル/system_desktop 起動・25アプリ認識を確認。GFX コマンド（91.5 cmds/s, 4.4 presents/s）は ACK のみで未描画（Phase 2 の作業）。I2C1 の hw_proxy 競合（GT911 が保持するため）は軽微エラーとして残存。

**Tab5 パネルバリアントについて（M5Stack 公式情報）:**

Tab5 は生産時期によりパネルドライバが異なる。背面ラベルで確認可能。

| 生産時期 | 表示ドライバ | タッチドライバ | lgfx_tab5.hpp の対応 |
|----------|-------------|---------------|----------------------|
| 2025/10/14 以前 | ILI9881C（独立） | GT911（独立） | 現在実装済み（ハードコード） |
| 2025/10/14 以降 | ST7123（一体型） | ST7123（一体型） | 未実装（将来課題） |

M5GFX は DSI 経由でパネル ID を読み取り実行時に自動判定する。NARYAv4 量産時の調達ロットによって ST7123 になる可能性があるため、自動判定の実装が将来的に必要。

### Phase 2〜4: GFX 描画・スプライト・キーボード・タッチ — 完了

**Phase 2 (GFX 描画 + フレームバッファ合成):**

- `display_p4_task.cpp` を全面実装。全 GFX コマンド（描画プリミティブ、キャンバス管理、カーソル、GfxBlock VM、スプライト、PNG 画像）を処理。
- フレームバッファ方式: 8bpp (RGB332) の共有フレームバッファ（INIT_DISPLAY で動的サイズ確保）にキャンバスを z-order 順で合成 → `pushRotateZoom` で 3x スケーリングして g_lcd に出力。
- キャンバス合成: 不透明は行単位 `memcpy`、透過は RGB332 値でバイト比較ループ。LovyanGFX の `pushSprite` は 8bpp 透過色を RGB888 に誤変換するため使用せず、直接バッファ操作で実装。
- PNG 画像: `CREATE_IMAGE_FROM_FILE` で 16bpp 中間スプライトに `drawPng` → 8bpp に `pushSprite` 変換して保存。RGBA PNG は白背景で事前クリア。
- 仮想画面サイズを 320x240 → 426x240 に変更（1280/3=426, LCD 横幅をフル活用）。

**Phase 2.5 (スプライト + VM):**

- `display_p4_vm.cpp/h`: GfxBlock バイトコード VM（9オペコード、16レジスタ、16プログラム）。
- `display_p4_sprite.cpp/h`: SpriteImage（8bpp PSRAM）、SpriteInstance（マルチフレーム、z-order）、1bpp Mask プール。
- スプライトはフレームバッファに直接合成（キャンバスバッファを破壊しない設計）。

**Phase 3 (Tab5 Keyboard):**

- `tab5_keyboard.c` を HID mode (レジスタ 0x10=1) で実装。I2C0 (GPIO0/1) で STM32F030 (0x6D) にアクセス。
- 50ms ポーリングで INT_STA (0x01) + HID_EVENT (0x30, 2バイト: modifier+keycode) を読み取り。
- USB HID と同じ modifier 変換 + `fmrb_host_send_key_down/up` でイベント送信。
- キーボード未接続時は probe 失敗で非致命的スキップ。

**Phase 4 (タッチ入力):**

- `drivers/touch/touch_task.c`: GT911 タッチをトラックパッド方式で実装。
- `display_p4_get_touch()` (g_lcd.getTouch のラッパー) を 33ms ポーリング。
- 絶対座標ではなく相対移動（指の移動量をカーソルに加算）。パネル座標の差分を 3 で割って仮想座標に変換。
- タッチダウン=クリック、ドラッグ=カーソル移動、タッチアップ=リリース。

### PPA (Pixel Processing Accelerator) 対応の経緯と今後

**調査結果:**

ESP32-P4 は PPA ハードウェアを搭載し、ESP-IDF v5.5 の `esp_driver_ppa` コンポーネント (`driver/ppa.h`) で SRM (Scale-Rotate-Mirror) / Blend / Fill の 3 操作が利用可能。

- SRM: 入力画像を 1/16 ステップでスケーリング + 0/90/180/270度回転 + ミラー
- 対応色空間: **RGB565, RGB888, ARGB8888, YUV 各種, GRAY8**
- **RGB332 は非対応**

**実装経緯 (全 3 回の試行):**

**試行 1: RGB565 フレームバッファ + PPA SRM + Panel_DSI 直接出力**
- フレームバッファ RGB565 化 + PPA SRM 3x スケーリング → Panel_DSI 内部バッファに直接書き込み
- 結果: 画面完全崩壊。DMA2D パイプラインとの競合が原因

**試行 2: RGB565 フレームバッファ + pushSprite 合成 + pushRotateZoom**
- 8bpp→16bpp の pushSprite が透過色を RGB888 として誤解釈 → 色化け
- pushSprite の代わりに手動 rgb332_to_565 変換を追加 → 変換誤差で色ずれ（赤→青）
- 8bpp に戻して正常動作回復

**試行 3 (現在): 全バッファ RGB565 + PPA Blend + PPA SRM + pushImage 出力**
- 全バッファ（キャンバス/フレームバッファ/SpriteImage）を RGB565 16bpp に統一
- GFX コマンドの色引数は `uint8_t` のまま渡す（LovyanGFX が RGB332→RGB565 自動変換）
- キャンバス合成: PPA Blend (color-key で透過色スキップ)
- スケーリング: PPA SRM 3x → 専用出力バッファ → `g_lcd.pushImage()` で転送
- バッファは `heap_caps_aligned_alloc(64, ...)` + `LGFX_Sprite::setBuffer()` でキャッシュアライン
- cache alignment エラー解消済み

**残存問題 (対応中):**

1. **R/B 色スワップ**: LovyanGFX の RGB565 メモリ格納が `rgb565_2Byte` (バイトスワップ済み: `GGGBBBBB RRRRRGGG`)。PPA は標準 RGB565 (`RRRRRGGG GGGBBBBB`) として読むためR/Bが入れ替わる。
   - 対策: PPA SRM/Blend の `byte_swap` フラグを `true` に設定。PPA が入力バイトペアをスワップして読むことで LovyanGFX の格納フォーマットに対応。
   - Blend では `fg_byte_swap` / `bg_byte_swap` の両方を `true` に。

2. **背景画像が見えず濃い緑**: PPA Blend の color-key 比較は PPA が byte_swap 後のピクセル値に対して行われるはず。byte_swap 修正で解消される見込み。

3. **描画が右→左に見える**: PPA SRM 出力を `pushImage` で毎フレーム 1278x720 全転送しているため遅い。パネルの vsync に同期していないので描画途中が見える。
   - 将来: ダブルバッファまたは DMA2D 転送完了待ちの導入が必要。

**LovyanGFX のバイトオーダーまとめ:**

```
rgb565_2Byte (swapped, default):  メモリ上 [GGGBBBBB] [RRRRRGGG] (MSB first)
rgb565_nonswapped:                メモリ上 [RRRRRGGG] [GGGBBBBB] (LSB first)
PPA RGB565 期待:                  メモリ上 [RRRRRGGG] [GGGBBBBB]
```

`setBuffer(buf, w, h, 16)` は bpp=16 → `rgb565_2Byte` を設定。PPA と不一致のため `byte_swap=true` が必須。

### PPA 修正計画（試行4）: 全スプライトを PPA ネイティブ RGB565 に統一

**根本原因の特定（ESP-IDF PPA ドライバソース精読による）:**

PPA の `byte_swap` は **INPUT(RX) のみ** で、OUTPUT(TX) に byte_swap レジスタは存在しない
（`ppa_ll.h`: `sr_rx_byte_swap_en`, `blend0_rx_byte_swap_en` 等。TX 側に対応フラグなし）。

従って、PPA Blend の出力は常に PPA ネイティブ（非スワップ）RGB565 でフレームバッファに書き戻される。
次の Blend で `bg_byte_swap=true` すると二重スワップとなり、色崩壊する。これが R/B 反転と緑背景の原因。

参考: LGFX_PPA ライブラリ（M5Stack 公式 PPA ラッパー, `tmp/LGFX_PPA/`）は、
RGB565 での PPA Blend を避け RGB888 を使用している。SRM では `byte_swap = is_panel` として
Panel_DSI バッファへの直接書き込み時のみ byte_swap を使用。

**修正方針:**

全 LGFX_Sprite を `rgb565_nonswapped` (= PPA ネイティブ RGB565) に設定する。
`setBuffer(buf, w, h, 16)` 後に `setColorDepth((lgfx::color_depth_t)17)` で override。
これにより PPA の入出力フォーマットが全段で統一され、byte_swap が一切不要になる。

LovyanGFX の描画関数（drawPixel, fillRect, drawPng, pushSprite 等）は内部で色深度に応じた
変換を行うため、`rgb565_nonswapped` でも正しく動作する。

**変更箇所:**

1. 全 LGFX_Sprite 生成箇所で `setColorDepth((lgfx::color_depth_t)17)` を追加
   - canvas_alloc(), cursor_init(), INIT_DISPLAY(framebuffer), CREATE_IMAGE_FROM_FILE, sprite_image_create()
2. PPA Blend: `fg_byte_swap=false`, `bg_byte_swap=false`
3. PPA SRM: `byte_swap=false`
4. Color-key: RGB332→RGB565(非スワップ)→R5G6B5 成分抽出→範囲指定で RGB888 閾値設定
5. 透過色比較: `swap565_t` → `rgb565_t` に変更（PUSH_CANVAS, DRAW_TILE）
6. キャッシュアラインメント: `esp_cache_get_alignment()` で動的取得（LGFX_PPA と同方式）

## HWターゲットの分離: TAB5 / NARYAv4

これまで `FMRB_HW_TARGET=NARYAv4` が M5Stack Tab5 実機を指していたが、
NARYAv4 は本来将来製作する ESP32-P4 搭載専用基板の名前であるため、ターゲットを分離した。

- `TAB5`: M5Stack Tab5（現行の Modern 開発機）
- `NARYAv4`: 将来の専用基板。ハード未確定のため、当面は Tab5 と同じ
  sdkconfig / system_conf / ピン割当（プレースホルダ）でビルドされ、CMake が警告を出す
- チップ世代共通コードは従来どおり `FMRB_HW_MODERN` で分岐し、ボード差分
  （映像/音声出力、GPIO 配置）は新設の `FMRB_HW_TAB5` / `FMRB_HW_NARYAV4` で分岐する
- マクロ定義は `components/fmrb_common/fmrb_hw_defines.cmake` の `fmrb_add_hw_defines()` に
  集約し、main / fmrb_hal / picoruby-esp32 の3コンポーネントが呼び出す
  （従来は main と picoruby-esp32 のみの PRIVATE 定義で、fmrb_hal 内の pin manager が
  Modern/ATOM ビルドでも Retro のピン表で compile される不整合があった。これも解消）
