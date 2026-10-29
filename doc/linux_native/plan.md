# Linux 機で動く Family mruby (Cardputer Zero・HDMI・小型 PC のコンポジタ)

> 状態: 進行中 | 更新: 2026-10-29 | L15b (L15 の残件: 角を空ける・MicroPython の印・アイコンの一覧の hidden) 検収済 (2d38a5f5)。L15 (VM の確認で見つけたものの直し: 既定のアプリ・窓の切り抜きの補強・残る記録・リサイズの三角・Flappy・Linux のショートカット) 検収済 (b63175b0)。L14 (PC の Wi-Fi・Bluetooth・音量・通知・画面の共有・落ちたときの起こし直し) 完了 (1999c103)。L13 (PC の最大化・Super + 矢印・画面ロック。全画面・鏡・明るさはユーザ判断でやめた) 完了 (8558f302)。L12b (PC は Family mruby のアプリにも Linux の IME、コンポジタは input-method-v2・text-input-v3、sdl は SDL の IME、無ければ今のかな入力) 完了 (7b60baa7)。L12a (全端末に共通のクリップボード、PC は Wayland の選択と SDL、ブラウザ版はブラウザのクリップボードとつなぐ) 完了 (326ab3b4)。L11 (知らない種類は Linux の既定のアプリ、右クリックの「Linux のアプリで開く」、関連付けの表に `linux`・`linux:<id>`) 完了 (b3e0d804)。L7d (File Manager と /mnt の小さな直し) 完了 (9713c082)。L9e (PC の全画面: エディタは画面の大きさ、`fullscreen_hires = true` のアプリは 640x360 を整数倍で真ん中に、コンポジタの小さな直し) 完了 (4842976f)。 L1・L2・L2n・L3a・L3c・L4・L5a・L6・L7・L7b・L7c・L10・L8 完了 (x86_64・SDL で起動、hosted/ に wasm と共有の土台、ファイルの振り分け、機種の定義と fbdev・DRM・evdev・ALSA、MCP の native_* と回帰の道具、ネットワークと Zenoh、.deb とパッケージ版の決まり、320x170 のキーボード専用のデスクトップ、OS のサービス、PC のログイン画面の入口 (cage) と /mnt の Linux のファイル)。L7 の VM (Kubuntu) での確認はユーザ。次は実機が来たら L0・L3b・L5b、L9b (ランチャーの「Linux」の区分、一覧からの起動、Xwayland の遅延起動、ログイン画面の入口をコンポジタに) も完了: VirtualBox の Kubuntu 26.04 でユーザ確認済 (2026-10-27: ログイン画面の入口から起動、Linux の区分、Konsole、重なり、xeyes、Exit でログイン画面へ。VMSVGA では GLES で画面が黒く、設定ファイルに pixman と矢印のソフト描画を書いて動いた。親の窓に付いたダイアログが枠なしで一番上に出て絵の外へはみ出す不具合あり)。L9c (親を持つダイアログを枠つきで親の上の真ん中に、メニューを絵の矩形の中に収める、VM と分かれば session が自分で pixman と矢印のソフト描画に切り替える) も完了: VM で見た枠なしの窓はダイアログでなくメニューで、制約の箱が無かったのが原因。L9d (コンポジタでは Family mruby の画面を「出力 ÷ 2」にして出力を埋める (1280x800 なら 640x400、縦長と小さい出力は 426x240)、全画面のアプリは 426x240 を最大の整数倍で真ん中に、Linux の窓の四隅を透かす、「Linux」の区分はペンギン・共通の印は窓の絵、keyboard_layout = "auto" で Linux の配列に合わせる) も完了: 高解像度の全画面 (640x360) を広い画面で 2 倍で出すかはユーザの判断待ち (今は 426x240 で出る)。続けて L9e (PC の高解像度の全画面はデスクトップと同じ解像度の全画面に、コンポジタの小さな直し)・L7d (File Manager と /mnt の小さな直し、特殊なファイルは出すが開かない)・L11・L12a・L12b を順に進める (2026-10-28 ユーザ決定、実装中)。その後 L11 (Linux のファイルを Linux のアプリで開く)。その先に、毎日使える環境 (Omarchy と並ぶ所) を目標に L12 (日本語入力とコピーと貼り付け、ESP32 の端末との共存を相談してから着手)・L13 (窓と画面)・L14 (設定と丈夫さ) を置く (2026-10-27 ユーザ)。L9a (Linux の窓を代役のアプリで Family mruby の窓として扱う、自由に前後、終えるときは閉じるよう頼んでから止める、`rake regress:wl` を regress:full に) も完了。L8 (コンポジタの試作、linux_pc_wl) も完了: GPU の描画 (GLES) は WSL2 で試せず、GPU のある Linux 機で確認が要る。L10 (Shell の `!` で Linux のコマンド、pty の口は fmrb_platform_sys に) も完了。L7b (右上の電源ボタン、確認のダイアログは Enter・Esc で答える) は NARYA v4 の実機でも確認済 (HDMI の印の見た目もユーザ確認済、Retro の NTSC は未確認)。L7c (File Manager の削除の前に確認、mouse はダイアログ、keys は下の 1 行) も完了。Linux の上で動く Family mruby を 3 つの形で作る: (1) Cardputer Zero 単体 (320x170・キーボードだけ)、(2) Cardputer Zero の HDMI 出力 (USB のマウス・キーボード)、(3) Linux PC のコンポジタ (GPD の UMPC)。(1)(2) は公式の App Store で配る。1 つの土台と受け口の組み合わせで作り分け、形ごとの独自開発をしない

## 目的

- M5Stack の Cardputer Zero (Linux 機) で Family mruby を遊べるようにする。
  公式のランチャー (APPLaunch) から起動するアプリとして、App Store で配る。
- 自作のキーボードやケースから始めなくても、市販の Linux の小型ノート PC
  (GPD の UMPC) を Family mruby の Cyberdeck にできるようにする。
  ログイン画面で GNOME と並べて選べ、Linux のアプリ (ブラウザ・端末) も
  Family mruby の窓として抱える。
- 3 つの形を別々に作らない。**土台は 1 つにし、違いは受け口の実装の
  選び方と「機種の定義」で表す**。

## 3 つの形

| | (1) Cardputer 単体 | (2) Cardputer + HDMI | (3) Linux PC のコンポジタ |
|---|---|---|---|
| CPU | aarch64 (CM0、A53 4 コア 1GHz、512MB) | 同左 | x86_64 (GPD) |
| 画面 | 本体の 320x170 LCD (fbdev、RGB565) | HDMI (DRM/KMS) | 本体の液晶 (wlroots 経由) |
| 画面の形 | **320x170 (新設)** | Modern 426x240 ×3 (全画面の高解像度は 640x360 ×2) | Modern。液晶に合わせて整数倍 |
| 入力 | 本体のキーボード (evdev) | USB のマウス・キーボード (evdev) + 本体のキーボード | 内蔵のキーボード・タッチパッド (wlroots の libinput) |
| 音 | ALSA (ES8389) | ALSA (ES8389 または HDMI) | PipeWire / PulseAudio |
| デスクトップ | **キーボード専用 (新設)** | Modern (今のデスクトップ) | Modern + 外の窓 (Linux のアプリ) |
| 起動の入口 | APPLaunch の `.desktop` | APPLaunch の `.desktop` (別項目) | ログイン画面の項目 (`wayland-sessions`) |
| 配布 | App Store (arm64 `.deb`) | (1) と同じパッケージ | 自分で入れる (amd64 `.deb`) |

(1)(2) は 1 つのパッケージ・1 つの実行ファイルで、起動の引数で機種の定義を選ぶ。

## 方針

### 土台はブラウザ版の構成の兄弟にする

- Modern と同じ「1 チップ型」: core 1 プロセスの中で描画 (display_local) まで
  完結させ、graphics-audio は使わない。今の Linux sim (3 コンテナ、Retro の
  2 チップ構成を写したもの) は**回帰の確認の基盤として残し**、土台にしない。
- FreeRTOS は**ブラウザ版の協調 port** を使う。SIGALRM を使わないので、
  SDL・wlroots・ALSA を同じプロセスに入れても衝突しない (sim が 3 プロセスに
  分かれた理由ごと消える)。協調 port の Emscripten 依存は時刻の取得と
  眠りだけ (`emscripten_get_now` / `emscripten_thread_sleep`) なので、
  `clock_gettime` / `nanosleep` に替えた版を共有する。自分で自分を剥がす
  (VM のバイトコード境界で実時間を見る) 方式もそのまま使う。
- ビルドは idf.py を使わず、ブラウザ版と同じく独立した CMake で組む。
  ブラウザ版の CMake から「core の共通ソースの一覧」を切り出して両方で使う
  (wasm と native で一覧を二重に持たない)。aarch64 はクロスコンパイラで、
  x86_64 は手元の gcc でビルドする。
