# R1a 実施報告: Modern の解像度を選べるようにする (426x240 / 640x360)

> 状態: 済 | 更新: 2026-11-18 | 同じファームで、Modern の画面を Config から 426x240 と 640x360 に切り替えられるようにした (既定は 426x240 のまま)。決め打ちを直し、マウスの倍率を 426x240 基準にし、遠隔の画面・sim・回帰の道具を設定の値に追従させた。戻り道は「Shift を押したまま起動すると、その回だけ 426x240」で、待ちを足さずに作れた (P4-Nano ではキーボードの認識が画面の初期化の約 0.3 秒前)。sim と P4-Nano で切り替えて戻せることを確認した。戻り道の実物のキーはユーザの確認待ち

計画: ../plan.md (R1 の行) / 指示書: ../instruction_r1a.md / 前段: r0.md、r0b.md

## 0. 先に書くこと

- 作るものは全部作った。止まった所は無い。
- **戻り道は作れた**が、実物のキーで確かめていない (P4-Nano のキーボードは Claude から押せない)。
  sim では作りの上で効かない (sdl2-display の入力は画面の初期化の後に開く。4.2 節)。
  コードの道と、P4-Nano の起動ログの時刻で「待たずに間に合う」ことまでを確かめた。
  **ユーザに見てほしいこと (10 章) の 1 番**。
- Config の保存の作りを 1 つ直した (3.2 節): 今までは保存のたびに、管理する行を全部書き直していて、
  値の後ろのコメントが消え、ファイルに無い鍵 (sim の `ble_auto_start` など) が既定値で書き足されて
  いた。指示書の「ほかの行・コメントを壊さない」に合わせ、**変えた行だけ** を書き、書くときも
  コメントを残すようにした。利用者から見える値は変わらない。判断を 9 章に書いた。
- P4-Nano は 426x240 に戻した (`/etc/system_conf.toml` は焼く前に退避したものと md5 が同じ
  `3c27f7cd...`)。`/home` には触っていない。

## 1. 直した決め打ち (r0.md 6 章の表に対して)

共通の定数を `main/kernel/fmrb_kernel.h` に置いた: `FMRB_MODERN_STD_W/H` (426x240)、
`FMRB_MODERN_HIGH_W/H` (640x360)、`FMRB_MODERN_RESOLUTIONS` (Modern の基板と Modern の sim。
ブラウザ版・ネイティブ版 = `FMRB_PLATFORM_HOSTED` は入らない)、`fmrb_kernel_modern_resolution()`、
`fmrb_kernel_force_standard_display()`。

| 場所 (r0.md 6 章) | 直し方 |
|---|---|
| `fmrb_kernel.c` の system_conf の読み込み | (新規) Modern で 426x240・640x360 以外なら 426x240 として起動し、`display WxH is not a Modern resolution ...; using 426x240` を 1 行出す。ファイルは書き換えない。Retro・ブラウザ版・ネイティブ版は今のまま |
| `fmrb_app.c` `fmrb_app_fullscreen_hires_size` | 定数に置き換えた。動きは同じ: 画面が 426x240 なら 640x360 へ切り替え、640x360 なら偽 (切り替えずに画面の大きさの全画面)。sim と P4-Nano でエディタの全画面が 640x360 のまま出入りできることを確認 (2 章) |
| `touch_task.c` `TOUCH_VIRTUAL_W/H` | 定数に。タッチは前から `fmrb_host_get_screen_size` に追従し、倍率は `720 / 画面の高さ` (640x360 で 2) なので、Tab5 で 640x360 を選んでも座標はずれない (コードで確認。Tab5 は焼いていない) |
| `rd_http.c` `rd_encoder_jpeg_init(426, 240, ...)` | 起動のときの今の画面の大きさで init。WS の `info` に基本の画面 `bw`/`bh` を足した |
| `rd_stream.c` 既定の 426x240 | 定数に (画面の大きさが取れないときだけ) |
| `tools/web/remote/remote.js` `BASE_W = 426`・`WIN_W` | `info` の `bw`/`bh` から。Window の表示は基本の画面の 2 倍 (426x240 は 852x480、640x360 は 1280x720) で、整数倍になる。426x240 で全画面の高解像度モードに入ったときの見た目の大きさは今のまま |
| `usb_task.c` マウスの倍率 | 倍率の基準を Modern では 426x240 に固定 (2.3 節) |
| `appstore.app.rb` `ENV_INFO["modern"]` | `/etc/system_conf.toml` が 640x360 なら 640x360 で判定する |
| `desktop_common.rb` の `screens` | シェルとエディタに `"640x360"` を足した |
| `shm_display.h` `FMRB_SHM_MAX_WIDTH/HEIGHT` | 480x320 から 640x360 へ。枠の大きさは前から 640x360 なので、共有メモリの配置と版は変えていない。graphics-audio を作り直し、`docker compose build sdl2-display` を流した。graphics-audio のソースは変えていない |
| `tools/regress/sim_run.rb` `SIZES` | `SimRun.size_for(family)` に。Modern は sim が起動に使う `flash/etc/system_conf.toml` の値 (426x240・640x360 以外は 426x240)。使う側 (`screens.rb`・`soak.rb`・`checkpoint_sim.rb`) も合わせた |
| (直下) `tools/mcp/lib/sim.rb` `MODERN_SIZE` | 同じく `fmruby-core/flash/etc/system_conf.toml` から。自己試験に 2 件足した (640x360 の設定、ほかの値は 426x240) |

