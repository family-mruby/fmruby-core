# wasm で MJPEG 再生と JPEG 画像を動かす

> 状態: 動作確認済み | 更新: 2026-09-03 | node とブラウザの両方で再生。
> 320x176 15fps を **drop 0** で回している。

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

## 動いた (2026-09-03)

`tools/gen_test_mjpg.py` で作った 320x176 / 60 フレームの `.mjpg` を、
`web_fs put` で `/flash/home/` に送って再生した。

| | |
|---|---|
| node (`node wasm/build/core.js`) | `opened ... 320x176 @15fps`、毎秒ちょうど 15 フレーム、**drop 0** |
| ブラウザ | 絵・色・四隅の目印・フレーム番号すべて正しい。`shown:234 drop:0` |

**15fps は software 復号で余裕をもって出る。** 落ちる心配をしていたが、
この寸法では杞憂だった。

### 詰まった 2 か所 (どちらもスタブの外)

実装そのものより、**呼ばれるまでの門**で 2 回止まった。

1. **`gfx.c` の `_video_open` が `#ifdef FMRB_HW_MODERN` で囲われていた。**
   wasm は `FMRB_HW_FAMILY_MODERN` しか定義しないので、コマンドを送る前に
   nil を返していた (ログすら出ない)。3 つの video 関数を
   `!defined(FMRB_HW_MODERN) && !defined(FMRB_PLATFORM_WASM)` に変更。
   **`FMRB_HW_FAMILY_MODERN` に緩めてはいけない** — Linux sim も通ってしまい、
   あちらの表示は別プロセスでこのコマンドを知らないので、毎回 10 秒の同期
   待ちになる。
2. **その `FMRB_PLATFORM_WASM` が mruby 側のビルドで定義されていなかった。**
   gem は `rake wasm:mruby` の別ビルドで、`core_objs` の
   `target_compile_definitions` は届かない。`lib/add/family_mruby_wasm.rb`
   に足した。**lib/ を触ったので `rake wasm:mruby` からやり直しが要る。**

### 切り分けに使った手

ブラウザのコンソールは取れないので、**`video_open` を呼ぶだけの probe アプリを
`startup_app` で node 実行**して firmware のログを読んだ。node では
`--fmrb-conf=` が効かない (NODERAWFS では `/flash/...` がホストの絶対パス扱いで
`page_settings` がファイルを読めない) ので、`flash/etc/system_conf.toml` を
直接書き換えて起動した。

復号器そのものは、`lgfx_tjpgd.c` をホストの gcc で単体リンクして先に潰した
(`jd_prepare` -> 320x176/3成分、`jd_decomp` OK、8KB プールで足りる)。

## まだ確かめていないこと

**ビルドしていない。** 以下は全部これから。

1. **静止画 JPEG** (`display_p4_jpeg_decode`)。同じ復号器なので通るはずだが、
   `.jpg` を実際に描かせていない。
2. **もっと大きい絵での速度。** 320x176 は余裕だったが、上限に置いた
   640x480 は 6 倍の画素で、そこで何 fps 出るかは測っていない。
3. **アプリの既定パス。** `/sd/movie/demo.mjpg` はブラウザに無いので、
   毎回 Open で選ぶことになる。bundle に見本を入れるかは別途。
4. **P4 実機の回帰。** `gfx.c` の門を触ったので、Modern 実機で従来どおり
   再生できることの確認がまだ (条件は `FMRB_HW_MODERN` を残したままなので
   壊れていないはずだが、見ていない)。
