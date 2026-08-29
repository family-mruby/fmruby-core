# P5 Native API Follow-ups: get_pixel / image_masked

> 状態: ほぼ消化済み | 更新: 2026-08-29 | get_pixel と image_masked は実装済み。残件は bulk get_pixels のみ (本文の該当節だけが生きている)

`P5#get_pixel` は実装済み。`P5#image_masked` は引き続き
`NotImplementedError`。本メモは LovyanGFX 側の機能を踏まえて、未対応の
`image_masked` と、`get_pixel` の bulk 版 (`get_pixels`) のネイティブ実装案
を整理する。実装はスコープ別 (core 側 / graphics-audio 側 / protocol) に
分かれる。

参照済みコード:
- 描画コマンド型: [components/fmrb_msg/fmrb_gfx_msg.h](../components/fmrb_msg/fmrb_gfx_msg.h)
- 同期コンテキスト: 同上 `sync_ctx` (`done` semaphore + `response_buf`/`response_len`)
- 既存の sync コマンド例: `GFX_CMD_CREATE_CANVAS`, `GFX_CMD_CREATE_SPRITE_IMAGE`,
  `GFX_CMD_LOAD_SPRITE_IMAGE_BMP`, `GFX_CMD_DEFINE_PROG`, `GFX_CMD_GET_PIXEL`
- WROVER 側の readPixel 実績:
  [fmruby-graphics-audio/main/graphics/graphics_handler.cpp](../../fmruby-graphics-audio/main/graphics/graphics_handler.cpp)
  にて `screen_buffer->readPixel(sx, sy)` 等を使用 (カーソル退避 / GET_PIXEL)
- LovyanGFX readback API: `readPixelValue(x, y)` (RGB332 そのまま) /
  `readRect(x, y, w, h, dst)` (bulk)

---

## 1. `get_pixel(x, y)` — 実装済み

実装は host_task の sync コマンドパスに乗せている (`GFX_CMD_GET_PIXEL`)。
直前までキューされていた描画コマンドが flush されてから読み取りが行われる
ため、`draw → get_pixel` の順序保証が自動で成立する。

実装ファイル:
- Protocol enum: [components/fmrb_common/include/fmrb_link_protocol.h](../components/fmrb_common/include/fmrb_link_protocol.h)
  に `FMRB_LINK_GFX_GET_PIXEL = 0x55`、`fmrb_link_graphics_get_pixel_t` /
  `fmrb_link_graphics_pixel_value_t` を追加
- 内部 enum: [components/fmrb_msg/fmrb_gfx_msg.h](../components/fmrb_msg/fmrb_gfx_msg.h)
  に `GFX_CMD_GET_PIXEL` と `params.get_pixel` を追加
- host_task: [main/kernel/host/host_task.c](../main/kernel/host/host_task.c)
  の `gfx_cmd_to_batch_entry()` に変換ケースを追加
- S3 binding: [lib/add/picoruby-fmrb-app/ports/esp32/gfx.c](../lib/add/picoruby-fmrb-app/ports/esp32/gfx.c)
  に `mrb_gfx_get_pixel` (sync_ctx で host_task 経由)、`_get_pixel` として
  Ruby に公開
- Ruby ラッパ: [lib/add/picoruby-fmrb-app/mrblib/fmrb-gfx.rb](../lib/add/picoruby-fmrb-app/mrblib/fmrb-gfx.rb)
  に `def get_pixel(x, y); _get_pixel(x, y); end`
- WROVER ハンドラ:
  [fmruby-graphics-audio/main/graphics/graphics_handler.cpp](../../fmruby-graphics-audio/main/graphics/graphics_handler.cpp)
  に `case FMRB_LINK_GFX_GET_PIXEL` を追加 (`target->readPixel(x, y)` で
  RGB332 値を読み出し、`send_ack` で 2 バイト応答)
- P5 公開: [flash/lib/p5.rb](../flash/lib/p5.rb)
  の `P5#get_pixel` を `@gfx.get_pixel(x, y)` 委譲に変更

### 性能注意点
- 1 ピクセル 1 ラウンドトリップ。連続呼び出しは遅い (SPI + semaphore 待ち)
- スキャン用途では bulk 版が必要 → 後述の `get_pixels` 提案

### 拡張案: `get_pixels(x, y, w, h)` (任意・後追い)
- `readRect(x, y, w, h, buf)` を使う bulk readback
- 応答ペイロードが大きくなるので transport fragmentation 機構の流用が必要
- 応答長 = `w * h` バイト (RGB332)。VRAM 全画面 (320x240) = 76.8KB は不可。
  小さい矩形 (例: 64x64 = 4KB) に制限するか、行単位の readback を反復する設計が現実的
