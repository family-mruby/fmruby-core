# W4a (前半) 関数 1 つで全部のビルドを通し、決めることの案を作る

> 親: plan.md の作るもの 2 と段 W4a、指示: instruction_w4a.md | 2026-11-15 | fmruby-core develop。
> 08:40〜09:10。前半はここで止まる (2 章の 3 点と Lua・MicroPython の書き方はユーザが決める)。
> BASIC は生成の対象に入れない (2026-11-15 ユーザ。BASIC は特別なので、案から外した)。

## 1. 関数 1 つ (`fill_rect`) で通した結果

### 1.1 選んだ関数と、作ったもの

`FmrbGfx#fill_rect(x, y, w, h, color)` を選んだ。包みを持たず、mruby・Spinel・Lua・MicroPython の 4 つに
手書きのつなぎがあり (wasm_apps/report/w0.md 1.6)、画面で効き目を見やすい。

| 作ったもの | 場所 | 中身 |
|---|---|---|
| 印 | `sig/fmrb_gfx.rbs` | `def fill_rect` の直前の行に `%a{fmrb.boundary}` を 1 行 (仮の書き方。2.1 で決める) |
| 生成の道具 | `tools/boundary/gen.rb`・`templates/*.erb` | `rbs` gem で読み、印のあるメソッドを中間の表 (平らな名前・引数・説明文) にし、型紙ごとに書く。変わったファイルだけ書き直す。`--check` は SDK が型定義と一致するかだけを見る |
| rake | `rakelib/boundary.rake` | `boundary:gen`・`boundary:check`。`setup` (全ビルドの入口) の最初・`spinel:gen`・`micropython:gen` が `boundary:gen` を呼ぶ。`rake test` に `boundary:check` |
| 平らな関数の本体 (手書き) | `main/app/fmrb_boundary_gfx.c` | `fmrb_gfx_fill_rect(gfx, x, y, w, h, color)`。どのエンジンも知らない。常にビルドする (Spinel の殻 `fmrb_spx_gfx.c` は Spinel の構成でしかビルドしないので、そこには置かない) |
| 受け手の番号 (手書き) | `components/fmrb_common/include/fmrb_boundary_recv.h` | 1.3 の仮の形 |

生成するもの (本体の中のものは毎回作り、リポジトリに入れない。`.gitignore` 済み):

| 出力 | 場所 | 使う所 |
|---|---|---|
| 平らな関数のヘッダ (stdint だけ) | `components/fmrb_common/include/fmrb_gen/fmrb_boundary.h` | 下の全部 |
| mruby のメソッド + 登録のマクロ | `fmrb_gen/fmrb_boundary_mrb.inc` | gem の `ports/esp32/gfx.c` が include |
| Lua の関数 + `luaL_Reg` の行 | `fmrb_gen/fmrb_boundary_lua.inc` | `components/lua/extension/fmrb_lua_gfx.c` が include |
| MicroPython の関数オブジェクト + 表の行 | `fmrb_gen/fmrb_boundary_mp.inc` | `components/micropython/modules/fmrb_module.c` が include |
| WAMR の raw の関数 + `NativeSymbol` の行 | `fmrb_gen/fmrb_boundary_wasm.inc` | 今は W1a の試作の本体役だけ |
| Spinel の `ffi_func` | `main/ruby/spinel/fmrb_gen/fmrb_boundary_ffi.rb` (`module FmrbBoundary`) | `tools/spinel/gen_app_combined.rb` が結合の部品に足す |
| C のヘッダ (SDK、**コミットする**) | `sdk/c/fmrb_boundary.h` | wasm のアプリ。説明文 (日本語・英語)・ABI の版の定数・関数の一覧のハッシュ・`import_module("fmrb")` |

置き換えた手書きのつなぎ (fill_rect の分だけ): gem の `mrb_gfx_fill_rect`、Lua の `lua_gfx_fill_rect`、
MicroPython の `gfx_fill_rect`、Spinel の `fmrb_spx_gfx_fill_rect` (C の本体・ヘッダ・`ffi_func`)。
Ruby・Python・Lua から見た名前と引数は変わらない (`_fmrb.gfx_fill_rect` の名前も同じなので、MicroPython
の qstr の表は作り直さずに済んだ)。

