# Family mruby カメラ対応 検討メモ

Tab5 (Modern) の前面カメラを使えるようにするための検討。実装計画ではなく、
どの道を通るか・何を先に潰すかを決めるための文書。

最終更新: 2026-08-09 (M5 公式実装の突き合わせにより方針を変更)

## 0. 目標 (ユーザ指定)

| 項目 | 内容 |
|---|---|
| 表示 | **窓の中に出れば十分**。全画面は要らない |
| 速度 | **かなり遅くてよい**。動画ではない |
| 用途 | **静止画カメラのアプリが Ruby で書ける**こと |
| 対象 | Tab5 (P4) のみ |

この絞り込みが効く。連続した映像を流し続ける必要が無いので、帯域の心配が
ほぼ消え、「押したときに 1 枚取る」形にできる。

## 1. 方針 (2026-08-09 変更)

**esp_video (V4L2 風の API) を使う。** 当初は IDF 素の
`esp_driver_cam` + `esp_driver_isp` を推していたが、
**M5 公式の Tab5 UserDemo が esp_video を使って動かしている**ことを確認した
ので、そちらに合わせる。同じ基板で動いている実物のほうが、こちらの推測より
強い根拠になる。

M5 の実装 (`platforms/tab5/main/hal/components/hal_camera.cpp`) の要点:

- `esp_video_init()` のあと `open()` / `ioctl()` / `mmap()` の V4L2 形式
- 形式は **RGB565**、解像度 **1280x720**、緩衝は 2 枚 (`V4L2_MEMORY_MMAP`)
- 取得は `VIDIOC_DQBUF`、返却は `VIDIOC_QBUF`
- **回転と反転は PPA の `ppa_do_scale_rotate_mirror()`**
- reset ピンと pwdn ピンは -1 (使わない)

最後の 2 点がこちらに都合が良い。PPA は表示側が拡大に使っている機構そのもの
で、既に扱いを知っている。

## 2. 部品と設定 (M5 の実績値)

必要な部品は 4 つ。`esp_video` / `esp_cam_sensor` / `esp_ipa` /
`esp_sccb_intf`。

M5 が実際に使っている設定 (`platforms/tab5/sdkconfig` より抜粋):

```
CONFIG_CAMERA_SC202CS=y
CONFIG_CAMERA_SC202CS_AUTO_DETECT=y
CONFIG_CAMERA_SC202CS_AUTO_DETECT_MIPI_INTERFACE_SENSOR=y
CONFIG_CAMERA_SC202CS_MIPI_RAW8_1280x720_30FPS=y

CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE=y
CONFIG_ESP_VIDEO_ENABLE_ISP=y
CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=y
CONFIG_ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE=y
CONFIG_ESP_VIDEO_ENABLE_HW_JPEG_VIDEO_DEVICE=y

CONFIG_ESP_IPA_AWB_ALGORITHM=y     # 白平衡
CONFIG_ESP_IPA_AGC_ALGORITHM=y     # 自動利得
CONFIG_ESP_IPA_DENOISING_ALGORITHM=y
CONFIG_ESP_IPA_SHARPEN_ALGORITHM=y
CONFIG_ESP_IPA_GAMMA_ALGORITHM=y
CONFIG_ESP_IPA_CC_ALGORITHM=y      # 色補正
```

分かること:

- **センサは 1280x720 の RAW8 で使う** (1600x1200 ではない)。1 枚 1.84MB の
  RGB565 が 2 枚で 3.7MB。PSRAM 32MB に対して十分収まる
- **絵の調整 (esp_ipa) が別部品として要る**。白平衡や自動利得を省くと、
  写るには写るが見られる絵にならない。ここを自前で調整する必要は無い
- **esp_video に「ハードウェア JPEG の video device」がある**。静止画の
  保存に直結する

sdkconfig は編集禁止なので、これらは**提案として出して裁可をもらう**。
生成し直すときは `rake clean_all`。

## 3. 表示経路との噛み合わせ

```
センサ (RAW8 1280x720) -> CSI -> ISP (RGB565) -> PPA で縮小/回転 -> キャンバス -> 画面
```

