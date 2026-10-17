# GS5 報告: 麻雀を AI が読める形にし、AI の打ち手 (Claude・Jev) を卓に座らせる

> 状態: 完了 (検収待ち、本物の API での確かめは保留) | 更新: 2026-10-17 | instruction_gs5.md の結果。asterism の meta に引数名と説明 (`args` / `doc`) を足し、アプリ単位の出入り (`on_app_join` / `on_app_leave`) を足した。asterism-games 0.10.0 はアプリ `game` で会員を見て、麻雀に AI 向けのメソッド (`describe` / `look` / `choose` / `points`) と AI の打ち手 (Claude・Jev、費用の上限、AI の席の持ち時間、切れたら CPU) を足した。卓の麻雀のアプリに AI を入れる操作と AI の席の印を足した。止まる条件には当たっていない (Jev は Cloudflare の文書で名前・API・料金を確かめられた)。鍵がこの環境に無いので、本物の API での東風戦は保留

## 1. コミット

| リポジトリ | ブランチ | 中身 |
|---|---|---|
| asterism | main `26afa9f` | meta の `args` / `doc` (`expose` の書き方、`DOC_MAX` ほかの上限)、`on_app_join` / `on_app_leave` (mruby と CRuby の Net)、CLI の `objects` / `tree` の表示、`asterism mcp` の入力名と説明、protocol.md 5 章、適合の見本 version 4、試験 (`test_meta_apps.rb`・`test_cli.rb`・host_test の on_app)、README・CHANGELOG (Unreleased) |
| asterism-games | main `36da32e` | games 0.10.0: `ai.rb` (つなぎの形、Claude・Jev・偽物、費用の数え方)、`ai_hub.rb` (ホストの側のスレッド)、`mahjong/readable.rb` (AI が読む形)、卓の AI の席、Remote の説明、アプリ単位の居る・居ない、`bin/asterism-games status` の AI の行、Gemfile に anthropic、試験 (`test_ai_players.rb`・`test_app_presence.rb` ほか)、README・docs/install.md・deploy/vps/env.example |
| fmruby-core | feature/gs5 (この報告と同じコミット) | 卓の麻雀のアプリ (`+Claude` / `+Jev`、`[AI]` の印)、この報告と画面 (`report/gs5/*.png`) |

push・タグ・rubygems への公開はしていない。asterism の VERSION は 0.7.1 のまま (CHANGELOG の Unreleased)。asterism-games には CHANGELOG のファイルが無い (これまでも README と VERSION で版を追っている) ので作っていない。コミットの前に差分から鍵・実在の IP・メールアドレスの検査をした (8 章)。

## 2. meta の args / doc

- 形 (protocol.md 5 章): `{"methods" => [{"name", "arity", "args" => [名前], "doc" => 文}], "doc" => 文}`。`args` と `doc` は任意。読む側は知らない欄を読み飛ばす決まりがすでにあるので、protocol 1 のまま (番号を変えない追加)。古いノードは送らず、古い読み手は無視する。
- 書き方: 今の数の表とまぜて書ける。

  ```ruby
  Asterism.expose("othello1", table, caller: :from, doc: "An othello table ...",
                  methods: { join: 1, "move" => { args: %w[token n square], doc: "Plays square ..." } })
  ```

  名前の数がそのまま引数の数 (確かめる数) になる。`arity: -1` なら数えず、名前は先頭の引数の名前。上限は doc 2048 バイト、名前 16 個・各 64 バイト (送る側で `ArgumentError`)。読む側は `args` が文字列の配列でない・数が arity と合わないものと、文字列でない `doc` を捨てる。
