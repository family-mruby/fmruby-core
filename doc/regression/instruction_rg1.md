# RG1 (+RG5 の入口) 指示書: 描画の記録の比較を、麻雀からほかのアプリと FmrbUI に広げる

> 状態: 指示 | 更新: 2026-10-25 | plan.md の RG1 と RG5 の最初の形。麻雀の検査の道具 (`tools/mahjong_overflow/check.rb`) の仕組み (アプリの Ruby を CRuby に読み、描画を記録する代役で決まった場面を描く) を一般化し、ほかのアプリと FmrbUI の画面の描画の記録を基準と比べる。sim もファームも使わない (2026-10-25 ユーザ決定: 麻雀の道具の一般化)。**実機は使わない**。目的は、次の app_refactor R6 (FmrbUI を FmrbText で詰める・HitAreas への統合・5 アプリの載せ替え) の確認を、画面を撮らずに済ませること。**基準は R6 の前の今のコードで取る**

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントは英語。

## 1. 読むもの

- `tools/mahjong_overflow/` (check.rb・fixtures.rb・fixture_host.rb) と `rake mahjong:overflow`、report (doc/ruby_asterism/report/mj5.md の「overflow checker」、doc/app_refactor/report/r5.md の `--dump` の使い方)。
- 対象のアプリと部品: オセロの卓 (`flash/app/game/othello/`)、お絵かきボード (`flash/app/game/board/`)、walk_scroller (`flash/app/demo/`)、step_sensor (`flash/app/test/`)、オセロ (直接) (`flash/app/test/othello.app.rb`)、MML (`flash/app/demo/mml.app.rb`)、StackChan Remote・動画の再生・rover_brain (R6 で載せ替える 5 アプリ)、FmrbUI を使う mruby のアプリ (appstore・logviewer・monitor・NSF/SMF プレイヤ・midi_apu・kamon・picorabbit など、`grep -rln FmrbUI`)。FmrbUI・FmrbText・FmrbButtons・FmrbGfx は gem (`lib/add/picoruby-fmrb-app/mrblib/`) の Ruby をそのまま読む (R5 の check.rb と同じ)。
- doc/regression/plan.md (方針、決まり)。

## 2. 作るもの

1. **共通の道具** (例 `tools/regress/`): 描画を記録する代役 (FmrbGfx の描く口・文字の幅 (FONT_METRICS)・スプライト・画像の口を、呼ばれた順に記録する。今の check.rb の代役を土台に)、アプリを読み込んで場面を描かせる枠、Asterism・FmrbAsterism (Link・TableClient)・ファイル (/home・/etc の設定)・時刻・乱数の偽物。場面ごとの記録をテキストで書き出す (人が読める、差分が取れる形。座標・大きさ・色・文字)。
   - 麻雀の check.rb はこの道具の上に載せ替えてよい (`rake mahjong:overflow` の出力は前と一字一句同じ)。手間が大きければ麻雀は今のままにして、共通の道具とは別に残す (理由を report に)。
2. **場面の台本**: アプリごとに、決まった状態 (偽の卓の様子・偽の値・キー入力の列) を与えて描かせる場面を並べる。少なくとも:
   - オセロの卓: 一覧・席に着いた (長い名前で「…」、案内 2 行)・対局中・投了の確認・終局、Modern と Retro (全画面)。
   - ボード: 一覧・卓・消す確認。
   - walk_scroller: 始め・歩いている・最後の文字・調整の画面 (T)。
   - step_sensor: 状態の画面 (WiFi ok/-- × AST ok/../--)。
   - R6 の 5 アプリ: 主な画面とボタンが出る場面。
   - FmrbUI のアプリ: 部品 (ラベル・ボタン・トグル・数値・選択・スクロールバー・入力欄) が出る主な画面。**わざと長い文字** (ja/en の切り替え、長いファイル名・曲名) の場面も入れる (R6 で詰めるようになる所の基準になる)。
   - 読めない作り (Spinel 専用・FFI を直接呼ぶなど) は、道具の側で受けられなければ対象外にして一覧を report に。
3. **基準**: `test/regress/baseline/` (名前は決めてよい) に場面ごとの記録を置き、git に入れる。**今の develop (R6 の前) で取る**。
4. **入口 (RG5 の最初の形)**: `rake regress` = ホストの試験 (今の `rake test` の中身) + 描画の記録の比較。出力は短い要約 (通った数・落ちた数・落ちた場面の名前と差分の最初の数行、全文は書き出したファイルの場所)。`rake regress:update` で基準を更新 (更新した場面の一覧を出す)。`rake test` の CI の流れに描画の記録の比較を入れる (.github/workflows/build.yml の test-host)。
5. **使い方の文書**: doc/regression/ に短い使い方 (場面の足し方、基準の更新の決まり: 意図した変化だと人が確認したときだけ)。

## 3. 確認

- `rake regress` が今の develop で全部通る。同じものを 3 回流して同じ結果 (揺れが無い)。
- わざと 1 か所 (例: オセロの文字を 1 px ずらす、FmrbUI のボタンの色を変える) 直すと落ち、差分が出て、戻すと通る (試したら必ず戻す)。
- `rake mahjong:overflow` の結果が前と同じ。
- 流す時間と、出力の長さ (要約の行数) を report に。
- sim・ブラウザ版・実機は使わない。

## 4. 範囲・決まり

- **触ってよいもの**: fmruby-core (develop) の tools/、test/、rakelib/、.github/workflows/build.yml (test-host の手順を足す範囲)、doc/regression/ の使い方の文書。アプリと gem のコードは**変えない** (読めない作りでも、アプリに試験用の分岐を足さない。どうしても要るなら止まる)。
- **触らないもの**: アプリ・gem・ファームのコード、sdkconfig、実機、VPS、`~/asterism_keys/`、asterism-games・console。
- push・タグはしない (親がする)。`git commit -a` は使わない。件名 `tools:` `test:` `ci:` など、本文英語、末尾 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。実在の値 (端末名・IP) を基準やコードに入れない (`fmruby-a1b2c3` のような伏せ字)。doc/regression の指示書・report はコミットしない (親がする)。使い方の文書はコミットしてよい。
- **止まる条件**: アプリかファームのコードを変えないと読めない対象が多い (一覧にして返す)、結果が揺れて止められない、`rake mahjong:overflow` の結果が変わる。
- **report**: `doc/regression/report/rg1.md` (常体): 道具の作り、対象にした場面と外したもの (理由)、基準の大きさ、流す時間と出力の長さ、わざと壊した確認、CI、残件、コミット。**このあとの R6 で `rake regress` がどこまで確認を受け持てるか (受け持てない所) の見通し**も書く。
