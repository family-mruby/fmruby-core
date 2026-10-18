# GS9 報告: 同じ招待リンクから作った部屋どうしで牌譜を見られる

> 状態: 完了 | 更新: 2026-10-18 | console 0.3.8 (`240a42d`) と games 0.16.0 (`781f130`)。招待リンクごとに秘密と無関係な印を持たせ、`rooms.json` の部屋に `"group"` として書く。ゲームホストは部屋の記録に印を残し、`records` / `record` は自分の部屋の記録と同じ印の記録を出す。両リポジトリの試験が通り、deploy/local と games の一式で、部屋 A の牌譜が同じ招待の部屋 B から一覧・読み出しでき、別の招待の部屋 C と管理者の部屋 D からは見えないことを `records` の答えで確認した

## 1. コミット

| リポジトリ | コミット | 件名 |
|---|---|---|
| asterism-console (main) | `240a42d` | console 0.3.8: the rooms of an invite link share their game records |
| asterism-games (main) | `781f130` | games 0.16.0: the rooms of one invite link see each other's records |

push・タグはしていない。差分は実在の値 (メールアドレス・ドメイン・IP) を grep し、無いことを確認してからコミットした。fmruby-core ではこの報告を書いただけで、git の操作はしていない。

## 2. 印の形

- **招待リンクの行ごとに作るランダムな文字列**。`SecureRandom.hex(16)` で 32 桁の 16 進 (例 `5b7c1d2e3fe09b4c641c2b15828241d8`)。招待を作るとき (`before_validation on: :create`) に付く。秘密 (`token`) ともそのハッシュ (`token_digest`) とも関係が無く、秘密でもない。
- 列は `room_invites.records_group` (string、NOT NULL、一意の索引)。`group` は SQL の予約語なので列名は `records_group` にした。`rooms.json` の上の名前は指示どおり `"group"`。
- 印は変わらない。Revoke しても消さないので、無効にした招待の部屋が開いている間はその招待の記録が見える (試験あり)。
- games 側は印の形を `/\A[A-Za-z0-9_-]{8,64}\z/` で受け、外れるもの (数値・空・記号入り) は印なしとして扱う。

## 3. `rooms.json` と記録の表の変更

### console (`rooms.json`)

招待から作った部屋だけ、スロットに `"group"` が付く。管理者が作った部屋には付かない。

```
"slots": {
  "02": {"room": "e1a6...", "until": ..., "max": 16, "app": "/app/game/mahjong/mahjong.app.rb", "seats": 16,
         "group": "5b7c1d2e3fe09b4c641c2b15828241d8"},
  "04": {"room": "7d32...", "until": ..., "max": 16, "app": "/app/game/mahjong/mahjong.app.rb", "seats": 16}
}
```

移行 `20261018180000_room_invite_records_group.rb`: 列を足し、既存の招待にはそれぞれ別の印を作って入れてから NOT NULL と一意の索引を付ける (試験で既存 2 行に別々の印が入ることを確認)。

### games (記録の表)

- `games` 表に列 `grp TEXT` を足した (部屋の印。部屋の外・印の無い部屋・これまでの記録は NULL)。索引 `games_grp (grp, tbl, id)`。
- **形式 (`user_version`) は 1 のまま、列をその場で足す**。開くたびに `PRAGMA table_info` で無ければ `ALTER TABLE` する (BEGIN IMMEDIATE の中なので、同時に起きる複数の部屋のホストでも一度だけ)。版を 2 に上げると 0.15.0 が「format 2」で拒むので、戻せるようにこの形にした。0.15.0 は列を名指しで INSERT するので grp を NULL のまま書き、読みにも影響しない (試験で確認)。
- 範囲:
  - `records` / `record` (`size` / `list` / `get`): 印のある部屋では `game = mahjong AND place = 'room' AND tbl = <卓名> AND (room = <自分> OR grp = <印>)`。印の無い部屋・常設・LAN は今までと同じ式。
  - 成績の `"table"` と `@status` の `last`: 自分の部屋の分だけ (今どおり)。`last` はこれまで `list(0, 1)` を使っていたので、自分の分だけを読む式に分けた。
  - 古い順に消す数 (`records_kept`) は今どおり、部屋の卓は卓の名前ごとにまとめて数える (印は関係しない)。
  - JSON 形式 (sqlite3 gem なし) は印を使わない (部屋の記録は部屋と一緒に消える形のまま)。0.13.0 の JSON から取り込む記録には印を付けない。
- 渡し方: `Rooms.wanted` が `"group"` を読み → `child_config` の `"records_group"` → `Host` が部屋の卓 (room_id のあるとき) にだけ渡す → `Mahjong::Records#init_records` が `RecordBook.open(group:)` に渡す。部屋の印はその部屋のホストが起きるときに読む (開いている部屋の途中で印が変わっても、ホストを起こし直さない)。

## 4. 受け入れ条件の結果

### 4.1 試験