直さなかったもの:

- `display_local_task.cpp` の `DISPLAY_LOCAL_HIRES_W/H`・`screen_mode_request` ほか (r0.md 6.1 の
  display_local の行): 合成の作りは触らない約束。どれも 640x360 では使われない (困らない)。R1b で
  逆向きの切り替えを作るときの場所。
- `fmrb_kernel.c` の設定が読めないときの既定 `.display_width = 360`: 工場出荷の写しがあるので
  通常は使われない。Retro と共通の既定で、指示書の範囲 (1 章) に入っていない。
- `app_lifecycle.rb` の全画面から戻るときの 240x200: 見た目だけ。R2。
- `default_user_app_*` (386x180): Window の既定の大きさと位置は画面の大きさに依らない
  (`fmrb_app.c` で固定値)。640x360 でもはみ出さないので変えていない。
- `config/system_conf_*.toml` の既定 (R2)、`tools/regress/targets.rb` (基準は R2)。
- 直下の `tools/mcp/lib/tab5.rb` の `FB_WIDTH = 426`: JPEG の 432 を 426 に直すだけで、640 は
  そのまま報告される (r0.md 6.4)。範囲の外 (直下は sim.rb だけ)。

## 2. 動き

### 2.1 Config の項目

- 「表示」のページの先頭に **「画面の解像度」 / "Resolution"**。値は **「標準 (426x240)」/
  「高精細 (640x360)」**、英語は "Standard (426x240)" / "High (640x360)"。< > で巡回する。
- 出す所: `FmrbConst::HW_FAMILY == "modern"` で、ブラウザ版 (`BOARD == "wasm"`) とネイティブ版
  (`FmrbSystem.linux_keyboard` を持つ。キーボード配列の auto と同じ見分け方) を除く。
  つまり P4-Nano・Tab5・Modern の sim。
- 中身は合成の値 `resolution` で、保存すると `display_width`・`display_height` の 2 行になる
  (テーマのプリセットが 9 行になるのと同じ形)。開いたときは 2 行から読み、426x240・640x360 以外は
  標準を見せる。
- 流れは今の「再起動が要る項目」(言語・余白・無線) と同じ: 値を選び、**「保存して再起動」**
  (ESP32 だけに出るボタン) で書いて再起動する。項目ごとの確認のダイアログは今の Config に無いので
  足していない (「保存して再起動」のボタンが確認の段にあたる)。「保存」だけなら次の起動から。
- sim: 「保存して再起動」は出ない (Linux では前から出さない)。「保存」で
  `fmruby-core/flash/etc/system_conf.toml` が書き換わる。stack を起こし直すと切り替わるが、
  graphics-audio は大きさを次の起動で使う (今ある持ち越し) ので **2 回** 起こし直す。sim_up は
  1 回目の食い違いを見て自分で起こし直す (新しい sim.rb は設定の値で照合する)。
  `tools/dev_run_check.sh` では手で 2 回。
- 640x360・426x240 の両方で、ダイアログは崩れない (写し: sim と P4-Nano、5 章)。

### 2.2 戻り道 (Shift を押したまま起動)

