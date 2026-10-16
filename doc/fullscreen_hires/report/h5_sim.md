# H5 実施報告: Linux sim (Modern) の高解像度モード

> 状態: 完了 (sim の確認まで) | 更新: 2026-10-16 | Modern の Linux sim でも、属性を持つ全画面アプリが上にいる間だけ画面が 640x360 になる。graphics-audio の Linux ビルドに SET_SCREEN_MODE を入れ、能力は INIT_DISPLAY の ACK で伝える。SHM は 640x360 の枠と枠ごとの大きさを持つ形 (layout 2) に変えた。WROVER と ESP32 の core のサイズは変更前と同じ

計画: ../plan.md (H5 の行) / 前段: h4.md

## 1. 結果の要約

- 決定 (ユーザ、2026-10-16): 案 (a)、graphics-audio (以下 GA) の Linux ビルドだけに
  SET_SCREEN_MODE を入れる。plan.md の受け入れ条件「sim は従来どおり動く
  (高解像度にはならない)」を変える。
- Modern の sim: 起動は 426x240 のデスクトップ。麻雀を起動すると 640x360 の
  大きい表 (手牌が絵の牌) になり、640x360 の座標のクリックが届く。kill で
  426x240 に戻る。エディタの F11 の往復、Ctrl+Tab の park と戻り、全画面 2 段の
  積みも確かめた (5 章)。
- Retro の sim (NARYAv3): 320x240 のまま。core は SET_SCREEN_MODE を 1 回も送らず、
  麻雀は「この卓は Modern とブラウザ版で動きます」を出す。
- ESP32 のビルド: GA (WROVER)、core (TAB5、NARYAv3) とも、変更前と同じコミットを
  基にしたビルドと静的メモリ・flash・bin の大きさ・アーカイブごとの表がすべて
  一致した (6 章)。

## 2. 変更の中身

### 2.1 graphics-audio (Linux ビルドだけ)

すべて `CONFIG_IDF_TARGET_LINUX` で囲った。ESP32 では元のコードがそのまま組まれる。

- `main/graphics/graphics_handler.cpp`
  - 画面モードの状態と規則を display_local (core の
    `main/drivers/display_local/display_local_task.cpp`) と同じ形で持つ: 要求で
    保留にして前の絵を保つ、持ち主が 640x360 で present したら切り替える、
    戻りは基本以下の大きさで present・消えた・隠れたで切り替える、保留は
    500ms で打ち切る、高解像度中に持ち主が 640x360 の絵を 500ms 出せなければ戻る。
    リサイズの受け取り (SET_SPRITE_CLIP) を待つ規則も同じ。
  - 高解像度中は持ち主の render buffer を 640x360 の画面にそのまま写し、
    カーソルは画面に直接描く (毎フレーム写し直すので背景の保存は要らない)。
  - 基本の画面では 640x360 の canvas を重ねない (切り替え前後の 1 枚が左上だけ
    出るのを防ぐ。display_local と同じ規則)。
  - **穴の修正**: CREATE_CANVAS と UPDATE_WINDOW が確保量を超える大きさを
    受けていた。確保量 (プールのブロックの大きさ) まで切り詰めて警告を出す。
  - EXPORT_FRAME は高解像度中なら持ち主の絵を書き出す。
  - LOAD_SPRITE_IMAGE_BMP のファイル上限を 256 KB に (ESP32 は 64 KB のまま)。
- `main/graphics/display_shm.cpp` / 新規 `display_shm.h`: 画面の sprite を
  max(基本, 640x360) のメモリの上に置き、`shm_display_set_screen_size` で
  setBuffer するだけで大きさを変える (graphics_handler が持つ g_lgfx の
  ポインタは変わらない)。大きさを変えるのは描画タスクだけで、フレームの間に
  行うので present と競合しない。
- `main/mempool/fmrb_mempool.c`: canvas のブロックを max(基本, 640x360) に
  (16 x 230,400 バイト、ホストのメモリ)。
- `main/common/fmrb_bmp332.c`: 辺の上限を `FMRB_BMP332_MAX_DIM` に。Linux は
  2048 で、上下反転は 1 バイトずつの入れ替え (行バッファをスタックに置かない)。
  ESP32 は 256 と元の行バッファのまま。
- `main/tasks/graphics_task.cpp`: INIT_DISPLAY の遅延 ACK を
  `[status][caps]` の 2 バイトに。caps は `FMRB_LINK_DISPLAY_CAP_SCREEN_MODE` (0x01)。