| | 結果 |
|---|---|
| console `bin/rails test` | 488 runs, 5034 assertions, 0 failures, 0 errors, 0 skips |
| console `bin/rubocop` | 262 files inspected, no offenses detected |
| games `MJ_GAMES=4 bundle exec rake test` | 248 runs, 0 failures, 0 errors, 3 skips (前からある skip: ルータの無い網の試験、gem 単体の試験、局が先に終わった場合) |

足した試験:

- console `test/controllers/room_invites_test.rb`: 招待 1 から部屋 A・B、招待 2 から C、管理者が D。`rooms.json` で A・B が招待 1 の印、C が招待 2 の印、D に `"group"` が無い。秘密 2 つとハッシュ 2 つが `rooms.json` に無い。招待 1 を Revoke しても A の印が残る。
- console `test/db/room_invite_records_group_test.rb`: 移行を戻して既存の招待 2 行を入れ、移行すると別々の 32 桁 16 進が入る。
- games `test_record_book.rb`: 同じ印の部屋 B が A の記録を一覧・`get` できる。印の違う部屋・管理者の部屋・別の卓名・常設・LAN からは出ない。管理者の部屋の記録は誰にも出ない。成績の `"table"` と `last` は自分の分だけ。印の形でないものは印なし。0.15.0 の形のデータベースに列が足され、形式は 1 のまま、古い記録は印を持たず自分の部屋だけに出る。0.15.0 の INSERT がそのまま通る。
- games `test_rooms.rb`: `wanted` が印を読み、形の外れたものは nil。部屋のホストの設定に `records_group`、印の無い部屋 (古い `rooms.json`) は nil。`Host` は部屋の卓にだけ渡す。
- games `test_mahjong_records.rb`: 実際の麻雀の卓で、部屋 A (招待 1) が東風戦を終局まで打って閉じる → 同じ招待の部屋 B の `records` に A の対局が出て、`record(id)` と `record(id, 0)` が A で読んだものと同じ。B の成績 `"table"` は 0 局、`last_result` は nil。招待 2 の部屋 C、管理者の部屋 D、常設、LAN には出ない。D が打った対局は B に出ない。

### 4.2 手元の確認 (deploy/local + games の一式)

構成: console の作業コピーから `rehearse prepare` → `build` (image `asterism-console-rehearsal:0.3.8`) → `bootstrap` → `up`。ゲームホストの行 `games` は console のコンテナの `bin/rails runner` で `Relay::LocalIdentity.issue` を使って作り、`.tar` (席の `.tar` と同じ形、名前空間なし) を作って `relay:generate` → zenohd を再起動。asterism-games の deploy/vps を project `asterism-games-rehearsal`、`ASTERISM_PROJECT=asterism-rehearsal`、`ASTERISM_GAMES_ROOM_CREDENTIALS=/credentials/games.tar` で、作業コピーから作った image `asterism-games:gs9` で起こした。AI の鍵は入れていない (CPU とボットだけ、外の API は呼んでいない)。

**画面は使わず `records` の答えで確認した**。部屋に入るブラウザ (Playwright と browser build) を用意する代わりに、部屋の対局はゲームホストの証明書で入るボット (`bin/mahjong-bots --credentials /credentials/games.tar --namespace room/<slot> --players 4 --rules 0=1`、部屋のホストが信頼する `games-...` の名前) で東風戦を終局まで打ち、`records` / `record` も同じ証明書の小さな CRuby のクライアントで各部屋の名前空間から呼んだ。麻雀アプリの「直近の結果」と振り返りはこの 2 つを読むだけなので、答えが同じなら画面も同じになる。

1. 招待 2 つ (印 `5b7c...41d8` と `a2cd...41fe`) を作り、招待 1 から部屋 A (スロット 01)。ボット 4 人が東風戦を終局まで (ホストのログ `game over (normal_end)`、`record 1 kept (8 rounds, sqlite)`)。
2. A を閉じる (ホストが止まり、保存した卓が消える)。
3. 招待 1 から部屋 B (02)、招待 2 から部屋 C (03)、管理者として部屋 D (04)。`rooms.json`:

```
rooms.json slot 02: {"room":"e1a6c7d4ea9ea413","app":"/app/game/mahjong/mahjong.app.rb","group":"5b7c1d2e3fe09b4c641c2b15828241d8"}
rooms.json slot 03: {"room":"bfccccc364934584","app":"/app/game/mahjong/mahjong.app.rb","group":"a2cdfa2e7ced4d580c83be74358a41fe"}
rooms.json slot 04: {"room":"7d32dbbb7b44a066","app":"/app/game/mahjong/mahjong.app.rb"}
digests in rooms.json: 0
```

4. 各部屋の `records` と `record`:

