# R1b 実施報告: 640x360 のデスクトップで、古い全画面のアプリの間だけ 426x240 にする

> 状態: 済 | 更新: 2026-11-18 | 基本の画面が 640x360 のとき、426x240 向けの全画面のアプリ (属性なし) が全画面の一番上にいる間だけ、画面を 426x240 (3 倍) に切り替えるようにした。今の SET_SCREEN_MODE の道を「切り替え先の大きさ」で一般化し、プロトコルの形は変えていない。どの大きさでも描けるアプリは `.app.toml` の `fullscreen_any_size = true` で切り替えから外し、組み込み・見本の 6 本に付けた。sim (graphics-audio) にも同じ一般化を入れた。sim と P4-Nano で、426x240 のときと同じ絵・入力・出入りを確認した。切り替えは 13〜144 ms (前の絵を保って待つ)、戻りは即時。基本が 426x240 のときの動きは変えていない

計画: ../plan.md (R1b の行) / 指示書: ../instruction_r1b.md / 前段: r0.md (7 章)、r1a.md

## 0. 先に書くこと

- 作るものは全部作った。止まった所は無い。プロトコル (`SET_SCREEN_MODE`) は項目を足さずに済んだ
  (今の width/height で切り替え先の大きさを運ぶ)。変えたのはヘッダのコメントだけ。
- **見立てに無かった直しを 1 つ入れた** (2.1 の 4): 切り替えた画面には今まで持ち主の canvas 1 枚
  だけを重ねていたので、P4-Nano で rpg_demo の地図 (アプリが別に作る canvas。P4 ではハードの
  スクロールに使う) が出ず、地図が黒くなった (`r1b_img/p4nano_640_rpg_demo_before_fix.jpg`)。
  持ち主のアプリの別の canvas も一緒に重ねるようにした。今の高解像度モードのアプリ
  (エディタ・麻雀・hires_test) は別の canvas を持たないので、426x240 の動きは変わらない。
- **残る穴を 1 つ見つけた (直していない、7 章)**: MicroPython・Lua・wasm のアプリは resize を
  処理した合図を送らないので、F キー・F11 などで大きさが変わる切り替えは打ち切り (500 ms) を
  経て 1 秒ほどかかる (最後は正しい絵になる)。組み込みでこれに当たる breakout は
  `fullscreen_any_size` を付けたので切り替えない。起動時から全画面のアプリ (BASIC など) は
  大きさが変わらないので当たらない。
- P4-Nano は 426x240 に戻した (`/etc/system_conf.toml` は焼く前と md5 が同じ `3c27f7cd...`)。
  `/home` には触っていない。

## 1. 宣言の鍵: `fullscreen_any_size`

- 鍵の名前は親の仮決めのまま **`fullscreen_any_size = true`** (整数 1 も可。`fullscreen_hires` と
  同じ読み方)。既存の鍵で意味が合うものは無かった (`resizable` は Window の角のドラッグ、
  `fullscreen_hires` は 640x360 向けの宣言)。ネイティブ版の `FMRB_FULLSCREEN_HIRES_ANY` の
  「どの大きさでも描ける」と同じ意味なので、その名前に揃えた。
- 意味: 「届いた大きさから配置を決めるので、640x360 の画面を 426x240 に切り替えなくてよい」。
  426x240 の画面では何も変わらない。宣言の無い全画面のアプリは 640x360 の画面で 426x240 に
  切り替える (既定)。
- 内部の表し方: アプリの文脈の `fullscreen_hires` (0/1/2 の値) を、ビットの
  `fullscreen_flags` (`FMRB_FULLSCREEN_HIRES` 0x01 = `fullscreen_hires`、
  `FMRB_FULLSCREEN_ANY_SIZE` 0x02 = `fullscreen_any_size`) に改めた。組み込みのエディタは
  両方 (今までの `FMRB_FULLSCREEN_HIRES_ANY` にあたる)。ネイティブ版の広い画面での振る舞い
  (HIRES は 640x360、HIRES+ANY_SIZE は画面全体、ANY_SIZE だけは今までどおり 426x240) は変えていない。