- 呼び手の keyword (`caller: :from`) と keyword 引数は `args` に入れない。
- 使う側: `asterism objects` は `move(token, n, square)` と doc の 1 行目を出し、`tree --json` は `args` / `doc` / オブジェクトの `doc` を持つ。`asterism mcp` はツールの入力を名前に (必須)、説明を doc に (後ろにオブジェクトの doc を括弧で) する。名前が無いメソッドは前どおり `arg1..`。
- 適合の見本 version 4: 試験される側のオブジェクト `other` に `args: []`・`doc` とオブジェクトの `doc` を付け、meta の答えの見本を変えた。CRuby・mruby とも 212 passed。
- asterism-games の Remote は `docs` / `object_doc` で説明を持ち、asterism が `DOC_MAX` を持つ (0.7.1 より後) ときだけ説明つきで出す。0.7.1 では前どおり数だけ (`Remote.expose_methods`)。麻雀 15 メソッド・オセロ 11 メソッドすべてに名前と説明を書いた (試験で「数と名前の数が合う、2048 バイト以内」を確かめる)。

## 3. アプリ単位の居る・居ない

- asterism: `Asterism.on_app_join { |node, app| }` / `on_app_leave`。liveliness の アプリの印 (`asterism/<node>/<app>`) の差を、`on_join` と同じ比べ方 (`tell_nodes` の中、印が変わったときだけ) で取る。自分のアプリは数えない。つないだときにいるアプリは join、切れたら全部 leave。オブジェクトの印はアプリではないので数えない。CRuby は `net.on_app_join` ほか (受け手のスレッド、例外は `on_error`)。
- asterism-games: ホストは会員を `asterism/<node>/game` で見る (`Host::PLAYER_APP`、設定 `player_app`)。新しい asterism では `net.on_app_join` / `on_app_leave` でアプリ名が `game` のものだけ卓に渡す。asterism 0.7.1 では session に `asterism/*/game` の liveliness の見張りを置く (同じ結果)。ノード単位の `on_join` / `on_leave` は使わなくなった (ノードが去ればそのアプリの印も消える)。
- `game` で動かない会員 (アプリ名が違う MCP の客や台本) は、前からの「呼び出しの間隔」(away_ms) で見る。MCP の客は `asterism mcp --app game` で起こせば卓のアプリと同じに扱われる (README に書いた)。

## 4. AI が読む形: メソッドを足した (look / choose / describe / points)

選んだのは「見え方の別の形を返すメソッド」。理由:

- 画面用の view は短い鍵と牌のバイト列 (`kw` は 1 牌 1 バイト、0x40 がリーチ) で、説明を MCP のツールの説明に載せても AI が毎回バイトを解かねばならず、取り違えやすい。言葉の形 (`"hand": ["1m", ...]`、`"moves": [{"n": 3, "type": "riichi", "tile": "5p", "text": "declare riichi, discarding 5p"}]`) なら説明が短くて済み、手は番号で選べる。
- 同じ形を、ホストの AI の打ち手に渡すもの (Claude の user の文、Jev の state) にも使える。AI に渡す情報の範囲が人間の画面と同じであることを 1 か所 (`Readable#look_view`) で守れる (公平さ)。
- 説明の長い文は `describe` の答え (3.4 KB) にし、meta (毎回の get で運ばれる) には短い説明だけを置いた。meta に長い文を載せると 1 通が大きくなるため。

| メソッド | 中身 |
|---|---|
| `describe()` | 遊び方 (join → sit → cpu / rule / start → look と choose の輪)、牌の名前、look の欄の説明 |
| `look(token)` | その席に見える卓: status、you、seats (名前、kind human/cpu/ai/empty、風、点、リーチ、河 (`river`・`riichi_index`・`called_indexes`)、副露、手牌の数)、round、dora_indicators、last_discard、hand、drawn、moves (番号つきの合法手)、help (アシスト 3 の中身: テンパイになる打牌と待ちの残り枚数、または向聴と受け入れ)、deadline_ms、result、final、rules、待ち合わせでは ai_players |
| `choose(token, sv, n)` | look の moves の n 番を打つ。`"stale"` (look し直し)、`"no such move"` |
| `points(token)` | いま和了れるならその点 (`now`)、テンパイの手なら待ちごとのロンとツモの点 (手番なら打牌ごと)。役が無ければ `no_yaku`。裏ドラは入れない (誰にも見えない)、振聴は見ない、リーチは宣言済みのときだけ |