### 1.2 確認したこと

| 確認 | 結果 |
|---|---|
| `rake regress` | ホスト 29 件・描画の記録 229 件、全部通った |
| Linux (`rake clean && rake build:linux`、x86-64) | 通った。1 回目は生成物の先頭の注釈に `sig/*.rbs` と書いたため `/*` の入れ子で `-Werror=comment` になり、直した |
| sim で呼ぶ所 | 描画の記録は Ruby の層しか通らず C のつなぎを通らないので、sim で見た。mruby (デスクトップの描画、枠つきの `pub_demo` のボタン = 原点を足す経路)、Lua (`lua.app.lua` の行の消し = 原点を足す経路、数秒動かしても消し残りなし)、MicroPython (`python.app.py` の区画の塗り)、Spinel (エディタを開いて 1 打鍵、メニューと状態の行) がどれも正しい位置に出た |
| ネイティブ版 (`rake native:mruby native:core`) | 通った |
| ブラウザ版 (`spinel:gen` → `wasm:mruby` → `wasm:dist`) | 通った |
| esp32 (S3・P4、`rake regress:size`) | 末尾の 1.6 |
| wasm の側 (W1a の試作の本体役) | 通った (1.4) |

### 1.3 原点の扱い (仮の形。W4b で置き換える)

`fill_rect` 1 つでも、描画の原点 (決定 17) の問題に当たった。原点を持つ場所がエンジンで違う:

| エンジン | 原点 |
|---|---|
| mruby・Lua | C の描き手の構造体 (`mrb_gfx_data`・`lua_gfx_data` の `ox, oy`) が持ち、つなぎが足す。同じ canvas に原点の違う描き手が 2 つある (`@gfx` と `@window_gfx`) |
| Spinel・MicroPython | 包み (Ruby・Python) が `@dx`・`self.dx` を足してから平らな関数を呼ぶ |

生成したつなぎは、どの引数が座標かを知らない (知るには型に印か別名が要り、決定 17 の向きに反する)。
そこで、受け手の番号の取り方だけをエンジンごとの手書きのフック (`fmrb_mrb_recv_gfx`・`fmrb_lua_recv_gfx`)
にし、

- 番号が 0 以上なら canvas の番号、座標は canvas の座標 (Spinel・MicroPython・wasm が今渡しているもの)。
- mruby・Lua のフックは、原点が (0, 0) でなければ、その呼び出しの間だけ「canvas と原点」をアプリの task の
  文脈 (`fmrb_app_task_context_t`、esp32 では PSRAM の表) に置き、特別な番号 `FMRB_GFX_RECV_BOUND` を渡す。
  平らな関数はそれを読んで原点を足す。

とした。生成したつなぎと平らな関数の形は、W4b で原点を境界の下へ移しても変わらない (変わるのはフックと
番号の解き方だけ)。W4b では「番号 = 描き手」の表 (原点・描く先の画像を持つ) を C に作り、この仮の形を消す。
その表をどこに置くか (アプリの文脈か canvas の表か) は W4b の設計で決める。

### 1.4 wasm の側

W1a の試作 (`tools/wamr_poc/`) に、`ruby tools/wamr_poc/run.rb boundary` を足した。

- 生成した `fmrb_boundary_wasm.inc` を本体役 (`host.c`) に include し、`"fmrb"` の名前で登録する。
  平らな関数の本体は本体役が 64x64 の canvas に塗る形で持つ。
- C で書いた最初のアプリ (`apps/c_boundary/app.c`) は `sdk/c/fmrb_boundary.h` だけを include し、
  `fmrb_gfx_fill_rect` を毎フレーム 8 回呼ぶ。`zig cc -target wasm32-freestanding` で 1,850 バイト。
- 100 フレーム後の canvas のハッシュが、同じ絵を C で直接描いた値と一致した (Linux x86-64 と emscripten
  の両方、`4b90f47e`)。

### 1.5 PicoRuby のビルドとの相性