- チェックポイントの像に添える `.app.toml` に `fullscreen_any_size` も書くようにした。

### 1.1 宣言を付けたアプリ・付けなかったアプリ

組み込み (`main/ruby/default_app/`、spawner の表) の全画面は `default/editor_fs` だけで、これは
`fullscreen_hires` を持つので切り替えない。`flash/app/` の全画面 (`default_window_mode =
"fullscreen"`、F キー・F11 で全画面になるもの) を sim の 640x360 で開いて決めた。

| アプリ | 宣言 | 理由 (640x360 での見え方) |
|---|---|---|
| game/shooter | 付けた | 使える幅から隊列・自機の範囲を決める。640x360 の全画面で正しい (`sim_640_shooter_any_size.png`) |
| tool/welcome | 付けた | 幅と高さから字の大きさと配置を決める。崩れない |
| tool/picorabbit | 付けた | `@window_width/@window_height` から一覧・ボタンを配置。崩れない (P4-Nano でも確認) |
| debug/ntsc_color_test | 付けた | 幅から色の帯を割る。調整のボタンは固定位置だが、描く位置と当たりの位置が同じなので使える |
| demo/walk_scroller | 付けた | 「大きい画面では絵をそのまま、空と地面を広げる」作り (`sim_640_walk_scroller_any_size.png`) |
| game/breakout (MicroPython、F で全画面) | 付けた | 大きさが変わるたびに配置を計算し直す。640x360 の全画面で正しい (`sim_640_breakout_any_size.png`) |
| game/rpg_demo | 付けない | 固定の配置 (左上に寄る) |
| game/raycaster | 付けない | 固定の配置 (崩れる) |
| basic (*.bas、既定で全画面) | 付けない | 224x192 の文字面を中央に。426x240 の方が大きく出る |
| game/mahjong・test/hires_test | 付けない | `fullscreen_hires` を持つので切り替えない |
| test/ の全画面のもの (ckpt_full・ring_probe・crash_soon・atoms3r_hello・fence_fs・ja_width ほか) | 付けない | 試験用。既定 (切り替える) のままで困らない。1 本ずつは見ていない |
| game/othello (卓) | 付けない | Window のアプリ。全画面になるのは画面が狭い端末だけ |

アプリの描き方は直していない。

## 2. 一般化の作り

### 2.1 表示 (`main/drivers/display_local/display_local_task.cpp`)

1. 「高解像度」を「切り替え先 (alternate)」に改めた: `fb_hires`/`hires`/`pending_hires` を
   `fb_alt`/`alt`/`pending_alt` にし、大きさ `alt_w`/`alt_h` を足した (状態は今までどおり PSRAM)。
   切り替え先として受けるのは `screen_mode_alt_ok` の 2 つだけ: 基本が 640x360 より小さいときの
   640x360 (今までの高解像度モード) と、基本が 640x360 のときの 426x240。ほかは警告して無視する
   (今の「基本が 640 以上なら無視」はこの中に入った)。
2. 切り替え先の fb は最初の要求で取り、以後持ち続ける (426x240 は 204,480 B の PSRAM)。基本の
   大きさは動いている間は変わらないので、切り替え先も 1 つ。完了・遅れて出た絵・打ち切り・
   持ち主の消失・入力への大きさの通知は、大きさを `alt_w/alt_h` で見るだけで規則は同じ。
   倍率は今までどおり fb の大きさから決まる (426x240 は 3 倍)。
3. 基本の画面で切り替え前後の canvas を重ねない規則 (`canvas_left_out`): 426x240 の基本では
   今までどおり「640x360 の canvas」。640x360 の基本では「426x240 の canvas で、全画面のアプリの
   本体の canvas であるもの」(アプリの文脈を錠なしで読む `fmrb_app_canvas_fullscreen`。今まで
   ネイティブ版だけにあった関数を全部の構成に出した)。426x240 の Window と区別するため。
   ブラウザ版とネイティブ版 (`FMRB_PLATFORM_HOSTED`) には入れていない (そこでは 426x240 の
   全画面が正しい形のことがある)。
