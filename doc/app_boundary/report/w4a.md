# W4a 関数 1 つで全部のビルドを通し、規則と生成の道具を作り込む

> 親: plan.md の作るもの 2 と段 W4a、指示: instruction_w4a.md (前半)・instruction_w4a_2.md (後半) |
> 2026-11-15 | fmruby-core develop。前半 08:40〜09:10 (1・2 章)、後半 09:16〜10:22 (3 章以降)。
> 前半の決めること (2 章) はユーザが決めた (plan.md の「W4a で決めたこと」)。
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

## 3. 後半: 印と規則を道具に入れる

### 3.1 印 (決めた形)

- `sig/fmrb_gfx.rbs` の `class FmrbGfx` の前に `%a{fmrb.boundary}`。前半のメソッドごとの印 (`fill_rect`) は消した。
  メソッドに `%a{fmrb.boundary}` が残っていると、道具はメソッドの名前を出して止まる (「クラスに付ける」)。
- 載せないメソッドに `%a{fmrb.internal}` (27 個、3.4)。フックは `%a{fmrb.hook}` で集めて SDK のヘッダに一覧を
  出すところまで (FmrbGfx にはフックが無い。FmrbApp は W4b)。
- 道具は、載せたメソッドの C の実体 (`main/app/fmrb_boundary_*.c` の `int32_t fmrb_<クラス>_<メソッド>(`) を探し、
  無いものを全部並べて止まる。本体を書く前の最初の生成では 40 個が並んだ。Ruby で書いたメソッドを載せかけたら
  ここで止まる。

### 3.2 道具が扱う規則 (`tools/boundary/gen.rb` の冒頭にも同じ表)

| 型定義 | 平らな関数 | mruby | Lua | MicroPython | wasm | Spinel |
|---|---|---|---|---|---|---|
| インスタンスメソッド | 第 1 引数 `int32_t <クラス>` (受け手の番号) | フック `fmrb_mrb_recv_<名>` | フック `fmrb_lua_recv_<名>` | フック `fmrb_mp_recv_<名>` (番号か、オブジェクトの `canvas_id`・`dx`・`dy`) | そのまま | そのまま |
| クラスメソッド (`def self.x`) | 受け手なし | クラスメソッド | 大域の表の関数 (`FmrbGfx.rgb_to_332`) | `@staticmethod` | 同じ | 同じ |
| `Integer` / `int8`〜`uint64` | `int32_t` / その幅 | `i` | `luaL_checkinteger` | `mp_obj_get_int` | i32 / i64 | `:int32` など |
| `bool` | `int32_t` 0・1 | `b` | `lua_toboolean` | `mp_obj_is_true` | i32 | `:int32` |
| `Float` / `float32`・`float64` | `double` / `float` | `f` | `luaL_checknumber` | `mp_obj_get_float` | f64 / f32 | `:double` / `:float` |
| `String` / `bin` | ポインタと長さ (`_len`) | `s` | `luaL_checklstring` | `mp_obj_str_get_data` (str と bytes) | 長さで確かめたオフセット | `:str` と `:int32` |
| `:a \| :b` (Symbol の選択肢) | `int32_t` (並びの番号。`FMRB_<関数>_<引数>_<名>` の定数) | Symbol | 文字列 (`luaL_checkoption`) | 名前の str か番号 | i32 | `:int32` |
| 記録 (record 型の別名) | packed の構造体へのポインタ (`fmrb_<クラス>_<名>_t`) | Hash (Symbol の鍵) | 表 | dict | 大きさで確かめたオフセット | 詰めたバイト列 `:str` |
| `Array[記録 \| 幅の整数]` | ポインタと個数 (`_count`) | Array (32 個まで) | 表 (32 個まで) | list (32 個まで) | 個数×大きさで確かめる | `:str` と `:int32` |
| 戻り値 自分 (`-> FmrbGfx`) | 状態 (0 以上) | self | 受け手 | None (Python のクラスは self) | i32 | i32 |
| 戻り値 `void` | 状態 | nil | なし | None | i32 | i32 |
| 戻り値 `Integer` / `bool` / 選択肢 | 値 (負はエラー) | Integer / true・false / Symbol | 同じ | 同じ | i32 | i32 |
| 戻り値 記録 (`記録?`) | 呼ぶ側の構造体 `out`。1 で埋めた、0 で無し (nil) | Hash か nil | 表か nil | dict か None | out のオフセット | 適合 `fmrb_spx_b_*` が `:binstr` で返す |
| 戻り値 `String` / `bin` | 呼ぶ側のバッファ `out`・`out_size`。全体の長さを返す (snprintf と同じ) | 128 バイトで足りなければ取り直す | `luaL_Buffer` | `vstr` | out のオフセット | 適合が 256 バイトまで |
| エラー | -1 失敗、-2 引数が範囲外 | RuntimeError / ArgumentError | Lua のエラー | RuntimeError / ValueError | 負の数のまま | 負の数のまま |

