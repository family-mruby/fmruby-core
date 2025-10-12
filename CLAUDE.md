# Family murby Graphics-Audio firmware

## 基本的な注意事項

- コミュニケーションは日本語で
- 勝手にgit操作しない

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

## ビルド方法

### ESP32ターゲット
```
docker run --rm --group-add=dialout --group-add=plugdev --privileged $DEVICE_ARGS --user $(id -u):$(id -g) -v $PWD:/project -v /dev/bus/usb:/dev/bus/usb esp32_build_container:v5.5.1 idf.py build
```

### Linuxターゲット（開発・テスト用）
```
docker run --rm --user $(id -u):$(id -g) -v $PWD:/project esp32_build_container:v5.5.1 idf.py set-target linux
docker run --rm --user $(id -u):$(id -g) -v $PWD:/project esp32_build_container:v5.5.1 idf.py build
```

### 簡単実行用スクリプト
```
make linux-build  # Linuxターゲットビルド
make esp32-build  # ESP32ターゲットビルド
```

## 設計指針

### プログラムコンポーネント

以下のコンポーネントは勝手に削除したり、ビルド対象から外してはいけない

- components
  - picoruby-esp32
    https://github.com/picoruby/picoruby-esp32.git
    これはsubmoduleである。勝手に編集してはいけない。
    これをビルドするための設定は「https://github.com/picoruby/R2P2-esp32」に存在している。
- lib/
  - picoruby-fmrb
    - PicoRuby向けのmrbgem
      PicoRubyをForkすることを避けるためにパッチの形で管理
      ヒルド時にpicoruby-esp32以下にコピーする
- main
  - Family mruby OS
    PicoRubyで動くWindow3.1ライクなGUIシステム。マルチタスク機能も提供する
  - Family mruby Hardware Abstraction Layer(fmrb)
    サブコア(ESP32-wROVER)にSPIで通信する機能、ESP32のSDK、FreeRTOS関連にアクセスするための抽象化層。
    Linuxターゲットビルド時はソケットで、SDL2を実行しているプロセスに通信する。将来的にはWASMなどでも動かせるような抽象化を提供したい。
- main/lib
  - fmrb_hal_*          // OS寄りの機能。時刻、スリープ、IPC(送受信/共有メモリ)、SPI/I2C/GPIO、DMA、ロック等
  - fmrb_ipc_*          // S3<->WROVER/ホストのプロトコル定義と再送/水位制御
  - fmrb_gfx_*          // 上位: コマンドリスト/ウィンドウ描画/テキスト/Present
  - fmrb_audio_*        // 上位: サンプルキュー/ミキサ/ストリーム制御/BGM/SE

- その他
  - TinyUSB


