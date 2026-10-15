# MJ3 報告: 部屋で遊ぶ麻雀 (Step 3)

> 状態: MJ3a・MJ3b 完了 (検収待ち) | 更新: 2026-10-15 | instruction_mj3.md の記録。MJ3a: ゲームホストが console の部屋の中に卓を出す作り (部屋のリンクが卓のアプリを開く部屋だけに、部屋ごとに 1 つのプロセス)、VPS 用の一式 (image・compose・update.sh・手順書)、console の見張りとバックアップを作った。手元の構成 (console の deploy/local) で、管理者が作った部屋のリンクをブラウザ版 4 つで開き、4 人で半荘 (東 1 局から西 4 局) を終局まで打った。途中でゲームホストの container と zenohd を起動し直し、どちらも数秒で同じ場面から続いた。作業中に console の door の誤り (127 バイトちょうどの WebSocket の 1 通で接続を切る) を見つけて直した。MJ3b (10-17 章): 卓の主が待ち合わせで決めるローカルルール 11 項目 (riichi_engine の鍵 10 個とゲームホストの持ち時間)、観戦の人数の上限 (12) と遅らせ (時間で数える、既定 0 秒)、局の結果の画面 (翻つきの役、副露、点数の前後、テンパイ・ノーテン) を作った。東風を選んだ対局が東場で終わること、喰いタンなしで鳴いたタンヤオにロンが出ないことを試験と画面で確かめた。1 通の最大は試験の最悪の形で 2,092 B、実際に打った中では 530 B。止まる条件には当たっていない

## 1. 作ったもの (MJ3a)

| リポジトリ | ブランチ | コミット | 中身 |
|---|---|---|---|
| asterism-games | main | `7cbec8b` | 部屋の卓 (`lib/asterism_games/rooms.rb`)、状態の口 (`GET /status`、`bin/asterism-games status`)、`deploy/vps/` (Dockerfile・compose・env.example・update.sh)、`config/rooms.yml`、`docs/install.md`、試験、README、VERSION 0.3.0 |
| asterism-console | main | `9bc8c19`、`fe06013` | door の直し、0.2.2 (部屋の開くアプリの選択、rooms.json の `app`、ゲームホストの見張り、バックアップの `ASTERISM_BACKUP_EXTRA`、`deploy/vps/docker-compose.games.yml`、文書) |
| fmruby-core | feature/mj3 (develop 2aacce0c から) | この報告と同じコミット | この報告、plan.md・game_rooms_comparison.md |

- fmruby-core のコード (卓のアプリ・ブラウザ版) は変えていない。部屋のリンクで開いたブラウザ版の卓のアプリは、そのまま部屋の中の卓を見つけて入れた (名前空間でキーが付け外しされるため)。
- riichi_engine の Fork・asterism ほかのリポジトリ・VPS・端末・sdkconfig・`.env` には触れていない。push・タグはしていない。

## 2. 決めた案: どの枠に入り、いつ卓を出すか

**部屋のリンクが卓のアプリを開く部屋にだけ、卓を出す** (指示書の 2 つ目の候補を、console の今の入力のまま使う形)。

- 管理者は部屋を作るとき、もともと「開くアプリ (Opens)」を決めている。そこで「Mahjong」を選ぶことが、そのまま「麻雀の卓を出す」の印になる。**部屋を作る人の操作は増えない** (選ぶ欄がオセロと並んだだけ)。
- console は rooms.json (door が読む、開いている部屋の一覧) に部屋ごとの `app` を足して書く。ゲームホストは console の `rooms` volume を読むだけの形で付け、`app` が卓のアプリ (`/app/game/mahjong/mahjong.app.rb`) の部屋を見つけると、その部屋の枠の証明書 (`certs/room-NN.tar`) と名前空間 `room/NN` で入るプロセスを 1 つ起こす。部屋が閉じる・終わると止める。
- 1 つ目の候補 (全部の枠に入っておき、使われている枠にだけ卓を出す) を選ばなかった理由:
  1. asterism の gem は 1 プロセス 1 接続なので、8 枠に入っておくと 8 プロセス (1 つ約 45-50 MB、合わせて約 400 MB) になり、VPS の空き (約 390 MB) に収まらない。部屋が開いたときだけ起こす形なら、使う分だけで済む。
  2. 「使われている」の判断 (誰かが入ってきたら卓を出す) は、オセロの部屋にも空の卓が現れることになり、図と状態の画面が紛らわしい。
- ゲームホストは console に問い合わせない (rooms.json はファイルなので、console が止まっていても卓は続く)。rooms.json が読めないときは何も変えない (全部の部屋が閉じたとは扱わない)。
- 卓を出す部屋の数は `GAMES_ROOM_MAX` (既定 3)。超えた部屋には卓を出さず、状態の口と console の画面に「卓の無い部屋」として出す。
- ゲームホストは部屋の中では `games` という名前のノードで、卓は `games/games/mahjong1` (家の LAN と同じ名前)。部屋ごとに卓は 1 つ (`rooms.tables` で変えられる)。

## 3. 作り (asterism-games)

### 3.1 監督のプロセスと部屋ごとのプロセス

- `bin/asterism-games` は `rooms` の設定 (または `ASTERISM_GAMES_ROOMS_DIR`) があると監督 (`Rooms`) として動く。1 秒ごとに rooms.json の mtime を見て、変わっていれば読み直す。
- 監督は部屋ごとに fork して `Host` (今までのゲームホスト) を動かす。子は `Asterism.connect(credentials: room-NN.tar, config: {"namespace" => "room/NN"})` で zenohd の `tls/7448` に直接入る (door を通らない)。ACL は枠の範囲 (`room/NN/**`) だけを許すので、子が別の部屋や登録済みの側に何かを出すことはできない。
- 子が止まったら 1, 2, 4 … 30 秒あけて起こし直す。監督が SIGTERM を受けると子に SIGTERM を送り (子は状態を書いて抜ける)、8 秒待つ。
- 家の LAN の形 (`rooms` なし) は今までどおり (`tables` の卓を 1 つのプロセスで持つ)。変えたのは、`Host` が名前空間と状態のファイル (`host.json`) を扱えるようにしたことだけ。

### 3.2 枠の使い回し

- 部屋の卓は `state_dir/rooms/<枠>/` に、部屋の ID (`room.json`) と一緒に書く。
- 次のどれかで、その部屋のプロセスを止め、ディレクトリごと消す: rooms.json の `revoked` にその部屋がある (閉じた)、終わりの時刻を過ぎた、枠が別の部屋の ID を持っている。監督が止まっている間に枠が別の部屋に移った場合も、起動したときに同じ判断で消す。
- これで新しい部屋の卓は空の待ち合わせから始まり、前の部屋の席・合言葉・状態のファイルは残らない。

