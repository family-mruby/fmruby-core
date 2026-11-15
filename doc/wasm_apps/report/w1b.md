# W1b: WAMR を本体に組み込み、TinyGo のアプリを P4-Nano で動かす準備 (2026-11-15)

> 親: plan.md の段 W1b と決定 1〜19、指示: instruction_w1b.md | 2026-11-15 19:48〜20:50 |
> worktree `/home/kishima/fmrb/wt/w1b-core` (branch `wt/w1b`、develop d9cf05a1 から)。
> 途中で 1 回止まった (内蔵 RAM +28 B、2 章)。ユーザが +28 B を受け入れ (パッチは当てない)、続けた。

## 0. 作業場所の用意

- submodule 8 つは本体の checkout から `rsync -a` で写した (`.git` と各 submodule の直下の `/build/` を除く)。
  **注意**: 最初に `--exclude build/` (アンカーなし) で写したら、`picoruby/lib/mruby/build/` (mruby の
  ビルドの道具の Ruby) まで落ちて `rake build:esp32` が止まった。`/build/` で写し直した。
- 無視されたファイル: `.env`・`config/wifi.toml`・`flash_local/`・`hosted/vendor/`・`vendor/`・
  `tools/wamr_poc/vendor/` を写した。`.env` は追跡されているファイルで、worktree では
  `FMRB_HW_TARGET=NARYAv4` に書き換えてある (コミットしていない)。S3 のビルドのときだけ NARYAv3 に戻した。

## 1. 部品の置き方

| 場所 | 中身 |
|---|---|
| `lib/upstream/WAMR_PIN` | WAMR 2.4.5 (タグ `WAMR-2.4.5`、commit 25bd7eb6)。ほかの PIN と同じ形 |
| `rakelib/wamr.rake` | `rake wamr:setup` が `vendor/wamr` に取り込む (`fetch_pinned_checkout`)。`rake setup` から呼ぶので、どのビルドでも用意される。docker はこの木だけをマウントするので、外の checkout を指す上書きは作っていない |
| `components/fmrb_wamr/CMakeLists.txt` | P4 (`esp32p4`) と Linux (`linux`) だけ WAMR をビルドする。ほかの対象では空の部品。WAMR の設定はここで固定: classic インタプリタ、参照型・拡張された定数式 (決定 1)・命令数の上限・bulk memory が有効、AOT・JIT・WASI・libc・スレッド・共有メモリ・複数モジュール・SIMD は無し。対象は P4 が `RISCV32_ILP32F`、Linux が `X86_64`。`invokeNative` を外して止まる関数に替えた (W1a と同じ)。WAMR のソースの警告はエラーにしない。P4 では `-msmall-data-limit=0` (2 章) |
| `components/fmrb_wamr/platform/` | 土台の層 (`platform_internal.h`・`fmrb_wamr_platform.c`・`fmrb_wamr_no_invoke_native.c`・`shared_platform.cmake`)。W1a の試作の層から、FreeRTOS・fmrb_hal・fmrb_mem の上へ移した。P4・sim・ホストのビルドで同じ 1 つ |
| `components/fmrb_wamr/fmrb_wasm.c`・`include/fmrb_wasm.h` | ランタイムの一生と、wasm のアプリを殻に乗せるつなぎ (3 章) |
| `components/fmrb_wamr/fmrb_wamr_psram.lf` | 部品の `.bss` を PSRAM へ (scheme は main の `fmrb_extram_bss`)。`CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` のときだけ |
| `main/CMakeLists.txt` | P4 と Linux のときだけ `fmrb_wamr` を REQUIRES に足す。部品が `FMRB_HAVE_WASM=1` を INTERFACE で出すので、main の側はこの印で分ける |

土台の層の中身:

| 関数 | 実装 |
|---|---|
| `os_malloc`・`os_realloc`・`os_free` | `fmrb_sys_*` (プール方式なので呼ばれない想定) |
| `os_mutex_*` | FreeRTOS の mutex。`korp_mutex` が `StaticSemaphore_t` を中に持ち `xSemaphoreCreateMutexStatic` で作る。FreeRTOS のヒープ (ESP32 では内蔵 RAM) を使わず、錠は WAMR がそれを置いた所 (プールか `.bss`、P4 ではどちらも PSRAM) にある |
| `os_self_thread` | `xTaskGetCurrentTaskHandle()` |
| `os_time_get_boot_us` | `fmrb_hal_time_get_us()` |
| `os_thread_get_stack_boundary` | NULL (境界は exec env ごとに `fmrb_wasm.c` が設定する) |
| `os_mmap`・`os_mremap`・`os_munmap` | wasm のメモリ。**そのアプリのプール** (`fmrb_current()->mem_handle`) から `fmrb_malloc` / `fmrb_realloc`。16 バイトの頭にプールを覚え、解放はそこへ返す |
| `os_cond_*`・`os_mprotect`・キャッシュ | 何もしない |