- `points` は局の状態を写して (`deserialize_round_state(to_json)`)、裏ドラを空にし、手牌を差し替えて riichi_engine の和了の評価を呼ぶ。riichi_engine は変えていない。
- look の答えは河をまとめた形で 2.4-2.6 KB (最初の形は河の牌ごとに map にしていて 5.9 KB あった。縮めた)。

## 5. 判断のつなぎ (AI の打ち手)

```
request = {"game" => "mahjong", "table" => "mahjong1", "seat" => 2,
           "instructions" => describe の文, "view" => look の答え}
decider.decide(request, timeout: 残り秒) # => AI::Decision.new(n:, tokens: {"in","out","cache_write","cache_read"}, ms:)
                                         # 答えが無いときは AI::Error (鍵を含まない短い理由)
```

- 卓 (`Table`) は待たない: AI の席の判断が要ると `@ai_want` に積み、ホストの `AI::Hub` が錠 (`Asterism::LOCK`) を握った step の中で `ai_requests` を取り出して自分のスレッド (既定 4 本) の列に渡す。スレッドは錠を持たずに API を待ち、答えを列に戻す。次の step (錠の中) で `ai_answer` → 卓の tick で打つ。手が 1 つしかない判断は聞かない。
- AI の席は CPU の席に `"ai" => "claude"` を足したもの (`sk` は 2 のまま。古い画面には Claude という名の CPU に見える)。名前は `Claude` / `Jev` (同じ種類が複数なら `Claude1`、`Claude2`)。view に `"ai"` (席ごとの種類)、待ち合わせに `"ak"` (入れられる種類) を足した。入れ方は `cpu(token, seat, "claude")` (CPU と同じ呼び方の延長。卓の主だけ、空いた席だけ、`ak` に無ければ `"no ai"`)。
- 持ち時間: AI の席だけ `ai_turn_seconds` 30 s (打牌)、`ai_call_seconds` 15 s (鳴きの判断)。人間は今のまま。打牌の待ちは画面の `dl` に出る (AI の席の印、残り)。切れたら、または失敗 (例外・拒否・出していない手の番号) なら、その手だけ CPU が打ち、次の判断からまた AI に聞く。鳴きの判断を AI が考えている間は、人間の判断と同じく解決を待つ。
- Claude (`AI::Claude`、anthropic gem 1.80.0): `claude-sonnet-5-5` (`ASTERISM_GAMES_CLAUDE_MODEL`)、effort `low`、system に指示書 (describe の文) を置いて `cache_control: ephemeral`、user に look の JSON、答えは構造化出力 (`output_config.format` の JSON schema `{"n": integer}`)。`max_retries: 0`、時間は残りの持ち時間。stop_reason が refusal なら失敗 (CPU が打つ)。server-side の fallbacks は付けていない (拒否も CPU の代打ちで受けるため。付けるならユーザの判断、9 章)。
- Jev (`AI::Jev`、net/http): `POST https://api.cloudflare.com/client/v4/accounts/<id>/ai/run`、`{"model": "typesafe/jev", "input": {"state": look, "questions": {"move": {"type": "choice", "instructions": ..., "criteria": {"0": "discard 1m", ...}}}}}`。答えの `answers.move.choice` を手の番号にする (REST の `result` の包みがあれば外す)。
- 偽物 (`AI::Fake`): 遅れ・n 回に 1 回の遅すぎ・n 回に 1 回の失敗・トークン 100/1 を返す。`ASTERISM_GAMES_AI_FAKE=1` で家の LAN にも出せる (鍵なしの確かめ用。ログに「fakes」と出す)。
- 鍵: `ANTHROPIC_API_KEY`、`CLOUDFLARE_ACCOUNT_ID`・`CLOUDFLARE_API_TOKEN` を環境から (VPS は deploy/vps/.env、600)。無い種類は選択肢に出さない (`ak` に入らず、アプリのボタンも出ない)。API の失敗はログに HTTP の番号かクラス名だけを出す (ライブラリの例外の文は出さない)。オセロの AI は入れていない (9 章)。

## 6. 費用の数え方と既定の単価

