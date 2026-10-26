# L1 指示書: Linux ネイティブ版の土台 (x86_64・SDL・ファイルの振り分け)

> 計画: doc/linux_native/plan.md (先に全部読む。特に「方針」「設計」の
> 画面の出力・ファイルの置き場所・既存のコードも整理の対象にする)

## 目的

WSL2 (x86_64) の上で、Family mruby の core を **1 プロセスのネイティブの
Linux プログラム**として動かす。画面・入力・音は SDL2。ブラウザ版 (wasm/) の
構成の兄弟として作り、wasm と共有できるものは共有する。今の Modern の
デスクトップと既存のアプリが動き、ヘッドレスで画面の取得・入力の注入・
アプリの起動ができるところまで。

L2 以降 (fbdev・DRM・evdev・ALSA の受け口、機種の定義の本実装、aarch64) は
**この段ではやらない**。ただし受け口の形は L2 で差し替えられるように作る。

## 前提として読むもの

- doc/wasm/plan.md (設計の章)、doc/wasm/implementation_plan.md
- wasm/CMakeLists.txt、rakelib/wasm.rake、wasm/port/、wasm/backend/、wasm/stub/
- main/drivers/display_local/display_backend.h (画面の出力の受け口)
- main/drivers/audio_p4/audio_backend.h (音の出力の受け口)
- components/fmrb_hal/platform/posix/fmrb_hal_file_posix.c (今の posix の
  file HAL。`BASE_PATH "flash"`)
- fmruby-graphics-audio/sdl2-display/main.c (SDL のキー → HID の変換の実例。
  **読むだけ、graphics-audio は変更しない**)
- tools/fmrb_input.rb、tools/fmrb_screenshot.py、tools/mcp/lib/sim.rb
  (sim の確認の道具の仕組み)
- doc/reference/core_build_structure.md、doc/regression/usage.md

## 作業の内容

### 1. ビルドの土台

- 新しいターゲットは `native/` に置く (CMake を独立に組む。idf.py は使わない)。
- **core の共通ソースの一覧を wasm/CMakeLists.txt から切り出し**、wasm と
  native の両方から使う (一覧を二重に持たない)。置き場所 (例: `cmake/` の
  下) は判断してよい。wasm 固有の除外・追加は wasm 側に残す。
- libmruby は native 用の build_config を足して作る (`lib/add/` の既存の
  family_mruby_linux.rb / family_mruby_wasm.rb を参考に)。**MRB_* の ABI の
  定義を CMake 側と必ず揃える** (ずれると GC で落ちる。
  mruby_abi_defines.cmake の扱いを wasm と同じにする)。
- Ruby のバイトコードと Spinel の生成物は、wasm と同じく既存の生成の道筋を
  使う (wasm:mrb・spinel:gen 相当)。生成を native の CMake に二重に書かない。
- Spinel の実行系は、Linux sim と同じ扱い (ネイティブの ISA なので fiber を
  使える) にする。wasm だけが `SP_NO_MMAN` の側にいる形を保つ。除外の条件が
  `CONFIG_IDF_TARGET_LINUX` に依存していて native で使えないなら、
  「ネイティブの ISA かどうか」で決まる形に直してよい。
- rake のタスクを `rakelib/native.rake` に置く (例: `native:setup`、
  `native:mruby`、`native:core`、`native:run`、`native:clean`)。名前は判断してよい。

### 2. FreeRTOS の協調 port を wasm と共有する

- wasm/port/ の Emscripten 依存は時刻の取得と眠り (`emscripten_get_now` /
  `emscripten_thread_sleep` ほか) に限られる見込み。そこを小さな受け口に寄せ、
  **wasm と native で 1 つの port** にする (`clock_gettime` / `nanosleep`)。
- 自分で自分を剥がす方式 (VM のバイトコード境界で実時間を見る) など、wasm の
  協調化で入れた仕組みが native でも効くようにする (`FMRB_PLATFORM_WASM` で
  分けている所を確認し、「協調 port かどうか」で分けるべき所はそう直す)。
- **SIGALRM を使わない**。SDL と同じプロセスに入れても衝突しないことがこの
  形の要点。

### 3. 受け口の SDL 実装

- 画面: `display_backend_t` の SDL 実装。合成は既存の CPU 合成
  (display_blend_cpu.c / display_backend_cpu.cpp) を使う。窓は整数倍に拡大
  (倍率は起動の引数か設定で、既定は 3)。全画面の高解像度モード
  (426x240 ⇔ 640x360) の切り替えに追従し、余白は黒で塗る。拡大・黒帯の処理は
  L2 で fbdev・DRM と共有するので、SDL 固有の所と分けて書く。
