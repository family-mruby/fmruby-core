# MJ9 報告: 局の始まりの表示、結果は全員の OK、役を 1 つずつ

> 状態: 完了 (検収待ち) | 更新: 2026-10-18 | instruction_mj9.md の結果。配牌のあとゲームホストが 2 秒待って「東1局 0本場」を大きく出す (時計・CPU・AI は止まる)。結果は、つながっている人間全員の OK (今の「次へ」) で次の局へ、押さない人がいてもカウントの終わりから 10 秒で自動で進む。結果の箱は役を 1 秒に 1 つ、最後に点数。古い画面の「次へ」は OK として数え、新しい画面は 0.14.0 のホストでも今どおり動く。asterism-games の試験とはみ出し検査は失敗 0。人間のいない卓の扱いの解釈に 1 つ確認が残る (8 章)

## 1. コミット

| リポジトリ | ブランチ | コミット | 中身 |
|---|---|---|---|
| asterism-games | main | `6e49ffc` | 0.15.0 (未タグ) に足す: 卓 (`table.rb`)、プログラム向けの説明 (`readable.rb`)、README (新しい節「Result」、設定、view の鍵、試験の一覧)、CHANGELOG、docs/install.md、`config/example.yml`、`deploy/vps/env.example`、試験 (新規 `test_mahjong_result_ok.rb`、helper と `test_mahjong_games.rb` の手直し) |
| fmruby-core | develop | (この報告と同じコミット) | 卓のアプリ (`mahjong.app.rb`・`mahjong_panels.rb`・`mahjong_xl.rb`)、はみ出し検査 (`tools/mahjong_overflow/check.rb`・`fixtures.rb`)、この報告と画面 (`report/mj9/*.png`) |

push・タグはしていない。

## 2. view の鍵 (asterism-games README "Views")

| 鍵 | いつ | 中身 |
|---|---|---|
| `in` | 配牌のあと、局の名前を出しているあいだ (`st` が `play`) | 残りの ms (整数) |
| `ok` | 人間のいる卓の結果 (`st` が `result`) | `[OK した席のビット, まだ待っている席のビット (座っていて、途切れていなくて、OK していない), 自動で進むまでの ms]` |

- 画面の「OK を待っています (n/m)」は n = 1 つ目のビットの数、m = 1 つ目と 2 つ目を合わせたビットの数。
- OK した席の `a` からは「次へ」(12) が消える。もう一度「次へ」を送っても `"ok"` で何も変わらない (古い画面の二度押し)。
- 観戦の遅れのある卓の観戦者の view (過去の写し) には `ok` を入れない (`dl` と同じ扱い)。`in` は写しに残る (遅れた観戦者にも局の名前が 2 秒出る)。
- 新しい設定: `deal_pause_seconds` / `MAHJONG_DEAL_PAUSE_SECONDS` (既定 2、0 で無し)、`result_auto_seconds` / `MAHJONG_RESULT_AUTO_SECONDS` (既定 10)。`result_seconds` / `MAHJONG_RESULT_SECONDS` (既定 8) は人間のいない卓の固定の時間だけになった (人間のいる卓の 8 秒の固定はやめた。README と CHANGELOG に書いた)。
- 保存: 局の名前の残り (`intro_left`)、OK した席 (`oks`)、OK を待つ結果か (`ok_humans`) を保存する。0.14.0 が保存した結果は保存された時間で進む。
- ログ: `OK from seat 1 (waiting for [0])`、`result moves on (every OK)` / `result moves on (by itself)`。

## 3. カウントの決まり (両側の共通の決まり)

- 1 ステップ = 1 秒。
- 和了 1 人ぶん = 役の行の数 (`wn[i].y` の要素の数) + 点数の 1 ステップ。包 (`pa`) は点数と一緒に出る。
- ダブロン・トリロンは和了者ごとに続けて数える (合計)。
- 和了の無い結果 (流局・途中流局・三家和・流し満貫) は 1 ステップ。
- ゲームホスト: 結果の時刻 + カウントの長さ + `result_auto_seconds` を自動で進む時刻にする (`Table.count_ms(res)`)。つながっている人間全員の OK でそれより前に進む。全員が途切れているときは時間で進む。
- 画面: 結果の箱が出た時刻 (和了の声の 1 秒のあと) から、k 番目のステップを (k-1) 秒の時点で出す。和了者のブロックは、その人の最初のステップで名前・手牌・最初の役が出て、役が 1 秒ずつ、最後に点数。全員の点数の表 (前→後) はカウントが終わってから出す。カウントの途中の押下 (キーかクリック) は残りを一度に出し、その次の押下 (Enter か OK ボタン) が OK。飛ばしてもゲームホストの時刻は変わらない。
- 画面の残り秒は `ok` の 3 つ目から数える。画面の箱は声の 1 秒ぶん遅れて始まるので、画面のカウントが終わってから自動で進むまでは約 9 秒になる (ホストの決まりはカウントの終わりから 10 秒)。

## 4. 画面

