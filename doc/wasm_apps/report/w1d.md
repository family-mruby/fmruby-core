# W1d: Retro (ESP32-S3) でも wasm のアプリ (2026-11-16)

> 親: plan.md の決定 24 | 本体の checkout (develop) | 開始 16:40 頃、終了 17:55 頃 (`date`)。
> 実機 (NARYAv3) には焼いていない (焼くのはユーザ。6 章)。

## 0. まとめ

- WAMR (classic インタプリタ、決定 1 と同じ機能の組) を S3 でもビルドし、S3 の全部のファーム
  (NARYAv3・StickS3・AtomS3R) が `FMRB_HAVE_WASM` を持つようにした。最初のビルドで通った。
- **内蔵 RAM の静的な増分は、P4 で受け入れた WAMR の初期値つきの static 28 B だけ** (DIRAM は境界の
  揃えを含めて +32 B、3 機種とも)。最初のビルドで見つかった +136 B (FreeRTOS の関数 1 つと
  newlib の atomic 1 つが IRAM に入った分) は消した。その直しで **P4 も DIRAM が 56 B 減った**。
- 部品の `.bss` (584 B) は S3 でも PSRAM に載った (`fmrb_wamr_psram.lf` がそのまま効く)。
- flash は S3 の各機種で約 +98 KB。app の区画の空きは NARYAv3 10% (608 KB)、StickS3・AtomS3R 6%
  (約 330〜340 KB)。
- NARYAv3 の storage に `app/tinygo` (bounce・hello・tour、39 KB) を入れた。StickS3・AtomS3R は
  ほかのデスクトップのアプリと一緒に外したまま。
- wasm のアプリのタスクのスタックは **内蔵 RAM の 24 KB のまま** (変えていない)。S3 で PSRAM に置くのは
  勧めない (3 章)。静的な見積もりでは最深で 8 KB 前後で、16 KB に下げられる見込み (ユーザ判断、7 章)。
- sim の Retro (320x240) で bounce と tour の描画・クリック・キー・`q` での終了を確かめた。
- `rake regress` 通過、`rake regress:size ALL=1` 通過 (5 機種)。build/ に NARYAv3 のファームを残した。

## 1. 変えたもの

| 場所 | 変更 |
|---|---|
| `components/fmrb_wamr/CMakeLists.txt` | 対象に `esp32s3` を足した。`WAMR_BUILD_TARGET` は S3 で `XTENSA`。`-msmall-data-limit=0` は P4 だけに (Xtensa に small data は無い)。`fmrb_wamr_flash.lf` を ESP32 で読む |
| `components/fmrb_wamr/fmrb_wamr_flash.lf` (新規) | `libfreertos.a` の `tasks:pxTaskGetStackStart` を flash へ。`FREERTOS_PLACE_FUNCTIONS_INTO_FLASH = n` のときだけ (2.2) |
| `components/fmrb_wamr/fmrb_wasm.c` | トラップの数え方を atomic から実行時の錠の下の `++` に (2.2) |
| `main/CMakeLists.txt` | S3 でも `fmrb_wamr` を REQUIRES に |
| `config/storage_exclude_naryav3.txt` | `app/tinygo` の行を消した (NARYAv3 のイメージに入る) |
| `config/storage_exclude_{atoms3r,sticks3,atom_display}.txt` | 行は残し、理由を直した (「S3 に WAMR は無い」は偽になったため) |
| コメント | `fmrb_wasm.h`・`fmrb_app.h`・`platform_internal.h`・`hosted/wamr/CMakeLists.txt`・`rakelib/{setup,wamr}.rake`・`tools/regress/sim_run.rb` の「P4 と Linux だけ」を直した |

WAMR の Xtensa への対応は WAMR のビルドの仕組みにある (`config_common.cmake` の `XTENSA`)。非整列の
読み込みができないので `config.h` が `WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS=0` にし、インタプリタは
バイトコードをバイトごとに読む (速さは P4 より落ちる見込み。測っていない)。`invokeNative` は今までの
とおり外して止まる関数に替えてある (`invokeNative_general.c` が対象で、正規表現で外れる)。境界の
検査はソフトウェア (土台の層がハードウェアの境界の検査を定義しない)。土台の層・`fmrb_wasm.c` に
S3 専用の分岐は要らなかった (スタックの境界は P4 と同じ `pxTaskGetStackStart`)。

