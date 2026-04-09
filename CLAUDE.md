# Family murby Core firmware

## 基本的な注意事項

- コミュニケーションは日本語
  - ただし指示は英語の場合もある。私の指示が英語の場合も返答は日本語にしてください。
- 勝手にgit操作しない
- ASCII以外の絵文字等は使用しない。✓など使用しない。
- 問題を解決する際に、ソースコードをビルド対象から外すことは本質的ではないので、禁止
- コミットログの作成を依頼された場合は、数行程度で完結にまとめた英文を提供する
- ソースコード上に記載するコメントは英語で記述する
- ソースコードのファイルは勝手に削除しない

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
- 編集をした場合、Legacyコードは残さず消す
- Doxygenコメントは、基本的に他のモジュールから参照されるヘッダのみに記載。Cソースには不要。（メンテナンスしきれないため）

## ビルド方法

```
rake build:linux  # Linuxターゲットビルド
rake build:esp32  # ESP32ターゲットビルド
rake host:build  # Hostビルド
rake -T # その他のコマンドの使い方
```

### 注意

- lib/ 以下のファイルを編集した場合は、ビルド前に `rake clean` を実行すること
- ターゲットをlinux - ESP32で切り替えてビルドするときは、ビルド前に `rake clean_all` を実行すること
- プログラムの実行にはGUIが必要なため、CLAUDE Code では実行できない。ユーザがテストを行う。

## ハードウェア構成

### 本番構成

fmruby-coreは、ESP32-S3で実行する
映像出力(NTSC)と音声出力（APUエミュレータを利用したI2S）は子マイコン（ESP32-WROVER）で実行する
S3とWROVERの間はSPI通信

### 開発環境構成（Linux）

fmruby-coreは、WSL2で動くコンテナで実行する
映像出力と音声出力は、WSL2側で動く別プロセスにソケット通信で通信して実現する。その別プロセスでは、SDL2を動かす

## 参考情報

doc/ 以下参照