```
room/02 records: {"records":[{"id":1,"at":"2026-10-18 14:27","game":"tonpuu","names":["Bot3","Bot4","Bot2","Bot1"],"ranks":[1,2,3,4],"scores":[25000,25000,25000,25000]}],"total":1}
room/02 record(1): {"id":1,"at":"2026-10-18 14:27","table":"mahjong1","game":"tonpuu","names":["Bot3","Bot4","Bot2","Bot1"],"final":[[0,1,25000,150],[1,2,25000,50],[2,3,25000,-50],[3,4,25000,-150]],"reason":"normal_end"} rounds=8
room/02 record(1, 0): round 0 of 8, 140 moves
room/03 records: {"records":[],"total":0}
room/03 record(1): {"error":"no record"}
room/04 records: {"records":[],"total":0}
room/04 record(1): {"error":"no record"}
```

A が開いていたときの答え (room/01) も同じ 1 件だった。データベースの行は `[1, "room", "c051132513cbf0f3", "5b7c1d2e3fe09b4c641c2b15828241d8", "mahjong1"]`、`user_version` は 1。

ボットの対局は 8 局すべて流局で終わった (点は 25000 のまま)。終局まで打った 1 局の記録として扱いに差は無い。

## 5. 互換

| 組み合わせ | ふるまい |
|---|---|
| 新しい games (0.16.0) + 古い console (0.3.7 まで、`"group"` なし) | どの部屋も印なし。記録は今どおりその部屋だけ (試験: 古い `rooms.json`)。 |
| 古い games (0.15.0 まで) + 新しい console (0.3.8) | `wanted` は知らないキーを読まないので今どおり。 |
| games を 0.16.0 から 0.15.0 に戻す | データベースはそのまま読める (形式 1、grp は無視、新しい行は grp NULL)。部屋ごとの記録に戻る。 |
| console を 0.3.8 から 0.3.7 に戻す | 移行があるので更新の backup を戻す (update.sh が戻す命令を出す)。games 0.16.0 は印を見なくなる。 |
| 0.16.0 以前の記録 | 印を持たない。今どおりその部屋だけ (さかのぼって付けない)。 |
| 更新の時点で開いている部屋 | console を先に更新しても、その部屋の印は games のホストが次に起きたとき (games の更新や再起動) から。それまでの対局には印が付かない。 |

成績の集計 (`stats` の `"all"` / `"table"`) と `@status` の `last` は変えていない。

## 6. VPS の更新に要ること

1. **console**: `./update.sh 0.3.8`。移行 1 つ (招待ごとの印。既存の招待にも作る)。`.env`・nginx・ルータの設定は変わらない。戻すには更新時の backup。
2. **games**: `./update.sh 0.16.0` (または `--when-idle 0.16.0`)。`.env` は変わらない。初回起動で記録のデータベースに列 `grp` が足される (時間はかからない、形式は 1 のまま)。
3. **順序はどちらが先でもよい**。片方だけでは何も変わらない。両方を更新したあと、games のホストが起きてから作った (または起き直した) 招待の部屋から効く。games を後に更新すれば、その再起動で開いている部屋も印を読む。
4. 麻雀アプリ (fmruby-core) と browser build は変えなくてよい (`records` / `record` を読むだけ)。
5. 公開 image のタグ (`v0.3.8`・`v0.16.0`) は付けていないので、VPS で `update.sh` を使う前にタグと image の公開が要る。

## 7. 気づいたこと

- **成績と `last` を自分の部屋に残した**: 1・2 章の「成績の集計は今どおり」に合わせ、成績の `"table"` と `@status` の `last` は印を見ない。麻雀アプリの「直近の結果」は `records` を読むので、同じ招待の前の部屋の対局が並ぶ (`last` を使う画面は無い)。
- **同じ卓の名前だけ**: 同じ印でも卓の名前 (`mahjong1`) が同じ記録だけを出す。部屋の卓は既定で各ゲームの最初の 1 つなので、同じ招待の部屋どうしは同じ名前になる。`rooms.tables` に麻雀の卓を 2 つ置く構成では、`mahjong2` の記録は `mahjong2` からだけ見える (部屋の中で今そうなっているのと同じ)。
- 麻雀アプリの画面を変える必要は無かった (止まる条件には当たらなかった)。

## 8. 後片付け

- games の一式を `docker compose -p asterism-games-rehearsal down -v` で volume ごと消し、deploy/vps の `.env` と `credentials/games.tar` を消した。作った image `asterism-games:gs9` を消した。
- deploy/local を `VOLUMES=1 rehearse down` で volume と network ごと消し、`deploy/local/.env` と `certs/` を消した。作った image `asterism-console-rehearsal:0.3.8` を消した。
- 前からある image (`asterism-console-rehearsal:0.3.2`、`asterism-games:7c87c17` ほか、`asterism-console:*`、Playwright) は残した。docker の prune はしていない。
- console の開発用データベース (storage/、git の外) は `db:migrate` で 0.3.8 の形になっている (招待は 0 行)。
- 両リポジトリの作業コピーは clean。Tab5・シリアル・sim・web・VPS・GitHub の設定には触っていない。