- 機種の系統は Modern (`FMRB_HW_FAMILY_MODERN`) を引き継ぐ。フォント・
  サービス・アプリの組は Modern と同じにし、画面の形だけを機種の定義で変える。

### 違いは受け口の実装で表す

既存の受け口をそのまま使い、足りないものだけを足す。各受け口には
「何もしない実装」を必ず置き、機種の定義で選ばなければそれになる。

| 受け口 | 既存 / 新設 | 実装 |
|---|---|---|
| 画面の出力 `display_backend_t` | 既存 (display_backend.h。P4 の PPA 版・CPU 版、wasm 版) | **fbdev** (1)、**DRM** (2)、**SDL** (開発用・(3) の試作)、**wlr** (3) |
| 音の出力 `audio_backend_t` | 既存 (audio_backend.h。P4・none・wasm) | **ALSA** (1)(2)、**SDL** (3、PipeWire へ) |
| 入力の取り込み | 既存の形 (`usb_task_*` をビルドで差し替え、`fmrb_host_send_*` へ流す) | **evdev** (1)(2)、**SDL**、**wlr** (3) |
| キーの変換 | 既存 (fmrb_keymap.c、HID の Usage ID が基準) | **Linux のキーコード → HID** の表を 1 枚。evdev も wlroots も Linux のキーコードを渡すので共通。Cardputer の Sym・Fn の層は追加の表 |
| 画面の形 | 既存の経路 (全画面の高解像度モードの実行時の大きさの切り替え) | **320x170** を足す。基本の画面の大きさを機種の定義から決める |
| OS のサービス | **新設** `fmrb_platform_sys` | 電源 (終了・電源断・休止)・電池・Wi-Fi・音量・画面の明るさ。実装は none (sim・wasm・既存の端末)、**linux** (sysfs と logind・NetworkManager) |
| 外の窓 | **新設** | (3) だけ。コンポジタが Linux のアプリの窓を kernel に知らせる (後述) |
| デスクトップ | 既存 (system_desktop、Ruby) | **mouse (今の Modern)** と **keys (キーボード専用)** の 2 種類に作り分ける |

### 置き場所 (L1 で確定)

- `hosted/`: wasm と native が共有する土台。協調 port (`hosted/port/`、時刻と
  眠りだけを `port_clock_{wasm,posix}.c` に分けた)、stub、FreeRTOS の vendor、
  core のソース一覧と定義 (`hosted/hosted.cmake`)。
- `native/`: native の CMake・SDL の受け口 (`native/backend/`)・拡大と黒帯の
  共通部品 (`native/present/`)。rake は `rakelib/native.rake` (`native:mruby`、
  `native:core`、`native:run`)。
- マクロ: `FMRB_PLATFORM_HOSTED` (1 プロセス・IDF なし、wasm と native)、
  `FMRB_RTOS_COOPERATIVE` (協調 port)、`FMRB_PLATFORM_WASM` /
  `FMRB_PLATFORM_NATIVE` (それぞれ固有)。sim と端末はどれも定義しない。
- 入力の受け口: hosted の両方は関数の表 (`input_source.h`) に載せた。端末の
  USB と sim は今の形のまま。
- 待つ呼び出しの受け口 (L2n): `hosted/gate/` の補助スレッドに、待つシステム
  コールを任せ、呼んだタスクは FreeRTOS のセマフォで眠る。native は
  `-Wl,--wrap=` でソケット・名前解決・OpenSSL の呼び出しを `native/net/net_wrap.c`
  に向ける (gem・submodule は変えない)。協調 port には poll と delete の口を
  足した (設定しなければ何もしない。ブラウザ版は設定しない)。L10 の pty にも使う。
- サービス: `config/services_native.toml` (timesync は無効。Linux の時計を使う)。
  Zenoh のルータは `system_conf_native.toml` の `zenoh_router`
  (開発中は `tcp/127.0.0.1:7447`、docker の zenohd)。
- パッケージ (L3a): `native/docker/` の Debian trixie のイメージ (日付で固定) で
  arm64 をクロスビルド、amd64 もビルドし、`rake native:deb ARCH=arm64|amd64`
  で `.deb` を作る。配布物の木は git で追跡している `flash/` だけから作り、
  `rake native:scan` で追跡外と認証情報が無いことを確かめる。パッケージ版は
  自分の実行ファイルの場所から配布物の木を見つけ、利用者の場所は
  `$XDG_*/family-mruby` (開発用は `family-mruby-dev`)。arm64 は APPLaunch の置き方で、
  入口は機種ごとの小さな起動用の実行ファイル (今は HDMI 用の 1 つ)。
- `/etc` の更新の規則 (L3a、native だけ): 起動のたびに、利用者の `/etc` に無い
  キー・サービスの節・ファイルだけを配布物から足す。利用者の値は変えない。
  利用者が消したキーは次の起動で足し直される。
- パッケージ版の決まり (2026-10-26 ユーザ決定、L3c):
  - debugd (アプリの起動・kill を外から行う口、認証なし) は**既定で止める**。
    設定で有効にしたときも **127.0.0.1 だけ**で待ち受ける (外からは ssh の転送で)。
    開発用のビルドと sim は今のまま。
  - 配布する system_conf は**配布用の値を別に用意**する (debug_mode = false、
    起動の表示は通常、Zenoh のルータは空で利用者が設定する)。開発用
    (`rake native:core`) は開発用の値のまま。
  - 実装 (L3c で確定): system_conf の `debugd` と `debugd_bind`。sim・開発用の
    既定は true / "0.0.0.0"、パッケージ版の既定は false / "127.0.0.1" で、
    パッケージ版は 127.0.0.1 以外を警告して使わない。配布用の設定は
    `config/system_conf_native_release.toml` (開発用との差は `debug_mode`・
    `zenoh_router`・`debugd` の 3 項目だけで、`native:root` が差を確かめる)。
  - Zenoh のルータが空のとき、Asterism のアプリは sim の既定のルータの名前に
    落ち、すぐ分かるエラーで止まる (固まらない)。パッケージ版で引けない
    名前なので、L5a で native のパッケージ版 (zenoh_router が空の行) では
    「ルータが設定されていない」のエラーにした。sim・端末・ブラウザ版は今のまま。
- 確認の道具: ルートの `tools/fmrb_native.rb` (`up|down|shot|input|app`)。
  debugd は TCP 5565、注入のソケットは sim と別の場所。

### 既存のコードも整理の対象にする

抽象化の過程で設計がきれいになるなら、既存のコード (端末・sim・ブラウザ版の
経路) を変えてよい (2026-10-26 ユーザ)。受け口を新しい形に合わせて
継ぎ足すより、既存の受け口のほうを直す。変えたら、既存の端末・sim・
ブラウザ版で回帰の確認を通す。候補:

- 入力: 今はビルドで `usb_task_*` の実装を丸ごと差し替えている (wasm は
  usb_task の名前のまま中身を入れ替える)。画面・音と同じ**関数の表の受け口**
  (入力の取り込み) にし、USB・evdev・SDL・wlr・ブラウザを並べる。
- ビルド: wasm/CMakeLists.txt が抱えている core のソース一覧を、wasm と
  native が共有する一覧へ切り出す。
- 協調 port: wasm/port/ の Emscripten 依存を時刻と眠りの 2 か所の
  受け口へ寄せ、wasm と native で 1 つの port にする。
- 画面の大きさ: 426x240 / 640x360 を前提にした箇所 (拡大の倍率の決め方など) を、
  機種の定義から来る値で決める形に直す。
- デスクトップ: system_desktop を、共通部分と作り分ける部分 (画面の枠・
  アプリの選び方・窓の方針) に分ける。今の Modern・Retro のデスクトップも
  新しい形の上で動かす。
- OS のサービス: 今デスクトップが端末の機能 (Wi-Fi・BLE・音量) を直接呼んでいる
  所を、`fmrb_platform_sys` 経由に揃える。

### 機種の定義

受け口の選び方・画面の形・デスクトップの種類を 1 枚の設定 (TOML) に書き、
起動の引数 `--machine <名前またはパス>` (環境変数 `FMRB_NATIVE_MACHINE`) で選ぶ。
正は `config/machines/*.toml` で、`rake native:etc` が配布物の木の `machines/` に
置く。`--set 項目=値` で 1 項目だけ変えられる。定義に書かれた受け口が無い・
装置が開けないときは、理由を出して終了コード 3 で断る (SDL に逃げない)。
用意済みの定義: `sdl`・`cardputer_zero`・`cardputer_zero_hdmi`・`linux_pc_drm`。

