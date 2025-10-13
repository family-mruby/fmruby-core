# Family murby Graphics-Audio firmware

## 基本的な注意事項

- コミュニケーションは日本語で
- 勝手にgit操作しない
- ASCII以外の絵文字等は使用しない。✓など使用しない。
- .gitsubmoduleに含まれるディレクトリは編集禁止

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

## 設計指針

### プログラムコンポーネント

以下のコンポーネントは勝手に削除したり、ビルド対象から外してはいけない

- components
  - picoruby-esp32
    https://github.com/picoruby/picoruby-esp32.git
    これはForkしたものであり編集可能であるが、なるべきFork元からの変更の範囲が限定されるよう注意する。
  - msgpack-esp32
    主にIPCで利用することを想定している
- lib/
  - submoduleを編集必要なときに差分を置く
- main
  - Family mruby OS
    PicoRubyで動くWindow3.1ライクなGUIシステム。マルチタスク機能も提供する
  - Family mruby Hardware Abstraction Layer(fmrb)
    サブコア(ESP32-wROVER)にSPIで通信する機能、ESP32のSDK、FreeRTOS関連にアクセスするための抽象化層。
    Linuxターゲットビルド時はソケットで、SDL2を実行しているプロセスに通信する。将来的にはWASMなどでも動かせるような抽象化を提供したい。
- main/lib
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