- `main/common/fmrb_link_protocol.h`: 0x59 と構造体の写し、caps の定義。

### 2.2 SHM と sdl2-display

- `main/common/shm_display.h` を **layout 2** にした: 枠を 640x360 に、
  各枠に自分の大きさ (`frame_width[2]` / `frame_height[2]`) を持たせ、
  `layout_version` と `shm_size` を足した。magic は "FMRB" から "FMR2" に変えた。
  音のリングは今までどおり末尾にある (`tools/fmrb_audio_probe.rb` は末尾から
  数えるので変更不要)。
- 書き手は枠に絵を書いてから大きさを書き、最後に write_index を切り替える。
- `sdl2-display/main.c`:
  - 枠の大きさが変わったらテクスチャを作り直し、`SDL_RenderSetLogicalSize` で
    窓の大きさのまま表示する。マウスの位置は SDL が論理座標で返す値を使う
    (窓の大きさを変えたときにずれていた件も同時に直る)。
  - 共有メモリが必要な大きさになるまで mmap しない (前は大きさを決める前に
    読む余地があった)。magic が違えば何が違うかを stderr に出して待つ。
    layout_version と shm_size が違えば止まる。
- 古い sdl2-display のイメージは新しい magic を待ち続け、GA は 5 秒ごとに
  「イメージが layout 2 より古いかもしれない: docker compose build sdl2-display」と出す。

### 2.3 core

- `main/kernel/host/host_task.c`: Linux ビルドだけ、INIT_DISPLAY の ACK の
  2 バイト目 (無ければ 0) を覚える (`fmrb_host_display_caps`)。
- `main/app/fmrb_app.c`: `fmrb_app_set_screen_mode` と
  `fmrb_app_fullscreen_hires_size` の #if に「Linux かつ FMRB_HW_FAMILY_MODERN」を
  足した。Linux では caps に SET_SCREEN_MODE が無ければ高解像度を返さない
  (古い GA に 640x360 の canvas を作らせない)。基本が 426x240 のときだけという
  条件は今までどおりなので、Retro の sim (320x240) では偽のまま。
- コメントの直し: `app_lifecycle.rb`、`fmrb_app.h`、`fmrb_link_protocol.h`、
  麻雀と hires_test のアプリ。カーネルの Ruby の判定は変えていない。

### 2.4 ルートの道具

- `tools/fmrb_screenshot.py`: layout 2 を読み、枠ごとの大きさで PNG にする。
  layout 1 の magic を見たら GA の作り直しを促して止まる。
- `tools/mcp/lib/sim.rb`: sim_up の最初に、sdl2-display のイメージの中の
  3 ファイル (shm_display.h、fmrb_hid_event.h、main.c) を作業ツリーと比べ、
  違えば `docker compose build sdl2-display` を示して止まる。
- `tools/mcp/fmrb_mcp_server.rb`: sim_* の説明に「高解像度中は 640x360」を足した。

## 3. display_local との違い

- **遅れて来た持ち主に画面を返す** (GA だけ)。持ち主が 640x360 の絵を出せないまま
  打ち切りで戻ったあと、その canvas が 640x360 の絵を present したら高解像度に
  入り直す (`late_owner`)。新しい要求か、その canvas が消える・隠れると忘れる。
  - 入れた理由: sim の麻雀は最初の起動で手牌のシート (95 KB) を GA へ転送して
    から描き始めるので、最初の present まで約 4 秒かかった。打ち切りの規則だけでは
    426x240 に戻ったまま、カーネルは高解像度を持っているつもりで、麻雀の絵は
    基本の画面では重ねない規則で隠れ、デスクトップの絵が出続けた。
  - display_local にも同じ穴がある (最初の present が 1 秒より遅いアプリ)。
    実機ではシートの読み込みが速いので表に出ていないが、同じ直しを入れるかは
    ユーザの判断を待つ (6 章の残り)。
- 確保量を超える canvas の切り詰めは GA の Linux ビルドだけ。WROVER にも同じ穴は
  あるが、WROVER のバイナリを変えない約束なので入れていない。Retro の core は
  画面より大きい canvas を要求しない。

## 4. 確認の手順 (Step)

- Step 1: 変更前 (core 88ab3bfb、GA e3044f2) の作業ツリーを複製し、ESP32 の
  3 ビルド (GA WROVER、core TAB5、core NARYAv3) を作ってサイズを記録した。