## 2. メモリの取り方 (決定 5 の結果) と内蔵 RAM

### 2.1 決定 5 は成り立った

- **バイナリの写し**: アプリの枠 (`ctx->mem_handle`、P4 で通常 1 MB) に読み込む。classic インタプリタが書き換えながら使う。
- **wasm のメモリ**: 同じ枠から土台の層の `os_mmap` が切り出す (W1a で別の固定の領域から切り出していた所を、
  アプリの枠に向けた)。最大ページ数は、インスタンスを作る時点の枠の空きから 64 KB を引いた分
  (1 MB の枠なら約 13 ページ)。TinyGo のデモは 2 ページで始まる。
- **共通のプール**: 256 KB (`FMRB_WASM_POOL_SIZE`)。モジュールの解析結果・インスタンス・exec env・演算の
  スタック (16 KB / アプリ) をここから取る。**最初の wasm のアプリの起動時に PSRAM から取り**
  (`heap_caps_malloc(SPIRAM)`、Linux は `malloc`)、その後は持ち続ける (2 度目からは取り直さない。
  断片化を避けるため)。起動時には取らない。
- **ランタイムの一生 (決定 4)**: 最初の wasm のアプリが `wasm_runtime_full_init`、最後の 1 つが
  `wasm_runtime_destroy`。利用者の数は起動時に作る錠 (`fmrb_wasm_init`) で守る。常時動くタスクは無い。
- 境界の関数の表 (`FMRB_GEN_WASM_NATIVES`) は flash の const に置き、初期化のたびに共通のプールへ写してから
  `wasm_runtime_register_natives_raw("fmrb", ...)` で登録する。WAMR が表をその場で並べ替えるので書ける場所が
  要り、イメージの中の書ける表は内蔵 RAM の `.data` になるため。

### 2.2 内蔵 RAM の静的な増分: +28 B (ユーザが受け入れた)

NARYAv4、esp_idf_size の `libfmrb_wamr.a`:

| 区画 | 大きさ |
|---|---|
| Flash (text + rodata) | 89,982 B (text 87,736・rodata 2,246) |
| PSRAM (.bss) | 324 B |
| **DIRAM (.data)** | **28 B** |

- 最初のビルドでは DIRAM が 92 B だった。RISC-V の small data (`.sbss`・`.sdata`) が PSRAM への写像
  (`bss` の scheme) から漏れていたため。部品に `-msmall-data-limit=0` を付けて `.sbss` の 64 B は PSRAM へ移した。
- 残る 28 B は WAMR のソースの初期値つきの static 変数で、ビルドの指定では移せない:

  | 変数 | ファイル | 大きさ | 何か |
  |---|---|---|---|
  | `invokeNative_Void`・`_Int32`・`_Int64`・`_Float32`・`_Float64` | `core/iwasm/common/wasm_runtime_common.c` | 20 B | 型を見て呼び分ける経路の関数ポインタ (`= invokeNative`)。raw の形だけで登録するこちらでは使わない |
  | `externref_global_id` | 同上 | 4 B | 参照型 (決定 1) の externref の番号 (`= 1`) |
  | `log_verbose_level` | `core/shared/utils/bh_log.c` | 4 B | WAMR のログの段 (`= BH_LOG_LEVEL_WARNING`) |

- 3 つとも、初期値なしにして初期化の後に入れる小さなパッチで 0 にできる見込みだった (`externref_global_id` は
  0 のときの入れ直しがソースにすでにある)。**ユーザの判断で、パッチは当てずに +28 B を受け入れた**
  (2026-11-15)。WAMR の版を上げるときに数字を見直す。
- main の側の変更 (`fmrb_app.c`・`fmrb_app_spawner.c`) は static を足していない (コードで確認)。
  同じコミットの WAMR なしの P4 ビルドとの全体の比較は取っていない (8 章の `regress:size` で親が確かめる)。
- **S3 (NARYAv3)**: DIRAM 125,403 B。W3 の report の値 (125,403) と同じで、**+0**。WAMR はリンクされない
  (map に `wasm_runtime` の名前が 0 件)。