- **キー: Shift (左右どちらでも)**。キーの事象が運ぶ修飾キーの状態から見るので、Shift 単独でも
  Shift を押したままのほかのキーでもよい。Shift にした理由: 修飾キーは押しっぱなしでも文字や
  キーリピートを生まない (起動の後にデスクトップへ何も打ち込まれない)、他の OS の「押しながら
  起動」でもよく使われる、Tab5 のキーボードにもある。
- **いつ見るか**: host タスクが INIT_DISPLAY を送る直前 (`init_gfx_audio` の頭) に 1 回だけ。
  Shift の状態は、入力のタスクが `host_send_key` で毎回書く (host のキューができる前でも書く。
  PSRAM の bool 1 個)。押されていれば `fmrb_kernel_force_standard_display()` が
  `g_system_config` の大きさをその回だけ 426x240 にする。ログは
  `Shift held at boot: starting at 426x240 this time (system_conf says 640x360, and is left as it is)`。
  ファイルは書き換えない。
- **待ちを足していない**。P4-Nano の起動ログ (R0b の最後のファーム、
  `tmp/modern_resolution/r0b/r0b_final_boot.log`) では、USB のキーボードが `Boot keyboard ready` に
  なるのが 1.607 秒、INIT_DISPLAY が 1.886 秒で、キーボードの方が約 0.28 秒早い。押したまま
  電源を入れれば、キーボードの最初の報告に Shift が載る。起動の時間への影響は無い (比較 1 回と
  bool の書き込み)。
- 間に合わない場合: キーボードの認識が INIT_DISPLAY より遅いとき (USB ハブを挟んだ、認識の遅い
  キーボード)。そのときは効かずに設定どおり上がる。待ちを足さない限りこれ以上は詰められない。
- PC からの戻し方は案内の文書に書いた (WiFi の遠隔操作で `/etc/system_conf.toml` の 2 行を戻して
  `fmrb_rd_fs.rb <IP> reboot`)。

### 2.3 マウスの進み方

- `usb_task.c` は前から「`mouse_scale` x 今の画面 / 基本の画面」で、全画面の高解像度モード
  (426x240 から 640x360) でも画面の上の速さを保っていた。その「基本の画面」を **Modern では
  426x240 に固定** した。640x360 を選ぶと倍率は `mouse_scale x 640/426`(縦 360/240) になり、
  同じ手の動きでポインタはモニタの上で 426x240 のときと同じ距離だけ進む。全画面の高解像度モードも
  同じ式のまま。Retro は今のまま (基本 = 設定の画面)。設定の値 (0.5) は変えない。
- タッチ (Tab5) は前から画面の高さで割る形なので、640x360 でも同じ距離 (1.1 の表)。
- 実物のマウスの進み方は確認していない (ユーザ、10 章)。

## 3. Config の保存の直し (2 行だけを書く)

### 3.1 見つけたこと

sim で 640x360 を選んで「保存」すると、2 行のほかに次が変わった (直す前):

```
- keyboard_layout = "jp" # "jp" or "us"     -> keyboard_layout = "jp"     (コメントが消える)
- language = "en" # "ja" or "en"            -> language = "en"
+ ble_auto_start = false                     (ファイルに無い鍵が既定値で足される)
+ wifi_auto_start = false
```

今の `cfg_write` は、Config が扱う行をすべて、値を変えていなくても書き直していた。sim の設定には
`ble_auto_start` が無く (カーネルの既定は true)、Config の既定 (false) で足されるので、保存しただけで
設定の意味が変わる。

### 3.2 直し方

- 開いたときの値 (`@cfg_loaded`) と比べ、**変えた行だけ** を書く。音量は今までどおり毎回書く
  (端末が自分で書く値で、ミュートと合わせて今の値を残す作り)。`audio_mute` も今までどおり。
  テーマはプリセットを変えたときだけ 9 行を書く。
- 書く行は、値の後ろのコメントを残す (`cfg_line`)。
- 保存の後は比べる基準を今の値にする (同じダイアログで 2 回保存しても正しい)。
- 結果: sim・P4-Nano とも、解像度だけを変えた保存の差分は 2 行だけ
  (`display_width`・`display_height`)。P4-Nano で 426x240 に戻した後のファイルは焼く前と
  md5 が一致した。

## 4. 受け入れ条件

### 4.1 条件 1 (sim の Modern、標準構成) — 通った

