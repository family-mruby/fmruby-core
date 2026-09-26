# 新しい Spinel gem を追加する手順

ホットな純計算の Ruby を Spinel で native 化し、mruby アプリから gem として繰り返し
呼べるようにするための手順です。Spinel 側は ext プログラム(`spinel --ext-init` /
`--ext-entry`)として生成し、C から型付きの関数として呼びます(経緯は
`doc/spinel_upstream_ext/report/p3.md`)。Ruby の書き方は `ruby_writing_constraints.md` を
参照してください。

## いつ使うか
- 向いているのは、呼び出し回数が多い、または 1 回が重い純計算です(信号処理・行列・
  画像処理など)。
- 向いていないのは、軽い処理(`:ruby` のままで十分です)、I/O が主体の処理、アプリ
  全体を Spinel 化できる場合です。

## 記号(SpinelHello を例に)
最小の実物 `picoruby-fmrb-spinel-hello` を手本にします(`Fmrb::SpinelHello.new.greet("world")`
が Spinel-AOT した Ruby で "Hello world!" を返す gem)。名前(入力)は entry の引数、
挨拶文(出力)は戻り値として境界を渡るので、入出力のつなぎ方が一度に分かります。
- `<gem>` … gem 名です。例: `spinel-hello`(dir は `lib/add/picoruby-fmrb-spinel-hello/`、
  gembox 名は `picoruby-fmrb-spinel-hello`、init は `src/picoruby_fmrb_spinel_hello.c`)。
- `<core>` … アルゴリズム本体のファイル/クラス名です。例: `spinel_hello_core.rb` /
  `SpinelHelloCore`。
- `<x>` … gem を識別できる短い接頭辞です。binding・kernel・受け皿の名前に使います。例:
  `spinel_hello` → `spinel_hello_binding.c` / `spinel_hello_kernel.rb` /
  `spinel_hello_native.c`。

## 置き場所の方針
gem のソースはすべて gem ディレクトリ(`lib/add/picoruby-<gem>/`)配下にまとめます。
picoruby-fmrb-spinel-hello がこの構成の最小の手本です。
- `mrblib/` には Ruby(アルゴリズム core と、それを呼ぶ gem の API)を置きます。`ports/esp32/` には
  mrbgem の binding を、`src/` には init を置きます。ここまでは picoruby-esp32(mrbgem)が
  コンパイルします。
- `native/` には mrb 非依存のファーム C(Spinel の受け皿)を置きます。これは main が
  パス参照でコンパイルします。パスはnative以外も可ですが、**`src/` でも `ports/` でもない名前**にしてください
  (mrbgem ビルドが `src/*.c` と `ports/<port>/*.c` を拾うので、そこに置くと二重
  コンパイルになります)。
- `spinel/` には Spinel の kernel(`<x>_kernel.rb`)を置きます。`rake spinel:gen` がこれと
  `mrblib/` の core を `prebuild_scripts/spinel/` へコピー(ステージ)してからコンパイルし、
  `gen/<x>_kernel.c` と契約ヘッダ `gen/<x>_kernel.h` を出します(ステージ先は gitignore で、
  gem が原本です)。

この構成なら、開発者が編集する場所は gem ひとつだけになります。`main/kernel/` には
置きません。

## 作るファイル
すべて `lib/add/picoruby-<gem>/` 配下です。
- `mrblib/<core>.rb` … アルゴリズム本体(I/O を持たない)。`:spinel` はこれを native 化します。
- `mrblib/<gem>.rb` … gem の Ruby API(`Fmrb::X.new(...)` で Spinel の呼び出しを包む)。
- `ports/esp32/<x>_binding.c` … mruby binding(受け皿の available? / begin / run / end を
  Ruby へ公開。SpinelHello では `SpinelHelloNative.available?` / `begin_instance` /
  `greet` / `end_instance`)。
- `src/picoruby_<gem>.c` … init。無いと init が登録されず `NameError` になります。
- `mrbgem.rake`。
- `native/<x>_native.c` / `.h` … Spinel の受け皿(インスタンスの生成・init・entry の
  呼び出し・破棄)。
- `spinel/<x>_kernel.rb` … kernel。モジュールの特異メソッドが entry になり、top-level は
  init で 1 回だけ走ります。