```toml
# config/machines/cardputer_zero_hdmi.toml (L2 で確定した項目)
name = "cardputer_zero_hdmi"
description = "..."
[screen]
form = "426x240"          # 426x240 / 320x170 (system_conf の画面の大きさより優先)
[display]
backend = "drm"           # sdl / fbdev / drm
device = "auto"           # fbdev は "auto:fb_st7789v" のように /proc/fb の名前でも
mode = "1280x720"         # auto / WxH / WxH@Hz (DRM)
rotate = 0                # 0 / 90 / 180 / 270
scale = "auto"            # auto (出力に整数倍で収まる最大) / 1-16
[input]
backend = "evdev"         # sdl / evdev
devices = ["auto"]
keymap = "cardputer_zero" # 記号の層の追加の表
keymap_device = "tca8418" # 追加の表を当てる装置 (evdev の名前に含まれる文字列)
[audio]
backend = "alsa"          # sdl / alsa / none
device = "default"
latency_ms = 40
[desktop]
variant = "mouse"         # mouse / keys
[system]
services = "linux"
exit_key = "esc_long"     # keys のメニューで Esc の長押しがプログラムを終える
```

用意済みの定義に、開発用の `sdl_keys` (SDL・320x170・keys・倍率 4) を L4 で足した。

(2) は `form = "426x240"`、`display.backend = "drm"`、`variant = "mouse"`。
(3) は `display.backend = "wlr"`、`input.backend = "wlr"`、`variant = "mouse"`、
`[compositor] external_windows = true`。

## 設計

### 画面の出力

- fbdev・DRM・wlr のどの実装も、受け取るのは今と同じ「合成済みの
  RGB565 の画面」(`present`) と、カーソルの小片 (`present_patch`)。
  合成は既存の CPU 合成 (display_blend_cpu.c) を使う。
- **拡大・黒帯・回転は共通の部品にする**。整数倍の拡大と、余白を黒で塗る処理を
  1 か所に置き、fbdev・DRM・SDL・wlr のすべてが使う。回転 (液晶が縦向きに
  付いた機種) もここに置く。
- (1) の LCD は 320x170 を等倍で出す。(2) は 426x240 を 3 倍した 1278x720 を
  720p に出す (全画面の高解像度は 640x360 の 2 倍)。
- DRM の実装は、HDMI のモードをアプリ側から設定する。Cardputer の OS イメージには
  HDMI を 320x170 に固定する設定があるが、KMS では効かない見込みで、
  L0 で確認する。

### キーボード専用のデスクトップ (keys)

- 今のデスクトップ (system_desktop.app.rb と部品 15 ファイル) から、
  画面の枠に依存しない部分を共通にする: i18n、設定とダイアログ類、
  ファイル選択、ショートカット、テーマ、起動時のアプリ情報の読み取り。
- 作り分けるのは 3 つ: 画面の枠 (メニューバー・アイコン・タスクバーと、
  keys の最小限の帯)、アプリを選ぶ画面 (mouse はランチャーの窓、keys は
  全画面のメニュー)、窓の扱いの方針 (mouse は窓、keys は常に全画面)。
- keys では、アプリは常に全画面で起動する。マウスは使わない (使えても前提にしない)。
- 画面の形 (2026-10-26 ユーザ決定): **上端に細い帯 (約 10px) を常に出し**、
  今のアプリの名前・かなの状態・時計・電池を表示する。残りの 320x160 を
  アプリの全画面に使う。アプリを選ぶメニューも帯の下に全画面で出す。
- アプリの切り替え (2026-10-26 ユーザ決定): 決まったキーで**切り替えの一覧**を
  開き、動いているアプリと「アプリを起動」を並べ、矢印と Enter で選ぶ。
  終了もここからできる。キーの組は設定で変えられるようにし、Cardputer の
  キーボードで押せる組を実機で決める (L5b)。
- 最初に対応させるアプリ (2026-10-26 ユーザ決定): **エディタ・Shell・
  File Manager**。
- 実装 (L4 で確定): デスクトップは 2 つのプログラム (`system_desktop.app.rb` と
  `system_desktop_keys.app.rb`) で、kernel から見ればどちらも `system_desktop`。
  native の spawner が `desktop.variant` で中身を選ぶ。共通の部分は
  `system_desktop/desktop_common.rb` (組み込みアプリの一覧と宣言・起動) と、
  大きさをメソッドにした File Manager・ファイル選択・i18n。keys のプログラムは
  native だけが持つ (端末・sim のビルドには入らない)。
- キー (2026-10-26 ユーザ決定、L4 で実装): 切り替えの一覧は system_conf の
  `keys_switcher_key` (既定 `ctrl+tab`、修飾キー + 1 キー)。Esc は押した・離したと
  時刻で判断し、`keys_esc_long_ms` (既定 1000) より短ければアプリへ、長ければ
  アプリの中ではアプリを閉じてメニューへ、デスクトップ (メニュー・一覧・
  File Manager) では `exit_key = "esc_long"` の機種でプログラムを終える。
  どちらも host_task が受け、native の keys のときだけ働く。
- 起動の絵と音は keys では出さない。電池は値が来るまで (L6) 出さない。

### 全画面アプリの切り替え

- mouse の全画面 (入れ子の積み重ね `@fs_stack` と退避の 1 枠
  `@parked_fullscreen_pid`) は**変えない** (L4 で確定)。積み重ねは「全画面の
  アプリがデスクトップも止める」形で、帯を出し続ける keys とは前提が違うため。
- keys では別の小さな状態 (`fmrb_kernel/keys_desktop.rb`) を持つ。デスクトップが
  起動時に `desktop_keys` (アプリを置く四角) を知らせて初めて有効になり、
  窓を持ったアプリを帯の下の全画面に置いて前に出し、前にいたアプリを
  suspend する。切り替えの一覧は動いているアプリとメニューを並べ、
  前に出す・終えるを kernel に頼む。mouse で全画面のアプリの一覧を使う形は
  今は作らない。
- 一覧を開くキーは設定で変えられるようにし、Cardputer のキーボードで押せる組を実機で決める (L5b)。

### アプリの宣言

- `.app.toml` に、対応する画面の形 (`screens`) と、マウスが要るか
  (`needs_pointer`) を宣言する欄を足す。
- keys のメニューには、320x170 に対応し、マウスの要らないアプリだけを出す。
  宣言の無いアプリは出さない (縮小して無理に出すことはしない)。
- 組み込みのアプリ (エディタ・Shell) の宣言は、デスクトップの共通の一覧
  (`desktop_common.rb`) に置く (L4 で確定。C の表を増やさないため)。
  `.app.toml` の 2 つの欄の手引きは L5a で書く。
- 対応するアプリは使い道の大きいもの (エディタ・Shell・File Manager・BASIC・
  ゲームの一部) から順に増やす。

### OS のサービス (`fmrb_platform_sys`)

- デスクトップの表示 (Wi-Fi・音量・電池) と、電源の操作を、OS に問い合わせる
  形にする。既存の端末では今の実装 (自前の Wi-Fi など) がそのまま答える。
- linux 実装はなるべく sysfs (電池は power_supply、明るさは backlight) と
  ALSA の mixer で済ませる。D-Bus を使うのは logind (電源) と NetworkManager
  (Wi-Fi) だけにする。Cardputer の OS (Raspberry Pi OS trixie) と GPD の
  Linux の両方で、同じ実装が動く見込み。
- (1)(2) では「終える = APPLaunch に戻る」で、電源断は OS 側に任せる。
  (3) ではデスクトップから電源断・再起動・休止ができる。
- 2026-10-26 ユーザ決定:
  - Wi-Fi は**状態の表示だけ** (繋がっているか・SSID・電波の強さ)。接続の
    設定は OS に任せ、Family mruby から OS のネットワークの設定を変えない。
  - 音量は **OS の音量 (ALSA の mixer)** を変える。デスクトップの音量と
    ミュートの操作が本体のスピーカーの音量そのものを動かす。
  - 電源の操作は**形で分ける**。(1)(2) は「終了 (APPLaunch に戻る)」だけ、
    (3) は電源断・再起動・休止。どちらにするかは機種の定義で選ぶ。
- 実装 (L6 で確定): 受け口は `main/kernel/fmrb_platform_sys.h` の関数の表
  (ネットワーク・電池・OS の音量・電源の操作)。既定の表が今までの端末・sim・
  ブラウザ版の答えを持ち、native は `native/sys/` の linux の表を入れる。
  ネットワークは /proc と sysfs を読むだけ、電池は power_supply、音量は ALSA の
  mixer (開けないときは中の音量)、電源は logind (libsystemd を実行時に読み込み、
  待つ所は blocking-call gate)。機種の定義に `audio.mixer`・`system.power`
  (`exit` / `logind`)・`system.power_log_only` (試験用に要求をログに出すだけ) を足した。
  デスクトップの電源の項目は `FmrbApp.power_actions` の答えで出し分ける。
