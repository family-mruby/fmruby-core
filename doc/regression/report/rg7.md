# RG7 報告: 画面の回帰を「起動と動作の確認」に絞る

> 状態: 完了 | 更新: 2026-11-17 | `rake regress:screens` を、構成ごとに上がる・エディタに 1 打鍵が入る・アプリが 3 本開いて閉じる・落ちていないだけを見る道具に作り直した。画面全体の画素の比較と基準の PNG 46 枚をやめ、判定はログ・プロセスの一覧・真っ黒でない・範囲が変わる・同じ回の写しとの一致で行う。領域を指定した比較 (`shot(rect:)`) は道具として残し、基準はその領域だけの PNG にした (今は 0 枚)。時間は 8 分 → 6.8 分、`FULL=1` は 12.5 分 → 10.3 分。指示書は instruction_rg7.md

## 1. 構成ごとに見ること

| 場面 | 構成 | 見ること |
|---|---|---|
| (起動) | 全部 | 今までどおり「Boot complete」(sim・ネイティブ版はコアのログ、ブラウザ版はページが 2 回続けて答える) |
| `boot` | retro・modern・web・native・-mruby | 画面が 1 色でなく、95% 以上が黒でない (`wait_not_blank`) |
| `editor_type` | 同上 | メニューからエディタ: 左上の Window の本体の範囲 `[24, 48, 100, 60]` が起動直後のデスクトップから変わる、プロセスの一覧 (sim・ネイティブ版) に `/Editor/` がいる。`a` を 1 打鍵してエディタの 1 行目 `[4, 30, 40, 22]` が変わる |
| `apps` | 同上 | `/app/demo/kamon.app.rb`・`/app/demo/picoruby.app.rb`・`/app/lua/lua.app.lua` を順にパスで開いて並べる。それぞれ Window の本体の範囲が変わり、一覧にいる。1 秒置いて 3 本とも一覧にいて、範囲はデスクトップと違う。後から開いた順に kill して、一覧から消える |
| `fmgr_remember` | retro・modern・native | ファイルマネージャを決まった木で開き、alpha に入った画面を写し、Esc で閉じてデスクトップに戻り、開き直した画面がその写しと同じ (メニューバーより下、タスクバーの行より上の範囲。同じ回の写し同士) |
| `keys_boot`・`keys_editor_type`・`keys_switcher`・`keys_fmgr_remember` | native-keys | 2.3 のとおり直したもの (3 章) |
| `alive` (場面の後) | 全部 | sim・ネイティブ版: debug server がプロセスの一覧を返す (sim はコンテナも動いている)、コアのログにクラッシュの印が無い。ブラウザ版: ページが `running=true` を返す |