- 入力: SDL のキー・マウス (ホイールを含む) を、wasm/backend/input_wasm.c と
  同じ形で core に流す。SDL の scancode は USB HID の Usage ID とほぼ同じなので、
  sdl2-display の変換を参考にする。
  - 今の入力の差し替えは `usb_task_*` を実装ごと入れ替える形。L2 で evdev を
    並べるので、**画面・音と同じ関数の表の受け口 (入力の取り込み) に直してよい**
    (wasm も同じ受け口に載せ替える)。直すかどうかは判断し、report に書く。
- 音: `audio_backend_t` の SDL 実装。

### 4. ファイルの置き場所の振り分け

plan.md「ファイルの置き場所」の表を実装する (この段は開発中の形)。

- path の解決 (`fmrb_hal_file_resolve_path` の辺り) に**振り分けの表**を
  1 つ置き、一番上の階層の名前ごとに実体の根を決める。場所ごとの `if` を
  散らさない。
- 開発中の実体: 配布物 = リポジトリの `fmruby-core/flash/` (**読むだけ**)、
  利用者 = 既定 `~/.local/share/family-mruby-dev/` (引数か環境変数で変えられる)、
  キャッシュ = `~/.cache/family-mruby-dev/`、`/tmp` = 今の posix の tmpfs。
  `flash_local/` は L1 では重ねない (report に書く)。
- `/etc` は、利用者の側に無ければ初回に配布物から写す。更新で増えた項目を
  足す処理は L3 (パッケージ化) でやるので、この段では不要。
- 配布物の側への書き込み・消去・改名はエラーにする。黙って利用者の側へ
  逃がさない。
- Linux の実ファイルへ素通しする種類を表に用意しておく (中身は L7。この段では
  表に載せない、または無効のまま)。
- **sim (Docker の Linux ビルド) の動きは変えない**: sim は「全部を `flash/`
  に向ける表」として同じ仕組みに乗せてよい。ブラウザ版の MEMFS / IndexedDB の
  扱いも変えない。

### 5. ヘッドレスで確認する口

親とサブが sim と同じ感覚で確認できるようにする。

- **画面の取得**: 合成済みの画面 (RGB565) を PNG で取る口。426x240 /
  640x360 の実寸で、色を落とさない (sim の SHM は RGB332 なので流用しない)。
- **入力の注入**: sim と同じパケット形式 (fmrb_hid_event の
  `[type][len16][payload]`) を Unix DGRAM のソケットで受ける。ソケットの
  場所は sim と別にし (同時に動かしても衝突しない)、`tools/fmrb_input.rb` に
  行き先を変える引数か環境変数を足す。
- **アプリの起動・一覧・kill**: 今の debugd (TCP) を native でも有効にし、
  `fmruby-core/tool/debug/fmrb_dbg_client.py` で使えるようにする。ポートは
  sim の 5555 と別にする。
- 起動・停止・画面の取得・入力・アプリをまとめる道具を **Ruby で**
  `tools/` に置く (例: `tools/fmrb_native.rb up|down|shot|input|app`)。
  ヘッドレスは `SDL_VIDEODRIVER=dummy`、音は `SDL_AUDIODRIVER=disk`
  (または dummy) で動かす。
- MCP の道具 (native_*) を足すのはこの段ではやらない (残件として report へ)。

### 6. 設定

- `config/system_conf_native.toml` を足す (system_conf は config/ から生成する
  決まり。flash/etc/ の生成物を手で書かない)。機種の系統は Modern。
- 機種の定義 (plan.md の TOML) の本実装は L2。この段では、起動の引数か
  環境変数で「SDL・拡大の倍率・利用者の場所」を渡せれば足りる。

## 触ってよい範囲

- fmruby-core: `native/` (新規)、`wasm/` (共有化のための整理)、共有の CMake の
  置き場所 (新規)、`rakelib/native.rake` (新規) と `rakelib/wasm.rake` (共有化に
  伴う追従)、`lib/add/` (native の build_config。**lib/ を触ったら rake clean**)、
  `config/system_conf_native.toml`、`components/fmrb_hal/platform/posix/` と
  `components/fmrb_hal/` の file HAL まわり、`main/` のうち受け口の整理に要る所、
  `doc/linux_native/report/`。