4. **切り替えた画面には、持ち主の canvas と、同じアプリの別の canvas (`create_canvas`) を重ねる**
   (`fmrb_app_canvas_extra_of`、錠なし)。0 章に書いた rpg_demo の地図のための直し。
   システムの重ね (漢字変換の行) は今までどおり。

### 2.2 カーネル (C)

- `fmrb_app_fullscreen_hires_size` を `fmrb_app_screen_mode_size(flags, &w, &h)` に置き換えた。
  「この宣言のアプリが全画面の一番上にいる間、画面を何に切り替えるか」の 1 か所:
  - 基本が 426x240 で `FMRB_FULLSCREEN_HIRES` → 640x360 (今までと同じ。対象の構成も同じ)。
  - 基本が 640x360 で HIRES も ANY_SIZE も無い → 426x240。`FMRB_MODERN_RESOLUTIONS`
    (Modern の基板と Modern の sim) だけ。ブラウザ版の 640x360 のページは切り替えない。
  - sim は graphics-audio が INIT_DISPLAY の ACK で SET_SCREEN_MODE を知ると伝えたときだけ
    (今までと同じ条件。新しい能力のビットは足していない)。
- `fmrb_app_pid_screen_mode_size(pid, ...)` (カーネルの Ruby から呼ぶ)。
- `fmrb_app_fullscreen_size_for(flags)` は上の切り替え先を最初に見る。そのため 640x360 の基本で
  宣言の無い全画面のアプリは、spawner で最初から 426x240 の canvas で作られ、F11 でも 426x240 に
  なる。
- spawner: `.app.toml` の `fullscreen_any_size` を読む。チェックポイントの像の Window を画面に
  収める所は、画面の大きさ (`fmrb_kernel_fullscreen_size`) を直接見るようにした (前は
  `fmrb_app_fullscreen_size_for(0)` で、640x360 の基本では 426x240 に縮められてしまうため)。
- Spinel の記録 (`fmrb_spx_app_info_snapshot`) の 167 バイト目 `fullscreen_hires` と、mruby の
  `_get_app_info` の `:fullscreen_hires` は使う所が無くなったので外した (記録は 167 バイト)。

### 2.3 カーネル (Ruby、mruby 版と Spinel 版の両方)

- `_fullscreen_hires_size` (引数なし) を `_screen_mode_size(pid)` (nil か [w, h]) に置き換えた。
  mruby は `lib/gems/picoruby-fmrb-kernel/ports/esp32/kernel.c`、Spinel は `fmrb_ffi.rb` と
  `fmrb_kernel_base_spinel.rb` (今までと同じく 1 整数に詰めて渡す)。
- `app_lifecycle.rb` の `screen_mode_for(pid)` は `_screen_mode_size(pid)` があれば pid、
  `sync_screen_mode` は大きさを同じ所から取る。呼ぶ場所と順番 (入るときは隠す前、戻るときは
  resize と resume の後) は変えていない。`@screen_hires_pid` は `@screen_mode_pid` にした。
- Spinel の生成 C は読んでいないが、標準構成の sim で切り替え・エディタの打鍵が動き、
  全部 mruby の構成でも同じ場面が通った (4 章)。

### 2.4 sim (graphics-audio、`main/graphics/graphics_handler.cpp`、Linux ビルドだけ)

- display_local と同じ一般化 (`alt`、`alt_w/alt_h`、`screen_mode_alt_ok`)。画面の sprite は
  今までどおり `shm_display_set_screen_size` で大きさだけを変える (共有メモリの枠は 640x360 なので
  426x240 も入る。共有メモリの配置と sdl2-display は変えていない)。
- 基本の画面で重ねない規則の 640x360 の側は、graphics-audio からアプリの文脈が見えないので、
  **形で決める**: 「原点にある色キー無しの 426x240 の canvas」。角の丸い Window (色キーあり) は
  外れない。色キー無しの 426x240 の Window をちょうど原点に置いたときだけ見えなくなる。
- 持ち主のアプリの別の canvas は graphics-audio では重ねていない (同じく文脈が見えない)。sim の
  rpg_demo は別の canvas を使わない (P4 だけの作り) ので、今の組み込みでは困らない。