| 点 | 分かったこと |
|---|---|
| 生成した C の置き場 | 新しい `.c` を足さず、**既存の C のファイルが `.inc` を include する**形にした。CMake の glob (gem の `ports/esp32/*.c`) を作り直す必要が無く、コンパイラの依存の記録 (`-MD`) で `.inc` の変化が拾われる。置き場は `fmrb_common/include/fmrb_gen/`。全部の部品・`hosted.cmake`・checkpoint の表を作る道具がこの include の道を既に持っていたので、CMake は平らな関数の本体 1 つを足しただけ (`main/CMakeLists.txt`・`hosted/hosted.cmake`) |
| libmruby (picoruby の rake) | 触らない。生成物は CMake の側の C だけで、mrblib は変えていない |
| ビルドの入口が複数 | `setup` (Linux・esp32・ネイティブ版・ブラウザ版・checkpoint の表の全部が通る)、単独で流す `spinel:gen`、`micropython:gen` の 3 つ。3 つとも最初に `boundary:gen` を呼ぶようにした。`rake test` は生成しない (`boundary:check` は SDK だけを見る) |
| 生成の順 | `boundary:gen` → gem の写し → Spinel の結合 (`FmrbBoundary` の部品が要る) → checkpoint の表。`micropython:gen` は qstr の抜き出しが生成した `.inc` を前処理するので、その前に生成が要る (`micropython.mk` に include の道を足した) |
| MicroPython の qstr | **関数を足すと `micropython:gen` を流して `mp_embed/` をコミットし直す必要がある** (qstr の表はコミットされた生成物)。生成物をリポジトリに入れない決まりと、ここだけ食い違う。名前の同じ関数を置き換える間は不要。後半で、qstr の表だけを毎回作る形にできるか見る |
| checkpoint の表 | mruby のメソッドの C の関数が変わっても、表はビルドのたびに作り直され、ホストで作る表の道具は未解決の名前を代わりのもので埋める (`fmrb_gfx_fill_rect` もそうなった)。メソッドの名前と数は変えていない |
| Spinel | `ffi_func` を宣言すると、その関数がアプリのプログラムごとにリンクされる。平らな関数の本体は常にビルドするので問題ない。Spinel の殻の C (`fmrb_spx_gfx.c`) は Spinel の構成でしかビルドされないので、平らな関数の本体はそこに置けない |
| 型 `int32_t` と `int` | Spinel の生成した C は `int` で宣言する。esp32 では `int32_t` が `long` なので、同じ翻訳単位で両方の宣言を見ると型の食い違いになる。今は別の単位なので問題ない (ABI は同じ) |
| 作り直しの罠 | 新しい罠は無い。sig を直したら `rake clean` が要るのは型データベースの既存の罠のまま (生成したつなぎは毎回のビルドで新しくなる) |

### 1.6 内蔵 RAM とファームの大きさ

`rake regress:size` (S3 = NARYAv3・P4 = TAB5、6.5 分) は通った。基準 (4e7e7e61) との差:

| 端末 | DIRAM | IRAM | その他の内蔵 | Spinel の単位の静的な内蔵 RAM |
|---|---|---|---|---|
| NARYAv3 | 125,403 (-56) | 16,384 (+0) | 104 (+0) | editor 92・kernel 128 (変化なし) |
| TAB5 | 129,338 (-360) | - | 948 (+0) | 同上 |

内蔵 RAM の静的な増分は 0 (減ったのは基準より後の別のコミットの分)。アプリの文脈に足した 8 バイト
(`gfx_bound_*`) は esp32 では PSRAM の表 (`g_ctx_pool`、`FMRB_EXT_RAM_BSS_ATTR`) に乗る。ファームの
大きさの差 (S3 +274 KB、P4 +779 KB) は基準が古いためで、この変更の分は `fill_rect` のつなぎ 4 つを
1 つの本体と生成したつなぎに替えただけなので、ほぼ同じ大きさ。

## 2. 決めることの案

### 2.1 印の書き方

確かめたこと: RBS の注釈 `%a{...}` は、`rbs` gem 4.1.3 で読め (メソッド・オーバーロード・クラスのどれに
付けても取れる)、**型データベースを作る道具 (tidbgen) の出力は印の有無でバイト単位で同じ**、F1 の長文
ヘルプ (`tools/ti/gen_help.rb`) の出力も同じだった。説明文 (`# ... <<en>> ...`) は注釈を挟んでも
メソッドに付いたまま。注意: `rakelib/fmrb_system.rake` は `^\s*def self\.` を正規表現で数えるので、
`def` と同じ行に印を書くと数え漏れる。印は `def` の前の行に置く。

