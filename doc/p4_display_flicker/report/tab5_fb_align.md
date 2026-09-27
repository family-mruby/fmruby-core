# report: Tab5 の DSI フレームバッファを 4KB 境界に揃える

> 状態: 実装済み・Tab5 実機は未確認 | 更新: 2026-09-27 | NARYAv4 で青ちらつき (DSI 走査のアンダーラン) を消した揃え (doc/naryav4/report/p6.md の B0) を共有ヘッダに移し、Tab5 にも掛けた。NARYAv4 は実機で `aligned, pad 4608`・起動窓のアンダーラン 0 件のまま。Tab5 は静的な確認とビルドまで。ブランチ feature/tab5-fb-align

## 要約

- 揃えの処理を `main/drivers/display_p4/dpi_fb_align.hpp` に移し、NARYAv4 (`lgfx_naryav4.hpp`) と Tab5 (`lgfx_tab5.hpp`) の両方から呼ぶ。
- Tab5 では M5GFX を編集せずに、パネルのクラスを継承した薄い包み (`Tab5AlignedPanel<PanelT>`) の `init()` で、M5GFX の `Panel_DSI::init()` を呼ぶ直前に揃えを掛ける。
- 起動ログに `tab5_panel: DSI 720x1280 up, RGB565 fb @%p (aligned|UNALIGNED, pad N)` を出す。
- 静的な D/IRAM は両ターゲットとも基準と同値 (TAB5 183,448 / NARYAv4 178,850)。
- NARYAv4 は flash:app 後、起動窓 (0-120 秒) でアンダーラン 0 件、`fb @0x49146000 (aligned, pad 4608)`、PSRAM の空きも B0 と同値。

## 1. Tab5 のフレームバッファの確保 (調べたこと)

- 確保するのは **esp_lcd の DPI** (IDF 5.5.4 `esp_lcd_panel_dpi.c` の `esp_lcd_new_panel_dpi`)。M5GFX の `Panel_DSI` は自分では確保せず、設定を組んで `esp_lcd_new_panel_dpi` を呼ぶだけ。
  - M5GFX は `managed_components/m5stack__m5gfx` (IDF の部品管理が取ってくる外部の部品) で、編集しない。
  - 確保は `heap_caps_calloc(1, fb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_DMA)`。揃いはキャッシュライン (64B) だけ。NARYAv4 と同じ。
- **大きさと形式**: 720x1280、RGB565 (`Panel_DSI` は RGB565 固定。IDF 6.0 未満なので `pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565`)。1 枚 720 x 1280 x 2 = 1,843,200 バイト (= 450 x 4096。4KB のちょうど倍数)。
- **M5GFX は `num_fbs = 2` で 2 枚確保する**。ただし走査されるのは 1 枚目だけ。
  - DPI は `cur_fb_index = 0` で走査を始め、切り替えるのは `esp_lcd_panel_draw_bitmap` に 2 枚目の中を指すポインタが渡されたときだけ。本体も M5GFX も `draw_bitmap` を呼ばない (grep で 0 件)。
  - 本体が描くのも 1 枚目 (`esp_lcd_dpi_panel_get_frame_buffer(.., 1, ..)` で取った `config_detail().buffer`。PPA も起動画面もここに書く)。
  - よって揃えるのは 1 枚目だけでよい。2 枚目は 1 枚目の直後 (管理領域の分ずれる) に置かれ、揃わないが走査されないので関係しない。2 枚目は 1.8MB の PSRAM を使うだけの遊び領域 (今回は触らない)。
  - `use_dma2d = true` で DMA2D の複写器も入るが、これは 2 枚の確保の後なので揃えに影響しない。
