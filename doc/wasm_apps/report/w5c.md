# W5c: 各言語の自然な導入とビルド、SDK の版を wasm の custom section に (2026-11-16)

> 親: plan.md の決定 23 | 指示: 親からの依頼 | 2026-11-16 07:10〜07:45 (`date`) | worktree
> `/home/kishima/fmrb/wt/w5c-core` (branch `wt/w5c`、develop cb40748f から) と family-mruby-sdk (main、ローカルの
> コミットだけ)。止まる条件には当たっていない (保留は 6 章)。

## 0. まとめ

- **版は .wasm の中へ**: SDK のライブラリが `VERSION` を custom section `fmrb.sdk` (中身は semver の文字列だけ) に
  入れる。本体 (`components/fmrb_wamr/fmrb_wasm.c`) は WAMR に渡す前のバイナリから読み、断るときのメッセージに
  使う。`.app.toml` の `sdk_version` は読まない (後方互換の読み取りも消した。SDK は未リリースで、外に出たアプリは
  無いため)。ビルドのスクリプトも書かなくなり、`flash/app` の `.app.toml` からも消した。
- **導入とビルド**: TinyGo は `go get` と `tinygo build -target=fmrb.json`、Rust は `cargo add --git` と
  `.cargo/config.toml` と `cargo build --release`、Zig は `zig fetch --save git+https://...` と SDK の `addApp` と
  `zig build`、C はヘッダ 2 つと `zig cc`/clang の 1 行 (と小さな Makefile)。アプリの作者に build.rb は要らない
  (SDK の build.rb はサンプルのビルド用に残した)。
- scratchpad の新しいディレクトリで手順書のとおりに 4 言語の hello と tour を作り、ネイティブ版とブラウザ版で動いた。
  無い関数を import するアプリは、custom section の版 (`SDK 9.1.0`) を示して断られた。
- 回帰: `rake regress`・`regress:spinel`・`wamr_poc boundary`・`boundary:check` 通過。

## 1. 各言語の手順 (手順書に書いたもの)

| 言語 | SDK の取り込み | ビルド |
|---|---|---|
| TinyGo | `go mod init example.com/hello`、`go get github.com/family-mruby/family-mruby-sdk/tinygo`、`cp "$(go list -m -f '{{.Dir}}' github.com/family-mruby/family-mruby-sdk/tinygo)/fmrb.json" .` | `tinygo build -target=fmrb.json -o hello.app.wasm .` |
| Rust | `cargo new --lib hello`、`cargo add --git https://github.com/family-mruby/family-mruby-sdk family-mruby-sdk` (公開後は `cargo add family-mruby-sdk`)、`Cargo.toml` に `[lib] crate-type = ["cdylib"]`、`.cargo/config.toml` (SDK の `rust/.cargo/config.toml` と同じ。target・スタック 16 KB・release の profile) | `cargo build --release` → `cp target/wasm32-unknown-unknown/release/hello.wasm hello.app.wasm` |
| Zig | `zig init --minimal`、`zig fetch --save git+https://github.com/family-mruby/family-mruby-sdk`、`build.zig` で `@import("family_mruby_sdk").addApp(b, .{ .name, .root_source_file, .app_toml })` | `zig build` → `zig-out/bin/hello.app.wasm` (+ `.app.toml` をコピー) |
| C | `c/fmrb_boundary.h`・`c/fmrb_sdk_version.h` をコピー (curl で raw の URL) か git submodule。1 ファイルに `FMRB_EVENT_BUFFER();` と `FMRB_SDK_VERSION_SECTION();` | `zig cc -target wasm32-freestanding -nostdlib -Os -I. -Wl,--no-entry -Wl,-z,stack-size=16384 -Wl,--strip-all hello.c -o hello.app.wasm` (clang は `--target=wasm32`)。Makefile の例は `c/examples/hello/Makefile` |

SDK の側の変更:

- TinyGo: ターゲットファイル `tinygo/fmrb.json` (`inherits: wasm-unknown`、`gc: conservative`、`ldflags: --strip-debug`)。
  今までのフラグ (`-opt=z -no-debug`) と出力は 1 バイトも違わない (hello で cmp)。
- Rust: `rust/.cargo/config.toml` (SDK の workspace もこれでビルド)。`rust/Cargo.toml` の profile はここへ移した
  (依存の crate の profile は使われないため、アプリが持つ形にした)。