| 場面 | 640x360 | 1280x720 |
|---|---|---|
| 局の始まり (`in`) | 卓の中央 (y 168 を中心) に 36 px の「南2局 2本場」の箱 | 盤の中央 (y 316) に 60 px の箱 |
| カウントの途中 | 役が出たところまで。点数と全員の点数の表はまだ。下の行に「OK を待っています (0/2) あと 11 秒」、ボタン無し | 同じ。箱の下に行 |
| カウントの後 | 点数・全員の点数の表、右下に「OK[Enter]」 | 同じ (ボタンは箱の右下) |
| 自分が OK した後 | ボタンが「待っています」(押せない色)、「(1/2)」 | 同じ |
| 観戦者 | 行だけ (ボタン無し) | 同じ |

- 426x240 の簡易表示は崩れなければよい扱い (はみ出し検査で例外なし、警告のみ)。
- 640x360 の結果の箱は、1 人の和了で OK の行のぶん 22 px 高くした (296 → 318)。複数の和了は前から最大 (352) で、行は箱の下に入る。
- 1280x720 は、結果の箱をステップ・残り秒・OK が変わるたびに描き直す (1 秒に 1 回、全部)。局の始まりの箱は、その間に描いたものがあれば上に描き直し、終わったら全体を描き直す。
- 牌譜の振り返り (mahjong_replay.rb) の結果は今のまま (view に `ok` が無いので全部一度に出る)。
- 本体ファイル (`mahjong.app.rb`、64 KB の上限に近い) には呼び出しを 6 行だけ足し、処理は `mahjong_panels.rb` に置いた (`round_take`・`round_tick`・`rv_skip`・`ok_footer`・`draw_intro` ほか)。

## 5. 受け入れ条件

### 5.1 試験

- asterism-games `MJ_GAMES=4 bundle exec rake test`: 243 runs, 0 failures, 0 errors, 3 skips (前からの skip)。`rake test:gem` も 0 failures。
- 新しい試験 (`test_mahjong_result_ok.rb`、18 件): 2 秒のあいだ時計が出ない・状態が進まない・打牌が `"not offered"`、時計は待ちの後から満額、CPU が打たない、AI に尋ねない、毎局の待ち、人間のいない卓は待たない、保存した待ちの続き、全員の OK で進む (4 人中 3 人では進まない)、途切れた人は数えない、誰も押さないと (カウント + 10 秒) で進む、役の数でカウントが変わる、ダブロンは 2 人ぶん、流局は 1 秒、全員が途切れたら時間で進む、古い「次へ」が OK、人間のいない卓は `result_ms` のまま `ok` 無し、観戦の写しに `ok` 無し、OK と時刻が再起動を越える、終局は今のまま、設定と環境変数。
- 既存の試験の手直し: helper の人間の卓は `deal_pause_ms` 0・`result_auto_ms` 0 (試験が時刻を止めたまま打つため)。4 人の人間の半荘で、結果の画面に出していない RON を送ったときの答えは、OK を待つ結果が長く続くようになったので `"not playing"` も受ける。
- はみ出し検査 `rake mahjong:overflow` (`ruby tools/mahjong_overflow/check.rb`): 207 drawings, 0 failed, 45 warnings (426x240 のみ。前は 165 drawings・31 warnings)。足した場面: `intro`・`intro_spectator` (北4局 12本場)、`result_ok_yakuman` (最初のステップ・3 ステップ目・点数・全部)、`result_ok_triple` (最初・2 人目の役の途中・全部)、`result_ok_given` (OK した後)、`result_ok_draw`、`result_ok_spectator` (観戦者)。局の始まりの箱は結果の箱と同じく卓の上に重なるもの (overlay) として検査する。

### 5.2 手元の一式で確認

構成: `rake mahjong:up` (zenohd・ゲームホスト (asterism-games の作業コピー)・ページの server、`PORT=8008`)、ゲームホストは `ASTERISM_GAMES_STATE_DIR` を scratchpad に、`MAHJONG_TURN_SECONDS=1`・`MAHJONG_BANK_SECONDS=0`・`MAHJONG_CALL_SECONDS=1`・`MAHJONG_CPU_MS=300` (自動の打牌で局を早く進めるため)。ページ A = web_* の道具のページ (1280x720、tester)、ページ B = docker の Playwright (Chromium) で開いたページ (640x360、bob。開発サーバ `rake wasm:serve PORT=8007` の drive の中継を `ruby tools/fmrb_web.rb --port 8007` で動かした)。人間 2 + CPU 2。外の AI の API は呼んでいない。