- 電源の項目の置き場所 (2026-10-27 ユーザ決定): mouse のデスクトップでは、
  メニューバーの右上の電源ボタンのサブメニューに置き、左上のメニューからは
  消す。全端末で揃える (項目の無い sim・ブラウザ版はボタンを出さない)。keys の
  デスクトップはメニューの最後のまま (L7b)。
  キーでは F10 で左上のメニューを開き、左右の矢印で電源のメニューへ移る。

### コンポジタと外の窓 ((3) だけ)

- wlroots を土台にする。出発点は付属の最小の例 (tinywl)。
- 版 (2026-10-27 ユーザ決定): Kubuntu 26.04 の wlroots 0.19 に合わせる。amd64 の
  `.deb` は Ubuntu 26.04 のコンテナで作り、arm64 (Cardputer、コンポジタを
  使わない) は trixie のまま。
- L8 で確定した作り: wlroots は native の主スレッド (FreeRTOS の外) で回す。
  受け渡しは絵 (今の native_frame に eventfd の起こしを足す) と入力 (SDL と同じ
  錠の無い環) の 2 つ。窓の通知の入れ物と入力の行き先の決め方は
  `native/wlr/comp_route` (wlroots 無しで試験できる)。手元の Ubuntu 22.04 の
  ビルドは wlroots 無しのまま、コンポジタは Ubuntu 26.04 のコンテナで作り WSLg
  に入れ子で動かす (`rake native:wl` / `native:wl_run` / `native:wl_check`)。
- **解像度を混ぜて描く**: Family mruby の画面 (426x240 の合成済みの絵) を
  整数倍に拡大して敷き、Linux のアプリの窓は液晶の本来の解像度のまま重ねる。
  窓の位置と大きさは、Family mruby の座標を倍率で写した場所に合わせる。
  窓枠・メニューバーは Family mruby の粗い画素、中身は精細になる。
- **外の窓は代役のアプリで表す (L9a)**: コンポジタは Linux のアプリの窓が
  できた・消えた・大きさ・題を、ネイティブ版だけにあるブリッジのタスクへ送る。
  ブリッジは Linux の窓 1 つに、代役の小さなアプリ (linux_window.app.rb) を
  1 つ起動する。代役は Family mruby の題の帯・枠・閉じるボタン・つまみを描き、
  中身を透過の色 0x01 で塗る。window manager と input_router は代役を普通の
  窓として動かし、前後とフォーカスを決める (kernel の Ruby は変えない)。
  ブリッジは代役の中身の矩形・前後・キーボードの持ち主をコンポジタへ渡す。
- 合成: コンポジタは Linux の窓を絵の下に描き、代役の中身の矩形の中で 0x01 の
  まま残った画素だけを透明にする。Family mruby の窓・メニュー・ダイアログが
  重なればそこは透けないので、前後関係は絵が決める。ポップアップは絵の上。
- 入力: 前にある窓が代役なら、キーはコンポジタから Linux の窓へ直接送る
  (Ctrl+Tab と Ctrl+, はいつも Family mruby)。ポインタは穴の上なら出力の
  画素の細かさで Linux の窓へ。キーの配列は keyboard_layout に追従する。
  題の帯の操作・閉じるボタン (xdg の close を頼む) は Family mruby の窓と同じ。
  最大化・全画面は今は断る。
- 動作の流れ: wlroots の処理の流れ (`wl_event_loop`) と FreeRTOS の協調の
  スレッド群は別々に走らせ、受け渡しの口を 2 つ (画面の絵・窓の通知と入力)
  だけにする。
- 決定 (2026-10-27 ユーザ): Family mruby の窓と Linux の窓は自由に前後する
  (Linux の窓を下に描き、Family mruby が見えるべき所を透過で空ける)。Family
  mruby を終えるときは、起動した Linux のアプリも閉じるよう頼み、数秒で
  終わらなければ止める。コンポジタの確認の一式は `rake regress:full` に載せる。
- L9 は L9a (窓の組み込み) と L9b (Linux のアプリの起動の口、Xwayland、
  ログイン画面の入口の切り替え) に分ける。
- 決定 (2026-10-27 ユーザ、L9b): Linux のアプリはランチャーの「Linux」の
  区分にまとめて並べる (Family mruby のアプリと混ぜない。区分を押すと一覧が
  入れ替わり、「..」で戻る)。ログイン画面の
  「Family mruby」の入口は cage からこのコンポジタに置き換える (cage の機種の
  定義は残す)。Xwayland は X11 のアプリが来たときに起こす。
- X11 のアプリは Xwayland で動かす。ポップアップ・メニュー・クリップボードは
  段階的に広げる。
- **注意: 画面の取り込みと打鍵の送り込みは、どのアプリにも開いている**
  (2026-10-28 ユーザ判断で現状維持、sway など wlroots 系の既定と同じ)。
  写しの口 (wlr-screencopy、ext-image-copy-capture、L14) と、仮想キーボード・
  入力メソッド (input-method-v2、L12b) を、global の出し分け
  (`wl_display_set_global_filter`) なしで全クライアントに出している。
  - どの Wayland のアプリも、確認なしで画面全体を写せる。
  - どのアプリも、フォーカスのある窓へキーを打ち込める。
  - 入力メソッドの席は 1 つで、先に fcitx5 が取っていれば後の者は断られるが、
    fcitx5 が動いていなければ空いている (取った者は他の窓の打鍵を受け取れる)。
  - 他の窓の一覧・操作 (foreign-toplevel) とクリップボードの横読み
    (data-control) の口は出していない。
  - まとめると、画面と打鍵の送り込みは X11 と同じ水準、盗み見と他の窓の操作は
    Wayland の守りが効いている。外向けに「Wayland だから安全」とは言わない。
  - 締めるときは、コンポジタが自分で起動した相手 (portal、fcitx5、swaylock) に
    だけ出す。Family mruby の中の権限 (doc/window_arranger の窓を並べる
    アプリ、打鍵の自動操作) を作るときは、この出し分けと考え方をそろえる。

### Linux のプログラムとの連携

(3) では、Family mruby の中から Linux のプログラムを使えるようにする
(2026-10-26 ユーザ)。3 つの面がある。

**1. Linux のアプリを起動する ((3)、L9)**
- kernel に「Linux のプログラムを起動する」口を足す。起動時の環境変数
  `WAYLAND_DISPLAY` をコンポジタ自身の口にするので、起動したアプリの窓は
  Family mruby の中に外の窓として出る。kernel は起動したプロセスを覚え、
  窓とプロセスを結び付ける。
- Linux のインストール済みのアプリの一覧 (XDG の `.desktop`、
  `/usr/share/applications/` と利用者の側) を読み、Family mruby のランチャーと
  メニューに「Linux のアプリ」として並べる。デスクトップのアイコンにも置ける。
- 窓の閉じるボタンは、Wayland の決まった手順でアプリに閉じるよう頼む。
  Family mruby を終えるときは、全部の窓に閉じるよう頼み、5 秒待ち、
  残ったものを止める (L9a)。
- GNOME の上の SDL の窓で動かす途中段階でも、起動の口は使える (窓は
  Family mruby の外、GNOME の普通の窓として出る)。cage の全画面の段階では
  見えないので使わない。

**2. Shell から Linux のコマンドを使う (全部の形、L10)**
- Family mruby の Shell に、Linux の端末 (pty) につないだ実行の口を足す。
  `ls` や `git` の出力を Shell の中に出し、キー入力を渡す。窓を抱える必要が
  ないので、コンポジタより前にできる。Cardputer も Linux 機なので (1)(2) でも
  使える。
- 決定 (2026-10-27 ユーザ): 頭に `!` を付けた行を Linux で実行し (`!git status`)、
  `!` だけなら Linux の対話のシェルに入って `exit` で戻る。作業ディレクトリは
  Shell の今の場所 (`/mnt/host/src` なら `~/src`)。解釈するのは行の出力と色だけ
  (`TERM=dumb`、vim・top は対象外)。
- L10 で確定した作り: pty の口は `fmrb_platform_sys` の表に足し (端末・sim・
  ブラウザ版は「使えない」)、プログラムは起こしたタスクの持ち物として、タスクが
  消えるとセッションごと終わらせる。Ctrl+C は 0x03 を端末に書く。色は SGR の
  16 色と背景・太字。Linux のコマンドには Family mruby の「読むだけ」は効かず、
  OS の権限だけが守り (全部のアプリに許す決定のとおり)。

**3. Linux のファイルを Linux のアプリで開く ((3)、L11)**
- Linux のファイルを見せる仕組み (L7) と組み合わせ、Family mruby の
  ファイル管理から Linux のファイルを選んで、Linux のアプリで開く。
