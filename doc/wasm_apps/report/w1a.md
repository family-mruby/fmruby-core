# W1a: 本体の外での WAMR の単体の試作 (2026-11-14)

ファームウェアの外に小さな本体役のプログラムを作り、WAMR 2.4.5 のインタプリタ (classic) で
Rust・TinyGo・Zig のアプリを動かした。本体役は 1 つのソースから Linux (x86-64) と emscripten
(node で実行) の 2 つにビルドした。**emscripten の版は動いた。** 止まる条件には当たっていない。

数字はすべて手元の PC (WSL2) で 1 回ずつ測ったもの。ビルドは両方とも最適化あり (`-O3`)。
「推測」と書いたもの以外は、実行またはソースで確かめた事実である。

## 1. 受け入れ条件

`ruby tools/wamr_poc/run.rb` を 1 回流した出力の要約 (全部 PASS)。

| 番号 | 結果 | 中身 |
|---|---|---|
| A1 | 合格 | Linux の版で 3 言語とも 100 フレーム後のチェックサムが `8ae482c5`。C で直接計算した期待値と一致。関数の表を通す間接呼び出し (`dispatch`) も 3 言語とも正しい値 |
| A2 | 合格 | emscripten の版 (node) で、A1 と**同じ `.wasm` ファイル**が同じチェックサム `8ae482c5` |
| A3 | 合格 | 本体の関数は `wasm_runtime_register_natives_raw` だけで登録 (`host/host.c`)。型を見て C の関数を呼び分ける部分 (`invokeNative`) は WAMR のソースをビルドから外し、呼ばれたら止まる関数に差し替えた (両方のビルド)。全部の試験がその関数を通らずに通った |
| A4 | 合格 | 終わらないループ (`spin`) が、両方のビルド・3 言語の 6 通りとも `Exception: instruction limit exceeded` で止まった (上限は 1 回の呼び出しにつき 1000 万命令)。本体役は落ちず、その後も同じインスタンスを使い続けられた (5 章) |
| A5 | 合格 | WAMR の確保は起動時に渡した 2 MiB のプールの中だけ。プールの外の確保 (`os_malloc`・`os_realloc`) は全部の試験で 0 回。wasm のメモリは、起動時に取った 2 つ目の固定の領域 (8 MiB) から土台の層の `os_mmap` が切り出した (4 章) |
| A6 | 合格 | 別のモジュール 2 つ (rust+zig・tinygo+rust・zig+tinygo) を交互に、また同じモジュールからのインスタンス 2 つを 30 フレームずらして交互に `on_update` し、全部のキャンバスが期待値と一致 (両方のビルド) |
| A7 | 合格 | 最大ページ数を「初期 + 2」にしたアプリが 6 ページ伸ばそうとして失敗し、もう 1 つのアプリは動き続けて期待値と一致。失敗の形は言語で違う: Rust・Zig は `memory.grow` が -1 を返す、TinyGo はヒープが取れずにトラップ (`Exception: unreachable`)。失敗したアプリ自身も、その後の `on_update` は正しく続いた |

## 2. 作ったもの

置き場は `tools/wamr_poc/`。WAMR のソース (`vendor/`) とビルドの出力 (`build/`) は git で無視する。

| ファイル | 中身 |
|---|---|
| `run.rb` | 取得・ビルド・実行・判定を 1 つにまとめたスクリプト。`ruby tools/wamr_poc/run.rb` で全部。`fetch`・`build`・`test`・`features`・`clean` を個別にも流せる |
| `host/host.c` | 本体役。ループを回し、試験ごとの命令 (`run`・`spin`・`multi`・`limit`・`bench`・`churn`) を持つ。結果は `RESULT` で始まる行で出す |
| `host/platform/` | 自作の土台の層 (`poc_platform.c`・`platform_internal.h`)、`invokeNative` の代わりの止まる関数、WAMR のビルドへのつなぎ |
| `host/baseline.c` | WAMR を除いた同じ形のプログラム (大きさの比較用) |
| `host/CMakeLists.txt` | 2 つのビルドの共通の指定。WAMR の設定はここで固定 |
| `apps/rust`・`apps/tinygo`・`apps/zig` | 同じ動きのアプリ |