- **P4 の全体** (NARYAv4、`.env` のビルドの指定、`regress:size` の固定の指定ではない): 9 章。

## 3. 殻に乗せる所 (殻は変えていない)

**`main/app/fmrb_app_shell.c`・`fmrb_app_shell.h` は 1 行も変えていない。** wasm のつなぎは Lua と同じく
`fmrb_shell_engine_t` を実装した (`components/fmrb_wamr/fmrb_wasm.c`)。W4b-3 で殻の関数の形が変わったら、
このファイルの `WASM_ENGINE` の表と `e_*` を合わせる。

アプリが公開する関数 (この段の仮の形。W4b-3 がフックを sig と生成のヘッダに載せたら、そちらに合わせる):

| 殻のフック | wasm の export | 形 |
|---|---|---|
| LOAD | `_initialize` (あれば) | `() -> ()`。TinyGo・WASI reactor が持つ |
| CREATE | `on_create` | `() -> ()` |
| UPDATE | `on_update` | `() -> i32`。次までの ms。負の数は既定の 330 ms (Lua の数でない返り値と同じ) |
| EVENT | `on_event` | `(kind, x, y, button, keycode, modifier, character, value)` すべて i32 |
| DESTROY・SUSPEND・RESUME・QUIT_REQUEST | `on_destroy`・`on_suspend`・`on_resume`・`on_quit_request` | `() -> ()` |
| RESIZE | `on_resize` | `(w, h) -> ()` |

- `on_event` の `kind` は `fmrb_hid_event_type_t` の値 (key_down 1 … mouse_wheel 10)。種類ごとの意味は
  `fmrb_wasm.c` の頭に書いた (key: keycode・modifier・character、mouse: x・y (区画の座標)・button、
  wheel: value = delta、gamepad: button・value = パッドの番号、axis: button = 軸・value・keycode = パッド、
  kana: value = mode)。`wx`・`wy`・`scancode` は渡していない。
- 起動の前に、公開された関数の引数と返り値の数を表と照合し、違えば理由つきで断る。
- **Window の描き手は番号 0**。殻の `layout` で最初に 1 つ開く (Lua と同じ。アプリの描き手の表はアプリごと
  なので、その時点で開いているものは無く 0 になる。0 でなければ警告をログに出す)。アプリの `fmrb_gfx_*` は
  受け手に 0 を渡す。大きさが変わったときも同じ描き手の原点と区画を入れ直す。
- 1 回の呼び出しの命令数の上限は 2,000 万 (`FMRB_WASM_CALL_INSN_LIMIT`)。トラップか上限で止まったら、
  理由 (`on_update: Exception: instruction limit exceeded` など) を出して終了させる (決定 3)。殻は
  `FMRB_SHELL_RAISED` を受けてエラーの Window (Ruby の例外と同じ口) にする。
- 本体のスタックの境界は手で設定する (WAMR は手で渡した境界に余裕を足さない): P4 は
  `pxTaskGetStackStart(NULL)`、Linux は `pthread_getattr_np`、それぞれ +6 KB。ブラウザ版は未設定 (NULL、
  検査なし)。
- wasm のアプリのタスクのスタックの既定は 24 KB (`FMRB_WASM_APP_TASK_STACK_SIZE`、決定 4)。`.app.toml` の
  `task_stack_kb` が勝つ (下限は今と同じ 16 KB)。

本体の変更:

| 場所 | 変更 |
|---|---|
| `components/fmrb_common/include/fmrb_app.h` | `FMRB_VM_TYPE_WASM` を `MICROPYTHON` の後ろに足した (既存の番号は変わらない。`FmrbApp.ps` の `vm_type` は 5) |
| `main/app/fmrb_app_spawner.c` | `.wasm` を見分ける。`FMRB_HAVE_WASM` の無いビルドでは `wasm apps are not in this build` と出して `FMRB_ERR_NOT_SUPPORTED`。スタックの既定 24 KB |
| `main/app/fmrb_app.c` | 起動時に `fmrb_wasm_init()`。VM を作る段では何もしない (モジュールはアプリのタスクで作る)。実行は `fmrb_wasm_app_run`、失敗はエラーの Window。資源の後片付けの switch に `WASM` を足した |
| `main/ruby/kernel/system_desktop/launcher.rb` | `SCRIPT_EXTS` に `"wasm"`、アイコンの文字 `W` (アイコンの絵は無い) |
| `main/ruby/kernel/system_desktop/file_manager.rb` | `.wasm` を起動できるファイルに |
| `main/ruby/kernel/system_desktop/taskbar.rb` | vm_type 5 の色 (紫 0xA3) |
| `config/storage_exclude_{naryav3,atoms3r,atom_display,sticks3}.txt` | `app/tinygo` を S3 のイメージから外す |

