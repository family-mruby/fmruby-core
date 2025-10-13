# FMRuby Host Environment

ホスト環境でのFMRubyテスト・開発用プログラムです。

## 概要

このディレクトリには、FMRuby Core（ESP32ターゲット）の開発・テスト用のホスト環境が含まれています。
実際のESP32ハードウェアを使わずに、PC上でFMRubyのグラフィック・オーディオ機能をテストできます。

## 構成

```
host/
├── common/           # 共通プロトコル定義
│   ├── protocol.h
│   ├── graphics_commands.h
│   └── audio_commands.h
├── sdl2/            # SDL2実装
│   ├── src/
│   ├── include/
│   └── CMakeLists.txt
└── wasm/            # WASM実装（将来）
```

## 通信プロトコル

FMRuby CoreとHost間の通信は以下のプロトコルで行われます：

### メッセージ形式
```c
typedef struct {
    uint32_t magic;      // FMRB_MAGIC (0x464D5242)
    uint32_t type;       // メッセージタイプ
    uint32_t size;       // メッセージ全体サイズ
} fmrb_message_header_t;
```

### メッセージタイプ
- `FMRB_MSG_GRAPHICS` (1): グラフィックコマンド
- `FMRB_MSG_AUDIO` (2): オーディオコマンド

### グラフィックコマンド
- `FMRB_GFX_CMD_CLEAR`: 画面クリア
- `FMRB_GFX_CMD_DRAW_PIXEL`: ピクセル描画
- `FMRB_GFX_CMD_DRAW_LINE`: 線描画
- `FMRB_GFX_CMD_DRAW_RECT`: 矩形描画
- `FMRB_GFX_CMD_FILL_RECT`: 矩形塗りつぶし
- `FMRB_GFX_CMD_DRAW_TEXT`: テキスト描画
- `FMRB_GFX_CMD_PRESENT`: 画面更新

### オーディオコマンド
- `FMRB_AUDIO_CMD_LOAD_BINARY`: 音楽データロード
- `FMRB_AUDIO_CMD_PLAY`: 音楽再生
- `FMRB_AUDIO_CMD_STOP`: 再生停止
- `FMRB_AUDIO_CMD_PAUSE`: 一時停止
- `FMRB_AUDIO_CMD_RESUME`: 再生再開
- `FMRB_AUDIO_CMD_SET_VOLUME`: 音量設定

## SDL2実装

SDL2を使用したホスト環境実装です。

### ビルド方法

```bash
cd host/sdl2
mkdir build
cd build
cmake ..
make
```

### 実行方法

```bash
./fmrb_host_sdl2
```

### 依存関係

- SDL2 development libraries
- CMake 3.16+
- C99対応コンパイラ

### 機能

- 320x240ピクセルのウィンドウでグラフィック表示
- 基本的な図形描画（ピクセル、線、矩形）
- テキスト描画（プレースホルダー実装）
- 音楽ファイルの読み込み・再生
- Unix socketでのFMRuby Coreとの通信

## 使用方法

1. SDL2ホストプログラムを起動
2. FMRuby Coreプログラムを起動（Linuxターゲットでビルドしたもの）
3. FMRuby CoreからUnix socket（/tmp/fmrb_socket）経由でコマンドを送信
4. SDL2ウィンドウに描画結果が表示される

## 将来の拡張

- WASM実装でWebブラウザ上での動作
- より高度なテキストレンダリング
- オーディオエフェクト・ミキシング機能
- デバッグ・プロファイリング機能