### 3.3 状態の口

- `GET /status` (image では `0.0.0.0:7460`、どこにも公開しない): 部屋ごとの状態 (起動中・動いている・起こし直し待ち)、卓ごとの `st` と数 (座っている人・CPU・卓にいる人・途切れ)、rooms.json を読めているか、卓の無い部屋、`busy` (誰かが座っている)、RSS の合計。名前・ノード ID・合言葉は入れない (試験で確かめた)。
- 子は 2 秒ごとに `host.json` に自分の状態 (つながっているか、卓の数) を書き、監督がそれをまとめる。
- `bin/asterism-games status [--idle]` は同じものを表示し、`--idle` は誰も座っていなければ 0 で終わる (update.sh の `--when-idle` が使う)。

### 3.4 VPS 用の一式 (deploy/vps)

- **Dockerfile**: ruby:3.2.6-slim の 2 段。riichi_engine は Gemfile.lock が固定した Fork の版 (`0912a7a`、`0.1.1.fmrb2`) を bundler が取る。asterism-zenoh はビルド中に zenoh-c を取ってコンパイルする (console の image と同じ)。uid 1000 で動く (枠の証明書は console の rails (1000) の 0600 のファイルなので)。image は 227 MB。
- **compose** (project `asterism-games`): console の project とは別にした。console の update.sh は `up -d --remove-orphans` で自分の project の知らない container を消すので、同じ project に入れると console の更新でゲームホストが消える。console の volume `asterism_rooms` (読むだけ) と network `asterism_router` を名前で使い、自分の volume `state` を持つ。`restart: unless-stopped`、`stop_grace_period: 15s`、`mem_limit` (既定 384 MB)、ログは json-file の 10 MB x 5、health check は状態の口。
- **update.sh** (console の流れに合わせた): 確かめ (作業コピーに変更なし、ref、console の volume と network) → ref の checkout と image のビルド (古いゲームホストは動いたまま) → (`--when-idle`) 誰も座っていなくなるまで待つ (`--wait-max` 分、既定 360。時間切れなら何も起こし直さず checkout を戻す) → 状態のファイルの写し (volume の中の `before-update/`) → `.env` の `GAMES_VERSION`・`GAMES_REF` → `docker compose up -d` → 状態の口が答えるかの確かめ → 戻し方の表示。image の版は、タグならその版、そうでなければ `<ref>-<commit>`。
- **console のバックアップ**: console 側に `ASTERISM_BACKUP_EXTRA` (`games=/extra/games`) を足し、毎日のバックアップの同じ .tar.gz の `extra/games/` にゲームホストの volume の中身を入れる (manifest に一覧)。console の restore はそこに触れない (戻すのは手で。docs/install.md 8 章)。設定と volume の取り付けは console の `deploy/vps/docker-compose.games.yml` 1 つにまとめた。

## 4. 作り (asterism-console 0.2.2)

- **部屋の画面**: 「Opens」を選ぶ欄にした。選択肢は `ASTERISM_ROOM_APPS` (`Othello=/app/test/othello.app.rb,Mahjong=/app/game/mahjong/mahjong.app.rb`、env.example に入れた) と、役割のブラウザ版のアプリ。それ以外は「Or another app」に書ける。ゲームホストがあると欄の説明に「Mahjong を開く部屋にはゲームホストが卓を出す」と出る。
- **rooms.json**: 開いている部屋ごとに `app` を足した (door は使わない)。
- **見張り** (`ASTERISM_GAMES_STATUS_URL`): 部屋の画面の上に「The game host (asterism-games 0.3.0, up since …): 2 rooms with tables (at most 3), 142 MB」か「The game host does not answer」。部屋の一覧と部屋の画面に「Tables」の列 (`mahjong1: playing, 4 seated, 0 CPU, 4 at the table`、まだなら `waiting for the game host`)。
- **図**: 何も変えずに、部屋の枠の中にゲームホストのノード `games` と卓のオブジェクト `mahjong1` が、部屋のページと並んで出た (0.2.1 の部屋の図のまま)。部屋の外 (登録済みの側) にはゲームホストは出ない。
- **door の直し** (`9bc8c19`): ブラウザからの WebSocket の 1 通の長さを、7 ビットの長さが 126 なら 16 ビットを読み、**続けて**「それが 127 なら 64 ビットを読む」としていた。16 ビットの長さがちょうど 127 の 1 通 (127 バイト) で 8 バイト余計に読み、ずれて接続を切っていた (door のログに `a 3555143691952231497-byte frame`)。卓の呼び出し (`act`・`view`) はこの長さになることがあり、部屋で打ち始めて 1 分ほどで全員の画面が「ホストが答えない」になった。オセロの 1 通は 127 バイトにならないので G2 の確認では出なかった。7 ビットの長さだけで分ける形に直し、125-65533 バイト (126・127 を含む) を送る試験を足した。試験の側の WebSocket の客も同じ読み方だったので直した。
- 版は 0.2.2 (VERSION・CHANGELOG・compose の既定の版・install.md の 0.2.1 から 0.2.2 の節)。タグは打っていない。

## 5. 受け入れ条件の結果

確認の構成: console の deploy/local (nginx 1.27・door・signer・zenohd 1.10.1・console・bridge・backup、作業中の console から作った image) に、asterism-games の deploy/vps を project `asterism-games-rehearsal` (`ASTERISM_PROJECT=asterism-rehearsal`) で並べた。console 側は `GAMES=1` で `docker-compose.games.yml` を足した。枠は 3 つ (`ASTERISM_ROOM_SLOTS=3`)。ブラウザは Playwright 1.56 の Chromium (container) で、ページごとに別の context。ブラウザ版は develop の `wasm:dist` (ユーザの 8006 の置き場の写し、作業用の場所に置いた)。卓の時間は試し用に短くした (`MAHJONG_TURN_SECONDS=15`・`CALL_SECONDS=6`・`CPU_MS=300`・`RESULT_SECONDS=5`)。

| # | 条件 | 結果 |
|---|---|---|
| 1 | 管理者が部屋を作り、リンクを 4 つのブラウザ版で開いて半荘を最後まで。別の部屋からは見えない・呼べない | 合格 (5.1、5.2) |
| 2 | 部屋を閉じて同じ枠を別の部屋に使うと、空の待ち合わせから | 合格 (5.3) |
| 3 | ゲームホストの container を起動し直すと同じ場面から。zenohd の起動し直しで数秒で戻る | 合格。container は 1.0 秒で戻り 2.1 秒後に次の打牌。zenohd は答えの無い時間 1.0 秒 (5.4) |
| 4 | image を作って compose で起動、console の図に出る。2 卓・4 人の RSS | 合格。RSS 合計 143.6 MB (5.5) |
| 5 | 試験・rubocop・`rake test`・wasm のビルド・家の LAN の卓 | 合格 (5.6) |

