# Family murby Graphics-Audio firmware

## 基本的な注意事項

- 会話のcontextが上限に達して、新しいコンテキストに引き継ぐ際は、まず最初にこの文章を読むこと
- コミュニケーションは日本語
  - ただし指示は英語の場合もある。私の指示が英語の場合も返答は日本語にしてください。
- 勝手にgit操作しない
- ASCII以外の絵文字等は使用しない。✓など使用しない。
- .gitsubmoduleに含まれるディレクトリは編集禁止
- 問題を解決する際に、ソースコードをビルド対象から外すことは本質的ではないので、禁止
- コミットログの作成を依頼された場合は、数行程度で完結にまとめた英文を提供する
- ソースコード上に記載するコメントは英語で記述する

### 開発時の注意

- mrbgem で ESP32やFreeRTOSのヘッダを利用するものは、`components/picoruby-esp32/CMakeLists.txt` の `set(PICORUBY_SRCS` でビルド管理する。

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

- components
  - picoruby-esp32
    https://github.com/picoruby/picoruby-esp32.git
    これはForkしたものであり編集可能であるが、なるべきFork元からの変更の範囲が限定されるよう注意する。
  - msgpack-esp32
    主にIPCで利用することを想定している
  - mem_allocator
    https://github.com/mattconte/tlsf.git
    TLSF (Two Level Segregated Fit) メモリアロケータ。
    Prismパーサーのメモリ割り当てに使用。O(1)の割り当て/解放性能を持つ。
    ホストビルド (picorbc) では288KB、ターゲット実行時 (eval等) では64KBのプールを使用。
- lib/
  - submoduleを編集必要なときに差分を置く
    差分ファイルは、lib/patch に配置して、rake setup でsubmodule配下にコピーする。
  - lib/patch/prism_xallocator.h - Prismパーサー用カスタムアロケータヘッダ (xmalloc等をTLSFにマッピング)
  - lib/patch/prism_alloc.c - TLSF実装 (ホスト/ターゲット両対応、PRISM_BUILD_HOSTで切り替え)
  - lib/patch/mruby-compiler2-mrbgem.rake - mruby-compiler2ビルド設定 (TLSF組み込み、PRISM_BUILD_HOST定義)
  - lib/patch/mruby-compiler2-compile.c - スレッドセーフなcompile.c (マルチタスク環境でMutex保護、eval/Sandboxコンパイル時の競合防止)
- main
  - Family mruby OS
    PicoRubyで動くWindow3.1ライクなGUIシステム。マルチタスク機能も提供する
  - Family mruby Hardware Abstraction Layer(fmrb)
    サブコア(ESP32-wROVER)にSPIで通信する機能、ESP32のSDK、FreeRTOS関連にアクセスするための抽象化層。
    Linuxターゲットビルド時はソケットで、SDL2を実行しているプロセスに通信する。将来的にはWASMなどでも動かせるような抽象化を提供したい。
- main/lib
  - fmrb_mem          // メモリアロケータ。現在はfmrb_alloc.c (汎用アロケータ) のみ。
  - fmrb_hal          // OS寄りの機能。時刻、スリープ、IPC(送受信/共有メモリ)、SPI/I2C/GPIO、DMA、ロック等
  - fmrb_ipc          // S3<->WROVER/ホストのプロトコル定義と再送/水位制御
    LinuxではSocket通信になる。メッセージはmsgpackを利用する。
    メッセージ単位で、CRCのチェックを行い、エラーが起きた場合は必要がある場合は再送する。
  - fmrb_gfx          // 上位: LovyanGFX＋α（Window描画、ビットマップ転送など）のAPIをラップした形。
    内部では、IPCで描画コマンドを送る。
  - fmrb_audio        // 上位: APUエミュレータ向け音楽バイナリ転送、再生停止制御。現状はESP32専用。Linux向けはスケルトンのみでOK
    内部では、IPCを使ってコマンドを実行する
  - fmrb_input
    キー入力のための抽象化
    Linuxではhost/sdl2プロセスと通信。SDL2で受信した結果を渡す
    ESP32では、USB HostのHIDでつながったKeyboardとMouse操作に対応
- host/
  PC環境で動かすためのソース。独立したプロセスで、fmrb-coreと通信する。

- その他
  - TinyUSB


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