- 1 判断ごとに API が返したトークン数 (input・output・cache の書き込み・読み出し) と単価から額を出し、`state_dir/ai_usage.json` (600) に日付 (ホストの地方時) ごと・種類ごとに足す。部屋のホストと常設の卓のホストは同じファイルを `flock` で共有する。保つのは 31 日分。中身は数と額だけ (鍵・卓・会員は無い)。
- 上限: その日の合計が `ASTERISM_GAMES_AI_DAILY_USD` (既定 3.0) に届いたら、どの卓も AI を出さず (`ak` が空)、座っている AI の席は CPU になる (`ai_off!`)。1 局 (1 ゲーム、半荘か東風戦) あたり `ASTERISM_GAMES_AI_CALLS_PER_GAME` (既定 400) 回聞いたら、その卓の AI の席は CPU になる。「1 局」は和了・流局の 1 局ではなく 1 ゲームと読んだ (1 局ごとだと半荘の途中で何度も切り替わり、費用の上限としての意味が薄い)。
- 出す先: `bin/asterism-games status` に「AI players <日付>: N decisions, $x of $y (claude n (in, out tokens, $), ...)」、ホストの @status に合計 `ai_today: {calls, usd, limit_usd}`、卓の数に `ais`。
- 既定の単価 (USD / 100 万トークン、2026-10-17 に公式の料金表で確かめた): Claude Sonnet 5.5 入力 2.0、出力 10.0、5 分のキャッシュ書き込み 2.5、キャッシュ読み出し 0.10。Jev (Cloudflare Workers AI、`typesafe/jev`) 入力 0.042、出力 0 (キャッシュ入力 0)。どれも .env で変えられる。
- 見積もり (偽物の局から): 東風戦 1 回で AI の席 1 つが 60-80 回聞かれる。look が 2.5 KB (およそ 800 トークン)、指示書 1.1 KB がキャッシュから読まれるとして、Claude で 1 回 0.002-0.003 USD (effort low の思考の出力しだい)、東風戦 1 回 0.15-0.25 USD ほど。本物の数字は保留 (7.5)。

## 7. 受け入れ条件ごとの結果

### 7.1 試験 (条件 1)

| | 結果 |
|---|---|
| asterism `rake` (msgs・objects・api・cli・protocol) | 93 runs、0 failures。test_meta_apps 2 runs / 25、test_cli 5 runs / 81 assertions、protocol (CRuby) 212 passed |
| asterism `rake test:mruby` | 40 / 15 / 26 checks、0 failed (26 が on_app_join / on_app_leave の 2 項目を含む) |
| asterism `rake test:protocol_mruby` | 212 passed、0 failed |
| asterism-games `MJ_GAMES=8 bundle exec rake test` | 180 runs、0 failures (ルータなしで skip 2)。ルータ越し (`ASTERISM_TEST_ROUTER`) で asterism 0.7.1 と手元の asterism (`RUBYLIB`) の両方とも 0 failures |
| asterism-console | 触っていない |

足した試験: asterism `test_meta_apps.rb` (2 つの子 (同じノード、別のアプリ) で meta の args/doc、アプリの出入り、片方を閉じてもノードは残る、method_doc の拒否)、`test_cli.rb` (objects の表示、tree の args/doc、MCP の入力名と説明)、host_test の on_app。games `test_ai_players.rb` (AI を入れる・断る・名前・`ai`/`ak`、偽物の局で遅れ・失敗のとき CPU、AI が見るもの (他人の手牌・act が無い)、AI の席の持ち時間、1 日の上限と 1 局の上限、日付ごとのファイル (600)、ハブのスレッド、鍵が出ないこと (Jev の 401、漏れる文の例外、設定)、Claude の要求の形 (モデル・cache_control・構造化出力・時間)、look/choose/points、テンパイの手の points)、`test_app_presence.rb` (ルータ越し、ノードに別のアプリを残して卓のアプリを閉じる)。

### 7.2 家の LAN、偽物のつなぎの AI を入れた半荘 (条件 2)

