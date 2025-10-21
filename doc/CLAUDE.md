# Family murby Graphics-Audio firmware

## 基本的な注意事項

- 会話のcontextが上限に達して、新しいコンテキストに引き継ぐ際は、まず最初にこの文章を読むこと
- コミュニケーションは日本語で
- 勝手にgit操作しない
- ASCII以外の絵文字等は使用しない。✓など使用しない。
- .gitsubmoduleに含まれるディレクトリは編集禁止
- 問題を解決する際に、ソースコードをビルド対象から外すことは本質的ではないので、禁止
- コミットログの作成を依頼された場合は、数行程度で完結にまとめた英文を提供する

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


