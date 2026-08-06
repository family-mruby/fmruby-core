# Tab5: SPI 液晶へのミラー映像出力 (計画)

状態: **未着手** (2026-08-06 に机上設計まで完了。ローカルタスク #3)。
DSI パネルへの表示と並行して、合成済みフレームを外付け SPI 液晶にも
出す。用途はミラー表示 (撮影・展示・NTSC 変換基板の入力など)。

検討時に「フレームミラー (本案)」と「GFX コマンドストリームを Retro 同様に
SPI リンクへ tee して WROVER 側で NTSC 出力する案」を比較し、本案 (案 A) を
採用した。tee 案は帯域は小さいが相手基板が要る。

## 土台 (すでにあるもの)

- P4 の描画は「426x240 RGB565 に合成 (`g_framebuffer`) → PPA SRM 3x 拡大 →
  DSI 1280x720」(`main/drivers/display_p4/display_p4_task.cpp`)。
- **リモートデスクトップ用のフレームキャプチャ API がそのまま使える**:
  `display_p4_capture_enable / acquire / release`
  (`display_p4_task.cpp:2726-`)。render_frame が合成済みフレームを
  ダブルバッファへコピーして通知する。参照カウント式で、RD ストリームと
  SPI ミラーは共存できる。

## 設計

1. **消費者タスクを 1 つ追加** (RD の `rd_stream` と同型)。
   `display_p4_capture_acquire` でフレームを取り、PPA SRM で 426x240 →
   320x240 に縮小 (PPA はクライアント複数登録可、display_p4 の 3x 拡大とは
   HW 内で直列化)、LovyanGFX の SPI パネル (ILI9341 等) に `pushImageDMA`。
   優先度は低め (3-4)、コアは display_p4 (core 1) を避けて core 0。
2. **ピン**: Tab5 の M-Bus に SPI 一式が出ている (公式ピンマップ確認済み):
   - SCK = **G5**、MOSI = **G18**、(MISO = G19、LCD なら不要)
   - CS/DC/RST は M-Bus の自由 GPIO から: G16 / G47 / G48 (他に G45, G35,
     G51, G2, G3, G4 も空き)
   - fmruby の Tab5 現行割当 (kbd 0/1/50、タッチ 31/32/23、BL 22、I2S
     26/27/29/30、MIDI 53/54、esp_hosted SDIO 8-15) と**衝突なし**
   - `fmrb_pin_assign.h` の TAB5 ブランチに `FMRB_PIN_MIRROR_SPI_*` として
     追加する
3. **SPI ホスト**: GPSPI2 (Retro の WROVER リンク用、Tab5 未使用) か
   GPSPI3 (SD 用、Tab5 は SD ピン NC) のどちらでも空いている。
4. **帯域**: 320x240 RGB565 = 150KB/フレーム。SPI 40MHz で 1 フレーム
   約 30ms → **実効 20-25fps**。ミラー用途には十分。426x240 のまま送るなら
   204KB/フレーム = 49Mbps@30fps で、80MHz クロックなら理論上は収まる。
5. **有効化**: system_conf.toml のフラグで on/off。キャプチャ常時 ON は
   render_frame に +200KB memcpy/フレームの負荷を足すため、既定 off。

## 注意 / 前提

- **着手前提**: display_p4 は GFX と音声コマンドの唯一の消費者で負荷が
  重い (2026-08-06 のフリーズ調査参照)。ミラーはフレームコピーの分だけ
  render_frame を重くするので、体感への影響を見ながら入れる。
- PSRAM 帯域は DSI リフレッシュ + PPA + SPI DMA で競合する。フレームレートは
  固定 30fps を狙わず「間に合った分だけ送る」(フレームスキップ前提) にする。
- ILI9341 の定格 SPI クロックは 10MHz だが実力 40MHz が通例。パネル個体で
  落ちるならクロックを下げる (fps が下がるだけ)。

## 参考

- Tab5 M-Bus ピンマップ: https://docs.m5stack.com/en/core/Tab5
- RD キャプチャの実装: `display_p4_task.cpp` の
  "Frame capture for the remote desktop stream" ブロック (:342-)
- 消費者タスクの雛形: `main/drivers/remote_desktop/rd_stream.c`