## 2. 内蔵 RAM

### 2.1 数字 (regress:size、同じコミットの変更前のビルドとの差)

変更前 = HEAD 57983fc のまま `rake regress:size ONLY=narya_v3,sticks3,atoms3r`、変更後 =
コミット b6c14e44 で `rake regress:size ALL=1`。

| 機種 | DIRAM 前 | DIRAM 後 | 差 | 増えたアーカイブ | IRAM | app のイメージの差 | app の区画の空き |
|---|---|---|---|---|---|---|---|
| NARYAv3 | 125,395 | 125,427 | **+32** | `libfmrb_wamr.a` 28 B だけ | 16,384 (+0) | +97,808 | 608,544 B (10%) |
| StickS3 | 114,463 | 114,495 | **+32** | 同じ | +0 | +97,968 | 329,056 B (6%) |
| AtomS3R | 114,099 | 114,131 | **+32** | 同じ | +0 | +98,016 | 343,600 B (6%) |
| TAB5 | 129,394 (2.2 の直しの前) | 129,338 | **-56** | — | — | +32 | — |
| NARYAv4 | 124,876 (report/w1b_2.md) | 124,820 | **-56** | — | — | — | — |

- 28 B は P4 で受け入れたものと同じ WAMR の初期値つきの static (report/w1b.md 2.2:
  `invokeNative_*` 20 B・`externref_global_id`・`log_verbose_level`)。残りの 4 B はアーカイブには
  出ない区画の揃え。
- `libfmrb_wamr.a` (NARYAv3): flash 84,603 B (text 80,877・rodata 3,726)、`.ext_ram.bss` 584 B
  (PSRAM)、`.data` 28 B。
- 基準 (`test/regress/size_baseline.json`、11-07 の 4e7e7e61) との比較は「ok」(内蔵 RAM はどの機種も
  基準より少ない)。基準は更新していない。

### 2.2 最初のビルドで見つかった +136 B と直し

最初のビルド (S3 の対象を足しただけ) は DIRAM +164 B (NARYAv3)・+68 B (StickS3・AtomS3R) で、
regress:size が FAIL を出した。WAMR の 28 B のほかに 2 つ:

| 増えたもの | 大きさ | 原因 | 直し |
|---|---|---|---|
| `libfreertos.a` +36 B | `pxTaskGetStackStart` | `fmrb_wasm.c` がスタックの境界を決めるのに呼ぶ。S3 は FreeRTOS を IRAM に置く設定で、ほかに呼ぶものが無いので新たにリンクされ IRAM に入った | `fmrb_wamr_flash.lf` で flash へ。ESP-IDF 自身が `FREERTOS_PLACE_FUNCTIONS_INTO_FLASH` のときに flash に置く関数 (`freertos/linker_common.lf`。ISR からは呼ばれない) なので、置き場として ESP-IDF が想定している範囲。その設定が y の機種では ESP-IDF の写像に任せ、こちらの写像は条件で消える |
| `libnewlib.a` +95 B (NARYAv3 だけ) | `__atomic_fetch_add_4` | トラップの数 `s_traps` を `__atomic_fetch_add` で数えていた。`s_traps` は PSRAM にあり、S3 では PSRAM の atomic は newlib の stdatomic の関数呼び出しになる (ESP-IDF はそれを IRAM に置く)。P4 (RISC-V) では命令になるので出ていなかった | 実行時の錠 (`s_lock`) の下で `++`。読む側 (`fmrb_wasm_stats`) も錠の中で読む。トラップは稀で、その経路はもともとログを出すので、錠の待ちは問題にならない |

P4 の -56 B は `pxTaskGetStackStart` (P4 でも IRAM にあった。literal と揃えを含めて 56 B) が flash に
移った分。W1b で受け入れた「+28 B」は、実際には P4 でもこの分が上乗せされていたことになる (W1b の
`regress:size` は回線で走らなかった)。今は P4 も S3 も WAMR の 28 B だけ。

