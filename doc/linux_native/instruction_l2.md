# L2 指示書: 受け口の実装と機種の定義 (fbdev・DRM・evdev・ALSA)

> 計画: doc/linux_native/plan.md (先に全部読む)。L1 の結果: report/l1.md

## 目的

L1 の native (SDL) に、Cardputer Zero と Linux PC で使う受け口の実装を
並べ、**機種の定義 (TOML) を書き換えるだけで組み合わせを選べる**ようにする。
あわせて、native を確認の道具 (MCP・回帰) に載せる。

WSL2 には fbdev・DRM の出力口も、evdev の装置も、ALSA の音の出口も無い。
そこで各実装を「装置を開く部分」と「中身の処理」に分け、中身は WSL2 で
試験できるようにする。装置を開く部分はビルドが通るところまでで、
実機での確認は L0・L3 で行う。

## 作業の内容

### 1. 機種の定義

- plan.md の TOML の形 (`[screen]` `[display]` `[input]` `[audio]` `[desktop]`
  `[system]`) を実装する。項目名は L1 の実装に合わせて直してよい
  (直したら report に書く。plan.md への反映は親が行う)。
- 起動の引数で選ぶ (例: `--machine <名前またはパス>`)。名前で選んだ場合は
  配布物の中の定義を読む。L1 で引数・環境変数で渡していた値は、
  定義の値を上書きする形で残す。
- 定義を置く場所と生成の仕方は、system_conf と同じく config/ を正にする。
- 最初に用意する定義:
  - `sdl` (開発用。L1 の既定と同じ動き)
  - `cardputer_zero` (fbdev・evdev・ALSA。画面の形は L4 まで 426x240 の
    縮小表示ではなく、**定義だけ用意して起動時に「320x170 は未対応」と
    はっきり断る**)
  - `cardputer_zero_hdmi` (DRM・evdev・ALSA・426x240)
  - `linux_pc_drm` (DRM・evdev・ALSA・426x240。L7 の前の試し用)
- 定義に書かれた受け口が、そのビルドに入っていないときは起動時に断る
  (黙って SDL に切り替えない)。
- 受け口の選択は 1 か所で行う。上の層に形ごとの `#ifdef` を入れない。

### 2. 画面: 拡大・黒帯・回転の共通部品と fbdev・DRM

- L1 の `native/present/` (拡大・黒帯、XRGB8888 版あり) に**回転 (0/90/180/270)**
  を足し、出力の形式に RGB565 と XRGB8888 の両方を持たせる。倍率は
  「出力の大きさに整数倍で収まる最大」を既定にし、定義で固定もできるようにする。
- fbdev: `/proc/fb` で名前 (例 `fb_st7789v`) から探す指定と、パスの直接の指定の
  両方。`FBIOGET_VSCREENINFO` / `FIX` で形式と行の長さを読み、mmap した
  面に共通部品で描く。
- DRM: dumb buffer で、接続されている出力を探し、定義で指定したモード
  (例 1280x720) か、無ければ推奨のモードを設定する。2 枚の面を交互に使う。
  DRM の主導権 (master) が取れないときは、理由をはっきり出して起動を断る。
- 試験: 「装置から得た面の情報 (幅・高さ・行の長さ・形式・回転)」を受け取って
  描く部分を切り出し、メモリ上の面で画素を比べる host 試験を書く
  (`rake test` に入れる)。

### 3. 入力: evdev と Linux のキーコードの表

- Linux のキーコード (`KEY_*`) → HID の Usage ID の表を 1 枚作る。evdev と、
  将来の wlroots (L8・L9) の両方が使うので、evdev の実装から独立させる。
- evdev: 定義の `devices` で装置を指定 (`auto` は `/dev/input/event*` から
  キーボードとマウスを探す。by-path の指定も可)。キー・相対移動・ボタン・
  ホイールを L1 の入力の表 (`input_source.h`) に流す。抜き差しは L2 では
  起動時の検出だけでよい。
- Cardputer Zero のキーボード: Sym・Fn の層はカーネルのドライバが受け持ち、
  記号の層は独自のキーコードで届く。元は M5Stack が公開している Linux の
  ドライバ集 (m5stack-linux-dtoverlays) の CardputerZero の overlay
  (`cardputerzero-v5-overlay.dts` の `linux,keymap` / `keymap1` / `keymap2`) と、
  コンソール用のキー表 `tca8418_keypad_m5stack_keymap.map`。リポジトリの外
  (scratchpad など) に取得して読み、表を作る (取得物はリポジトリに入れない)。
  入手できなければ止まる。この表を定義の `keymap` で選ぶ追加の表として用意する。
- 試験: `struct input_event` の並びを記録した入力 (ファイルやパイプ) から、
  core に流れる HID の値を比べる host 試験を書く。`/dev/uinput` は root
  専用なので使わない。

