# GS2b 報告: console の図で、部屋のゲームホストと常設の卓のホストを見分ける

> 状態: 一部完了・止まる条件 (ACL) で判断待ち | 更新: 2026-10-17 | instruction_gs2b.md の結果。部屋のホストのラベル `games (room NN)` (console 0.3.3) と、常設の卓のホストのプロフィールの名前 `permanent tables` (asterism-games 0.7.1) は入れた。ただしプロフィールが図に出ない原因は console 自身の ACL (`asterism/**` が `@profile` に当たらない) で、直すには ACL の作り方を変える必要があるため止めた (4 章)

## 1. コミット

| リポジトリ | ブランチ | コミット | 中身 |
|---|---|---|---|
| asterism-console | main | `1f53ced` console 0.3.3 | 部屋のホストのラベル、試験、CHANGELOG・docs/rooms.md・docs/install.md |
| asterism-games | main | `f2e329e` games 0.7.1 | 常設の卓のホストのプロフィールの名前 `permanent tables`、試験、README |
| fmruby-core | develop | この報告と同じコミット | この報告と画面 (`report/gs2b/*.png`) |

push・タグはしていない。コミットの前に差分から実在の IP・ドメイン・端末 ID・鍵を検査して 0 件だった。

## 2. プロフィールが図に出ていなかった理由

**console 自身の ACL が `@profile` の鍵を通していない。** bridge の取りに行き方・時機・ゲームホストの ACL は正しかった。

- zenoh の鍵式では、`@` で始まる部分 (verbatim chunk) には `*` も `**` も当たらない。cloud_config.rb も部屋・席・ゲームホストの規則にはこれを踏まえて `.../@profile` と `.../**/@adv/**` を別に書いている (ゲームホストの役割 `games-host` にも `asterism/${node}/@profile` がある)。
- ところが console 自身の行 (`bin/asterism setup` が作る `console`、`rw: ["asterism/**"]`) には `asterism/*/@profile` が無い。生成された cloud.json5 の `console_rw` も `["asterism/**"]` だけ。
- そのため cloud router は、console の bridge が出す `asterism/<node>/@profile` への問い合わせ (`Asterism.profile_of`) も、`asterism/*/@profile` の購読 (ノードが put する通知) も通さない。bridge は「答えなし」として覚え、ノードが抜けて戻るまで聞き直さない (bridge のログにも何も出ない。答えなしは変化として記録しないため)。
- ゲームホストだけの問題ではない。**cloud router 越しでは、console はどのノードのプロフィールも読めていない** (名前・印・アイコン)。console 自身のものは網を通さずに描くので出る。G1b で名前が出たのは ACL の無い構成だったと見られる。

確かめ方 (deploy/local、console 0.3.2 の image、ゲームホスト `asterism-games:7c87c17`):

1. そのままの構成で、bridge の container の中から console の証明書で `Asterism.profile_async(n, false, 3, nil).raw_value` を `games`・`player-a`・`console` に向けて呼んだ。どれもすぐに `TimeoutError: ... (nobody answers)` で、答えを返すはずの console 自身 (bridge) も含めて返事が無かった。
2. 手元の登録の `console` 行だけを `asterism/**` + `asterism/*/@profile` に変え (rails runner。コードは変えていない)、`bootstrap` で cloud.json5 を書き直して zenohd・bridge・ゲームホストを起動し直すと、bridge が `profile games: protocol=1 kind=cruby name=asterism-games icon_hash=2a8c0626` を出し、図は `asterism-games (games)` になった。
3. 確かめのあと、行を `asterism/**` に戻した (deploy/local は最後に volume ごと消した)。

ついでに見えたこと: ゲームホストは `Asterism.connect` の時点で名前なしのプロフィールを put し、そのすぐあとに `net.profile(name:)` で名前つきを put する。bridge の通知は 1 本の列で順に処理し、最後のものが残るので、図に害は無い (ログに名前なしの行が 1 回挟まる)。

## 3. 直したこと

### 3.1 部屋のホストのラベル (console 0.3.3)