`.sbss` の問題 (P4 で `-msmall-data-limit=0` を足した理由) は Xtensa には無い。`fmrb_wamr_psram.lf`
は S3 の `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y` (NARYAv3・AtomS3R・StickS3 の
sdkconfig.defaults にある) でそのまま効き、`.bss` は全部 PSRAM に入った。

## 3. wasm のアプリのタスクのスタック (24 KB) を S3 でどうするか

### 3.1 PSRAM に置けるか: 仕組みはあるが、勧めない

- 既存の仕組みは `FMRB_TASK_FLAG_PSRAM` (`fmrb_task_create_ex` が `xTaskCreateWithCaps(...,
  MALLOC_CAP_SPIRAM)`)。sdkconfig の変更は要らない (ESP-IDF 5.5 の `xTaskCreateWithCaps` は
  `SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY` を見ない。今の使い手は漢字変換と Wi-Fi のマネージャの 2 つ)。
  ファイルは `fmrb_hal_file_esp32.c` が「スタックが PSRAM なら hw_proxy に代わってもらう」形になって
  いる。
- しかし **アプリのタスク (Ruby のアプリを含む) は 2026-05-09 に S3 で PSRAM のスタックをやめている**
  (doc/reference/internal_ram_budget.md「経緯」: ファイルだけでなくメッセージの通信など DMA が関わる
  所で落ちた)。
- wasm のアプリのタスクが通る経路は Ruby のアプリと同じ境界の関数 (`main/app/fmrb_boundary_*.c`) で、
  その中には **アプリのタスクから直接 graphics-audio への転送を呼ぶもの** がある
  (`fmrb_bnd_gfx_create_image`・`create_mask` の `fmrb_transport_send_sync`/`fmrb_transport_send`。
  SPI の転送をアプリのタスクで行う)。撤退した理由の経路そのもので、hw_proxy はファイルしか代わらない。
- よって **S3 で wasm のアプリのタスクだけ PSRAM のスタックにするのは、撤退の判断を覆すことになる**。
  コードは変えず、24 KB の内蔵 RAM のまま。やるなら P4 から (同じ文書の勧め) で、境界の関数のうち
  転送を直接呼ぶものを host のタスクへ回すのが前提。ユーザ判断 (7 章)。

### 3.2 小さくできるか: 静的な見積もりで最深 8 KB 前後

sim の高水位は使えない (sim のタスクは pthread の大きなスタックで動き、ログの `native stack room` は
184,296 B。regress:run の基準の `stack_free` もこのため実機の値にならない)。そこで NARYAv3 のビルドの
compile_commands から、関わるファイル 21 個を Xtensa の gcc で `-fstack-usage -fcallgraph-info=su` を
付けてコンパイルし直し、フレームの大きさを足した (ビルドの成果物は変えていない。scratchpad で)。

| 段 | フレーム (B) |
|---|---|
| `app_task_main` | 624 |
| `fmrb_wasm_app_run` | 624 |
| `fmrb_app_shell_run` | 272 |
| `wait_and_dispatch` | 624 |
| 殻の `call_hook` → wasm の `call_hook` | 32 + 48 |
| `wasm_runtime_call_wasm` (+ `_a`) | 80 + 160 |
| `wasm_interp_call_wasm` | 176 |
| `wasm_interp_call_func_bytecode` (classic の本体) | 416 |
| `wasm_interp_call_func_native` → `wasm_runtime_invoke_native_raw` | 192 + 208 |
| **境界の関数を呼ぶ所までの計** | **約 3.5 KB** |
| 生成のつなぎ (`fmrb_gen_wasm_*`) | 最大 64 |
| 境界の関数の中 (このファイルの中で辿れる最深) | 最大 1.4 KB (`clear_user_area` → 枠の描き直し → `fmrb_msg_send`)。`gfx_transfer_file_raw` は単体で 1,056 |
| その下 (FreeRTOS の queue、ログの vsnprintf、littlefs) | 測っていない。合わせて 2〜3 KB と見る |