`main/kernel/fmrb_spx_kernel.c` の vm_type の写し (Spinel のカーネル用) は `default` で native (4) になる。
直していない (表示だけ)。

## 4. TinyGo の SDK とデモ

置き場はユーザの決定どおり SDK 専用のリポジトリ `/home/kishima/fmrb/family-mruby/family-mruby-sdk`
(ローカルのコミットだけ、push していない)。Go のモジュールは `github.com/family-mruby/family-mruby-sdk/tinygo`。

| ファイル | 中身 |
|---|---|
| (C のヘッダ) | **写していない**。生成のヘッダは説明文が日本語と英語の 2 行で、SDK のリポジトリはソースを ASCII に保つ決まり (ユーザ、2026-11-15) のため。照合の道具は隣の fmruby-core の `sdk/c/fmrb_boundary.h` を読む (`--header`・`FMRB_HEADER` で変えられる)。英語だけのヘッダを生成する形は W5 で |
| `tinygo/fmrb/raw.go` | 手書きの `//go:wasmimport fmrb <name>` 29 個。FmrbGfx (記録を使わないもの)・FmrbApp の一部 (クリップボード・`uptime_us`)・Log。記録を使う関数 (wallclock・image・file・nsf_status・composite regions) と FmrbAudio はまだ入れていない |
| `tinygo/fmrb/fmrb.go` | 包み: `fmrb.SetApp(v)` と、フックの export (`on_create` ほか 8 つ。アプリが実装した `OnCreate`・`OnUpdate`・`OnEvent` などを呼ぶ)、`fmrb.Window` (描き手 0)・`Gfx` のメソッド・`RGB`・`Log*`・`Itoa` (strconv を避けて小さく) |
| `tinygo/examples/bounce/main.go` | デモ: 区画の中で角丸の四角が跳ね、クリックかキーで色が変わる。2 行の文字 (`TinyGo on Family mruby`、frame の数と最後のキー)。33 ms ごと。作るとき・終わるときにログ |
| `tinygo/build.rb` | `ruby tinygo/build.rb bounce [-o <dir>]`。`~/toolchains/tinygo` と Go、キャッシュは `~/toolchains/cache`。指定は `-target=wasm-unknown -gc=conservative -opt=z -no-debug` (決定 1 の解放する GC) |
| `tools/check_bindings.rb` | 照合: `raw.go` の各 import が生成の C のヘッダにあり、引数と返り値の wasm の型が一致するか。`.wasm` を渡すと、その import (モジュール `fmrb` だけか、ヘッダにあり型が合うか) と公開しているフックも調べる。わざとヘッダの 1 つの型を変えると止まることを確かめた |

- `bounce.app.wasm` は 7,892 B (解放する GC 込み)。import は `gfx_area_width/height`・`gfx_draw_text_raw`・
  `gfx_fill_rect`・`gfx_fill_round_rect`・`gfx_present_raw`・`gfx_rgb_to_332`・`log_info_raw` の 8 つ。
- fmruby-core には `flash/app/tinygo/bounce.app.wasm` と `bounce.app.toml` (180x120 の枠つき、
  `app_screen_name = "TinyGo bounce"`、`language = "tinygo"`) を入れた。ランチャーには `tinygo` の分類で
  出る (アイコンは文字の `W`)。

## 5. C のアプリ (決定 4・5・12)

- `tools/wasm_apps/c_check/app.c` → `flash/app/test/c_check.app.wasm` (1,282 B)。台本は
  `ruby tools/wasm_apps/build.rb` (`zig cc -target wasm32-freestanding`、生成の `sdk/c/fmrb_boundary.h`)。
- 生成のヘッダの関数を直接呼ぶ: 区画を塗る・文字 4 行 (frame・events・起動からの秒・不正なポインタの結果)・
  動く丸・present、`on_event` で数える、`on_create` で**アプリのメモリの外を指すポインタが
  `FMRB_BOUNDARY_ERR_ARG` で断られるか**をログに出す。設定を変える関数 (音量・クリップボード) は呼ばない
  (試作の `c_boundary` の tour はそれらを呼ぶので、端末に置く版は別にした)。