- 幅の別名は `sig/fmrb_types.rbs` の 1 つのファイル (`int8`〜`uint64`・`float32`・`float64`・`bin`)。道具は名前で
  見分け、定義が無いか中身が違えば止まる。記録の整数は詰めるときに幅の範囲を確かめ、外れたら ArgumentError
  (Lua はエラー、Python は ValueError)。エディタの型 db は別名を Integer などに解き、記録は untyped として読む
  (tidbgen の出力は印の有無で変わらないことを前半で確かめた)。
- 扱わずに、名前を出して止まるもの: 省略できる引数・可変長・キーワード・ブロック、オーバーロード、引数の nil
  (`Integer?`)、記録以外の `?` の戻り値、Symbol 以外の和、要素の型の無い `Array`・`Hash`・`untyped`、
  記録の中の文字列・配列・入れ子、記録の省略できる欄、幅のある整数・`Float` の戻り値、引数名 `out`・`out_size`・
  受け手の名前。`_raw` の付くメソッドの包みの名前が同じクラスで境界に載っているときも止まる (包みは internal)。
- wasm のポインタは `wasm_runtime_validate_app_addr` で長さ (記録はその大きさ) ごと確かめ、外れたら本体を
  呼ばずに -2 を返す。確かめが立てる例外は消す (消さないとアプリが落ちる。3.6 の確認で見つけた)。
- 記録は packed の little-endian。ヘッダに `_Static_assert` で大きさを書く。esp32・x86-64・wasm32 で同じ並び。
- ABI のハッシュは関数の形と記録の欄から作る (前半は関数だけ)。

### 3.3 生成物の置き場

| 生成物 | 場所 | コミット |
|---|---|---|
| 平らな関数・記録・選択肢の定数 | `components/fmrb_common/include/fmrb_gen/fmrb_boundary.h` | しない |
| mruby のメソッドと登録 | `fmrb_gen/fmrb_boundary_mrb.inc` (gem の `ports/esp32/gfx.c` が include) | しない |
| Lua の関数と `luaL_Reg` | `fmrb_gen/fmrb_boundary_lua.inc` (`components/lua/extension/fmrb_lua_gfx.c`) | しない |
| MicroPython の `_fmrb.gfx_*` | `fmrb_gen/fmrb_boundary_mp.inc` (`components/micropython/modules/fmrb_module.c`) | しない |
| MicroPython のクラス (`_FmrbGfxBoundary`、Python の源) | `fmrb_gen/fmrb_boundary_py.h` (`fmrb_mp.c` が prelude の前に流す) | しない |
| WAMR の raw の関数と表 | `fmrb_gen/fmrb_boundary_wasm.inc` | しない |
| Spinel の適合 (記録・バイト列を `:binstr` に) | `fmrb_gen/fmrb_boundary_spx.inc` (`main/app/fmrb_spx_gfx.c`) | しない |
| 本体の代わりの実体 (届いた引数を報告する) | `fmrb_gen/fmrb_boundary_stub.inc` (`tools/wamr_poc` の本体役) | しない |
| Spinel の `ffi_func` | `main/ruby/spinel/fmrb_gen/fmrb_boundary_ffi.rb` (`module FmrbBoundary`) | しない |
| C のヘッダ (SDK・基準の資料) | `sdk/c/fmrb_boundary.h` | する |
| MicroPython の qstr の表 | `components/micropython/mp_embed/genhdr/qstrdefs.generated.h` (`rake micropython:gen`) | する |

型紙は `tools/boundary/templates/` の 10 個 (前半の 7 個に `_py.h`・`_spx.inc`・`_stub.inc` を足した)。

### 3.4 FmrbGfx の境界 (載せた 41 個・載せなかった 27 個)