`FMRB_HW_TARGET=NARYAv4 rake build:linux` (Spinel カーネル + Spinel エディタ)、graphics-audio の
作り直し、sdl2-display のイメージの作り直しの後、`tools/dev_run_check.sh --keep` と
`fmrb_input.rb`・`fmrb_screenshot.py` で確認した。

| 確認 | 426x240 | 640x360 |
|---|---|---|
| Config から切り替え (2 行だけ変わる) | 426 → 640 を保存 | 640 → 426 を保存 |
| 起こし直すと切り替わる | 2 回目で 640x360 (1 回目は graphics-audio の持ち越しで 426x240) | 2 回目で 426x240 |
| デスクトップ | 崩れない | 崩れない (アイコンは左の列、右が空く) |
| メニュー (左上のクリック) | 開く | 開く |
| ランチャー (メニューから) | 開く | 開く (上寄り、2.5 の崩れ) |
| ファイルマネージャ (メニューから、[etc] をダブルクリック) | 開く | 開いて /etc に入る |
| Window のエディタ、1 打鍵 (`x`) | 出る | 出る |
| 全画面のエディタ (F11 で入って出る) | 640x360 に切り替わって戻る (今の高解像度モード) | 切り替えずに 640x360、戻る |
| Config | 崩れない | 崩れない |
| クリックの位置 | 上の操作が全部狙った所に当たった | 同じ |

全部 mruby の構成 (`FMRB_KERNEL_ENGINE=mruby FMRB_APP_ENGINE_DESKTOP=mruby
FMRB_APP_ENGINE_EDITOR=mruby`) で 640x360: 起動・エディタの 1 打鍵・Config が通った。
AI ヘルパーも開いた (Window の大きさのまま)。

### 4.2 条件 3 (戻り道) — 作れた。sim では確かめられず、P4-Nano は実物のキーが要る

- sim: 起動中に Shift の押下を 0.5 ms ごとに注入し続けて起動したが、効かなかった。sdl2-display が
  注入のソケットを開くのは、core が INIT_DISPLAY を送って画面ができた後 (`sdl2-display/main.c` の
  `setup_inject_socket` は FreeRTOS 側の初期化を待った後)。sim では入力の道そのものが画面の初期化
  より後にしか開かないので、この戻り道は sim では効かない。確かめのため、手元だけで host タスクの
  判定の前に 3 秒の待ちを入れた版も試したが、同じ理由で届かなかった (版は戻した。コミットしていない)。
  sim では設定ファイルを手で直せるので、sim に戻り道は要らない見立て。
- P4-Nano: キーボードを押せないので、コードの道 (2.2 節) と起動ログの時刻 (キーボードの認識が
  INIT_DISPLAY の約 0.28 秒前) までを確かめた。**ユーザに見てほしいこと** の 1 番。

### 4.3 条件 2 (P4-Nano) — 通った

`FMRB_HW_TARGET=NARYAv4 rake build:esp32`、`FMRB_HW_TARGET=NARYAv4 FLASH_BAUD=115200
HOST=192.0.2.20 PORT=/dev/ttyACM0 rake flash:app_sync` (flash 333 秒、sync 74 秒)。
操作はすべて WiFi の遠隔 (`fmrb_rd_input.rb`・`fmrb_rd_snap.rb`・`fmrb_rd_fs.rb`)。シリアルは
開いていない。

| 確認 | 結果 |
|---|---|
| 426x240 で Config を開き、640x360 を選んで「保存して再起動」 | 再起動して 640x360 で上がった。`/etc/system_conf.toml` の差分は 2 行だけ |
| 遠隔の画面の写し | 426x240 のとき JPEG 432x240 (従来)、640x360 のとき 640x360 |
| WS の `info` | 640x360: `{"w":640,"h":360,"bw":640,"bh":360,"encw":640,...}`、426x240: `{"w":426,"h":240,"bw":426,"bh":240,"encw":432,...}` |
| 遠隔の入力の座標 | 両方で、メニュー・Config の行・ボタンに狙ったとおり当たった |
| 640x360 でエディタ (Window、1 打鍵)、F11 で全画面 | 切り替えずに 640x360 の全画面、戻る |
| 640x360 で Config から 426x240 に戻して「保存して再起動」 | 426x240 で上がった。ファイルは焼く前と md5 一致 |
| マウスの倍率 | コード (2.3 節) と、640x360 で遠隔の入力が正しいことまで。実物のマウスはユーザ |

遠隔のページ (`remote.js`) の 1280x720 の表示はブラウザで開いていない (`info` の値まで)。

