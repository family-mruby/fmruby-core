# MJ7 報告: 牌譜を SQLite に入れ、成績を集計する

> 状態: 完了 (検収待ち) | 更新: 2026-10-18 | instruction_mj7.md の結果。ゲームホストは終局した牌譜を状態の volume の SQLite (`records.sqlite3`) に 1 回書き、卓ごとに直近 1000 対局を残す。部屋の牌譜は部屋を閉じても残り、その部屋のホストだけが出す。新しい公開メソッド `stats` が人ごと (表示名)・CPU / Claude / Jev ごとの成績を出す (asterism-games 0.14.0)。卓の麻雀のアプリの「直近の結果」から開ける「成績」の画面を足した (別ファイル `mahjong_stats.rb`)。終局のたびに一貫した写し (`records-backup.sqlite3`) を作り、console のバックアップはそれを取る。console のコードの変更は要らなかった。止まる条件には当たっていない

## 1. コミット

| リポジトリ | ブランチ | 中身 |
|---|---|---|
| asterism-games | main `b1d914b` | 枠組みの記録の帳簿 `lib/asterism_games/record_book.rb` (SQLite と JSON)、麻雀の記録を帳簿へ (`app/asterism_games/mahjong/records.rb`)、`stats` (`remote.rb`)、ホスト・監督から DB の場所 (`host.rb`・`rooms.rb`・`config.rb`)、試験 (`test/test_record_book.rb` 新規、`test_mahjong_records.rb` に 7 件、`test/no_sqlite3/`)、Gemfile (sqlite3)、README・docs/install.md・CHANGELOG・deploy/vps の注記、VERSION 0.14.0 |
| fmruby-core | feature/mj7 (develop `119152f0` から) | 卓のアプリ (新規 `mahjong_stats.rb`、`mahjong_replay.rb` に入口)、はみ出しの検査 (`tools/mahjong_overflow/check.rb`・`fixtures.rb`)、この報告と画面 (`report/mj7/*.png`) |

push・タグはしていない。asterism-console は触っていない (docs も変えずに済んだ。7 章)。

## 2. 表の形

置き場所は状態の volume の `state_dir/records.sqlite3` (設定 `records_db`、環境変数 `ASTERISM_GAMES_RECORDS_DB`)。部屋のホストと常設の卓のホストは別のプロセスなので、監督が自分の `state_dir` の DB の道を子の設定に渡す (`records_db`、`records_place`)。WAL・`synchronous=NORMAL`・busy timeout 10 秒。書くのは終局のときだけ (`BEGIN IMMEDIATE` で先に錠を取るので、同時に終局した 2 つのプロセスは commit で失敗せず待ち合う)。

```sql
games (id INTEGER PRIMARY KEY AUTOINCREMENT,   -- 牌譜の id (再利用しない)
       game TEXT,      -- "mahjong"
       place TEXT,     -- lan / permanent / room
       room TEXT,      -- 部屋の ID (place = room のとき)
       tbl TEXT, time INTEGER, at TEXT,
       variant TEXT,   -- tonpuu / hanchan
       reason TEXT, rounds INTEGER,
       body TEXT)      -- 牌譜そのもの (今の record の形の JSON)
seats (game_id REFERENCES games ON DELETE CASCADE, seat, name, kind,  -- kind: human / cpu / claude / jev
       rank, score, points,   -- points: 順位点 x 10 (final のまま)
       counts TEXT)           -- {"rounds","wins","dealins","riichi"} (JSON)
PRAGMA user_version = 1
```

- 枠組み (`AsterismGames::RecordBook`) は麻雀を知らない: ゲームが牌譜 (Hash) とその要約 (席ごとの名前・種類・順位・点・順位点・数えたもの) を渡す。数える中身は麻雀の `record_summary` (8 章)。数えたものを JSON の列にしたのは、枠組みの表を麻雀の列に縛らないため (`json_extract` で SQL からも読める)。
- 直近 1000 は卓ごと (設定 `records_kept` / `MAHJONG_RECORDS_KEPT`)。家の LAN と常設は (場所, 卓) ごと、部屋は**部屋をまたいで卓の名前ごと** (全部の部屋の `mahjong1` で 1000)。部屋ごとにすると部屋を開くたびに上限が増え、DB の大きさに上限が無くなるため (9 章、見立てと違った点)。
- 牌譜の `id` は DB の行の番号になった (卓をまたいで増える。卓の中では飛び番になる)。0.13.0 の「卓ごとに 1 から」とは違うが、アプリも CLI も `records` で得た id を `record` に渡すだけなので影響しない。
- 牌譜に `ais` (席ごとの AI の種類、`"claude"` / `"jev"` / `""`) を足した。`record(id)` の頭の鍵は変えていない。0.13.0 の牌譜は AI の名前 (`Claude`、`Jev2` ...) から種類を読む。
- sqlite3 gem が無いとき (gem だけ、または `records_db: json`) は 0.13.0 と同じ JSON (`<卓>.records.json`、直近 20、部屋のものは部屋と一緒に消える)。`stats` はその 20 を数える。

