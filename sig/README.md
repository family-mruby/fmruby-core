# sig — FMRB API の型定義

このディレクトリの `.rbs` は **FMRB の API がどんな形をしているかの正**です。
ビルド時に tidbgen が読んで C の表に変換し、ファームウェアに入ります。
使う人は 2 通り:

- **エディタ**の補完 (Tab)、型の表示 (Ctrl+T)、まちがい探し (Ctrl+E)。
  doc コメントはそのままステータス行に出るので、**API の説明文そのもの**に
  なる。
- **PC 側の道具**。同じ定義から picoruby-ti の LSP/MCP サーバが立つので、
  VSCode や Vim からも同じ型支援が受けられる (P6 の予定)。

## Ruby の API を足したり変えたりしたら、ここも直す

実装だけ直して sig を放っておくと、**エディタが古い形を教え続ける**。
レビューでは「Ruby 側の変更に対応する sig の変更があるか」を見る。

## 直したら `rake clean` してからビルドする

ここから作った型 db (`picoruby_ti_builtin_database.c`) は picoruby 側の
ビルド木に入る。そこは**作り直しを検出しないので、`rake build:linux` だけ
だと古い db のまま**リンクされ、画面は変わらないのに理由が分からない、と
いうことになる。`rake clean` を挟めば直る。ブラウザ側 (`rake ti:wasm`) は
毎回作り直すので、**ブラウザだけ新しい説明が出て実機は古い**という食い違いが
出たら、まずこれを疑う。

## 表示のされ方 (書く前に知っておくこと)

長文は **F1 のヘルプ画面**に出る。画面は **39 桁**しかない。

- **例のコードは 39 桁以内**に折り返して書く。散文はどこで折れても読めるが、
  **コードは語の途中で割れる**。長いパスや長い呼び出しは自分で改行する。
- **markdown は表示される** (2026-09-01 から)。`**強調**`、`` `code` ``、
  ` ``` ` のブロック、`#` 見出しは記号が消えて色が付く。**記号のぶんの
  空きは残る**ので、強調は語の切れ目に置くと落ち着く。

## 要約 (1 行目) は 112 バイトまで

エディタは要約を **112 バイトの箱にコピーして持つ** (`ET_DOC_MAX`、
lib/gems/picoruby-fmrb-editor-core/ports/esp32/editor_ti_bridge.c)。
はみ出した分は**黙って切られる**。

**二か国語を 1 行に載せると届く**ので、`日本語 <<en>> English` の全体で
112 バイト以内に収める。目安は**全角 20 字 + 半角 45 字**。
超えると英語側の尻が切れ、切れ方によってはマーカーごと消えて日本語が
英語の読み手にも出る。

長い説明は 2 行目以降 (F1 の長文ヘルプ) に置く。そちらに上限は無い。

## 書き方の約束

- **実在するメソッドだけ**書く。実装 (mrblib の Ruby と C バインディング) を
  読んで書き、推測で書かない。`_` で始まる内部用は書かない。
- **必須引数だけの形**で書く。型推論エンジンは省略可能引数もキーワード引数も
  持たないので、キーワードが本質の API は**いちばん多い呼び方**を必須引数に
  直した形にする (例: `create_canvas_gfx(width, height)`)。
  正確に書けなかったものは report に残す。