載せたもの (平らな関数 `fmrb_gfx_<名>`、本体は `main/app/fmrb_boundary_gfx.c`):

- そのまま (包みなし、31 個): `clear` `set_pixel` `get_pixel` `draw_line` `draw_rect` `fill_rect` `blend_rect`
  `draw_round_rect` `fill_round_rect` `draw_circle` `fill_circle` `draw_ellipse` `fill_ellipse` `draw_triangle`
  `fill_triangle` `draw_arc` `fill_arc` `draw_wallclock` `draw_free_iram` `create_image` (→ `image_info?`)
  `delete_image` `create_mask` (`bin data`) `delete_mask` `set_viewport` `clear_viewport` `set_sprite_clip`
  `clear_sprite_clip` `set_output_level` `set_chroma_level` `file_status` (→ `file_info`)、`FmrbGfx.rgb_to_332`。
  mruby の mrblib にあった素通しの包み (`get_pixel`・`create_mask`・`delete_mask`・`create_image`・`file_status`・
  `delete_image`・`set_viewport`・`clear_viewport`・`set_sprite_clip`・`clear_sprite_clip`) は消した (名前と引数は同じ)。
- `_raw` (包みの下、10 個): `draw_text_raw(x, y, text, color, bg_color, mixed)` (bg_color が負なら背景なし)、
  `set_font_raw(:default | :ja | :ja_bold family, size)`、`set_text_size_raw`、`present_raw(x, y, positioned)`、
  `draw_image_raw(image_id, x, y, Float scale_x, Float scale_y)`、`draw_tile_raw`、`draw_image_masked_raw`、
  `set_composite_regions_raw(Array[composite_region])`、`sync_file_raw(path, dest)`、`transfer_file_raw(path, dest)`。
  mruby と Spinel の包み (`draw_text`・`draw_text_mixed`・`set_font`・`present` など) はこれを呼ぶ。
- 記録は 3 つ: `image_info { id, width, height: uint16 }`、`file_info { exists: bool, size: uint32 }`、
  `composite_region { src_x, src_y, dst_x, dst_y, w, h: int16, transparent: bool }` (13 バイト)。

載せなかったもの (`%a{fmrb.internal}`) と理由:

| メソッド | 理由 |
|---|---|
| `canvas_id` `canvas_width` `canvas_height` `area_width` `area_height` `origin_x` `origin_y` `current_font` `current_text_size` | 各エンジンのオブジェクトが持つ状態 (mruby は C の構造体と ivar、Spinel・Python は ivar・属性)。原点と状態を境界の下へ移すのは W4b (決定 17) |
| `draw_text` `set_font` `set_text_size` `present` `draw_image` `draw_tile` `draw_image_masked` `sync_file` `transfer_file` `set_composite_regions` | 省略できる引数・キーワードを持つ包み (Ruby)。`_raw` を載せた。`set_font` は機械の持つ字への置き換えと current_font、`set_text_size` は current_text_size も持つ |
| `text_width` `font_height` `draw_thick_line` `draw_button` `load_image` | Ruby で書いたメソッド (本体に実体が無い) |
| `hsv_to_rgb` | 結果が `[r, g, b]` の Array で、規則に合わせるには戻り値を変える (記録にする) 必要がある。計算だけなので各言語で書ける |
| `destroy` | オブジェクトの一生 (mruby は C の参照を外すだけ、Spinel は何もしない) |

`FmrbText` (fit_text など) は別のモジュールで印が無い (Ruby で書いたもの)。型定義の引数名 `str` は `text` に直した
(`draw_text`・`text_width`・`fit_text`・`wrap_text`・`draw_text_lines`)。型定義の戻り値を実装に合わせた
(`draw_wallclock`・`draw_free_iram` は bool、`delete_mask`・`set_viewport`・`clear_viewport`・`set_composite_regions` は
FmrbGfx。どれも mruby と Spinel が前から返していた値)。

### 3.5 各エンジンで置き換えたもの

