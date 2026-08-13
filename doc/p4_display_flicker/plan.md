# 計画書: Tab5 (ESP32-P4) 表示ちらつきの根本修正

作業ブランチ: `feature/p4-display-doublebuffer`(develop から分岐)。
戻せるように、ブランチ上で段階的に進める。レポートは `report/` に随時記録。

> **解決済み (結論)**: 当初の見立て(下記「根本原因」の DSI テアリング説)は
> **誤り**だった。実機計装(report/r1.md の P4DIRTY)で確定した真因は
> 「render レート制限で遅延した合成が draw_foreground の**クリア途中**
> (前景 cid=1 が全面透過)で発火し、壁紙のみの1フレームを表示」。修正は
> DSI 二重バッファではなく **flush-before-clear**(+ 選択反転の点滅修正)。
> 詳細と最終状態は report/r1.md「解決」節。以下は調査当初の記録として残す。

## 症状

Tab5 の UI で、ディレクトリの単クリック等の再描画時に一瞬壁紙が見える。
特定操作に限らず、他の場面でもたまにちらつく(ユーザ報告)。Retro (S3) や
Linux sim では出ない = Tab5 (P4) 固有。

## 根本原因(確定)

- 合成側は無罪: 計装(`display_p4_task.cpp` に一時ログ)で、コマンド列は常に
  `CLEAR -> PRESENT -> RENDER` の順で、render_frame は clear と present の
  間に割り込まない(完全フレームを合成)ことを確認済み。
- 真因は**表示ハンドオフ**: `render_frame` が合成した `g_framebuffer` を
  **PPA SRM で `g_dsi_fb` に直接・BLOCKING で全面書込**している。
  `g_dsi_fb = g_lcd.getFrameBuffer()` は **DSI パネルが常時走査している唯一の
  フレームバッファ**(m5gfx `Panel_DSI.cpp` が `dpi_config.num_fbs = 1`)。
- **二重バッファも vsync 同期も無い**ため、パネル走査中に PPA 書込が重なると
  テアリング/ちらつきが出る。全面書込なので影響範囲が広い。
- Retro は映像出力が子マイコン(WROVER)側の別フレームバッファ、sim は SHM
  ダブルバッファなので、この直書き競合が起きない。

## ゴール

DSI 出力を**二重バッファ化 + vsync でスワップ**し、PPA は非表示側(裏 FB)へ
書き、vsync で表に切り替える。走査中の FB を書き換えないのでテアリングが
消える。追加コピーは無し(ping-pong)、追加メモリは FB 1 枚分(720x1280x2 =
約 1.8MB PSRAM)。

## 方針(managed component を直接パッチしない)

- `lgfx_tab5.hpp` で `Panel_ILI9881C` を継承した `Panel_ILI9881C_DB` を作り、
  `init()` を override して DPI パネルを **`num_fbs = 2`** で生成、FB を 2 枚
  取得して公開(fb0 / fb1 / パネルハンドル)。`num_fbs` は基底 `Panel_DSI::
  init_dpi` にハードコード + 非 virtual なので、DPI 生成を再実装する。
- スワップ制御は我々の `display_p4_task.cpp` に置く: render_frame で裏 FB へ
  PPA → `esp_lcd_panel_draw_bitmap`(full-frame)で vsync 切替 → 表裏を交代。
- cursor パッチ / ブート画面 / リモート capture の各経路も裏 FB(または現在の
  表 FB)へ整合させる。

## 段階(戻せる単位で)

1. **Stage 1 (スパイク / de-risk)**: `num_fbs=2` にして裏 FB へ描画 → vsync
   スワップが実機で成立するか最小改造で確証。表示が出るか、テアリングが
   消えるか。ダメなら即戻す(ブランチ破棄)。esp_lcd DPI のスワップ挙動
   (`draw_bitmap` full-frame or 専用 API)を実機で確定する。
2. **Stage 2**: `display_p4_task` を本格 ping-pong 化。
3. **Stage 3**: cursor / boot / capture 経路を裏 FB 対応。
4. **Stage 4**: 性能計測(render/PPA/スワップ、30fps 維持)+ 全 UI で
   ちらつき・崩れなし確認。

## リスクと戻し方

- **表示が全く出なくなる**可能性(DPI 再実装ミス)。→ ブランチ作業なので
  develop に影響なし。復帰は develop を焼き直し。可能なら二重バッファを
  コンパイル/実行時トグルにして即無効化できるようにする。
- esp_lcd DPI の API/スワップ挙動は IDF バージョン依存。ヘッダはビルド
  コンテナ内。Stage 1 で実機確証してから本改修。
- cursor のちらつき(表裏どちらに描くか)。Stage 3 で詰める。
- 性能: PPA は裏 FB へ書くだけ、スワップは vsync 待ちのみ。追加コピー無し。
  Stage 4 で実測して 30fps 維持を確認。

## 参考(コード位置)

- 合成/表示: `main/drivers/display_p4/display_p4_task.cpp`
  (`render_frame` 654-、PPA SRM 769-、cursor パッチ 428-、capture 742-)
- パネル: `main/drivers/display_p4/lgfx_tab5.hpp`、m5gfx
  `Panel_DSI.cpp`(`num_fbs=1`)/ `Panel_ILI9881C.*`