- Step 2: 変更後に同じ複製の手順で 3 ビルドを作り、比べた。
- Step 3: 両リポジトリを Linux (Modern) で作り、`docker compose build sdl2-display`、
  sim_up で確認した (5 章)。
- Step 4: core を `rake clean_all` → `FMRB_HW_TARGET=NARYAv3 rake build:linux` で
  Retro にし、sim を上げて確認した。GA は同じバイナリ。
- Step 5: core を Modern の Linux ビルドに戻した。`rake test` は通った。

## 5. sim での確認 (Modern、TAB5)

| 項目 | 結果 |
|---|---|
| sim_up の 1 枚 | 426x240 のデスクトップ。core のログに `INIT_DISPLAY ACK received (status=0, display caps=0x01)` |
| 麻雀の起動 (シートの転送あり) | 保留 501ms で打ち切り → 持ち主が出せず戻る → 約 4 秒後 `owner presented late` で 640x360。`mahjong: tiles: tiles_hand.bmp 4154 ms`、GA に `loaded ... tiles_hand.bmp (420x224)` |
| 麻雀の起動 (2 回目以降) | `owner presented, waited 41ms` で 640x360 |
| 640x360 の座標のクリック | 一覧の 2 行目 (200,95) で卓に入る。「始める」(50,196) で対局開始。手牌の 中 (530,325) で 中 を捨てた |
| 麻雀の kill | `owner deleted` で 426x240 のデスクトップ |
| エディタの F11 | 640x360 の全画面 → F11 で 426x240 の窓 (元の位置) → 1 打鍵 → F11 で 640x360 |
| Ctrl+Tab | 麻雀の上のエディタを park → 麻雀の 640x360 に持ち主が移る。麻雀を kill → 426x240 → Ctrl+Tab でエディタが 640x360 で戻る |
| 全画面 2 段 | 麻雀 (640x360) の上にエディタ (`default/editor_fs`) → 640x360 のまま持ち主が移る。打鍵できる |
| hires_test | 起動 640x360、F11 の往復、q で終了して 426x240 |
| 音 | 426x240 で mml の再生中に `fmrb_audio_probe.rb`: 60 窓中 39 窓で音、peak 547。高解像度中はリングが進む (3.05 秒で 95,744 サンプル) ことまで確認 |
| sim_down | 3 コンテナと zenohd が止まる |

- 麻雀の Ctrl+Tab は「fullscreen app is not switchable」で無視される (アプリの属性
  どおり)。park はエディタで確かめた。
- 麻雀には終了のキーが無いので、抜け方は kill で確かめた。自分で終わる経路は
  hires_test の q で確かめた。
- スクリーンショットは `C:\Users\Public\asterism_deploy\screenshots\sim_hires\`。

## 6. ESP32 のサイズ (変更前 → 変更後)

| ビルド | 静的メモリ | flash | bin |
|---|---|---|---|
| GA (WROVER) | DRAM 33,544 / IRAM 70,111 → 同じ | 1,545,407 → 同じ | 1,624,288 → 同じ |
| core TAB5 | D/IRAM 129,698 → 同じ | 6,533,818 → 同じ | 6,634,864 → 同じ |
| core NARYAv3 | IRAM 16,384 / D/IRAM 126,035 → 同じ | 4,357,440 → 同じ | 4,481,600 → 同じ |

- アーカイブごとの表 (`esp_idf_size --archives`) も 3 ビルドとも一致した。
- GA の ESP32 の ELF には SET_SCREEN_MODE の文字列が入っていない。

## 7. 確認できていないこと・残り

- GUI (X11 の窓) での見た目とマウス。論理サイズでの拡大 (852x480 の窓に
  640x360 は 1.33 倍) と、窓の大きさを変えたときのクリック位置はユーザの目視。
- 古い sdl2-display のイメージと新しい GA を組んだときの表示 (GA の警告と
  sim_up の拒否) は、比較の関数を作業ツリーに 1 行足して拒否されることまでは
  確かめたが、古いイメージを実際に動かしてはいない。
- 高解像度中に音が鳴っている状態の probe。鳴らせるアプリが全画面の下で止まる
  ため、リングが進むことまでしか見ていない。
- display_local に「遅れて来た持ち主」の直しを入れるか (3 章)。
- 切り替えの瞬間の 1 枚: GA は保留中は前の絵を保つので、sim では乱れは見えていない。