| エンジン | 消した手書き | 残る手書き |
|---|---|---|
| mruby (gem `ports/esp32/gfx.c`) | 41 個の C のメソッド (`_draw_text`・`_draw_text_hybrid`・`_set_font` など `_` の付いた内部用を含む)、ファイル転送の補助 | 受け手のフック、`_init`・`_set_origin`・原点と区画の 4 つ、`destroy`、動画、スプライト、`hsv_to_rgb`、checkpoint |
| Spinel (`fmrb_spx_gfx.c`・`.h`・`fmrb_app_ffi.rb`) | 境界に載った関数の C と `ffi_func` | 動画・スプライト。`FmrbGfx` の Ruby のクラスは `FmrbBoundary.fmrb_gfx_*` を呼び、原点 (`@dx`) は今までどおり自分で足す。`_raw` のメソッドも足し、包みはそれを呼ぶ |
| Lua (`fmrb_lua_gfx.c`) | 8 個の手書きのメソッド | フック、小さな Lua の前置き (`gfx_prelude`、3.7)、`FmrbApp` の関数 |
| MicroPython (`fmrb_module.c`・`fmrb_bridge.c`・`fmrb_mp_bridge.h`・prelude) | 18 個の `_fmrb.gfx_*` と bridge の 17 個、prelude の素通しのメソッド | フック、スプライト、prelude の包み (既定値を持つもの) と `text_width`・`font_height` |

本体の内部の名前が平らな関数の名前とぶつかった 2 つは、内部の方を改名した (`fmrb_gfx_set_sprite_clip` →
`fmrb_gfx_set_canvas_sprite_clip`、`fmrb_gfx_composite_region_t` → `fmrb_gfx_canvas_region_t`。`fmrb_gfx.h` の
既存の `fmrb_gfx_set_canvas_viewport` に揃えた)。W4b への注意は 5 章。

描画の記録の道具 (`tools/regress/recorder.rb`) は FmrbGfx の C の側の代わりなので、名前を新しいものに直した
(記録の行は同じに作るので基準は変えていない)。`test/fmrb_text/run.rb` の代わりも同じ。

### 3.6 wasm の側 (`ruby tools/wamr_poc/run.rb boundary`)

C のアプリ (`apps/c_boundary/app.c`) に、規則の種類を 1 回ずつ通る呼び出しの並び (`tour.h`: 整数・文字列・選択肢・
小数・バイト列・記録・記録の配列・記録の結果・アプリのメモリの外のポインタ) を足した。本体役 (`host/host.c`) は
`fill_rect` 以外を生成した代わりの実体 (`_stub.inc`) で持ち、届いた引数のバイトを FNV で混ぜる。同じ並びを本体役が
ネイティブに呼んだ値と、wasm のアプリから届いた値が一致することを見る。外のポインタは glue が -2 で断り、本体の
実体は呼ばれない。

### 3.7 Lua と MicroPython の書き方の変化

Lua:
- `gfx:draw_text(x, y, text, color [, bg_color [, mixed]])` (Ruby と同じ順)。直したのは 2 本で 6 か所
  (`flash/app/lua/lua.app.lua` 5・`flash/app/test/busy.app.lua` 1)。
- 小さな Lua の前置き (`fmrb_lua_gfx.c` の `gfx_prelude`): `draw_text`・`present([x, y])`・`set_font(family [, size])`・
  `set_text_size`・`draw_image`・`draw_tile`・`draw_image_masked`・`sync_file`・`transfer_file`・`set_composite_regions`
  を `_raw` の上に Ruby と同じ既定値で書いた。描くメソッドは全部生成 (前は 8 個だけ手書きであった)。
- 大きさは `FmrbApp.user_area_width()`・`user_area_height()`・`window_width()`・`window_height()` の関数
  (殻が渡した区画をレジストリに持ち、呼んだ時点の値を返す)。`WINDOW_WIDTH`・`WINDOW_HEIGHT` と区画の欄は消した。
- 記録は表、選択肢は文字列 (`gfx:set_font("ja", 12)`)。`FmrbGfx.rgb_to_332` が増えた。
- README (`components/lua/extension/README.md`) とヘッダの説明を直した。

MicroPython:
- prelude の `FmrbGfx` は生成した `_FmrbGfxBoundary` を継ぐ。包みを持つメソッド (`present`・`draw_text`・
  `set_font`・`set_text_size`・`draw_image`・`draw_tile`・`draw_image_masked`・`sync_file`・`transfer_file`・
  `set_composite_regions`) だけ手書きで既定値を持つ。ほかは生成で、`clear()` の既定の黒は無くなった (決定どおり)。
  直す要のある Python のアプリは無かった (`clear` を引数なしで呼ぶものが無い)。