- Ruby API: `FmrbGfx#get_pixels(x, y, w, h) -> String` (RGB332 packed)
- P5 側に追加メソッドとして公開する場合は `pixels(x, y, w, h) -> String`

---

## 2. `image_masked(...)` — 実装済み

実装は推奨パス B (マスク事前登録) を採用。プロトコルは 3 コマンド:
`FMRB_LINK_GFX_CREATE_MASK` (sync, fragment 対応) /
`FMRB_LINK_GFX_DELETE_MASK` (async) /
`FMRB_LINK_GFX_DRAW_IMAGE_MASKED` (async)。

画像ソースは PNG ベース `image_id` ではなく **SpriteImage** (LGFX_Sprite を
保持しているため `readPixelValue` でピクセル直サンプル可)。WROVER 側で
事前登録 PNG をピクセルアクセス可能な形に変換するのはコスト高なため除外。

LovyanGFX に組み込み `pushImageWithMask` 相当はなく、行単位の
`drawPixel` ループ実装 (大型マスクではやや重め)。

### 実装ファイル
- Protocol:
  [components/fmrb_common/include/fmrb_link_protocol.h](../components/fmrb_common/include/fmrb_link_protocol.h)
  に enum (`0x84/0x85/0x86`) と `create_mask_t` / `mask_created_t` /
  `delete_mask_t` / `draw_image_masked_t` を追加。graphics-audio 側
  [main/common/fmrb_link_protocol.h](../../fmruby-graphics-audio/main/common/fmrb_link_protocol.h)
  にも同じ定義を追加。
- 内部 enum: [components/fmrb_msg/fmrb_gfx_msg.h](../components/fmrb_msg/fmrb_gfx_msg.h)
  に `GFX_CMD_DELETE_MASK` / `GFX_CMD_DRAW_IMAGE_MASKED` (CREATE_MASK は
  ペイロードが大きく可変長なので host_task を経由しない直接 transport_send_sync)。
- host_task: [main/kernel/host/host_task.c](../main/kernel/host/host_task.c)
  の `gfx_cmd_to_batch_entry()` に 2 ケース追加。
- S3 binding: [lib/add/picoruby-fmrb-app/ports/esp32/gfx.c](../lib/add/picoruby-fmrb-app/ports/esp32/gfx.c)
  に `mrb_gfx_create_mask` (transport_send_sync 直送 + 自動 fragmentation)、
  `mrb_gfx_delete_mask`、`mrb_gfx_draw_image_masked` を追加。
- Ruby ラッパ: [lib/add/picoruby-fmrb-app/mrblib/fmrb-gfx.rb](../lib/add/picoruby-fmrb-app/mrblib/fmrb-gfx.rb)
  に `create_mask` / `delete_mask` / `draw_image_masked` を追加。
- WROVER ハンドラ:
  [fmruby-graphics-audio/main/graphics/graphics_handler.cpp](../../fmruby-graphics-audio/main/graphics/graphics_handler.cpp)
  にマスクストア (16 スロット、PSRAM 確保) と 3 ケース追加。描画は
  `sprite->readPixelValue` + `canvas->draw_buffer->drawPixel` の 2 重ループ。
- P5 公開: [flash/lib/p5.rb](../flash/lib/p5.rb)
  の `P5#image_masked(image_id, mask_data, x, y, w, h)` を create→draw→delete
  ラップに変更。長寿命マスクは `@gfx.create_mask` を直接使う。

### 既存スコープ外 (検討メモ)
- PNG image_id をマスク描画ソースにする (RGB332 ピクセルバッファのキャッシュ
  追加) は未実装。SpriteImage で代替可能なので優先度低。
- 大きなマスクの drawPixel ループ最適化 (行単位 pushImage + マスク前計算など)
  は未実装。性能ボトルネックになるなら検討。

### 全体像 (旧設計メモ)
`pushImage` 系 LovyanGFX API に 1bpp マスクを組み合わせて描画する。
画像本体は既存の `create_image` (ファイルベース) で WROVER に登録済みの
`image_id` を再利用し、マスクの渡し方だけ新規設計する。

### マスクデータの形式
- 1bpp packed, MSB-first per byte (Adafruit GFX / LovyanGFX 慣習に揃える)
- size = `ceil(w / 8) * h` bytes
- 1 ビット = "描画する"、0 ビット = "透過" (P5/Processing 慣習に合わせる)

### マスク転送パスの選択肢