配線を足す既存ファイルは次のとおりです。
- `lib/add/family_mruby.gembox` … gem を登録します。
- `rakelib/setup.rake` … gem を submodule へ丸ごとコピーします。
- `components/picoruby-esp32/CMakeLists.txt` … binding を PICORUBY_SRCS に加えます。
- `main/CMakeLists.txt` … `native/*.c` と生成 C を COMPONENT_SRCS へパス参照で加え、
  `native` を INCLUDE_DIRS に足し、`FMRB_ANY_SPINEL` を true にします(Spinel 固定なので
  無条件で含めます)。生成 C と受け皿の両方に生成プログラムのフラグ
  (`SPINEL_GEN_TU_FLAGS`、受け皿はさらに `gen/` を include path に)を付けます。
  `generate_ruby_spinel_command(<kernel>.rb GEM ...)` を呼びます。
- `wasm/CMakeLists.txt` … 生成 C と受け皿を同じく加え、同じフラグを付けます。
- `rakelib/spinel.rake` … `gem.call(kernel, core...)` の 1 行を足します(staging と生成を
  します)。

## 手順

### 1. アルゴリズムを Ruby で書く(`mrblib/<core>.rb`)
- 何のために: `:spinel` はこの Ruby をそのまま native 化します。ここが速くしたい計算の
  本体です。
- どう書くか: 入出力は引数と戻り値だけにし、I/O を持たせません。
  `ruby_writing_constraints.md` の制約(`while` を優先、block 呼び出しを避ける、要素の
  入れ替えは一時変数で、並列代入を使わない、など)を守ってください。

### 2. Ruby API と binding を用意する
- 何のために: アプリが `Fmrb::X.new(...).forward(...)` の形で Spinel の計算を呼べるように
  します。
- 何を書くか: `mrblib/<gem>.rb`(Spinel の呼び出しを包む gem の API)、
  `ports/esp32/<x>_binding.c`(`XNative.spinel_run` などを C で定義する binding)、
  `src/picoruby_<gem>.c`(init)。
- どう書くか: API では `spinel_open` / `spinel_close` を参照カウントで呼び、インスタンスの
  begin/end が 1 回ずつになるようにします。

### 3. Spinel の kernel と受け皿を書く(ここが本体)
- 何のために: kernel(Ruby)を native 化し、mruby タスクから C 関数のように呼び出します。
- kernel `spinel/<x>_kernel.rb` の書き方:
  - 冒頭に `# spinel-ext-init: Init_<x>` と `# spinel-ext-entry: Mod.meth,...` の 2 行を
    書きます。rake と CMake はこの 2 行から init 名と entry を読みます。
  - entry は `module Mod; def self.meth(args)` の形にします。生成ヘッダには実際の C 型で
    宣言されます(Integer は `sp_int`、String は `const char *`、Float は `sp_float`)。
    SpinelHello は `SpinelHelloKernel.greet(name)` が String を受けて String を返します。
  - 引数の型は末尾の `if __FILE__ == $0` ブロックの呼び出しから推論されます。entry を
    1 回ずつ呼ぶ型推論用のブロックを必ず書きます(init では走りません)。
  - core は `require_relative` で読みます。
  - 呼び出しをまたいで残したい物(前処理の重いオブジェクト)は、モジュールの
    インスタンス変数(`@core`)に置きます。top-level は init で 1 回しか走らないので、
    インスタンスが生きている間は残ります。新しいインスタンスの init は nil から始めます。
    重い例は FFT と Raycast を参照。
  - 1 つの変数に 2 つのクラスを入れないでください(型推論が untyped に広がります)。
- 受け皿 `native/<x>_native.c` の書き方:
  - 生成ヘッダ `<x>_kernel.h` を include します。
  - インスタンスを作った直後に `Init_<x>()` を 1 回呼びます。
  - **init と entry の呼び出しはすべて `Init_<x>_try` で包みます**。包まずに Ruby が
    raise するとファームごと落ちます。コストは setjmp 1 回で、測って見えない程度です。
  - String の引数は `sp_str_from_bytes(buf, len)` で作ります(Spinel の文字列は長さを
    前のヘッダに持つので、素の C バッファは渡せません)。戻りの String の長さは
    `sp_str_byte_len` で読み、次の呼び出しまでに写します。
  - `fmrb_spinel_instance_begin` / `fmrb_spinel_instance_end`(`fmrb_spinel_host.h`)で、
    インスタンスを begin は 1 回、run は複数回、end は 1 回、という寿命で扱います。
  - プールは `fmrb_sys_malloc`(= PSRAM。内蔵 RAM は消費しません)で確保します。小さめで
    よいですが**下限があり、16KB では instance 生成に失敗します**。128KB 程度(SpinelHello と
    Raycast は 128KB)を取ってください。
  - この呼び方は**シングルトン(1 TU = 1 インスタンス = 1 タスク)**が前提です。複数の
    タスクから同じ entry を使わないでください。
  - 受け皿は常にコンパイルされるので、スタブと実体を分ける `#ifdef`(FFT の
    `FMRB_FFT_SPINEL` のようなもの)は不要です。
  - 受け皿は生成プログラムと同じフラグ(`-w` を含む)でコンパイルされます。