## 6. 確かめたこと (worktree の中)

| 確かめたこと | 結果 |
|---|---|
| `FMRB_HW_TARGET=NARYAv4 rake build:esp32` (`.env` で指定) | 通る (clean_all から)。map に WAMR (`wasm_runtime_full_init`・`fmrb_wasm_app_run`)。新しい警告なし |
| `FMRB_HW_TARGET=NARYAv3 rake build:esp32` (`.env` で指定) | 通る (clean_all から)。WAMR は入らず、storage に `app/tinygo` が無い。DIRAM 125,403 (+0) |
| `rake build:linux` (clean_all から) | 通る。`file build/fmruby-core.elf` は x86-64、WAMR がリンクされている |
| `rake regress` | host 29 suites 全部通過 (60.2 s)、描画の記録 229/229 |
| `rake regress:spinel` | 6 programs、0 failed |
| デモと C のアプリを WAMR で動かす (ホスト) | 試作の本体役 (`tools/wamr_poc/host`、同じ WAMR の設定、境界の関数は生成の stand-in) を worktree の外 (scratchpad) にビルドし、`poc_host boundary <app>` で `bounce.app.wasm` と `c_check.app.wasm` がそれぞれ `_initialize`→`on_create`→`on_update` 100 回を例外なく通った (返り値 33・100 ms)。チェックサムは tour の期待値と違うが、別のアプリなので当然 (比べる意味はない) |
| `check_bindings.rb` | 29 個の宣言とアプリ 2 本の import がヘッダと一致 |

**確かめていないこと (正直に)**:

- **本体の中での動作は一度も見ていない**。sim は使えず (本体の checkout の作業と衝突する)、ネイティブ版・
  ブラウザ版のビルドには WAMR をまだつないでいない (8 章)。殻の上での描画・イベント・枠・終了、
  土台の層の `os_mmap` がアプリの枠から取る所、FreeRTOS の錠、スタックの境界は、P4 で焼いて初めて動く。
  ホストの試験は WAMR の設定・アプリの export と import・TinyGo の `_initialize` を確かめただけで、
  `fmrb_wasm.c` と土台の層は通っていない。
- `regress:screens`・`regress:size`・`regress:run` は流していない (親が取り込んだ後)。

## 7. 親が焼いた後に見るログの行

焼く: worktree の `build/` (NARYAv4、9 章のビルド)。`esptool --chip esp32p4 ... write_flash 0x2000
build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000
build/fmruby-core.bin 0x800000 build/storage.bin` (`build/flash_args`)。storage に `app/tinygo/bounce.app.wasm`・
`.app.toml` と `app/test/c_check.app.wasm`・`.app.toml` が入っている。`rake flash` は /home を消すことに注意
(MEMORY の記録)。

| 時点 | 見る行 (タグ `wasm` は `fmrb_wasm.c`) |
|---|---|
| 起動 | `wasm: WAMR classic interpreter linked (pool 256 KB, taken by the first wasm app)` |
| 起動 (ランチャーまたは `POST /app/launch?path=/app/tinygo/bounce.app.wasm`) | spawner の `Detected wasm app: /app/tinygo/bounce.app.wasm` → `[<名前>] wasm app (mempool=N)` |
| ランタイム | `wasm: runtime started (pool 256 KB at 0x..., 68 boundary functions)` (最初の 1 つだけ) |
| 読み込み | `wasm: [<名前>] loaded /app/tinygo/bounce.app.wasm (7892 bytes, memory 2 pages, up to N) on_create on_update on_event` |
| フック | アプリのログ `bounce: hello from TinyGo (area WxH)` (`on_create`)。画面では四角が跳ね、クリック・キーで色が変わり、frame の数が進む |
| 閉じる (閉じるボタン) | `bounce: bye after N frames` (`on_destroy`) → `wasm: [<名前>] ended (memory 2 pages)` → `wasm: runtime ended (pool high mark X of 262144 bytes)` → `[<名前>] wasm app ended` |
| 失敗したとき | `wasm: [<名前>] <理由>` (読み込みの失敗・フックの形・`on_update: Exception: ...`) とエラーの Window。`invokeNative called` が出たら raw でない登録がある (abort) |
| C のアプリ | `/app/test/c_check.app.wasm`: `c_check: bad pointer refused (ok)`、画面に `bad ptr -2` |

`M1|spawn:<名前>` の内蔵 RAM の行で、アプリの起動ごとの内蔵 RAM の消費 (タスクのスタック 24 KB) も見られる。