## 3. JSON からの取り込み

- SQLite の帳簿を開いた卓は、自分の保存の場所に `<卓>.records.json` があれば、その対局を古い順に (元の時刻・`at` のまま) 1 つの transaction で入れ、**ファイルを `<卓>.records.json.imported` に名前を変える** (消さない)。以後は誰も読まない。部屋のファイルはその部屋のホストが部屋の ID 付きで入れる。
- 名前を変える形にしたのは、0.13.0 に戻したいときに `.imported` を戻せば 20 局が戻るため (docs/install.md に書いた)。console のバックアップにも入る (数十 KB)。
- 家の LAN で確認した (受け入れ条件 5、10.5)。

## 4. バックアップの形

- console のバックアップ (`ASTERISM_BACKUP_EXTRA`) は extra の dir の普通のファイル (`*.tmp` を除く) をそのまま読む。使用中の `records.sqlite3` をそのまま写すと不整合になりうる。実際、deploy/local のバックアップでは `records.sqlite3` は 4096 B (頭だけ) で、中身は全部 `-wal` にあった (自動の checkpoint は 1000 頁から)。
- そこで、ゲームホストが**終局のたびに** `VACUUM INTO` で一貫した写しを作る: `records-backup.sqlite3.<pid>.tmp` に書いて `records-backup.sqlite3` へ rename。console は `*.tmp` を読まないので、作りかけは入らない。バックアップの `extra/games/records-backup.sqlite3` がいつも丸ごとの DB になる。
- 戻し方: ホストを止め、volume の `records.sqlite3*` を消して写しだけを置き、ホストを起こす。DB が無く写しがあるとき、ホストは写しから DB を作る (`records: /state/records.sqlite3 made from records-backup.sqlite3`)。手順は asterism-games docs/install.md 9 章 (ゲームホストの volume の戻し方は元から asterism-games 側の文書に書いてある)。
- **console の変更は要らなかった**: console のバックアップは今のまま写しを含めて取り、リストアは extra に触れない (元からそう)。console の docs/install.md・docs/rooms.md も「ゲームホストの保存した卓を取り込む」という書き方で、SQLite でも正しいので変えていない。
- 写しの分だけ volume とバックアップは倍になる (1000 対局で 17-23 MB ずつ。tar.gz で縮む)。
- 見立てと違った点: 部屋と常設のホストが一斉に起きて写しから戻すとき、最初の版 (写しを tmp に写して rename) は、先に DB を開いたプロセスの下へ別のプロセスが新しいファイルを rename で置き、`attempt to write a readonly database` で常設のホストが 1 回落ちた (監督が 1 秒後に起こし直した)。hard link (`File.link`、既にあれば失敗) で 1 つのプロセスだけが置く形に直し、4 プロセス同時の試験を足した。直した image で同じリストアをやり直し、落ちないことを確認した (10.4)。WAL への切り替えの錠の取り合いにも再試行を入れた。

## 5. stats の形と数え方

`stats(days, game, scope, from)` (全部省ける、-1 個)。meta に args / doc。

| 引数 | 既定 | |
|---|---|---|
| days | 0 | 0 は残っている全部、N は直近 N 日 |
| game | `""` | `"hanchan"` / `"tonpuu"` / `""` 両方 |
| scope | `"all"` | `"all"`: DB の全部の対局 (全部の卓・場所・部屋、閉じた部屋も)。`"table"`: この卓の対局 (部屋の中ならその部屋の) |
| from | 0 | 頁 (前の答えの `next`) |

答え: `{"rows" => [...], "games" (数えた対局数), "total" (行の数), "next", "days", "game", "scope", "store" ("sqlite" / "json")}`。1 通 (`RECORD_BYTES` = 3,300 B の MessagePack) に入るだけ入れて頁に分ける (長い名前で 13 行ほど、試験で 61 行を頁で全部読めることを確認)。

