# ESP32-P4 PPA と LovyanGFX の RGB565 描画パイプライン知見

2026-07-03 に M5Stack Tab5 (ESP32-P4) 実機で行った PPA 検証テストと、
それによって解決した「PPA Blend で合成した canvas が真っ黒になる」問題の記録。

検証テストコードは `main/drivers/display_p4/display_p4_task.cpp` の
`PPA_VERIFICATION_TEST` マクロで有効化できる（通常は無効。有効にすると起動を
約30秒ブロックするため、カーネルの host wait がタイムアウトし OS は起動しない）。

---

## 1. RGB565 のバイトオーダー整理

LovyanGFX には 16bit RGB565 の色深度が2種類ある。

| depth | uint16 値（赤の例） | メモリバイト列 | 用途 |
|---|---|---|---|
| `rgb565_2Byte` (=16, swap565) | 0x00F8 | `F8 00`（big-endian） | LCD バス転送順そのまま |
| `rgb565_nonswapped` (=272) | 0xF800 | `00 F8`（little-endian） | CPU / PPA ネイティブ |

- LCD（SPI/DSI）はバイト列 `F8 00` の順で受け取るため、LovyanGFX のスプライトは
  デフォルトで swap565。
- PPA は RGB565 を **little-endian の uint16** として読み書きする（実測、下記）。

## 2. PPA の実測仕様（実機で確定）

### SRM (Scale-Rotate-Mirror)

- `byte_swap=false`: 完全パススルー。入力 `0xF800` → 出力 `0xF800`
- `byte_swap=true`: 出力 = bswap(入力)。`0xF800` → `0x00F8`
- `rgb_swap=true`: R/B チャネル交換。`0xF800`（赤）→ `0x001F`（青）

### Blend

- `fg_byte_swap` / `bg_byte_swap` は **入力の読み取りにのみ作用**する
- **出力は常に nonswapped（little-endian）で書き込まれる。swap565 で出力する手段は無い**
- `fg_alpha_update_mode=PPA_ALPHA_FIX_VALUE, fg_alpha_fix_val=255` で FG 不透明合成
- color-key (`fg_ck_en` + low/high threshold) は RGB888 range で指定
  （PPA は RGB565 を内部で RGB888 に拡張して比較するため、量子化分の幅を持たせる）

### 帰結: パイプラインは nonswapped に統一するしかない

swap565 スプライト + `byte_swap=true` 方式は、Blend の出力領域だけが
nonswapped 化され、未処理領域（swap565 のまま）と混在したバッファになるため
必ず色崩壊する。全バッファを `rgb565_nonswapped` に統一し、PPA は
`byte_swap=false` で使う。LCD への最終出力は `pushImage(..., (lgfx::rgb565_t*)buf)`
とすれば LovyanGFX が転送時にスワップしてくれる（実機で色を目視確認済み）。

## 3. LGFX_Sprite の重大な落とし穴: setColorDepth はバッファを作り直す

`LGFX_Sprite::setColorDepth()`（M5GFX `LGFX_Sprite.hpp`）は、バッファ装着済みの
スプライトに対して呼ぶと `deleteSprite()` + `createSprite()` で
**バッファを内部確保し直す**。

```cpp
// NG: 整列済み PSRAM バッファがリークし、内部 RAM の非整列バッファに差し替わる
sprite->setBuffer(buf, w, h, 16);
sprite->setColorDepth(lgfx::rgb565_nonswapped);  // ここで buf が捨てられる

// OK: 深度を先に設定してからバッファを装着する
sprite->setColorDepth(lgfx::rgb565_nonswapped);
sprite->setBuffer(buf, w, h);  // bpp=0 なら設定済みの深度が維持される
```

NG パターンで実際に起きていたこと:

1. `ppa_alloc_buffer()` で確保した整列済み PSRAM バッファがリーク
2. 代替バッファは `heap_alloc_dma`（`_psram=false` の場合、内部 RAM・非整列）に確保
   される。起動ログの `IRAM free` が約 205KB（=426x240x2）減っていたのが証拠
3. `esp_cache_msync` が非整列アドレスで失敗（戻り値未チェックだと無音）し、
   PPA が CPU の描画内容を読めず canvas が真っ黒になる
4. 外部バッファ（`Preallocated`）は `deleteSprite()` で解放されないが、
   内部確保バッファは解放されるため、`getBuffer()` を `heap_caps_free()` する
   解放コードが**二重解放**になる

なお `SpriteBuffer` の所有権: `setBuffer()` 経由は `AllocationSource::Preallocated`
となり `release()` で解放されない（呼び出し側が解放する）。`createSprite()` 経由は
スプライトが所有し解放する。

## 4. esp_cache_msync の注意点

- アドレスとサイズの両方が**キャッシュラインに整列**していないと失敗する
  （Tab5 の PSRAM は L2 キャッシュで 128B。`esp_cache_get_alignment()` で取得する）
- サイズは `幅x高さx2` ではなく、**整列済み確保サイズ**を使う
  （`display_p4_task.cpp` では `p4_canvas_t.buf_aligned_size` / `g_fb_aligned_size` に保持）
- **戻り値を必ずチェックしてログを出す**。無音で失敗すると「PPA が古いデータを読む」
  という追いにくい症状になる
- 方向: CPU 書き込み後 PPA が読む前に `C2M`、PPA(DMA) 書き込み後 CPU が読む前に
  `M2C | INVALIDATE`

## 5. 正しいパイプライン構成（display_p4 実装）

```
canvas sprite (rgb565_nonswapped, ppa_alloc_buffer で整列 PSRAM)
  |  PPA Blend (byte_swap=false, FG alpha 固定 255, color-key で透過)
  v
framebuffer (rgb565_nonswapped, 整列 PSRAM)
  |  PPA SRM (byte_swap=false, 3x 拡大)
  v
SRM 出力バッファ (整列 PSRAM)
  |  g_lcd.pushImage(..., (lgfx::rgb565_t*)buf)  … LovyanGFX が転送時にスワップ
  v
ILI9881C (MIPI-DSI)
```

修正コミット: `9ba26e5` fix(display_p4): set sprite color depth before attaching PPA buffer
