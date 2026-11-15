# W1c: イベントの記録の置き場と、ネイティブ版・ブラウザ版の wasm のアプリ (2026-11-15〜16)

> 親: plan.md の段 W1b の残りと決定 20、指示: instruction_w1c.md | 2026-11-15 23:42〜2026-11-16 00:20 (`date`) |
> worktree `/home/kishima/fmrb/wt/w1c-core` (branch `wt/w1c`、develop 72b2fea8 から)。止まる条件には当たっていない。

## 0. まとめ

- **イベントの記録の置き場 (決定 20)**: SDK が `fmrb_event_buffer() -> i32` を export し、本体はその番地 (64 バイトの
  静的な置き場) に記録を書いてから `on_event` を呼ぶ。無いモジュールは今までどおりスタックに置く。スタックを
  使わない最小の TinyGo のアプリが読み込まれ、クリックを受けることをネイティブ版とブラウザ版で確かめた。
  ABI のハッシュは変わらない (`0x922a7dad`)。
- **ネイティブ版・ブラウザ版**: `hosted/wamr/CMakeLists.txt` で WAMR と `fmrb_wasm.c` を静的なライブラリにし、
  両方の実行ファイルにつないだ。bounce・hello・c_check が両方で描き、クリック・キー・`q`・閉じる・再起動が通った。
- **ブラウザ版のスタックの境界**: `emscripten_stack_get_end()` (呼んだスレッドのスタックの下端) + 6 KB。NULL を
  やめた。
- 回帰: `rake regress`・`regress:spinel`・`regress:screens ONLY=web`・`ONLY=native` が全部通った。基準は更新していない。

## 1. イベントの記録の置き場 (決定 20)

| 項目 | 決めたこと |
|---|---|
| 名前 | **`fmrb_event_buffer`** (export する関数)。生成のヘッダの `FMRB_BND_EVENT_BUFFER_EXPORT` |
| 形 | `fmrb_event_buffer() -> i32` (引数なし、置き場の番地を返す)。大きさは `FMRB_BND_EVENT_BUFFER_SIZE` = 64 バイト (記録 60 バイトを 16 に切り上げ。生成の道具がフックの記録の大きさから出す) |
| 本体 | `find_hooks` で探し、形 (0 引数・1 結果) が違えば理由つきで断る。最初の `on_event` の前に 1 回だけ呼んで番地を覚え、`wasm_runtime_validate_app_addr` で 64 バイトがアプリのメモリの中かを見る。以後は記録をそこへ写して `on_event(番地)` |
| 無いとき | 今までどおりスタックポインタの大域変数の下に置く (古い SDK のアプリはそのまま動く)。置き場の関数が 0 や範囲外を返したときも、スタックポインタがあればそちらへ戻る (警告のログ) |
| 両方無いとき | 読み込みで断る: `on_event needs fmrb_event_buffer (the SDK's event buffer) or a stack pointer global (__stack_pointer) for its record: rebuild with the current SDK` |
| 読み込みのログ | 置き場があると行の終わりに `(event buffer)` |

export を大域変数でなく関数にした理由: TinyGo は変数を export できない (`//export` は関数だけ)。関数なら TinyGo・Rust・
Zig・C のどれでも同じ形で書ける。

**ABI のハッシュは変わらない** (`0x922a7dad`)。ハッシュは `gen.rb` が関数・記録・フックから出すもので、この段で変えて
よいのは型紙だけ。置き場は両方向で任意 (古い本体は export を無視してスタックに置き、新しい本体は古いアプリを
スタックで受ける) なので、ハッシュを変える理由もない。

### 1.1 生成の型紙

| 型紙 | 足したもの |
|---|---|
| `tools/boundary/templates/fmrb_boundary.h.erb` (本体の生成のヘッダ) | `FMRB_BND_EVENT_BUFFER_EXPORT "fmrb_event_buffer"`・`FMRB_BND_EVENT_BUFFER_SIZE 64` |
| `tools/boundary/templates/sdk_c.h.erb` (`sdk/c/fmrb_boundary.h`) | `FMRB_EVENT_BUFFER_SIZE 64`、宣言 `FMRB_EXPORT("fmrb_event_buffer") void *fmrb_event_buffer(void);`、マクロ `FMRB_EVENT_BUFFER()` (ファイルの外側に 1 行書くと、16 バイトに揃えた 64 バイトの static な置き場と export の関数を定義する) |

`fmrb_wasm.c` は `_Static_assert(sizeof(fmrb_bnd_app_event_t) <= FMRB_BND_EVENT_BUFFER_SIZE)` で記録が収まることを見る。

### 1.2 SDK (family-mruby-sdk、ローカルのコミットのみ)

- `tinygo/fmrb/fmrb.go`: `var eventBuffer [8]uint64` (64 バイト、8 バイト境界) と `//export fmrb_event_buffer`。
  アプリの作者は何も書かない。