- 拡張子ごとの関連付けは、既存の `/etc/associations.toml` の仕組みに
  「Linux のアプリ」という行き先を足す形にする。Linux 側の既定のアプリ
  (`xdg-open` と MIME の関連付け) に任せる行き先も選べるようにする。

**権限** (2026-10-27 ユーザ決定): Linux のプログラムの起動・Linux のコマンド・
Linux のファイルは、**全部の Family mruby のアプリに許す**。アプリごとの許可の
仕組みは作らない。

### ファイルの置き場所

Family mruby の中から見える `/` は、端末と同じ 1 つの名前空間のまま、
Linux の上では**一番上の階層の名前ごとに**実体を振り分ける
(2026-10-26 ユーザ決定、3 案から選んだ。全体を初回に写す案は更新の
取り込みに別の規則が要り、重ね合わせる案は消す・一覧の合成が複雑になるので
採らなかった)。

| Family mruby の場所 | 実体 | 備考 |
|---|---|---|
| `/usr`、`/lib`、`/bin`、`/boot`、`/help`、`/app` (下の `/app/usr` を除く) | 配布物 (読み出し専用) | パッケージの更新で丸ごと入れ替わる |
| `/home` | 利用者 | 空で始める (端末と同じく file HAL が作る) |
| `/etc` | 利用者 | 初回に配布物から写す。更新で増えた設定の項目は `system_conf.factory.toml` から、無いものだけを足す |
| `/app/usr` | 利用者 | 店から入れたアプリ |
| `/var`、`/cache`、`/data` | 利用者 (キャッシュの場所) | 消えても作り直せる物。`/data` には配布するファイルが無く、デスクトップが起動のたびに壁紙とアイコンを写す場所なので、こちらに置く (L1 で確定) |
| `/tmp` | メモリ | 今の posix の tmpfs と同じ |
| `/mnt/sd` | 無し | 端末の SD に当たる物は持たない |
| `/mnt/host`、`/mnt/root` | Linux の実ファイル | ホーム (読み書き) と `/` (読むだけ)。後述 |

- 実体の場所 (インストール時):
  - 配布物: `/usr/share/family-mruby/root/`
  - 利用者: `$XDG_DATA_HOME/family-mruby/` (既定は `~/.local/share/family-mruby/`)
  - キャッシュ: `$XDG_CACHE_HOME/family-mruby/`
  - 開発中 (WSL2) は、配布物 = リポジトリの `flash/` (読むだけ。`flash_local/` も
    重ねるかは L1 で決める)、利用者 = リポジトリの外の開発用の場所にする。
    **sim と違い、`flash/` には書き込まない**。
- 振り分けの表は file HAL の中の 1 か所 (path の解決) に置き、場所ごとに
  `if` を散らさない。sim (今の posix) は「全部を `flash/` に向ける表」として
  同じ仕組みに乗せ直してよい。
- 配布物の側に書き込もうとしたら、エラー (読み出し専用) にする。黙って
  利用者の側へ逃がさない。
- **Linux のファイルを見せる**: せっかく Linux で動くので、最終的には Linux の
  ファイルシステムを Family mruby から見えるようにする。特に (3) では必須
  (2026-10-26 ユーザ)。
- Linux のファイルの見せ方 (2026-10-27 ユーザ決定): `/mnt/host` に利用者の
  ホーム (読み書き)、`/mnt/root` に Linux の `/` 全体 (読むだけ)。(1)(2) も
  同じ (root で動くのでホームは `/root`)。読むだけの保証とホームの外へ
  書けないことは、OS の権限に頼らず Family mruby の側で守る。
- L7 で確定した作り: 振り分けの表に「Linux の実ファイルへ素通しする」行を
  2 つ足した (`/mnt/host`・`/mnt/root`)。`/mnt/host` への書き込みは、実際の
  場所 (リンクを辿った先、まだ無いファイルは有る所までの親) がホームの中に
  あるときだけ許す。装置・FIFO・ソケットは一覧には出すが開かない。見せるか
  どうかと場所は機種の定義の `[files]` (`host`・`root` に `auto`・`none`・
  ディレクトリ。既定は全機種 `auto`)。

### 配布

- (1)(2): arm64 の `.deb`。`/usr/share/APPLaunch/` の下に実行ファイル・
  アイコン・`.desktop` を 2 つ (単体用・HDMI 用) 置く。App Store の決まり
  (アイコン必須、320x170 ちょうどの画面写真 4 枚) に合わせる。配布物は
  `/usr/share/family-mruby/root/` に入れ、利用者のファイルは上の振り分けで
  利用者の側に置く (パッケージの更新で消さない)。
- (3): amd64 の `.deb`。`/usr/share/wayland-sessions/` にログイン画面の項目を置く。
  開発中は SDL の窓で起動する形も残す。
- (3) の入口 (L7 で確定): ログイン画面の項目は `family-mruby-session` を起動し、
  それが cage の中で `--machine linux_pc` (SDL の全画面、整数倍で余りは黒、
  電源は logind の 3 つと終了) を動かす。`cage` は Recommends。縦向きの液晶は
  cage の側で回す (カーソルの向きも合うため)。設定は
  `/etc/default/family-mruby-session` の `ROTATE`。音は SDL で、音量は
  Family mruby の中の音量。

## スコープ

対象:
- Linux ネイティブ版の土台 (aarch64・x86_64)
- 受け口の実装: fbdev・DRM・SDL・wlr (画面)、evdev・SDL・wlr (入力)、ALSA・SDL (音)
- 機種の定義、320x170 の画面の形、キーボード専用のデスクトップ、全画面アプリの切り替え
- OS のサービスの linux 実装
- (1)(2) の `.deb` と App Store への登録の準備 (登録そのものはユーザの許可で行う)
- (3) のログイン画面の項目とコンポジタ (外の窓)
- Linux のプログラムとの連携: Linux のアプリの起動、Shell から Linux のコマンド、
  Linux のファイルを Linux のアプリで開く

対象外 (今回はやらない):
- Retro (2 チップ構成) の Linux ネイティブ版。sim はそのまま残す
- (1)(2) で Linux のアプリを窓として抱えること
- 端数の倍率での拡大 (整数倍と黒帯だけ)
- X11 の窓の管理役として動くこと (Wayland のコンポジタだけ)
- 320x170 に未対応のアプリを縮小・スクロールで出すこと
- Family mruby 自身の描画を GPU で行うこと (合成は CPU、GPU はコンポジタの最終段だけ)

## 開発環境

- **WSL2 で大半を進める**。L1・L2・L4 (320x170 とキーボード専用のデスクトップは
  SDL の窓を 320x170 にして作る)、aarch64 のクロスビルドと qemu-user での起動の確認、
  L8・L9 (wlroots を WSLg の中で入れ子に動かす `WLR_BACKENDS=wayland`) は実機なしでできる。
- WSL2 でできないこと: DRM/KMS で画面を握る、libinput で実際の入力を読む、
  ログイン画面・logind・電源・電池・明るさ・NetworkManager。これらは実機で確かめる。
- 実機:
  - Cardputer Zero (到着待ち): L0・L3・L5 の確認 (LCD での見え方、速さ、ES8389、
    APPLaunch との受け渡し、HDMI のモードの変更)。
  - Raspberry Pi Zero 2 W: Cardputer Zero と同じ SoC (RP3A0) なので、速さと
    DRM・HDMI の確認の代わりになる (家にあるかユーザが探す)。
  - Linux の小型 PC: (3) の L7 と DRM・libinput の確認。候補は Chuwi MiniBook X
    (10.5 インチ 1920x1200、液晶は縦向きに付いていて回転が要る) を検討中。
    GPD は高いので見送りの方向。
- 仮想機械は必須にしない。小型 PC が届く前に DRM・ログイン画面を試すなら、
  Hyper-V の仮想機械を使う。

## 段取り

L1・L2 は Cardputer Zero の到着を待たずに進められる。

