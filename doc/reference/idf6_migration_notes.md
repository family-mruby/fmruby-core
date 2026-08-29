# ESP-IDF v6.0 移行メモ（IDF6対応で判明した課題と回避策）

## 背景

ESP32-P4(Modern/Tab5) 対応の初期に Modern を **ESP-IDF v6.0.1** でビルドする方針で進めたが、
IDF6 はメジャー初版で破壊的変更が多く、特に **linux preview の上流バグ**が深刻なため、
当面は **IDF v5.5.4 に統一**する判断をした（Retro/Modern/Linux すべて v5.5.4）。

ESP32-P4 自体は IDF v5.3 以降で正式サポートされ、MIPI-DSI(esp_lcd) も v5.5 で利用可能、
M5GFX(Tab5/P4 DSI) も元々 Arduino-ESP32(=IDF5.x) 前提のため、**P4 対応に IDF6 は不要**。

このメモは、将来 IDF6（linux preview 安定後、依存ライブラリの IDF6 正式対応後）へ移行する際に
再利用するため、IDF6 で実際にぶつかった課題と回避策を記録する。

検証環境: `espressif/idf:v6.0.1` ベースのコンテナ（GCC15.2 / CMake4.0.3 / picolibc / xtensa+riscv toolchain）。

---

## 1. libc: newlib → picolibc への移行（影響大）

IDF6 は esp32p4 で **picolibc** をリンクする（v5.5 は newlib）。これにより:

- **`_ctype_` 未定義**: 別ビルドの libmruby(picoruby の rake クロスビルド)が newlib 形式の
  `<ctype.h>`（`_ctype_` 配列参照）でコンパイルされ、picolibc にその実体が無くリンク失敗。
  - 回避: mruby クロスビルド設定にも **`-specs=picolibc.specs`** を付け、picolibc の `<ctype.h>` を使わせる
    (`lib/add/family_mruby_esp32p4.rb`)。toolchain 同梱の `picolibc.specs` が `-specs=` 名前解決で見つかる。
- **C11 `timespec_get` 欠落**: picolibc が同関数を提供せず、mruby-time(`TIME_UTC` 定義で C11 分岐選択)が
  リンク失敗。
  - 回避: `clock_gettime` ベースのシムを追加 (`main/compat/fmrb_libc_compat.c`、esp32p4 限定)。
  - 注意: **newlib(v5.5)では `timespec_get` が存在するため、このシムは重複定義になる**。v5.5 では不要/削除。

---

## 2. driver コンポーネントの分割（影響大）

IDF6 は monolithic な `driver` コンポーネントを `esp_driver_*` に分割し、`driver/i2s.h` 等の
legacy ヘッダを削除した（`driver/i2c.h` は EOL 警告付きで残存）。

- 自前コードが使う `driver/*.h` のため、対応する `esp_driver_*` を **REQUIRES に明示追加**が必要:
  - uart→esp_driver_uart, gpio→esp_driver_gpio, spi→esp_driver_spi, sdspi→esp_driver_sdspi,
    i2c→esp_driver_i2c, rmt→esp_driver_rmt
  - 変更箇所: `components/fmrb_hal/CMakeLists.txt`, `components/fmrb_common/CMakeLists.txt`,
    `main/CMakeLists.txt`（いずれも v5.5 でも有効＝後方互換）。
- `driver`(IDF6メタ) は `esp_hal_i2c/twai/touch_sens` のみ public REQUIRES し、
  `esp_driver_*` は public 公開しないため、`driver` を要求しても分割ヘッダの include パスは伝播しない。

---

## 3. M5GFX(LovyanGFX) の IDF6/P4 ビルド（影響大）

`managed_components` は gitignore のため編集不可。main 側からの対処が必要。

- `device.hpp` は **esp32p4 でも legacy esp32 バス**(`esp32/Bus_Parallel8.hpp`, `esp32/Panel_CVBS.hpp`)を
  **無条件 include**する。これらのヘッダが分割/削除された driver ヘッダを参照:
  - `Bus_Parallel8.hpp` → `<driver/i2s_types.h>`→`<driver/i2s_std.h>`→（無ければ）削除済み`<driver/i2s.h>`
  - `Light_PWM.cpp` → `<driver/ledc.h>`
  - `Panel_CVBS.cpp` の `driver/dac.h` 等はファイル全体が `CONFIG_IDF_TARGET_ESP32` ガード内で P4 非コンパイル。