構成: この Step の zenohd `gs5-zenohd` (tcp 17567・ws 17569。sim の網に入れ、sim の zenohd へ `-e`)、ゲームホスト (手元の asterism を `RUBYLIB` で、`ASTERISM_GAMES_AI_FAKE=1`、遅れ 300 ms、40 回に 1 回遅すぎ、25 回に 1 回失敗、AI の持ち時間 5 s / 4 s、`MAHJONG_CPU_MS=150`)、ボット (人間の席を look / choose で打つ CRuby の客、アプリ `game`)。

- ボットが卓の主になり `cpu(token, 1, "claude")` と CPU 2 つで半荘を始め、**終局まで進んだ**: 78 s、5 局、箱下 (tobi) で終局 (`final [[0,1,84400,744],[3,2,18000,-20],[1,3,5700,-243],[2,4,-8100,-481]]`、席は CPU1・Bot・Claude・CPU2)。
- **持ち時間切れで CPU**: 2 回 (`AI seat 2 (claude) out of time: the CPU plays this move`、21:13:40 と 21:14:17)。**失敗で CPU**: 3 回 (`AI player claude: fake: failing on purpose (25/50/75); the CPU plays this move`)。
- このホストを止めたときのまとめ (半荘のあと sim とブラウザ版の卓も Claude・Jev の偽物入りで動かした分を含む): 聞いた 407、答え 392 (claude 262・jev 130)、失敗 15、判断の時間 平均 416 ms・最大 5,993 ms (遅すぎの回)、その日の数え 392 回・入力 39,200・出力 392 トークン・0.0555 USD (偽物のトークンを Claude の単価で数えた値)。
- 単体の試験の東風戦 (時間を進める形、7 回に 1 回遅すぎ・11 回に 1 回失敗): 2 回の実行で 61 回聞いて 48 回 AI が打ち遅れ 8・失敗 5、136 回聞いて 106 回・遅れ 19・失敗 11 (遅れと失敗は CPU)。局の長さは山しだい。

### 7.3 外の MCP の客で東風戦 (条件 3)

mcp gem 1.4.0 の客の台本から `asterism mcp --router tcp/127.0.0.1:17567 --allow 'games/mahjong1.*'`。台本はツールの名前・入力名・説明 (と describe の答え) だけを使う。

- ツールは見るだけの 3 つ + mahjong1 の 15。`look` の入力 `["token"]`、`choose` の入力 `["token", "sv", "n"]`、説明は doc とオブジェクトの doc。
- `describe` (3,437 B) → `join("MCP")` → `sit(-1)`・`rule(0, 1)` (東風)・`cpu(-1, true)`・`start` がすべて "ok" → look / choose の輪で **東風戦を最後まで打った**: 102 s、choose 159 回 (look 636 回)、`normal_end`、MCP の客は 4 位 (台本は「テンパイの打牌、和了、ツモ切り」だけの素朴な打ち手)。
- AI に打たせるのは鍵が要るので行っていない (Claude Code を MCP の客にする形は README に書いた)。

### 7.4 アプリ単位 (条件 4)

ルータ越しの試験 `test_app_presence.rb`: プレイヤーのノードがアプリ `other` を動かしたまま卓のアプリ `game` を閉じると、**6 ms** (asterism 0.7.1 の見張りの道) / **6 ms** (手元の asterism の `on_app_leave` の道) / 14 ms (全部の試験の中で) で卓の会員が途切れになった。ノードは網に残っている (`Asterism.nodes` に居る)。GS3 のノード単位では、別のアプリが残る限り途切れにならなかった。

### 7.5 本物の API での確かめ (条件 5)

**保留**。この作業の環境に `ANTHROPIC_API_KEY` も Cloudflare の鍵も無い (探していない・作っていない)。Claude の東風戦 1 回のトークン数・額・1 手の判断時間は、鍵が渡されてから測る。手順: deploy/vps/.env か作業の環境に鍵を置き、`ASTERISM_GAMES_AI_FAKE` を外してホストを起こし、卓の主が `+Claude` を入れて東風戦。数字は `bin/asterism-games status` (部屋のホスト) とホストの止めるときのログ (`AI players: ... N decisions, avg ms, max ms; today {...}`) に出る。Claude の要求の形は試験で確かめた (7.1)。