- `Bridge::Graph.build` に `room_hosts:` を足した (bridge は `Relay::Settings.room_hosts`、`ASTERISM_ROOM_HOSTS`、既定 `games`。値がノード ID でないときは空)。部屋の枠の中のノードで、ID が `room_hosts` の**どれかと等しい**ものに data `room_host: true` を付ける。
- ページ (`nodeElement`) は `room_host` のノードを `<ID> (room <slot>)` と描く (`games (room 05)`)。ラベル (`label`) は ID のまま残し、表示だけを変えた (呼び出し・権限・登録は ID を使うため。G1b のプロフィールの名前と同じ考え方)。部屋の中のプロフィールは console が読めないので、この表示は console の側だけで決まる。
- 枠の外の同じ ID、`games-bot1` のように名前が `games` で始まるだけのノード、席 (`wasm-0103`)、console 自身は変わらない。ACL のゲームホストの鍵は `games$*` (ボットも含む) だが、ラベルは指示書の「ノード ID が ASTERISM_ROOM_HOSTS のものに当たる」を「等しい」と読んだ。部屋のボット (`bin/mahjong-bots --namespace`) はホストの証明書で `games` 以外の node ID で入るので、それらにも付けたければ前方一致に変えるだけ (1 行)。
- `Bridge::Fixture` も `room_hosts` を読む (開発用の記録)。

### 3.2 常設の卓のホストのプロフィールの名前 (asterism-games 0.7.1)

- `Rooms#start_permanent` が子の設定に `profile_name: "permanent tables"` (`Rooms::PERMANENT_PROFILE_NAME`) を渡し、`Host#profile_name` がそれを使う。家の LAN の単体のホストは今の `asterism-games` (`Host::PROFILE_NAME`)、部屋のホストは名前なし (今と同じ)。
- 図には今の決まりどおり `permanent tables (games)` と出る (console がプロフィールを読めるとき。4 章)。

### 3.3 試験と文書

- console: `graph_rooms_test.rb` に 1 件 (枠の中のホストに印、枠の外の同じ ID・ボット・席・console には無し、`room_hosts` を渡さなければ誰にも無し、外のノードのプロフィールの名前は data にそのまま)。
- games: `test_rooms.rb` に 1 件と 1 行 (常設の子の設定に名前、ホスト 3 種それぞれが `net.profile` に渡す名前)。
- console: CHANGELOG 0.3.3 (2 章の穴を「Known」として書いた。直していないと明記)、docs/rooms.md (部屋の枠のホストのラベル、常設の卓の表示と、プロフィールが読めるときだけ名前が出ること)、docs/install.md「From 0.3.2 to 0.3.3」。VERSION・compose の既定・env.example を 0.3.3 に。
- games: README (常設の卓と Host のプロフィールの段)、VERSION 0.7.1。

## 4. 止めたこと (判断待ち)

指示書の 2 の「直すのが console の側なら直す」にあたる直しは、console 自身の ACL に `asterism/*/@profile` を足すことで、「ACL の作り方 (変更が要るなら止まる)」に当たる。コードの変更はしていない。選択肢:

1. **console の行に `asterism/*/@profile` を足す (コードで)。** `bin/asterism setup` の `console` の `rw` を `["asterism/**", "asterism/*/@profile"]` にする。今ある VPS の登録の行は setup では書き換わらない (`LocalIdentity` は行の作り直しをしない作りなので、確かめが要る) ため、移行 (migration か setup での追記) と apply (zenohd の再起動) が要る。範囲は console の rw 全体なので、console は全ノードの `@profile` に put・queryable もできるようになる。読むだけに絞るなら、cloud_config.rb で console に `asterism/*/@profile` の READ_IN と egress を別の規則として足す形 (席の `room/<slot>/asterism/*/@profile` と同じ形) がよい。
2. **VPS の登録の `console` 行を手で直す (コードは変えない)。** Admin > Relay で行の rw に `asterism/*/@profile` を足して apply。今日から効くが、新しく setup した console には入らない。
3. **今は直さない。** 部屋のホストは `games (room NN)`、枠の外は `games` のままでも 2 つは見分けられる (4 章の画面 `graph_acl_as_is.png`)。常設の卓の名前はプロフィールが読めるようになった時点で出る。

推奨は 1 の「読むだけの規則を cloud_config で足す」形。ほかのノード (端末・ブラウザ版) の名前・印・アイコンも同時に戻るので、ラベルの見た目は VPS で変わる (名前のある端末は「名前 (ID)」になる)。これは G1b の決まりどおりの表示で、決まりそのものは変えない。

## 5. 受け入れ条件の結果

