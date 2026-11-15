# W5b: SDK に Rust と Zig を足す (生成のつなぎ・手書きの層・hello と tour)、0.x の版の付け方 (2026-11-16)

> 親: plan.md の決定 1・13・20・22 と report/w5.md の 8 章 | 指示: 親からの依頼 (と途中の追加 1 つ: 0.x の版の付け方) |
> 2026-11-16 02:06〜02:35 (`date`) | worktree `/home/kishima/fmrb/wt/rz-core` (branch `wt/rz`、`wt/w5` から) と
> family-mruby-sdk (main、ローカルのコミットだけ)。止まる条件には当たっていない (保留は 7 章)。

## 0. まとめ

- **生成**: SDK の `tools/lib/boundary.rb` に `Rust`・`Zig` のモジュール (名前と型の変換) を足し、型紙
  `tools/templates/rust/{raw,types}.rs.erb`・`tools/templates/zig/raw.zig.erb` を `OUTPUTS` に並べた。生成物は
  `rust/src/raw/{mod,types}.rs`・`zig/src/raw.zig` (どちらも 122 関数・記録 8・選択肢すべて)。
- **手書きの層**: Rust は crate `fmrb` (`rust/src/lib.rs`、`no_std`・アロケータなし・依存なし)、Zig はモジュール
  `fmrb` (`zig/src/fmrb.zig`)。フックの export とイベントの置き場 (`fmrb_event_buffer`、決定 20) はアプリの作者が
  1 行書くだけ (Rust `fmrb::app!(T);`、Zig `comptime { fmrb.exportApp(T); }`)。包む範囲は TinyGo と同じ。
- **ビルド**: `rust/build.rb` (cargo、スタック 16 KB、決定 1)・`zig/build.rb` (`zig build`、設定は `build.zig` の
  `addApp`)。どちらも `.app.toml` に `sdk_version` を書く。ダウンロードなし。
- **tour と hello**: `docs/tour.md` のとおりに Rust・Zig で書き、`flash/app/rust/`・`flash/app/zig/` に入れた。
  ネイティブ版とブラウザ版 (worktree のビルド) で 4 言語の tour が同じ画面になり、入力・音・`q` が通った。
- **照合**: `check_bindings.rb` が生成物 (6 つ) に加え、Go・Rust・Zig の生成した宣言を読み戻して境界の wasm の型と
  比べる (型紙そのものの確認)。`--examples` で全言語のサンプルをビルドして照合。Rust・Zig の記録はコンパイル時に
  大きさとオフセットを `boundary.json` と比べる (assert / `@compileError`)。
- **版 (追加の依頼)**: 0.x の間は壊す変更で MINOR、追加と残りで PATCH (semver の慣例)。1.0.0 からは今までどおり。
  1.0.0 は手で書く。`tools/test_version.rb` で試験。crate と Zig のパッケージの版も `VERSION` に揃える。
- 回帰: `rake regress`・`regress:spinel`・`wamr_poc boundary`・`boundary:check` は通った。

## 1. つなぎの生成

| | Rust (`rust/src/raw/`) | Zig (`zig/src/raw.zig`) |
|---|---|---|
| import | `#[link(wasm_import_module = "fmrb")] unsafe extern "C" { pub fn gfx_fill_rect(gfx: i32, ...) -> i32; }` | `pub extern "fmrb" fn gfx_fill_rect(gfx: i32, ...) i32;` |
| 名前 | 関数は import の名前のまま、記録は `AppWallclock`、定数は C のヘッダから `FMRB_` を外したもの (`GFX_SET_FONT_RAW_FAMILY_JA`、`APP_EVENT_TYPE_KEY_DOWN`) | 関数は同じ、記録は `AppWallclock`、定数は snake_case (`gfx_set_font_raw_family_ja`) |
| ポインタ | `*const u8`/`*mut u8`、記録は `*mut AppWallclock` | optional (`?[*]const u8`・`?*AppWallclock`、数を伴う記録の列は `?[*]const GfxCompositeRegion`)。null が C の NULL (大きさを問う呼び出し) |
| 記録 | `#[repr(C)]` (並びが自然なもの) / `#[repr(C, packed)]` (`AppWallclock`・`GfxCompositeRegion`・`GfxFileInfo`)。`Default`・`Copy` | `extern struct`、packed のものは欄ごとに `align(1)`。欄は既定値 0 |
| 並びの確認 | `const _: () = assert!(size_of / offset_of! == ...)` を記録ごと・欄ごとに | `comptime { if (@sizeOf/@offsetOf != ...) @compileError(...) }` |
| 予約語 | `r#type` (記録の欄 `type`)。手書きの層に `Event::kind()` | 欄は予約語だけ `@"..."` (0.17 では `type` は欄の名前に使え、`zig fmt` も `ev.type` に直す)、引数は primitive の名前も逃がす |
| 版・定数 | `SDK_VERSION`・`ABI_HASH`・`ERR_*`・`EVENT_BUFFER_SIZE` | `sdk_version`・`abi_hash`・`err_*`・`event_buffer_size`・`event_buffer_export` |