アプリの形:

- 本体の関数 (`env` の import): `fmrb_log(ptr, len)`・`fmrb_plot(x, y, color)`・
  `fmrb_millis() -> i64`。速さの計測用に `fmrb_nop(v) -> i32`。
- 公開する関数: `on_create`・`on_update` (16 を返す)。試験用に `noop`・`bench_compute`・
  `bench_host_calls`・`spin`・`grow`・`dispatch`、TinyGo だけ `alloc_churn`。
- 本体役は `_initialize` があれば最初に 1 回呼ぶ (TinyGo が持つ)。
- アプリの状態 (フレームの数) はアプリの中に持つ。本体役はインスタンスごとに 64x64 の
  キャンバスを持ち、インスタンスに付けた利用者データで本体の関数から引く。

WAMR の設定 (`host/CMakeLists.txt`): classic インタプリタ。AOT・JIT・WASI・libc・
スレッド管理・共有メモリ・複数モジュール・SIMD は無し。命令数の上限あり。メモリの一括操作・
参照型・拡張された定数式はあり (6 章)。ハードの境界検査は使わない (土台の層が定義しない
ので、ESP32 と同じソフトの境界検査になる)。

## 3. emscripten で動かすまでに要ったこと

WAMR のソースは 1 行も変えていない。手当ては、自作の土台の層とビルドの指定だけで済んだ。

1. **ヘッダの衝突。** emscripten の `<errno.h>` が emscripten 自身の `<wasi/api.h>` を読み込み、
   WAMR の `platform_wasi_types.h` と型の定義がぶつかってコンパイルできなかった
   (`typedef redefinition with different types` ほか多数)。WASI は使わないので、
   emscripten のときだけ土台の層の `platform_internal.h` で `<wasi/api.h>` を先に読み、
   WAMR の側のヘッダを読まない印 (`_PLATFORM_WASI_TYPES_H`) を立てた。
2. **ビルドの対象。** WAMR のビルドの対象に emscripten は無い。32 ビット・リトルエンディアン
   なので `X86_32` を名乗らせた。対象ごとのアセンブラの `invokeNative` は使えないので、C の
   版を選ばせてからビルドの一覧から外し、止まる関数に差し替えた (Linux の版も同じにした)。
3. **本体の関数は raw の形だけで登録。** W0 の見立てどおり、raw の形なら引数の型を見て C の
   関数を呼び分ける部分を通らない。差し替えた止まる関数が一度も呼ばれなかったことで確かめた。
   `(*~)` (ポインタと長さ) の指定も raw の形で効き、範囲の検査と本体の番地への変換は WAMR が
   してくれた。
4. **本体のスタックの境界。** 土台の層は境界を返さず (NULL)、本体役が実行環境ごとに
   `wasm_runtime_set_native_stack_boundary` で渡す形にした。emscripten では
   `emscripten_stack_get_end()` (C のスタックは emscripten の線形メモリの中にある)、Linux では
   main の局所変数の番地から 1 MiB 下。溢れの検出が実際に効くかは試していない (8 章)。
5. **リンクの指定。** `-sENVIRONMENT=node -sNODERAWFS=1` (ファイルを node から直接読む)、
   `-sALLOW_MEMORY_GROWTH=1`、`-sSTACK_SIZE=1048576`、`-sEXIT_RUNTIME=1`。スタックの 1 MiB は
   余裕を見た値で、インタプリタが実際に使う量は測っていない。

分岐の表 (labels as values) は既定のまま有効 (emscripten の clang も対応している)。
上流に出ている emscripten 対応の変更は**読んでおらず、使っていない**。

## 4. 土台の層

`host/platform/poc_platform.c` が実装した関数 (両方のビルドで同じソース):

