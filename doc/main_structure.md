# main/ ディレクトリ構成

## ルートレベル

```
main/
├── CMakeLists.txt
├── main.c              # エントリポイント
├── boot.c / boot.h     # ブート処理
```

## app/ - アプリケーション層

```
app/
├── fmrb_app.c          # アプリケーション管理(プロセス管理、タスク生成)
└── default_app/        # デフォルトアプリケーション群
    ├── mrb/            # C拡張
    │   ├── .gitkeep
    │   ├── config.c
    │   ├── editor.c
    │   └── shell.c
    └── mrblib/         # Ruby実装
        ├── config.app.rb
        ├── editor.app.rb
        └── shell.app.rb
```

## dev/ - デバイスドライバ

```
dev/
└── usb/
    └── usb_task.c      # USBホスト制御
```

## include/ - 公開ヘッダファイル

```
include/
├── fmrb_app.h          # アプリケーション管理API
├── fmrb_err.h          # エラーコード定義
├── fmrb_mem.h          # メモリ管理API
└── fmrb_task_config.h  # タスク設定
```

## kernel/ - カーネル

```
kernel/
├── fmrb_kernel.c / fmrb_kernel.h  # カーネル本体
├── host/                           # ホストタスク
│   ├── host_task.c
│   └── host_task.h
├── mrb/                            # C拡張
│   ├── .gitkeep
│   ├── kernel.c                    # カーネルRuby拡張
│   └── system_gui.c                # システムGUI拡張
└── mrblib/                         # Ruby実装
    ├── kernel.rb
    └── system_gui.app.rb
```

## lib/ - ライブラリ群

### fmrb_audio - オーディオ制御ライブラリ

```
lib/fmrb_audio/
├── CMakeLists.txt
├── fmrb_audio.c
└── fmrb_audio.h
```

### fmrb_gfx - グラフィックスライブラリ

```
lib/fmrb_gfx/
├── CMakeLists.txt
├── fmrb_gfx.c
├── fmrb_gfx.h
├── fmrb_gfx_commands.c      # 描画コマンド実装
├── fmrb_gfx_commands.h
└── fmrb_gfx_window.h        # ウィンドウ管理
```

### fmrb_hal - ハードウェア抽象化層

```
lib/fmrb_hal/
├── CMakeLists.txt
├── fmrb_hal.c / fmrb_hal.h          # HAL統合ヘッダ
├── fmrb_hal_esp.h                   # ESP32固有定義
├── fmrb_hal_freertos.h              # FreeRTOS抽象化
├── fmrb_hal_file.h                  # ファイルシステムAPI
├── fmrb_hal_gpio.c / fmrb_hal_gpio.h    # GPIO制御
├── fmrb_hal_ipc.h                   # IPC抽象化
├── fmrb_hal_spi.h                   # SPI抽象化
├── fmrb_hal_time.c / fmrb_hal_time.h    # 時刻管理
├── fmrb_pin_assign.h                # Pinアサイン定義
└── platform/                        # プラットフォーム固有実装
    ├── esp32/
    │   ├── fmrb_hal_file_esp32.c    # ESP32ファイルシステム(LittleFS)
    │   ├── fmrb_hal_ipc_esp32.c     # ESP32 IPC(SPI)
    │   └── fmrb_hal_spi_esp32.c     # ESP32 SPI実装
    └── linux/
        ├── fmrb_hal_file_linux.c    # Linux ファイルシステム(POSIX)
        ├── fmrb_hal_ipc_linux.c     # Linux IPC(Socket)
        └── fmrb_hal_spi_linux.c     # Linux SPI実装(スタブ)
```

### fmrb_ipc - プロセス間通信ライブラリ

```
lib/fmrb_ipc/
├── CMakeLists.txt
├── fmrb_ipc_cobs.c / fmrb_ipc_cobs.h        # COBS符号化
├── fmrb_ipc_protocol.c / fmrb_ipc_protocol.h  # プロトコル層
└── fmrb_ipc_transport.c / fmrb_ipc_transport.h # トランスポート層
```

### fmrb_mem - メモリ管理ライブラリ

```
lib/fmrb_mem/
├── fmrb_alloc.c      # 汎用アロケータ
└── fmrb_mempool.c    # メモリプール管理
```

## 主要コンポーネントの役割