- Zig: `build.zig`・`build.zig.zon` をリポジトリのルートへ移した (`zig fetch` はリポジトリ全体を取り、下の
  ディレクトリを指せないため)。`paths` は `build.zig`・`build.zig.zon`・`zig/src`・`LICENSE` だけ。`addApp(b, opts)`
  は利用者の `family_mruby_sdk` の依存からモジュールを取り、出力名を `<name>.app.wasm` にする。設定は `addAppModule`。
- C: `c/examples/hello` (hello.c・hello.app.toml・Makefile) を足した。
- build.rb (4 言語) は上の設定を使い、`.app.toml` は出力先に無いときだけサンプルのものをコピーする。
- README・入門ガイド (英・日)・maintaining (英・日)・tour.md を書き直した (3 章・6 章を言語ごとのプロジェクトに)。

## 2. custom section の作り

- 名前 `fmrb.sdk`、中身は `VERSION` の文字列そのもの (例 `0.1.0`)。`tools/lib/boundary.rb` の `SDK_SECTION`。
- どの言語にも custom section を作る仕組みが無いので、アセンブラの `.section .custom_section.fmrb.sdk,"",@` と
  `.ascii "<版>"` で作る (LLVM の wasm のオブジェクトがこの名前の section を custom section にし、wasm-ld は
  `--strip-all` でも残す)。C の `__attribute__((section))`・Zig の `linksection` はデータの segment になり使えない
  ことを確かめた。

| 言語 | 置き場 |
|---|---|
| TinyGo | 生成物 `tinygo/fmrb/raw/version.go` (cgo の前文の top-level asm、`//go:build tinygo.wasm`)。サイズは +16 B |
| Rust | `app!` マクロがアプリの crate に `#[unsafe(link_section = "fmrb.sdk")] #[used] static` を置く (生成の `raw::SDK_VERSION_SECTION`)。ライブラリ側に置くと LTO なしで落ちた |
| Zig | `zig/src/fmrb.zig` のトップレベルの `comptime { asm (...) }` (Debug・ReleaseSmall とも残る) |
| C | `FMRB_SDK_VERSION_SECTION();` をアプリに 1 度 (生成の `c/fmrb_sdk_version.h`)。2 つのファイルに書くと文字列が重なる |

- `tools/check_bindings.rb` はアプリごとに `SDK x.y.z` を表示し、`--examples` では版が `VERSION` でないと落ちる。
- 本体: `read_sdk_version(bin, size, ...)` が section の見出しだけを歩いて `fmrb.sdk` を探す (LEB128 の範囲検査、
  印字できる文字だけ、23 文字まで)。WAMR の `WASM_ENABLE_LOAD_CUSTOM_SECTION` は使わない (WAMR の設定とメモリを
  変えないため)。読み込みの前に読むのは、WAMR の loader がバッファを書き換えうるため。メッセージの形は今までどおり
  (`the app was built with SDK 9.1.0, newer than this firmware` / 版が無いと `a newer SDK`)。

## 3. 新しいディレクトリでの確認

scratchpad に `GIT_CONFIG_GLOBAL` (url.insteadOf で `https://github.com/family-mruby/family-mruby-sdk` を
ローカルの SDK へ)、`GOPRIVATE`・別の `GOPATH`・`CARGO_HOME`・Zig のキャッシュを置き、手順書の code block を
そのまま抜き出して作った。ツールチェーンは `~/toolchains` を PATH に入れた形 (手順書の「PATH にある」)。

| 言語 | hello | tour | 備考 |
|---|---|---|---|
| TinyGo | 7,554 B (疑似版 `v0.0.0-...-752fd18c0b72` を取得) | 16,972 B、`flash/app` と同一 | `fmrb.json` はモジュールキャッシュから読み取り専用のままコピーされる |
| Rust | 1,261 B (edition 2024 の `cargo new` で通る) | 11,033 B (`flash/app` と 3 B 違い。edition の差と見る) | `cargo add --git` は `version = "0.1.0"` も書く |
| Zig | 578 B | 10,655 B、`flash/app` と同一 | `git+https` はネットに出せないので、`git archive HEAD` の tar.gz を `zig fetch --save` した (git+https で取るのと同じ木)。`git+file` は Zig が受け付けない |
| C | 1,363 B (`zig cc` の 1 行) | submodule + Makefile で 9,159 B、`flash/app` と同一 | clang は入っておらず未確認 |

どれも custom section `fmrb.sdk` = `0.1.0` (`check_bindings.rb` と自前の読み取りで確認。wasm-objdump は無い)。