| 方式 | 利点 | 欠点 |
|---|---|---|
| **A. インライン送信** (描画コマンドに mask payload を同梱) | API 1 個で済む / 状態を持たない | SPI 1 メッセージのサイズ上限 (transport の MTU) を超えるとフラグメント必須。連続呼び出しで毎回マスク再送 |
| **B. マスク事前登録** (`upload_mask` 系コマンドで mask_id を取得し、描画時は id 参照) | 再描画ごとに mask を再送せず高速 / 大きなマスクも分割アップロード可 | マスクの life cycle 管理 (delete_mask) が必要 / API 2 個 |
| **C. 既存 image_id で代用** (`create_image` でマスク BMP も上げる) | 追加コマンド最小 | マスクが「画像」として扱われるので扱いが直観的でない / 1bpp 専用最適化が効きにくい |

**推奨: B (マスク事前登録)**。
理由:
- ゲーム用途 (繰り返し描画) で毎フレーム mask を SPI に流すのは帯域がもったいない
- LovyanGFX に `pushImageWithMask` 風のメソッドがあり、`uint8_t mask[]` を
  保持しておけば 1 行で描画できる
- インライン版 (A) はワンショット用途として補助的に追加してもよい

### Protocol (推奨パス B)
- 新 sync コマンド: `GFX_CMD_CREATE_MASK_FROM_DATA`
  - 入力: `{ width: u16, height: u16, data: u8[ceil(w/8)*h] }`
    - データが MTU を超える場合は既存の fragment transport (transfer_file 同様の
      分割アップロード) を流用
  - 応答: `{ mask_id: u16 }`
- 新 async コマンド: `GFX_CMD_DRAW_IMAGE_MASKED`
  - 入力: `{ canvas_id, image_id: u16, mask_id: u16, x: i16, y: i16,
    scale_x: i32_fp, scale_y: i32_fp }`
  - 応答なし
- 新 async コマンド: `GFX_CMD_DELETE_MASK`
  - 入力: `{ mask_id: u16 }`

### core 側 (`gfx.c` + `fmrb-gfx.rb`)
- 新 C メソッド:
  - `FmrbGfx#_create_mask_from_data(width, height, data_str)` → mask_id
  - `FmrbGfx#_draw_image_masked(image_id, mask_id, x, y, scale_x, scale_y)`
  - `FmrbGfx#_delete_mask(mask_id)`
- Ruby ラッパ (`fmrb-gfx.rb`):
  ```ruby
  def create_mask(width, height, data); _create_mask_from_data(width, height, data); end
  def draw_image_masked(image_id, mask_id, x:, y:, scale_x: 1.0, scale_y: 0.0)
    _draw_image_masked(image_id, mask_id, x, y, scale_x, scale_y)
  end
  def delete_mask(mask_id); _delete_mask(mask_id); end
  ```

### graphics-audio 側
- マスクテーブルを新設 (固定上限・例 16 個)。エントリ = `{w, h, uint8_t* data}`
  (PSRAM 確保 → 既存の sprite image 用 allocator を再利用)
- 描画は `image_pool[image_id]` と `mask_pool[mask_id]` を組み合わせ、行単位ループで
  - `dst = sprite/screen->getBuffer()`
  - mask の該当ビットが 1 のときだけ `image.data[i]` を `dst[i]` に書く
  - LovyanGFX に `pushImageWithMask` 相当があるバージョンならそちらを優先
- canvas / sprite 両方のターゲットに描画できるよう、現状 `set_sprite_image_target`
  で切り替え中のターゲットを尊重する

### P5 側 (`flash/lib/p5.rb`)
インライン版 (旧 harucom 互換) は段階的に提供する:

```ruby
# harucom 互換シグネチャ: 毎回マスクを送る (最も単純な書き味)
def image_masked(image_id, mask_data, mask_width, mask_height, x, y)
  mid = @gfx.create_mask(mask_width, mask_height, mask_data)
  @gfx.draw_image_masked(image_id, mid, x: x, y: y)
  @gfx.delete_mask(mid)
end
```

長寿命マスクが必要なユースケース向けに、`P5#prepare_mask(...)` /
`P5#image_with_mask(image_id, mask_id, x, y)` の二段 API も同時に提供すると
ゲーム用途で実用的になる (毎フレームの SPI 帯域を節約)。

---

## 3. 残課題と将来の拡張

1. **`get_pixels` (bulk readback)** — 中規模対応 (中リスク)
   - `readRect` 経由で矩形領域の RGB332 をまとめて返す
   - 応答が transport の MTU を超える場合があり、`fmrb_transport_send_sync` の
     既存 fragment 経路に乗せれば対応可能。応答長制限と PSRAM 上限の検討必要
2. **PNG image_id を image_masked のソースに対応** — オプション
   - 現在は SpriteImage のみがソース。PNG image_id を使う場合は、復号後の
     RGB332 ピクセルバッファをキャッシュする実装が必要 (PSRAM 配分が増える)
3. **マスク描画の性能改善** — オプション
   - 行単位 `pushImage` + 行ごとのマスク bit パターンで `drawPixel` ループを
     置換できれば数倍高速化可能。複雑度と引き換え