1 行:

- `kind`・`name`: 人は `"human"` と卓で入れた表示名 (同じ名前は同じ人)。CPU は 1 行 (`"cpu"`、名前 `"CPU"`)、AI は種類ごとに 1 行 (`"claude"` / `"jev"`、名前はそのラベル)。並びは人 (対局数の多い順)、そのあと種類。
- `games`、`ranks` (1-4 位の回数)、`avg_rank`、`top_rate` (1 位率)、`rounds` (出た対局の局数の合計)、`wins`、`dealins`、`riichi`、`win_rate` / `dealin_rate` / `riichi_rate` (局数に対する率)、`avg_score` (終局の点の平均)、`points` (順位点の合計 = `final` の 4 つ目 / 10、ウマ込み)。

数え方 (終局のときに牌譜から数えて `seats` に入れる。`stats` は牌譜を読まない):

- 和了: 局の結果の `wins` の席 (ダブロンは 2 人とも)。三家和の流局は数えない。
- 放銃: 結果が `ron_agari` のときの `from` の席。ダブロンでも 1 局 1 回。
- リーチ: 行いの `riichi`。
- CPU・AI の行の `games` は**席の数**で数える (1 対局に CPU が 2 席なら 2)。種類の行は「その種類の席の成績」と読む (README に書いた)。
- 試験は、牌譜を別の数え方 (和了は `tsumo` / `ron` の行い (三家和を除く)、放銃は `ron` の行いの出した席) で数えた値と `stats` の数が全部の鍵で合うことを、AI 2 種と人を混ぜた東風戦 4 回で確認した。

## 6. 部屋の中の stats の範囲

- **部屋の中の卓も、既定 (`"all"`) は全体を数える**。人の成績は場所をまたいでまとめるもの (1 章の 5) で、閉じた部屋の牌譜を残すのも集計のため (1 章の 3) なので、部屋の中でも同じ人の通算が見えるようにした。`"table"` でその部屋の対局だけ。
- 出すのは名前と数だけで、牌譜 (配牌・行い) は `stats` からは出ない (試験で答えに `moves` / `hands` が無いことを確認)。牌譜を出す `records` / `record` は、部屋の中のホストならその部屋の対局だけ、常設・家の LAN ならその卓の対局だけ。部屋の外の人に部屋の牌譜は見えない (1 章の 3 と矛盾しない)。
- 裏返しとして、常設の卓や別の部屋の人にも、部屋で打った人の名前と成績 (数) が `"all"` に出る。指示書の既定の見立て (名前と数だけの集計は見せてよい) どおりだが、部屋の参加者の名前を部屋の外に出したくない運用なら変える余地がある (12 章)。

## 7. console について

コードも docs も変えていない (4 章)。止まる条件の「console のコードの変更が要る」には当たらなかった。

## 8. 卓のアプリ

- 「直近の結果」の画面に「成績 S」のボタンと S の鍵。開くと `mahjong_stats.rb` を `require` する (振り返りと同じく、対局中は読まない)。
- 表: 名前・対局・平均順位・1 位率・和了率・放銃率・リーチ率・平均点・順位点。自分の名前は黄、CPU・AI は薄い色。平均と率はアプリが整数の数から出す (float を描かない)。426x240 は後ろの 3 列 (リーチ率・平均点・順位点) を落とす。
- 切り替え: P で期間 (全期間 → 30 日 → 7 日)、T で範囲 (全体 ↔ この卓)、↑↓ で送る、Esc で直近の結果へ。ボタンでも同じ。全部の頁を読んでから描く (最大 120 行)。
- 0.14.0 より前のホストでは「このホストは成績を出さない (0.14.0 より前)」を出す。
- 卓に座っている間に対局が始まったら、成績の画面も閉じて卓に戻る (振り返りと同じ)。

## 9. 見立てと違った点・判断したこと

- **部屋の牌譜の上限の単位**: 「卓ごとに直近 1000」を部屋ごとの卓に当てると、部屋が増えるたびに上限が増え、DB が際限なく育つ。部屋の卓は名前ごと (全部の部屋の `mahjong1` で 1000) にした。部屋の外には出ない牌譜なので、利用者から見える違いは「古い部屋の対局が成績から先に落ちる」ことだけ。
- **使用中の DB は 4096 B で、中身は WAL にある** (4 章)。写しを作らないとバックアップはほぼ空になるところだった。
- **写しからの同時の戻し** (4 章): rename では壊れる。hard link にした。
- CPU・AI の行の対局数は席の数になる (5 章)。
- `records` の 1 頁は牌譜の JSON を 24 件ずつ読んで要約する (15-21 ms、1000 対局)。頭だけの列から作れば速くなるが、今の数で足りるのでそのままにした。