| 確認 | 結果 | 画面・ログ |
|---|---|---|
| 局の始まりに 2 秒表示が出て時計がその後から動く | 合格。両方のページに「南2局 2本場」。表示の間、手番の時計は出ない (ガイドの行だけ) | `web1280_intro.png`・`web640_intro.png` |
| 役が 1 秒ずつ出て最後に点数 | 合格。結果から 2.6 秒 (声 1 秒 + 1.6 秒) で役 2 つ、点数と点数の表はまだ。3.6 秒で点数と表、OK ボタン | `web1280_count_a.png`・`web640_count_b.png` (途中)、`web1280_count_a2.png` (点数のあと) |
| カウントの途中の押下で残りを一度に | 合格。役 1 つの時点で Enter → 3 つの役・点数・表・OK ボタンが出た。OK は送られていない (ログに出ない)。次の Enter で OK | `web1280_count_before_press.png` → `web1280_after_press.png` |
| 片方だけ OK しても進まない | 合格。B が OK → B は「待っています」「(1/2)」、A は「(1/2)」と OK ボタン。ログ `20:43:27 OK from seat 0 (waiting for [1])` のあと進まない | `web640_ok_given_b.png`・`web1280_waiting_a.png` |
| 片方が押さないとカウント + 10 秒で進む | 合格。役 2 つの和了 (3 ステップ) で、結果 `20:43:23` → `20:43:36 result moves on (by itself)` (13 秒) | ログ |
| 両方 OK で進む | 合格。`20:42:45 OK from seat 1 (waiting for [0])` → `20:42:52 OK from seat 0 (waiting for [])` → `result moves on (every OK)`。OK した A の画面は「待っています」(流局の結果) | `web1280_ok_given.png` |
| 終局 | 南4局の結果で両方 OK → `game over (normal_end)`、記録 1 件。終局の扱いは今のまま | ログ |

### 5.3 古い組み合わせ

- **新しいゲームホスト + 古い画面**: 今の develop の麻雀アプリ (この作業の前、`ced02130` の flash) で作ったブラウザ版の bundle (作業の前の `wasm/build`) を 2 つのページで開き、新しいホストにつないだ。結果で A の「次へ」→ `OK from seat 1 (waiting for [0])` で進まず、B の「次へ」→ `OK from seat 0 (waiting for [])`・`result moves on (every OK)` で次の局へ。押さないときは `result moves on (by itself)` で進んだ。古い画面は役を一度に出し、局の名前は出さない (知らない鍵は読まない)。合格。`old_web640_result_before_ok.png`・`old_web1280_result_after_ok.png` (古い 1280x720 は結果の箱を一度しか描かないので、押した後も「次へ」のボタンが残って見える。押すと OK になり、もう一度押しても何も変わらない)。
- **新しい画面 + 古いゲームホスト (0.14.0)**: `git archive v0.14.0` を scratchpad に出してその `bin/asterism-games` で起動し、新しい bundle のページをつないだ。結果は今までどおり全部一度に出て「次へ[Enter]」、押すとすぐ次の局 (`東3局`)。局の名前は出ない。落ちない。合格。`new_on_014_result.png`・`new_on_014_next.png`。

## 6. 実装の気づき

- 人間のいない卓は「今どおり」を、待ちも OK も無い (配牌の後もすぐ打つ、結果は `result_seconds`) と読んだ。人間の席が 1 つも無い卓 (`humans` 0、または全員が抜けて CPU に代わった卓) が当たる。
- 全員が途切れたときは、OK を待つ相手がいないが、すぐには進めず自動の時刻まで待つ (押さない人がいるのと同じ扱い)。
- `act` は結果の間、`sv` を変えないので、OK を押したページの `sv` は古くならない。OK のたびに view の版は上がる (ほかの人の n/m が変わる)。
- 640x360 の結果の箱の下には前から「次へ」のボタンが隠れていて (操作の行に描かれ、箱が上に重なる)、そこをクリックしても OK になる。前からの形のまま (カウントの途中なら残りを出すだけ)。

## 7. 片付け

- 止めて消した: ゲームホスト (新しいものと 0.14.0)、ページの server (8008)、開発サーバ (8006 は web_down、8007 は TERM)、web_* のブラウザ (web_down)、Playwright の container `mj9-pageb` (rm)、zenohd (`mahjong:down` で止め、この作業で作った container `fmruby_zenohd` と網 `family-mruby_default` を rm)。docker の prune はしていない。sim は使っていない。Tab5・シリアルには触っていない。
- `wasm/build` はこの作業の flash で作り直したまま (dist も)。
- 最初の 1 回だけ、ゲームホストを asterism-games の `var/state` (gitignore の手元の状態) で起動してしまい、mahjong1 に tester が入って出たことがそこに保存された。すぐに止め、以後は scratchpad の状態で動かした。

## 8. 残ったこと・確認したいこと

1. **人間のいない卓の局の始まり** (1 章の 1 には例外が書かれていない。6 章の読みで作った)。
   1. 人間のいない卓は待たない (推奨、今の形): CPU だけの卓は結果の扱いと揃えて今どおり。観戦者には局の名前が出ない。
   2. 人間のいない卓も 2 秒待つ: 観戦者にも局の名前が出る。CPU だけの卓 (`humans` 0) の試験・常設の観戦用の卓が 1 局 2 秒ずつ長くなる。
2. 古い 1280x720 の画面は押した後も「次へ」が見えたまま (画面側の作りで、ホストからは直せない)。新しい flash に上げれば「待っています」になる。
3. 実機 (Tab5) とブラウザ版の公開版はまだ古い画面。古い画面のままでも遊べることは 5.3 で確認した。
