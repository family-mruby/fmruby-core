# Linux 機で動く Family mruby (Cardputer Zero・HDMI・小型 PC のコンポジタ)

> 状態: 進行中 | 更新: 2026-10-26 | L1・L2・L2n・L3a 完了 (x86_64・SDL で起動、hosted/ に wasm と共有の土台、ファイルの振り分け、機種の定義と fbdev・DRM・evdev・ALSA、MCP の native_* と回帰の道具、ネットワークと Zenoh)。arm64・amd64 の .deb を arm64 のコンテナで確認済。次は L4 (キーボード専用のデスクトップ)、実機が来たら L3b。Linux の上で動く Family mruby を 3 つの形で作る: (1) Cardputer Zero 単体 (320x170・キーボードだけ)、(2) Cardputer Zero の HDMI 出力 (USB のマウス・キーボード)、(3) Linux PC のコンポジタ (GPD の UMPC)。(1)(2) は公式の App Store で配る。1 つの土台と受け口の組み合わせで作り分け、形ごとの独自開発をしない

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
form = "426x240"          # 320x170 は L4 まで断る
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
exit_key = "esc_long"     # L2 では読むだけ (未実装)
```

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
  キーボードで押せる組を実機で決める (L5)。
- 最初に対応させるアプリ (2026-10-26 ユーザ決定): **エディタ・Shell・
  File Manager**。

### 全画面アプリの切り替え

- 今は全画面のアプリを退避できる枠が 1 つしかない (`@parked_fullscreen_pid`、
  Ctrl+Tab)。これを**全画面のアプリの一覧**に広げ、キーで巡回する。
  keys のデスクトップではこれが通常の操作になる。mouse でも使える
  (Modern の全画面エディタ 2 つの行き来など)。
- 一覧を開くキーは設定で変えられるようにし、Cardputer のキーボードで押せる組を実機で決める (L5)。

### アプリの宣言

- `.app.toml` に、対応する画面の形 (`screens`) と、マウスが要るか
  (`needs_pointer`) を宣言する欄を足す。
- keys のメニューには、320x170 に対応し、マウスの要らないアプリだけを出す。
  宣言の無いアプリは出さない (縮小して無理に出すことはしない)。
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

### コンポジタと外の窓 ((3) だけ)

- wlroots を土台にする。出発点は付属の最小の例 (tinywl)。
- **解像度を混ぜて描く**: Family mruby の画面 (426x240 の合成済みの絵) を
  整数倍に拡大して敷き、Linux のアプリの窓は液晶の本来の解像度のまま重ねる。
  窓の位置と大きさは、Family mruby の座標を倍率で写した場所に合わせる。
  窓枠・メニューバーは Family mruby の粗い画素、中身は精細になる。
- **外の窓を kernel に知らせる**: コンポジタは Linux のアプリの窓ができた・
  消えた・大きさが変わった、を kernel に送る。kernel の window manager
  (window_manager.rb) は、それを「中身を Family mruby が描かない窓」として
  扱い、配置・前後関係・フォーカスを今の仕組みで決める。Family mruby は
  その窓の場所を透過で空けて合成し、コンポジタがそこに Linux の窓を描く。
- 入力: input_router.rb がフォーカスを持つ窓を決め、外の窓ならコンポジタに
  返し、コンポジタが wlroots の seat 経由で Linux のアプリへ配る。
- 動作の流れ: wlroots の処理の流れ (`wl_event_loop`) と FreeRTOS の協調の
  スレッド群は別々に走らせ、受け渡しの口を 2 つ (画面の絵・窓の通知と入力)
  だけにする。
- X11 のアプリは Xwayland で動かす。ポップアップ・メニュー・クリップボードは
  段階的に広げる。

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
  Family mruby を終えるときに、起動した Linux のアプリをどうするか
  (一緒に閉じる・残す) は L9 で決める。
- GNOME の上の SDL の窓で動かす途中段階でも、起動の口は使える (窓は
  Family mruby の外、GNOME の普通の窓として出る)。cage の全画面の段階では
  見えないので使わない。

**2. Shell から Linux のコマンドを使う (全部の形、L10)**
- Family mruby の Shell に、Linux の端末 (pty) につないだ実行の口を足す。
  `ls` や `git` の出力を Shell の中に出し、キー入力を渡す。窓を抱える必要が
  ないので、コンポジタより前にできる。Cardputer も Linux 機なので (1)(2) でも
  使える。
- 端末の制御の文字 (色・カーソルの移動) をどこまで解釈するかは L10 で決める。
  まずは行単位の出力と、色の基本だけ。

**3. Linux のファイルを Linux のアプリで開く ((3)、L11)**
- Linux のファイルを見せる仕組み (L7) と組み合わせ、Family mruby の
  ファイル管理から Linux のファイルを選んで、Linux のアプリで開く。
- 拡張子ごとの関連付けは、既存の `/etc/associations.toml` の仕組みに
  「Linux のアプリ」という行き先を足す形にする。Linux 側の既定のアプリ
  (`xdg-open` と MIME の関連付け) に任せる行き先も選べるようにする。

**権限**: Linux のプログラムを起動する・Linux のコマンドを打つことは、
PC の中身全部に手が届くことを意味する。どの Family mruby のアプリに許すかは
「Linux のファイルを見せる範囲」と同じ種類の判断なので、L7 で一緒に決める
(案: システムの側 = デスクトップ・ランチャー・Shell だけに許し、ほかのアプリは
利用者が設定で許したものだけ)。(1)(2) は APPLaunch が root で動くので、
特に絞る。

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
| Linux のファイル (後述) | Linux の実ファイル | 最終的に必須 |

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
  (2026-10-26 ユーザ)。振り分けの表に「Linux の実ファイルへ素通しする」種類を
  用意しておき、どこに何を見せるか (`/mnt/host` に利用者のホームを、など) と
  書き込みを許す範囲は L7 で決める。(1)(2) では APPLaunch が root で動くので、
  見せる範囲は特に絞る。

### 配布

- (1)(2): arm64 の `.deb`。`/usr/share/APPLaunch/` の下に実行ファイル・
  アイコン・`.desktop` を 2 つ (単体用・HDMI 用) 置く。App Store の決まり
  (アイコン必須、320x170 ちょうどの画面写真 4 枚) に合わせる。配布物は
  `/usr/share/family-mruby/root/` に入れ、利用者のファイルは上の振り分けで
  利用者の側に置く (パッケージの更新で消さない)。
- (3): amd64 の `.deb`。`/usr/share/wayland-sessions/` にログイン画面の項目を置く。
  開発中は SDL の窓で起動する形も残す。

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
| L3b | **(2) の形を実機で**: Cardputer Zero (または Pi Zero 2 W) で DRM で HDMI に Modern、USB のマウス・キーボード、APPLaunch からの起動と終了の決まり、L2 の実機の確認の一覧 | (2) |
| L4 | **キーボード専用のデスクトップ**: デスクトップの共通部分の切り出し、keys のデスクトップ、320x170 の画面の形、全画面アプリの一覧と巡回、`.app.toml` の宣言 | (1) |
| L5 | **(1) の形**: fbdev で本体の LCD、本体のキーボードの表、対応アプリの最初の組、App Store の準備 (アイコン・画面写真) | (1) |
| L6 | **OS のサービス**: `fmrb_platform_sys` と linux 実装、デスクトップの表示と電源の操作をつなぎ替える | 全部 |
| L7 | **(3) の入口**: GPD でログイン画面から選べる項目 (まず cage + SDL で全画面)、拡大・回転の確認 | (3) |
| L8 | **コンポジタの試作**: tinywl に Family mruby の画面を 1 枚の絵として敷き、その上に端末を 1 つ出す。二つの動作の流れの受け渡しを確かめる | (3) |
| L9 | **コンポジタの本実装**: 外の窓の通知を kernel に入れ、window manager と input router で扱う。解像度を混ぜた描画、Xwayland。Linux のアプリの起動 (起動の口、XDG の `.desktop` の一覧をランチャーに並べる、終了の扱い) | (3) |
| L10 | **Shell から Linux のコマンド**: pty につないだ実行の口、出力と入力の受け渡し、色の基本。L1 の後ならいつでもでき、L9 を待たない (L6 の後を目安) | 全部 |
| L11 | **Linux のファイルを Linux のアプリで開く**: L7 のファイルを見せる仕組みと L9 の起動の口を使い、`associations.toml` に Linux のアプリの行き先を足す | (3) |

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

## 未確定事項

- 機種の定義の項目名と、引数での選び方 (L2)。
- Cardputer の OS で、DRM から HDMI のモードを変えられるか (L0)。
  変えられなければ、(2) は利用者に設定の変更を頼む形になる。
- APPLaunch から起動したとき、アプリが画面 (LCD の fb) と入力を
  独占できるか。ランチャーが更新を止めるだけで、evdev を開いたままに
  していないか (L0)。
- 切り替えの一覧を開くキーの組 (Cardputer のキーボードで押せるもの、実機で決める、L5)。
- App Store の審査の基準 (ライセンス、パッケージの大きさ、root で動くこと)。
- (3) の Linux の配布 (GPD に入れる OS) と、wlroots の版。
- Linux のプログラムの起動・Linux のコマンドの実行を、どの Family mruby の
  アプリに許すか (権限。L7 で、Linux のファイルを見せる範囲と一緒に決める)。
- Family mruby を終えるとき、起動した Linux のアプリを一緒に閉じるか残すか (L9)。
- Shell で端末の制御の文字をどこまで解釈するか (L10)。
- Cardputer Zero のキーボードの細部 (L0・L5、実機で決める): 記号の層の Shift の
  位置が KEY_KPPLUS で届き、M5Stack のコンソールの表でも割り当てが無い
  (今はテンキーの `+` として届く)。記号の層は他のキーのコードを借りて届くので、
  追加の表は `keymap_device` の装置だけに当てている。
- fbdev・DRM で動くときのテキストのコンソール (VT) との関係、キーボードの
  独占 (EVIOCGRAB)、起動後の抜き差し、タッチパッド (EV_ABS) の扱い (L3・L7)。
- パッケージ版の debugd (TCP 5565、認証なし) の扱い、配布する system_conf の値
  (今は開発用の debug_mode などのまま)。L3b でネットワークにつなぐ前に決める。
- コンポジタの合成で、外の窓をどの前後関係まで Family mruby の窓と混ぜられるか
  (Family mruby の窓が外の窓の上に重なる場合の扱い、L9)。