| 関数 | 実装 | 使われ方 |
|---|---|---|
| `bh_platform_init`・`bh_platform_destroy` | 何もしない | 初期化と後片付け |
| `os_malloc`・`os_realloc`・`os_free` | 標準の関数。呼ばれた回数を数える | プール方式では呼ばれない (試験で 0 回) |
| `os_printf`・`os_vprintf` | 標準出力 | ログ |
| `os_time_get_boot_us`・`os_time_thread_cputime_us` | `clock_gettime(CLOCK_MONOTONIC)` | 時刻 |
| `os_self_thread` | 定数 | スレッドなし |
| `os_thread_get_stack_boundary` | NULL を返す | 境界は本体役が実行環境ごとに設定 |
| `os_thread_jit_write_protect_np`・`os_dcache_flush`・`os_icache_flush` | 何もしない | JIT 用 |
| `os_mutex_init`・`destroy`・`lock`・`unlock` | 数を数えるだけ | スレッドなしの試作のため。ファームウェアでは本物が要る |
| `os_cond_init`・`os_cond_destroy` | 何もしない | 使わない部品 (`runtime_timer.c`) がリンクに引き込まれて参照するだけ |
| `os_mmap`・`os_mremap`・`os_munmap`・`os_mprotect` | 2 つ目の固定の領域から WAMR の確保器 (`mem_allocator_*`) で切り出す。伸ばすときは確保器の realloc、新しい部分を 0 で埋める | wasm のメモリの確保と伸長 |
| `os_dumps_proc_mem_info` | -1 を返す | メモリの表示 (使わない) |
| `invokeNative` (別ファイル) | 呼ばれたら止まる | 型を見て呼び分ける部分の代わり |

`platform_internal.h` は、型 (`korp_mutex` など)、`os_getpagesize`、ファイルの番号の型を
定義する。WASI もファイルも使わないので、ファイルの関数は 1 つも要らなかった。

wasm のメモリの出どころ: プール方式でも、WAMR は wasm のメモリをプールではなく
`os_mmap`・`os_mremap` から取る (ソフトの境界検査の構成)。この試作では、起動時に `malloc`
で取った 8 MiB の 2 つ目の領域を、WAMR の確保器で管理して切り出した。つまり起動後は、WAMR の
管理データもアプリのメモリも、起動時に取った 2 つの固定の領域の外から取っていない。W0 で
挙げた `WAMR_BUILD_ALLOC_WITH_USAGE` は使わずに済んだ。同じプールから取るか別の領域にするかは
計画の未確定事項のまま (この試作は別の領域にしただけで、どちらが良いかは決めていない)。

## 5. 止めたアプリを続きから使えるか (A4 の後)

| 確かめたこと | 結果 |
|---|---|
| 止めた後、同じインスタンスで次の `on_update` を呼べるか | 呼べる。例外を消して (`wasm_runtime_clear_exception`) から、残りの 50 フレームを回してチェックサムが期待値と一致 (3 言語・両方のビルド) |
| インスタンスを作り直す必要があるか | この試作の範囲では無かった |
| 止まった呼び出しそのものを続きから再開できるか | できない。止まった呼び出しは捨てられ、次の呼び出しは最初から始まる |
| もう一度 `spin` を呼ぶと | また上限で止まる (上限は呼び出しごとに新しく数え直される。インタプリタは上限を局所変数に写して数えており、残りを実行環境へ書き戻さない: ソースで確認) |
| アプリのスタックの位置 | **止めるたびにずれる。** Zig は `__stack_pointer` を公開しているので読めた: 16384 → 16368。止まった関数が確保したスタックの枠が戻らない |

推測: Rust も `__stack_pointer` を持つ (公開していないので読めなかった) ので、同じくずれるはず。
TinyGo の `spin` はスタックの枠を使わない形にコンパイルされた可能性があり、分からない。
1 回で 16 バイトでも、止めるたびに積もるので、長く動かすと溢れる。また、アプリが自分のヒープを
操作している途中で止めれば、ヒープが壊れた状態で続くことになる (推測)。

