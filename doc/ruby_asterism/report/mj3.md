# MJ3 報告: 部屋で遊ぶ麻雀 (Step 3)

> 状態: MJ3a 完了 (検収待ち)、MJ3b 未着手 | 更新: 2026-10-15 | instruction_mj3.md の記録。MJ3a: ゲームホストが console の部屋の中に卓を出す作り (部屋のリンクが卓のアプリを開く部屋だけに、部屋ごとに 1 つのプロセス)、VPS 用の一式 (image・compose・update.sh・手順書)、console の見張りとバックアップを作った。手元の構成 (console の deploy/local) で、管理者が作った部屋のリンクをブラウザ版 4 つで開き、4 人で半荘 (東 1 局から西 4 局) を終局まで打った。途中でゲームホストの container と zenohd を起動し直し、どちらも数秒で同じ場面から続いた。作業中に console の door の誤り (127 バイトちょうどの WebSocket の 1 通で接続を切る) を見つけて直した。止まる条件には当たっていない。MJ3b の章は次のエージェントが書く

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
