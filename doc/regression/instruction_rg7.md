# RG7 指示書: 画面の回帰を「起動と動作の確認」に絞る

> 2026-11-17 | 親セッションからサブ (Opus) への指示。決定の元は doc/reference/TODO.md の
> 「画面の回帰を『起動と動作の確認』に絞る (2026-11-17、ユーザ)」。

## 1. 背景と目的

`rake regress:screens` は sim (Retro・Modern)・ブラウザ版・ネイティブ版の 40 場面ほどを、画面全体の画素で
基準の PNG と比べている。メニューバーのように全画面に映る物を変えると基準がほぼ全部作り直しになる
(今がその状態: メニューバーのロゴと 3 段の帯の変更で、基準の大半が古い)。

これを、**構成ごとに「上がる・動く」だけを見る道具**に作り直す。描画の中身は描画の記録 (`rake regress`) が
受け持つので、ここでは見ない。

## 2. 作るもの

### 2.1 構成ごとに見ること (これだけ)

構成は今までどおり retro・modern・web・native (`FULL=1` で retro-mruby・modern-mruby)。

1. **デスクトップまで上がる**: 今の起動の待ち (Boot complete など) が通る。画面が真っ黒でない・1 色でない。
2. **エディタが開いて 1 打鍵が入る**: エディタの Window が出て、1 打鍵で文字の出る範囲の画面が変わる。
3. **アプリが 2、3 本開く**: パスで開き (今の `launch`・`open_app`)、アプリが動いていること
   (sim・ネイティブ版はプロセスの一覧、ブラウザ版はページから分かる方法) と、Window の出る範囲の画面が
   デスクトップから変わったことを見る。閉じて、いなくなることを見る。
   どのアプリにするかは、今の場面にあるもの (kamon・picoruby の見本・Config など) から、全構成で開ける
   ものを選ぶ。構成で開けないものがあれば、構成ごとに違ってよい。
4. **落ちていない**: 終わりに、コアのログにクラッシュの印 (`Guru`・`abort`・`Segmentation` など、sim・
   ネイティブ版・ブラウザ版それぞれで取れる範囲) が無く、まだ動いている。

### 2.2 比べ方

- **画面全体の画素の比較と、その基準の PNG はやめる**。`test/regress/screens/baseline/` の今の全面の基準は消す。
- 判定は、ログ・プロセスの一覧・「真っ黒でない」・「この範囲が変わった」(`wait_change(rect)`・`wait_until`) で行う。
- **領域を指定した比較 (`shot(rect:)`) は道具として残す**。基準はその領域だけの PNG にする。領域には
  メニューバー・タスクバー・時計・空きメモリの数字・Window の枠と題 (テーマで変わる) を**入れない**。
  今回、領域の基準を置くかどうかは任せる (置かなくても 2.1 が判定できるなら置かない。置くなら数枚まで)。
- 「場面の後でデスクトップが起動直後と同じ画面に戻る」確認は、同じ回の中の写しとの比較で基準の PNG を
  使わないので、安定しているなら残す。不安定の元になる (空きメモリの数字など) なら、その範囲を隠すか、
  外して report に書く。
- `regress:screens:update` は領域の基準のために残す (基準が 1 枚も無くなるなら、残すか消すかを report に書く)。

### 2.3 今ある、動きを見ている場面

`40_keys.rb` (native-keys: メニュー・切り替え・エディタ・ファイルマネージャのキー操作) と
`50_file_manager.rb` (`fmgr_remember`) は、絵ではなく動きを見ている。全面の基準なしで判定できる形
(ログ・範囲の変化・Window の中だけの領域の比較) に直せるものは直して残す。直すのに大きな手間が要る、
または判定が弱くなるものは、**消さずに外した状態で止め、report に一覧と理由を書く** (残すかは親が決める)。
`config.*`・`launcher`・`menu`・`kamon`・`picoruby_*`・`walk_scroller.*` の絵の場面は、2.1 の 3 に使う分を
除いてやめる。

### 2.4 ほかの道具を壊さない

- `rake regress:live` は `test/regress/screens/helpers.rb` の言葉を使っている。通ったままにする。
- `rake regress:full` の中の screens (`--skip-busy-web`、ビルドの使い回し `build_tree.rb`) はそのまま動くこと。
- `ONLY=`・`SCENE=`・`FULL=1`・`--list`・`--attach`・`--keep` は残す。
- 出力の形は今までどおり (通ったら 2 行、落ちたものだけ、6 つまで)。落ちたときは何を見て落ちたか
  (どの確認・どの範囲・写しの場所) が 1 行で分かること。

### 2.5 文書