対処の選択肢 (決めていない):
- 本体が呼ぶ前にスタックの位置を覚え、止まったら戻す。公開されていない言語では、WAMR の
  内部の構造からグローバルを探す必要がある。
- 止まったアプリは続けず、インスタンスを作り直す (状態は失う)。

## 6. wasm の機能の組

**既定のまま読めなかった組み合わせ** (WAMR を参照型なし・拡張された定数式なしでビルドした
とき。`run.rb features` で再現できる):

| アプリ | 結果 |
|---|---|
| Rust 1.97 既定 (`wasm32-unknown-unknown`) | 読み込み失敗: `WASM module load failed: zero byte expected. The module uses reference types feature which is disabled in the runtime.` |
| Zig 0.17.1 既定 (`wasm32-freestanding`、CPU は `lime1`) | 同じエラー |
| TinyGo 0.44 既定 (`wasm-unknown`) | 読める |

原因は関数の表を通す間接呼び出し (`call_indirect`) の表の番号の符号化で、関数ポインタを
使わない単純なアプリでは出ない (最初の版のアプリは参照型なしでも読めた。`dispatch` を
足して出た)。

読めるようにする方法は 2 通りあり、どちらも動いた:

| 方法 | WAMR の設定 | 各言語の指定 |
|---|---|---|
| (a) WAMR を合わせる (**この試作で採った**) | `WAMR_BUILD_REF_TYPES=1`、`WAMR_BUILD_EXTENDED_CONST_EXPR=1` | 既定のまま (下のスタックの大きさだけ) |
| (b) 言語を合わせる | 両方 0 | Rust: `-C target-cpu=mvp -C target-feature=+bulk-memory,+sign-ext,+nontrapping-fptoint,+mutable-globals,+multivalue`。Zig: `-mcpu=lime1-call_indirect_overlong-extended_const`。TinyGo: 既定 |

- (a) の費用は Linux の版の text で約 17 KB (参照型なし・拡張された定数式なしの版 209,646 →
  226,814 バイト)。
- (b) の Rust で `-C target-feature=-reference-types,-call-indirect-overlong` とする方法も
  読めたが、`call-indirect-overlong` は rustc が「未知で不安定な機能」と警告するので採らない。
  また (b) の Rust は、`lto = true` でコア部分も同じ指定でコンパイルし直されるので効いている。
  LTO を使わないと、配布されたコア部分のコードに古い符号化以外が残るかもしれない (推測、
  試していない)。
- Zig の拡張された定数式は、この試作のアプリでは実際には使われていない (`lime1` から
  `call_indirect_overlong` だけを外した出力も、拡張された定数式なしの WAMR で読めた)。
  (a) では念のため WAMR の側を有効にした。
- (a)・(b) のどちらに固定するかは計画で決める。この試作は (a) で全部の試験を通した。

各言語のコンパイルの指定 (この試作で使ったもの):

| 言語 | 指定 |
|---|---|
| Rust | `cargo build --release --target wasm32-unknown-unknown`、`RUSTFLAGS="-C link-arg=-zstack-size=16384"`、`opt-level="s"`・`lto=true`・`panic="abort"`、`#![no_std]` |
| TinyGo | `tinygo build -target=wasm-unknown -opt=z -no-debug` |
| Zig | `zig build-exe -target wasm32-freestanding -fno-entry -rdynamic -O ReleaseSmall --stack 16384` |

Rust はスタックの大きさを指定しないと、既定の 1 MiB のスタックのせいで wasm のメモリが
初めから 17 ページ (1.06 MiB) になる。指定すると 1 ページ。

## 7. TinyGo の GC

組み込み向けの対象 (`wasm-unknown`) の既定は解放しない GC (`leaking`)。`-gc=conservative`・
`-gc=precise` を選べ、どちらもビルドでき、両方のビルドの本体役で動いた。

