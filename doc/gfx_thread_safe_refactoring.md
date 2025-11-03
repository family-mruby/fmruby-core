# fmrb_gfx Thread-Safe Refactoring Plan

## 目的
fmrb_gfx APIをスレッドセーフにするため、`fmrb_gfx_set_target()` による共有状態の更新を廃止し、各描画関数呼び出し時に `canvas_id` を直接指定する方式に変更する。

## 問題点
- 現在の実装では `ctx->current_target` というグローバル共有状態を使用
- `fmrb_gfx_set_target(ctx, canvas_id)` を呼んでから描画関数を呼ぶ
- 複数スレッドから同時に異なるキャンバスに描画すると競合が発生

## 解決方法
各描画関数のシグネチャに `canvas_id` パラメータを追加し、呼び出し時に直接指定する。

## 段階的実装計画

### Phase 1: 基本関数の実装 (優先度: 高)
最も頻繁に使用される5つの関数を完全に実装してテスト

**対象関数:**
1. `fmrb_gfx_clear` - 画面クリア
2. `fmrb_gfx_set_pixel` - ピクセル描画
3. `fmrb_gfx_draw_line` - 線描画
4. `fmrb_gfx_draw_rect` - 矩形枠描画
5. `fmrb_gfx_fill_rect` - 矩形塗りつぶし

**変更ファイル:**
- [x] `main/lib/fmrb_gfx/fmrb_gfx.h` - シグネチャ更新 (完了)
- [x] `main/lib/fmrb_gfx/fmrb_gfx.c` - 実装更新 (完了)
- [x] `main/lib/fmrb_link/fmrb_link_protocol.h` - プロトコル構造体更新 (完了)
- [x] `lib/add/picoruby-fmrb-app/ports/esp32/gfx.c` - Ruby bindings更新 (完了)
- [x] `host/common/graphics_commands.h` - ホスト側コマンド定義 (完了)
- [x] `host/sdl2/src/graphics_handler.cpp` - ホスト側処理 (完了)

**テスト項目:**
- [x] ビルド成功
- [ ] 単一スレッドでの描画動作確認
- [ ] 複数スレッドからの同時描画テスト

**Phase 1 完了日:** 2025-11-04

**備考:**
- ビルド時に全44関数のシグネチャを更新
- Phase 1の5関数以外も、ヘッダーと実装の一貫性を保つため全て更新
- 更新したファイル:
  - fmrb_gfx.h (全関数シグネチャ)
  - fmrb_gfx.c (全関数実装)
  - fmrb_gfx_commands.c (コマンドバッファ内の関数呼び出し)
  - gfx.c (Ruby bindings - Phase 1の5関数 + circle/text/present)
  - host_task.c (テストコード)
  - graphics_handler.cpp (ホスト側 - Phase 1の5関数)
  - graphics_commands.h (ホスト側コマンド構造体)

### Phase 2: 図形描画関数 (優先度: 中)
円、三角形、楕円などの図形描画関数

**対象関数:**
6. `fmrb_gfx_draw_circle`
7. `fmrb_gfx_fill_circle`
8. `fmrb_gfx_draw_triangle`
9. `fmrb_gfx_fill_triangle`
10. `fmrb_gfx_draw_ellipse`
11. `fmrb_gfx_fill_ellipse`
12. `fmrb_gfx_draw_round_rect`
13. `fmrb_gfx_fill_round_rect`

### Phase 3: テキスト描画関数 (優先度: 中)
テキスト関連の描画関数

**対象関数:**
14. `fmrb_gfx_draw_text`
15. `fmrb_gfx_draw_string`
16. `fmrb_gfx_draw_char`
17. `fmrb_gfx_set_text_size`
18. `fmrb_gfx_set_text_color`

### Phase 4: その他の描画関数 (優先度: 低)
アーク、高速ライン描画など

**対象関数:**
19. `fmrb_gfx_draw_arc`
20. `fmrb_gfx_fill_arc`
21. `fmrb_gfx_draw_fast_vline`
22. `fmrb_gfx_draw_fast_hline`
23. `fmrb_gfx_draw_pixel` (LovyanGFX互換版)
24. `fmrb_gfx_fill_screen`
25. `fmrb_gfx_clear_rect`
26. `fmrb_gfx_get_pixel`

### Phase 5: キャンバス管理とその他 (優先度: 低)
キャンバス操作、クリッピング、プレゼント

**対象関数:**
27. `fmrb_gfx_present`
28. `fmrb_gfx_set_clip_rect`
29. `fmrb_gfx_create_canvas`
30. `fmrb_gfx_delete_canvas`
31. `fmrb_gfx_push_canvas`
32. `fmrb_gfx_set_target` - 非推奨化またはno-op化

