# fmruby-core のビルド構造とコンパイル定義のスコープ

2026-07-06 作成。P4対応で「FmrbHw::PIN_* がRetroのピン値のままになる」問題
(コンパイル定義のスコープ漏れ) を踏んだため、同じ間違いを繰り返さないよう
ビルドの仕組みを整理する。

## ソースは「3つの場所」でコンパイルされる

ESP32ターゲット (`rake build:esp32`) のビルドは、実体としては docker 内の
idf.py build だが、その中で mruby 関連は3系統に分かれる:

```
rake build:esp32
 └─ idf.py build (CMake)
     ├─ (1) IDFコンポーネント群
     │      main/ … アプリ本体 (boot, kernel, drivers, app)
     │      components/ … fmrb_common, fmrb_hal, fmrb_gfx, fmrb_msg,
     │                    picoruby-esp32, など
     │      managed_components/ … esp_hosted, m5gfx, esp_codec_dev など
     │
     ├─ (2) picoruby (mruby rake) ビルド  ← IDFの外部コマンドとして実行
     │      components/picoruby-esp32/CMakeLists.txt が
     │      `MRUBY_CONFIG=lib/add/family_mruby_esp32p4.rb rake` を起動し
     │      libmruby.a を生成する。ここに入るのは mruby本体 + 各gemの
     │      mrblib (Ruby→バイトコード) + ESPヘッダ不要な gem の C ソース
     │
     └─ (3) picoruby-esp32 コンポーネントの PICORUBY_SRCS
            ESP-IDF / FreeRTOS のヘッダを使う gem の「ports」Cソース
            (例: picoruby-fmrb-const/ports/esp32/const.c,
                 picoruby-fmrb-app/ports/esp32/{app.c, gfx.c}, machine系)
            は (2) ではなく IDFコンポーネントとしてコンパイルされ、
            libmruby.a とリンクされる。CLAUDE.md の
            「ESP32やFreeRTOSのヘッダを利用するmrbgemは
              components/picoruby-esp32/CMakeLists.txt の PICORUBY_SRCS で
              ビルド管理する」の実体がこれ
```

このほかに:

- **lib/add / lib/patch / lib/replace**: Rakefile が picoruby サブモジュール
  ツリーへコピー/上書きする (サブモジュール直接編集禁止のため)。
  **lib/ を編集したら `rake clean` が必須** (コピーと picoruby build の再生成)
- **main/prebuild_scripts/**: kernel / system_desktop の Ruby は、ビルド時に
  combined .rb に結合 → mrbc でバイトコード化 → .c 配列として main に埋め込み。
  mrb/ 以下の *_combined.rb や *.c は生成物 (タイムスタンプで確認可能)
- **flash/**: storage.bin (LittleFS イメージ) に入るアプリスクリプト。
  ビルドごとに再生成される

## 罠: コンパイル定義 (-D) はスコープごとに独立

ターゲット依存マクロ (例: `FMRB_HW_MODERN`) を共有ヘッダ
(`fmrb_pin_assign.h` 等) の分岐に使う場合、**そのヘッダを include する
ソースがコンパイルされる場所すべてに定義が必要**:

| スコープ | 定義する場所 |
|---|---|
| (1) main のソース | main/CMakeLists.txt の `target_compile_definitions(${COMPONENT_LIB} PRIVATE ...)` |
| (1) 他のIDFコンポーネント | 各コンポーネントの CMakeLists.txt (PRIVATE定義は他コンポーネントに伝播しない!) |
| (2) rake ビルドの libmruby | lib/add/family_mruby_esp32p4.rb の `conf.cc.defines` |
| (3) PICORUBY_SRCS (gem ports) | components/picoruby-esp32/CMakeLists.txt の `target_compile_definitions` |

### 実際に起きた事故 (2026-07-06)

`FmrbHw::PIN_I2C1_SDA/SCL` (const.c が fmrb_pin_assign.h から定義) が
P4ビルドでも Retro の値 (GPIO14/21) を返し、Modern の I2C1 (GPIO31/32)
仲介が「ピン不一致」で拒否 → RTC 設定が失敗した。

- 原因: `FMRB_HW_MODERN` が main コンポーネントにしか定義されておらず、
  const.c は上記 (3) でコンパイルされるため定義が届いていなかった
- 一次対応で (2) の conf.cc.defines に追加したが、const.c は (2) では
  コンパイルされないため効かなかった (2敗目)
- 最終修正: components/picoruby-esp32/CMakeLists.txt に
  `if(IDF_TARGET STREQUAL "esp32p4") target_compile_definitions(... FMRB_HW_MODERN)`

## 定義が効いたかの検証方法

ビルド後に compile_commands.json で対象ファイルのコンパイルコマンドを
直接確認するのが確実:

```sh
python3 - <<'EOF'
import json
db = json.load(open("build/compile_commands.json"))
for e in db:
    if "const.c" in e["file"]:
        print("FMRB_HW_MODERN" in e["command"], e["file"])
EOF
```

(2) の rake ビルド分は compile_commands.json に載らないので、
ビルドログの `MRUBY_CONFIG=... rake` セクションと build_config の
conf.cc.defines を確認する。

## チェックリスト: ターゲット依存の #define を増やすとき

1. そのマクロを使うヘッダを include するファイルを洗い出す
   (`grep -rl <header>` を main/ components/ lib/ で)
2. 各ファイルがどのスコープでコンパイルされるか確認
   (ビルドログで `Building C object` の行 = IDF、`CC ` の行 = rake)
3. 該当スコープすべてに定義を追加
4. compile_commands.json / ビルドログで実際のフラグを検証
