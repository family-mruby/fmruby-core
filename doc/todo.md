# TODO

## ホストのCanvas描画

### Phase 1: Linux版実装
- Canvasごとに２枚のスプライト
  - １枚目は描画用
  -２枚目はレンダリング用
- Presentコマンドでレンダリング用にPushする
- Canvas構造体を作る
  - スプライトのポインタ
  - Z Order
  - dirty管理
  - 可視不可視フラグ
- render_frame()の実装
  - Z-orderの低い方からCanvasを再帰的に描画
  - SystemAppのOderは0固定

### Phase 2: 共通化 (ESP32移植準備)
- Canvas管理ロジックをプラットフォーム非依存に抽出
- 描画バックエンドの抽象化インターフェース定義

### Phase 3: ESP32移植 (将来)
- SPI server実装
- WROVER通信プロトコル実装
- FreeRTOSタスク統合

### 将来拡張
- 子Canvasサポート (親子Chain構造)
- 再帰的描画 (親→子の順)


# 資料

ESP32とLinux(Host)の違い

Linux (Host) 環境

S3 ⇔ Host: Socketソケット通信
映像出力: SDL2
2スレッド構造 (Panel_sdlメインループ + user_func)

ESP32 (本番) 環境

S3 ⇔ WROVER: SPI通信
映像出力: WROVER (NTSC)
FreeRTOS タスク構造

移植を考慮した設計

共通化すべき部分 (プラットフォーム非依存)

graphics_handler の構造:
├ canvas_state_t 構造体定義
├ Canvas管理配列 (g_canvases[])
├ graphics_handler_render_frame()
│   └ Z-order順でCanvas合成
└ 各描画コマンド処理
    └ draw_buffer への描画
これらはESP32でもLinuxでも同じロジックで動作

プラットフォーム依存部分
Linux版 (host/sdl2/)
// main.cpp: user_func()
while (running && *thread_running) {
    socket_server_process();           // ソケット経由
    graphics_handler_render_frame();   // 共通
    lgfx::delay(16);
}
ESP32版 (main/drivers/ など)
// spi_server タスク (新規作成)
void spi_server_task(void* param) {
    while (1) {
        spi_server_process();              // SPI経由
        graphics_handler_render_frame();   // 共通！
        fmrb_task_delay(16);
    }
}

ディレクトリ構成案
fmruby-core/
├ host/
│ └ sdl2/
│   ├ src/
│   │ ├ main.cpp                    // Linux専用
│   │ ├ socket_server.c             // Linux専用
│   │ ├ graphics_handler.cpp        // ← 共通化候補
│   │ └ graphics_handler.h
│   └ common/                        // 新規
│     └ fmrb_link_cobs.h            // 既存共通
│
├ main/
│ ├ drivers/
│ │ └ spi_server.c                  // ESP32専用 (新規作成)
│ │
│ └ lib/
│   └ fmrb_graphics/                // 新規: 共通Graphics層
│     ├ fmrb_canvas.c               // Canvas管理 (共通)
│     ├ fmrb_canvas.h
│     └ CMakeLists.txt
│
└ components/
  └ graphics_common/                 // または既存コンポーネント配下
    ├ canvas_manager.c               // プラットフォーム非依存
    └ canvas_manager.h

共通化のポイント
1. Canvas管理を抽象化
// canvas_manager.h (共通)
typedef struct {
    uint8_t canvas_id;
    void* draw_buffer;      // LGFXまたはFreeRTOS heap
    void* render_buffer;
    int16_t z_order;
    bool is_visible;
    // ... 他のフィールド
} fmrb_canvas_t;

// プラットフォーム非依存API
fmrb_canvas_t* fmrb_canvas_create(uint8_t id, uint16_t w, uint16_t h);
void fmrb_canvas_destroy(fmrb_canvas_t* canvas);
void fmrb_canvas_present(fmrb_canvas_t* canvas);
void fmrb_canvas_render_all(void);  // render_frame()の共通実装
2. 描画バックエンドの抽象化
// platform_gfx.h (インターフェース)
typedef struct {
    void (*fillRect)(void* buffer, int16_t x, int16_t y, ...);
    void (*drawLine)(void* buffer, ...);
    void (*pushSprite)(void* src, void* dst, ...);
    // ...
} platform_gfx_ops_t;

// Linux版: LovyanGFX実装
// ESP32版: LovyanGFX実装 (WROVERへSPI転送)
3. render_frame()の共通化
// canvas_manager.c (共通)
void fmrb_canvas_render_all(void) {
    // Z-orderソート
    qsort(g_canvases, g_canvas_count, ...);
    
    // 全Canvas合成
    for (int i = 0; i < g_canvas_count; i++) {
        fmrb_canvas_t* canvas = &g_canvases[i];
        if (canvas->is_visible) {
            // プラットフォーム依存のpushSprite呼び出し
            platform_gfx_push_sprite(
                canvas->render_buffer,
                g_screen_buffer,
                canvas->push_x,
                canvas->push_y
            );
        }
    }
}

ESP32移植時の実装ステップ

Phase 1: Linux版で実装・検証
Canvas管理構造
ダブルバッファリング
render_frame()実装

Phase 2: 共通化リファクタリング
canvas_manager.c/h を抽出
プラットフォーム依存部分を分離

Phase 3: ESP32実装
spi_server.c 実装
platform_gfx_ops_t のESP32版実装
FreeRTOSタスクとして統合