- **今の揃い方**: 実機のログが無いので番地は分からない。管理領域とキャッシュラインの揃えしか掛かっていないので、4KB に揃っているかは PSRAM の確保の履歴しだい (NARYAv4 では同じソースでも確保量の変化で 0x...6000 と 0x...4dc0 の間を動いた)。**実機で確かめる** (5 節)。
- **確保の順番と割り込める場所**: `LGFX_Device::init()` → `Panel_DSI::init()` (仮想関数) → `Panel_Device::init()` (背面光、`Bus_DSI::init()` = LDO・DSI バス・DBI の IO) → リセット待ち → `init_dpi()` (`esp_lcd_new_panel_dpi`: パネル構造体 → フレームバッファ 2 枚 → DMA2D) → パネルの初期化コマンド。
  - `init_dpi()` は仮想ではないので、その直前には入れない。入れられる一番近い場所は `Panel_DSI::init()` の入口 (派生クラスでの override)。
  - 入口からフレームバッファまでの間の確保は、DSI の部品の構造体 (`esp_lcd_dsi_bus_t` / `esp_lcd_dbi_io_t` / `esp_lcd_dpi_panel_t`) だけ。どれも `DSI_MEM_ALLOC_CAPS` で、sdkconfig が `CONFIG_LCD_DSI_OBJ_FORCE_INTERNAL=y` なので**内蔵 RAM**に取られる。PSRAM の形は変わらず、試しの確保の結果がそのまま当たる。
  - `Panel_Device::init()` の `reserveDMABuffer` は `Bus_DSI` が override していない (何もしない)。行の表 (`lineArray`) はフレームバッファの後。
  - 前提は NARYAv4 と同じ: パネルの生成はブート初期に表示タスクが行う。その間に別のタスクが PSRAM を取ると外れうる。外れても UNALIGNED のログが出るだけで、表示は従来どおり。

## 2. 実装

- `main/drivers/display_p4/dpi_fb_align.hpp` (新規): `dpi_fb_align_next(fb_size, tag)` と `dpi_fb_is_aligned(fb)`、`DPI_FB_ALIGN` (4096)。
  - 中身は NARYAv4 の `align_next_frame_buffer()` をそのまま移したもの。違いは、境界の定数名 (`NARYAV4_FB_ALIGN` → `DPI_FB_ALIGN`、値は同じ 4096) と、警告のログの tag を引数で受けることだけ。NARYAv4 は従来どおり `naryav4_hdmi` を渡すので、ログも同じ。
- `lgfx_naryav4.hpp`: メンバ関数と `NARYAV4_FB_ALIGN` を消し、共有の関数を呼ぶ。起動ログの文言は変えていない。
  - NARYAv4 の esp_idf_size の値は、.data / .bss / IRAM .text / flash .text / .rodata まで B0 (858f6aa0) の控えと 1 バイトも違わない。
- `lgfx_tab5.hpp`: `template <class PanelT> class Tab5AlignedPanel : public PanelT` を足し、3 種のパネル (ILI9881C / ST7121 / ST7123) をこれで包んで `new` する。
  - `init()` の override で、`panel_width x panel_height x 2` を渡して揃えを掛け、`PanelT::init()` を呼び、成功したら起動ログを出す。
  - メンバ変数は足していない (包みの大きさは元のクラスと同じ)。詰め物のポインタも持たない (解放しない。パネルは終了まで生きる)。

## 3. 内蔵 RAM とサイズ

どちらも `FMRB_HW_TARGET=<target> rake clean_all && rake build:esp32` (コマンドラインで指定、`.env` は触らない) で作り、esp_idf_size で測った。ビルドログの `HW target:` と CMakeCache で対象を確かめた。

| | 基準 (develop 6dcdbca9) | 今回 | 差 |
|---|---|---|---|
| TAB5 静的な D/IRAM | 183,448 | 183,448 | 0 |
| TAB5 .data / .bss / IRAM .text | 20,868 / 74,644 / 87,936 | 20,868 / 74,644 / 87,936 | 0 |
| TAB5 flash .text + .rodata | (p6 7 節の控え: 3,477,636 + 2,702,000) | 3,479,412 + 2,702,248 | 下記 |
| NARYAv4 静的な D/IRAM | 178,850 | 178,850 | 0 |
| NARYAv4 flash .text + .rodata | 3,458,940 + 2,698,624 | 3,458,940 + 2,698,624 | 0 |

- TAB5 の flash の比較元は p6 の 7 節の値 (858f6aa0 のビルド)。今回の差 (+1,776 / +248) には包みのクラス (3 種ぶん) とログの文字列が入る。develop の TAB5 を同じ日に作り直しての比較はしていない (内蔵 RAM は同値なので省いた)。
- app イメージは TAB5 0x600330 (7M 区画の空き 14%。p6 の 0x5ffb50 から +2,016)、NARYAv4 0x5fad20 (空き 15%)。
- PSRAM: NARYAv4 は待機時の `PSRAM free:` が 11,269,212 で B0 と同値。Tab5 は詰め物が 4,095 バイト以下 + 管理領域だけ減る見込み (実機で確かめる)。

## 4. NARYAv4 の確認 (実機)

- 上の NARYAv4 のビルドを MCP の flash (app_only) で書いた。/home は触っていない。
- `serial_start reset:true` で板をリセットし、そこから 120 秒 (p6 の窓 A、操作なし) の記録を数えた。