- ESP32 (WROVER) のコードは変えていない (すべて `CONFIG_IDF_TARGET_LINUX` の中)。

## 3. 切り替えにかかる時間と、切り替えの瞬間

P4-Nano のシリアル (`SET_SCREEN_MODE` の受け取りから `screen mode: 426x240 owner=N (owner presented,
waited N ms)` まで):

| 場面 | 待ち |
|---|---|
| rpg_demo を開く | 25 ms (2 回) |
| raycaster を開く | 144 ms |
| BASIC (sample_12_maze) を開く | 13 ms |
| 宣言を外した ntsc_color_test を開く | 29 ms |
| 閉じる (kill) | 0 ms (`owner presented` か `owner deleted`。待たない) |

- カーネルが切り替えを決めてから表示が受け取るまでは約 20〜30 ms (`Screen mode: PID N takes ...` から
  `SET_SCREEN_MODE:` まで)。
- 待つ間は前の絵 (デスクトップ) を保ち、アプリの最初の 426x240 の絵が来た所で一度に切り替わる。
  黒い画面は挟まない (sim の写しと P4-Nano のログの順番で確認)。左上に小さく出る 1 枚は、
  作りの上で出ない (2.1 の 3。HDMI では見ていない)。
- 切り替えの瞬間の 1 枚は、今の高解像度モードと同じ作り (DSI のフレームバッファは 1 枚で、
  新しい倍率の絵をそこへ上から書く。doc/fullscreen_hires/report/h1.md 6 章) なので、HDMI では
  一瞬の乱れが出うる。Claude からは見えない (**ユーザに見てほしいこと**)。
- 打ち切り (`timed out`)・持ち主が出せない (`unshowable`)・Guru・abort は、P4-Nano の 2 回の
  シリアル (切り替え 10 回ほど) で 0 件。

## 4. 受け入れ条件

### 4.1 条件 1 (sim の Modern、基本 640x360) — 通った

`FMRB_HW_TARGET=NARYAv4 rake build:linux` (標準構成)、graphics-audio `rake build:linux`。
`flash/etc/system_conf.toml` を手で 640x360 にし、`tools/dev_run_check.sh --keep` を 2 回
(graphics-audio の持ち越しのため)。アプリは `tools/debug/fmrb_dbg_client.py localhost spawn`、
操作は `tools/fmrb_input.rb`、写しは `tools/fmrb_screenshot.py`、比較は `tools/fmrb_pngdiff.rb`。

| 確認 | 結果 |
|---|---|
| rpg_demo を開く | 426x240 になり、基本 426x240 のときの同じ場面と **画素の差 0** |
| rpg_demo でキー (右・右・下) | 位置が (32,33) → (34,34)。キーが届く |
| raycaster | 426x240。差は 12 画素で、すべて計測の数字 (`cast:87us`) の所 |
| BASIC (sample_12_maze) | 426x240。差 0 |
| 閉じる | 640x360 のデスクトップに戻る (`sim_640_desktop_after.png`) |
| クリックの位置 | 宣言を外した ntsc_color_test (426x240 になる) の調整のページで、-16 と +16 のボタン (426x240 の座標) が狙いどおり当たった (`sim_640_click_in_426.png`) |
| 宣言を付けた 6 本 | 640x360 のまま正しく出る (1.1 の表) |
| エディタの全画面 (F11、`default/editor_fs`) | 640x360 のまま、切り替えなし |
| F キーで Window と全画面を行き来 (宣言を外した breakout) | Window (640x360) → 426x240 → Window → 426x240。最後は正しいが、MicroPython なので毎回打ち切りを経る (7 章の 1) |
| park (宣言を外した picorabbit、Ctrl+Tab) | 426x240 → park で 640x360 のデスクトップ → 戻すと 426x240 → q で 640x360 (ファイルの選択のダイアログも同じ park の道) |
| 全画面の上に全画面 | エディタの全画面 (640x360) の上に rpg_demo → 426x240 → rpg_demo を閉じるとエディタの 640x360 に戻り 1 打鍵できた。rpg_demo の上にエディタの全画面 → 640x360 → エディタを閉じると rpg_demo の 426x240 に戻る (前と同じ絵。差はカーソルだけ) |
| 全部 mruby の構成 (`FMRB_KERNEL_ENGINE=mruby FMRB_APP_ENGINE_DESKTOP=mruby FMRB_APP_ENGINE_EDITOR=mruby`) | rpg_demo が 426x240 で差 0、エディタの Window で 1 打鍵、F11 で 640x360 の全画面と戻り |