### 4.4 条件 4 (ほかが変わっていない)

- `rake regress`: 各段で `host: 33 suites, 33 passed`・`regress: 233 runs ..., 233 passed`。
- `rake regress:screens` (最後に 1 回): 7 章に結果。
- `rake regress:size` (最後に 1 回): 7 章に結果。
- 直下の `ruby tools/mcp/selftest_sim.rb` と `ruby tools/mcp/selftest.rb`: 全部通った。

### 4.5 条件 5 (後片付け)

8 章。

## 5. 写し

`r1a_img/` (sim は PNG、P4-Nano は遠隔の JPEG)。

| ファイル | 中身 |
|---|---|
| `sim_426_config.png` / `sim_640_config.png` | Config の「画面の解像度」(426x240 で「高精細」を選んだ所 / 640x360) |
| `sim_640_launcher.png` | 640x360 のランチャー (上寄り) |
| `sim_640_editor_window.png` / `sim_640_editor_fullscreen.png` | Window のエディタで 1 打鍵 / F11 で全画面 (切り替えなし) |
| `sim_640_file_manager.png` | ファイルマネージャで /etc に入った所 |
| `sim_640_mruby_ai_helper.png` | 全部 mruby の構成、640x360 でエディタと AI ヘルパー |
| `p4nano_426_config.jpg` / `p4nano_640_config.jpg` | P4-Nano の Config (426x240 / 640x360) |
| `p4nano_640_editor_window.jpg` / `p4nano_640_editor_fullscreen.jpg` | P4-Nano 640x360 のエディタ (1 打鍵 / 全画面) |

## 6. 640x360 で見つけた崩れ (R1b・R2 の材料、直していない)

- 属性の無い全画面のアプリ: r0.md 3 章・6.3 節のとおり (rpg_demo は左上に寄る、raycaster は
  崩れる、BASIC は小さく中央)。この段では何も変えていない (R1b)。
- デスクトップの並び: アイコンは左の 1 列 (縦に 8 個) で、右の 3/4 が空く。426x240 の 2 列から
  1 列になる。
- ランチャー: 大きさ (画素) が同じなので、640x360 では上寄りに出て、下に約 150 画素空く
  (426x240 ではほぼ全高)。
- Window のエディタ: 240x200 で左上に出て、デスクトップのアイコンの列に重なる (r0.md と同じ)。
- AI ヘルパー: Window の大きさのまま、エディタの右に並んで出る。読める。
- Config・ファイルマネージャ: 中央に出る。崩れない。
- カーソルは 2 倍で 32 画素 (r0.md 13 章、HDMI で未確認)。
- ダブルクリック: sim で、`fmrb_input.rb` の 2 回のクリックの間が空くとアイコンが開かない
  ことがあった (r0b.md の P4-Nano と同じ)。今回はメニューから開いた。640x360 に固有か
  ダブルクリックの時間の判定かは調べていない。

## 7. 回帰の結果

- `rake regress` (各段): `host: 33 suites, 33 passed`・`regress: 233 runs (82 scenes on 4 profiles), 233 passed`。
- `rake regress:screens` (最後に 1 回): `28 passed / 0 failed (modern 6/6, retro 6/6, web 5/5,
  native 6/6, native-keys 5/5)`。標準構成のエディタの 1 打鍵を含む。Modern の sim は既定の 426x240。
- `rake regress:size` (最後に 1 回): `size: 2 boards vs baseline 4e7e7e61, ok`。DIRAM は NARYAv3
  121,307 (-4,152)、TAB5 129,306 (-392)、IRAM・その他の内蔵は ±0 で、R0・R0b と 1 バイトも違わない。
  **内蔵 RAM は増えていない** (足した静的な変数は PSRAM の bool 1 個だけ)。Tab5 のファームは
  標準構成で作れた (焼いていない)。
- sim の VM プール (`regress:run`) は流していない (親が流す)。

## 8. 終わったときの状態

- P4-Nano: このブランチのファーム (app_sync)、426x240、`/etc/system_conf.toml` は焼く前と同じ。
  `/home` は触っていない。シリアルは開いていない。
- sim: 止めた。graphics-audio の `flash/etc/display_conf_linux.txt` は HEAD (320x240) に戻した。
  `fmruby-core/flash/etc/system_conf.toml` は `build:linux` が作り直す (Retro の標準構成)。
