# RG2 報告: 台本で sim とブラウザ版を動かし、画面を基準と比べる

> 状態: 完了 | 更新: 2026-10-26 | `rake regress:screens` を作り、`rake regress:full` に入れた。台本は Ruby の DSL (`test/regress/screens/*.rb`)、基準は PNG を git に入れた (28 枚・146 KB)。sim の Retro・Modern (標準構成と全 mruby) とブラウザ版で 9 場面・28 枚、`FULL=1` で 3 回続けて全部通った。実機は使っていない

## 1. 作り

| ファイル | 役目 |
|---|---|
| `tools/regress/screens.rb` | 入口。構成ごとにビルド → 起こす → 場面を流す → 比べる → 片付け。`--only`・`--scene`・`--full`・`--update`・`--list`・`--attach`・`--keep`・`--port` |
| `tools/regress/screens/dsl.rb` | 台本の書き方 (`Regress::Screens.scene`) と場面の中の `s` (Session): 入力、画面で待つ、隠す範囲、`shot` |
| `tools/regress/screens/drivers.rb` | sim (ルートの `fmrb_input.rb`・`fmrb_screenshot.py`・debug server の spawn/ps/kill) とブラウザ版 (`fmrb_web.rb`) を同じ顔で包む |
| `tools/regress/screens/image.rb` | 写しの読み (ルートの `fmrb_png.rb`)、隠す範囲を除いた画素の比較、PNG の書き出し (写しと差分の絵) |
| `tools/regress/build_tree.rb` | `keep_built!` を足した: 借りている間に作った wasm の libmruby を退避した古いものと入れ替え、戻したときに新しいものが残るようにする |
| `rakelib/regress.rake` | `regress:screens` (ONLY=・SCENE=・FULL=1・WEB_PORT=)・`regress:screens:update`。`regress:full` に足した |
| `test/regress/screens/helpers.rb`・`10_desktop.rb`・`20_editor.rb`・`30_apps.rb` | 共通の手順と場面 |
| `test/regress/screens/baseline/<構成>/<場面>.png` | 基準 28 枚 |

- ルートの tools/ は読むだけで済んだ (必要なものはどれもコマンドとして呼べた)。

### 1.1 構成

| 構成 | ビルド | 場面 |
|---|---|---|
| `retro` | NARYAv3 の Linux ビルド、targets.rb の `FIRMWARE_ENV` (Spinel のカーネル・エディタ、mruby のデスクトップ) | 8 |
| `modern` | TAB5 の Linux ビルド、同じ | 10 枚 (9 場面) |
| `retro-mruby`・`modern-mruby` (`FULL=1`) | カーネル・デスクトップ・エディタを mruby に | 2 ずつ (デスクトップ、エディタの 1 打鍵) |
| `web` | `rake spinel:gen wasm:mruby wasm:web` (標準構成) | 6 |

- 全 mruby の構成はビルドが 2 回増える (約 3.6 分) ので `FULL=1` の別口にした (指示書の許す形)。`rake regress:full` は標準の 3 つだけ流す (`FULL=1` を付ければ全部)。
- ブラウザ版は**自分の開発サーバ (port 8016) とヘッドレスのブラウザ**を起こす。port 8006 の `rake wasm:serve` (親・人のもの) は使わない。別の port は別の origin なので、ページの保存 (/home と設定) も人のものと混ざらない。始める前に `fmrb_web.rb reset` (Erase と同じ) で保存を消し、出荷時の設定から始める。
- `tools/fmrb_web.rb` が起こしたブラウザ (`wasm/build/drive_browser.pid`) があるとき、使う port がふさがっているとき、sim が動いているときは始めない。

### 1.2 場面