| 確認 | ネイティブ版 (`fmrb_native.rb`、debug port 5579) | ブラウザ版 (`rake wasm:serve PORT=8018`、headless Chromium) |
|---|---|---|
| 新しく作った 4 言語の hello (キーで `key x`、`q` で終わる) | 通った | 通った (`/flash/app/usr/w5c/` に put してシェルの `run`) |
| 4 言語の tour (作り直した `flash/app`)、1・2 ページ | 4 言語で同じ画面 | 同じ |
| 無い関数 (`gfx_draw_hologram`) を import、section の版 9.1.0 | `... built with SDK 9.1.0, newer than this firmware ...` の Window とログ | 同じ Window |
| 同じで section なし | `... built with a newer SDK ...` | 同じ |

画面は scratchpad の `w5c/n_*.png`・`w5c/w_*.png` (`n_tours.png`・`w_tours.png`・`n_bad.png`・`w_bad.png`・
`w_fresh_grid.png`)。

| 照合・回帰 | 結果 |
|---|---|
| SDK `check_bindings.rb --examples` | 10 本 (C hello を足した) すべて SDK 0.1.0、ok |
| `cargo fmt --check`・`zig fmt --check`・`tools/test_version.rb` | 通過 |
| `rake boundary:check` | 通過 |
| `ruby tools/wamr_poc/run.rb boundary` | linux・emscripten とも `4b90f47e`、`b7957f81/33` |
| `rake regress` | host 29 suites、描画 229/229 |
| `rake regress:spinel` | 6 programs、0 failed |

## 4. 気づいたこと

- worktree の `tools/wamr_poc/vendor/wamr-2.4.5` は `.git` を除いて写すと run.rb が clone し直そうとして失敗する
  (`.git` の有無で判定)。`.git` ごと写し直した。
- ブラウザ版の `fmrb_web.rb launch` はこの develop の page に無い (`unknown op`)。シェルの `run` で起動した。
  `fmrb_web.rb put` の `/app/usr/...` は Emscripten の FS の path なので、実際は `/flash/app/usr/...` に置く。
- native:core は `rake spinel:gen` の前だと黙ってバイナリを作らない (ログに missing と出るだけ)。

## 5. 取り込んだ後に親が流すもの

- `regress:screens`: `flash/app` の `.wasm` の大きさと `.app.toml` の大きさが変わったので、System のページなど
  大きさが出る場面の基準が変わりうる。
- P4 で tour を起動 (ESP32 のビルドはしていない。`fmrb_wasm.c` は移植性のある C だけ)。
- SDK と fmruby-core の push。公開した後で `go get`・`cargo add --git`・`zig fetch --save git+https://...` を本物の
  URL で 1 度流す。

## 6. 保留 (ユーザが決めること)

- TinyGo の `fmrb.json` で `--strip-all` にすると name section が消え、hello 6,209 B (−1.3 KB)・tour 13,101 B
  (−3.9 KB) になる。今は従来と同じ `--strip-debug` (トラップ時の関数名を残す)。Rust・Zig は既に strip。
- C の hello を `flash/app/c/` に入れるか (今は SDK のサンプルだけ。入れるとランチャーの基準が変わる)。
- `fmrb.json` が読み取り専用でコピーされる (手順書に `chmod` を書くか)。
- 手順書の C の 1 行のうち clang は未確認 (この PC に clang と wasm-ld が無い)。
- Zig の `minimum_zig_version` 0.15.0 は 0.17.1 でしか確かめていない (W5b からの持ち越し。`zig init --minimal` の
  有無も版による)。

## 7. 終わり

ネイティブ版・ブラウザ・開発サーバは止めた。裏の処理なし。worktree で `rake clean_all`・`native:clean`・`wasm:clean`
を流した。push していない。

### コミット

fmruby-core (`wt/w5c`):

- 9a8cfa61 feat(wasm): name the SDK version from the module's custom section fmrb.sdk when refusing an app
- 6ceb6ba5 feat(apps): the SDK samples rebuilt with the SDK version in the .wasm, no sdk_version in their .app.toml
- この report (docs)

family-mruby-sdk (main、ローカルだけ):

- 1b8d9b5 feat(sdk): the SDK version in a wasm custom section, and each language's own way to build an app
- 752fd18 docs(guide): make an app in a project of its own with each language's tools (go get, cargo, zig fetch, a C header)
