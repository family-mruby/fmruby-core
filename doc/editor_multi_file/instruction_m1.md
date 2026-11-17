# M1 指示書: slot の切り替えと、文書ごとの状態の出し入れ

> 2026-11-17 | 親セッションからサブ (Opus) への指示。計画の正は plan.md (先に全部読む)。

## 1. この段で作るもの

1 つのエディタが 5 つまでの文書を持ち、切り替えても文書ごとの中身と表示の状態が混ざらない、という土台。
タブの行・File メニューの変更・終了の確認・外から開く、は M2・M3 で作る。**この段では作らない**。

### 1.1 C の束ね (plan 4.1)
- エディタが持つ slot の組 (5 つまで) と「今の slot」を束ねが持つ。Ruby 側の `EditorCore` の今の呼び方
  (slot を渡さない) は変えない。足す口は「文書を足す」「今の文書を切り替える」「今の文書を閉じる」と、
  要るなら「いくつ持っているか」程度。名前と形は任せる (report に書く)。
- Spinel 版: `main/app/fmrb_spx_editor.c` と `main/ruby/spinel/fmrb_editor_ffi.rb` (FFI の宣言と shim)。
- mruby 版: `lib/gems/picoruby-fmrb-editor-core/ports/esp32/editor_core_mrb.c`。
  **`components/picoruby-esp32/picoruby/` の下の写しは直さない** (submodule。lib/gems が正で、Rakefile が写す。
  lib/ を直したらビルドの前に `rake clean`)。
- エディタのソース (`main/ruby/default_app/editor.app.rb`・`editor/*.rb`) は、両方の構成で同じものを使う。
- エディタが終わった・殺されたときに、持っていた slot を**全部**返す (今の `fmrb_spx_ec_release_slot`・
  `slot_release` を組の分に広げる)。
- 足す静的な表は PSRAM に置く (`FMRB_EXT_RAM_BSS_ATTR`。今の `s_slot_plus1` と同じ)。**内蔵 RAM を増やさない**。
- 文書の本体 (`editor_core.c` の `g_docs`・`ED_MAX_DOCS` = 5) は変えなくて済む見立て。変える必要が出たら
  理由を report に書く。

### 1.2 文書ごとの状態 (plan 4.2)
- まず、エディタの全部の ivar を「文書ごと」と「エディタに 1 つ」に仕分け、表にして report に書く。
  文書ごとの見立て: カーソル (`@cx`・`@cy`)、スクロール (`@scroll_y`・`@anchor_seg`・`@scroll_x`)、
  `@wrap_on`・`@cur_line_segs`、`@modified`・`@current_file`、選択 (`@sel_anchor_x/y`)、
  ハイライト (`@hl_enabled`・`@hl_manual`)。
- 切り替えは「今の状態を保存 → slot を切り替え → 状態を戻す → 全体を描き直す」。開いているダイアログ・
  メニュー・補完の候補は、切り替えの前に閉じる。
- 持ち方は Spinel で型が絞れる形にする。Spinel の罠 (下の 4 章) を踏まないこと。**生成 C を読んで、
  保存と戻しが黙って 0 になっていないことを確認する**。
- 型支援の診断 (`@diag_msgs` など) は、切り替えたら消す (別の文書の診断を見せない)。
- **ブレークポイント・デバッグのペインの持ち方は変えない**。今どう持っていて (行の番号だけか、ファイル名
  付きか)、切り替えると何が起きるかを report に書く。文書ごとに持つかはユーザが決める。

### 1.3 仮の操作 (この段だけ。M2 で作り直す)
- `Alt+.` 次の文書、`Alt+,` 前の文書、`Ctrl+W` 今の文書を閉じる、無題の文書を足すキー (空いているものを
  選び、report に書く)。キーの一覧 (Alt+K)・i18n・案内の文書にはまだ載せない。
- 2 つ以上のとき、ステータス行に `[2/3]` のような仮の印を出してよい (確認のため。M2 でタブの行に替える)。
- File > Open は今のまま (今の文書を置き換える)。
- 閉じる: 最後の 1 つを閉じたら空の無題になる。**変更のある文書を閉じるとき・終了のときの確認は M2**。
  この段では、変更のある文書の `Ctrl+W` は閉じずにステータス行で断る (仮)。終了は今のまま。
- 6 つ目を足そうとしたら、足さずにステータス行で知らせる。領域が足りない (`EC_ERR_NOMEM`) ときも同じで、
  今の文書を壊さない。

## 2. 受け入れ条件

標準構成 (Spinel のエディタ) と全部 mruby の構成の**両方**で、sim (Modern でよい。Retro は 1 回) で確認する。

1. 文書を 3 つ持ち、それぞれに違う中身を打ち、切り替えて戻ると、本文・カーソルの位置・スクロール・
   変更の印・ファイル名が文書ごとに保たれている。**判定は絵でなく中身で**: 文書ごとに違う名前で保存し、
   保存されたファイルの中身が打ったとおり (混ざっていない) ことを確認する。カーソルは、戻ってから
   1 文字打って保存した結果の位置で確認できる。
2. 文書 A でコピーして文書 B に貼れる (保存したファイルで確認)。
3. 6 つ目は断られ、5 つの文書は無事。
4. エディタを終えた後と、kill した後に、文書の領域の使用量 (`ec_mem_used`。ログか debug の口で見る) が
   起動前に戻る。もう一度起動して普通に使える。