- 生成した Python のメソッドは `_fmrb.gfx_*` にオブジェクト自身を渡し、C のフックが `canvas_id`・`dx`・`dy` を読んで
  原点を束ねる (mruby・Lua と同じ形)。prelude が座標に `dx` を足すのはやめた。数を渡せば今までどおり canvas の番号。
- 今まで無かったもの (`get_pixel`・円弧・`blend_rect`・マスク・合成の矩形・表示範囲・`transfer_file`・`file_status`・
  `rgb_to_332` など) が使える。`FONT_JA_BOLD` を足した。
- 文書: `doc/micropython/known_limitations.md` (FmrbGfx の一覧と無いもの)、`doc/micropython/README.md`
  (境界を変えたら `micropython:gen`)。

### 3.8 生成物の古さの検出

`rake test` の `boundary:check` が、`sdk/c/fmrb_boundary.h` が型定義から作ったものと同じか、生成した MicroPython の
つなぎが使う qstr が全部コミットされた表 (`qstrdefs.generated.h`) にあるかを見る。足りなければ名前を並べ、
`rake micropython:gen` を流してコミットするよう言う。後半は関数が増えたので gen を流してコミットした (qstr が 38 増え
8 減った)。qstr の表を毎回作る形にはしていない (make と python3 がビルドの入口に要るようになるため。検査で止める
方にした)。`sig/README.md` に印・規則・qstr の注意を書いた。

### 3.9 ついでに直した取りこぼし (T1 の残り)

- `flash/usr/share/template/app.rb.template`: `on_event` の `super` と「super を呼べ」の注を消し、`clear_user_area`
  の後の `draw_window_frame` を消した (`clear_user_area` が枠を描き直す)。
- `sig/fmrb_app.rbs` の `draw_window_frame`: 「大きさの変わるアプリは毎回呼ぶ」を、「ふつうは呼ばない。起動時は
  基底が、`clear_user_area` も枠を描く。区画の端を `fill_rect` などで塗って枠を踏んだときだけ呼ぶ」に。

## 4. 確かめたこと

| 確認 | 結果 |
|---|---|
| `rake regress` | ホスト 29 件・描画の記録 229 件、全部通った (基準は変えていない。記録係の名前を直しただけ) |
| `rake regress:spinel` | 6 本、全部生成できた (25.6 秒) |
| Linux (`rake clean && rake build:linux`) | 通った。x86-64。1 回目は内部の名前のぶつかり 2 つで止まった (3.5) |
| sim (Retro) | mruby: デスクトップ (文字・時計・空きメモリ・mixed の文字)、`rpg_demo` (`present(x, y)`・タイル)。Lua: `lua.app.lua` (新しい順の `draw_text`・`user_area_width()`・クリックの円、数秒動かして消し残りなし)。MicroPython: `python.app.py` の図形・文字 (既定・日本語・mixed)・スプライト (画像へ描く、原点なし)・PNG の読み込みと拡大。Spinel: エディタを開いて打鍵、状態の行が進んだ。どれも正しい位置に出た |
| `ruby tools/wamr_poc/run.rb boundary` | Linux・emscripten とも canvas `4b90f47e` が期待と一致、引数のハッシュ `d799991a` (13 回の呼び出し) がネイティブと一致。C のアプリは 4,811 バイト |
| `rake regress:screens` | 33 通った / 0 落ちた (modern 11・retro 9・web 6・native 3・native-keys 4、12.1 分。ブラウザ版とネイティブ版のビルドもこの中) |
| `rake regress:size` | 通った (6.3 分)。NARYAv3 DIRAM 125,403 (基準比 -56)・IRAM +0・その他 +0、TAB5 DIRAM 129,338 (-360)・その他 +0、Spinel の単位の静的な内蔵 RAM は変わらず。前半の終わり (1.6) と同じ数字で、**内蔵 RAM の静的な増分は 0**。Spinel の適合の静的な領域は PSRAM (`FMRB_EXT_RAM_BSS_ATTR`)。ファームの大きさは前半の終わりから S3 +9.7 KB・P4 +13.8 KB (Lua・MicroPython・mruby に今まで無かった関数のつなぎが入った分) |
| `rake regress:run` | 通った (4.4 分)。VM プールは揺れの幅の中、desktop は -1.4 KB (mrblib の素通しの包みが減った分)。KB 単位で増えたものは無い |
| 道具が止まること | `text_width` の印を外す → 「C の実体が無い」で名前を出して止まる。メソッドに `%a{fmrb.boundary}` → 「クラスに付ける」。引数を `Hash` に → 型と引数の名前を出して止まる。`present` の印を外す → 実体が無いで止まる |