| 段 | 内容 | 主に使う形 |
|---|---|---|
| L0 | **Cardputer Zero の実機調査**: `uname -m`、fb の番号と形式、DRM で HDMI のモードを変えられるか、evdev のキーコード (Sym・Fn の層)、ALSA の名前、APPLaunch からの起動の権限と画面・入力の受け渡し | (1)(2) |
| L1 | **土台**: 共通ソース一覧の切り出し、協調 port の native 版、x86_64 で SDL の受け口 (画面・入力・音) で起動、ファイルの置き場所の振り分け (開発中の形)、ヘッドレスで確認する口 (画面の取得・入力の注入・アプリの起動)。今のデスクトップと既存アプリが動く | 全部 |
| L2 | **受け口と機種の定義**: 機種の定義の読み込み、fbdev・DRM・evdev・ALSA の実装、拡大・黒帯・回転の共通部品、Linux のキーコードの表。native を MCP の道具と回帰の道具に載せる | 全部 |
| L2n | **ネットワーク**: native にソケット系の gem と Zenoh を入れる。協調 port では待ち続けるソケットが全体を止めるので、待たずに確かめる (poll する) 形にする。サービス (net・timesync) の扱い (L1 で見つかった残件) | 全部 |
| L3a | **aarch64 と .deb (WSL2)**: Debian trixie のコンテナで aarch64 と x86_64 をビルドし、`.deb` を作る (Cardputer は APPLaunch の置き方、PC は普通の置き方)。arm64 のコンテナ (qemu) で入れて起動を確認。`/etc` の更新の規則 (無い項目だけ足す) | (1)(2)(3) |
| L3c | **パッケージ版の決まり**: debugd を既定で止め、有効時も 127.0.0.1 だけ。配布用の system_conf | (1)(2)(3) |
| L3b | **(2) の形を実機で**: Cardputer Zero (または Pi Zero 2 W) で DRM で HDMI に Modern、USB のマウス・キーボード、APPLaunch からの起動と終了の決まり、L2 の実機の確認の一覧 | (2) |
| L4 | **キーボード専用のデスクトップ**: デスクトップの共通部分の切り出し、keys のデスクトップ、320x170 の画面の形、全画面アプリの一覧と巡回、`.app.toml` の宣言 | (1) |
| L5a | **(1) の形の WSL2 でできる部分**: APPLaunch の 320x170 用の入口、L4 の残件 (一覧・File Manager での Esc の長押し、一覧から終えた後に前へ出すもの、`.app.toml` の欄の手引き、Zenoh のルータが空のときの扱い) | (1) |
| L5b | **(1) の形を実機で**: fbdev で本体の LCD、本体のキーボードの表、一覧のキーの組、対応アプリの最初の組、App Store の準備 (アイコン・画面写真) | (1) |
| L6 | **OS のサービス**: `fmrb_platform_sys` と linux 実装、デスクトップの表示と電源の操作をつなぎ替える | 全部 |
| L7 | **(3) の入口**: GPD でログイン画面から選べる項目 (まず cage + SDL で全画面)、拡大・回転の確認 | (3) |
| L7b | **電源ボタン**: mouse のデスクトップのメニューバーの右上に電源ボタンを置き、電源の項目をそのサブメニューに移す (全端末) | 全部 |
| L7c | **削除の確認**: File Manager の削除の前に確認を出す (mouse・keys とも、Enter = はい、Esc = いいえ) | 全部 |
| L8 | **コンポジタの試作**: tinywl に Family mruby の画面を 1 枚の絵として敷き、その上に端末を 1 つ出す。二つの動作の流れの受け渡しを確かめる | (3) |
| L9 | **コンポジタの本実装**: L9a (完了) は外の窓を代役のアプリで window manager と input router に扱わせ、解像度を混ぜて合成する。L9b (完了) は Xwayland、Linux のアプリの起動 (起動の口、XDG の `.desktop` の一覧をランチャーに並べる)、ログイン画面の入口を cage からこのコンポジタへ。L9e (完了) は PC の全画面 (エディタは画面の大きさ、`.app.toml` の `fullscreen_hires = true` のアプリは 640x360 を最大の整数倍で真ん中に) と、ダイアログへのキー・X11 のメニューを閉じる・止まった矢印の下の穴・切り替えの 1 枚・終える前の制御の口・Breeze の SVG のアイコン・Qt の Plasma の見た目 | (3) |
| L10 | **Shell から Linux のコマンド**: pty につないだ実行の口、出力と入力の受け渡し、色の基本。L1 の後ならいつでもでき、L9 を待たない (L6 の後を目安) | 全部 |
| L11 | 決定 (2026-10-28 ユーザ): Family mruby が開ける種類は今どおり、それ以外は Linux の既定のアプリ、右クリックに「Linux のアプリで開く」。**Linux のファイルを Linux のアプリで開く**: L7 のファイルを見せる仕組みと L9 の起動の口を使い、`associations.toml` に Linux のアプリの行き先を足す。完了 (b3e0d804、報告 report/l11.md) | (3) |
| L12 | 決定 (2026-10-28 ユーザ): L12a はクリップボード (kernel に全端末共通の文字のクリップボード、端末は PSRAM で内蔵 RAM +0、エディタと Shell が使う、PC は Linux の、ブラウザ版はブラウザのクリップボードとつなぐ)。L12b は日本語入力 (PC はコンポジタの形と SDL の窓の形で Family mruby のアプリにも Linux の IME を使い漢字を打てる、IME が無ければ今のかな入力、cage の形・Cardputer・端末は今のかな入力)。**毎日使う環境 (1) 日本語入力とコピーと貼り付け**: Linux の窓へのかな漢字の入力 (text-input・input-method の口、または fcitx などとつなぐ)、Family mruby と Linux のアプリの間・Linux のアプリ同士のクリップボード。今の端末 (ESP32) の Family mruby のかな入力とクリップボードとの共存の仕方を、着手の前にユーザと相談して決める | (3) |
| L13 | **毎日使う環境 (2) 窓と画面**: 最大化、キーで窓を並べる操作、画面ロック (2026-10-28 ユーザ: 全画面・複数のモニタ・明るさはやらない)。完了 (8558f302、報告 report/l13.md) | (3) |
| L14 | **毎日使う環境 (3) 設定と丈夫さ**: Wi-Fi・Bluetooth・音量の操作、通知、画面の共有と録画 (portal)、コンポジタが落ちたときの復帰。完了 (1999c103、報告 report/l14.md) | (3) |

## 受け入れ条件

共通:
- 既存の回帰の確認 (`rake regress`) が、L1 以降のどの段でも落ちない
  (sim・ブラウザ版・端末のビルドを壊さない)。
- 回帰の道具に native 版のビルドと起動の確認を足す (x86_64 の SDL を
  ヘッドレスで起動し、デスクトップの画面を撮る)。
- 受け口ごとの実装の差し替えが、機種の定義の書き換えだけで済む
  (形ごとの `#ifdef` を上の層に入れない)。

(1) Cardputer 単体:
- APPLaunch から起動し、320x170 の LCD にキーボード専用のデスクトップが出る。
- メニューからアプリを選び、全画面で起動し、キーで別のアプリに切り替えられる。
- Esc の長押しで終わり、APPLaunch に戻る (画面と入力がランチャーに戻る)。
- 音が本体のスピーカーから出る。
- App Store に出せる `.deb` (アイコン・画面写真 4 枚つき) ができる。

(2) Cardputer + HDMI:
- APPLaunch の別項目から起動し、HDMI に 1278x720 で Modern のデスクトップが出る。
- USB のマウス・キーボードで操作でき、全画面の高解像度モードも 1280x720 で出る。
- (1) と同じパッケージに入っている。

(3) Linux PC のコンポジタ:
- GPD のログイン画面で Family mruby を選んで入れ、デスクトップから電源を切れる。
- Family mruby の中から Linux の端末とブラウザを起動でき、Family mruby の窓として
  動かせ、キーとマウス (タッチパッド) がフォーカスのある窓に届く。
- Linux のインストール済みのアプリがランチャーに並び、そこから起動できる。
  窓の閉じるボタンでアプリが終わる。
- Family mruby のファイル管理から Linux のファイルを選び、関連付けた Linux の
  アプリで開ける。
- 許していない Family mruby のアプリからは、Linux のプログラムを起動できない。

Shell から Linux のコマンド (全部の形):
- Family mruby の Shell で Linux のコマンド (`ls`、`git status` など) を実行し、
  出力が Shell に出て、対話のコマンドにキー入力を渡せる。

## 親の仮決め (ユーザのレビュー待ち)

2026-10-28 ユーザ指示: 「L14 まで、推奨で実装してみて。判断はあとでまとめて
やる。多少の手戻りは許容する」。L13・L14 の仕様は親が推奨で仮に決め、
指示書 (instruction_l13.md・instruction_l14.md) の「親の仮決め」に書いた。
サブが作る途中で足した選択は各 report の「仮決めに足した選択」に書く。
ユーザがまとめてレビューし、変えるものはそのあとで直す。

- L13 の範囲 (2026-10-28 ユーザ): 最大化・キーで並べる・画面ロックだけにする。
  Linux のアプリの全画面・2 つ目のモニタ・明るさはやらない。L14 の方が重要。
- L13: PC だけ。題の帯のダブルクリックで最大化 (Linux のアプリの頼みにも
  応じる)、Linux のアプリの全画面に応じる、Super + 矢印で半分・最大化・戻す、
  画面ロックは ext-session-lock で swaylock などを使う (Family mruby は
  パスワードを扱わない)、2 つ目のモニタは鏡、明るさは logind で。
