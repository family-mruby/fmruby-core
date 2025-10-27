# Family murby Graphics-Audio firmware

## 基本的な注意事項

- 会話のcontextが上限に達して、新しいコンテキストに引き継ぐ際は、まず最初にこの文章を読むこと
- コミュニケーションは日本語
  - ただし指示は英語の場合もある。私の指示が英語の場合も返答は日本語にしてください。
- 勝手にgit操作しない
- ASCII以外の絵文字等は使用しない。✓など使用しない。
- 問題を解決する際に、ソースコードをビルド対象から外すことは本質的ではないので、禁止
- コミットログの作成を依頼された場合は、数行程度で完結にまとめた英文を提供する
- ソースコード上に記載するコメントは英語で記述する

### 開発時の注意

- .gitsubmoduleに含まれるディレクトリは編集禁止
- sdkconfigおよびsdkconfig.defaults は編集禁止
  - sdkconfig に関する変更が必要なときは、編集せずに、提案すること
- mrbgem で ESP32やFreeRTOSのヘッダを利用するものは、`components/picoruby-esp32/CMakeLists.txt` の `set(PICORUBY_SRCS` でビルド管理する。
- main/以下のコードの関数の戻り値定義は `fmrb_err.h` を標準とする
- GPIOのPinアサインは、 `fmrb_pin_assign.h` を参照する
- 素のmallocは使わず、fmrb_mem.h の関数を利用する。もし少量のメモリならファイルスコープのstatic配列変数を利用することを検討する
  - mruby実行タスクでは、fmrb_mallocを利用して、その他のmain/以下のOS関連ではfmrb_sys_mallocを利用する。
- シンボリックリンクの仕様は原則禁止

## 目的

ESP32-S3-N16R8 をターゲットとして、以下の２つの機能を提供する。
- Family mruby OS
  - 関連する機能にはfmrbというprefixを用いる
- mruby実行エンジン
- USBホスト
  - Keyboard/Mouse
- SDカード

映像音声は、別のESP32で動作する。SPI経由のRPCで利用する。

API仕様は検討中。

## ハードウェア構成

### 本番構成

fmruby-coreは、ESP32-S3で実行する
映像出力(NTSC)と音声出力（APUエミュレータを利用したI2S）は子マイコン（ESP32-WROVER）で実行する
S3とWROVERの間はSPI通信

### 開発環境構成（Linux）

fmruby-coreは、WSL2で動くプロセスで実行する（Dockerコンテナになるかもしれない）
映像出力と音声出力は、WSL2側で動く別プロセスにソケット通信で通信して実現する。その別プロセスでは、SDL2を動かす。（将来的にはSDL2対応をベースに、WASMで動かしたりもしてみたい）

## ビルド方法

```
rake build:linux  # Linuxターゲットビルド
rake build:esp32  # ESP32ターゲットビルド
rake -T # その他のコマンドの使い方
```

注意
ターゲットをlinux - ESP32で切り替えてビルドするときは、ビルド前に `rake clean` を実行すること 

## 設計指針

### プログラムコンポーネント

以下のコンポーネントは勝手に削除したり、ビルド対象から外してはいけない