## 5. W4b への注意

- **C の名前のぶつかり**: 平らな関数の `fmrb_<クラス>_<メソッド>` は、本体の内部の API と同じ名前の空間にある。
  FmrbGfx では 2 つで、内部の方を改名した。FmrbApp は `fmrb_app_*` (アプリの task の API) と大きく重なる見込み。
  内部を改名し続けるか、境界の C の名前を変えるか (決定では `fmrb_gfx_draw_text_raw`) を W4b の前に決める
  (plan.md の未確定事項に足した)。
- **原点は仮の形のまま** (1.3)。MicroPython も mruby・Lua と同じ「束ねる」形になった (フックが `dx`・`dy` を読む)。
  Spinel は Ruby の側で `@dx` を足す。sprite の画像へ描く間の `set_sprite_clip` は、mruby で前は原点を足していたが
  今は足さない (フックが番号だけを渡すため。使う場面は見当たらない)。W4b で「番号 = 描き手」の表にするときに揃う。
- **mruby の `present` は Ruby の包みになった** (省略できる引数があるため)。1 フレームに 1 回、呼び出しが 1 段
  深い。W4b の命令数の比較の対象に入れる。ほかの包みの呼び出しの段の数は前と同じ (`draw_text` → `draw_text_raw` は
  前の `draw_text` → `_draw_text` と同じ)。
- **checkpoint**: mruby の C のメソッドの名前が変わった (`_draw_text` などが消え、`*_raw` が増えた)。表はビルドごとに
  作り直されるので同じコミットの端末の間は変わらないが、この前後の版の間で動いているアプリを運ぶ互換は確かめて
  いない (`regress:checkpoint` は流していない)。plan.md の未確定事項のとおり W4b の前に確かめる。
- **エディタの型 db**: 記録の型 (`image_info?` など) は tidbgen が untyped として読むので、`create_image` の戻り値の
  補完が前の `Hash?` より弱くなった。picoruby-ti が record を Hash として読めば戻る。
- **フック**は印で集めて SDK に一覧を出すところまで。FmrbApp に印を付けるのは W4b。
- **色の定数**は生成していない (Lua・Python は手書きのまま、Lua に `GRAY` が無いのも前のまま)。
- **Spinel の `set_font`** は前と同じく `:ja_bold` を選ばない (FONT_METRICS に太字が無い)。`set_font_raw(:ja_bold, 12)` は
  呼べる。
- **MicroPython に関数を足すときは `rake micropython:gen` とコミットが要る** (3.8)。`boundary:check` が止める。
- W4b で他のクラスを載せるときの手順: クラスに印 → `rake boundary:gen` が並べる「C の実体が無い」ものを
  `main/app/fmrb_boundary_<名>.c` に書くか internal にする → 各エンジンの手書きを消して、フックを書く。

## 6. 後半のコミット

- 0798b942 feat(boundary): put the whole FmrbGfx class on the app boundary from sig/
- adf16382 test(wamr_poc): the C app walks every kind of boundary argument
- 3f6db018 fix(template): drop super and the extra frame call from the app template
- この report と plan.md の更新 (docs)

## 7. 後半の時刻

開始 09:16、終了 10:22 (`date`)。sim 約 3 分、Linux のビルド 3 回 (1〜2 分)、`micropython:gen` 1 分弱、
`wamr_poc boundary` 4 回 (1 回 30 秒ほど)、`regress:screens` 12.1 分、`regress:size` 6.3 分、`regress:run` 4.4 分。

## 前半のコミット

- 6b46d8a0 feat(boundary): generate fill_rect's glue for every engine from sig/
- c01ae4c1 test(wamr_poc): a C app calls the generated boundary function
- この report と plan.md の状態の更新 (docs)

## 前半の時刻

開始 08:40、終了 09:10 (`date`)。ビルドと確認: Linux 2 回 (clean から 1 分強)、sim 約 2 分、ネイティブ版 1 分、
wasm の側 13 秒、ブラウザ版 4 分、`regress:size` 6.5 分。