| 場面 | 構成 | 見るもの |
|---|---|---|
| `desktop` | 5 つ全部 | 起動したデスクトップ (壁紙の BMP、アイコン、メニューバー) |
| `menu` | retro・modern・web | メニューの開いた絵 |
| `launcher` | retro・modern・web | ランチャー (アイコンの BMP) |
| `config` | retro・modern・web | Config の画面 (Cancel で閉じる) |
| `editor_type` | 5 つ全部 | メニューからエディタを開き `a` を 1 打鍵 (CLAUDE.md の必須の確認。全 mruby では Debug メニューのある mruby 版が出る) |
| `picoruby_shapes` | retro・modern | PicoRuby demo の最初の頁: 角丸・楕円・三角 (LovyanGFX の本当の画素) |
| `picoruby_fonts_ja` | retro・modern | 同じアプリの Fonts の 2 頁: misaki 8 と efont 12 の日本語 |
| `kamon` | retro・modern・web | FmrbUI の部品 (ボタン・数の入力・スライダー) と日本語の札。ブラウザ版はランチャーの矢印キーと Enter で開く |
| `walk_scroller` (start・next) | modern | 全画面のスプライトと背景。`n` で次の印まで歩かせた後の絵も撮る (Retro は storage から外れている) |

- mruby のアプリは picoruby demo・kamon・walk_scroller の 3 本。デスクトップ (mruby) とエディタ (Spinel と mruby) を数えると、sim では 5 アプリ以上の画面を比べている (plan.md 5 章の受け入れ条件)。
- **各場面の後で、デスクトップが起動直後の画面と画素で同じに戻ることを確認する** (指示書には無い足し)。sim は場面が開いたアプリを debug server の ps と kill で閉じる。ブラウザ版はプロセスの一覧が無いので、戻らないときだけページを読み直す。ダイアログを閉じた跡が残る不具合 (doc/ui_widgets で手で見ていた種類) がこれで落ちる。作っている途中、Config が Esc で閉じない (Cancel で閉じる作り) のをこの確認が見つけた (不具合ではない。台本を Cancel に直した)。

### 1.3 台本の書き方 (plan.md の未確定事項 2 → Ruby の DSL)

```ruby
Regress::Screens.scene "picoruby_fonts_ja", on: %w[modern retro] do |s|
  s.open_app "/app/demo/picoruby.app.rb"
  s.wait_stable
  s.key "tab"     # Shapes -> Fonts
  s.wait_change
  s.key "right"   # page 2
  s.wait_change
  s.park
  s.shot
end
```

- 待つのは画面の条件: `wait_stable(quiet:)` (その秒数だけ変わらない)、`wait_change(rect)` (直前の入力の前の画面から変わる)、`wait_until { |img| ... }`、`wait_pixel`、`wait_log` (sim のログ)。どれも上限の秒数を持つ。時間で待つ `pause` はアプリを開いた直後の 0.5 秒だけに使っている。
- `shot` は「落ち着くのを待つ → 基準があれば基準と同じになるまで最大 8 秒待つ → 比べる」。描き終わりの遅れで落ちず、最後まで違えば落ちる。
- 窓が出たことは題の色の画素で待つ (`wait_title`)。テーマが sim (Light) とブラウザ版 (Cyberpunk) で違うので、色は helpers.rb が構成ごとに返す。Config の Cancel はボタンの色の塊を下から探して押す (画面と行の数で位置が変わるため)。

## 2. 揺れと隠した範囲

- 隠したのは 2 つだけ (helpers.rb の `before_scene`、全場面):
  - メニューバーの時計: `[w - 90, 0, 90, 13]`。デスクトップが `draw_wallclock(window_width - 90, 2, ...)` で描く所で、日付と秒が入る。デスクトップを 8 秒見て変わったのは x 408〜418 の秒の所だけだった (雲などは動かない)。sim の空きメモリは `---KB` で動かない。
  - 置いたポインタ: `[w - 6, 60, 6, 16]`。ポインタは絵に描き込まれるので、撮る前に決まった場所 (右の空) へ動かす。ブラウザ版はページを読み直した直後の移動を描かないことがあり (同じ移動で絵にポインタが出たり出なかったりした)、見る物でもないので隠した。
- 幅 (許す色の差) は 0。sim の写しは共有メモリ、ブラウザ版はフレームバッファをそのまま読むので、同じ版なら完全に同じ画素になる。3 回の通しで 1 画素も揺れなかった。
- 揺れかけた所 (台本で直した):
  - walk_scroller で `n` を押した後、歩いている途中の絵を「落ち着いた」と取った。前後 2 枚が同じなら落ち着いたとしていたのを、`quiet` 秒 (既定 1 秒、この場面は 2.5 秒) 変わらないまで待つ形にした。
  - ブラウザ版でランチャーの添字が 1 つずれた (ランチャーは最初のアイコンを選んだ状態で開く)。
  - 起動直後のブラウザ版でポインタの移動が落ちる。起動後の `park` を 2 回にし、ポインタの所を隠した。