- **doc コメントは 2 段構え**にする。
  - **1 行目 = 一文**。全角 20 字が目安。何をするかだけを書き、
    引数の説明はシグネチャに任せる。**db に入るのはこの行だけ**で、補完の
    一覧とホバーにそのまま出る。**両言語あわせて 112 バイトまで** (上記)。
  - **2 行目以降 = 長文ヘルプ** (任意)。詳しい説明と使用例。
    ` ```ruby ` のコードブロックが書ける。ビルド時に
    `flash/help/<クラス名>/<メソッド名>.md` へ書き出され、エディタの
    **F1** で開く。db には入らないので、いくら長くしても
    ファームウェアの文字列プールは太らない。
  - クラスの直前のコメントも同じ形。2 行目以降は
    `flash/help/<クラス名>/index.md` になる。
  - **長文は全部の API に置く** (方針変更 2026-09-01)。理由は説明ではなく
    **例**にある: シグネチャの型を目で数えるより、呼び方が 1 行あるほうが
    速い。ただし**二層に分ける**。
    - **つまずくメソッド**: 散文 + 例。実装を読まないと分からない振る舞い
      (描いたものが出ない、部品が貼り付く、一度きり、上限バイト数) を書く。
    - **素直なメソッド**: **例だけ。散文を書かない。** 引数の順と型が
      読めれば用は足りる。「引数 x は x 座標です」は書かない — 何も足さずに
      読む時間だけ奪う。書くことが無いなら、例 1 行で終える。

    ```ruby
    # 四角のわくを描く <<en>> draw a rectangle outline
    #
    # ```ruby
    # gfx.draw_rect(10, 20, 60, 40,
    #               FmrbGfx::WHITE)
    # ```
    def draw_rect: (...) -> FmrbGfx
    ```
- **文章のトーンは、大人が読んでも普通の説明**にする。やさしい語で書くのは
  よいが、子供向けに寄せた言い回し (「〜してみよう」「おえかき」など) は
  使わない。用語は実装で使っている言葉に合わせる。
- **二か国語 (ja/en) は 1 つのマーカー `<<en>>` で同居させる**。
  ファイルは分けない。表示側 (エディタ) が現在の言語の側だけを出す。
  - **1 行目 (要約) はインライン**: `日本語 <<en>> English` の順。
    **行全体で 112 バイトまで** (上の節)。長い英語は一文に縮める。
  - **長文は `<<en>>` だけの行**で日本語側と英語側を区切る。
  - マーカーが無い doc は**その 1 言語が両方の言語で出る** (フォールバック)。
    日本語だけの既存の doc はそのままで壊れない。英語を足すのは API を
    触るときで構わない。

  ````
  # 黒 (RGB332) <<en>> black (RGB332)     <- 1 行目 = 要約。<<en>> で ja | en
  #
  # 背景を塗りつぶすときに使う。          <- 2 行目以降 = 長文 (日本語側)
  # ```ruby
  # gfx.clear(FmrbGfx::BLACK)
  # ```
  # <<en>>                                <- 長文の言語境 (行として置く)
  # Used to fill the background.
  # ```ruby
  # gfx.clear(FmrbGfx::BLACK)
  # ```
  ````

  英語を足すと**要約の分だけ db の文字列プールが増える** (長文は flash の
  ファイルなので増えない)。上限は 65,535 バイト。

  マーカーは fork / tidbgen / gen_help のどれも解釈しない。ただの文字列と
  して db と `.md` を通り、エディタの `ti_pick_lang` (要約) と
  `help_filter_language` (F1) が切り分ける。
- 読み取り専用の属性も `def name: () -> String` の形で書く。RBS の
  `attr_reader` 構文は tidbgen が読まない。
- インスタンス変数は `@gfx: FmrbGfx` の形で宣言できる。アプリが直接触る
  変数 (`@gfx` など) はこれを書いておくと補完が効く。
- **ドメインごとにファイルを分ける** (`fmrb_app.rbs` / `fmrb_gfx.rbs` /
  `fmrb_sprite.rbs` / `fmrb_tile.rbs` / `fmrb_audio.rbs` / `fmrb_midi.rbs` /
  `fmrb_misc.rbs` / `fmrb_p5.rbs` / `fmrb_file.rbs`)。
- **上流由来の 15 ファイルは変更禁止**: `array` `class` `false_class`
  `float` `hash` `integer` `kernel` `nil_class` `object` `proc` `range`
  `string` `symbol` `true_class` `untyped`。picoruby-ti の example/rbs の
  逐語コピーで、エンジンのテストが文字列一致で見ている
  (`enumerable.rbs` と `gpio.rbs` はうちのもの。ただしエンジンのテストが
  前提にしているので、宣言を減らさないこと)。

## 本体との境界の印 (doc/app_boundary)

型定義は、本体とアプリの間の境界 (平らな C の関数) と、各言語のつなぎ
(mruby・Spinel・Lua・MicroPython・WAMR・C のヘッダ) の元でもある。生成の道具は
`tools/boundary/gen.rb` (`rake boundary:gen`、`rake setup` が毎回流す)。

- **クラスに `%a{fmrb.boundary}`** を付けると、そのクラスに書いたメソッドが
  全部境界に載る。載せないメソッドに **`%a{fmrb.internal}`** (Ruby で書いた
  メソッド・オブジェクトが持つ状態・省略できる引数を持つ包み)、アプリが書く
  フックに **`%a{fmrb.hook}`**。印は `def` の前の行に置く (`def` と同じ行に
  書くと `rakelib/fmrb_system.rake` の数え方から漏れる)。印は RBS の標準の
  注釈なので、エディタの型 db と F1 のヘルプは印の有無で変わらない。