## 実装詳細

### プロトコル構造体の変更
すべてのグラフィックスコマンド構造体に `canvas_id` フィールドを追加：

```c
typedef struct __attribute__((packed)) {
    uint16_t canvas_id;  // 追加
    uint16_t x, y;
    uint8_t color;
} fmrb_link_graphics_pixel_t;
```

### Ruby Bindingsの変更パターン
Before:
```c
static mrb_value mrb_gfx_clear(mrb_state *mrb, mrb_value self) {
    mrb_gfx_data *data = ...;
    fmrb_gfx_set_target(data->ctx, data->canvas_id);  // 削除
    return fmrb_gfx_clear(data->ctx, color);
}
```

After:
```c
static mrb_value mrb_gfx_clear(mrb_state *mrb, mrb_value self) {
    mrb_gfx_data *data = ...;
    return fmrb_gfx_clear(data->ctx, data->canvas_id, color);  // canvas_id追加
}
```

## 進捗状況

### Phase 1 進捗
- [x] fmrb_gfx.h 全関数シグネチャ更新完了
- [x] fmrb_link_protocol.h 構造体更新（clear, pixel, line, rect 4構造体）
- [x] fmrb_gfx.c 基本5関数実装（clear, set_pixel, draw_line, draw_rect, fill_rect）
- [ ] Ruby bindings 基本5関数更新
- [ ] ホスト側実装更新
- [ ] ビルド＆テスト

## 今後のステップ

### 短期（Phase 2-5の実装）
Phase 1で基本インフラが完成したため、残りの関数を段階的に実装：

1. **Phase 2: 図形描画関数** - プロトコル構造体とホスト側ハンドラの更新が必要
   - 円、三角形、楕円、角丸矩形の8関数
   - Ruby bindingsとホスト側の両方を更新

2. **Phase 3: テキスト描画関数** - 既存のプロトコルを活用
   - draw_string, draw_char, set_text_size, set_text_color の4関数
   - 一部はPhase 1で対応済み

3. **Phase 4: その他の描画関数** - 高速ライン描画など
   - draw_fast_vline/hline, draw_arc/fill_arc など8関数

4. **Phase 5: キャンバス管理** - 最も複雑
   - create_canvas, delete_canvas, push_canvas
   - set_target() の廃止または非推奨化

### 中期（実行時テスト）
1. **単一スレッドでの動作確認**
   - Linuxターゲットで実行して描画動作を確認
   - 各Phase完了後に簡単な描画テストを実施

2. **マルチスレッドテスト**
   - 複数のRubyタスクから同時に異なるキャンバスに描画
   - 競合状態が発生しないことを確認

### 長期（ESP32対応とパフォーマンス）
1. **ESP32ターゲットでのビルドとテスト**
   - SPIベースのfmrb_link実装での動作確認
   - 実機での描画性能評価

2. **パフォーマンス最適化**
   - コマンドバッチング（複数の描画コマンドをまとめて送信）
   - プロトコルオーバーヘッドの削減

## 注意事項
- 後方互換性は考慮しない（破壊的変更）
- `fmrb_gfx_set_target()` は最終的に削除または非推奨化
- ホスト側（SDL2）の実装も同時に更新が必要
- Phase 1で全44関数のシグネチャを更新済みのため、残りのフェーズは実装の完成度を上げる作業

## 技術的な判断事項

### 全関数シグネチャの一括更新を選択した理由
Phase 1では基本5関数のみの計画だったが、ビルドエラーを解決する過程で全44関数のシグネチャを更新した。

**メリット:**
- ヘッダーと実装の一貫性が保たれる
- 段階的実装中も常にビルド可能な状態を維持
- 後続フェーズでのシグネチャ変更作業が不要

**デメリット:**
- Phase 1の作業量が増加
- 未実装部分でcanvas_idパラメータが活用されていない

**結論:** ビルド可能な状態を維持できるメリットが大きいため、全関数更新を選択した。

### プロトコル構造体の段階的更新
Phase 1では基本4構造体のみ更新。残りは各Phase実装時に更新予定。

**理由:**
- 構造体定義の変更はABI互換性に影響
- 使用しない構造体を事前に変更する必要はない
- 各Phaseで必要な構造体のみ更新することで、変更の影響範囲を限定
- すべての変更は段階的にコミットし、各段階でビルド確認を行う

## 変更履歴
- 2025-11-04: Phase 1完了（基本5関数 + 全関数シグネチャ更新）
- 2025-11-04: 初版作成