### 4.2 条件 2 (sim の Modern、基本 426x240) — 通った

同じビルドで 426x240 のまま: hires_test が 640x360 で開き q で 426x240 に戻る。エディタ
(Window) で 1 打鍵、F11 で 640x360、F11 で Window。graphics-audio のログは
`screen mode: 640x360 owner=N (owner presented, waited 9ms ...)` ほか、今までと同じ形。
rpg_demo・raycaster・BASIC は切り替えない (これを 4.1 の比較の基準にした)。

### 4.3 条件 3 (P4-Nano) — 通った

`FMRB_HW_TARGET=NARYAv4 rake build:esp32`、`FMRB_HW_TARGET=NARYAv4 FLASH_BAUD=115200
HOST=192.0.2.20 PORT=/dev/ttyACM0 rake flash:app_sync` (2 回: 2.1 の 4 の直しの前と後。
各 flash 約 330 秒、sync 76 秒)。`/etc/system_conf.toml` の 2 行を 640x360 にした写しを
`fmrb_rd_fs.rb put` で置き、シリアルを開いて (リセットして) 640x360 で上げた。操作は WiFi
(`fmrb_rd_launch.rb`・`fmrb_rd_input.rb`・`fmrb_rd_snap.rb`・`fmrb_rd_kill.rb`)。シリアルは
2 回開いた (flash の前後)。

| 確認 | 結果 |
|---|---|
| デスクトップ | 遠隔の写し 640x360 |
| rpg_demo | 写し 432x240 (426x240 の JPEG)。キーで位置が動く。地図は 2.1 の 4 の後に出る (`p4nano_640_rpg_demo.jpg`) |
| raycaster | 写し 432x240、sim と同じ絵 |
| BASIC | 写し 432x240 |
| クリック (宣言を外した ntsc_color_test を /tmp に置いて開いた) | 426x240 の座標の -16 / +16 が狙いどおり当たった (128 → 112 → 128)。置いたファイルは消した |
| 閉じる | 写し 640x360 に戻る |
| shooter・picorabbit (宣言あり) | 写し 640x360 のまま |
| エディタ (Window で開いて 1 打鍵、F11) | 640x360 のまま全画面、F11 で Window |
| ログ | `screen mode:` の行に打ち切り・失敗なし (3 章) |
| 終わり | 焼く前の `/etc/system_conf.toml` を戻して `reboot`。426x240 で上がり (`Framebuffer allocated: 426x240`)、md5 が焼く前と同じ。426x240 でエディタの F11 が 640x360 になることも見た |

### 4.4 条件 4 (回帰) — 8 章

### 4.5 条件 5 (後片付け) — 9 章

## 5. 写し

`r1b_img/` (sim は PNG、P4-Nano は遠隔の JPEG)。

| ファイル | 中身 |
|---|---|
| `sim_426_rpg_demo.png` / `sim_640_rpg_demo.png` | 基本 426x240 と、基本 640x360 で切り替えた rpg_demo (画素の差 0) |
| `sim_640_raycaster.png` / `sim_640_basic.png` | 基本 640x360 で切り替えた raycaster と BASIC |
| `sim_640_desktop_after.png` | 閉じた後の 640x360 のデスクトップ |
| `sim_640_click_in_426.png` | 426x240 に切り替えた画面でのクリック (-16 を押した後) |
| `sim_640_shooter_any_size.png` / `sim_640_walk_scroller_any_size.png` / `sim_640_breakout_any_size.png` | 宣言を付けたアプリ (640x360 のまま) |
| `sim_640_editor_fs.png` | rpg_demo を閉じた後のエディタの全画面 (640x360) で 1 打鍵 |
| `p4nano_640_rpg_demo_before_fix.jpg` / `p4nano_640_rpg_demo.jpg` | P4-Nano の rpg_demo (地図が黒い直す前 / 直した後) |
| `p4nano_640_raycaster.jpg` / `p4nano_640_click_in_426.jpg` / `p4nano_640_desktop_after.jpg` | P4-Nano の raycaster、クリック、閉じた後 |
| `p4nano_640_picorabbit_any_size.jpg` / `p4nano_640_editor_fs.jpg` | P4-Nano の宣言ありのアプリとエディタの全画面 (640x360) |