- 回避: m5gfx コンポーネントを **`idf::esp_driver_i2s` / `idf::esp_driver_ledc` に target_link_libraries** し、
  推移的 interface include（`driver/i2s_types.h`, `driver/ledc.h`+`hal/ledc_types.h`）を供給
  (`main/CMakeLists.txt`、esp32p4 限定)。直接 include dir 追加では hal 等の推移依存が取れず不足。
- **GCC15 + -Werror**: IDF ヘッダ(`hal/assert.h` の `[[noreturn]]` マクロ)で `-Werror=attributes` 等に当たる。
  - 回避: m5gfx と当該 main ソースに **`-Wno-error`**（upstream コードのため。既存 m5unified 対応と同方針）。
- 補足: v5.5 では `driver/i2s.h` 等が健在なため M5GFX は素直にビルドでき、これらの回避策は不要。

---

## 4. FreeRTOS API 変更

- `xTaskGetAffinity()` が IDF6 で削除 → **`xTaskGetCoreID()`** (`components/fmrb_common/src/fmrb_task.c`)。
  v5.5 でも `xTaskGetCoreID` は存在するため後方互換。

---

## 5. mbedtls の再編（PSA crypto 移行）

- IDF6/mbedtls3.6 系で `mbedtls/cipher.h` が `mbedtls/private/cipher.h` へ移動（cipher API 非公開化、PSA へ移行）。
  - `picoruby-mbedtls`(cipher.c) がビルド不能。ただし gembox 未収録の dead gem のため esp32p4 で
    PICORUBY_SRCS から除外 (`components/picoruby-esp32/CMakeLists.txt`)。pwm.c も同様(dead)。
  - v5.5 では `mbedtls/cipher.h` 健在のため除外不要（dead gem なのでどちらでも可）。

---

## 6. linux preview の上流バグ（移行の最大の壁・未解決）

IDF6 の linux(POSIX) preview は、本プロジェクトのコンポーネント構成に対し複数の上流バグを持つ:

- **`esp_driver_ana_cmpr` 等が linux で登録され続ける**のに、要求する `esp_hal_ana_cmpr` は
  linux で早期 `return()`（"not supported by the POSIX/Linux simulator"）→ 依存解決失敗。
  同型: cam/isp/jpeg/mcpwm/parlio/pcnt/rmt/sdio/sdmmc/tsens/usb_serial_jtag/esp_lcd など多数。
- **`esp_hal_i2c` が存在しない `linux/include` を追加**しようとして CMake エラー。
- `EXCLUDE_COMPONENTS` で1つ潰すと次が出る whack-a-mole。IDF6.0.1 固有の脆い回避策が積み上がり、
  IDF 更新で壊れやすい。→ **IDF6 linux は実用に耐えないと判断し、追求を中止**。

結論: IDF6 へ移行するなら、linux preview の上記バグが上流で修正されてからにすべき。

---

## 7. その他

- toolchain: CMake 4.0.3 / GCC 15.2。`-DSDKCONFIG` 系の一部シンボルが rename
  （例: `CONFIG_OPTIMIZATION_LEVEL_RELEASE` → `CONFIG_COMPILER_OPTIMIZATION_SIZE`、info 表示のみ）。
- esp32p4 PSRAM: IDF6 既定で `SPIRAM_MODE_HEX` / `SPIRAM_SPEED_200M`（Tab5 32MB HEX 200MHz に合致）。

---

## IDF6 移行時に再利用できる成果（このブランチの IDF6 対応コミット）

- `380f740` ビルド環境 Retro/Modern 分離 + P4 ターゲット配線
- `d88d6d2` P4 を IDF6 でコンパイル&リンク（上記 1〜5 の回避策）
- `5d92557` Tab5 表示/入力のビルド配線 + M5GFX の IDF6/P4 統合（上記 3）
- `ef6bc50` Tab5 DSI パネル LGFX bring-up（IDF非依存。そのまま v5.5 で再利用可）
- `8022e6a` linux を Retro(IDF5.5) コンテナ固定（上記 6 の回避＝当面の正解）

P4 の本体作業（CMake 配線、sdkconfig/partition/system_conf、表示バックエンド、LGFX_Tab5、
Tab5 Keyboard）は **IDF バージョン非依存**であり、v5.5 でもそのまま生きる。
IDF6 固有の回避策（1,2,3,5,6）のみが、IDF6 移行再開時に再度必要となる。