- ISP の出力 RGB565 はキャンバスと同じ形式。変換が要らない
- 窓の大きさ (例えば 200x150) への縮小は PPA の 1 手
- 画面に出すときは既存の「画像」経路 (LGFX スプライトとして持ち
  `draw_image` で描く) に載せる
- **注意**: アプリ向けの色 API は RGB332 (`FmrbGfx::COLOR_*`)。Ruby の描画
  命令で 1 画素ずつ運ぶと色が落ちるうえ遅い。RGB565 のまま扱える画像経路を
  通すこと

## 4. 危険箇所

### 4.1 電源チャネルの共有 (確認済み・問題なし)

MIPI の PHY 用 LDO を DSI が握っている (`lgfx_tab5.hpp`: ch3 / 2500mV)。
IDF の `esp_ldo_regulator.c` は**参照カウント式** (`ref_cnt++`) なので、
同じ電圧で取り直せば 2 つ目の取得も通る。着手前の心配事から外してよい。

### 4.2 JPEG 符号化器の取り合い (一番の実務的な問題)

**ハードウェアの JPEG 符号化器はリモートデスクトップが既に使っている**
(`rd_encoder_jpeg.c`)。esp_video の JPEG device も同じ器を欲しがる。

対処は 3 択。

1. リモートデスクトップが使っていないときだけ撮れる (最も簡単)
2. 撮る瞬間だけ遠隔配信を止めて器を譲る
3. 静止画を無圧縮 (BMP) で保存し、JPEG を使わない

**まずは 3 で始めるのが安全**。撮れることと保存できることを、器の調停と
切り離して確かめられる。

### 4.3 メモリ帯域

目標が「遅くてよい」なので、常時流しっぱなしにしなければ影響は小さい。
それでも preview を出す段では、表示の `render:` の ms を前後で比べる。

### 4.4 容量

factory 6MB に対して現在 4.16MB。残り 1.84MB。部品 4 つぶんが収まるかは
`idf.py size-components` で実測する。**ここが唯一、事前に読み切れない**。

## 5. 段階

| 段 | 内容 | 意図 |
|---|---|---|
| C1 | 部品と設定を入れ、1 枚取得して統計 (平均・分散) をログに出す | **絵が取れること**だけを確かめる。画面には出さない |
| C2 | PPA で縮小して窓に出す。ボタンで 1 枚更新 | 見える形にする。表示性能への影響を実測 |
| C3 | Ruby API (`Camera.open` / `Camera.shot` / `Camera.save`) と静止画アプリ | **目標地点**。保存はまず無圧縮 |
| C4 | JPEG の調停、連続 preview、明るさ等の応用 | 余力があれば |

C1 で「真っ黒でも真っ白でもない、変化する絵が取れている」ことを数字で示す
のが要点。画面に出す前に切り分けを終わらせる。

## 6. 検証の見通し

**マイクと違い、ここはこちらだけで検証できる**。窓に出た絵はリモート
デスクトップの画面取得でそのまま見えるし、保存した画像は HTTP で取り出せる。
ユーザに頼むのは「機械を何かに向ける」ことだけ。

| 見るもの | 方法 |
|---|---|
| 取得が成立しているか | C1 の統計 (固定値でない、明るさが環境で変わる) |
| 絵が正しいか | 窓を画面取得で見る。向き・色・上下反転 |
| 表示を壊していないか | `render:` の ms を前後で比べる |
| 保存が正しいか | 保存した画像を取り出して開く |

## 7. 見積もり

目標が静止画に絞られ、M5 の実績設定をそのまま使えるぶん、当初見積もりより
軽い。

| 段階 | 時間 |
|---|---|
| C1 (部品 + 設定 + 立ち上げ + 統計) | 半日〜1 日 |
| C2 (窓に出す) | 半日 |
| C3 (Ruby API と静止画アプリ) | 半日 |
| 実機での詰め | 半日 |

合計 2〜2.5 日ぶん。実装中に分かったことは [report/](report/) に置く。

## 8. 先に決めたいこと

- **窓の大きさ**。preview を何ドットで出すか (例: 200x150)
- **保存先と形式**。`/home` に無圧縮で置くか、最初から JPEG を狙うか
- **カメラの向き**。前面なので自分が写る。M5 の既定は 270 度回転