## 6. 見立て (r0.md 7 章) と違った点

- 見立てどおり、決め打ちは「切り替え先 = 640x360」の所にほぼ集まっていて、一般化で済んだ。
  プロトコルは変えずに済んだ。
- 見立てに無かったもの: 切り替えた画面に持ち主の canvas 1 枚しか重ねない規則が、別の canvas を
  使うアプリ (P4 の rpg_demo) で地図を消した (2.1 の 4)。sim では起きない (sim の rpg_demo は
  別の canvas を使わない)。
- 基本の画面で重ねない規則は、見立ての「持ち主に限る」ではなく「全画面のアプリの canvas」に
  した。持ち主が決まる前 (起動直後、カーネルが要求を送る前) の 1 枚も外すため。
  graphics-audio は文脈が見えないので形で決めた (2.4)。
- MicroPython などの resize の合図が無いこと (7 章の 1) は、今まで高解像度モードを使うアプリが
  Ruby だけだったので表に出ていなかった。

## 7. 残件

1. **MicroPython・Lua・wasm のアプリの resize の合図**: 切り替えの完了は「resize を処理した後の
   present」で、その合図は FmrbApp (Ruby) が resize の処理の中で送り直す sprite clip。ほかの言語の
   土台 (例: `components/micropython/prelude/fmrb_app.py` の `_handle_resize`) は送らないので、
   大きさが変わる切り替え (F キー・F11) は打ち切り 500 ms → 持ち主が出せない 500 ms →
   遅れて出た絵で入り直す、の約 1 秒になり、その間に 2 回切り替わる。sim の breakout (宣言を外した
   もの) で確認。直すならそれぞれの土台の resize で sprite clip を送り直す (指示書の範囲の外なので
   直していない)。今の組み込みで当たるものは無い (1.1)。
2. **チェックポイント**: この段では作っていない。コードを読んだ範囲では今より悪くならない:
   全画面のアプリの像は、運んだ先の spawner が添えた `.app.toml` (今回 `fullscreen_any_size` も
   書く) から全画面の大きさを決め直す (640x360 の端末では 426x240、カーネルが切り替える)。
   Window の像を画面に収める所は 640x360 の画面の大きさを見る (2.2)。実際に運んではいない。
3. **ブラウザ版**: 640x360 のページは今までどおり切り替えない (`FMRB_MODERN_RESOLUTIONS` の外)。
   合わせるかは別に決める。
4. graphics-audio は、持ち主のアプリの別の canvas を切り替えた画面に重ねない (2.4)。今の組み込みで
   困るものは無い。
5. アプリの書き方の skill (直下の fmrb-app-new) に `fullscreen_any_size` を書き足すとよい
   (「全画面のアプリで、届いた大きさから配置するなら `fullscreen_any_size = true`」)。直下は
   触っていない。
6. 回帰の道具の `tools/regress/targets.rb`・`run.rb` は `fullscreen_hires` だけを見て全画面の大きさを
   決めている。640x360 の基本の基準は R2 で作るので、そのときに `fullscreen_any_size` も見るように
   する。

## 8. 回帰の結果

- `rake regress` (直しのたび。最後は 2.1 の 4 の後): `host: 33 suites, 33 passed`・
  `regress: 233 runs (82 scenes on 4 profiles), 233 passed`。