- `build-linux/`: 両リポジトリとも Retro の標準構成で作り直した。
- `build/`: NARYAv4 (regress:size が退避して戻した。P4-Nano に焼いたものと同じ構成)。
- graphics-audio: ソースは変えていないので、ブランチ `modern-resolution` は切ったが空のまま消した
  (develop のまま)。`build-linux/` は新しい `shm_display.h` で作り直してある。sdl2-display の
  イメージも作り直してある。

## 9. 自分で決めた細部

- 戻り道のキーを Shift にした (2.2 節)。
- 項目の言葉: 「画面の解像度」/「標準」「高精細」、Resolution / Standard / High。値の後ろに
  `(426x240)` を付けた。
- Config の保存を「変えた行だけ」にした (3 章)。指示書の「2 行だけ」「ほかの行・コメントを
  壊さない」を満たすのに要った。見える値は変わらないが、今まで保存のたびに起きていた
  コメントの消失と既定値の書き足しが起きなくなる。
- 解像度の項目はブラウザ版・ネイティブ版に出さない。見分けは `BOARD == "wasm"` と、
  キーボード配列の auto と同じ `FmrbSystem.respond_to?(:linux_keyboard)`。
- appstore は端末の今の画面ではなく、設定ファイルの値で判定する (戻り道で 426x240 で上がった回は
  640x360 として判定する)。アプリから画面の大きさを聞く口が無く、足すほどのことではないため。
- 案内の文書は新しく `flash/usr/share/doc/display.md`・`display_ja.md` を作り、`home*.md` の一覧に
  足した。Retro・小さい端末の storage からは外した (`config/storage_exclude_{naryav3,atoms3r,sticks3}.txt`)。
  ブラウザ版の束には入る (除く仕組みが無い。中身は Modern の端末の話と書いてある)。
- 遠隔のページの Window の表示を、基本の画面の 2 倍にした (640x360 で 1280x720)。PC のブラウザの
  幅によっては大きいかもしれない (10 章)。

## 10. ユーザに見てほしいこと

1. **戻り道**: P4-Nano を 640x360 にして、USB のキーボードの Shift を押したまま電源を入れる
   (リセットする)。426x240 で上がり、Config では「高精細」のままになっていること。
   キーボードを USB ハブ経由でつないだときにも効くか。
2. HDMI のモニタで 640x360 のデスクトップの字の読みやすさと、実物のマウスの進み方
   (426x240 のときと同じ手の動きで同じ距離か)。
3. 遠隔のページ (`http://<IP>/`) の 640x360 の表示 (1280x720) が大きすぎないか。

## 11. ユーザに決めてほしいこと

- 戻り道のキーが Shift でよいか (ほかに、F キーや Esc もありうる)。
- 640x360 のときのランチャー・デスクトップのアイコンの並び (6 章) を R2 で直すか。

## 12. 確認していないこと

- 戻り道の実物のキー (4.2 節)。
- 実物のマウスの進み方。Tab5 の実機 (焼いていない。regress:size のビルドだけ)。Tab5 のタッチの
  640x360。
- 遠隔のページをブラウザで開いた見た目。H.264 の 640x360 (基本の画面として)。
- ブラウザ版・ネイティブ版で Config に項目が出ないこと (regress:screens の web・native が通る
  ことまで。画面は見ていない)。

## 13. コミット

fmruby-core (ブランチ modern-resolution):

- `feat(kernel): Modern boots at 426x240 or 640x360, and at 426x240 with Shift held` (4db258de)
- `feat(input): the mouse keeps its on-screen speed at 640x360` (f80e610d)
- `feat(remote_desktop): follow the chosen base screen` (ffb2d086)
- `feat(desktop): Config's Resolution row, 426x240 or 640x360` (a4e8ef5b)
- `feat(sim): the base screen may be 640x360` (2ea9ab96)
- `test(tools): the Modern sim's size comes from its system_conf` (c804e6aa)
- `docs(modern_resolution): guide to the resolution and the way back` (27db86ed)
- `docs(modern_resolution): R1a report, the resolution chosen in Config` (この文書と r1a_img、docs:index)

family-mruby (直下、ブランチ modern-resolution):

- `feat(tools): sim_up expects the resolution the Modern sim's system_conf asks for` (63732c3)

fmruby-graphics-audio: コミットなし (ソースの変更が要らなかった)。