## 10. 受け入れ条件ごとの結果

### 10.1 試験 (条件 1)

| 確認 | 結果 |
|---|---|
| asterism-games `MJ_GAMES=8 bundle exec rake test` | **220 runs、39,633 assertions、0 failures、0 errors** (skip 3、ルータなし。リストアの直しのあとの最後の形) |
| `bundle exec rake test:gem` (sqlite3 なしの道: `test/no_sqlite3/sqlite3.rb` が `require "sqlite3"` を LoadError にする) | **42 runs、1,361 assertions、0 failures** (skip 6: SQLite の 5 件と、麻雀の登録の 1 件 (麻雀を読まないので)。JSON の道と「sqlite3 が無ければ JSON」の試験は走る) |
| 足した試験 | `test_record_book.rb` 7 件: JSON の形、sqlite3 が無いとき JSON (別プロセス)、卓ごとの上限 (席の行も消える、id の再利用なし)、部屋の牌譜は部屋だけが出し閉じても残る (部屋の卓は名前ごとに数える)、期間・種類の絞り込み、**4 プロセスが同時に 100 対局を書いて欠けない** (integrity_check ok、写しも)、写しからの戻し (4 プロセス同時)。`test_mahjong_records.rb` 7 件: 次のホストが読む、**1000 の上限** (1003 入れて 1000、頁で全部)、**JSON からの取り込み** (同じ一覧・同じ牌譜、`.imported`、2 回目は取り込まない)、**部屋を閉じても残り、ほかの部屋・常設・家の LAN には出ない** (stats の `"all"` は数える)、**stats が牌譜から別に数えた値と合う**、stats の頁、Remote の stats |
| fmruby-core `rake mahjong:overflow` (`rake test` の中) | **147 drawings: 0 failed、26 warnings** (426x240 の警告。MJ6 と同じ 26。成績の画面 3 つ (全期間・30 日とこの卓と古いホストの文・7 日、30 人の最長の名前と最大の数) は 3 つの大きさとも警告なし) |
| fmruby-core `rake test` | 通った (golden 103、MicroPython smoke ほか) |

### 10.2 家の LAN の卓 (条件 2)

構成: zenohd `mj7-zenohd` (eclipse/zenoh:1.10.1、tcp 17747・ws 17749、sim の網に入れて sim の zenohd へ `-e`)、ゲームホスト (asterism-games の作業コピー、node `mj7`、手元の asterism を `RUBYLIB`、`ASTERISM_GAMES_AI_FAKE=1`、状態は scratchpad)、sim (Modern 標準構成、卓は 640x360、音はミュート)、ブラウザ版 (`?w=1280&h=720&zenoh=ws/localhost:17749`)。

- 打ったもの: mahjong1 でボット 2 人 (Bot1・Bot2) と CPU の東風戦 3 回 (0.13.0 のホストで。10.5)、mahjong2 で客の台本 (look の代わりに view / act、名前 Alice・Bob) と CPU・偽物の Claude・Jev の東風戦 4 回。計 7 対局。
- 成績の画面: sim 640x360 (`sim640_stats_all.png`: 7 対局、Alice・Bob・Bot1・Bot2・CPU・Claude・Jev)、この卓・30 日 (`sim640_stats_table_30days.png`: mahjong2 の 4 対局)、ブラウザ版 1280x720 (`web1280_stats_all.png`)、入口の「成績 S」 (`web1280_records.png`)。
- **CLI と MCP で同じ数**: `asterism call --router tcp/127.0.0.1:17747 mj7/games/mahjong2 stats` の答えと、`asterism mcp --allow 'mj7/games/mahjong2.stats'` のツール `mj7.games.mahjong2.stats` (引数なし) の答えを比べて**一致** (7 行、`games` 7)。画面の数とも一致 (例: CPU 9 席・平均順位 1.33・1 位率 66.7 %・和了率 38.5 %、Claude 4 席・2.25)。MCP のツールの入力は `days`・`game`・`scope`・`from`、説明は doc。

### 10.3 部屋 (条件 3)

