# GS8 報告: AI の 1 日の上限を種類ごとに

> 状態: 完了 (検収待ち) | 更新: 2026-10-18 | instruction_gs8.md の結果。asterism-games 0.15.0 で、AI プレイヤーの 1 日の上限を種類ごと (`ASTERISM_GAMES_AI_DAILY_USD_CLAUDE` / `_JEV`) に持てるようにした。届いた種類だけが止まり、その席は CPU が打ち、ほかの種類は続ける。種類ごとの指定が無ければ 0.14.0 と同じ動き。止まる条件には当たっていない

## 1. コミット

| リポジトリ | ブランチ | 中身 |
|---|---|---|
| asterism-games | main `1714bfe` | games 0.15.0: `ai.rb` (Settings#kind_daily_usd、Budget の種類ごとの判定)、`ai_hub.rb` (種類ごとに止める)、`mahjong/table.rb` (`ai_off!(why, kind:)`)、`host.rb` (起動の log、@status)、`rooms.rb`・`bin/asterism-games` (rooms の status)、試験、README、deploy/vps/env.example、docs/install.md、CHANGELOG、VERSION |
| fmruby-core | develop | この報告だけ (git 操作はしていない) |

push・タグはしていない。コミットの前に差分を鍵の形・IP の形・実在のドメインとメールで検査し、該当なし (`api.typesafe.ai` など既存の公開 API の名前だけ)。CHANGELOG の「Unreleased」(update.sh の credentials/ の件) は、これまでの流儀 (0.13.0 のとき) に合わせて 0.15.0 の節に入れた。

## 2. 設定の形

- `ASTERISM_GAMES_AI_DAILY_USD_<KIND>`。`<KIND>` は種類の名前の大文字 (今は `CLAUDE`・`JEV`、`AI::LABELS` の種類から作る)。値は USD。
- 値が数でない・負・有限でないときは無視し、その種類は合計の上限に従う。起動の log に一行: `games: AI players: ASTERISM_GAMES_AI_DAILY_USD_CLAUDE is not a number of dollars (0 or more); ignored, claude follows ASTERISM_GAMES_AI_DAILY_USD`。0 は有効 (その種類はその日最初から止まる)。
- 既存の `ASTERISM_GAMES_AI_DAILY_USD` (既定 3.0) は残る。種類ごとの指定が無い種類の上限。

## 3. 判定

- `Budget#over?(kind)`: その種類に自分の上限があれば、その種類のその日の額 >= 自分の上限。無ければ、その日の全種類の合計 >= `ASTERISM_GAMES_AI_DAILY_USD` (1 章のとおり、合計には自分の上限を持つ種類の額も入る)。引数なしの `over?` は今までどおり合計の判定。
- 読み出し: `usd_today(kind = nil)`、`limit_usd(kind = nil)`、`own_limit?(kind)`、`kind_usd`。記録の形 (`ai_usage.json` の days → kind) は変えていない。
- Hub は毎回、決める側のいる種類ごとに `over?(kind)` を見る。止まった種類は卓に出さない (`ai_kinds`) 、その種類の席だけ `ai_off!("today's limit for claude", kind: "claude")` で CPU に。届いていない種類には要求を出し続ける。全部止まったときだけ要求を出さない (今と同じ)。
- log は種類ごとに 1 回: `games: AI players: today's limit for claude ($10.0) is reached; no more claude players today, those seated are played by the CPU`。合計に従う種類が止まったときは `($3.0, every kind's together)` と添える。日付が変わって `over?` が偽に戻ると、その種類はまた出る (次に届けばまた 1 回言う)。
- **種類ごとの指定がまったく無いとき**は 0.14.0 と同じ: 全種類が同時に止まり、log は従来の一行 (`today's limit of $3.0 is reached; no more AI players today, ...`)、`ai_off!("today's limit")` を全席に 1 回。
- 起動の log: 指定なしは従来どおり `at most $3.0 a day`。指定ありは `games: AI players: claude, jev (at most $10.0 a day for claude, $2.0 for jev, 400 decisions a game)`。片方だけ指定したときは `$3.0 for jev with every kind's together`。

## 4. @status の欄

`ai_today` の `calls`・`usd`・`limit_usd` (合計の上限) は今のまま。種類ごとの上限が一つでもあるときだけ `kinds` を足す (指定なしでは 0.14.0 と同じ形):

```
"ai_today" => {"calls" => 72, "usd" => 0.0, "limit_usd" => 3.0,
               "kinds" => {"claude" => {"usd" => 0.0002, "limit_usd" => 0.0001, "own" => true, "over" => true},
                           "jev" => {"usd" => 0.0003, "limit_usd" => 3.0, "own" => false, "over" => false}}}
```

- `usd` は種類の額 (小数 4 桁。Jev はとても安く 2 桁では 0 になるため)、`limit_usd` はその種類が止まる上限 (自分のか合計の)、`own` は自分の上限か、`over` は今日止まったか。種類はホストが出せる種類 (鍵のある種類) だけ。
- rooms の status (`GET /status`・host.json の `ai`、`Budget#summary`) には `kind_limit_usd` ({種類 => 上限}、指定があるときだけ) を足した。`bin/asterism-games status` は種類の額のあとに `of $10.0` を出す。
- fmruby-core の画面 (flash/app/game) は `ai_today`・`limit_usd` を読んでいない (grep で確認。読んでいるのは卓の view の `"ai"` で、これは変えていない)。

## 5. 受け入れ条件の結果

1. **試験**: `MJ_GAMES=4 rake test` 225 runs、0 failures、0 errors (3 skips は既存の条件つき試験)。`rake test:gem` 42 runs、0 failures。rubocop は使っていない (Gemfile にも設定ファイルにも無い)。足した試験 (test/test_ai_players.rb): claude だけ止まり jev は残って要求も出続ける・log は 1 回・日付が変わると戻る / 指定の無い種類は合計に従う / 指定がまったく無いと 0.14.0 と同じ (log の文面と `ai_off` 1 回まで) / 環境変数の読み方と不正な値の無視 / ホストの @status の `kinds`。
2. **手元の卓** (外の API は呼ばない、鍵の環境変数は外して起動): zenohd (`eclipse/zenoh:1.10.1`、コンテナ `gs8-zenohd`、127.0.0.1:17447) に `bin/asterism-games` を `ASTERISM_GAMES_AI_FAKE=1`・`ASTERISM_GAMES_AI_DAILY_USD_CLAUDE=0.0001` で繋ぎ、試しの client が人 1・claude 1・jev 1・CPU 1 で東風戦を始めた。偽の AI は 1 回 100 token なので claude は 1 回 ($0.0002) で届く。
   ```
   games: AI players are fakes (ASTERISM_GAMES_AI_FAKE): no API is called
   games: AI players: claude, jev (at most $0.0001 a day for claude, $3.0 for jev with every kind's together, 400 decisions a game)
   mahjong: mahjong1: game started (Jev, CPU1, Human, Claude; ...)
   games: AI players: today's limit for claude ($0.0001) is reached; no more claude players today, those seated are played by the CPU
   mahjong: mahjong1: AI seats [3] are CPUs now (today's limit for claude)
   ...
   games: AI players: {"asked"=>81, "answered"=>81, "answered_jev"=>80, "answered_claude"=>1}; ...
   ```
   卓の view の `ai` は `["jev", "", "", "claude"]` → `["jev", "", "", ""]`。@status (`asterism/gs8-games/games/@status` を get) は上の 4 章の例のとおりで、`mahjong1` は `"ais"=>1` (jev の席)、`ai_today.kinds` は claude `over: true`、jev `over: false`。`ai_usage.json` は claude 1 回、jev 40 回 (その後も増え続けた)。
3. **指定を外したとき**: 新しい state で `ASTERISM_GAMES_AI_DAILY_USD=0.0005` だけを渡した。起動の log は `(at most $0.0005 a day, 400 decisions a game)`、届くと従来の一行 `today's limit of $0.0005 is reached; no more AI players today, ...` と `AI seats [1, 2] are CPUs now (today's limit)` (両方同時)。`ai_today` は `{"calls"=>6, "usd"=>0.0, "limit_usd"=>0.0005}` で `kinds` 無し。0.14.0 と同じ。

試しのホスト (2 回とも TERM で止めた) とコンテナ `gs8-zenohd` は消した。image は前からある `eclipse/zenoh:1.10.1` を使っただけで残している。作業用のファイルは scratchpad の中だけ。

## 6. VPS の更新に要ること

1. `deploy/vps/.env` に 2 行を足す (エディタで):
   ```
   ASTERISM_GAMES_AI_DAILY_USD_CLAUDE=10
   ASTERISM_GAMES_AI_DAILY_USD_JEV=2
   ```
   既存の `ASTERISM_GAMES_AI_DAILY_USD` は残してよい (両方に自分の上限があれば判定には使われない。summary の `limit_usd` に出るだけ)。
2. `./update.sh 0.15.0` (または `--when-idle 0.15.0`)。その前に v0.15.0 のタグと ghcr の image が要る (タグ・公開はユーザ)。
3. 起動の log が `at most $10.0 a day for claude, $2.0 for jev` になっていることを確認する。`... is not a number of dollars` が出ていたら、その行の値を直す。
4. 0.14.0 に戻すと 2 行は読まれず、合計の上限だけになる。`ai_usage.json` の形は同じなのでそのまま読める。
