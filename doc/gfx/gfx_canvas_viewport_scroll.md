# Canvas Viewport スクロール (SET_CANVAS_VIEWPORT) — P4/PPA 活用

2026-07-06 設計・実装。RPGデモのマップスクロール高速化を動機とした、
P4 (Modern/Tab5) コンポジタの新機能。

## 背景 / 問題

RPGデモ (flash/app/game/rpg_demo) は 176x176 のビューポートを
64x64タイル (1024x1024px) のマップ上でスクロールする際、毎フレーム
ビューポート全域を fill_rect + 約150回の draw_tile RPC で再描画して
いた (スライド1歩 = 4ステップ x 33ms)。リンク帯域・描画とも P4 では
律速となりスクロールが非常に遅い。

## P4 コンポジタの仕組み (前提)

display_p4_task.cpp render_frame() は毎フレーム:

1. 全 canvas を z 順に PPA **Blend** で 426x240 RGB565 フレームバッファへ合成
   (canvas の画面位置 push_x/y は blend の block_offset レジスタに
   入るだけなので、位置変更は再描画コストゼロ)
2. canvas 毎に、その canvas に属する sprite instance を CPU pushSprite で合成
3. フレームバッファ全体を PPA **SRM** で 3x スケール + 90度回転して DSI FB へ

つまり canvas バッファは毎フレーム読み直されており、「大きな canvas
の中のどこを見せるか」というソース側オフセットさえ指定できれば、
スクロールは PPA レジスタ設定のみで実現できる。これは PPA Blend の
`in_fg.block_offset_x/y` + `block_w/h` にそのまま対応する。

## 新コマンド: FMRB_LINK_GFX_SET_CANVAS_VIEWPORT (0x57)

```
payload (packed): { u16 canvas_id, u16 src_x, u16 src_y, u16 view_w, u16 view_h }
```

- canvas に「表示するソース矩形」を設定する。合成時のフットプリントは
  (push_x, push_y) 位置に view_w x view_h。
- **canvas はトーラス (リングバッファ) として扱う**: ソース矩形が canvas
  端をまたぐ場合は折り返す (src_x/src_y は canvas サイズで mod)。
  これにより「ビューポートよりわずかに大きいだけの canvas」で任意サイズの
  ワールドをスクロールできる。アプリはカメラ移動でトーラスの見えない側に
  新しく見えるタイルを随時描き足す (TileRing 参照)。
- `view_w == 0` で viewport 解除 (従来どおり全面合成)。既定値。
  view_w/view_h は canvas サイズへ clamp。
- **Retro (S3/WROVER) 側は未実装**。共有ヘッダ
  (components/fmrb_common/include/fmrb_link_protocol.h) に opcode と
  struct を追加しただけで、アプリが P4 分岐
  (`FmrbConst::CHIP_MODEL == "ESP32-P4"`) の中でのみ送信するため
  GA ファームの更新は不要。

### 設計判断: 全マップ事前描画ではなくリングバッファ

初版は「マップ全体 (1024x1024, RGB565 2MB) を起動時に一括描画」だったが、
マップサイズにメモリと起動時間が線形で伸びるためスケールしない。
リングバッファ + 随時描き足し (古典的ハードウェアスクロールの定石) に
変更した。RPGデモでは 12x12 タイル (192x192px, **72KB**) で足り、
起動時の一括描画 (約4096コマンド/1秒) も不要になった。

### 配線 (新コマンド追加の定石)

1. fmrb_link_protocol.h: opcode enum + packed struct
2. components/fmrb_msg/fmrb_gfx_msg.h: GFX_CMD_* + union メンバ
3. components/fmrb_gfx/fmrb_gfx.c/.h: fmrb_gfx_set_canvas_viewport()
   (async 経路 = host_task バッチキュー。描画/present との順序保証のため)
4. main/kernel/host/host_task.c gfx_cmd_to_batch_entry(): 変換 case
5. main/drivers/display_p4/display_p4_task.cpp: ハンドラ + render_frame
6. Ruby: lib/add/picoruby-fmrb-app/ports/esp32/gfx.c `_set_canvas_viewport`
   + mrblib/fmrb-gfx.rb `set_viewport` / `clear_viewport`

## アプリの追加 canvas API

アプリはこれまでメイン canvas 1枚のみだった (デスクトップだけ bg canvas)。
マップを保持する大きな canvas を作るため、以下を追加:

- `FmrbApp#create_canvas_gfx(width:, height:, z_offset: 1, transparent: false,
  transparent_color: 0)` → 追加 canvas を作成し、それに紐付いた FmrbGfx を返す
- 実体は app.c の `_create_canvas` / `_delete_canvas` バインディング

### ライフサイクル (設計判断)