構成: asterism-console の作業コピー (0.3.4) の deploy/local (`rehearse prepare → build → bootstrap → up`)、役割 `games-host` と行 `games` を console の container の `bin/rails runner` で作り (`Relay::Issuer#issue_local`、docs/rooms.md の規則)、もう一度 bootstrap。ゲームホストは作業コピーから作った image `asterism-games:mj7` を deploy/vps (project `asterism-games-rehearsal`) で、`games.tar` で上げた (部屋の監督と常設の卓)。部屋は runner で `Room.open!` (麻雀を開く部屋を 2 つ、あとで 1 つ) と `Relay::RoomFiles.write!`、閉じるのも runner の `close!` (管理画面の Close と同じ)。

- 部屋 01 で `bin/mahjong-bots --credentials games.tar --namespace room/01` (ホストの名前のボット 2 人) と CPU の東風戦を 1 回 (`room 01: mahjong: mahjong1: record 1 kept (4 rounds, sqlite)`)。
- 閉じる前: 部屋 01 の `records` は 1 件、部屋 02 と常設の mahjong1 は 0 件。
- 部屋 01 を閉じた: 監督が `stopping host pid 29 (the room is closed or ended)`、`removed the saved tables of room e134...`。`/state/rooms/` に 01 が無くなり、**DB には `(1, "room", "e134...", "mahjong1")` が残った** (席の行も)。
- 閉じた後: 部屋 02・新しく開いた部屋 03・常設の mahjong1 の `records` は 0 件、常設の `record 1` は `no record`。`stats` (`"all"`) はどこからも 1 対局 (Bot1 など) を数え、`"table"` は 0。

### 10.4 バックアップとリストア (条件 4)

- 常設の mahjong2 でも 1 局打ち (id 2)、`bin/asterism backup --label mj7` (console の backup の container)。tar.gz の `extra/games/` に `records-backup.sqlite3` (61,440 B)、`records.sqlite3` (4,096 B)・`-wal` (135,992 B)・`-shm` と、卓の保存が入った。
- docs/install.md 9 章の手順で、ホストを止め、volume の `records.sqlite3*` と写しを消して、バックアップの写しだけを置いて起こした: `permanent: records: /state/records.sqlite3 made from records-backup.sqlite3`。常設の mahjong2 の `records` は前と**同じ**、`stats` は 2 対局、DB は 2 行で `integrity_check` ok。部屋の牌譜 (id 1) も戻った。
- 1 回目は 4 章の同時の戻しで常設のホストが 1 回落ちた。直した image でやり直して落ちなかった (部屋 02・03 と常設が同時に起き、写しから作ったのは 1 プロセスだけ)。

### 10.5 0.13.0 の JSON から上げる (条件 5)

0.13.0 (`536defe` の worktree) のホストで mahjong1 に 3 対局 (JSON 48,787 B) を作り、`records` を取った。止めて 0.14.0 のホストを同じ状態の場所で起こすと `mahjong1: 3 records taken into records.sqlite3 (mahjong1.records.json renamed to mahjong1.records.json.imported)`。`records` の答えは 0.13.0 のものと (id も含めて) **一致**。sim の「直近の結果」にも同じ対局が出た。

### 10.6 数字 (条件 6)

| | 値 |
|---|---|
| 1 対局の SQLite の増え方 | 東風戦 約 17 KB、半荘 約 23 KB (1000 対局の DB を `VACUUM INTO` した写しの大きさ / 1000。牌譜の JSON はそれぞれ 14,339 B・22,127 B、席の行と索引が乗る) |
| 1000 対局のときのファイル | 1 卓 17 MB (東風戦) - 23 MB (半荘)、写しも同じだけ。VPS の形 (常設 2 卓 + 部屋の卓 1 つ) で最大 約 50-70 MB と写し |
| 終局のときの書き込み (入れる・古いものを消す・写し) | 23 ms (東風戦 1000 対局)、35 ms (半荘 1000 対局)。`VACUUM INTO` 単独で 25 / 45 ms |
| `stats` の答えの時間 (ホストの中) | 1000 対局 (4 人) で 17-45 ms |
| `stats` の答えの大きさ | 家の LAN の 7 行で 1,435 B (返事の最大 1,472 B)、1000 対局・7 行でも同じ程度 (行の数で決まる)。1 通は 3,300 B まで (頁) |
| `records` の 1 頁 / `record` の 1 局 | 15-21 ms / 16-17 ms (1000 対局) |
| ゲームホストの RSS | 家の LAN: 0.14.0 で 66 MB (成績を何度か答えた後)、0.13.0 の起動直後 62 MB。deploy/local: 監督 43 MB、部屋のホスト 51 MB ずつ、常設 51 MB (前の報告の 45 / 50 / 51 MB と同じ程度) |
| 卓のアプリの VM (sim、LARGE 3 MB) | 卓の一覧 822 KB → 直近の結果を開いて 890 KB → 成績を開いて **921 KB** (+31 KB: `mahjong_stats.rb` のコンパイルと 7 行)。`mahjong_stats.rb` 8,138 B |