4096 バイトの確保を 1000 回繰り返し、毎回前のものを捨てるアプリ (`alloc_churn`) で、
wasm のメモリのページ数:

| GC | 前 | 後 | `.wasm` の大きさ |
|---|---|---|---|
| `leaking` (既定) | 2 | 64 | 3,185 バイト |
| `conservative` | 2 | 2 | 6,146 バイト |
| `precise` | 2 | 2 | 6,501 バイト |

解放しない GC では、捨てた分だけメモリが伸び続ける (4 MB で 64 ページ)。解放する GC なら
伸びない。GC を足した分の大きさは約 3 KB。

## 8. 大きさ

| もの | 大きさ |
|---|---|
| アプリ `.wasm` | Rust 1,334・TinyGo 3,185・Zig 723 バイト |
| 本体役の増分 (Linux、text) | 224,621 バイト (WAMR と本体役の分。比較用の `baseline` は 2,193) |
| 本体役の増分 (emscripten、出力の wasm) | 127,734 バイト (`poc_host.wasm` 141,491、`baseline.wasm` 13,757) |
| プールの最大使用量 (アプリ 1 つ、100 フレーム) | Linux: Rust 25,112・TinyGo 27,736・Zig 24,512 バイト。emscripten: 22,224・24,624・21,624 バイト |
| wasm のメモリ (同じ条件) | Rust・Zig 1 ページ (64 KiB)、TinyGo 2 ページ (128 KiB) |

- プールの使用量には、`.wasm` の写し (プールの中に置いた)、モジュールの解析結果、
  インスタンス、演算のスタック (16 KiB) が入る。大半は演算のスタック。emscripten の方が
  小さいのはポインタが 4 バイトだから (推測)。
- Linux の text の数字は x86-64 を `-O3` でビルドしたもので、RISC-V の ESP32-P4 での
  大きさの目安にはならない。
- wasm のメモリはページ (64 KiB) 単位なので、どんなに小さいアプリでも 64 KiB は使う。
- 気づいたこと: TinyGo のモジュールを外した後、プールの空きが最初より 448 バイト多かった。
  初期化の中で取られた何かが外すときに解放されたと見られるが、追っていない。

## 9. 速さ

いずれも 1 回だけの計測。命令数の上限の仕組みは有効のまま (上限は十分大きく)。

整数の計算だけのループ (`x = x*1103515245+12345; acc += (x>>16)^i` を 100 万回) の時間:

| | Rust | TinyGo | Zig |
|---|---|---|---|
| Linux の版 (WAMR) | 19.7 ms | 22.8 ms | 21.6 ms |
| emscripten の版 (WAMR、node) | 76.8 ms | 83.2 ms | 81.5 ms |
| 同じ計算を C で書いて直接コンパイル (Linux) | 0.82 ms | | |
| 同じ計算を C で書いて直接コンパイル (emscripten、node) | 0.56〜0.78 ms | | |

- WAMR のインタプリタは、C を直接動かすより Linux で約 24〜28 倍遅い。
- emscripten の版のインタプリタは、Linux の版より約 3.6〜3.9 倍遅い。
- 試しに流した別の回では emscripten の Zig が 134 ms になったことがあり、1 回の計測は
  ぶれる。

呼び出しの費用 (10 万回の合計):

| | 本体からアプリの空の関数 | アプリから本体の関数 |
|---|---|---|
| Linux の版 | 1.9〜2.0 ms (1 回 約 20 ns) | 2.7〜2.9 ms (1 回 約 28 ns) |
| emscripten の版 | 2.3〜3.1 ms (1 回 約 23〜31 ns) | 4.3〜4.6 ms (1 回 約 45 ns) |

本体からの呼び出しの数字には、毎回の命令数の上限の設定と関数の検索は入らない (検索は
最初に 1 回)。