### 5.1 部屋の中の半荘 (ブラウザ版 4 つ)

- 管理者 (admin の住所から TOTP つき) が Admin > Rooms で「Opens: Mahjong」の部屋を作り、出たリンク (1 回だけ表示) を 4 つのページで開いた。1 秒以内にゲームホストのログに `room 01: room <ID> opens /app/game/mahjong/mahjong.app.rb: host pid …` と `room 01: games: games/games up (mahjong1) via credentials in room/01`。
- 4 つのページは卓の一覧に `games/games/mahjong1` を見て入り (`join` 31 ms)、1-4 で座り、卓の主が始めた。操作は MJ2 と同じ作業用の台本 (ゲームホストの状態のファイルから手番と鳴ける席を読み、ページにキーを押す。状態のファイルはゲームホストの volume を読むだけで付けた)。
- 半荘は東 1 局から西 4 局まで 12 局 (全部流局。自摸切りの打ち方なので和了は出ない。MJ2 と同じ)、鳴き (ポン・チー) あり、2 人以上が同時に鳴ける場面 4 回、西入して西 4 局で終局、卓の主の Enter で待ち合わせに戻った。所要 1,456 秒。
- 操作から画面まで (卓のアプリの「応答」): 1,063 回、中央値 60 ms、95% 367 ms、最大 553 ms (door と nginx を通る。MJ2 の家の LAN の 60 / 366 / 582 ms とほぼ同じ)。1 通の最大は、部屋で打った卓のゲームホストのログ (部屋を閉じたときに出す値) で 349-356 B。
- 同じ時間に 2 つ目の部屋 (枠 02、Mahjong) で `bin/mahjong-bots` の 4 人 (枠 02 の証明書と名前空間で入る) が半荘を続けて打った。

### 5.2 別の部屋から

枠 02 の証明書を持つ CRuby (`isolation.rb`、作業用) で確かめた:

- 名前空間 `room/02` で入ると、卓の一覧は `games/games/mahjong1` の 1 つで、呼ぶと枠 02 の卓 (名前は Bot1-4) が答えた。枠 01 の卓 (名前は wasm-…) は見えない。同じ名前の卓でも部屋が違えば別のもの。
- 名前空間なしで枠 02 の証明書のまま `room/01/**` に get と生存の印の問い合わせ: 0 件・0 件 (ACL が断る)。比べのため `room/02/**` の生存の印は 6 件。
- ブラウザのページは部屋の鍵の枠の名前空間に固定されるので、別の部屋の卓を名指しする方法が無い (G2 の作りのまま)。

### 5.3 枠の使い回し

- 部屋 A (枠 01、対局中) と部屋 B (枠 02) が開いている間にオセロの部屋 X (枠 03) を作り、A を閉じた。ログ: `room 01: stopping host pid 15 (the room is closed or ended)`、`room 01: removed the saved tables of room aaac…` (1 秒以内)。
- 次の部屋 C は枠 01 に入った (空きはそこだけ)。卓の状態のファイルは `"ver":1,"sv":0,"status":"wait","seats":[null,null,null,null],"members":{}` で、空の待ち合わせから始まった。部屋 X (オセロ) には卓を出さなかった。
- 試験 (`test_rooms.rb`) でも、閉じた部屋・終わった部屋・枠が別の部屋に移った場合 (監督が止まっている間に移った場合も) の消し方、同じ部屋なら起動し直しても卓が残ること、rooms.json が読めないときに何も変えないことを確かめた。

### 5.4 起動し直し

| 起こしたこと | 前 | 後 |
|---|---|---|
| ゲームホストの container (`docker restart`、東 3 局の途中) | `play sv 328 ton 3 残り 64` | 再起動の命令は 0.56 秒で返り、命令から 1.03 秒で部屋 01 のプロセスが `resumed (play, …, 4 members)` と `up`。状態のファイルは `ton 3 残り 64` のまま。2.1 秒後に次の打牌 (応答 61 ms)。4 人の画面は自分でつなぎ直して同じ局を続けた |
| zenohd (`docker restart`、南 2 局の途中) | `play sv 821 nan 2 残り 63` | ブラウザ 4 つは zenohd が上がってから 1 秒以内に door を通ってつなぎ直した (door のログ)。半荘はそのまま終局まで続いた |
| zenohd (終局のあと、枠 01 の卓を 200 ms ごとに呼ぶ CRuby を置いて) | 答えている | 答えの無い時間は 1,010 ms (zenohd の再起動の命令から数えて約 3 秒)。ゲームホストのプロセスは切れたことに気づかないまま (zenoh-c のつなぎ直し) 答え続けた |

- 南 2 局の zenohd の起動し直しでは、ちょうど手番だったページの打牌が、つなぎ直しの間に送られて答えを失い、その手番は時間切れ (15 秒) の自摸切りになった (作業用の台本は 1 つの手番に 1 回しか押さないため)。人が打つなら押し直せば済む。

### 5.5 image・compose・図・メモリ

- `docker build -f deploy/vps/Dockerfile` で image (227 MB)。compose で起動すると `games: rooms from /rooms/rooms.json: tables for rooms opening /app/game/mahjong/mahjong.app.rb (at most 3 rooms)` と `status on 0.0.0.0:7460`。health check は healthy。
- update.sh も試した (作業用の clone で、console の通し稽古の project に向けて): 誰かが座っている間に `--when-idle --wait-max 1` → 1 分待って「still busy … nothing restarted」で終わり、checkout を戻した。部屋を閉じてから `--when-idle main` → image `asterism-games:main-7cbec8b` を作り、待たずに状態の写しを取って起こし直し、状態の口の確かめまで通った。
- console の図: 部屋の枠「Mahjong E · room 01」の中にゲームホストの `games` と卓 `mahjong1`、4 つのページ。「Mahjong B · room 02」の中に `games` とボット 4 人。オセロの部屋は空の枠 (`s4_console_graph_rooms_with_game_host.png`)。部屋の画面に Tables の列 (`s4_console_rooms_page_tables.png`)。
- console のバックアップ: backup の container で `bin/asterism backup` → .tar.gz に `extra/games/rooms/01/{host,mahjong1,room}.json` が入り、manifest の `extra` に 4 ファイル 550 B。

**メモリ** (2 部屋に卓、それぞれ 4 人が打っている: 部屋 01 はブラウザ版 4 つ、部屋 02 はボット 4 人):

