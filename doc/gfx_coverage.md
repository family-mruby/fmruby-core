# LovyanGFX API カバー率レポート

更新日: 2025-10-14

## 概要

本ドキュメントは、fmruby-core グラフィックスサブシステムにおける LovyanGFX API の実装カバー率を分析したものです。

## サマリ

| カテゴリ | 実装済み | 未実装 | カバー率 |
|----------|----------|--------|----------|
| 基本図形描画 | 10 | 4 | 71% |
| テキスト描画 | 5 | 7 | 42% |
| 画面制御 | 3 | 0 | 100% |
| グラデーション描画 | 0 | 8 | 0% |
| ビットマップ/画像 | 0 | 5 | 0% |
| スムーズ描画 | 0 | 2 | 0% |
| クリッピング | 1 | 2 | 33% |
| **合計** | **19** | **28** | **約40%** |

## 実装済みAPI

### 基本図形描画 (10/14)

- [x] `drawPixel` - ピクセル描画
- [x] `drawFastVLine` - 高速垂直線描画
- [x] `drawFastHLine` - 高速水平線描画
- [x] `drawLine` - 2点間の直線描画
- [x] `drawRect` - 矩形の枠線描画
- [x] `fillRect` - 矩形の塗りつぶし
- [x] `drawRoundRect` - 角丸矩形の枠線描画
- [x] `fillRoundRect` - 角丸矩形の塗りつぶし
- [x] `drawCircle` - 円の枠線描画
- [x] `fillCircle` - 円の塗りつぶし
- [x] `drawEllipse` - 楕円の枠線描画
- [x] `fillEllipse` - 楕円の塗りつぶし
- [x] `drawTriangle` - 三角形の枠線描画
- [x] `fillTriangle` - 三角形の塗りつぶし
- [x] `drawArc` - 弧の描画
- [x] `fillArc` - 弧の塗りつぶし

### テキスト描画 (5/12)

- [x] `drawString` - 文字列描画
- [x] `drawChar` - 1文字描画
- [x] `setTextSize` - テキストサイズ倍率設定
- [x] `setTextColor` - テキスト前景色/背景色設定
- [x] `setCursor` - カーソル位置設定（ヘッダ定義済み）

### 画面制御 (3/3)

- [x] `fillScreen` - 画面全体を色で塗りつぶし
- [x] `clear` - 画面クリア（fillScreen経由）
- [x] `display` / `present` - ディスプレイバッファ更新

## 未実装API

### 高度な図形描画 (4)

- [ ] `drawBezier` - 2次/3次ベジェ曲線描画
- [ ] `drawEllipseArc` / `fillEllipseArc` - 楕円弧の描画
- [ ] `drawCircleHelper` / `fillCircleHelper` - 円描画ヘルパー
- [ ] `fillAffine` - アフィン変換での塗りつぶし

### グラデーション描画 (8)

- [ ] `drawGradientLine` - グラデーション直線描画
- [ ] `drawGradientHLine` - グラデーション水平線描画
- [ ] `drawGradientVLine` - グラデーション垂直線描画
- [ ] `fillGradientRect` - グラデーション矩形塗りつぶし
- [ ] `drawSmoothLine` - アンチエイリアス直線描画
- [ ] `drawWideLine` - 太線描画
- [ ] `drawWedgeLine` - くさび形線描画
- [ ] `drawSpot` / `drawGradientSpot` - グラデーションスポット描画

### スムーズ描画 (2)

- [ ] `fillSmoothRoundRect` - アンチエイリアス角丸矩形塗りつぶし
- [ ] `fillSmoothCircle` - アンチエイリアス円塗りつぶし

### ビットマップ/画像 (5)

- [ ] `drawBitmap` / `drawXBitmap` - ビットマップ描画
- [ ] `pushImage` / `pushImageDMA` - 画像バッファ転送
- [ ] `pushImageRotateZoom` - 回転/拡大縮小画像転送
- [ ] `fillRectAlpha` - アルファブレンド矩形塗りつぶし

### テキスト高度機能 (7)

- [ ] `setFont` - フォント設定
- [ ] `setTextDatum` - テキスト基準位置設定
- [ ] `setTextWrap` - テキスト折り返し有効化/無効化
- [ ] `textWidth` - テキスト幅取得（ピクセル単位）
- [ ] `fontHeight` - フォント高さ取得（ピクセル単位）
- [ ] `print` / `println` - Arduino Print API互換
- [ ] `printf` - フォーマット済みテキスト出力

### クリッピング/スクロール (4)

- [ ] `setClipRect` - クリッピング矩形設定（定義済みだが未実装）
- [ ] `clearClipRect` - クリッピング矩形クリア
- [ ] `setScrollRect` - スクロール領域設定
- [ ] `scroll` - 画面内容スクロール

### 色/パレット (2)

- [ ] `setPaletteColor` - パレットエントリ色設定
- [ ] `setColorDepth` - 色深度変更

## 推奨実装優先度

### 高優先度（GUI構築に必須）

GUI機能構築に不可欠なAPI:

1. **`setFont`** - 異なるテキストスタイル用のフォント選択
2. **`textWidth` / `fontHeight`** - テキストレイアウト計算
3. **`setClipRect` 実装** - 描画範囲外への描画防止
4. **`pushImage`** - アイコンや画像の表示

### 中優先度（リッチUIの機能）

より洗練されたユーザー体験を可能にするAPI:

5. **`fillRectAlpha`** - 半透明オーバーレイ
6. **`drawBezier`** - UI要素向けの滑らかな曲線
7. **`fillGradientRect`** - モダンなグラデーション背景
8. **`setTextDatum`** - 柔軟なテキスト配置
9. **`textWidth` / `fontHeight`** - テキスト寸法測定

### 低優先度（装飾的機能）

視覚的な磨き上げに有用なAPI:

10. **スムーズ描画API群** - アンチエイリアス図形
11. **太線/スポット描画** - 視覚効果
12. **アフィン変換** - 高度なグラフィックス

## 現在の状況評価

現在の実装で提供される機能:

- **堅牢な基盤**: すべての基本図形描画プリミティブが利用可能
- **基本的なテキストレンダリング**: 文字列と文字の描画が動作
- **画面管理**: バッファ制御とディスプレイ更新が機能

**対応可能**: テキスト、図形、基本レイアウトを用いたシンプルなGUIアプリケーション

**今後の課題**: 画像ベースUI、カスタムフォント、複雑なテキストレイアウト、高度な視覚効果

## 実装ファイル

### ESP32側（IPC送信側）
- `main/lib/fmrb_gfx/fmrb_gfx.c` - グラフィックスAPI実装
- `main/lib/fmrb_gfx/fmrb_gfx.h` - APIヘッダ定義

### Host側（IPC受信側）
- `host/sdl2/src/graphics_handler.cpp` - コマンド処理
- `host/common/graphics_commands.h` - コマンド構造体

### プロトコル
- `main/lib/fmrb_ipc/fmrb_ipc_protocol.h` - IPCメッセージ定義

## 備考

- すべての実装済み描画コマンドは msgpack ベースの IPC プロトコルを使用
- 色フォーマット: ARGB8888（アルファチャンネル付き32ビット）
- 座標系: LovyanGFX互換性のため int32_t を使用
- IPCデバッグログはcmakeオプションで有効化可能: `-DFMRB_IPC_DEBUG=ON`