- 選択肢の名前の組み立て (`Boundary.function_choices`・`record_choices`) を言語に依らない形に括り出し、Go の
  `function_choices` もそれを使う形にした (TinyGo の生成物は 1 バイトも変わらない、照合で確認)。
- 生成物はフォーマッタの結果と一致させた: `zig fmt --check` が通る。Rust は `#[rustfmt::skip]` を `mod types` と
  import の塊に付け (`cargo fmt` が長い宣言を折り返して生成物と食い違うため)、`rust/rustfmt.toml` は `max_width = 120`。

## 2. 手書きの層

| | Rust | Zig |
|---|---|---|
| アプリ | `impl fmrb::App for T` (8 つのフックに何もしない既定) + `Default`、`fmrb::app!(T);` | 全欄に既定値のある struct、フックはメソッド (`onCreate` ほか、無いものは `@hasDecl` で何もしない)、`comptime { fmrb.exportApp(T); }` |
| export | マクロが `on_*` 8 つと `fmrb_event_buffer` (16 バイト境界の 64 バイト) を `#[unsafe(no_mangle)]` で。状態は `static mut Option<T>` (`&raw mut`) | `exportApp` が `@export` で同じ 9 つ |
| panic | 既定の feature `panic-handler` がトラップする handler を持つ (外せば自分のもの) | `pub const panic = fmrb.panic;` (`std.debug.no_panic`) をアプリのルートに書く |
| 文字列 | アロケータなし。返る文字列は `Text<N>` (固定のバッファ、`push`・`int`・`int2`・`core::fmt::Write`) | 渡したバッファに書いてスライスを返す。行は `std.fmt.bufPrint` |
| 包み | `WINDOW: Gfx` (描画・文字・フォント・画像)、`app::{name, language, script_path, pid, platform, stop, ...}`、`uptime_ms`・`wallclock`・`memory_free`、`AUDIO`、`file::{read, write, exist, size, delete}`、`log::*` | `window: Gfx`、`app.*`、`uptimeMs`・`wallclock`・`memoryFree`、`audio`、`file.*`、`logInfo` ほか |

配り方 (決定 13): Rust は workspace の root がパッケージ `family-mruby-sdk` (lib 名 `fmrb`)、サンプルは
`examples/{hello,tour}` の crate (cdylib) で workspace のメンバー。Zig は `build.zig.zon` (name `family_mruby_sdk`、
0.17 が求める fingerprint 入り) と `build.zig` (`addModule("fmrb")` と、利用者のための `addApp`)。どちらも
公式の置き場には出していない。

## 3. ビルドと大きさ

- Rust: `cargo build --release --target wasm32-unknown-unknown -p <名前>`、`RUSTFLAGS=-C link-arg=-zstack-size=16384`、
  profile は `opt-level="s"`・`lto`・`panic="abort"`・`codegen-units=1`・`strip`。CPU の機能は既定のまま (決定 1:
  WAMR の側で参照型と拡張された定数式を有効にしている)。読み込みのログは `memory 1 pages`。
- Zig 0.17.1: `zig build -Dexample=<名前>` (`wasm32-freestanding`、`entry = .disabled`、`rdynamic`、`ReleaseSmall`、
  `strip`、`stack_size = 16384`)。キャッシュは `~/toolchains/cache`。

| tour | 大きさ | import |
|---|---|---|
| TinyGo | 16,956 B | 35 |
| Rust | 11,016 B | 35 |
| Zig | 10,639 B | 35 |
| C | 9,143 B | 34 |

hello: Rust 2,980 B・Zig 2,812 B (TinyGo 7,538 B)。どれも import 8。C の tour だけ 34 なのは `log_warn_raw` を
使っていない (警告を info で出している) ため。

## 4. 確かめたこと

MCP の native_*・web_* は使わず、W5 と同じく family-mruby の `tools/fmrb_native.rb`・`fmrb_web.rb`
(と `fmrb_msgpack.rb`・`fmrb_input.rb`) を scratchpad に写し、`fmruby-core` を worktree への symlink にして動かした。
ネイティブ版は run ディレクトリ `/tmp/claude-1000/frz`・debug port 5577・別の user と cache。ブラウザ版は
worktree の `rake wasm:serve PORT=8017` と、Playwright の Linux の Chromium (`FMRB_CHROME`、プロファイルは
worktree の `wasm/build/drive_profile`。Windows の Chrome と共有のプロファイルを避けた) の headless。
ブラウザ版の束は git の管理下のファイルだけを入れるので、アプリを `git add` してから `rake wasm:web` した。
画面は `/tmp/claude-1000/-home-kishima-fmrb-family-mruby/d865add5-d638-40f7-89da-40dce8f29b87/scratchpad/rz/shots/`。