### 7.6 卓のアプリの画面と VM (条件 6)

| 画面 | ファイル |
|---|---|
| sim (640x360): 卓の主の待ち合わせ。空いた席があると「CPU で埋める」の隣に `+Claude` `+Jev` | `report/gs5/sim_lobby_ai_buttons.png` |
| sim: `+Claude` を押したあと。席 2 が「Claude [AI]」 | `sim_lobby_ai_seated.png` |
| sim: 対局中、西家の名前が「Claude」 | `sim_table_ai.png` |
| ブラウザ版 1280x720: 待ち合わせのボタン | `web1280_lobby_ai_buttons.png` |
| ブラウザ版 1280x720: Jev と Claude を入れたところ | `web1280_lobby_ai_seated.png` |
| ブラウザ版 1280x720: 対局中、中央の板に「南 Claude」「西 Jev」 | `web1280_table_ai.png` |
| ブラウザ版 640x360: 対局中、「西 Jev」「南 Claude」 | `web640_table_ai.png` |

- 最初はボタンを席の行に並べたが、640x360 では右のルールの欄に重なった。待ち合わせの下の行 (「CPU で埋める」の隣、最初の空いた席に入れる) に移した。
- VM (sim、large 3 MB): コンパイル直後 ソース 61,586 B / 使用 578,448 B / 山 1,506,208 B (GS3: 60,440 / 576,040 / 1,487,344 → +2.4 KB / +18.9 KB)。対局中の使用 856 KB (27 %、GS3 797-837 KB)。大きくは増えていない。

### 7.7 ゲームホストの RSS と 1 通の最大 (条件 7)

| | GS4 | GS5 |
|---|---|---|
| ゲームホストの RSS (VmHWM)。GS5 は 2 卓に AI の席 (偽物) と MCP の客、sim・ブラウザ版の卓のアプリ | 60.4 MB | **70.0 MB** (手元の asterism、anthropic gem は鍵が無いので読み込んでいない) |
| 1 通の最大 | 552 B (view reply) | **3,484 B (describe reply)**。look reply 2,587 B、麻雀の卓の meta 2,325 B (前は 200 B ほど)。画面の view は前と同じ |

## 8. 見立てと違った点・判断したこと

- AI に渡す形を最初は河の牌ごとの map にして 5.9 KB になった。河を牌の名前の配列とリーチ・鳴かれた位置の番号にし、help を `{牌: 残り}` にして 2.4-2.6 KB に縮めた (Claude のトークンと MCP の 0.2 秒ごとの look に効く)。
- AI の打牌の持ち時間は画面の `dl` にそのまま出る (AI の席の印と残り 30 s)。人間の席の持ち越し (bank) は AI の席には無い。
- 鳴きの判断を AI が考えている間、他家の判断が全部済んでいても解決を待つ (人間と同じ扱い)。偽物で 4 s にしても局の流れは止まらなかった。
- MCP の台本は `asterism mcp` を `--app game` なしで起こした (指示書どおり)。0.2 秒ごとの look が呼び出しの間隔の備えにかかるので途切れにはならない。考える時間の長い AI の客は `--app game` か卓の持ち時間を延ばす (README)。
- 鍵の検査: コミットの前に 3 つの差分を `sk-ant`・`ANTHROPIC_API_KEY=`・`CLOUDFLARE_API_TOKEN=`・IP の形・メールアドレスで検査した。出てくるのは試験の偽の鍵 (`sk-ant-test-...`、`cf-token-...`) と env.example の空の行、例の `192.0.2.2` だけ。
- ルータ越しの `test_app_presence.rb` は最初、この試験のプロセスで Asterism につないで見ていた。試験の順で `test_network.rb` (ホストを fork する) があとに来ると、zenoh-c を使ったあとの fork で子が止まり、全体がかかったままになった (`test_network.rb` の注意書きのとおり)。見る側も別のプロセスに出し、試験のプロセスは zenoh-c を使わない形に直した。
- 途中で作業のプロセスが一度止まった (前の会話の終わり)。差分と動いていたもの (ゲームホスト、gs5-zenohd、sim) を確かめて続けた。