- 種類を切り替えた直後の 1 回目の起動が上がらない件 (rg3b.md) は sim_run と同じく起こし直しで受けた。通しの間に起こし直しが要ったかどうかは数えていない (上がるまでの時間に含まれる)。

## 3. 確認

### 3.1 3 回続けて

`rake regress:screens FULL=1` を 3 回続けて流し、3 回とも `28 passed / 0 failed`。

| 回 | 全体 | retro | modern | retro-mruby | modern-mruby | web |
|---|---|---|---|---|---|---|
| 1 | 12.4 分 | 81 + 62 秒 | 83 + 67 秒 | 62 + 44 秒 | 62 + 43 秒 | 71 + 124 秒 |
| 2 | 12.7 分 | 85 + 62 秒 | 82 + 68 秒 | 67 + 44 秒 | 67 + 44 秒 | 77 + 125 秒 |
| 3 | 12.6 分 | 89 + 63 秒 | 90 + 69 秒 | 66 + 44 秒 | 67 + 44 秒 | 77 + 99 秒 |

(ビルド + 起動から片付けまで。全体は rake の壁時計。)

- 標準 (`FULL` なし、retro・modern・web) は 1 回流して 8.2 分 (`24 passed / 0 failed`、retro 80 + 62 秒、modern 80 + 70 秒、web 73 + 97 秒)。`rake regress:full` はこれまでの 9 分ほど (usage.md 7 章) + 8 分で 17 分ほどになる (通しては流していない)。
- ブラウザ版の 100〜125 秒のうち 70 秒ほどはページの読み直し 2 回 (エディタと kamon の後。窓を閉じる口が無いため) と起動直後に落ち着くのを待つ分。
- sim の場面 1 つは 2〜8 秒 (エディタ・アプリは閉じて戻るのを含む)。

### 3.2 わざと壊す

`rake regress:screens ONLY=modern SCENE=kamon,picoruby_shapes,desktop` で 2 つ同時に壊した:

- 基準の画像をいじる: `modern/kamon.png` の (200,100) から 4x4 を黒に塗った → `FAIL modern kamon: 9 px in (200,100)-(203,103)` (16 画素のうち 7 画素は元から黒)。
- アプリの文字を 1 px 動かす: `flash/app/demo/picoruby.app.rb` の `"round_rect"` の札の x を +1 → `FAIL modern picoruby_shapes: 152 px in (25,77)-(84,83)`。差分の絵で札だけが赤紫になった。
- どちらも戻して同じコマンドで `3 passed / 0 failed`。`git status` で flash/ に差が無いことを確認した。
- 出力は 4 行 (build/ の行、FAIL 2 行、要約)。

### 3.3 出力の長さ

- 通ったとき: rake の出力は 2 行 (`build/: TAB5 (esp32p4)` と `screens: 28 passed / 0 failed (retro 8/8, ..., 11.9 min: retro build 89s + run 63s, ...)`。要約の行は 260 字ほど)。
- 落ちたとき: 落ちたものを 6 つまで 1 行ずつ (差分の絵の場所つき)、残りは数と results.json の場所。最後に `regress:screens: failed`。
- 写し・差分の絵・コアのログ・ビルドのログは `tmp/regress/screens/` にあり、場所だけ出す。

## 4. 基準の大きさ (plan.md の未確定事項 4 → git に入れる)

- 28 枚で 146 KB (1 枚 3〜14 KB、平均 5 KB。RGB の PNG を zlib の最大圧縮で)。目安の 5 MB に遠いので、画素の要約 (領域ごとのハッシュ) の案は要らないと判断した。
- 場面を倍にしても 0.3 MB ほど。全画面の 640x360 の場面を足すと 1 枚 10〜20 KB になる見込み。

## 5. R6 の手で撮った 19 枚のうち、置き換わったもの