- 境界のメソッドは必須の位置引数だけで書く。省略できる引数・キーワードを
  持つメソッド (包み) は internal にし、隣に後ろへ `_raw` を付けた省略なしの形を
  書く (`draw_text` と `draw_text_raw`)。説明文に「包みは draw_text」と書く。
  包みは各エンジンの Ruby (mruby は gem の mrblib) で書くのが決まりで、例外は
  mruby の `present` (gem の C、`gfx.c` の `mrb_gfx_present`。毎フレーム
  呼ぶので、Ruby の包みでは命令数が W4a の前の 2 倍になった。2026-11-15 決定) と
  mruby の `Log.info` など (picoruby-fmrb-log の C。前からの C のメソッドで、
  タグを先に渡す 2 引数の形も読む)。例外も印は同じ internal で、道具はどちらで
  書いたかを区別しない。
- 受け手の番号: インスタンスメソッドは第 1 引数に受け手の番号を取る
  (FmrbGfx は描き手、FmrbAudio は今は 0)。`FmrbApp` のインスタンスメソッドは
  取らない (受け手は呼んだアプリ。道具の `NO_RECEIVER`)。Lua では `FmrbApp` の
  表の関数 (`FmrbApp.window_width()`)、Python では基底クラスのメソッドになる。
- 書き込みのメソッド (`main_volume=`) は、C と他の言語では `set_main_volume`。
  問いのメソッド (`exist?`) は、C と他の言語では `?` を落とした名前
  (`fmrb_file_exist`、Python の `ev_ctrl` と同じ)。落とした名前が同じクラスで
  ぶつかると道具が止まる。
- **FmrbApp の Window の状態を読むメソッドと `stop`・`draw_window_frame`・
  `clear_user_area_raw`** は、境界に載せたうえで、Ruby (mruby・Spinel) と
  MicroPython では基底クラスが自分の値で答える (mrblib は gem の C の後に読まれる
  ので、同じ名前の Ruby の定義が生成した C の登録を上書きする)。C の実体は C の殻
  (Lua・wasm) のためのもの。Ruby の基底が持つ状態 (`@window_width` など) は今の
  まま。
- アプリが書くメソッド (`on_create` など) は `%a{fmrb.hook}`。C の殻が呼ぶもの
  だけに付け、C のヘッダの終わりに wasm のアプリが公開する関数の形を出す
  (`on_update` は C では次までの ms を返す。`on_event` は記録 `event` を受ける。
  記録 `event` の `type` は Symbol の選択肢で、C では番号)。Ruby だけが呼ぶフック
  (`on_checkpoint` など) は internal。
- 型の規則: `Integer` は int32、幅は `sig/fmrb_types.rbs` の別名
  (`int16`・`uint8`・`float32`・`bin` など、ROS 2 と同じ名前) で書く。Symbol の
  選択肢はリテラルの型 (`:default | :ja`)。記録は record 型の別名
  (`type image_info = { id: uint16, ... }`)、要素の決まった配列は
  `Array[記録]`。結果に nil があり得るのは記録 (`image_info?`。C は 0) と、整数・
  文字列・バイト列 (`Integer?`・`int64?`・`String?`・`bin?`。C は
  `FMRB_BOUNDARY_NONE` (-3) を返し、mruby・Lua は nil、Python は None)。整数の
  結果は `Integer` (int32) か `int64` (時計の µs など。C は `int64_t` を返す)。
  扱えない型を書くと、道具がメソッドの名前を出して止まる。
- 載せたメソッドには本体の C の実体 (`main/app/fmrb_boundary_*.c` の
  `fmrb_bnd_<クラス>_<メソッド>`) が要る。無ければ道具が名前を出して止まる
  (Ruby で書いたメソッドを載せかけたら internal を付ける)。C の名前は 2 つ:
  本体の中の実体と生成するつなぎは `fmrb_bnd_<クラス>_<メソッド>`
  (記録は `fmrb_bnd_<クラス>_<名>_t`、選択肢は `FMRB_BND_...`)、C のアプリから
  見える名前 (SDK のヘッダ・wasm の import) は `fmrb_<クラス>_<メソッド>`。
  本体の内部の API (`fmrb_gfx_*` など) とぶつからないため (2026-11-15)。
- **MicroPython の qstr の表はコミットされた生成物**。境界に関数を足す・
  名前や記録のキーを変えたら `rake micropython:gen` を流して
  `components/micropython/mp_embed` をコミットする。`rake test`
  (`boundary:check`) が、C の SDK (`sdk/c/fmrb_boundary.h`) と qstr の表が型定義と
  合っているかを見る。

## 直したら確かめる

```
rake ti:test     # エンジンのホストテスト (sig から db を作って回す)
rake ti:help     # flash/help を作り直す (rake setup でも走る)
rake build:linux # ファームウェアに入る形で生成できるか
```

**別のファイルにあるクラスを参照していると、そのファイルが欠けたときに
生成ごと止まる** (`class ID cannot be resolved: ::FmrbGfx` のように出る)。
生成が止まればビルドも止まるので、黙って古い db が使われることはない。