- `doc/regression/usage.md`: 0 章の表 (時間と「何を見るか」)、9 章 (流れ・台本の書き方・基準と落ちたとき・
  分からないこと) を今の形に書き直す。「画面の中身は見ていない。変えた所の反映は sim で撮って目視か、
  領域を指定した比較で」と明記する。ほかの章で `regress:screens` を画素の比較として書いている所も直す。
- `doc/reference/TODO.md` のこの項目を消す (済んだら)。
- `rakelib/regress.rake` の説明文、`fmruby-core/CLAUDE.md`・直下の `CLAUDE.md`・
  `.claude/skills/` に `regress:screens` を画素の比較として書いている所があれば直す (grep する)。
- `doc/regression/report/rg7.md` に報告を書く (5 章)。`rake docs:index`。plan.md は親が直す。

## 3. 受け入れ条件

1. `rake regress:screens` (retro・modern・web・native) が通る。`FULL=1` も 1 回通す。時間を report に書く
   (今は 8 分・12.5 分。短くなるはず)。
2. `test/regress/screens/baseline/` に全面の PNG が残っていない。
3. **落とせることを示す**: 少なくとも次の 2 つを手で起こして、道具が FAIL と言うことを確認し、出力を
   report に貼る (起こすための変更はコミットしない)。
   - 無いパスのアプリを開く場面 (「アプリが開く」が落ちる)
   - 打鍵が入らない、または画面が変わらない場合 (「範囲が変わる」が上限の秒数で落ちる)
4. `rake regress` と `rake regress:live` が通る。
5. `git status` に、作業の前からあった変更 (`components/esp_littlefs`・`components/picoruby-esp32/picoruby`
   の `m`、`wasm/vendor/`) 以外の取りこぼしが無い。

確認は上のとおり。**重い道具はまとまりの最後に 1 回ずつ**。直している途中は `ONLY=`・`SCENE=`・`--attach` で
絞る。`regress:size`・`regress:run`・`regress:checkpoint`・`regress:wl`・`regress:full` は流さない。
ESP32 のビルドはしない。

## 4. 範囲と止まる条件

### 触ってよい所
- fmruby-core: `tools/regress/screens.rb` とその周り (`tools/regress/` の screens が使う部分)、
  `test/regress/screens/`、`rakelib/regress.rake` の screens の部分、2.5 の文書。
- 直下 (family-mruby) の `tools/` は、足りない物 (例: 写しの範囲の読み取り) があるときだけ、最小限。
  変えたら report に書く。
- **触らない**: ファームのソース (main/・components/・lib/・flash/)、ほかの回帰の道具の判定、
  graphics-audio、`wasm/vendor/`、submodule の変更。

### 作業場所と git
- 本体の checkout (`/home/kishima/fmrb/family-mruby/fmruby-core`) で作業する。worktree は作らない
  (sim は本体からしか動かない)。親はこの間ビルドと sim に触らない。
- fmruby-core で `develop` からブランチ `regress-screens-smoke` を切り、そこへコミットする
  (この指示書も最初のコミットに入れる)。直下を変えたら直下でも同じ名前のブランチ。
  コミットの書き方は fmruby-core/CLAUDE.md の「コミット規約」。**push しない。develop へマージしない。**
- 終わったら sim・ブラウザ・開発サーバ・ネイティブ版を止め、build-linux/ が戻っていること
  (道具の最後の行) を確認する。作業の前に build-linux/ に何が入っていたかを最初に記録しておく。

### 止まる条件 (止まったら report に状況を書いて返す)
- ユーザの作業が要る (sudo・機器・目や耳での確認)。
- ユーザの sim・ブラウザ版のページが動いていて道具が始められない (止めない。web だけなら `web SKIPPED` で
  ほかを進め、最後に report に書く)。
- 見立てと食い違う事実 (例: ブラウザ版でアプリが動いているかを知る方法が無い、デスクトップへ戻る確認が
  元から不安定)。小さく解けるなら解いて report に書き、道具の外 (ファーム) の変更が要るなら止まる。
- ファームの不具合を見つけた (直さない。再現の手順を report に書く)。
- 2.1 の範囲を変えないと受け入れ条件を満たせない。

## 5. report に書くこと (doc/regression/report/rg7.md)

- 構成ごとに何を見ることにしたか (アプリの選び方、領域の基準を置いたか) と、その理由。
- 時間 (構成ごと、全部、`FULL=1`)。変更の前と後。
- 落とせることの確認の出力 (受け入れ条件 3)。
- 2.3 の場面をどうしたか (直した・外した の一覧と理由)。
- 見立てと違った点、撤回した案、踏んだ罠。
- 残件と、親・ユーザに決めてほしいこと。
- コミットの一覧 (リポジトリごと)。