- **components/** - ESP-IDFコンポーネント (submodule含む)
  - **picoruby-esp32/** (submodule)
    - https://github.com/picoruby/picoruby-esp32.git
    - Forkしたものであり編集可能だが、Fork元からの変更範囲を最小限に
    - **picoruby/** (submodule)
      - 編集禁止。編集したいときは、lib/patch にコードを置いてRakefile でコピーする
  - **msgpack-esp32/** (submodule)
    - 主にIPCで利用。MessagePack C実装
  - **mem_allocator/** (submodule)
    - https://github.com/mattconte/tlsf.git
    - TLSF (Two Level Segregated Fit) メモリアロケータ
    - main以下のメモリ割り当てに使用。O(1)の割り当て/解放性能
  - **esp_littlefs/** (submodule)
    - LittleFS ファイルシステム実装
  - **tar_lib/** (submodule)
    - microtar - TARアーカイブ処理
  - **toml_parser/** (submodule)
    - tomlc99 - TOML設定ファイルパーサー

- **lib/** - submodule編集用差分とfmruby独自mrbgem
  - **lib/mrbgem/** - fmruby独自のmrbgem実装
    - **picoruby-fmrb-app/** - Family mruby アプリケーションAPI
    - **picoruby-fmrb-kernel/** - Family mruby カーネルAPI
  - **lib/patch/** - submodule用パッチファイル
    - submoduleを編集する際は、差分ファイルをここに配置
    - `rake setup`でsubmodule配下にコピーされる
    - **compiler/** - mruby-compiler2関連パッチ
      - `prism_xallocator.h` - Prismパーサー用カスタムアロケータヘッダ
      - `prism_alloc.c` - TLSF実装 (ホスト/ターゲット両対応)
      - `mruby-compiler2-mrbgem.rake` - ビルド設定 (TLSF組み込み)
      - `mruby-compiler2-compile.c` - スレッドセーフなcompile.c (Mutex保護)
    - **esp_littlefs/** - esp_littlefs CMakeLists.txt パッチ
    - **picoruby-env/** - picoruby-env パッチ
    - **picoruby-filesystem-fat/** - picoruby-filesystem-fat パッチ
    - **picoruby-machine/** - picoruby-machine パッチ
    - **picoruby-mruby/** - picoruby-mruby パッチ
    - `family_mruby_linux.rb` - Linux向けビルド設定
    - `family_mruby_esp32.rb` - ESP32向けビルド設定

- **main/** - Family mruby OS本体
  - **app/** - アプリケーション層
  - **kernel/** - カーネル層
    - PicoRubyで動くWindow3.1ライクなGUIシステム
    - マルチタスク機能を提供
  - **drivers/** - デバイスドライバ
    - TinyUSBによるUSB HOST
    - UART0 によるPC-ESP32間ファイルR/W
  - **include/** - 共通ヘッダファイル
  - **lib/** - Hardware Abstraction Layer (HAL)
    - サブコア(ESP32-WROVER)とSPI通信、ESP32 SDK/FreeRTOS抽象化
    - Linuxターゲット時はソケット経由でSDL2プロセスと通信
    - **fmrb_mem/** - メモリアロケータ (fmrb_alloc.c等)
    - **fmrb_hal/** - OS基盤機能
      - 時刻、スリープ、IPC、SPI/I2C/GPIO、DMA、ロック等
      - `platform/esp32/` - ESP32実装
      - `platform/posix/` - POSIX/Linux実装
    - **fmrb_ipc/** - S3⇔WROVER/ホスト間プロトコル
      - メッセージはmsgpack形式、CRCチェック、再送機能
      - Linuxではソケット通信
    - **fmrb_gfx/** - グラフィックAPI
      - LovyanGFX+α (Window描画、ビットマップ転送等)
      - 内部ではIPCで描画コマンド送信
    - **fmrb_audio/** - オーディオAPI
      - APUエミュレータ向け音楽バイナリ転送、再生停止制御
      - 内部ではIPCでコマンド実行
    - **fmrb_toml/** - TOML設定ファイル処理

- **host/** - PC環境用プロセス
  - fmrb-coreと通信する独立プロセス
  - **sdl2/** - SDL2ベース実装
  - **LovyanGFX/** - グラフィックライブラリ
  - **common/** - 共通コード

- **docker/** - ビルド環境
  - ESP-IDF用Dockerコンテナ定義

- **flash/** - フラッシュファイルシステム内容
  - **app/** - アプリケーションファイル
  - **etc/** - 設定ファイル
  - **home/** - ユーザーファイル

- **tool/** - 開発ツール類

- **doc/** - ドキュメント
  - 設計資料、仕様書等

## コンパイル定義

### CMakeList.txtで参照

#### IDF_TARGET=linux

IDFでlinuxターゲット指定してる場合に有効

### コード上で参照

####  CONFIG_IDF_TARGET_LINUX

IDFでlinuxターゲット指定してる場合に有効？

#### FMRB_PLATFORM_LINUX

halのCMakeListsで定義。
fmrb_hal.hでもCONFIG_IDF_TARGET_LINUXに連動して定義している。

#### FMRB_PLATFORM_ESP32

halのCMakeListsで定義。
fmrb_hal.hでもCONFIG_IDF_TARGET_LINUXに連動して定義している。



## 参考情報

### ESP-IDFのLinuxターゲットについて

POSIX/Linux Simulator Approach
The FreeRTOS POSIX/Linux simulator is available on ESP-IDF as a preview target already. This simulator allows ESP-IDF components to be implemented on the host, making them accessible to ESP-IDF applications when running on host. Currently, only a limited number of components are ready to be built on Linux. Furthermore, the functionality of each component ported to Linux may also be limited or different compared to the functionality when building that component for a chip target. For more information about whether the desired components are supported on Linux, please refer to Component Linux/Mock Support Overview.

Note that this simulator relies heavily on POSIX signals and signal handlers to control and interrupt threads. Hence, it has the following limitations:

Functions that are not async-signal-safe, e.g. printf(), should be avoided. In particular, calling them from different tasks with different priority can lead to crashes and deadlocks.

Calling any FreeRTOS primitives from threads not created by FreeRTOS API functions is forbidden.

FreeRTOS tasks using any native blocking/waiting mechanism (e.g., select()), may be perceived as ready by the simulated FreeRTOS scheduler and therefore may be scheduled, even though they are actually blocked. This is because the simulated FreeRTOS scheduler only recognizes tasks blocked on any FreeRTOS API as waiting.

APIs that may be interrupted by signals will continually receive the signals simulating FreeRTOS tick interrupts when invoked from a running simulated FreeRTOS task. Consequently, code that calls these APIs should be designed to handle potential interrupting signals or the API needs to be wrapped by the linker.

Since these limitations are not very practical, in particular for testing and development, we are currently evaluating if we can find a better solution for running ESP-IDF applications on the host machine.

Note furthermore that if you use the ESP-IDF FreeRTOS mock component (tools/mocks/freertos), these limitations do not apply. But that mock component will not do any scheduling, either.

Note

The FreeRTOS POSIX/Linux simulator allows configuring the Amazon SMP FreeRTOS version. However, the simulation still runs in single-core mode. The main reason allowing Amazon SMP FreeRTOS is to provide API compatibility with ESP-IDF applications written for Amazon SMP FreeRTOS.