- `tools/check_bindings.rb`: 返り値がポインタのフック (`void *fmrb_event_buffer`) を読めるようにした。`on_event` を
  持つのに置き場の無い .wasm には `note:` を出す (失敗にはしない)。
- `docs/getting_started.md`・`.ja.md`: トラブルシューティングから回避策 (文字列を 1 つ組み立てる) を消し、その行を
  「古い SDK のアプリなので今の SDK でビルドし直す (C は `FMRB_EVENT_BUFFER();`)」にした。対応表のネイティブ版・
  ブラウザ版を「今の fmruby-core からビルドしたものなら動く (公開中のブラウザ版にはまだ無い)」に、
  `wasm apps are not in this build` の行も合わせた。英語の文書とコードは ASCII のまま (grep で確かめた)。

### 1.3 スタックを使わない最小のアプリ

scratchpad の `w1c/nostack/src/main.go` (`OnUpdate` で区画を塗るだけ、`OnEvent` でクリックとキーで色を反転、
文字列も確保も無い)。同じソースを旧 SDK と新 SDK でビルドした:

| | 大きさ | 大域変数 | `fmrb_event_buffer` | ネイティブ版 | ブラウザ版 |
|---|---|---|---|---|---|
| 旧 SDK (`old.wasm`) | 2,124 B | **0 個** (スタックポインタが消えている) | 無い | 理由つきで断られ、エラーの Window | 同じ |
| 新 SDK (`new.wasm`) | 2,171 B | 0 個 | ある | 読み込まれ、クリックで青→赤 | 同じ |

### 1.4 作り直したアプリ (fmruby-core の `flash/app/`)

| アプリ | 前 | 後 |
|---|---|---|
| `tinygo/bounce.app.wasm` | 9,238 B | 9,285 B |
| `tinygo/hello.app.wasm` | 7,460 B | 7,507 B |
| `test/c_check.app.wasm` (`tools/wasm_apps/c_check/app.c` に `FMRB_EVENT_BUFFER();` を 1 行) | 1,676 B | 1,706 B |

`check_bindings.rb --header <worktree の sdk/c/fmrb_boundary.h>`: 48 の import が 122 関数と一致、3 本とも import と
フック (`fmrb_event_buffer` を含む) の型が一致。

## 2. ネイティブ版のつなぎ

| 場所 | 変更 |
|---|---|
| `hosted/wamr/CMakeLists.txt` (新規) | WAMR (`vendor/wamr` の `runtime_lib.cmake`) と `components/fmrb_wamr/fmrb_wasm.c` を静的なライブラリ `fmrb_wamr_hosted` に。設定は ESP-IDF の部品 (`components/fmrb_wamr/CMakeLists.txt`) と同じ (classic インタプリタ・参照型・拡張された定数式・命令数の上限・bulk memory、`invokeNative` は外して止まる関数)、土台の層も同じ 1 つ。対象は wasm32 が `X86_32`、arm64 のネイティブ版が `AARCH64`、ほかは `X86_64` |
| `hosted/hosted.cmake` | `add_subdirectory(hosted/wamr)`、本体のオブジェクトに `FMRB_HAVE_WASM=1` と `components/fmrb_wamr/include` |
| `native/CMakeLists.txt` | `fmruby-native` が `fmrb_wamr_hosted` をリンク |

- 別のディレクトリにした理由: WAMR の `runtime_lib.cmake` と土台の `shared_platform.cmake` は `add_definitions`・
  `include_directories` でディレクトリ全体に定義を足す (`BH_MALLOC`・`WASM_ENABLE_*` など)。hosted.cmake の中で読むと
  本体の全部のオブジェクトに漏れる。`fmrb_wasm.c` はインタプリタの内部の構造 (`WASMModuleInstance`) を読むので、
  その定義と同じ所でコンパイルする。
- ネイティブ版は `CONFIG_IDF_TARGET_LINUX` で `pthread_getattr_np` の経路を通るが、ESP-IDF と違い `_GNU_SOURCE` が
  無いので暗黙の宣言の警告が出た。`fmrb_wasm.c` の頭で (Linux かつ emscripten でないときだけ) 定義した。
- 共通のプールは `malloc` (今までの Linux の経路のまま)。
- スタックの境界は sim と同じ `pthread_getattr_np`。ネイティブ版のタスクのスタックは端末の値の 4 倍で最低 256 KB
  なので、境界の上に約 244 KB ある (ログ `native stack room 249608`)。

## 3. ブラウザ版のつなぎとスタックの境界

- `wasm/CMakeLists.txt`: `core` (node) と `core_web` (ブラウザ) が `fmrb_wamr_hosted` をリンク。WAMR は emscripten の
  clang で `X86_32` としてビルドされる (W1a と同じ。emscripten の WASI のヘッダの手当ては土台の層にもう入っている)。
  WAMR の警告は出なかった。