### 10.7 sim の標準構成のエディタ (条件 7)

sim (Modern、`build/fmruby-core.elf` x86-64) でランチャーからエディタを開き `x` を 1 打鍵、`Ln 1, Col 2 *` (`sim_editor_one_key.png`)。core のログに `Guru` / `abort` は無い (`[Mahjong] No exception detected` 2 回)。

## 11. 試験・ビルド・片付け

| 確認 | 結果 |
|---|---|
| asterism-games `MJ_GAMES=8 bundle exec rake test` (最後の形) | 220 runs、0 failures |
| asterism-games `bundle exec rake test:gem` | 42 runs、0 failures |
| fmruby-core `rake test` | 通った (はみ出し 147・失敗 0) |
| fmruby-core `rake spinel:gen wasm:mruby wasm:mrb wasm:web` | 通った (成績の画面を入れた bundle) |
| fmruby-core build/ | x86-64 のまま (`file build/fmruby-core.elf`)。アプリは flash/ なので Linux の再ビルドは要らなかった |

片付けは 13 章。

## 12. 残ったこと・選べること

- 実機 (Tab5) の成績の画面と VM は見ていない (範囲外)。sim の +31 KB からは LARGE プール (2 MB) に余裕がある見込み。
- 426x240 の成績の画面は、はみ出しの検査だけ (落ちない・列を減らす)。画面では見ていない。
- 新しいアプリを 0.13.0 までのホストに向けると、成績の画面は「このホストは成績を出さない」。VPS は 0.14.0 を入れるまでその状態。
- 部屋の参加者の名前が部屋の外の `stats` (`"all"`) に出ること (6 章) は既定の見立てどおりにしたが、運用で嫌なら次から選べる (どれもこの作業では実装していない):
  1. **今のまま** (推奨): 名前と数は全体で見える。人の通算が場所をまたいで揃う。
  2. 部屋の外の `"all"` は部屋の対局を数えない: 部屋の参加者の名前が外に出ない。代わりに部屋で打った分は部屋の中でしか通算に入らない。
  3. 部屋の対局の人を外では匿名の 1 行にまとめる: 数は残り名前は消える。人ごとの通算は部屋をまたがなくなる。

## 13. 鍵と実在の値の検査・片付け

- コミットの前に 2 つのリポジトリの差分 (新しいファイルを含む) を `sk-ant`・`Bearer`・`API_KEY=`・`TOKEN=`・IPv4 の形・メールアドレスで検査した。該当は README の例の `192.0.2.2` (文書用の番地) と、この報告の `127.0.0.1` (手元の折り返し) だけ。写しに出る名前は Alice・Bob・Bot1・Bot2・CPU・Claude・Jev と卓の名前 (mj7/mahjong1-2) だけ。
- 片付け: 家の LAN のゲームホスト (TERM)、0.13.0 の worktree (`git worktree remove`)、`mj7-zenohd` (rm)、web_* の開発サーバとブラウザ (web_down)、sim (音を戻して sim_down、残った網 `family-mruby_default` を消した)、deploy/local (`GAMES=1 VOLUMES=1 rehearse down`、`.env` と `certs/` を消した。console の作業コピーは clean)、asterism-games の rehearsal の project (`down -v`、残った volume `asterism-games-rehearsal_state` を消した)、deploy/vps の `.env` と `credentials/games.tar`、作った image `asterism-games:mj7`・`asterism-console-rehearsal:0.3.4`・取ってきた `debian:stable-slim`。scratchpad の鍵 (.tar)・管理者のパスワード・状態の場所・bootstrap のログを消した。前からある image (`asterism-console-rehearsal:0.3.2`、Playwright、`eclipse/zenoh:1.10.1` ほか) は残した。docker の prune はしていない。API の鍵は使っていない。
- fmruby-core の build/ は x86-64 のまま。wasm/build はこの branch で作り直したまま。