## 8. 残ったこと・W5・W6・W2 への注意

- **ネイティブ版とブラウザ版の本体に WAMR をつないでいない** (`hosted/hosted.cmake`)。両方とも
  `FMRB_HAVE_WASM` が無いので、`.wasm` は理由のログを出して断る形でビルドは変わらない (流してはいない)。
  土台の層とつなぎはそのまま使える作り (emscripten のヘッダの手当てと、スタックの境界なし) にしてあるので、
  `hosted.cmake` に WAMR のソースの一覧と `components/fmrb_wamr` を足せばよい見込み。
- **強制の kill**: kill がアプリのタスクを消すと、共通のプールのモジュール・インスタンス・exec env と
  利用者の数が戻らない (wasm のメモリとバイナリの写しはアプリの枠ごと消えるので漏れない)。消された
  タスクが WAMR の錠を持っていた場合、次の wasm のアプリが止まる恐れもある。暴走は命令数の上限で
  先に止まるはず。対処 (例: kill の前に止めの印を立てて呼び出しの合間を待つ、利用者の数の回収) は未着手。
- フックの形 (`on_event` の 8 つの引数など) はこの段の仮。W4b-3 で sig に載ったら、`fmrb_wasm.c` の表と
  SDK の `fmrb.go` を合わせる。
- 本体の checkout の `sdk/c/fmrb_boundary.h` は W4b-3 で変わりつつある (FmrbAudio が受け手の番号を取る形)。
  SDK の照合は隣の checkout のヘッダを読むので、取り込み後に `check_bindings.rb` を流し直す
  (この段では d9cf05a1 の生成と、W4b-3 の作業中の本体の checkout のヘッダの両方で一致した)。
- TinyGo の bindings は手書き (照合つき)。W5 で生成に切り替える。記録を使う関数と FmrbAudio が未。
- `FMRB_WASM_POOL_SIZE` (256 KB)・命令数の上限 (2,000 万)・スタックの余裕 (6 KB)・タスクのスタック
  (24 KB) は仮の値。W2 (P4 での計測) で決める。W1a の見立てでは 1 アプリ 22〜28 KB なので、256 KB で
  8 アプリ程度。
- メモリの上限のページ数 (未確定事項) は、今は枠の空きから決めている (全環境で同じ数にはしていない)。
- C の tour のアプリ (`tools/wamr_poc/apps/c_boundary`) は回帰の道具に入れていない (決定 12 の補足)。
- `app/test/c_check.*` は S3 のイメージにも入る (起動すると理由のログで断る)。
- SDK の `README.ja.md` に TinyGo の節を足していない (英語の `README.md` だけ)。

## 9. 数字と終わり (P4 の最終のビルド)

worktree の `build/` に残した NARYAv4 のビルド (`.env` の指定、clean_all から、コミット f3517f57 の内容):

| 項目 | 値 |
|---|---|
| DIRAM (内蔵 RAM の静的な使用) | 124,876 B (うち WAMR の部品 28 B、2.2 章) |
| HP core RAM・LP RAM | 2,996 B・56 B |
| Flash (イメージの中身) | 7,478,156 B (うち WAMR の部品 89,990 B) |
| `fmruby-core.bin` | 7,581,136 B (0x73add0)。app の区画 0x7f0000 の 9% (742,960 B) が空き |
| storage | `app/tinygo/bounce.app.{wasm,toml}`、`app/test/c_check.app.{wasm,toml}` が入っている (`build/storage_root`) |
| アプリ | `bounce.app.wasm` 7,892 B・`c_check.app.wasm` 1,282 B |

- DIRAM の全体は `regress:size` の固定の指定のビルドではないので、基準の値とは比べていない (親が流す)。
- 焼く引数は `build/flash_args` (0x2000 bootloader、0x8000 区画の表、0x10000 アプリ、0x800000 storage)。
- 裏の処理は残していない。sim・docker の fmruby_* コンテナ・シリアルには触れていない。

### コミット

`wt/w1b` (fmruby-core):

- fd48407b feat(wamr): build pinned WAMR 2.4.5 into the P4 and Linux firmware
- f68c3860 feat(app): run .app.wasm apps on the app shell on the P4 and Linux
- f3517f57 feat(apps): a TinyGo bounce demo and a C check app as wasm apps
- この report (docs)

family-mruby-sdk (main、ローカルだけ、push していない):

- 90d665b feat(tinygo): first bindings, the bounce example and the build and check scripts