| 案 | 書き方 | RBS として | 独自の記法 |
|---|---|---|---|
| **A (推奨)** | メソッドごとに `%a{fmrb.boundary}` を `def` の前の行に 1 行 | RBS の正規の注釈。Steep なども同じ仕組みで `%a{pure}` などを使う | 増えない。`fmrb.` で他の道具の注釈と衝突しない |
| B | クラスに `%a{fmrb.boundary}` を付けて全メソッドを載せ、載せないものに `%a{fmrb.internal}` | 同上 | 増えない。ただし印の無いメソッドが暗黙に載るので、FmrbGfx のように Ruby で書かれたメソッド (`fit_text` など) が混ざるクラスで誤って載せやすい |
| C | 境界のものだけを別のファイル (`sig/boundary/*.rbs`) に書き、クラスを開き直す | RBS はクラスの開き直しを許す | 記法は増えないが、1 つのクラスの説明が 2 か所に分かれる。エディタの型データベースが開き直しを正しく合わせるかは未確認 |
| D | 説明文の中の目印 (`# @boundary`) | RBS から見ればただのコメント | 独自の記法になる。採らない |

### 2.2 境界の関数の名前

前提: 包みを持たない関数 (`fill_rect`・`present` など) は、境界の関数がそのまま公開の名前なので名前は
変わらない (W4a の後半はこれが中心)。名前を決める要があるのは、包みを持つ関数 (`draw_text`・`draw_image`
など。今は `_draw_text` のように隠れている) が W4b で表に出るとき。

| 案 | Ruby | 他の言語 (Lua / C / Python) | 良い所 | 気になる所 |
|---|---|---|---|---|
| **A (推奨)** 後ろに `_raw` | `draw_text_raw(x, y, str, color, bg, flags)` と包みの `draw_text` | 同じ名前 (`gfx:draw_text_raw`・`fmrb_gfx_draw_text_raw`) | 全言語で同じ名前。エディタの一覧で包みの隣に並ぶ。「省略の無い生の形」が語から分かる。Rust・Go の慣習 (`raw`) とも合う | 包みの無い言語でも接尾辞が付く。「検査が無い」と読まれるかもしれない (説明文で補う) |
| B 他の言語は素の名前、Ruby だけ `_raw` | `draw_text_raw` | `draw_text` | 他の言語で自然 | 言語で名前が変わる。C のヘッダ (基準の資料) と Ruby の対応を説明で引く必要がある。道具が「包みがあるか」を知る必要がある |
| C 後ろに `_ex` | `draw_text_ex` | 同じ | Win32 などで見慣れた | 「機能を足した版」の意味に読まれやすく、実際は逆 (省略が無いだけ) |
| D 名前空間を分ける | `@gfx.raw.draw_text` | 素の名前 | 名前が変わらない | Ruby では 1 段深くなり、速さのために直接呼ぶという目的に反する。エディタでは別のクラスに出る |

`_` で始める今の形は、計画の方針 (公開の API としてエディタに出す) により候補に入れない。

### 2.3 フック・頼みごと・内側の区別

確かめたこと: RBS の `interface` (`interface _FmrbAppHooks ... end`) は、tidbgen が**読み飛ばす** (型データ
ベースが同じだった)。

| 案 | 書き方 | 良い所 | 気になる所 |
|---|---|---|---|
| **A (推奨)** 印で分ける | 頼みごと = `%a{fmrb.boundary}` (境界の関数と同じ)、フック = `%a{fmrb.hook}`、内側 = 印なし (今のまま `fmrb_app.rbs` に書いてよい) | 1 つのクラス・1 つのファイルのまま。Ruby のアプリの書き方とエディタの補完 (`on_create` など) が変わらない。新しい記法は 2.1 と同じ注釈の値が 1 つ増えるだけ | 印を付け忘れると「内側」扱いになる (道具は FmrbApp の `on_` で始まる印なしを警告できる) |
| B フックを `interface` に分ける | `interface _FmrbAppHooks` にフックを書き、`class FmrbApp` は `include _FmrbAppHooks` | 「アプリが実装する関数の一覧」は RBS の interface の本来の使い方で、Rust の trait・Python の基底に素直に写る | tidbgen が interface を読まないので、エディタから `on_create` などの説明が消える。tidbgen を直すまで採れない |
| C 名前空間・ファイルを分ける | `FmrbApp::Hooks` など | 道具が見分けやすい | Ruby のアプリから見た型の置き場が変わり、エディタの補完の出方が変わる。RBS として不自然 |