終わらないループを 1000 万命令で止めるまでの時間 (別の回、1 回ずつ): Linux で Rust 11.8 ms・
TinyGo 47.2 ms・Zig 11.1 ms、emscripten で 49.1・41.3・42.2 ms。

## 10. 道具の版と置き場所

| 道具 | 版 | 置き場所 |
|---|---|---|
| WAMR | 2.4.5 (タグ `WAMR-2.4.5`、浅い clone) | `tools/wamr_poc/vendor/wamr-2.4.5` (git で無視) |
| emscripten | 6.0.6 | 既存の `~/emsdk` |
| Rust | 1.97.0 | 既存 |
| TinyGo | 0.44.0 (LLVM 22.1.4) | `~/toolchains/tinygo` |
| Go | 1.26.11 (TinyGo が使う) | `~/toolchains/go` |
| Zig | 0.17.1 | `~/toolchains/zig-x86_64-linux-0.17.1` |
| node | 22.23.4 | 既存 |
| gcc | 11.4.0 | 既存 |

- 圧縮ファイルは `~/toolchains/dl` に残してある。
- TinyGo・Go・Zig のキャッシュと設定は、スクリプトが環境変数で `~/toolchains/cache` に
  向ける。最初に手で試したときに `~/.cache` の下 (go-build・tinygo・zig)・`~/go`・
  `~/.config/go` へ書かれてしまったので、どれも今回作られたもの (それより古いファイルが
  無い) と確かめてから消した。
- 道具の版は、計画を立てた時点で W0 が見た版 (Zig 0.15) より新しい。各道具の最新の安定版を
  入れた。

## 11. W0 の見立てと違った点・残った課題

### 違った点・撤回した仮説

| W0 の見立て | 分かったこと |
|---|---|
| emscripten の上で動くか分からない (計画の最大のリスク) | 動いた。raw の形の登録で足り、WAMR のソースを変えずに済んだ。予想していなかった障害は emscripten の WASI のヘッダとの衝突だけ |
| Rust の既定の出力には `WAMR_BUILD_CALL_INDIRECT_OVERLONG` が要る | 2.4.5 にその設定は無い。要るのは参照型 (`WAMR_BUILD_REF_TYPES=1`)。Zig の既定 (`lime1`) も同じ理由で読めない (W0 は Zig については拡張された定数式だけを挙げていた) |
| wasm のメモリを決まった場所に向けるには `WAMR_BUILD_ALLOC_WITH_USAGE` を使う | 不要だった。`os_mmap` を自分で書けば、置き場所を自由に決められる |
| 命令数の上限で止めた後、再開できるかは未確認 | 次の呼び出しはできる。ただし止めるたびにアプリのスタックの位置がずれる (5 章) |
| TinyGo で解放する GC を選べるかは未確認 | 選べる (`conservative`・`precise`)。`wasm-unknown` で動いた |
| (見立て無し) | Rust はスタックの大きさを指定しないと wasm のメモリが 17 ページになる |

### 残った課題

- 止めたアプリのスタックの位置を戻すか、作り直すか (5 章)。
- 本体のスタックの溢れの検出: 境界は渡したが、実際に溢れさせて止まるかは試していない。
  emscripten の版でインタプリタが C のスタックをどれだけ使うかも測っていない。
- 機能の組を (a) WAMR を合わせる・(b) 言語を合わせる のどちらに固定するか (6 章)。
- wasm のメモリを WAMR のプールと同じ領域から取るか、別の領域にするか (4 章)。
- スレッドなしの試作なので、ミューテックスは形だけ。ファームウェアでは `fmrb_hal` の
  本物を使い、複数のタスクから WAMR を使う決まり (実行環境を共有しない、など) を守る必要がある。
- ESP-IDF・RISC-V でのビルド、大きさ、速さ、スタックの使用量は W1b で測る。
- emscripten のリンクの指定 (スタック 1 MiB など) はこの試作のための仮の値。ブラウザ版の
  本体に入れるときは、今のブラウザ版の指定に合わせる必要がある。