5. 切り替えた後に、型支援 (F1・Ctrl+T・Ctrl+E)・検索 (Alt+S・F3)・実行 (F5) が今の文書に対して動く。
6. `rake regress` が通る。段の最後に 1 回 `rake regress:screens FULL=1` が通る (エディタの 1 打鍵が
   全構成で通ること。ブラウザ版・ネイティブ版のビルドもこれで通る)。
7. `git status` に取りこぼしが無い (前からある `m components/esp_littlefs`・
   `m components/picoruby-esp32/picoruby`・`?? wasm/vendor/` は除く)。

確認の型: 直すたびに `rake regress` だけ。sim を手で動かす確認 (`tools/` の道具か MCP の sim_*) は、
上の 1〜5 のためにまとめて行う。`regress:size`・`regress:run`・`regress:checkpoint`・`regress:wl`・
`regress:full` は流さない (size と run は、M3 の後に親が流す)。ESP32 のビルドはしない。
確認の手順は、後で回帰の場面にできるように、打ったキーの列を report に残す。

## 3. 範囲と止まる条件

### 触ってよい所 (fmruby-core)
- `main/app/fmrb_spx_editor.c`、`main/ruby/spinel/fmrb_editor_ffi.rb`
- `lib/gems/picoruby-fmrb-editor-core/` (束ねと、要れば `editor_core.c`・ヘッダ)
- `main/ruby/default_app/editor.app.rb`、`main/ruby/default_app/editor/*.rb`
- `main/app/fmrb_app_spawner.c` のうち、エディタの slot を返す所だけ
- `doc/editor_multi_file/report/m1.md`、`rake docs:index` の結果
- **触らない**: カーネルの Ruby (`main/ruby/kernel/`)、ほかのアプリ、回帰の道具と場面、
  sdkconfig、submodule の中、graphics-audio、直下 (family-mruby) のリポジトリ、`wasm/vendor/`。
  plan.md は親が直す。

### 作業場所と git
- 本体の checkout (`/home/kishima/fmrb/family-mruby/fmruby-core`)。worktree は作らない。
- `develop` からブランチ `editor-multi-file` を切り、そこへコミットする (この指示書を最初のコミットに
  入れる)。コミットの書き方は fmruby-core/CLAUDE.md の「コミット規約」。**push しない。develop へマージしない。**
- 終わったら sim・ブラウザ・開発サーバ・ネイティブ版を止める。build-linux/ は今の木から作ったものが
  残っていてよい (親が検収で使う)。ESP32 の `build/` には触らない。

### 止まる条件 (止まったら report に状況を書いて返す)
- ユーザの作業が要る (sudo・機器・目や耳での確認)。
- 見立てと食い違う事実 (例: slot を渡さない呼び方のままでは切り替えられない、Spinel で状態の保存が
  型の都合で書けない、文書の本体の作りを大きく変える必要がある)。
- 触ってよい範囲の外の変更が要る。
- **利用者から見た動きを、この指示書・plan に無い形で変える必要が出たとき** (今できる操作をできなくする、
  既定を変える、など)。選ばずに止まる。
- 今のエディタの不具合を見つけた (この段と関係なければ直さず、再現の手順を report に書く)。

## 4. Spinel で気をつけること (過去に踏んだもの)

- 型が絞れない呼び出しは、エラーにならず黙って 0 (nil) になる。配列に違う型を混ぜない。Hash に違う型の
  値を入れない。**生成 C (`main/ruby/spinel/gen/linux/` の editor_combined.c) を読んで確認する**。
- 逃げる proc (保存されるブロック) は外の局所変数を捕まえられない。
- 基底クラス・module に ivar を足すと、Spinel の ivar の並びが親子で食い違うことがある
  (doc/spinel_aot/reports/editor_ivar_layout_bug.md)。エディタが開いて 1 打鍵できることを、標準構成で
  必ず確認する。
- 強制インラインでスタックが溢れることがある。大きなメソッドに保存・戻しを全部インラインで書かない。
- module の ivar は、次のインスタンスの開始で nil に戻る (だから今の slot は C 側が覚えている。
  `fmrb_editor_ffi.rb` の説明を読む)。
- PicoRuby (mruby 版) の罠: Regexp 無し、`Array#pack` 無し、配列要素への多重代入が壊れる。

## 5. report に書くこと (doc/editor_multi_file/report/m1.md)

- 足した口 (C と Ruby) の名前と形、slot の組の持ち方。
- ivar の仕分けの表 (文書ごと・エディタに 1 つ) と、状態の持ち方、その理由。試してやめた持ち方。
- 受け入れ条件 1〜7 の結果 (流したもの・打ったキーの列・保存されたファイルの中身・`ec_mem_used` の値)。
- ブレークポイント・デバッグのペイン・型支援の診断の今の持ち方と、切り替えで起きること。
- 型支援が、複数の文書で領域が詰まっているときにどう断るか (見たなら)。
- 今のエディタをチェックポイントで移せるか (分かった範囲で。試さなくてよい)。
- VM プール・PSRAM の増え方の見積もり (測れたら値)。
- 見立てと違った点、踏んだ罠、残件、親・ユーザに決めてほしいこと。コミットの一覧。