### 5.1 試験 (条件 1)

| | 結果 |
|---|---|
| console `bin/rubocop` | 247 files, no offenses |
| console `bin/rails test` | 436 runs, 0 failures (49 秒) |
| games `MJ_GAMES=8 bundle exec rake test` | 159 runs (+1)、0 failures、skip 1 (28 秒) |
| games、平文の zenohd (`ASTERISM_TEST_ROUTER=tcp/127.0.0.1:17457`) で test_tally.rb / test_network.rb | 9 / 1 合格 (家の LAN のホストのプロフィールは今どおり `asterism-games`) |

### 5.2 手元の構成 (条件 2)

構成: console の deploy/local を作業コピーから `rehearse prepare` → `build` (image `asterism-console-rehearsal:0.3.3`) → `bootstrap`、行 `games` (役割 `games-host`) と `player-a` (`games`・`match`) を rails runner で作って .tar を出し、もう一度 `bootstrap`、`GAMES=1 rehearse up`。`.env` の `ASTERISM_ROOM_APPS` にオセロの卓のアプリを足した (gs2 6.4 と同じ)。ゲームホストは asterism-games の deploy/vps を作業コピーから build (image `asterism-games:gs2b`)、project `asterism-games-rehearsal`、`ASTERISM_PROJECT=asterism-rehearsal`、常設の卓 mahjong1・mahjong2・othello1。管理画面で部屋「Othello night」(スロット 01、Opens: オセロの卓) を作り、図は Playwright 1.56 (Chromium、rehearsal の web の網、管理者の IP) で写した。

- **ACL そのまま (VPS と同じ)**: 部屋の枠の中に `games (room 01)`、枠の外は `games`。合格 (部屋のラベル)。常設の卓の `permanent tables (games)` は **出ない** (2 章の理由)。画面 `report/gs2b/graph_acl_as_is.png`。
- **console の行に `asterism/*/@profile` を足した確かめ (手元だけ、2 章と同じ手順)**: 枠の中に `games (room 01)`、枠の外に `permanent tables (games)`。合格。画面 `report/gs2b/graph_with_profiles.png`。bridge のログは `profile games: ... name=permanent tables`。

### 5.3 ほかのノードのラベル (条件 3)

同じ 2 つの画面で:

| ノード | ACL そのまま | プロフィールが読めるとき |
|---|---|---|
| 部屋のブラウザ版 (席) | `wasm-0104` | `wasm-0103` |
| 端末 (CRuby、`player-a.tar` で `net.profile(name: "Alice's board")`) | `player-a` | `Alice's board (player-a)` |
| console 自身 | `console (this console)` | `console (this console)` |

どれも 0.3.2 と同じ決まりで、0.3.3 の変更で変わったものは無い。合格。

## 6. 残ったこと

- 4 章の判断 (console の ACL と `@profile`)。決まるまで VPS の図には `permanent tables (games)` もほかのノードの名前も出ない。
- 部屋のボットのラベル (3.1、等しいか前方一致か)。
- ACL の試験の道具 (`test/test_helpers/acl_evaluator.rb`) は `**` が `@` で始まる部分に当たらないことを正しく扱っているが、「console が `asterism/<node>/@profile` を問い合わせ・購読できる」を問う試験が無かった。4 章の 1 をするなら、その試験を一緒に足す。
- fmruby-core に親の指示書 (`instruction_gs2b.md`・`instruction_gs3.md`) が未追跡のまま残っている (この報告のコミットには入れていない)。
- 後片付け: deploy/local を `GAMES=1 VOLUMES=1 rehearse down` で volume ごと消し、`deploy/local/.env` と `certs/` を消した。asterism-games の rehearsal の project を `down -v`、volume `asterism-games-rehearsal_state`、deploy/vps の `.env` と `credentials/games.tar` を消した。作った image `asterism-games:gs2b`・`asterism-console-rehearsal:0.3.3` と、確かめの平文の zenohd `gs2b-zenohd` (`--rm`) を消した。前からある image (`asterism-console-rehearsal:0.3.2`・`asterism-games:7c87c17` ほか・`nginx:1.27-alpine`・Playwright) は残した。scratchpad の鍵 (.tar)・TOTP の秘密・cloud.json5 の写しも消した。docker の prune はしていない。console・games の作業コピーは clean。