| コンポーネント | 役割 |
|--------------|------|
| **app/** | アプリケーション実行環境、デフォルトアプリ(config/editor/shell) |
| **dev/** | デバイスドライバ(USB等) |
| **kernel/** | カーネル本体、システムタスク、GUI基盤 |
| **lib/fmrb_audio** | 音声出力制御(APUエミュレータ向け) |
| **lib/fmrb_gfx** | グラフィックス描画、ウィンドウ管理 |
| **lib/fmrb_hal** | ハードウェア抽象化(ESP32/Linux対応) |
| **lib/fmrb_ipc** | S3-WROVER間通信、ホスト通信 |
| **lib/fmrb_mem** | メモリアロケータ、プール管理 |
| **include/** | 公開API定義 |

## ファイル一覧

### ルート
- main.c - エントリポイント
- boot.c / boot.h - ブート処理
- CMakeLists.txt - ビルド設定

### app/
- fmrb_app.c - アプリケーション管理(プロセス管理、タスク生成、状態遷移)

### app/default_app/
**C拡張 (mrb/)**
- config.c - 設定アプリのC拡張
- editor.c - エディタアプリのC拡張
- shell.c - シェルアプリのC拡張

**Ruby実装 (mrblib/)**
- config.app.rb - 設定アプリ
- editor.app.rb - エディタアプリ
- shell.app.rb - シェルアプリ

### dev/usb/
- usb_task.c - USBホスト制御タスク

### include/
- fmrb_app.h - アプリケーション管理API
- fmrb_err.h - エラーコード定義
- fmrb_mem.h - メモリ管理API
- fmrb_task_config.h - タスク設定(優先度、スタックサイズ等)

### kernel/
- fmrb_kernel.c / fmrb_kernel.h - カーネル本体

**ホストタスク (host/)**
- host_task.c / host_task.h - ホスト環境用タスク

**C拡張 (mrb/)**
- kernel.c - カーネルRuby拡張
- system_gui.c - システムGUI拡張

**Ruby実装 (mrblib/)**
- kernel.rb - カーネルRuby実装
- system_gui.app.rb - システムGUIアプリ

### lib/fmrb_audio/
- fmrb_audio.c / fmrb_audio.h - オーディオ制御API
- CMakeLists.txt - ビルド設定

### lib/fmrb_gfx/
- fmrb_gfx.c / fmrb_gfx.h - グラフィックスAPI
- fmrb_gfx_commands.c / fmrb_gfx_commands.h - 描画コマンド実装
- fmrb_gfx_window.h - ウィンドウ管理
- CMakeLists.txt - ビルド設定

### lib/fmrb_hal/
**共通**
- fmrb_hal.c / fmrb_hal.h - HAL統合ヘッダ
- fmrb_hal_esp.h - ESP32固有定義
- fmrb_hal_freertos.h - FreeRTOS抽象化マクロ
- fmrb_hal_file.h - ファイルシステムAPI定義
- fmrb_hal_gpio.c / fmrb_hal_gpio.h - GPIO制御
- fmrb_hal_ipc.h - IPC抽象化定義
- fmrb_hal_spi.h - SPI抽象化定義
- fmrb_hal_time.c / fmrb_hal_time.h - 時刻管理
- fmrb_pin_assign.h - Pinアサイン定義
- CMakeLists.txt - ビルド設定

**ESP32プラットフォーム (platform/esp32/)**
- fmrb_hal_file_esp32.c - ESP32ファイルシステム(LittleFS)
- fmrb_hal_ipc_esp32.c - ESP32 IPC実装(SPI)
- fmrb_hal_spi_esp32.c - ESP32 SPI実装

**Linuxプラットフォーム (platform/linux/)**
- fmrb_hal_file_linux.c - Linuxファイルシステム(POSIX)
- fmrb_hal_ipc_linux.c - Linux IPC実装(Socket)
- fmrb_hal_spi_linux.c - Linux SPI実装(スタブ)

### lib/fmrb_ipc/
- fmrb_ipc_cobs.c / fmrb_ipc_cobs.h - COBS符号化(Consistent Overhead Byte Stuffing)
- fmrb_ipc_protocol.c / fmrb_ipc_protocol.h - プロトコル層(メッセージング)
- fmrb_ipc_transport.c / fmrb_ipc_transport.h - トランスポート層(送受信、再送制御)
- CMakeLists.txt - ビルド設定

### lib/fmrb_mem/
- fmrb_alloc.c - 汎用メモリアロケータ
- fmrb_mempool.c - メモリプール管理(アプリ毎の独立メモリ領域)
