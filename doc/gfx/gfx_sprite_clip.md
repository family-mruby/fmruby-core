# スプライトのクリップ矩形 (SET_SPRITE_CLIP)

2026-08-15 設計・実装。ウィンドウアプリのスプライトがウィンドウ枠の上に
描かれてしまう問題 (flappy デモ) を動機とした、両バックエンド共通の機能。

## 背景 / 問題

スプライトは **canvas に描かれた絵の上に、後から重ねて合成される**。

- Retro / Linux sim: `present` (PUSH_CANVAS) が `draw_buffer` を
  `render_buffer` にコピーした直後に `sprite_manager_composite()`
  (fmruby-graphics-audio `main/graphics/graphics_handler.cpp`)
- Modern / P4: canvas を PPA Blend でフレームバッファに合成した直後に
  `display_p4_sprite_composite()` (`main/drivers/display_p4/display_p4_task.cpp`
  `render_frame()`)

ウィンドウ枠とタイトルバーは**アプリ自身が同じ canvas に描く**
(`FmrbApp#draw_window_frame`) ため、スプライトが枠より上に来るのは構造上
必然だった。クリップの粒度は canvas しかなく、user area ではない。

flappy デモではこれが次の形で出ていた:

- 柱スプライトが左右のボーダー (1px) と下辺の上を通過する
- 柱の最下段タイルが user area をはみ出し、canvas 端で切れて見える
- スコア表示 (canvas 描画) の上を柱が通る

## 新コマンド: FMRB_LINK_GFX_SET_SPRITE_CLIP (0x58)

```
payload (packed): { u16 canvas_id, u16 x, u16 y, u16 w, u16 h }
```

- canvas の**スプライト合成だけ**を (x, y, w, h) の矩形に閉じ込める。
  canvas 本体の合成 (draw_buffer の内容) には影響しない。
- 座標系は**スプライト座標と同じ** (canvas ローカル。viewport 使用時は
  viewport 相対)。`SpriteInstance#move` に渡す値がそのまま使える。
- `w == 0` または `h == 0` で解除 (canvas 全体。既定値)。矩形は canvas の
  アクティブ領域へ clamp する。
- **リサイズで解除される**。UPDATE_WINDOW は `setBuffer()` を呼ぶが、
  LovyanGFX の `setBuffer()` は `_clip_*` をリセットするため、
  クリップを sprite に張りっぱなしにはできない。真実は canvas 状態
  (`canvas_state_t` / `p4_canvas_t`) 側に持ち、合成のたびに張り直す。
  古い寸法の矩形が残らないよう、リサイズ時に 0 クリアしてアプリに
  再送させる。

## 既定の挙動: FmrbApp が user area を入れる

`FmrbApp` はウィンドウモードなら起動時とリサイズ時に user area を
クリップとして送る (`_apply_user_area_sprite_clip`)。フルスクリーンでは
解除する。つまり**アプリは何もしなくてもスプライトが枠に被らない**。

**例外: bg_canvas を持つアプリ (= system_desktop) は除外する**。
デスクトップはウィンドウ枠を描かず、メニューバーの指示器やランチャーの
アイコンを canvas 全域に置くため、守るものが無いのに範囲だけ狭まる
(実際、初版は user area (1,11) 318x228 が入っていた)。
`_apply_rounded_corner_regions` が同じ理由で bg_canvas を除外しているのと
同じ扱い。

さらに狭めたいアプリは `FmrbGfx#set_sprite_clip(x, y, w, h)` を後から呼ぶ。
flappy はプレイフィールド (スコア帯より下、地面より上) を入れており、
スコアにも地面にも柱が掛からない。

## 配線 (新コマンド追加の定石どおり)

1. `components/fmrb_common/include/fmrb_link_protocol.h`: opcode + struct
   (**graphics-audio 側 `main/common/fmrb_link_protocol.h` は手動コピー**。
   同期スクリプトも CI チェックも無いので両方直す)