- classic インタプリタは wasm から wasm の呼び出しで C の再帰をしない (フレームは演算のスタック 16 KB の
  側)。C のスタックが深くなるのは境界の関数の中だけ。
- WAMR はインタプリタに入る所で「今のスタックが境界 (スタックの下端 + 6 KB) より上か」を見るので、
  必要な大きさは「インタプリタに入る所までの深さ (約 3.5 KB) + 6 KB の余裕」 ≒ 9.5 KB が下限、
  6 KB の余裕で境界の関数の中 (上の見積もりで 4〜5 KB) を受ける。
- 見積もり: 最深 8 KB 前後。**16 KB (ユーザのアプリの下限と同じ) で約 2 倍の余裕**。24 KB → 16 KB で
  アプリ 1 つあたり内蔵 RAM 8 KB が浮く。実機の高水位 (10 秒ごとの `fmrb_task:` の Free 列) で確かめて
  から決めるのがよい (7 章)。変えるなら `FMRB_WASM_APP_TASK_STACK_SIZE` を S3 だけ 16 KB に。

## 4. Retro の画面 (graphics-audio 経由) は変更不要

読んで確かめた: wasm の生成のつなぎ (`fmrb_boundary_wasm.inc`) は境界の関数 (`fmrb_bnd_*`) を呼ぶだけで、
境界の関数は Ruby・Lua・MicroPython と共通 (`main/app/fmrb_boundary_*.c`)。描画は
`fmrb_gfx_submit_from` → host のタスク → graphics-audio (S3 では SPI の先の WROVER) と、Ruby のアプリと
同じ道を通る。機種で分かれる所は境界の関数の中に既にある (例: Retro では `play_wav` は 0 を返す)。
wasm の側に Retro 用の変更は要らなかった。sim の Retro で bounce・tour が描けた (5 章) のはこの道。

## 5. 確かめたこと

| 確かめたこと | 結果 |
|---|---|
| `FMRB_HW_TARGET=NARYAv3 rake build:esp32` (clean_all から) | 通った (最初のビルドで)。`file build/fmruby-core.elf` は Xtensa |
| StickS3・AtomS3R・TAB5・NARYAv4 のビルド | `regress:size ALL=1` の中で通った (ATOM_DISPLAY は中断中でビルドしない) |
| `rake regress:size ALL=1` | ok (5 機種、15.2 分) |
| `rake build:linux` (clean_all から) → `rake regress` | host 31 suites 全部通過、描画の記録 233/233 |
| sim の Retro (320x240、`.env` は NARYAv3) | bounce と tour を `sim_app` で同時に起動。tour: ページの切り替え (クリック・`3` キー)・画像のページ・`q` で終了。bounce: 描画・クリック・`q` で終了。ログ `loaded ... (9356 bytes, memory 2 pages, up to 22, shared pool used 30576 ...)`・tour は `shared pool used 61136`・`runtime ended (pool high mark 61136 of 261608 bytes)`。sim は片付けた |
| NARYAv3 の storage | `build/storage_root` 2,379,710 B (区画 5 MB)。`app/tinygo` 39,366 B が入っている |

- `up to 22` ページ: Retro のアプリの枠 (500 KB、doc/reference/app_memory.md) からバイナリの写しなどを
  引いた残り。P4 (1 MB の枠) より小さいが、bounce・tour は 2 ページで動く。
- 流していないもの: `regress:screens`・`regress:run` (重い。親が段の終わりにまとめて流す前提)。
  **`regress:run` の Retro の場面に TinyGo hello が入るようになる** (場面はアプリが storage にあるか
  `targets.rb` の `carries?` で決まり、NARYAv3 が `app/tinygo` を持つようになったため)。Retro の
  `running_langs` に新しい行が出るので、基準の更新は人が確かめてから。画面 (screens) は sim が
  もともと全部のアプリを持つので変わらない見込み。

## 6. 実機 (NARYAv3) での確認の手順 (ユーザ向け)