追加 canvas は **app context の extra_canvas_ids[2] に登録**する。
カーネルの suspend / resume / kill / クラッシュ後クリーンアップは
「ctx に登録された canvas」だけを面倒みる構造のため、ここに載せる
ことでリーク (canvas pool は全システムで 8 枚) を構造的に防ぐ:

- kill/crash 時の C-level クリーンアップ → DELETE_CANVAS
- suspend → SET_CANVAS_VISIBLE 0 / resume → 1

### z-order の制限 (ドキュメント化された制限)

追加 canvas は生成時に `メイン canvas の z + z_offset` を貰うが、
ウィンドウマネージャの z 再割当 (フォーカス変更等) には追随しない。
フルスクリーンアプリでは他アプリが suspend = 非表示なので実害なし。
**ウィンドウモードのアプリで追加 canvas を使うことは想定外**
(フォーカス変更で他ウィンドウとの上下関係が壊れ得る)。

## 表示位置指定つき present

追加 canvas の画面配置は `FmrbGfx#present(x, y)` (引数は省略可能) で行う。
省略時は従来どおりウィンドウ位置 (ctx->window_pos) を使うため、
既存アプリは無変更で動く。

## render_frame 側の実装ポイント

- blend 処理は `blend_canvas_block()` (display_p4_task.cpp) に共通化。
  通常 canvas は 1 ブロック、viewport 付き canvas はトーラス折り返しで
  **最大4ブロック** に分割して PPA Blend する
  (x折り返し x y折り返し。折り返さない通常スクロールは1ブロック)
- in_fg の pic_w/pic_h は canvas 全体サイズ、block_offset がソース起点
- **msync 最適化**: canvas の C2M flush は blend block の行レンジのみ
  (キャッシュライン境界へ丸め)。リングバッファ運用なら canvas 自体が
  小さいので実質フル flush でも軽い
- **sprite clip**: sprite 合成はフレームバッファ境界 clip しかないため、
  viewport 付き canvas の sprite はフットプリント矩形に setClipRect して
  合成する (viewport の外へのはみ出し防止)。sprite 座標は viewport 相対
  (トーラスの折り返しとは無関係で、そのまま成立する)
- **fallback**: PPA Blend がブロックを拒否した場合に備え、
  CPU 行コピー (不透明 memcpy) の代替パスを用意。発動時はログで判別可能

## RPGデモでの使い方 (利用例)

```ruby
if FmrbConst::CHIP_MODEL == "ESP32-P4"
  buf_tiles = VIEWPORT_W / TILE + 1   # 176px viewport -> 12x12 tiles (192x192)
  @map_gfx = create_canvas_gfx(width: buf_tiles * TILE, height: buf_tiles * TILE)
  @sheet   = TileSheet.new(@map_gfx, sheet_path, cols: ..., tile_size: 16)
  @ring    = TileRing.new(@map, @sheet, tiles_w: buf_tiles, tiles_h: buf_tiles)
  # プレイヤー sprite はマップ canvas に紐付け、座標は viewport 相対
  @map_gfx.present(16, 16)   # 位置決め + 表示 (一度だけ)
end

# 毎フレーム (スクロール時):
@ring.ensure_view(@view_x, @view_y, 176, 176)   # 新しく見えるタイルだけ描き足し
@map_gfx.set_viewport(@view_x % @ring.buf_w, @view_y % @ring.buf_h, 176, 176)
@player.move(@player_px - @view_x, @player_py - @view_y)
@gfx.present
```

- `TileRing` (lib/add/picoruby-fmrb-app/mrblib/fmrb-tilemap.rb) が
  「world タイル -> トーラススロット (tile % buf_tiles)」の対応と
  スタンプ済み管理を行う。タイル境界を跨いだフレームだけ約12タイルを
  draw_tile し、それ以外のフレームは描き足しゼロ
- 毎フレームのコマンド数: 約150 → 通常3 (タイル境界跨ぎ時 約15)
- 注意: リングは最下層レイヤが全セル埋まっている前提
  (空セルはスロットをクリアしないため)

## PPA 制約の実測メモ (実装後に追記)

- (未実測) トーラス分割 blend (最大4ブロック/フレーム/canvas) の動作
- (未実測) block_offset が奇数ピクセルの場合の挙動
  (デモは 4px ステップなので通常発生しない)

## 検証手順

1. 起動ログ: `Canvas alloc: id=N 192x192`、`PPA Blend failed` なし
2. GFX STATS: スクロール中の cmds/s が激減していること
3. スライドが滑らか (トーラス折り返し部にズレ/チラつきがないか特に確認)、
   プレイヤーがパネルへはみ出さない、マップ端 clamp、
   イベント発火、ステータスパネル正常
4. デモ終了で canvas free ログ、8回連続起動で pool 枯渇しない
5. 回帰: デスクトップの合成/ドラッグ/フォーカス、present 無引数アプリ