- `rake regress:screens` (最後に 1 回): `28 passed / 0 failed (retro 6/6, modern 6/6, web 5/5,
  native 6/6, native-keys 5/5)`。標準構成のエディタの 1 打鍵を含む。Modern の sim は既定の 426x240。
- `rake regress:size` (最後に 1 回): `size: 2 boards vs baseline 4e7e7e61, ok`。DIRAM は NARYAv3
  121,307、TAB5 129,306 で R1a と 1 バイトも違わない。IRAM・その他の内蔵も ±0。**内蔵 RAM は
  増えていない** (足した状態は PSRAM の表示の状態だけ)。Tab5 のファームは標準構成で作れた
  (焼いていない)。
- sim の VM プール (`regress:run`) は流していない (親が流す)。

## 9. 終わったときの状態

- P4-Nano: このブランチのファーム (app_sync、2 回目の版)。426x240 で動いている。
  `/etc/system_conf.toml` は焼く前と md5 が同じ。`/home` は触っていない。試験で /tmp に置いた
  2 ファイルは消した。シリアルの capture は止めた。
- sim: 止めた (`docker compose down`。regress:screens も片付け済み)。sim のために手で直した
  `fmruby-core/flash/etc/system_conf.toml` は `build:linux` が作り直した (Retro の標準構成)。
  graphics-audio の `flash/etc/display_conf_linux.txt` は HEAD に戻した。
- `build-linux/`: 両リポジトリとも Retro の標準構成で作り直した。
- `build/`: NARYAv4 (regress:size が退避して戻した。P4-Nano に焼いたものと同じ構成)。
- 試験のための宣言の外し (breakout・ntsc_color_test・picorabbit) は戻してある。

## 10. 自分で決めた細部

- 鍵の名前 `fullscreen_any_size` と、内部のビット `fullscreen_flags` (1 章)。
- 基本の画面で重ねない規則を「全画面のアプリの canvas」にした (6 章)。
- 切り替えた画面に持ち主のアプリの別の canvas も重ねる (2.1 の 4)。
- 使われなくなった app info の `fullscreen_hires` を外した (2.2)。
- 宣言を付ける・付けないの線 (1.1)。ntsc_color_test は固定位置のボタンがあるが、描く位置と
  当たりが同じで使えるので付けた。試験用のアプリには付けていない。

## 11. ユーザに見てほしいこと

1. HDMI で、640x360 のデスクトップから rpg_demo・raycaster・BASIC を開く・閉じるときの切り替えの
   一瞬の乱れ (今の高解像度モードの F11 と同じくらいか、目に付くか)。ゲームを開くたびに起きる。
2. 426x240 に切り替わった間のマウスの進み方 (倍率は 426x240 基準なので同じはず)。

## 12. ユーザに決めてほしいこと

- MicroPython・Lua・wasm の土台に resize の合図を足すか (7 章の 1)。
- 試験用 (test/) の全画面のアプリに宣言を付けるか (今は付けていない)。

## 13. 確認していないこと

- HDMI での見た目 (切り替えの瞬間、426x240 の 3 倍の余白)。Tab5 (焼いていない。regress:size の
  ビルドだけ)。Tab5 で 640x360 を選んだときのタッチでの切り替え。
- チェックポイントで全画面のアプリを運ぶこと (7 章の 2)。
- 遠隔のページ (`http://<IP>/`) をブラウザで開いた見た目と H.264 での切り替え (JPEG の写しの
  大きさまで)。
- 宣言を付けなかった試験用のアプリを 1 本ずつ。

## 14. コミット

fmruby-core (ブランチ modern-resolution):

- `feat(kernel,display): a plain fullscreen app gets a 426x240 screen on the 640x360 one` (9f9b1714)
- `feat(app): fullscreen_any_size on the apps that lay themselves out` (b302b0ac)
- `docs(modern_resolution): guide to the 426x240 screen under old fullscreen apps` (ca0bee62)
- `docs(modern_resolution): R1b report, the 426x240 screen under old fullscreen apps` (この文書と r1b_img、docs:index)

fmruby-graphics-audio (ブランチ modern-resolution、develop から):

- `feat(sim): the screen mode switches to either of Modern's two screens` (1187ba2)