| プロセス | RSS | PSS (共有分を割った値) |
|---|---|---|
| 監督 (`bin/asterism-games`) | 43.2 MB | 19.7 MB |
| 部屋 01 のプロセス | 49.8 MB | 23.2 MB |
| 部屋 02 のプロセス | 50.6 MB | 24.1 MB |
| 合計 | 143.6 MB | 67.0 MB |

- container の使用量 (cgroup、`docker stats`) は 63 MiB / 384 MiB。fork した子は監督が読み込んだコード (riichi_engine・asterism) を共有するので、実際に増えるのは 1 部屋 25 MB 前後。上限の 3 部屋でも RSS で約 195 MB、PSS で約 90 MB の見込み。
- 部屋が開いていないときは監督だけ (RSS 42-43 MB)。
- 同じ時間の door は 20 MB、zenohd は 6 MB (通し稽古の値)。G2 の見積もり (VPS の空き約 390 MB) に収まる。

### 5.6 試験・ビルド・家の LAN

| 確認 | 結果 |
|---|---|
| asterism-games `bundle exec rake test` | 60 件、失敗 0 (ネットワークの試験 1 件は飛ばす)。CPU 4 人の半荘 20 回と人間 4 人の半荘、部屋の試験 11 件 |
| asterism-games `test_network.rb` (ユーザの zenohd、別のノード ID・作業用の状態) | 通った (家の LAN の形で `join`・`sit`・`cpu`・`start` と断ること) |
| asterism-console `bin/rails test` | 391 件、失敗 0 |
| asterism-console `bin/rubocop` | 0 件 (直した 2 か所の字下げを自動で直した後) |
| asterism-games の rubocop | 設定が無いリポジトリなので、新しいファイルに Lint と Security だけを当てた (試験の 1 か所の括弧の勧めのみ。既存の試験と同じ書き方) |
| fmruby-core `rake test` | 通った (golden 103、MicroPython の smoke) |
| fmruby-core wasm のビルド (`rake wasm:web wasm:scan`) | 通った。`wasm:dist` (ユーザの 8006 が出している置き場) は作り直していない |
| 家の LAN の卓 | ユーザの zenohd (ws 7449)・ゲームホスト (pid 418264)・ページのサーバ (8006) は止めず、`tools/mahjong_local.rb status` で 3 つとも動いていることを確かめた。新しいコードの家の LAN の形は `test_network.rb` で確かめた |

- sim は使っていない (fmruby-core のコードを変えていないため)。fmruby-core の `build/` は Linux の標準構成のまま (`file build/fmruby-core.elf` は x86-64)。

## 6. ユーザがすること (VPS への反映)

ssh で VPS に入って行う。名前は例 (`/srv/asterism` は console の checkout の置き場、`router.example.org` は自分のもの)。手順の正は asterism-games の `docs/install.md` と console の `docs/install.md` 12 章・20 章。

1. **push とタグ**: asterism-console の main (`9bc8c19`・`fe06013`) を push し、`v0.2.2` のタグで image を作る (CI)。asterism-games の main (`7cbec8b`) を push する (VPS が clone できるように。GitHub のリポジトリが非公開なら、VPS に読み取りの鍵を置くか、手元から rsync する)。
2. **console を 0.2.2 に**:
   ```
   cd /srv/asterism/asterism-console/deploy/vps
   git fetch --tags
   ./update.sh 0.2.2        # systemd の unit で動かしているなら --sudo --unit <unit> を足す
   ```
   `.env` に `ASTERISM_ROOM_APPS=Othello=/app/test/othello.app.rb,Mahjong=/app/game/mahjong/mahjong.app.rb` を足す (door の直しはこの更新で入る)。
3. **ゲームホストを置く**:
   ```
   cd /srv/asterism
   git clone <asterism-games のリポジトリ> asterism-games
   cd asterism-games/deploy/vps
   cp env.example .env && chmod 600 .env     # ASTERISM_PROJECT=asterism のまま
   ./update.sh main                          # image を作り、起動し、状態の口を確かめる (数分)
   docker compose exec games bundle exec bin/asterism-games status
   ```
4. **console にゲームホストを知らせる** (console の `deploy/vps`):
   - `.env` に `COMPOSE_FILE=docker-compose.yml:docker-compose.games.yml` を足す (systemd の unit で `-f` を書いているなら、そこに `-f docker-compose.games.yml` を足す)。
   - `docker compose up -d` (console と backup が新しい設定で作り直される)。
5. **確かめ**: Admin > Rooms に「The game host (asterism-games 0.3.0 …)」。「Opens: Mahjong」で部屋を作り、リンクを開くと卓の一覧に `games/games/mahjong1`。図の部屋の枠に `games` と `mahjong1`。部屋を閉じるとゲームホストのログに `removed the saved tables`。
6. **外から入れるようにするか**: G2 のとおり、`/room` の住所の制限と 7443 のパケットフィルタはそのまま (知り合いが外から入るには G2 の 5 章の 4 のとおり開ける)。ゲームホストは VPS の中だけで動くので、ポートを開ける必要は無い。
7. **更新**: `./update.sh --when-idle main` (誰も座っていないときに起こし直す)。

## 7. 画面の写し

