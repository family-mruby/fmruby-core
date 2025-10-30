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

#### presym (Pre-Symbolization) について

presymはPicoRubyのビルド時に、Rubyコードを静的解析してシンボルIDを事前生成する仕組み。

**重要な制約:**
- Cコード内で `MRB_SYM(method_name)` や `MRB_SYM_E(method_name)` を使用する場合、そのシンボルは必ずRubyコード内で参照されている必要がある
- Rubyコード内で参照されていないシンボルは presym が生成せず、コンパイルエラーになる
- 実行時には到達しないダミーメソッド内でも、メソッド呼び出しを記述すればpresymが認識する

**presymエラーの対処方法:**
1. `MRB_SYM__xxx undeclared` エラーが出たら、該当CファイルでどのシンボルがMRB_SYM()で使われているか確認
2. 対応するRubyファイル（mrblib/*.rb）に、ダミーメソッドを作成してそのシンボルを参照
3. インスタンスメソッドの場合は、そのクラス内にダミーメソッドを配置（例: FAT::Dir, FAT::File）
4. Rubyコードを変更した場合は、必ず `rm -rf components/picoruby-esp32/picoruby/build/host` でビルドキャッシュをクリーンしてから再ビルド

**ダミーメソッドの例:**
```ruby
class FAT
  class Dir
    def self._dummy_for_presym
      dir = FAT::Dir.new("/")
      dir.findnext    # MRB_SYM(findnext) を生成
      dir.pat = ""    # MRB_SYM_E(pat) を生成
      dir.rewind      # MRB_SYM(rewind) を生成
    end
  end
end
```

**注意:**
- presymエラーが出た時に「ビルドキャッシュを疑う」ことは禁物。まずRubyコード内でのシンボル参照を確認すること
- ビルドキャッシュのクリーンが必要なのは、Rubyコードを変更した後に presym が更新されない場合のみ

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
    - **fmrb_link/** - S3⇔WROVER/ホスト間データリンク層プロトコル
      - メッセージはmsgpack形式、CRCチェック、再送機能
      - Linuxではソケット通信
      - COBS/CRC/フレーム処理/チャンク転送などを提供
    - **fmrb_gfx/** - グラフィックAPI
      - LovyanGFX+α (Window描画、ビットマップ転送等)
      - 内部ではfmrb_linkで描画コマンド送信
    - **fmrb_audio/** - オーディオAPI
      - APUエミュレータ向け音楽バイナリ転送、再生停止制御
      - 内部ではfmrb_linkでコマンド実行
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

TODO: 同じ意味のものが乱立してそうなので、統合する

### picoruby-esp32/CMakeList.txtで参照

#### IDF_TARGET=linux

IDFでlinuxターゲット指定してる場合に有効

### コード上で参照可能なdefine

####  CONFIG_IDF_TARGET_LINUX

IDFでlinuxターゲット指定してる場合に有効？

#### PICORB_PLATFORM_POSIX

`picoruby-esp32/picoruby/build_config/family_mruby_linux.rb` で定義。
IDFでlinuxターゲット指定してる場合に有効

#### FMRB_PLATFORM_LINUX

halのCMakeListsで定義。
fmrb_hal.hでもCONFIG_IDF_TARGET_LINUXに連動して定義している。

#### FMRB_PLATFORM_ESP32

halのCMakeListsで定義。
fmrb_hal.hでもCONFIG_IDF_TARGET_LINUXに連動して定義している。


## 参考情報

doc/ 以下参照
