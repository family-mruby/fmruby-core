# T0 指示書: 調べ (コードは変えない)

> 親: plan.md の T0。決めたこと・守ることは plan.md の「決めたこと」。結果は report/t0.md に書く。

## 調べること

1. **起動の流れ** (T3 の材料): カーネルがアプリの `.app.rb` を読んでから `start` が呼ばれるまでを、mruby の VM・Spinel (AOT、
   `main/prebuild_scripts/spinel/` と生成)・MicroPython (`.app.py`)・BASIC・checkpoint の復元 (`on_restore`) のそれぞれで追う。
   「新しく定義された FmrbApp の子を見つけて起動する」を入れる場所の候補と、Spinel で子クラスを見つける手段があるか
   (コンパイル時に分かるか)、二重起動を防ぐ手段。末尾の起動コードの形のばらつき (rescue の中身、引数つき、2 つのクラスを持つ
   ファイル) の一覧。
2. **座標** (T4 の材料):
   - `@user_area_x0/y0` の作られ方と更新される時機 (枠の有無の切り替え・全画面・高解像度の出入り・`on_resize`・checkpoint の復元)。
   - 座標を取る API の全部 (Ruby の `@gfx` の描画、文字、画像・BMP、スプライト、キャンバス、`set_composite_regions`、
     `set_window_position`、クリップ、FmrbUI のウィジェット、マウス・タッチ・ドラッグのイベントの座標、`draw_window_frame` と
     基底の枠描き) と、それぞれの今の原点 (画面・Window・user area)。C 側 (graphics-audio への描画命令) で原点を足しているか。
   - 枠なしの Window (`window_frame = "none"`、doc/frameless_window)・全画面 (`fullscreen`)・高解像度 (`fullscreen_hires`) の
     アプリの一覧と、今それぞれが何を原点に描いているか。
   - Window の外や枠の上に描いているアプリ・ライブラリの一覧 (`@user_area_x0` を引かずに描いている所、負の座標)。
3. **FmrbApp のクラスメソッド** (T5 の材料): Ruby・Spinel・sig (`sig/fmrb_app.rbs`) の全部の一覧を、使っている所
   (アプリ・カーネル・デスクトップ・サービス・道具) の数と一緒に。一般アプリ用か system 用か、の分け方の案を 2 つ程度。
4. **重複するヘッダ** (T2 の材料): fmruby-core・fmruby-graphics-audio・`docker/` (sdl2-display) で同じ名前、または同じ定義
   (プロトコル番号・構造体・イベント型) を持つヘッダの全一覧と、内容の差。どれが正に近いか。graphics-audio と sdl2-display の
   ビルドが、隣の fmruby-core を読める作りかどうか (CMake の include のパス、docker の build の context)。
5. **古い契約の残り** (T1 の材料): `super(ev)`・`draw_window_frame`・`clear_time` の残る場所 (skill・テンプレート・文書・アプリ・
   sig・help)。`.app.toml` と `#---fmrb` の読み手 (どこで読み、どの項目を知っていて、両方あるとき今どうなるか) と 7 本の一覧。
6. **置き場** (T6 の材料): `tool/`・`tools/` (core) とルートの `tools/` の中身と呼び出し元 (Rakefile・rakelib・文書・skill・CI・MCP)、
   `flash/app/test/` の 64 本を参照している所 (regress の道具・文書・MCP・skill)、`main/prebuild_scripts/` の中身と参照元、
   `lib/add/` の中身を「上流への差し込み (patch・置き換え)」と「自作 gem」に分けた一覧、`ports/` の中身、`config/` の
   ファイルを端末ごとに並べた表 (同じ端末が別の名前で出る所)。それぞれ、名前を変えたら直す所の数。

## 触ってよい範囲

- 読むだけ。**コードは変えない**。fmruby-core の `doc/codebase_tidy/report/t0.md` だけを書いてコミットしてよい
  (`docs(codebase_tidy): ...`、pathspec を明示、Co-Authored-By)。push しない。
- 動かして確かめたいことがあれば、sim・ネイティブ版で動かしてよい (ビルドは手元の asterism のまま。終わったら片付ける)。

## report (report/t0.md)

段ごと (T1〜T6) に節を分け、表を中心に。各段について「入れる場所の候補」「直す数」「危ない所」「ユーザに選んでもらうことの案
(候補と推奨)」を書く。T5 のモジュールの名前と分け方、T6 の新しい名前の一覧は、案を 2〜3 個ずつ。