2. `components/fmrb_msg/fmrb_gfx_msg.h`: `GFX_CMD_SET_SPRITE_CLIP` + union
3. `components/fmrb_gfx/fmrb_gfx.c/.h`: `fmrb_gfx_set_sprite_clip()`
   (async 経路 = host_task バッチキュー。描画/present との順序保証のため)
4. `main/kernel/host/host_task.c` `gfx_cmd_to_batch_entry()`: 変換 case
5. バックエンド:
   - graphics-audio `graphics_handler.cpp`: `canvas_state_t` に clip を持ち、
     `sprite_manager_composite()` を `render_buffer->setClipRect()` /
     `clearClipRect()` で囲む
   - P4 `display_p4_task.cpp`: `p4_canvas_t` に clip を持ち、`render_frame()`
     の既存フットプリント clip (canvas / viewport) と**交差**を取る
6. Ruby:
   - mruby: `lib/add/picoruby-fmrb-app/ports/esp32/gfx.c` `_set_sprite_clip`
     + `mrblib/fmrb-gfx.rb` `set_sprite_clip` / `clear_sprite_clip`
   - Spinel: `main/app/fmrb_spx_gfx.c/.h` + `prebuild_scripts/spinel/
     fmrb_app_ffi.rb` + `fmrb_app_base_spinel.rb`
   - 既定値の適用: mruby は `mrblib/fmrb-app.rb` (生成時) と
     `ports/esp32/app.c` の resize 経路、Spinel は
     `fmrb_app_base_spinel.rb` の `initialize` と `_dispatch_control`

## 設計判断

### canvas ごと (per-canvas) にした理由

スプライトごとのクリップにすれば柔軟だが、必要なのは「この canvas の
スプライト層は user area の中だけ」という意味付けであり、instance 単位の
状態 (128 instance x 8 バイト) と protocol を増やす価値が無い。canvas は
最大 8 (GA) / 16 (P4) なので状態は数十バイトで済む。

### 却下した代替案

- **スプライトを draw_buffer に焼き込む**: `draw_buffer` / `render_buffer`
  の 2 枚構成は「アプリが再描画しなくてもスプライトだけ動かせる」ための
  ものなので、焼き込むとその性質が失われる (下の背景を毎フレーム塗り直す
  必要が出る)。1 回きりのスタンプが要るなら `FmrbGfx#draw_tile` が既にある。
- **プレイフィールド専用の子 canvas を作る**: クリップは効くが、
  `doc/gfx/gfx_canvas_viewport_scroll.md` のとおり**ウィンドウモードでの追加
  canvas は z 順がフォーカス変更に追随しない**。加えて canvas バッファは
  要求サイズではなく画面サイズで確保される (draw + render の 2 枚) ため、
  小さなプレイフィールドでも全画面ぶん x2 を消費する。
- **P4 のスプライト合成先を canvas に変える**: canvas バッファは PPA Blend
  の DMA 入力で、viewport (トーラススクロール) は canvas 内容が安定して
  いることを前提にしている。

### バージョンを上げた

プロトコルにコマンドが増えたので `FMRB_LINK_VERSION` を 4 -> 5、
GA ファームウェア版数を 2.0.0 -> 2.1.0 に上げた (core `fmrb.h` と GA
`main/include/fmrb_ga_version.h` の両方。**手動同期**)。

どちらの照合も起動時に厳密で、失敗すると fatal
(`fmrb_kernel.rb`、3 回リトライ後にエラー LED + raise) になる。つまり
**組み合わせが古いと起動しない**。これは意図した挙動で、据え置きにすると
古い GA ファームのまま「クリップだけ黙って効かない」状態が見分けにくい形で
残る (このコマンドは非同期 fire-and-forget なので、受信側は
`ESP_LOGE` + `ESP_LOGW` を出して捨てるだけ)。**Retro では core と
graphics-audio を必ず一緒に焼く**こと。

## 確認方法

Linux sim は **ターゲットが Retro でも Modern でも graphics-audio 経路**
(`display_p4_task.cpp` は Linux ビルドに含まれない) なので、GA 側の実装は
sim でそのまま確認できる。P4 側はハードウェア (Tab5/NARYAv4) が要る。