### 4. ビルド配線を足す
- 何のために: gem のファイルをビルドに認識させます。
- どうするか: 上の「配線を足す既存ファイル」を編集します。`native/*.c` は main の
  COMPONENT_SRCS にパス参照で加え、`native` を INCLUDE_DIRS に足し、`FMRB_ANY_SPINEL` を
  true にします。`spinel/*.rb` は `rakelib/spinel.rake` で staging してから生成します。
  初回だけ `rake spinel:setup` を実行してください(以降 `rake spinel:gen` はビルド時に
  自動で走ります)。

### 5. 実機で確認する
- 何のために: native 化した Spinel の結果が正しいか、そして速くなったかを確認します。
- どう確認するか: `rake clean_all && rake build:esp32 && FLASH_BAUD=115200 rake flash` で
  焼き、既知の入力に対して結果が期待どおり(許容誤差内)であることを確認します。
  `core.rb` を変更したときは Spinel の再生成が必要です。

## 落とし穴
- **gem を gembox に足したら `rake clean`(できれば `rake clean_all`)を実行**して
  ください。しないと mruby の `gem_init.c` が再生成されず、C 側(binding/native)は
  ビルドが通るのに **gem の mrblib と init が未登録**のままになり、実機で
  `NameError: uninitialized constant Fmrb::X` になります(実際に踏んだ無言の穴)。
- gem のクラス内から top-level の `XNative` を呼ぶときは **`::XNative`** と書きます。
  bare の `XNative` は `Fmrb::X::XNative` として探されて `NameError` になります。
- **Spinel インスタンスのプールには下限があります**。前処理が軽くても、インスタンス
  生成そのものに基盤ヒープ(class 表・intern 済みシンボル・文字列ヒープ)が要るため、
  16KB では `fmrb_spinel_instance_begin` が NULL(begin が -3)になります。128KB 程度
  (Hello と Raycast は 128KB)を確保してください。プールは `fmrb_sys_malloc` = PSRAM から取られ、
  内蔵 RAM は消費しません。
- `src/picoruby_<gem>.c` を置き忘れると `NameError` になります。
- entry を `Init_<x>_try` で包み忘れると、Ruby の raise でファームが落ちます。
- 1 つの変数に 2 つのクラスを入れないでください(型推論が untyped に広がります)。
  クラスごとに別のモジュール変数にします。
- キャッシュは kernel 側にだけ書き、`core.rb` には書かないでください。
- `rake spinel:doctor` は gem の kernel に behavior の ERR を出します(型推論用の
  `if __FILE__ == $0` ブロックが CRuby では走り、生成物では走らないため)。許可表で
  除外してあります。
- linux と esp32 を切り替えるときは `rake clean_all` を実行し、`file build/*.elf` で
  ターゲットを確認します。
- vendor/spinel(コンパイラ)を変えたときは
  `ruby components/fmrb_spinel_rt/import_from_fork.rb vendor/spinel` と `SPINEL_PIN` を
  合わせ、fork も push してください。

## 手本
- **最小の手本 = picoruby-fmrb-spinel-hello**。全ソースが
  `lib/add/picoruby-fmrb-spinel-hello/` 配下(`mrblib/` `ports/esp32/` `src/` `native/`
  `spinel/`)に集約され、このドキュメントの各手順とそのまま対応します。まずはこれを
  真似てください。
- **応用の手本 = picoruby-fmrb-fft / picoruby-fmrb-raycast**。モジュール変数による
  状態保持、複数の entry(FFT は load / run / magnitudes に分けて変換だけを計時)、C 実装
  との比較など、SpinelHello に無い要素はこちらを参照。
- 配線はどちらも `main/CMakeLists.txt`(`native/*.c` のパス参照と `native` を
  INCLUDE_DIRS)と `rakelib/spinel.rake`(`spinel/` と `mrblib/` の core を staging して
  生成)を参照してください。