| 確かめたこと | ネイティブ版 | ブラウザ版 |
|---|---|---|
| 4 言語の tour の 5 ページ・`T` (ja・mixed)・画像・`W`・クリックで次のページ・`q`、同じ画面 | 通った (並べた画: `compare_n_p1_p2b_p2c_p3.png`・`compare_n_p4_p5_p5_click_quit.png`、列は TinyGo・C・Rust・Zig) | 通った (`compare_w_p1_p2b_p2c_p3.png`・`compare_w_p4_p5_p1_click_quit.png`。シェルの `run` で起動) |
| 音階と 988 Hz (Rust・Zig) | audio の peak 547 (W5 の TinyGo と同じ値) | 見ていない |
| Rust hello・Zig hello (同時に 2 つ、キー、`q`) | 通った (`n_hellos.png`・`n_rust_hello.png`) | — |
| 読み込みのログ | `memory 1 pages ... on_create on_update on_event (event buffer)` (Rust・Zig とも) | — |

画面の違いは、System のページの `toml` の大きさ (`.app.toml` の説明の行の長さ) と時刻・`file` の数だけ。
ブラウザ版の画像は束の別のアイコン (W5 と同じ)。

| 照合・回帰 | 結果 |
|---|---|
| SDK `check_bindings.rb --examples` | 生成物 6・C のヘッダ・Go/Rust/Zig の宣言 122 関数、9 本のサンプルすべて `ok` |
| わざと崩した宣言 (Rust の結果の型、Zig の引数の型) | 「stale」と「functions differ」の両方で落ちた (後で生成し直した) |
| 版のずれ (`Cargo.toml`・`build.zig.zon` を 9.9.9 に) | 落ちた (後で戻した) |
| `ruby tools/test_version.rb` | 17 件すべて通過 (0.x と 1.x の規則、偽の古い境界 3 つ × 版 2 つ) |
| ガイドの Rust・Zig の短い例 | 写してビルドが通った (1,193 B・562 B) |
| `cargo fmt --check`・`zig fmt --check` | 通った |
| `rake boundary:check` | 通った (122 関数・記録 8・フック 8) |
| `ruby tools/wamr_poc/run.rb boundary` | linux・emscripten とも `4b90f47e`、`b7957f81/33` |
| `rake regress` | host 29 suites 全部通過、描画の記録 229/229 |
| `rake regress:spinel` | 6 programs、0 failed |

## 5. 版の付け方 (途中の追加の依頼、ユーザの決定)

- `tools/version.rb`: 変化の判定を「壊す (breaking)・追加 (addition)・同じ (same)」の 3 つで出し、番号は
  `Boundary.next_version` が決める。0.x: 壊す → `0.(m+1).0`、追加・同じ → `0.m.(p+1)`。1.0.0 から: MAJOR・MINOR・
  PATCH。1.0.0 は道具が出さない (境界を安定と決めたときに手で `VERSION` に書く)。
- `--write` は `VERSION` と一緒に `rust/Cargo.toml`・`zig/build.zig.zon` の版も書く。`check_bindings.rb` がずれを
  落とす。
- README (英・日) の「版」の節を表にし、手順書の `sdk_version` の行に一文を足した。w5.md 9 章の保留の 1 つ目は
  これで決まった。

## 6. 取り込んだ後に親が流すもの

- `regress:screens`: `flash/app` に `/app/rust`・`/app/zig` が増えたので、ランチャーのスクロールバーと
  ファイルマネージャの `/app` の一覧の場面の基準が変わるはず (W5 と同じ種類の差)。
- P4 で Rust・Zig の tour を起動し、日本語のフォント・画像・音を見る (ESP32 のビルドはしていない。本体の C は
  変えていない)。
- SDK と fmruby-core の push (どちらもしていない)。公開のブラウザ版に Rust・Zig を入れるかは次の公開で。

## 7. 保留 (ユーザが決めること)

- crate の名前: パッケージ名 `family-mruby-sdk`・lib 名 `fmrb` にした (`use fmrb::...`)。crates.io に出すときの
  名前は未定 (`fmrb` は短く取られている可能性がある)。Zig のパッケージ名 `family_mruby_sdk` も同じ。
- Zig の `minimum_zig_version` は 0.15.0 と書いたが、確かめたのは 0.17.1 だけ (Zig は版ごとに build の API が変わる)。
- `rust/Cargo.lock` をコミットした (依存なしで中身は workspace の 3 つだけ)。ライブラリとして出すときに外すかは
  その時に。

## 8. 終わり

- ネイティブ版・ブラウザ・開発サーバは止めた。裏の処理なし。worktree で `rake clean_all`・`native:clean`・
  `wasm:clean` を流した。push していない。

### コミット

fmruby-core (`wt/rz`):

- 4a862d91 feat(apps): the SDK's tour and hello in Rust and Zig, sdk_version in their .app.toml
- この report (docs)

family-mruby-sdk (main、ローカルだけ):

- 6a28b33 feat(sdk): generate the Rust and Zig bindings from the boundary's data, check every language's declarations
- b0a9c23 feat(rust): the Rust crate (hooks, event buffer, Window, App, Audio, File), build script, hello and the tour
- ae67e08 feat(zig): the Zig package (hooks, event buffer, Window, App, Audio, File), build script, hello and the tour
- b463601 feat(sdk): number 0.x versions by semver's convention (breaking bumps minor, additions patch), with a test
- ae5c68b feat(sdk): keep the Rust crate's and the Zig package's version at VERSION
- 2a81ac7 docs(guide): Rust and Zig in the README, both guides and the tour's specification
