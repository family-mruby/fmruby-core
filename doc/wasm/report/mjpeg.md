# wasm で MJPEG 再生と JPEG 画像を動かす

> 状態: 実装済み・**未ビルド未検証** | 更新: 2026-09-03

## やったこと

`wasm/backend/display_video_stub.c` (全部 `NOT_SUPPORTED` を返す拒否スタブ) を
`display_video_wasm.c` (実装) に置き換えた。P4 の
`main/drivers/display_p4/display_p4_video.cpp` と**同じ契約**の、software 版。

## なぜ安く済んだか

調べて分かった 3 点。**新しい依存は 1 つも要らなかった。**

1. **復号器が既に wasm のバイナリに入っている。** LovyanGFX が TJpgDec を
   連れてきており (`lgfx/utility/lgfx_tjpgd.c`)、`wasm/CMakeLists.txt` の
   `lgfx_wasm` で既にコンパイルされている。
2. **消費側は機種非依存で、既に wasm でも動いている。**
   `display_p4_task.cpp` は wasm の `CORE_SRCS` に入っており (287 行目)、
   `video_service()` が `take_frame` -> canvas へ memcpy -> commit まで
   実装済み。**触っていない。**
3. **`xTaskCreatePinnedToCore` が wasm でも使える** (vendored freertos_idf)。
   優先度もスタックも P4 と同じ定数 (`FMRB_VIDEO_P4_TASK_*`) が使える。

## P4 版との違い

| | P4 | wasm |
|---|---|---|
| 復号 | ハードウェア JPEG エンジン | TJpgDec (software) |
| 画素 | エンジンが RGB565 を吐く | RGB888 -> RGB565 を出力コールバックで詰める (lgfx は `JD_FORMAT 0`) |
| stride | 16 画素グリッドに切り上げ | **切り上げ無し** (ハードの都合が無い)。`stride_px` = 幅 |
| 確保 | `jpeg_alloc_decoder_mem` (DMA) | 素の `malloc` |
| 最大寸法 | 448x256 | 640x480 |

## 他のタスクを邪魔しないための作り

ご要望の「再生タスクが他に悪影響なければよい」に対して:

- **優先度は `FMRB_VIDEO_P4_TASK_PRIORITY`** = P4 と同じで、**表示タスクより
  下**。合成とその上は取り分を失わない。
- **20ms 刻みで眠る** (`VIDEO_WAIT_SLICE_MS`)。1 フレーム分まとめて眠らない
  ので譲る頻度が高く、stop/pause の反応も速い。
- **間に合わないときは復号せず読み飛ばす。** software 復号は P4 より桁違いに
  遅いので、ここが効く。4 間隔ぶん遅れたら債務を捨てて現在時刻から再開する。
  **画は落ちるが、間に合わない予定を守るために CPU を占有しない。**
- **動画を開いていないときはタスクが存在しない。** `video_open` で作り、
  停止で自分を消す。

## おまけ: 静止画の JPEG も直った

`display_p4_jpeg_decode` は `display_p4_task.cpp:1984` で **`.jpg` 画像の
読み込みにも使われている**。拒否スタブは `NULL` を返していたので wasm では
JPEG 画像が出なかった。同じ復号器で実装したので、こちらも出るようになる
(はず — 未検証)。

## 未検証・確かめること

**ビルドしていない。** 以下は全部これから。

1. **ビルドが通ること。** ヘッダの解決性だけは確認した
   (`fmrb_hal_time.h` / `fmrb_link_protocol.h` / `fmrb_log.h` /
   `display_p4_video.h` / `lgfx/utility/lgfx_tjpgd.h` はいずれも
   `core_objs` の include path 上)。
2. **TJpgDec の作業プールが足りること。** `JPEG_WORK_BYTES` は 8192 に
   丸めてある (3 成分の古典的な目安は 3100)。`jd_prepare` が `JDR_MEM1` を
   返すようなら増やす。
3. **`.mjpg` をどこから読むか。** ブラウザでは `/mnt/sd` が無い。
   `web_fs` で送るか bundle に入れるかを決める必要がある。
   アプリの既定は `/sd/movie/demo.mjpg`。
4. **実際の速度。** software 復号が 320x176 で何 ms かかるか。15fps に
   届かなければ落として再生する作りだが、**どのくらい落ちるかは測らないと
   分からない**。
5. **静止画 JPEG** が実際に描けること。