build/ にコミット b6c14e44 の NARYAv3 のファームがある (Spinel のカーネルとエディタ、Asterism 入り。
`.env` のとおり)。区画表は変えていない。storage が変わった (`app/tinygo` が増えた) ので、どれか:

```
# /home を残す、WiFi で storage の差分を送る (区画表とフォントの照合つき)
cd fmruby-core && rake flash:app_sync HOST=<NARYAv3 の IP>
# シリアルだけで /home を残して全体を書く
cd fmruby-core && rake flash:keep_home_serial
# /home を消してよいなら
cd fmruby-core && rake flash
```

(NARYAv3 の USB シリアルは CH340 `1a86:7523`。`.env` の `VIDPID` は今 Tab5・P4-Nano のものなので、
`rake attach` を使うなら `VIDPID=1a86:7523` を付けるか `.env` を戻す。)

見るもの:

1. 起動ログ: `WAMR classic interpreter linked (pool 256 KB, taken by the first wasm app)`、
   `Guru|abort` が 0、`M1|` の内蔵 RAM が前のファームと同じ程度 (静的には +32 B)。
2. ランチャーの `tinygo` の分類に TinyGo bounce・hello・tour。bounce を起動して
   `wasm: [TinyGo bounce] loaded ... native stack room N` の N (24 KB のスタックで、境界まで残っている量。
   P4 の値と比べる)。
3. 描画 (四角が跳ねる)・クリックとキーで色が変わる・`q` で終わる・閉じるボタン・もう一度起動。
   tour の 5 ページ (クリックか 1〜5 のキー)。
4. 動かしたまま 10 秒以上待ち、周期ダンプの `fmrb_task:` で wasm のアプリの行の Free (高水位)。
   **これで 3.2 の「16 KB に下げられるか」が決まる** (24 KB − Free が実際の使用量)。
5. `M1|spawn:<名前>` の内蔵 RAM の行 (起動ごとに 24 KB + 数 KB 減る)。
6. 速さ: bounce の動きが目で見て滑らかか (Xtensa は非整列の読み込みができず、インタプリタは
   P4 より遅い見込み。数字は測っていない)。

## 7. ユーザの判断が要るもの (止めずに記録した)

1. **wasm のアプリのタスクのスタックを S3 で 16 KB にするか** (3.2)。勧め: 6 章の 4 で実機の高水位を
   見てから。
2. **S3 で PSRAM のスタックを wasm のアプリに使うか** (3.1)。勧め: 使わない (撤退の経緯と、アプリの
   タスクが SPI の転送を直接呼ぶ境界の関数があるため)。やるなら P4 から。
3. **StickS3・AtomS3R (ヘッドレス) に WAMR を入れたままにするか**。今は S3 の全部に入る (flash +98 KB、
   内蔵 RAM +32 B)。アプリの区画の空きは 6% に減った (約 330 KB)。wasm のアプリは Window を開くので、
   この 2 機種では今は使い道が無い。外すなら `FMRB_WASM` のようなビルドの指定を足し、
   `MINI_BUILD_DEFAULTS` で 0 にする形 (今のフォント・FFT と同じやり方)。
4. ATOM_DISPLAY (中断中) は `app/tinygo` を外したまま (再開するときに決める)。

## 8. 終わり

- コミット (fmruby-core、develop、push していない):
  - b6c14e44 feat(wasm): run .app.wasm apps on the ESP32-S3 (Retro and the headless S3 boards)
  - この report (docs)
- 既存の未コミットの変更 (`components/esp_littlefs`・`components/picoruby-esp32/picoruby` の
  submodule、`wasm/vendor/`) には触れていない。sdkconfig は変えていない。
- build/ に NARYAv3 の ESP32 のファームを残した (regress:size は build/ を退避して戻す)。sim は止めた。
  裏の処理・コンテナは残っていない。シリアル・flash の道具は使っていない。
- 静的な見積もりの生データ (`.su`・`.ci`) は scratchpad
  `/tmp/claude-1000/-home-kishima-fmrb-family-mruby/d865add5-d638-40f7-89da-40dce8f29b87/scratchpad/w1d/su/`。