- L14: PC だけ。Wi-Fi は NetworkManager で一覧・つなぐ・切る、Bluetooth は
  BlueZ で入り切りと既知の機器 (ペアリングはしない)、音量は PipeWire があれば
  そちら、通知は freedesktop の口を受け持ち右上に数秒、画面の共有は
  xdg-desktop-portal-wlr、落ちたらセッションが起こし直す (1 分に 3 回まで)。
- L11 (サブが今の動きに合わせて選んだもの): Linux にも既定が無い種類は今どおり
  エディタで開く (指示書は「開けない」と出す、変えるなら 1 行)、`.rb` は今どおり
  実行 (出荷の表が `rb = "run"`)、`NoDisplay=true` でも `MimeType` を持つ
  `.desktop` は開く候補に出す (ランチャーには出さない)、Linux のアプリで開いたら
  File Manager を閉じる。
- L12a (サブが選んだもの): クリップボードは 64 KB の静的な塊 (端末は PSRAM に
  +65,644 バイト、内蔵 RAM +0。指示書は native・ブラウザ版はヒープ)、64 KB を
  超える Linux の文字は取り込まず空にする、エディタの Ctrl+X は今どおり終了
  (切り取りは Edit メニュー)、Shell の貼り付けは Ctrl+V と Ctrl+Shift+V で改行は
  空白に (`!` の中は Ctrl+Shift+V だけ)、選択の持ち主の Linux のアプリが終わったら
  コンポジタが最後の文字を持ち直す。
- L12b (サブが選んだもの): 変換中の文字 (preedit) はコンポジタが Family mruby の
  字体でカーソルの位置に重ねて描く (右の文字に重なる。アプリの中に差し込む形は
  大きな変更)、IME が受け持つのは入力の位置を知らせたアプリ (エディタ・Shell)
  だけでゲームのキーは通さない、IME がいる間はかなの表示を PC では出さない、
  fcitx5 はコンポジタが起こす、.deb は fcitx5・mozc を Suggests (Recommends に
  するとかな入力が止まる人が出る)、Linux のアプリの IM の環境変数は触らない。
- L13 (サブが選んだもの): 窓の操作は kernel の Ruby でなくネイティブ版の入力の口
  (C) に置く、作業領域はメニューバーより下の全部、ダブルクリックの 2 回目は
  Family mruby に渡さない、最大化した Linux の窓はアプリが小さく落ち着いても
  代役を縮めない、「ロック」は電源のメニューの一番下で確認を出さない、
  ロックのプログラムが落ちたらキーで起こし直す、機種の欄 `desktop.window_ops`
  (Cardputer と sdl_keys は false)。
- L14 (サブが選んだもの): Wi-Fi・Bluetooth はダイアログでなくネイティブ版だけの
  「設定」の窓 (280x200) で、ネットワークのダイアログに「Wi-Fi の設定...」、
  メニューに「Bluetooth」(機種の欄 `system.settings`、Cardputer と sdl_keys は
  false)、nmcli は使わず D-Bus だけ、試みの間は秘密の代理人を受け持つ (KWallet の
  パスワードを聞き直す)、失敗した新しい接続は消す、802.1X・WEP は扱わない、
  音量のキーはネイティブ版の全部の機種で効き知らせは PC だけ、知らせは 4 つまで
  押しても消えない、portal は ScreenCast と Screenshot を wlr・ほかは kde、出力は
  聞かずに 1 つ渡す (`chooser_type=none`)、pixman でも共有できるよう
  ext-image-copy-capture を足した、0 以外の終了も落ちたとして数える。

## 後で直すもの (2026-10-27 ユーザ指示で記録)

L7c の検収で見つけたもの (L7d で直した: 削除の失敗の表示、確認の外の押下で確認だけ閉じる、ダイアログを閉じた後の重なり、keys の確認の行、`/mnt/root` を確認の前に断る、`openat` と `O_NOFOLLOW` で 1 段ずつ辿る、大きさの単位、特殊なファイルは出すが開かない)。
- L7d の残件: File Manager の上のファイルの選択は外の押下で File Manager ごと
  閉じる (同じ扱いにするかは仕様の判断)、受け取りのファイル転送
  (`host_file_local.c`) は名前で開くので判定と書き込みの間の隙が残る、知らせの
  文は英語のまま、ブラウザ版の画面の比較は未実施。
- VM で見つけたもの (2026-10-28、L14 までの .deb): File Manager のエラーの知らせ
  (「Cannot delete etc: /mnt/root is read-only.」) の右の端が、File Manager の
  スクロールバーに上書きされて「(click to close)」が欠ける (知らせの上に
  スクロールバーが描かれる)。ユーザ指示で今は直さない。日本語入力 (fcitx5 +
  Mozc) は VM で打てた。Firefox (snap) からの画面の共有も VM で確認済 (https の
  ページで。http では Firefox が共有を許さず何も起きない)。
- NET1 (2026-10-28 検収済、3417cc12): PicoRuby から Chrome の DevTools Protocol を
  動かす確認で見つけた gem の欠点を直した (Net::HTTP は届いた分を読む、WebSocket は
  どの 101 も受ける・Host にポート、受信の見直しは 100 ms から 30 ms)。1 往復は
  約 100 ms から約 30 ms。NET2 (2026-10-28 検収済、b364c1a5): Linux 版
  (`FmrbConst::PLATFORM == "linux"`、ネイティブ版と sim) だけ見直しを 5 → 10 → 30 ms、
  その待ちで 1 秒何も来なければ 100 ms にした (ユーザ決定)。CDP の 1 往復は約 5 ms。
  ブラウザ版はこの gem を持たない。残件: 実機 (S3・Tab5) は一定の 30 ms のままで、
  30 ms の負担も段階の見直しも実機で未確認 (確認の後に段階を実機にも広げるかは
  ユーザ決定)、`close` の待ち (100 ms 刻み、最大 1 秒) はそのまま、client.rb は丸ごとの
  写しなので submodule を進めたら写し直す。上流への提案は台帳
  (reference/picoruby_upstream_pr_candidates.md の 2026-10-28 追加)。
- L14 の残件: VM での確認 (本物の NetworkManager・BlueZ と polkit、Plasma の
  portal との並び、ブラウザからの共有)、通知のボタン・markup・アイコン、全画面の
  アプリの上には知らせが出ない、Wi-Fi の無線の入り切りは表示だけ、arm64 の .deb は
  作り直していない。
- L13 の残件: VM での確認 (本物の swaylock をパスワードで外す所を含む)、X11 の
  アプリの最大化の頼みは自動の確認が無い、arm64 の .deb は作り直していない。
- L12b の残件: VM での確認 (Plasma の fcitx5、Konsole・Kate・Firefox)、X11 の
  プログラムで fcitx5 の XIM は使えない、SDL の窓では IME がいなくなっても
  かな入力に戻らない、SDL の X11 の D-Bus の経路は未確認。
- L12a の残件: ブラウザ版を実際のブラウザで確認 (開いたままのブラウザで道具が
  止まり未実施)、標準の形はエディタを同時に 1 つしか開けない (今の決まり)、
  primary selection・文字でない種類・64 KB 超は扱わない、VM での確認。
- L11 の残件: Shell の `open` は表の `linux` の行き先を知らない、種類の判定は
  ファイル名だけ (中身を見ない)、VM での確認。
- L9d の残件: 出力の大きさは起動時に 1 回だけ決める、既に jp を選んだ人は auto にならない、auto は起動時に 1 回だけ読む。
- 決定 (2026-10-28 ユーザ): Firefox などの二重の飾りは今はそのまま、出力の大きさの追従は今の仕様 (起動時に 1 回) のまま、以前に jp を選んだ人は auto に変えない。
- VM で見つけたもの (2026-10-28): Firefox などの自分で飾りを描くアプリは、
  サーバ側で描く頼みに従わず、Family mruby の題の帯と自分のタブの帯・窓の
  ボタンが二重になる (X11 の二重の帯と同じ扱いを決める、L13 の候補)。ログイン
  した後で出力の大きさを変えても画面は作り直さない (追従させるかは未定)。
  Linux の窓の日本語入力 (ibus が無いと Firefox が言う) は L12。