- アプリの選び方: 今の場面にあった kamon と picoruby の見本 (mruby) に、言語の違う Lua の見本を足して 3 本。どれも網・センサ・入力なしで上がり、sim・ブラウザ版・ネイティブ版の全構成で開けた (構成で分ける必要はなかった)。BASIC の見本 (`/app/basic/basic.app.bas`) は全画面で、プログラムが終わると自分で閉じる (5 秒ほどで一覧から消える) ので「動いている」を見るのに向かず外した。walk_scroller は Modern だけで、全画面の高解像度 (640x360) に切り替わるので外した。
- 範囲の決め方: 新しい Window はどの構成でも左上に出て (開いているものがあると数 px ずつ下へ)、本体はテーマ・題に関わらず中身で変わる。メニューバー・タスクバー・時計・空きメモリ・Window の枠と題は範囲に入れていない。ファイルマネージャの範囲は広い (メニューバーより下のほぼ全部) が、比べるのは同じ回の写しなので基準の PNG は要らない。
- **領域の基準は置かなかった**。どの場面も 2.1 の確認だけで判定できた。`shot(rect:)` と `rake regress:screens:update` は道具として残した (基準は `test/regress/screens/baseline/<構成>/<場面>.png` に、その領域だけの PNG として書く)。0 枚のときも `--list` が `region baselines: 0 PNGs` と出す。
- **デスクトップへ戻る確認は残した**。基準の PNG ではなく、同じ回の起動直後の写しと比べる (時計は今までどおり隠す)。下の 4 回 (作業中の 2 回と受け入れ条件の 2 回) で一度も揺れなかった。sim・ネイティブ版の空きメモリの欄は `---KB` のままで動かないので隠していない。
- 場面は「落ちずに終わり、何か 1 つでも確かめた」ときに通る。通った場面ごとに確かめたことの一覧を `results.json` の `confirmed` に書く (例: `editor_type` は「the desktop's menu」「the editor's Window」「the editor in the process list」「the typed key in the editor's text」)。
- クラッシュの印は `tools/regress/boot_log.rb` の `CRASH` (Guru Meditation・`abort()`・Backtrace・stack overflow・assert failed ほか) に、Linux のプロセスの死に方 (`Segmentation fault`・`core dumped`・`AddressSanitizer`・`terminate called`・SIGSEGV・SIGABRT) を足したもの。ブラウザ版はファームのログがブラウザの console に出て道具から読めないので、印は見ていない (ページが答えて machine が動いていることだけ)。

### 1.1 道具の変更

- `tools/regress/screens/dsl.rb`: `shot` は `rect:` が要る (無いと落ちる)。基準と写しはその領域を切り出したもの。確認の言葉を足した: `confirm`・`wait_change(rect, what:)`・`wait_differs(写し, rect)`・`wait_same(写し, rect)`・`wait_not_blank`・`apps`・`wait_running(pid)`・`close_app(pid)`・`desktop_at_rest`。`launch` は pid を返し、開けないパスはその場面の失敗 (`<path> did not open (...)`)。待ちの失敗は確認の名前と範囲を言う。
- `tools/regress/screens/image.rb`: `crop`・`colour_stats`。
- `tools/regress/screens/drivers.rb`: 3 つの driver に `launch` (pid)・`kill`・`alive?`・`core_log`・`log_name` を揃え、クラッシュの印 (`Screens::CRASH`・`crash_lines`)。sim の debug client の誤り (RuntimeError) を DriverError に包んだ (前は 1 つの場面の誤りが構成ごと落としていた)。
- `tools/regress/screens.rb`: 場面の後に `alive`。通った場面を 1 行ずつ結果に入れ、`N passed` は通った場面と `alive` の数。落ちた場面はその時の画面を `tmp/regress/screens/out/<構成>/<場面>.fail.png` に書いて行に付ける。落ちた行 (場面の誤りと領域の比較の両方) を合わせて 6 つまで。
- `tools/regress/live.rb` と `tools/regress/origin_compare.rb` (どちらも screens の Runner を継ぐ) は、画面全体の shot を使い続け、場面ごとの行と `alive` を出さないよう 3 行ずつ足した (`whole_shots?`・`record_scene_ok`・`check_alive?`)。origin_compare は一度きりの道具で、流していない。
- `test/regress/screens/helpers.rb`: テーマの色 (`title_rgb`・`button_rgb`)・`wait_title`・`click_button` をやめ、範囲 (`window_body_rect`・`editor_text_rect`・`below_menu_bar_rect`)・`open_app_checked`・`wait_app_named`・`wait_for_apps` を足した。メニューの項目は先頭の 3 つ (launcher・editor・file_manager) だけを持つ (メニューに項目が増えても動かない)。
- 直下 (family-mruby) の tools/ は変えていない。

## 2. 時間

| | 前 (指示書の値) | 後 (2026-11-17 実測) |
|---|---|---|
| `rake regress:screens` | 8 分 | 6.8 分 (`real` 7 分 6 秒) |
| `rake regress:screens FULL=1` | 12.5 分 | 10.3 分 (`real` 10 分 50 秒) |

構成ごと (後。ビルド + 流す時間):

| 構成 | 標準の回 | `FULL=1` の回 |
|---|---|---|
| retro | 78 秒 + 33 秒 | 87 秒 + 52 秒 |
| modern | 71 秒 + 49 秒 | 98 秒 + 35 秒 |
| retro-mruby | - | 67 秒 + 27 秒 |
| modern-mruby | - | 69 秒 + 29 秒 |
| web | 11 秒 + 114 秒 | 12 秒 + 86 秒 |
| native | 3 秒 + 19 秒 | 3 秒 + 22 秒 |
| native-keys | 3 秒 + 27 秒 | 3 秒 + 27 秒 |

- 場面そのものは sim で 1 構成 10〜20 秒 (`boot` 0.4 秒、`editor_type` 3 秒、`apps` 3 秒、`fmgr_remember` 7 秒)。残りは起動と、**sim のビルド (1 構成 1.1〜1.6 分) が大半**。sim の構成の流す時間 (33〜52 秒) の多くは 3 コンテナの起動と Boot complete 待ち。
- ブラウザ版とネイティブ版のビルドは、作業中の回で作ったものが新しいままなので数秒で済んでいる (wasm/build と native/build が古ければ、ブラウザ版の作り直しに数分かかる)。
- ブラウザ版はプロセスの一覧が無く、`editor_type` と `apps` の後の片付けがページの読み直し (1 回 30 秒ほど) になるので、流す時間の大半がそれ (場面の時間 37 秒のうち 33 秒ほど)。

## 3. 2.3 の場面をどうしたか

外した場面は無い。全部直して残した。

| 場面 | 前 | 後 |
|---|---|---|
| `keys_menu` | 起動直後の画面を基準と比べる | `keys_boot`: 落ち着くのを待って、真っ黒でない・1 色でない |
| `keys_editor_type` | 1 打鍵の後の画面を基準と比べる | Enter で画面が変わる、一覧にエディタ、`a` で 1 行目 `[0, 12, 40, 24]` が変わる |
| `keys_switcher` | 切り替えの一覧の画面を基準と比べる | エディタ・シェルを開くたびに画面が変わって一覧にいる、2 つが並んで動いている、Ctrl+Tab で切り替えの一覧が出て (画面が変わる)、Esc で直前のシェルの画面に戻る (同じ回の写しと一致) |
| `fmgr_remember` | 開き直した画面を基準と比べる | 1 章のとおり、alpha に入った画面の写しと開き直した画面を比べる |
| `keys_fmgr_remember` | 同上 | 同じ形 (全画面なので範囲は画面全体。時計は隠す) |

判定の強さ: ファイルマネージャは、覚えていなければ開き直したときに決まった木の頭 (`..`・alpha・beta・gamma.txt) を出すので、alpha の中の写しと一致しない。同じ回の写しなので、ファイルマネージャの見た目を変えても動かない。やめた絵の場面は `menu`・`launcher`・`config` (4 枚)・`kamon`・`picoruby_shapes`・`picoruby_fonts_ja`・`walk_scroller` (2 枚)。kamon と picoruby の見本は `apps` で開いている。

## 4. 落とせることの確認 (受け入れ条件 3)

一時の場面のファイル `test/regress/screens/99_fail_demo.rb` を置いて (コミットしていない、確認の後に消した) retro・modern・web・native で流した:

```ruby
Regress::Screens.scene "fail_missing_app", on: %w[modern retro web native] do |s|
  s.open_app_checked("/app/demo/no_such_app.app.rb")
end
Regress::Screens.scene "fail_no_key", on: %w[modern retro web native] do |s|
  s.open_editor
  s.mark
  # no key typed: the region has to stay as it is
  s.wait_change(s.editor_text_rect, what: "the typed key in the editor's text")
end
```

`ruby tools/regress/screens.rb` (全構成) の出力:

```
build-linux/: linux sim (retro)
FAIL modern fail_missing_app: /app/demo/no_such_app.app.rb did not open (debug client spawn path=/app/demo/no_such_app.app.rb: {"error": -7, "cmd": "spawn"}) (screen: tmp/regress/screens/out/modern/fail_missing_app.fail.png)
FAIL modern fail_no_key: the typed key in the editor's text: nothing changed in [4, 30, 40, 22] within 10s (screen: tmp/regress/screens/out/modern/fail_no_key.fail.png)
FAIL retro fail_missing_app: /app/demo/no_such_app.app.rb did not open (debug client spawn path=/app/demo/no_such_app.app.rb: {"error": -7, "cmd": "spawn"}) (screen: tmp/regress/screens/out/retro/fail_missing_app.fail.png)
FAIL retro fail_no_key: the typed key in the editor's text: nothing changed in [4, 30, 40, 22] within 10s (screen: tmp/regress/screens/out/retro/fail_no_key.fail.png)
FAIL web fail_missing_app: /app/demo/no_such_app.app.rb did not open (fmrb_web.rb launch /app/demo/no_such_app.app.rb: page: launch /app/demo/no_such_app.app.rb failed (error -7)) (screen: tmp/regress/screens/out/web/fail_missing_app.fail.png)
FAIL web fail_no_key: the typed key in the editor's text: nothing changed in [4, 30, 40, 22] within 10s (screen: tmp/regress/screens/out/web/fail_no_key.fail.png)
     ... 2 more (tmp/regress/screens/results.json)
screens: 24 passed / 8 failed (modern 5/7, retro 5/7, web 4/6, native 5/7, native-keys 5/5; 8.0 min: modern build 74s + run 47s, retro build 85s + run 66s, web build 12s + run 132s, native build 4s + run 33s, native-keys build 4s + run 26s)
```

`results.json` の残りの 2 つ:

```
native fail_missing_app: /app/demo/no_such_app.app.rb did not open (fmrb_native.rb app spawn /app/demo/no_such_app.app.rb: app spawn: {"error": -7, "cmd": "spawn"}) (screen: tmp/regress/screens/out/native/fail_missing_app.fail.png)
native fail_no_key: the typed key in the editor's text: nothing changed in [4, 30, 40, 22] within 10s (screen: tmp/regress/screens/out/native/fail_no_key.fail.png)
```

- 4 構成とも、無いパスは「開かない」で、打鍵の無い場合は「範囲が変わらない」で上限 (10 秒) で落ちた。後者は、エディタのカーソルが点滅しない (打たなければ 1 行目が変わらない) ことの確認にもなっている。
- `alive` が落ちることは、実際にクラッシュを起こしては確かめていない。印の正規表現だけを手で確かめた (`Guru Meditation Error: Core 0` と `Segmentation fault (core dumped)` を拾い、`I abort the plan` は拾わない)。

## 5. 受け入れ条件の結果

1. `rake regress:screens`: `screens: 24 passed / 0 failed (retro 5/5, modern 5/5, web 4/4, native 5/5, native-keys 5/5; 6.8 min: ...)`。`rake regress:screens FULL=1`: `screens: 32 passed / 0 failed (modern 5/5, modern-mruby 4/4, retro 5/5, retro-mruby 4/4, web 4/4, native 5/5, native-keys 5/5; 10.3 min: ...)`。どちらも最後に `build-linux/: linux sim (retro)`。
2. `test/regress/screens/baseline/` は 46 枚 (220 KB) を `git rm` して空 (ディレクトリごと無い)。
3. 4 章。
4. `rake regress`: `host: 33 suites, 33 passed, 0 failed (49.4 s)` / `regress: 233 runs (82 scenes on 4 profiles), 233 passed, 0 failed (1.1 s)`。`rake regress:live`: `live: 17 passed / 0 failed (modern 17/17; 3.2 min: modern build 96s + run 95s)` (walk_scroller の画面全体の shot もそのまま通った)。
5. `git status` の残りは、作業の前からの `m components/esp_littlefs`・`m components/picoruby-esp32/picoruby`・`?? wasm/vendor/` と、親が足した `?? doc/editor_multi_file/` だけ (`rake docs:index` がこのテーマの行を索引に足したのはコミットに入っている)。sim・ブラウザ・開発サーバ・ネイティブ版は止まっていて、build-linux/ は作業の前と同じもの (`linux sim (retro)`、NARYAv3、印の HEAD 058ac732・作った時刻 2026-11-17 18:17) に戻った。`flash/var/cache/launcher_index` (`--keep` で流した 1 回が残したもの) は手で消した。

## 6. 見立てと違った点・踏んだ罠

- ブラウザ版でアプリが動いているかをページから知る口は無い (`fmrb_web.rb status` は machine が動いているか・フレームの番号まで)。launch の答え (pid か、無いパスなら `failed (error -7)`) と Window の範囲で見ることにし、閉じたことは道具の読み直しの後にデスクトップへ戻ることで見ている。指示書の止まる条件の「小さく解けるもの」として進めた。
- ブラウザ版のコアのログも道具から読めない (ファームの出力はブラウザの console)。`alive` はページが答えることだけ。
- sim の debug client の誤り (無いパス) は RuntimeError で、前の道具では場面の失敗でなく構成ごとの失敗になっていた。DriverError に包んだ。
- `launch` の戻りが driver ごとに違っていた (sim は pid、ブラウザ版は `{"pid"=>N}`、ネイティブ版は答えの Hash)。pid に揃えた。
- BASIC の見本は全画面で、終わると自分で閉じる。手で開いたとき、それが閉じた後に開いた Lua の Window の後ろに BASIC の出力が残って見えた (2 秒後の写し)。全部を閉じるとデスクトップは起動直後と同じに戻った。**確かめていない** (BASIC が閉じたのと Lua を開いたのがどちらが先かを見ていない)。再現の手順: Modern の sim で `/app/basic/basic.app.bas` を spawn し、終わる前後に `/app/lua/lua.app.lua` を spawn して写す。
- 撤回した案: 閉じるのをブラウザ版でも Window の閉じるボタンのクリックで行う案。ボタンの位置が Window の幅とテーマで変わり、探すのに色を使うことになるのでやめた。

## 7. 残件と、親・ユーザに決めてほしいこと

- ブラウザ版の片付けの時間: `editor_type` と `apps` の後に毎回ページを読み直す (1 回 30 秒ほど、ブラウザ版の流す時間の大半)。縮めるなら (a) エディタを Ctrl+Q などで自分で閉じる、(b) ページに「動いているアプリの一覧」と「pid で閉じる」口を足す (ファームと wasm/web の変更なので今回の範囲の外)。(b) なら「動いている」「閉じた」をブラウザ版でも見られる。どれにするか (しないか)。
- ブラウザ版のコアのログを道具で読む口 (ページがログの行を貯めて drive の命令で返す) を足すか。足せば `alive` のクラッシュの印がブラウザ版でも見られる。
- 領域の基準は 0 枚。`rake regress:screens:update` は残した (領域の shot を足したときに使う)。0 枚の間も残すかを決めてほしい (消すなら rake のタスクと `--update` と usage.md 9.2 の 1 行)。
- `FULL=1` の構成 (retro-mruby・modern-mruby) には今までどおり `fmgr_remember` を入れていない。入れるか。
- plan.md への反映は親が行う。

## 8. コミット

fmruby-core (ブランチ `regress-screens-smoke`、develop から。push していない):

- `379b2189` docs(regression): instructions for narrowing the screen regression to boot and run checks
- `eaac6b5c` test(regression): screens check that each configuration comes up and runs, not its pixels
- `6e8a89ad` docs(regression): screens described as boot and run checks, not a pixel comparison
- (この報告) docs(regression): report on narrowing the screen regression

直下 (family-mruby) は変えていない。