## 9. 残ったこと・ユーザの判断

- **本物の API での確かめ (保留)**: 鍵が要る。7.5 の手順で東風戦 1 回のトークン・額・判断時間 (平均・最大) を測る。Jev も同じ (Cloudflare の account id と Workers AI の token)。
- Claude の server-side fallbacks (拒否されたとき別のモデルで答え直す): 付けていない。選択肢: (a) このまま (推奨。拒否はその手だけ CPU が打つ、追加の費用なし)、(b) `fallbacks: "default"` を付ける (beta の呼び出しになり、拒否の手も AI が打つ。費用は代わりのモデルの単価)。
- オセロの AI の打ち手: 入れていない (麻雀だけで判断のつなぎの形を固めた)。つなぎは `game` / `view` / `moves` の形なので、オセロの `look` 相当を足せば同じハブで動く。
- asterism の公開: `args`/`doc` と `on_app_*` は Unreleased。公開して asterism-games の Gemfile を上げれば、0.7.1 用の道 (説明なしの expose、liveliness の見張り) は消せる。
- 1 局あたりの上限を「1 ゲーム」と読んだ (6 章)。和了・流局の 1 局ごとにしたいなら直す。

## 10. 後片付け

zenohd `gs5-zenohd`、家の LAN のゲームホスト・ボット・MCP の客、web_* の開発サーバとブラウザ、sim (音を戻して sim_down) を止めて消した。scratchpad の台本と状態を消した。前からある image は残した。docker の prune はしていない。fmruby-core の build/ は x86-64 のまま (`file build/fmruby-core.elf`)、ブラウザ版は作り直した (`rake spinel:gen wasm:mruby wasm:mrb wasm:web`)。sim の標準構成でエディタを起動して 1 打鍵した。

## 10. 本物の API での確かめ (2026-10-18 追記、7.5 と gs5b.md 5.2 の保留を解いた)

構成: asterism-games 230a5ca (0.11.1、Jev は TypeSafe の直接の API) を別の作業場所に写し、平文の zenohd (1.10.1) と家の LAN の形で起こした。鍵は作業の環境の変数で渡した (ログ・状態のファイルには出ていない)。卓は mahjong1、東風戦、席は CPU・卓の主 (look / choose で素朴に打つ CRuby の客)・Claude (`claude-sonnet-5-5`)・Jev (`jev-latest`)。

- 東風戦は 210 秒で終局 (`normal_end`、和了 6 局)。順位: CPU 40,000 (1 位)、**Claude 35,000 (2 位)**、卓の主 14,300 (3 位)、**Jev 10,700 (4 位)**。1 回きりなので強さの比べにはならない。
- 判断: 聞いた 147 回、答え 147 回 (Claude 68・Jev 79)、失敗 0、持ち時間切れ 0 (CPU の代打ちなし)。判断の時間は 2 つ合わせて平均 725 ms・最大 4,618 ms (AI の席の持ち時間 30 秒に十分収まる)。
- 費用 (`ai_usage.json`、その日の数え):

| | 呼び出し | 入力 | 出力 | キャッシュ書き込み / 読み出し | 額 |
|---|---|---|---|---|---|
| Claude (Sonnet 5.5) | 68 | 109,646 | 1,767 | 1,578 / 105,726 | 0.2515 USD |
| Jev | 79 | 228,337 | 5,967 | - | 0.0096 USD |
| 合計 | 147 | 445,287 (読み出し込み) | 7,734 | | 0.2611 USD (1 日の上限 3.0 USD) |

- 見積もり (7 章: Claude は東風戦 1 回 0.15-0.25 USD、Jev は 0.004 USD) に対し、Claude は上の端、Jev は約 2.4 倍 (look が見積もりより長く、1 手の入力が約 2,900 トークン)。それでも Jev は Claude の約 26 分の 1。
- 1 通の最大は look の答えの 2,877 B。