- VM の確認 (2026-10-29、L14 までの .deb git972、ユーザ): 下以外は全部通った
  (L9e の全画面・麻雀・Breeze、L7d の全項目、L11 の右クリックの候補、L12a の貼り
  付け、L12b の日本語入力、L13 の全項目、L14 の設定・Bluetooth・Mute・知らせ・画面
  の共有・終了で起こし直さない)。
  - **NOK: PDF と PNG が Okular・Gwenview でなくブラウザ (Firefox) で開く**。見立て:
    セッションが `XDG_CURRENT_DESKTOP=family-mruby` を名乗るので、Kubuntu が KDE の
    既定を書く `kde-mimeapps.list` を linux_mime.c が読まない。**VM で裏が取れた**
    (2026-10-29): `XDG_CURRENT_DESKTOP=family-mruby`、`kde-mimeapps.list` に
    `image/png=org.kde.gwenview.desktop`・`application/pdf=okularApplication_pdf.desktop`、
    汎用の mimeinfo.cache は Chrome・Firefox が先。ただし VM の Okular は PDF の
    .desktop も実行ファイルも無い (djvu・epub など一部だけ) ので、KDE の一覧を読んでも
    PDF は Okular にならない (無い先は飛ばして次に落ちる必要がある)。
  - **NOK: PDF を開いた Firefox の窓が崩れた** (Firefox の中身が代役の枠より大きく、
    後ろの Konsole の穴から見える。代役の題の帯は古い幅のまま)。原因は未調査。
  - **NOK: `pkill -SEGV` で起こし直さずログイン画面に戻った**。台本の筋は正しい。
    見立て: 起こし直した 2 回目以降がすぐ失敗して 4 回で諦めた (DRM・席の解放待ち
    など)。2026-10-29 の fm_check では、落ちたときのセッションのログは次のログインで
    上書きされて残っておらず、coredump も無く、決められなかった。もう一度落として、
    文字の端末から fm_crashlog.sh で読む。セッションの台本の記録が次のログインで
    消える作りも直す候補 (残るログに書く)。**再試行 (2026-10-29) では自動で起き
    直し、右上に知らせも出た**。最初の失敗は `pkill` 1 回だけで起きた (ユーザ談) ので
    4 回の制限ではない。記録が残らず原因は不明、時々起きる失敗として扱う。
  - 部分的: Kate の Ctrl+O のダイアログの親を押すと、親が一瞬前に出てダイアログの
    下に戻るが、キーはダイアログに戻らない (入力欄を押せば入る)。
  - 未確認 (手順の説明が要った): X11 の Kate のメニューを開いて壁紙を押す、矢印を
    止めたまま窓を出してクリック、hello.rb (Family mruby の中で動き `puts` はログに
    出るだけ)、Konsole への貼り付け (ユーザの記入が途中)、`!` で動くプログラムへの
    貼り付け。ログの行 (IME: started "fcitx5"・volume: PipeWire (wpctl)・serving
    org.freedesktop.Notifications・activation environment) は fm_check で 4 行とも
    出ていた (通った)。
  - その他のユーザの指摘: リサイズの印は四角より右下に寄せた直角三角形がよい
    (Linux の窓だけでなく大きさを変えられる窓すべてに出せるか)、Linux の
    Konsole・Firefox・Chrome があればデスクトップのショートカットにしたい、
    FlappyDemo の鳥の輪郭が透ける (原因: 鳥の BMP の輪郭が 0x01 = 透明色。Linux 版に
    限らない)、行数の多い Linux のファイルの分割をどうするか。
- L15 (2026-10-29 検収済、5413e329〜b63175b0、.deb git992): 既定のアプリは機種の
  定義の `[compositor] mime_desktops` (linux_pc_wl は `["kde"]`) の一覧も読む。
  Firefox の崩れは手元で再現できず、切り抜きの補強と試験 (wl_size_check) を足した。
  残る記録は `~/.local/state/family-mruby/session.log`。リサイズの印は大きさを変え
  られる窓すべてに直角三角形 (描画の記録の基準 24 場面を更新、9ae28058)。鳥の輪郭は
  0x20。デスクトップの `"linux:<id>"` で Linux のアプリのショートカット (既定は
  Konsole・Firefox・Chrome)。残件 (ユーザ判断待ち): Wayland の窓が頼んだ configure の
  後の別の configure に答えて自分の大きさで描くと代役が合わせない (VM の題の帯の
  古い幅の原因と見る。合わせると代役が広がる)、印がエディタのステータス行・LogViewer の
  スクロールバーに隠れる、MicroPython のアプリには印が無い、`flash/usr/share/basic/tile_a.bmp`
  にも 0x01 がある、既に `/home/desktop.toml` を持つ人にはショートカットが出ない。
  VM の確認 (2026-10-29、git992、ユーザ): PNG が Gwenview で開く、Firefox の窓が
  崩れない、ショートカットと三角の印の見え方、の 3 つとも問題なし。
  L15 の残件の決定 (2026-10-29 ユーザ):
  - **課題**: Wayland の窓が頼んだ configure の後の別の configure に答えて自分の
    大きさで描くとき、将来は代役を窓に合わせて広げる (L15 の案 b) べき。いったん
    今のまま (代役の大きさで切る)。
  - 印が隠れる窓は、各アプリが右下の角を空ける (案 c。エディタのステータス行、
    LogViewer のスクロールバー。LogViewer の下ボタンが角と重なって押せない件も一緒)。
  - MicroPython のアプリにも三角の印を出す。
  - BASIC の `tile_a.bmp` の 0x01 は色でなく色番号 (0 透明・1 輪郭・2 本体・3 明るい所、
    tool/basic/gen_basic_tiles.rb) で、意図どおり。直さない。
  - デスクトップのアイコンの一覧 (`/home/desktop.toml`): `apps` (表示する、順に) と
    `hidden` (知っているが出さない) を持つ形にする。起動時、既定の一覧にあって両方に
    無いもの (新しく増えた既定) を `apps` の末尾に足して書き戻す。アイコンのメニューで
    外すと `hidden` へ、足すと `hidden` から外して `apps` へ。入っていない Linux の
    アプリは `apps` にあっても出さない。`hidden` の無い今の形のファイルは、最新の
    既定で無条件に上書きする (まだ一般公開していないため)。
  - L15b (2026-10-29 検収済、bcf2ebb8〜2d38a5f5、.deb git1008): 上の決定を実装した。
    大きさを変えられる窓のアプリは右下の 9 px (カーネルの大きさの変更の掴み 10x10 の
    枠の内側。印は 8 px) を空ける (`resize_corner`)。エディタのステータス行、LogViewer の
    スクロールバー (下ボタンが押せる)、Retro の App Store の詳しい欄。MicroPython の窓
    にも印。`/home/desktop.toml` は apps + hidden (試験 test/desktop_icons)。描画の記録
    11 場面と editor_type の画面 3 枚の基準を更新 (c0cd675f)。残件: アイコンのメニューから
    `linux:` の項目を足す手段が無い、Shell の履歴のスクロールバーの下ボタンの最後の
    1 行が窓の大きさによって掴みに入る、doc/desktop_icons/ の文書が apps だけの形のまま。
- L9c の残件: VM での実物 (Qt・GTK のメニューとダイアログ) の確認。
- L9e の残件: VM での確認 (実物の X11 のメニュー、GLES)、代役 (タスクバー) の
  アイコン (デスクトップの Ruby の変更が要る)、`.app.toml` で「画面の大きさの
  全画面」を頼む書き方 (仕様を増やすのでユーザの判断)、ダイアログへのキーは
  モーダルかを見ずに移す、種類を付けない古い X11 のメニュー (xterm) は閉じない、
  矢印の先が窓の内容の左上の角の 1 画素にあると穴にならない。
- L9a・L9b の残件: xdg と X11 の move・resize の頼みの自動の確認、最大化・
  全画面 (X11 の前に出す頼みも無視している)、クリップボード、Linux の窓への
  かな入力、自分で題の帯を描く X11 の窓は帯が二重になる (枠だけの代役を足す
  かは仕様の判断)、GLES と人の手での操作の確認
  (SVG・Breeze のアイコン、Qt の見た目、終える前の制御の口は L9e で直した)。

## 未確定事項

- Cardputer の OS で、DRM から HDMI のモードを変えられるか (L0)。
  変えられなければ、(2) は利用者に設定の変更を頼む形になる。
- APPLaunch から起動したとき、アプリが画面 (LCD の fb) と入力を
  独占できるか。ランチャーが更新を止めるだけで、evdev を開いたままに
  していないか (L0)。
- 切り替えの一覧を開くキーの組 (Cardputer のキーボードで押せるもの、実機で決める、L5b)。
- App Store の審査の基準 (ライセンス、パッケージの大きさ、root で動くこと)。
- (3) の小型 PC (Chuwi MiniBook X を検討中)。
- Cardputer Zero のキーボードの細部 (L0・L5、実機で決める): 記号の層の Shift の
  位置が KEY_KPPLUS で届き、M5Stack のコンソールの表でも割り当てが無い
  (今はテンキーの `+` として届く)。記号の層は他のキーのコードを借りて届くので、
  追加の表は `keymap_device` の装置だけに当てている。
- fbdev・DRM で動くときのテキストのコンソール (VT) との関係、キーボードの
  独占 (EVIOCGRAB)、起動後の抜き差し、タッチパッド (EV_ABS) の扱い (L3)。