- family-mruby (ルートのリポジトリ): `tools/` (fmrb_input.rb の行き先の追加、
  新しい道具)。
- 触らないもの: fmruby-graphics-audio (読むだけ)、sdkconfig と
  sdkconfig.defaults、submodule の中 (変更が要るなら lib/add・lib/patch・
  lib/replace と Rakefile)、`flash/` の中身、`flash/etc/display_conf_linux.txt`
  (ユーザの未コミットの変更がある)、`doc/linux_native/plan.md` (親が反映する)。

## 作業場所と git

- 本体の checkout で作業する (worktree は作らない)。
- fmruby-core と family-mruby (ルート) のそれぞれで、`develop` から
  `feature/linux-native` を切ってコミットする。コミットの規約は
  fmruby-core/CLAUDE.md (件名 `<領域>: <要約>`、本文は英文、Co-Authored-By を
  付ける)。`git add` は自分のファイルを明示して行う。
- **develop へのマージと push はしない**。
- 今の `build/` は ESP32-P4 (RISC-V) のビルドで残っている。sim の回帰の確認の
  ために `rake clean_all` → `rake build:linux` をしてよい。**`rake clean_all` は
  picoruby のビルドの木ごと消すので、wasm と native の libmruby も消える**。
  作り直しの順番 (build:linux → wasm:mruby → native:mruby など) に注意する。
- 終わったら `file build/fmruby-core.elf` で何のビルドが残っているかを report
  に書く。

## 受け入れ条件

1. `rake native:...` (名前は任意) で x86_64 の実行ファイルができる (`file` で確認)。
2. ヘッドレスで起動し、デスクトップが出る。画面の取得で 426x240 の PNG が
   取れ、sim (Modern) またはブラウザ版のデスクトップと同じ構成に見える。
3. 標準の構成 (Spinel のカーネル + Spinel のエディタ) で、エディタを起動して
   1 打鍵し、その文字が画面に出る。**全部 mruby の構成でも同じことができる**
   (エンジンの検証は標準 + 全 mruby の 2 点)。
4. 全画面の高解像度のアプリ (`/app/test/hires_test.app.rb`) を起動すると、
   画面の取得が 640x360 になり、終えると 426x240 に戻る。
5. 音の出るアプリ (例: NSF Player か APU のデモ) で、音の出力 (SDL の disk の
   出力など) に 0 でない値が出る。
6. ファイル: エディタで `/home` に保存したファイルと、設定の変更 (`/etc`) が
   利用者の側に書かれ、再起動後も残る。`/usr` の下への書き込みがエラーになる。
   起動・操作の前後で **`git -C fmruby-core status --short flash/` が変わらない**。
7. GUI (WSLg) でも窓が出て操作できる (画面の目視はユーザが行うので、起動の
   コマンドを report に書く)。
8. 既存を壊さない: `rake regress` が通る。wasm と sim を触った分、
   **`rake regress:screens` (sim の Retro・Modern とブラウザ版) が通る**。
   基準の更新が要るほどの変化が出たら、更新せずに変化を report に書いて止まる。

## 止まる条件

- `sudo` やパッケージの導入 (apt) が要るとき (手元には gcc 11、cmake 3.31、
  SDL2 2.0.20、libasound2-dev がある。ninja は無い)。
- 計画の見立てと食い違う事実が出たとき (例: 協調 port の native 化が時刻と
  眠りの差し替えでは済まない、CPU 合成が wasm でしか成り立たない前提を持つ)。
- 触ってよい範囲の外 (graphics-audio、sdkconfig、submodule の中) を変える
  必要が出たとき。
- 直し方の候補のうち、利用者から見た動き (仕様) が変わるものを選ぶ必要が
  出たとき (sim・ブラウザ版・端末の動きが変わる、既定値や操作が変わる)。
- 止まったら、状況を report に書いて返す。

## report (doc/linux_native/report/l1.md) に書くこと

- 何をどこに置いたか (ディレクトリ・rake のタスク・道具の使い方)
- 共有化のために wasm・posix・main を変えた所と、その理由
- 入力の受け口を関数の表に直したか、直さなかったならその理由
- 受け入れ条件ごとの結果と、確かめたコマンド・数値 (起動の時間、画面の
  取得の大きさ、音の値、`git status` の結果)
- 見立てと違った点、撤回した仮説、踏んだ罠
- 残件 (MCP の道具、`flash_local/` の扱い、L2 へ送るもの)
- 最後に残っているビルドの種類 (`file build/fmruby-core.elf`)