| 項目 | 結果 |
|---|---|
| 起動ログ | `naryav4_hdmi: HDMI 1280x720 up, RGB888 fb @0x49146000 (aligned, pad 4608)` |
| 窓 A のアンダーラン | **0 件** |
| Guru / abort | 0 件 |
| 待機時の `IRAM free:` / `PSRAM free:` | 150,064-150,088 / 11,269,212 (B0 と同じ範囲・同値) |

- 形式もタイミングも変えていないので、画面の目視は依頼していない。
- 記録の控え: scratchpad の `tab5align/n4_windowA.log`。

## 5. Tab5 の実機で確かめる手順 (後で行う)

Tab5 は USB-Serial-JTAG で、ポートを開くだけでリブートする。記録は `serial_start reset:false` で開きっぱなしにする (これで ROM のバナーから採れる)。

1. ビルド: `FMRB_HW_TARGET=TAB5 rake clean_all && FMRB_HW_TARGET=TAB5 rake build:esp32`。ビルドログの `HW target: TAB5 (esp32p4)` を確かめる。
   - 基準も採るなら、同じ日に develop (6dcdbca9) の TAB5 も作って bin/elf を控える (比較は同じコミット同士。基準と今回を交互に焼く)。
2. `serial_start` (port /dev/ttyACM0、reset false)。
3. MCP の `flash` を `app_only: true` で (/home を保つ)。DL モードで止まったらユーザにリセットボタンを頼む (故障と誤診しない)。
4. 起動ログを見る: `serial_log grep "tab5_panel"`。
   - 期待: `tab5_panel: DSI 720x1280 up, RGB565 fb @0x........000 (aligned, pad N)`。N は 0-4095。
   - `UNALIGNED` なら、試しの確保からフレームバッファまでの間に PSRAM の確保が挟まっている。`could not place the frame buffer` の警告も見る。
   - 同じ起動で `M1|` の行・`fmrb_task: IRAM free:` を前後比較し、内蔵 RAM が動いていないことを見る。
5. アンダーランを数える: Tab5 はポートを開くたびにリブートするので、**`serial_stop` → `serial_start reset:false` の開き直しがそのまま 0 秒の起点**になる。そこから 120 秒 (窓 A、操作なし) の `can't fetch data from external memory fast enough, underrun happens` の行を数える。
   - 窓 B (120-180 秒、待機) と窓 C (180-240 秒、10 秒ごとにアプリを入れ替え) も要るなら、p6 の scratchpad の `protocol.rb` (HTTP の `/app/kill` と `/app/launch` を使う。IP はログの `sta ip:`) がそのまま使える。Tab5 では tab5_app でも同じことができる。
   - 基準 (develop) と今回をそれぞれ 2 回以上。NARYAv4 では基準が 16-18 件、揃えると 0 件だった。Tab5 は帯域が NARYAv4 の 2/3 なので、基準でも 0 件のことがありうる。その場合は「揃えは害が無い」ことの確認にとどまり、ちらつきの原因は別と見る。
6. ユーザの目視: 起動直後と、アプリの起動のたびに、画面が一瞬乱れる (一部が青や別の色で塗られる) かを、基準と今回で見比べてもらう。

## 6. 見立てと違った点・気づき

- M5GFX が `num_fbs = 2` を要求していて、Tab5 は 1.8MB の 2 枚目を使わずに持っている。以前の二重バッファの試み (report/r1.md) の名残ではなく M5GFX の既定値。PSRAM に余裕を作りたくなったときは、`Panel_DSI::init()` を置き換えずに減らす方法は無い (`init_dpi()` が非仮想で `num_fbs` を固定している) ことに注意。
- 1 枚の大きさがちょうど 4KB の倍数なので、仮に 2 枚目を走査するようになっても、1 枚目の直後に詰めて置かれれば 2 枚目も管理領域ぶんだけずれる。2 枚目を揃える必要が出たら、同じ関数を 2 枚目の確保の直前に掛けることはできない (2 枚は同じ関数呼び出しの中で続けて確保される)。

## 後始末と状態

- 板 (P4-Nano): 今回の NARYAv4 ビルドで動作中。シリアルの記録は閉じた。
- 本体の build/ は NARYAv4 のビルド。TAB5 のビルドの bin/elf/map は scratchpad の `tab5align/tab5/` に控えた。Tab5 に焼くときは TAB5 を clean_all から作り直す。
- `.env` は作業前と同じ。sdkconfig は触っていない。
