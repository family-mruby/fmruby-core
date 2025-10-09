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