- **スタックの境界の扱い (決定)**: `emscripten_stack_get_end()` + 6 KB (`FMRB_WASM_NATIVE_STACK_GUARD`、ほかと同じ)。
  - ブラウザ版の FreeRTOS のタスクは 1 つずつ pthread (Worker) で、C のスタックは emscripten の線形メモリの中にある。
    emscripten はスレッドの開始時にそのスレッドのスタックの範囲を設定し、`emscripten_stack_get_end()` は呼んだ
    スレッドの下端を返す。wasm のアプリの exec env はアプリのタスク (そのスレッド) で作るので、その場で取れば
    そのタスクのスタックの境界になる。範囲が取れるので、命令数の上限だけに頼る形にはしなかった。
  - ブラウザ版のタスクのスタックは `.app.toml` の値をそのまま使い最低 64 KB (`portHOSTED_MIN_TASK_STACK_BYTES`)。
    wasm のアプリの既定 24 KB は 64 KB になり、境界の上に約 56 KB ある (ブラウザのコンソールで
    `native stack room 57556` を確かめた)。
  - 溢れさせて止まるかは試していない。classic インタプリタは wasm から wasm への呼び出しで C の再帰をしない
    (フレームは演算のスタック 16 KB の側) ので、C のスタックを深く使うのは境界の関数の中だけで、溢れを起こす
    アプリは作りにくい。
- 読み込みのログに `native stack room N` (境界から今のフレームまでのバイト数) を足した。P4 の値は親が焼いたときに読める。
- **bundle**: `wasm:webflash` は git が追跡する `flash/` を束ねるので、`/app/tinygo` (bounce・hello) と
  `/app/test/c_check` はそのまま入る (`wasm/build/webflash/app/` で確かめた)。決まりのとおり、公開の bundle
  (`FMRB_RELEASE=1`、`wasm:site`・`wasm:deploy`) では `/app/test` が外れ、`/app/tinygo` は残る。設定の変更は要らなかった。

## 4. 大きさ

同じコミットの変更前 (hosted の 3 つの CMake を戻したもの) と変更後のビルドを比べた (どちらも worktree で、
別のビルドのディレクトリ。終わってから消した)。

| 項目 | 前 | 後 | 差 |
|---|---|---|---|
| ブラウザ版 `core_web.wasm` | 6,998,528 B | 7,124,940 B | **+126,412 B** (gzip で 2,917,466 → 2,962,293、+44,827 B) |
| ブラウザ版 `core_web.data` (bundle) | 4,491,898 B | 4,491,898 B | 0 (アプリは W1b から追跡されている。この段で変わったのはアプリ 3 本の +141 B だけで、同じ版のアプリで比べた) |
| ブラウザ版 `core_web.js` | 183,392 B | 183,392 B | 0 |
| ネイティブ版 `fmruby-native` (RelWithDebInfo) | 27,198,464 B | 28,300,304 B | +1,101,840 B (text +165,798・data +6,208・bss +928) |

W1a の本体役の増分 (emscripten、127,734 B) とほぼ同じ。

## 5. 確かめたこと

MCP の native_* と web_* は本体の checkout を見るので使わず、ルートの道具 (`tools/fmrb_native.rb`・
`tools/fmrb_web.rb`) を scratchpad に写し、`fmruby-core` を worktree へ向けて動かした (道具は `HERE/../fmruby-core`
を見る)。ネイティブ版は別の run ディレクトリ・debug port 5575・user ディレクトリで、ブラウザ版は worktree の
`rake wasm:serve PORT=8016` と headless の Chrome で。画面は scratchpad の
`/tmp/claude-1000/-home-kishima-fmrb-family-mruby/d865add5-d638-40f7-89da-40dce8f29b87/scratchpad/w1c/shots/`。

| 確かめたこと | ネイティブ版 | ブラウザ版 |
|---|---|---|
| bounce の描画 (四角・frame・pid と toml 448 B) | 通った (`native_bounce.png`) | 通った (`web_bounce.png`) |
| クリックで色、キーで `key 120` (x) | 通った (`native_bounce_input.png`) | 通った (`web_bounce_input.png`) |
| `q` で終わる | 通った (bounce・hello、`bye after N` → `runtime ended`) | 通った (bounce) |
| c_check: `bad ptr -2`、イベントの記録 (`ev 4 x 89 y 49` = 区画の座標の mouse_move)、`area 138x68` | 通った (`native_c_check.png`) | 通った (`web_c_check.png`) |
| 閉じるボタン | 通った (c_check、`on_destroy` → `runtime ended`) | 通った (c_check、`web_closed.png`) |
| 再起動 (終わった後にもう一度) | 通った (bounce、`native_bounce_restart.png`) | 通った (bounce、`web_bounce_restart.png`) |
| hello (`k` キー、`q`) | 通った (`native_hello.png`) | — |
| debugd の kill | 通った (nostack_new、`Exited on request`) | (道具が無い) |
| 置き場の無い最小のアプリ (旧 SDK) | 断られる (`native_nostack.png`) | 断られる (`web_nostack_old.png`) |
| 置き場のある最小のアプリ (新 SDK) | 動き、クリックで青→赤 (`native_nostack_clicked.png`) | 同じ (`web_nostack_new_clicked.png`) |
| ログの `(event buffer)` | bounce・c_check・hello・nostack_new | bounce (コンソール) |