| R6 の確認 | 枚数 | 台本 |
|---|---|---|
| sim Modern 標準: エディタを起動して 1 打鍵 | 2 (4 のうち) | `modern/editor_type` |
| sim Modern 標準: MIDI APU の SMF で詰めた `..` の字形 | 2 (4 のうち) | 置き換わっていない (SMF のファイルを選ぶ操作が要る。字形の画素を見る場面は足していない) |
| sim Modern、Spinel のデスクトップ (指示外): デスクトップ、ファイルマネージャ | 2 | 置き換わっていない (Spinel のデスクトップは構成に入れていない) |
| sim Modern 全 mruby: エディタの 1 打鍵 | 2 | `modern-mruby/editor_type` (FULL=1) |
| sim Retro 標準: エディタの 1 打鍵 | 2 | `retro/editor_type` |
| sim Retro 全 mruby: エディタの 1 打鍵 | 2 | `retro-mruby/editor_type` (FULL=1) |
| ブラウザ版: デスクトップ、エディタの 1 打鍵、ランチャーから MML を開いて行をクリック | 7 | デスクトップ・エディタ・ランチャーは `web/desktop`・`web/editor_type`・`web/launcher`。アプリは MML ではなく kamon (`web/kamon`)。MML の行のクリックは置き換わっていない |

- 19 枚のうち 13 枚ほど (エディタの 1 打鍵の 8 枚、ブラウザ版の 5 枚ほど) が台本に置き換わった。残りは SMF の字形 2 枚、Spinel のデスクトップ 2 枚、ブラウザ版の MML のクリック 2 枚ほど。どれも場面を足せば入る (6 章)。
- R6 で手でやっていた「Retro のために `FMRB_HW_TARGET=NARYAv3` でビルドし、docker compose・screenshot・spawn・input を scratchpad のスクリプトで組む」は道具の中に入った。

## 6. 残件・指示との違い

- 指示との違い:
  - 場面の後に「デスクトップが元に戻る」確認を足した (1.2)。
  - 全 mruby の構成は `FULL=1` の別口。`rake regress:full` には標準の 3 つだけが入る。
  - 写しの基準の更新 (`rake regress:screens:update`) は流し直さず、前回の結果の落ちた・新しい写しを写す (size・run の update と同じ考え方)。
  - ブラウザ版は自分の開発サーバを port 8016 に起こす (`WEB_PORT=` で変える)。`.env` の `PORT` (wasm:serve 用) と混ざらないように名前を分けた。
- 残件:
  - 場面を足す候補: MIDI APU の SMF の行 (`..` の字形)、ブラウザ版の MML の行のクリック、Spinel のデスクトップの構成 (ファイルマネージャ)、全画面の高解像度 (640x360) のアプリ (hires_test など)。
  - ブラウザ版はエディタやアプリを閉じる口が無いので、そういう場面の後はページを読み直す (1 回 35 秒ほど)。窓の閉じるボタンを押す形にすれば縮められるが、エディタは未保存の確認が出るので今回はしていない。
  - CI で回すか (plan.md の未確定事項 3) は決めていない。docker・emsdk・Chrome が要る。
  - ランチャーの絵はアプリを 1 本足すと変わる (並びが変わる)。そのときは `launcher` の場面と、ブラウザ版の kamon の添字 (`web_index: 11`) を見直す。
  - Retro のエディタの窓の位置が標準 (左端 x=5) と全 mruby (x=107) で違った。どちらも毎回同じなので基準はそのまま取った (原因は追っていない。不具合かどうかも見ていない)。
  - `doc/README.md` の索引の更新 (`rake docs:index`) は doc/regression の文書をコミットするときに親が行う。

## 7. コミット (fmruby-core develop、push していない)

- bb8b368f tools: rake regress:screens, scripted screens on the sim and the browser build (RG2)
- eee81710 test: screen scenes and their baseline PNGs for rake regress:screens
- eafd9313 doc: rake regress:screens, the scripted screen comparison, in the regression usage (usage.md だけ。件名は usage.md のこれまでのコミットに合わせて `doc:`)

指示書 (instruction_rg2.md) とこの report はコミットしていない。わざと壊した変更 (基準の画像、picoruby.app.rb) は戻した。build/ は始める前と同じ TAB5 (esp32p4)、wasm/build は今の develop (と未コミットの道具。ファームの中身は同じ) から作ったもの。sim・ブラウザ・port 8016 のサーバは止めた。port 8006 の `rake wasm:serve` は触っていない。