### 4. 音: ALSA

- `audio_backend_t` の ALSA 実装。装置名は定義から (既定 `default`)。
  `snd_pcm_writei` で書き、詰まり (underrun) から戻れるようにする。
  協調 port なので、**書き込みで全体を止めない** (待つなら専用のスレッドと
  リングで。L1 の `native_audio` のリングを使う)。
- 試験: ALSA の `file` プラグイン + `null` を使う設定 (専用の asoundrc を
  環境変数 `ALSA_CONFIG_PATH` などで渡す) で、WSL2 でも音の値をファイルに
  書き出して確かめる。音の出るアプリで 0 でない値が出ること。

### 5. 確認の道具に載せる

- MCP サーバ (tools/mcp/) に `native_up` / `native_down` / `native_screenshot`
  / `native_input` / `native_app` を足す。中身は `tools/fmrb_native.rb` を
  呼ぶ形でよい。description は既存の sim_* に合わせ、「sim と同時に
  動かせる」「`--machine` を選べる」ことを書く。tools/mcp/README.md と
  ルートの CLAUDE.md の表に 1 行足す。
- 回帰の道具 (doc/regression/usage.md、tools/regress/) に native を足す:
  `rake regress:screens` で native のヘッドレス起動とデスクトップ・
  エディタの 1 打鍵の画面を、sim・ブラウザ版と同じ扱いで比べる。基準の
  画像は作ってよいが、**作ったことと画像の場所を report に書く**
  (既存の基準は更新しない)。

## 触ってよい範囲

- fmruby-core: `native/`、`hosted/` (必要なら)、`config/` (機種の定義)、
  `rakelib/native.rake`、`test/` と host 試験の置き場所、
  `doc/regression/usage.md` (native の追記)、`doc/linux_native/report/`。
- family-mruby (ルート): `tools/` (fmrb_native.rb・mcp・regress)、
  `CLAUDE.md` の MCP の表 (1 行)。
- 触らないもの: fmruby-graphics-audio、sdkconfig、submodule の中、
  `flash/` の中身、`doc/linux_native/plan.md`、端末と sim の動き。

## 作業場所と git

- 本体の checkout、両リポジトリとも `feature/linux-native` に続けてコミットする
  (規約は fmruby-core/CLAUDE.md、Co-Authored-By を付ける、`git add` は明示)。
- develop へのマージと push はしない。
- `build/` は ESP32-P4 のビルドが置いてある。回帰の道具が退避して戻すので、
  自分で `rake clean_all` はしない。必要になったら report に書いて止まる。

## 受け入れ条件

1. `--machine sdl` で L1 と同じに動く (L1 の受け入れ条件 2・3 が通る)。
2. 4 つの機種の定義が読め、`--machine cardputer_zero_hdmi` などを WSL2 で
   起動すると、装置が無い・主導権が取れないことを**理由つきで**断って
   終わる (固まらない、SDL に逃げない)。`cardputer_zero` は 320x170 が
   未対応であることを言って断る。
3. host 試験 (`rake test` に入る): 共通部品の拡大・黒帯・回転 (RGB565 と
   XRGB8888、4 つの角度)、Linux のキーコードの表、evdev の記録からの変換、
   Cardputer の記号の層の表。
4. ALSA: file プラグインの設定で起動し、音の出るアプリで出力のファイルに
   0 でない値が出る。
5. MCP の `native_*` で起動・画面・入力・アプリの一覧ができる
   (MCP サーバの selftest があれば同じ形で足す)。
6. `rake regress` と `rake regress:screens` が通り、native の画面が
   regress:screens の要約に出る。

## 止まる条件

- `sudo` やパッケージの導入が要るとき (手元には libdrm 2.4.113 の開発用ヘッダと
  linux の uapi ヘッダがある。ALSA の開発用ヘッダもある)。
- Cardputer Zero のキー表の元 (dts の keymap) が手に入らないとき。
- 計画の見立てと食い違う事実が出たとき。
- 端末・sim・ブラウザ版の動き (仕様) が変わる直し方を選ぶ必要が出たとき。
- 止まったら report に状況を書いて返す。

## report (doc/linux_native/report/l2.md) に書くこと

- 機種の定義の項目名 (確定した形) と、置き場所・選び方
- 受け口ごとに「中身の処理」と「装置を開く部分」をどこで分けたか
- 実機で確かめるべき点の一覧 (L0・L3 に渡すもの: fb の形式、DRM の
  モードの設定、evdev の装置の見つけ方、ALSA の装置名)
- 受け入れ条件ごとの結果と、確かめたコマンド・数値
- 回帰の基準として作った画像と場所
- 見立てと違った点、撤回した仮説、踏んだ罠、残件