- ブラウザ版のログは、写した `fmrb_web.rb` に Chrome の `--enable-logging --v=0` を足し、プロファイルの
  `chrome_debug.log` (コンソールの行が入る) で読んだ。読んだ後に消した。
- シェルの `run` は `.app.toml` の無いファイルをサンドボックスで走らせる (アプリとして起動しない) ので、ブラウザ版の
  最小のアプリには `.app.toml` を隣に置いた。wasm に限らない、今の決まりどおりの動き。

### 回帰

| コマンド | 結果 |
|---|---|
| `rake regress` | host 29 suites 全部通過 (43.3 s)、描画の記録 229/229 |
| `rake regress:spinel` | 6 programs、0 failed (20.0 s) |
| `rake regress:screens ONLY=web` (`FMRB_ROOT` に写した道具) | 6/6 通過 (3.1 分) |
| `rake regress:screens ONLY=native` (同じ) | native 3/3・native-keys 4/4 通過 (1.9 分) |
| `rake build:linux` (clean_all から、IDF の Linux) | 通った。`fmrb_wasm.c` の警告なし。`file build/fmruby-core.elf` は x86-64 |
| `ruby tools/boundary/gen.rb --check` | 通った (122 関数・記録 8・フック 8) |

## 6. 取り込んだ後に親が流すもの

- `rake build:esp32` (NARYAv4 か TAB5) と `rake regress:size`: `fmrb_wasm.c` の変更 (置き場・ログ) が P4 で通ること、
  内蔵 RAM が W1b から増えないこと (この段は static を足していない。足したのは exec env ごとの局所変数だけ)。
  この段では ESP32 のビルドをしていない (回線と、本体の checkout の作業との兼ね合い)。
- sim の `rake regress:screens` (retro・modern): アプリの .wasm が変わったが表示は同じはず。
- P4 で bounce を起動し、ログの `(event buffer)` と `native stack room N` (24 KB のタスクのスタックの残り) を読む。
- SDK: 取り込み後の本体の checkout のヘッダで `ruby tools/check_bindings.rb tinygo/build/*.wasm` を流し直す
  (SDK の照合は隣の `fmruby-core/sdk/c/fmrb_boundary.h` を既定で読む)。
- 公開のブラウザ版 (studio) への反映と、SDK の手順書の「ブラウザで試す」はユーザと相談 (指示のとおり、この段では
  手を付けていない)。

## 7. 残ったこと・気付いたこと

- ブラウザ版・ネイティブ版で溢れさせて境界で止まるかは試していない (3 章)。
- `tools/wamr_poc/host` (試作の本体役) は置き場を知らない (スタックに置く形のまま)。試作なので直していない。
- ブラウザ版の `rake wasm:serve` の道具には wasm のアプリを起動する口が無い (デバッグの道具が無い)。今回は
  シェルの `run` で起動した。regress の場面に wasm のアプリを足すかはユーザの方針どおり見送った。
- ネイティブ版を動かしたとき、ランチャーのアイコンのキャッシュはユーザの `~/.cache/family-mruby-dev` を使った
  (道具の既定。`--cache-dir` を付ければ分けられた)。
- 本体の checkout・build/・sim・シリアル・flash には触れていない。`wasm/vendor/` は本体の checkout と同じく追跡外の
  まま (コミットしていない)。

## 8. 終わり

- 終了 2026-11-16 00:20 頃。ネイティブ版・ブラウザ・開発サーバは止めた。裏の処理なし。worktree で `rake clean_all`
  を流して build/ を消した。push していない。

### コミット

fmruby-core (`wt/w1c`):

- cb578b1a feat(wasm): on_event's record goes into a buffer the SDK exports
- c77c2216 feat(apps): bounce, hello and c_check export the event buffer
- e64b2503 feat(native,wasm): run .app.wasm apps in the native program and the browser build
- この report (docs)

family-mruby-sdk (main、ローカルだけ):

- ce16bb1 feat(tinygo): export the event buffer the firmware writes the record into
- 7372308 docs(guide): the native and browser builds run wasm apps when built from the current firmware