### 2.4 BASIC

対象に入れない (2026-11-15 ユーザが決めた。案から外した)。

### 2.5 生成に切り替えると Lua・MicroPython の書き方が変わる所

生成した関数は、Ruby の境界の形 (省略なし・引数の順と名前は型定義のもの) になる。今の手書きとの差:

| 言語 | 所 | 今 | 生成した形 | 案 (推奨を先に) |
|---|---|---|---|---|
| Lua | `gfx:draw_text` の順 | `(text, x, y, c [, bg])` | Ruby と同じ `(x, y, text, color, ...)` | **A: Ruby の順に揃える** (Ruby が中心。Python は既に Ruby の順。直す Lua のアプリは 2 本で 6 か所)。B: Lua だけ今の順を残す (生成の外の手書きが 1 つ残る) |
| Lua | 省略できる引数 (`draw_text` の `bg`、`FmrbApp.clear_user_area([c])`) | 省略できる | 省略できない (境界の形) | **A: Lua も Ruby の包みに当たるものを持つ** (小さな Lua の前置き、MicroPython の prelude と同じ形。省略・既定値はそこで書く)。B: 省略をやめ、「透明」「テーマの色」などの定数を渡させる |
| Lua | 区画と Window の大きさ (`FmrbApp.user_area_width`・`WINDOW_WIDTH`) | 表のフィールド (殻が大きさの変化で書き直す) | 境界の関数にするなら `FmrbApp.user_area_width()` の呼び出し | **A: 関数の呼び出しに変える** (値がいつも今のものになる。Ruby の `@user_area_width` とは形が違うままだが、他の言語と揃う)。B: フィールドのまま残す (殻の書き直しが続く) |
| Lua | 色とテーマの定数 (`FmrbGfx.BLACK`・`FmrbApp.THEME_*`) | 起動時に入れた値 | 型定義の定数から生成するか未定 | 後半で決める (定数の生成は今回の範囲外) |
| MicroPython | `_fmrb.gfx_*` (prelude が呼ぶ低い層) | 位置の引数、受け手の番号が先頭 | 同じ (名前も同じ規則 `gfx_fill_rect`) | 変わらない |
| MicroPython | prelude の `FmrbGfx` の省略できる引数 | `clear(color=BLACK)`・`present(x=None, y=None)`・`draw_text(..., bg_color=None, mixed=False)`・`draw_image(id, x=0, y=0, scale_x=1.0, ...)` | 型定義には既定の**値**が書けない (RBS は「省略できる」までしか書けない) | **A: prelude の Python のクラスは、包みを持つメソッドだけ手書きで残し、包みの無いメソッドは生成する** (Ruby の mrblib と同じ 2 段)。B: 既定値を印で書く (独自の記法が増えるので勧めない) |
| MicroPython | 引数の名前 (Python はキーワードで呼べる) | `draw_text(x, y, text, ...)` | 型定義の名前 (`str`) | **A: 型定義の名前を Python でも自然な名前に直す** (`str` → `text`。Ruby のエディタの表示も読みやすくなる)。B: 生成の側で名前を言語ごとに差し替える |
| MicroPython | 原点 | prelude が `self.dx` を足す | 同じ (W4b で C へ) | 決定 17 のとおり W4b |

`draw_text` の順は、Lua 以外 (mruby・Spinel・MicroPython) が既に `(x, y, text, color)` で揃って
いる。Lua だけが違う。

## 3. コミット

- 6b46d8a0 feat(boundary): generate fill_rect's glue for every engine from sig/
- c01ae4c1 test(wamr_poc): a C app calls the generated boundary function
- この report と plan.md の状態の更新 (docs)

## 4. 時刻

開始 08:40、終了 09:10 (`date`)。ビルドと確認: Linux 2 回 (clean から 1 分強)、sim 約 2 分、ネイティブ版 1 分、
wasm の側 13 秒、ブラウザ版 4 分、`regress:size` 6.5 分。