`C:\Users\Public\asterism_deploy\screenshots\mj3\`:

| ファイル | 中身 |
|---|---|
| `s1_admin_rooms_page_opens_mahjong.png` | 部屋の画面 (ゲームホストの行、Opens の選択、Tables の列) |
| `s1_room_table_list.png` | 部屋のリンクで開いたページの卓の一覧 |
| `s1_room_lobby_four_seated.png` | 4 人が座った待ち合わせ |
| `s1_room_game_started_P1.png` | 始めた直後 |
| `s1_room_calls_P1.png` | 2 人が鳴ける場面 |
| `s1_room_game_over_west4_P1.png` | 西 4 局のあとの終局 |
| `s1_room_back_to_lobby_P1.png` | 卓の主の Enter で待ち合わせに戻った |
| `s4_console_graph_rooms_with_game_host.png` | console の図 (部屋 2 つにゲームホストと卓、オセロの部屋は空) |
| `s4_console_rooms_page_tables.png` | 部屋の画面の Tables の列 (2 部屋とも対局中) |

## 8. 見立てと違った点・残ったこと

- **door の誤り** (4 章): G2 の確認はオセロだけだったので、127 バイトちょうどの 1 通が出なかった。VPS で部屋の麻雀を打つには console 0.2.2 が要る。
- 通し稽古で console の container を作り直すと、nginx が古い住所を持ったままで 502 になった (nginx を起動し直して戻した)。手元の通し稽古だけの話 (VPS の nginx は 127.0.0.1 の公開ポートを向く)。
- console の stack を `docker compose down` すると、ゲームホストがつながっている network を消せない (警告が出る)。手順書に「先にゲームホストを止める」と書いた。
- 部屋の中の卓は `games/games/mahjong1` 1 つ。部屋の中で 2 卓にするのは設定 (`rooms.tables`) でできるが、確かめていない。
- 半荘は自摸切りの打ち方なので全部流局。和了・点数の画面は MJ1・MJ-L で見ている。
- 端末 (Tab5・P4-Nano) を部屋に入れる確認はしていない (端末の部屋のリンクは無い。後の Step)。
- 片付けで `docker image prune -f` を 1 回実行した (名前の無い image を全部消す命令で、作業で作った image 以外の名前の無いものも消えた可能性がある)。

## 9. 片付け

通し稽古の container・volume・network (`asterism-rehearsal_*`、`asterism-games-rehearsal_*`)、試しの image (`asterism-games:mj3test`・`asterism-games:main-7cbec8b`・`asterism-console-rehearsal:0.2.1`)、`deploy/local/.env`・`certs/`、asterism-games の `deploy/vps/.env`、Playwright の container を消した。sim・開発サーバは起動していない。ユーザの zenohd・ゲームホスト・8006 のページのサーバは止めていない。卓 mahjong1 (ユーザのもの) は使っていない。fmruby-core の作業コピーは develop に戻した。

## 10. 作ったもの (MJ3b)

| リポジトリ | ブランチ | コミット | 中身 |
|---|---|---|---|
| asterism-games | main | `607fd9c` | ローカルルール (`rule` の呼び出し、見え方の `ru`、保存)、観戦の人数の上限と遅らせ、和了の役ごとの翻 (`yh`)、`bin/mahjong-bots` の `--rules`・`--start-ms`・`--result-ms`、試験 (`test/test_mahjong_local_rules.rb`)、README の "Local rules"・"Spectators" |
| fmruby-core | feature/mj3 | この報告と同じコミット | 卓のアプリ: 待ち合わせのルールの欄、観戦の画面 (手番の印、640x360 の点数の帯)、局の結果の画面 (1280x720 と 640x360)。新しいファイル `flash/app/game/mahjong/mahjong_panels.rb` |

- asterism-console は変えていない (MJ3b は卓とゲームホストの中で済んだ)。riichi_engine の Fork・asterism ほかのリポジトリ・VPS・端末・sdkconfig・`.env` には触れていない。push・タグはしていない。
- asterism-games の版 (VERSION 0.3.0) は上げていない。VPS に入れるときに上げるかはユーザが決める (呼び出しを足しただけで、古い卓のアプリはそのまま動く。12.4)。

## 11. ローカルルール

### 11.1 項目と riichi_engine の鍵

卓の主が待ち合わせで決め、全員の待ち合わせの画面に出る。始まったあとは変えられない (`rule` は `"not waiting"` を返す)。

| # | 画面の名前 | 値 | riichi_engine の鍵 | 既定 (ゲームホスト) |
|---|---|---|---|---|
| 0 | 対局 | 半荘 / 東風 | `game_type` (`hanchan` / `tonpuu`) | 半荘 |
| 1 | 喰いタン | あり / なし | `kuitan` | あり |
| 2 | 赤ドラ | なし / 3枚 / 4枚 | `aka_dora` (0/0/0、1/1/1、1/2/1) | 3枚 |
| 3 | ダブロン | あり / なし (頭ハネ) | `double_ron` | あり |
| 4 | 三家和 | 全員和了 / 流局 | `triple_ron_draw` | 全員和了 |
| 5 | 役満の複合 | あり / なし | `multiple_yakuman` | あり |
| 6 | 数え役満 | あり / なし | `kazoe_yakuman` | あり |
| 7 | 切り上げ満貫 | あり / なし | `kiriage_mangan` | なし |
| 8 | トビ | あり / なし | `tobi` | あり |
| 9 | 西入 | あり / なし | `nishi_iri` | あり |
| 10 | 持ち時間 | 10 / 20 / 30 / 60 秒 | (ゲームホストの `turn_seconds`) | 20 秒 |

- 既定は雀魂の既定 (ゲームホストの `Engine::RULE_DEFAULTS` と卓の設定の `rule`)。riichi_engine の Fork の既定は上流のまま。各項目の選択肢に、ゲームホストの設定の値も必ず入る (たとえば `MAHJONG_TURN_SECONDS=15` なら 15 秒も選べる)。ゲームホストの値に戻すと、その項目の選択は消える。
- 赤ドラの 4 枚は 5p を 2 枚にする形 (よくあるローカルルールの形)。
- 東風を選ぶと、西入の行は灰色になる (riichi_engine の東風戦は東 4 局で終わり、南入が無い。雀魂の東風戦の南入はエンジンに無い)。
- 持ち時間は riichi_engine の鍵ではないが、指示書の項目に挙がっているのでゲームホストのタイマーとして入れた。鳴きの時間 (`call_seconds`) は設定のまま。

画面に出さなかったもの:

| 鍵 | 出さない理由 |
|---|---|
| `double_yakuman` (四暗刻単騎などを 2 倍に) | riichi_engine の鍵はあるが、エンジンのどこも読んでいない (README "Rules"、MJ2 と同じ)。指示書の「ダブル役満」はこれなので出せない。役満の重なり (`multiple_yakuman`) は項目 5 として出した |
| `atozuke` | 鍵はあるが使われていない |
| `initial_score`・`oka_origin`・`uma` | 効く。ただ打ち方ではなく終局の点数だけを変えるので、画面の 11 行に入れなかった。卓の設定の `rule` では今までどおり変えられる |
| `renpuu_jantou_fu` (連風牌の雀頭の符) | 効く。雀魂の値を確かめられなかった |
| 途中流局 (四風連打ほか)・包・テンパイ止め・槓ドラの時機 | エンジンに無い (README "Rules") |

効かない鍵を出して誤る、ということは無かった (出した 10 個の鍵はどれもエンジンが読んでいることを試験で確かめた)。遊ぶ流れを止める誤りは見つからなかった。

### 11.2 作り (asterism-games)

- 新しい呼び出し `rule(token, item, value)`: 卓の主・待ち合わせの間・選択肢の値のときだけ受ける (`"not owner"`・`"not waiting"`・`"no rule"`・`"value"`・`"no change"`)。変わるたびに見え方の版が上がり、全員の画面が取り直す。
- 見え方の `ru`: 11 個の数の配列 (上の表の順)。待ち合わせでも対局中でも、公の見え方と全員の見え方に入る (約 15 B)。
- 卓の主の選択は riichi_engine の鍵 (`game_type`・`aka_dora` ほか) と持ち時間 (ms) で持ち、ゲームホストの規則の上に重ねて `RuleConfig` を作り直す。待ち合わせの間だけ変わるので、対局中の規則は始めたときのまま。
- 保存: 状態のファイルに `rules` (`choice`・`turn_ms`) を足した (版の番号は 2 のまま。無いファイルは今までどおり読める)。起動し直しても、終局で待ち合わせに戻っても、選んだ規則は残る。部屋の枠が使い回されると状態のファイルごと消える (MJ3a) ので、新しい部屋はゲームホストの規則から始まる。
- 対局を始めたときのログに規則が出る (`game started (...; rules [1, 0, 3, ...])`)。

### 11.3 画面 (卓のアプリ)

- 待ち合わせの右側に「ルール」の欄 (640x360 は x 452 から、1280x720 はその 2 倍)。1 項目 1 行で、名前と値。卓の主には各行がボタンになり (クリックで次の値)、上下キーで行を選び、左右キーで値を変える。ほかの人には「ルール (卓の主が決める)」と値だけ。席の行の強調は欄にかからない幅に縮めた。
- 小さい画面 (sim の 426x240) は欄を出さず、題の下に 1 行の要約 (`半荘 喰タンあり 赤3 20秒`)。変えるのは大きい画面から。
- ルールの欄の無いゲームホスト (`ru` が無い、たとえば今のユーザのゲームホスト) では何も出さない。sim で、ユーザのゲームホストの卓 (mahjong2) の待ち合わせが今までどおり出ることを見た (`s10_sim_lobby_old_host.png`)。
- アプリの大きさ: 64 KB の上限があるので、局の結果の描き方 (640x360) とルールの欄、観戦の帯を新しいファイル `mahjong_panels.rb` (10.6 KB) に分け、`mahjong_xl.rb` と同じく `require` で読む。`mahjong.app.rb` は 52.4 KB (前は 54.7 KB)、`mahjong_xl.rb` は 25.2 KB。

## 12. 観戦の仕上げ

### 12.1 手牌を見せない

- 観戦者 (席の無い人) の見え方は公の見え方 (`me` -1、`a` は空) で、手牌 (`h`・`dt`)・助け (`wt`・`uk`) を足さない。公の put と `table` も同じものを返す。和了のときの和了者の手牌だけは公開 (前から)。
- 試験 (`test_spectators_never_see_a_hand`): 東風の 1 局面ずつ (人 1 人が `act` で打ち、CPU 3 人)、観戦者の見え方 (段階 3 の助けを求めても) と put に `h`・`dt`・`wt`・`uk` が無く、`a` が空であることを対局の終わりまで毎回確かめた。既存の 4 人の半荘の試験 (各席の見え方に他人の手牌が無い) もそのまま通る。

### 12.2 遅らせ

- ゲームホストの設定 `spectator_delay_seconds` (環境 `MAHJONG_SPECTATOR_DELAY_SECONDS`)。0 より大きいと、観戦者の見え方・put・`table` は、その秒数だけ前の対局の場面を出す。
- 作り: 状態の版 (`sv`) か状態 (`st`) が変わるたびに、公の見え方のうち対局の部分 (`st sv r sc ri dora kw fu hc ls ev res fin`) を時刻つきで残し、遅らせの秒数より古いものは 1 つを残して捨てる。観戦者には、その残した 1 つと、今の人の部分 (`nm hu sk aw ow mb ru`、`v`) を合わせて返す。残しの先頭が進むたびに版を上げるので、観戦の画面は遅れた場面が動いたときに取り直す。遅れた見え方には締め切り (`dl`) を入れない。座っている人の見え方は遅らせない。
- **数で遅らせず、時間で遅らせた**。手の数で数えると、局の結果は手が進まない間 (結果の 8 秒) 流れず、次の局の数手の間だけ一瞬出て消える。また CPU の 1 手 (0.7 秒) と人の 1 手 (最大 20 秒) で遅れの長さが大きく変わる。時間で数えれば、結果は観戦者にも同じ長さで出る。
- **既定は 0 秒 (遅らせない)**。観戦者には手牌も助けも無く、座っている人が見ていないものは何も見えない。遅らせて隠せるものが無いので、遅らせると観戦者が卓の人と話す (同じ部屋の声など) ときに場面がずれるだけになる。観客に見せる場 (配信など) や、いつか観戦者に手牌を見せるときに設定する。
- 起動し直すと残しは空から始まる (保存しない)。そのときだけ観戦者は今の場面に飛ぶ。
- 試験 (`test_spectators_see_the_game_as_it_was_the_delay_ago`、遅らせ 5 秒、30 秒の間 10 ms ごと): 観戦者の `sv` が「5 秒前の時点の `sv`」以下で、「5.02 秒前の `sv`」以上 (遅れすぎない)。put と同じ。座っている人の `sv` は今のまま、版は今のもの。既定では遅れない (`test_no_delay_by_default`)。
- 実際の卓 (手元の試しのゲームホスト、遅らせ 6 秒) で、`table` を 1 秒ごとに呼んで状態のファイル (今の `sv`) と比べた: 観戦者の場面は今の場面に「6.1 秒前の場面」として付いていき、手の数では 31-34 手遅れていた (CPU の手が速い場面)。局の結果 (結果の待ちを 12 秒にした) は、観戦者にも 12 秒出たあとで次の局に移った。

### 12.3 人数の上限

- `spectators` (環境 `MAHJONG_SPECTATORS`、既定 12): 席の無い人の数の上限。部屋は 1 つ 16 接続 (door の `WS_DOOR_ROOM_PER_ROOM`) で、席の 4 人を引いて 12。ゲームホストは zenohd に直接つなぐので 16 に入らない。待ち合わせの間は、まだ座れる席の数だけ多く入れる (入ってから座るため)。超えた人の `join` は `{"error" => "spectators"}` で、画面は「観戦の人数が上限」と出す。入っていた人は戻れる。卓の 16 人 (`MAX_MEMBERS`) はそのまま。
- 試験 (`test_how_many_spectators`): 上限 2 で、待ち合わせでは 2 + 空き席 3 = 5 人まで入り 6 人目は断る、対局中は 2 人まで、入っていた人は戻れる。

### 12.4 観戦の画面

- 誰の手番か: 手番の席の名前の下に黄の線 (640x360 の卓・1280x720 の中央の板、座っている人の画面も同じ)。観戦者の状態の行に「手番: 南 Bot1」。
- 640x360 の観戦者: 手牌の場所が空くので、ボタンの行の下に 4 席の帯 (風・名前・点数・リーチ)。手番の席は枠と色。下の席の副露 (右下) にかからないよう 1 行 (高さ 20) にした。
- 1280x720 の観戦者は、前からの中央の板 (4 席の風・名前・点数) に手番の線が足された形。
- 局の結果は次の 13 章 (観戦者と座っている人で同じ)。
- 古い卓のアプリ (端末に入っている版) は `ru`・`yh` を読まずに今までどおり動く (足しただけで、形は変えていない)。

## 13. 局の結果の画面

1280x720 と 640x360 で同じ中身にした:

- 1 行目: 局 (`東3局 5本場`、見え方の `rd`)。
- 和了: 和了者 (ロンは放銃者も)、手牌と和了牌 (枠つき)、**副露** (和了牌の右、鳴いた牌の並びのまま。暗槓は両端を裏)。役は **翻つき** (`平和 1翻`、`ドラ 2翻`、役満は `大三元 役満`)。ゲームホストが和了ごとに役の翻の配列 `yh` を足した (riichi_engine の `evaluate_win` の役ごとの `han`、役満は -1)。最後に符・翻・点 (`30符 3翻 7300点`)。ダブロンは和了者ごとに区画を並べる (前から)。
- 流局: 「流局」と、4 席それぞれに「テンパイ」「ノーテン」。
- 点数の移動: 4 席の行に「風 名前 (テンパイ・ノーテン) 前 → 後 変化」。変化は増えたら水色、減ったら橙、0 は灰色の `±0`。前の点は今の点 (結果のときの `sc`) から変化を引いたもの。
- 終局: 順位・点・ポイントと「次へ: 卓の主が待ち合わせに戻す」(前から)。
- 640x360 の箱は 520x296 (ダブロンは 348 まで縦に広げる)、1280x720 は 800 幅で中身に合わせた高さ。1280x720 の副露は、牌が箱に入らないときだけ小さい牌 (24x32) にする。
- riichi_engine のドラの名前は数を含む (`ドラ 2`) ので、画面では数を外して翻で出す。
- 3 人の和了 (三家和を流局にしない既定) は 640x360 の箱に入りきらないことがある (確かめていない。残ったこと)。

## 14. MJ3b の受け入れ条件の結果

確認の構成: ユーザの zenohd (tcp 7447・ws 7449) に、作業中の asterism-games の試しのゲームホスト (ノード `gamesmj3`、卓 `mahjong7` と `mahjong8`、状態は作業用の場所、`mahjong8` だけ遅らせ 6 秒、CPU の間 300 ms、結果 40 秒) を足した。ブラウザ版は feature/mj3 の `rake wasm:web` を開発サーバ (ポート 8016、MCP の `web_*`) で開いた (ユーザの 8006 と `wasm:dist` は作り直していない)。打ち手は `bin/mahjong-bots` 1 人 (卓の主、`--rules` で規則を決める) と CPU 3 人、ブラウザ版はそこに観戦者として入った。

| # | 条件 | 結果 |
|---|---|---|
| 1 | ルールを変えて始めた半荘がその設定で進む (東風で終わる・喰いタンなしで鳴いたタンヤオが和了にならない、など) を試験と画面で | 合格 (14.1) |
| 2 | 観戦の遅らせ・手牌の非表示の試験。観戦の画面の写し | 合格 (12.1・12.2、写しは s6) |
| 3 | 画面の写しを report と共有の場所に | 合格 (15 章) |
| 4 | 1 通の最大が 3800 B 以下 | 合格。試験の最悪の形で 2,092 B、打った中で 530 B (14.2) |
| 5 | MJ3a の 5 と同じ試験 | 合格 (14.3) |

### 14.1 ルールがその設定で進むこと

試験 (`test/test_mahjong_local_rules.rb`、13 件):

| 試験 | 確かめたこと |
|---|---|
| `test_a_tonpuu_game_chosen_in_the_lobby_ends_in_the_east` | 待ち合わせで東風を選んで始め、人 1 人 (エンジンの CPU と同じ打ち方を `act` で) と CPU 3 人で終局まで: 全部の局が東場 (7 局、別の実行で 4 局)、順位が 4 人分出る |
| `test_no_kuitan_no_ron_on_an_open_tanyao` | 2p をポンした手 (2m3m4m 5p6p7p 4s4s 6s7s、タンヤオだけ) に 8s が出たとき、喰いタンありならロンが出て、待ち合わせで喰いタンなしにした卓では出ない |
| `test_red_fives_by_the_lobby` | 赤ドラ 0・3・4 で山の赤五の数が 0・3・4 |
| `test_every_engine_item_reaches_the_engine` | 項目 3-9 を切り替えると `RuleConfig` の値が変わる |
| `test_the_time_to_discard_is_the_chosen_one` | 持ち時間 10 秒を選ぶと、手番の締め切りが 10,000 ms |
| `test_only_the_owner_in_the_lobby_with_values_offered` ほか | 卓の主だけ・待ち合わせだけ・選択肢の値だけ、始めたあとは `"not waiting"`、見え方の `ru`、ゲームホストの値に戻すと選択が消える、保存して読み直し・終局のあとも残る |

画面:

- 卓の主の待ち合わせで東風・喰いタンなし・赤 4 枚・西入なし・30 秒に変えた (`s5_web1280_lobby_rules_owner.png`、640x360 は `s5_web640_lobby_rules_owner.png`。行のクリックで赤ドラが 4 枚からなしに変わることも見た)。ほかの人の待ち合わせ (観戦) に同じ値 (`s5_*_lobby_rules_member.png`)。
- 640x360 で卓の主として 30 秒を選んで始めると、手番の残りが 30 秒から数えた (6 秒後に「24秒」)。打牌の応答 61 ms (`s9_web640_player_after_discard.png`)。
- ボットが東風・喰いタンなし・持ち時間 10 秒を選んだ卓を観戦: 1 回目は東 4 局 10 本場で 1 人が 0 点を割って終局 (トビ、東場のまま。`s8_web1280_tonpuu_over_east4.png`)、2 回目は東 4 局 2 本場のあと終局 (トビなし、南入なし。`s8_web640_tonpuu_over_east4.png`)。ゲームホストのログに `rules [1, 0, 3, 1, 0, 1, 1, 0, 1, 1, 10]`。
- 喰いタンなしは画面で「出ない」ことを見せる場面を作れなかった (ボットは鳴かず、CPU の手は選べない)。試験で確かめた。

### 14.2 1 通の大きさ

| | 大きさ (呼び出しの鍵を含む答え) |
|---|---|
| 試験の最悪の形 (段階 3 の受け入れ、長い名前 16 人、満杯の河、加槓 4 つずつ) | **2,092 B** (MJ-L の 2,077 B に `ru` の 15 B) |
| 試験の結果の最悪の形 (3 人の和了で役 14 個ずつ、`yh` つき) | 1,995 B |
| リーチの助けの最悪の形 | 1,490 B |
| CPU の半荘 8 回 (4 席を段階 1・2・3・0 で見る) | 見え方の最大 966 B、公の見え方の最大 530 B |
| 試しのゲームホストで実際に打った中 (東風 3 回と半荘の途中、観戦者あり、止めたときのログ) | **530 B** (`view` の答え) |

上限 4096 B、試験の線 3800 B の内。`ru` は 11 個の小さい数で約 15 B、`yh` は和了 1 つで役の数のバイト。

### 14.3 試験・ビルド

| 確認 | 結果 |
|---|---|
| asterism-games `MJ_GAMES=8 bundle exec rake test` | 73 件、失敗 0 (ネットワークの試験 1 件は飛ばす)。新しい試験 13 件を含む |
| asterism-games `test_network.rb` (`ASTERISM_TEST_ROUTER=tcp/127.0.0.1:7447`、ユーザの zenohd、別のノード ID) | 1 件、失敗 0 |
| asterism-games の rubocop (設定が無いので、変えたファイルに Lint と Security) | 新しい指摘なし (既存の 3 件: 使われない引数 2、`rescue` の重なり 1) |
| asterism-console | 変えていないので回していない |
| fmruby-core `rake test` | 通った (golden 103、MicroPython の smoke) |
| fmruby-core `rake wasm:web` (`wasm:webflash` を含む) | 通った。新しいファイルは git に登録してから束に入った。`wasm:dist` (ユーザの 8006 の置き場) は作り直していない |
| sim の標準構成 (Modern 426x240、x86-64、develop と同じ build) | 卓のアプリが新しいファイルを読んで起きた。ユーザのゲームホストの mahjong2 の待ち合わせに戻り (MJ-L で sim が座ったまま)、ルールの欄の無い古いゲームホストで今までどおり出た。エディタを開いて 1 打鍵 (`Ln 1, Col 2 *`、`s10_sim_editor_one_key.png`)。core のエラー行 0 |
| 家の LAN の卓 | ユーザの zenohd・ゲームホスト (pid 418264)・8006 のページのサーバは止めていない。sim_up・sim_down が zenohd の container を作り直したので、そのたびに `tools/mahjong_local.rb up` で ws 7449 を戻し、`status` で 3 つとも動いていることを確かめた |

- ESP32 のビルドはしていない (C を変えていない。アプリのファイルが 1 つ増えただけで、Tab5・NARYAv4 の storage は `flash/app/game/mahjong/` の .rb を全部入れる)。
- sim では待ち合わせのルールの欄 (小さい画面の 1 行) を、新しいゲームホストの卓で見ていない。sim はユーザのゲームホストの mahjong2 に座っていて、出ると卓の状態を変えるので、出さなかった。

## 15. 画面の写し (MJ3b)

`C:\Users\Public\asterism_deploy\screenshots\mj3\`:

| ファイル | 中身 |
|---|---|
| `s5_web1280_lobby_rules_owner.png` | 卓の主の待ち合わせ: ルールの欄 (東風・喰いタンなし・赤 4 枚・30 秒、選んでいる行) |
| `s5_web1280_lobby_rules_member.png` | 観戦者の待ち合わせ: 卓の主 (ボット) が決めた値 (東風・喰いタンなし・10 秒) |
| `s5_web640_lobby_rules_owner.png` | 640x360 の卓の主 (クリックで赤ドラをなしに) |
| `s5_web640_lobby_rules_member.png` | 640x360 の観戦者の待ち合わせ (赤 4 枚・20 秒) |
| `s6_web1280_spectator_turn.png` | 1280x720 の観戦: 板の手番の線、「手番: 北 CPU3」 |
| `s6_web640_spectator_turn.png` | 640x360 の観戦: 4 席の帯 (手番の枠)、名前の下線、状態の行の手番 |
| `s7_web1280_result_ron_yaku.png` | ロン: 局、手牌・和了牌・副露、翻つきの役、符と翻と点、4 席の前後と変化 |
| `s7_web1280_result_draw_tenpai.png` | 流局: テンパイ・ノーテンと点の移動 |
| `s7_web640_result_ron_yaku.png` | 640x360 のロン (平和 1翻) |
| `s7_web640_result_draw_tenpai.png` | 640x360 の流局 |
| `s8_web1280_tonpuu_over_east4.png` | 東風の終局 (東 4 局、トビ) |
| `s8_web640_tonpuu_over_east4.png` | 東風の終局 (東 4 局、南入なし) |
| `s9_web640_player_after_discard.png` | 卓の主として始めたあとの打牌 (応答 61 ms) |
| `s10_sim_lobby_old_host.png` | sim: ルールの欄の無いゲームホストの待ち合わせ (今までどおり) |
| `s10_sim_editor_one_key.png` | sim のエディタの 1 打鍵 |

## 16. ユーザがすること・残ったこと (MJ3b)

- **VPS への反映**: asterism-games の main (`607fd9c`) を push し、6 章の 7 のとおり `./update.sh --when-idle main` で入れ直す。卓のアプリ (fmruby-core) は feature/mj3 を develop に入れ、ブラウザ版の束 (`wasm:dist`) と公開の置き場を作り直すと、部屋のリンクのページにルールの欄と新しい結果の画面が出る。順番はどちらが先でもよい (古いアプリは `ru`・`yh` を読まない、新しいアプリは無ければ出さない)。
- **遅らせの既定**: 0 秒にした (12.2)。観客に見せる卓なら `MAHJONG_SPECTATOR_DELAY_SECONDS` を compose の `.env` に足す。
- 鳴きの場面の `dl` (締め切りを待っている席のビット) は、座っている人にも観戦者にも、誰が鳴けるかを見せている (MJ2 からの形)。雀魂は全員を同じ時間待たせて隠す。遊び方の決めごとなので、このステップでは変えていない。
- 3 人の和了の結果は 640x360 の箱に入りきらないことがある (確かめていない)。
- 終局の画面は、終わった理由 (トビ・東 4 局・西 4 局) を書かない。
- ボットが結果の画面を待つ間 (`--result-ms`) に、その席が「途切れ」と出る (ボットが呼ばないため。試しの道具だけの話)。
- 観戦者の数の上限は、部屋の 16 接続と別々に数える (部屋に卓のアプリ以外のページがあると、16 接続の方が先に尽きる)。

## 17. 片付け (MJ3b)

試しのゲームホスト (SIGTERM で止め、状態は作業用の場所に残して後で消した)、ボット、開発サーバ (8016) とブラウザ (`web_down`)、sim (`sim_down`) を止めた。作った image・container・volume は無い。ユーザの zenohd (ws 7449 を戻した)・ゲームホスト・8006 のページのサーバは止めていない。卓 mahjong1 は使っていない (sim と画面の一覧に出ただけで入っていない)。fmruby-core の build/ は Linux の標準構成 (x86-64) のまま、作業コピーは develop に戻した。
