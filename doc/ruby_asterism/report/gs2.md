# GS2 報告: オセロの卓 (ホスト版) とその画面、CRuby の端末のクライアント

> 状態: 一部完了 (受け入れ条件 4・5 は保留、検収待ち) | 更新: 2026-10-17 | instruction_gs2.md の結果。互換の 3 つを落とし、部屋の卓を開くアプリのゲームから選ぶ形にし、オセロの卓 (asterism-games)・卓のアプリ (fmruby-core)・CRuby の端末のクライアントを作った。条件 4 (部屋) と 5 (常設の卓) は console の手元の構成に使える image が無く、作業コピーからのビルドは取り決めで禁止なので手を付けていない (8 章)

## 1. コミット

| リポジトリ | ブランチ | コミット | 中身 |
|---|---|---|---|
| asterism-games | main | `08f3f92` games 0.7.0 | 互換を落とす、部屋の卓の選び方、オセロの卓、`bin/othello-tty`、設定の例、README・docs/install.md |
| asterism-games | main | `7c87c17` | `bin/othello-tty`: 断られた join をバックトレースでなく 1 行で言う |
| fmruby-core | feature/gs2 | `30b5b29d` | オセロの卓のアプリ (`flash/app/game/othello/`)、今のオセロの名前、mahjong.app.rb の `view` を 2 引数に |
| fmruby-core | feature/gs2 | この報告と同じコミット | この報告と画面 (`report/gs2/*.png`) |

push・タグはしていない。VERSION は 0.7.0 (呼び方が変わったため)。コミットの前に鍵 (`~/.openai_key` の中身) と実在の IP・ドメイン・端末 ID の検査をして、どちらも 0 だった (例の `fmruby-aaaaaa`・`192.0.2.x`・`*.localtest` だけ)。

気づき: 作業中に fmruby-core の develop へ別の作業のコミット (`doc: ZH2 ...`) が入っていた。feature/gs2 はそれを含む develop (`70c83a67`) から切った。

## 2. 落とした互換

### 2.1 確かめた鍵

今の卓の麻雀のアプリ (mahjong.app.rb と mahjong_panels.rb。ブラウザ版も同じソース) が読む・送るもの:

| | アプリ | 扱い |
|---|---|---|
| `join` | `join(name)` (1 引数、秘密なし) | 秘密を消した。`exposes join: 1` |
| `mine` | `mine()` | 秘密を消した。`exposes mine: 0` |
| `view` | assist が既定 (1) のとき `view(token)`、ほかは `view(token, assist)` | アプリを常に `view(token, @assist)` に。ホストは `exposes view: 2` |
| `ru` / `rx` / `ry` | 3 つを足し合わせて読む (`rules`) | **読んでいるので残した** |
| `ru[10]` と `tb` | `ru[10] + tb * 1000` で持ち時間の値を作る | 読んでいるので残した |
| `tc` / `dc` | 待ち合わせの選択肢 (`tc` が無いときだけ自分の `RULE_TURNS`) | 残した。ホストは待ち合わせで必ず `tc` を出すので、アプリが `RULE_TURNS` の値 (10・20・30・60) を送ることはない |
| `RULE_TURNS` (ホスト) | 送らない | **消した**。`rule(10, 20)` などは `"value"` になる |

`ru`/`rx`/`ry` の分け方そのものは、アプリが 3 つを読む作りなので残した (コメントの「古いアプリが ru だけ読む」という説明は今の形に直した)。アプリ側の `RULE_TURNS` の予備は、指示書の範囲 (mahjong.app.rb の `view` の呼び方だけ) の外なので触っていない。

### 2.2 消したコードと試験

- `Game`: `secret_ids` の既定、`room_node_id`、`identity_error` の秘密の確かめ、`secret_key` と会員の `"key"`、`"taken"` の答え。部屋の中では「席の node ID か、ホスト自身の名前」だけが入れ、ほかは `"identity"` (前の `secret_ids: false` と同じ動き)。
- `Remote`: `join(name, secret = nil)` → `join(name)`、`mine(secret = nil)` → `mine`。
- `Config`: `rooms.secret_ids` と `ASTERISM_GAMES_ROOM_SECRET_IDS`。`Rooms`: 子への `secret_ids`、ログの「secrets checked」。
- `Mahjong::Remote#view`: assist の省略 (nil を 1 とみなす分岐)。`Mahjong::Table`: `RULE_TURNS` と set_rule のその分岐。
- `bin/mahjong-bots`: `--room` (秘密から node ID を作る形)。view は `ASSIST_DEFAULT` を付けて呼ぶ。
- 試験: test_mahjong_room_identity.rb の秘密の 7 件を消し、「join・mine は秘密を取らない (席でない呼び手は identity)」の 1 件を足した。tally の部屋の試験から「秘密を持つ古いページ」の 1 行を消した。`view` の 1 引数・`RULE_TURNS` の値を使っていた試験 (assist・rules・local_rules・time・network) は 2 引数・`tc` の値に直した。
- README の「Who joins」の「Before the seats」の段、`secret_ids` の行、MJ-E3 より前のアプリの説明を消した。docs/install.md の「secrets checked」と `ASTERISM_GAMES_ROOM_SECRET_IDS` の手順を直した。

なお、部屋に席が無い console (0.3.0 より前、スロットの証明書だけ) では、もう誰も部屋の卓に入れない。ホストがスロットの証明書でつなぐ道 (rooms.rb) は 3 つの互換に入っていないので残してある。

## 3. 部屋の卓の選び方

- `rooms.apps` (既定: 全ゲームの `screens`) は今のまま 1 つの一覧。
- `rooms.tables` (既定: **ゲームごとに最初の卓**、今は `mahjong1`・`othello1`) から、部屋が開くアプリを `screens` に持つゲームの卓だけをその部屋のホストに渡す (`Config.tables_for_app`、`Rooms#child_config(…, app)`)。
- `rooms.apps` にあっても、そのアプリのゲームの卓が `rooms.tables` に無いときは、その部屋は卓を持たない (`wanted` に渡すアプリを絞る)。起動時のログにアプリごとの卓を出す (`games: rooms opening /app/game/othello/othello.app.rb: othello1`)。
- `GET /status` の形と `summary` の鍵は変えていない (`apps` も設定のまま)。console の変更は要らない (console の `ASTERISM_ROOM_APPS` にオセロの卓のアプリを足すのは console の .env の設定)。
- ついでに、環境変数の卓の名前 (`ASTERISM_GAMES_PERMANENT_TABLES`) がゲームを見つけるようにした: `name:game`、なければその名前を `tables` に持つゲーム、なければ最初のゲーム (前はいつも最初のゲーム = 麻雀で、`othello1` と書くと麻雀の卓になった)。
- ホスト単体 (家の LAN) と常設の卓の既定 (`tables` を書かないとき) は前と同じ「最初に登録したゲーム (麻雀) の卓」。bin/asterism-games は麻雀・オセロの順に読む。設定の例 (config/example.yml・config/rooms.yml) には `othello1` を書いた。

## 4. オセロの卓

### 4.1 形と行数

| ファイル | 行 | 中身 |
|---|---|---|
| `lib/asterism_games/othello.rb` | 12 | 入口 (読むと `othello` が登録される) |
| `othello/board.rb` | 161 | `Board` (盤・合法手・返す石・"d3" の読み書き)、`Match` (手の履歴・手番・自動のパス・終局・勝ち)、`Cpu` (位置の重み + 返せる数) |
| `othello/table.rb` | 351 | `Table < Game`: 2 席・卓の主・CPU・始める・打つ・投了・持ち時間・見え方・`summary`・保存と再開 |
| `othello/remote.rb` | 68 | `Remote < AsterismGames::Remote` (出すメソッドと `from:`) |

ゲームが書いたのは 592 行 (ルールと CPU を含む)。gs1.md 6 章の「新しいゲームが書くもの」の形 (`register` / `remote` / `tables` / `screens` / `settings` / `DEFAULTS`、`tick`、見え方、`to_saved` / `restore` / `resume_clock`、`summary`) のとおりで、枠組みには何も足していない。枠組みの名前 `Game` とぶつかるので、オセロの 1 局は `Othello::Match` にした。

出すメソッド: `join(name)`、`mine`、`sit(token, seat)`、`stand`、`cpu(token, seat, on)`、`start`、`move(token, n, "d3")` (n は view の `n`。古い盤からの手は `"stale"`)、`resign`、`view(token)`、`table`、`leave`。

見え方は 1 つ (隠すものが無い): 状態の put と `table` が同じ形、`view` はそれに `me` (自分の席) を足しただけ。鍵は `st` `nm` `sk` `aw` `ow` `mb` `ts`、対局中は `bd` (64 文字) `tn` `n` `last` `lg` (手番の合法手 `"d3c4…"`) `cnt` `dl`、終局は `res`。画面は `lg` を使うので、客の側にルールは要らない (CRuby のクライアントも盤のルールを持たない)。

### 4.2 指示書に無かった細部で決めたこと (利用者から見える動き)

1 章に書かれていない細部は、麻雀の卓と今の端末どうしのオセロに合わせて決めた。止まる条件 (1 章に無い仕様の選択) に当たるほどではないと判断したが、検収で見てほしい。

- 色は始めるときにくじで決める (席 0 が黒・先手)。今の端末どうしのオセロも誘われた側がくじで決める。
- パスはホストが自動で打つ (履歴に `"--"`)。客はパスのボタンを持たない。
- 終局のあとは盤と結果が残り (`st: over`)、誰かが座る・立つと待ち合わせに戻り、卓の主は続けてもう 1 局始められる (麻雀の `over_seconds` のような自動の戻りは無い)。
- 対局中に席の人が抜けると CPU が代わる (麻雀と同じ)。途切れ (away) は表示だけで、代わりに打つのは持ち時間が切れたときだけ。
- 観戦の上限 `spectators` の既定は 14 (部屋の 16 人 - 2 席。麻雀の 12 = 16 - 4 と同じ考え方)。待ち合わせでは空いた席の数も足す。
- CPU の待ち `cpu_ms` は 700 ms (麻雀と同じ)。持ち時間切れで CPU が打つときは待たない。

### 4.3 席の管理を枠組みに上げるか

上げなかった。オセロの席まわり (sit / stand / cpu / pass_owner / forget / member_list / spectators_full?) は約 90 行で、麻雀とほぼ同じ形だが、麻雀の方は対局中の抜けで宣言・答えも片付ける、CPU に通し番号の名前を付ける、`humans` の上限がある、保存の形 (`seats` と `counts`) を持つ、と違いがある。上げると麻雀の 1327 行の卓の保存の形と動きに手を入れることになり、「麻雀の見た目・動き・鍵は変えない」を試験だけで保証するのは重い。GS3 で居る・居ないを on_join / on_leave に変えると `forget` と `keep_member?` の呼ばれ方が変わるので、そこで 2 つの使い手を見比べて上げるのがよい。

## 5. 卓のアプリと CRuby のクライアント

### 5.1 卓のアプリ (fmruby-core `flash/app/game/othello/othello.app.rb`、822 行)

- 名前は「Othello (table)」/「オセロ (卓)」、今のものは「Othello (direct)」/「オセロ (直接)」(.app.toml の画面の名前だけ変えた。中身は触っていない)。
- 窓のアプリ (372x222、今のオセロと同じ大きさ)。盤の描き方は今のオセロから写した (共通の部品には分けていない)。卓の一覧 (`*/games/othello*` と状態の購読) → Enter で join → 座る (S)・CPU (C)・始める (Enter、卓の主)・クリックか矢印 + Enter で打つ・Esc で投了 (確かめる) か卓を出る。
- 網のつなぎ方は麻雀の卓と同じ (`FmrbAsterism.settings`、APP `game`)。view は put の版が新しいときと 3 秒ごと (いることの印)。
- メモリ: 普通のプールで足りた (`large_memory` なし)。sim で VM プールの使用 479-492 KB / 1536 KB (31-32 %、30 秒ごとのログ。卓の一覧から 4 局を通して)。

### 5.2 CRuby の端末のクライアント (asterism-games `bin/othello-tty`、262 行)

- CRuby と asterism gem だけ (asterism_games の lib も読まない)。`Asterism.connect { |net| }` と `net.start`、状態の put を `net.session.subscribe` で受け、受け手のスレッドでは待つ呼び出しをしない (Queue で別のスレッドを起こして view を呼ぶ)。
- 盤はアスキーアート (X 黒、O 白、自分の手番に `*` で合法手)。コマンド: `sit` `stand` `cpu` `start` `d3` `resign` `auto` (最初の合法手を打つ) `quit`。`--commands "sit,cpu,start,auto"` で最初に流し、`--until-over` で 1 局見終わったら抜ける (試しの台本用)。
- `--router` (平文) と `--credentials x.tar` (mTLS、node は証明書の名前)、`--namespace room/NN` を取る。部屋で席でない名前で入ると `othello-tty: …: join refused: identity` で終わる。

## 6. 受け入れ条件ごとの結果

### 6.1 試験 (条件 1)

| | 件数 | 結果 |
|---|---|---|
| 前 (c733ba2) `MJ_GAMES=8 bundle exec rake test` | 145 | 0 failures、skip 1、31 秒 |
| 後 (08f3f92) 同 | 158 | 0 failures、skip 1、43 秒 (別プロセスの tally 9 件も合格) |
| 後、`ASTERISM_TEST_ROUTER=tcp/127.0.0.1:17457` で test_network.rb / test_tally.rb | 1 / 9 | 合格 (19 / 65 assertions) |

増減: 互換の分 -7 +1 (2.2)、`test_othello.rb` +15 (ルール 5・CPU 2・席 3・時間 2・保存 1・出すメソッド 1・最大の view 1)、`test_rooms.rb` +4 (既定の apps と卓、麻雀を開く部屋に麻雀の卓だけ・オセロを開く部屋にオセロの卓だけ・どのゲームの画面でもない部屋には無し、与えた卓をゲームで分ける・オセロの卓が無ければオセロの部屋は持たない、環境変数の名前からゲーム)。145 - 7 + 1 + 15 + 4 = 158。

### 6.2 麻雀の卓のアプリとボット (条件 2)

家の LAN の卓 (この Step の zenohd `gs2-zenohd`、tcp 17457 と ws 17459、ホストは平文、卓 mahjong1・mahjong2・othello1):

- ブラウザ版 (feature/gs2 で `rake wasm:web` し直した束、640x360、`?zenoh=ws/localhost:17459`) の麻雀: 一覧に `games/games/mahjong1` だけ (オセロの卓は出ない)、Enter で入る、1 で席 1、C で CPU、Enter で始める、打牌 2 回 (応答 62 ms)、ポンの申し出が出た。合格 (`report/gs2/mahjong_web_640x360.png`)。
- `bin/mahjong-bots --players 3 --rules 0=1` (3 人 + CPU、view は 2 引数) が東風戦を最後まで (87・89・90 手)、同時に同じホストでオセロの 1 局 (CRuby 対 CPU) も打った。合格。

### 6.3 同じオセロの卓で 3 種類の客 (条件 3)

同じ家の LAN の卓 `games/games/othello1`。sim の端末は sim の zenohd (`fmruby_zenohd`) につなぎ、`gs2-zenohd` を sim の網に入れて sim の zenohd へ `--connect` した (ルータどうしでつながり、sim の設定は変えていない)。

1. **ブラウザ版 (黒) と sim (白) が座って 1 局を終局まで**、CRuby のクライアント (`Watcher`) が観戦: 64 手 (パスを含む)、白の勝ち 19-45。打ち手は台本 (状態を読んで、手番の側の合法手の先頭を、ブラウザ版は `tools/fmrb_web.rb click`、sim は `tools/fmrb_input.rb click` で盤の升目をクリック) で、58 回のクリックはどれも通った。CRuby の観戦の画面は終局の盤 (下) を出した。
2. **CRuby のクライアントが座ってブラウザ版と 1 局**: CRuby が座り、ブラウザ版が S で座り、CRuby が `start`。CRuby の手はパイプ (FIFO) で標準入力に `d3` のような座標を 32 回打ち込み、ブラウザ版は 28 回クリック。64 手、CRuby (白) の勝ち 19-45。
3. **CRuby のクライアントが CPU と 1 局**: `--commands "sit,cpu,start,auto" --until-over` で終局まで (CPU の勝ち 10-54)。

CRuby の観戦の画面 (1 の終局):

```
games/games/othello1  over: linux (white) wins, 19-45
    a b c d e f g h
  1 O O O O O O O X
  2 O O O O O O X X
  3 O O O O O X O X
  4 O O O O X O O X
  5 O O O O O O O X
  6 O O O X O O O X
  7 O O O O X X O X
  8 X X X X X X O O
  X black (ブラウザ版の ID) (owner) 19
  O white linux                    45
  watching: Watcher
```

写した画面 (`report/gs2/`):

| 大きさ | ファイル | 中身 |
|---|---|---|
| 1280x720 (ブラウザ版) | `table_web_1280x720_over.png` | 1 の終局 (黒の側: 「あなたの負け」) |
| 640x360 (ブラウザ版) | `table_web_640x360_play.png` | ブラウザ版対 CPU の対局中 (自分の番、合法手の点) |
| 426x240 (sim) | `table_sim_426x240_over.png` | 1 の終局 (白の側: 「あなたの勝ち」) |

どの大きさでも窓は崩れていない (窓の大きさは同じで、1280x720 では画面の左上に小さく出る)。

### 6.4 部屋 (条件 4) と常設の卓 (条件 5): 保留

8 章。代わりに、次の 6.6 の image の確かめの中で、自分で立てた mTLS の zenohd に対して同じ形を通した (console ではないので条件の代わりにはならない):

- 常設の卓の形: 自分の CA で作った `alice.tar` (`asterism.json` に名前とルータ) で、CRuby のクライアントが `othello1` に座り CPU と 1 局を終局まで (CPU の勝ち 41-23)。
- 部屋の形: rooms.json に「麻雀を開く部屋 01」「オセロの卓を開く部屋 02」「今のオセロ (直接) を開く部屋 03」と `hosts: ["games"]` を書くと、01 に `mahjong1` だけ、02 に `othello1` だけのホストが上がり、03 には上がらなかった。部屋 02 で `games-tty1` (ホストの名前の下) の CRuby が CPU と 1 局を終局まで、部屋 01 でボット 3 + CPU が東風戦を最後まで。席でない名前 (`wasm-0209`) の CRuby は `identity` で断られた。

### 6.5 今の端末どうしのオセロ (条件 6)

sim の「Othello (direct)」とブラウザ版の「Othello (direct)」(ブラウザ版はランチャーに `/app/test` が出ないので Shell の `run /app/test/othello.app.rb`。前から同じ) が網で互いを見つけ、ブラウザ版が誘い、sim が Y で受け、3 手まで打った (応答 65-103 ms、置けない升目は「そこには置けない」)。窓の題が「Othello (direct)」、卓の方は「Othello (table)」で区別できる (`direct_web_640x360.png`・`direct_sim_426x240.png`)。合格。

### 6.6 VPS 用の一式 (条件 8)

`deploy/vps/` で `docker compose -p asterism-games-gs2 build` (image `asterism-games:08f3f92`)。console の代わりに、名前の決まった volume `gs2_rooms` (rooms.json) と network `gs2_router`、その上の mTLS の zenohd (`gs2-zenohd-tls`、自分の CA、証明書は scratchpad で作って捨てた) を用意し、`ASTERISM_PROJECT=gs2` と常設の卓の `games.tar` で起こした。**8 秒で healthy**。`status`:

```
asterism-games 0.7.0 since …: 2 room(s) with tables (at most 3), RSS 179.6 MB
  room 01: up, mahjong1 wait (0 seated, 0 CPU, 0 at the table)
  room 02: up, othello1 wait (0 seated, 0 CPU, 0 at the table)
  permanent tables (games): up, mahjong1 wait (…); mahjong2 wait (…); othello1 wait (…)
idle: nobody sits at a table
```

合格 (麻雀とオセロの卓が出た)。`update.sh` は VPS 用なので動かしていない。

## 7. 数字 (条件 7)

| | GS1 | GS2 |
|---|---|---|
| オセロの 1 通の最大 (試験、16 人・名前 32 バイト・全手の view) | - | 899 B |
| オセロの 1 通の最大 (実戦、平文 zenohd、打ち手 + 観戦の 2 人、長い名前) | - | 332 B |
| 麻雀の 1 通の最大 (実戦、平文 zenohd、ボット 3 + CPU の東風戦) | 515 / 522 / 530 B | 531 B |
| 麻雀の最悪の view (試験) | 2120 B | 2120 B (試験は変えていない) |
| ゲームホストの RSS、平文、1 プロセス (GS1 は麻雀の卓 1。GS2 は mahjong1・mahjong2・othello1 で、麻雀の東風戦とオセロの 1 局を同時に) | 59.6-60.0 MB | 60.8 MB (起動直後 58.3 MB) |
| image: 監督 / 常設 / 部屋 (VmHWM) | 43.3 / 51.2 / 51.4 MB | 43.8 / 47.3 (麻雀 2 + オセロ 1) / 51.2 (麻雀の部屋、東風戦のあと) MB、オセロの部屋 47.0 MB |
| image: `status` の `rss_kb` (全プロセスの和) | 145.9 MB (部屋 1 + 常設) | 185.0 MB (部屋 2 + 常設) |
| image: container (docker stats、外からの対局中) | 67.7 MiB | 73-74 MiB (部屋 2 + 常設)。ボットを container の中で動かした間は 157.8 MiB (ボットの分を含む) |
| 卓のアプリの VM の使用 (sim) | - | 479-492 KB / 1536 KB (31-32 %)、普通のプール |

麻雀の 1 通の最大と RSS は GS1 と同じ幅に入っている。

## 8. 保留した条件 4・5 と選択肢

着いた時点で `docker ps` に `asterism-zh2` の container は無く、asterism-console の `git status` も clean だった (main は 0.3.2 の `3cbe318`)。しかし:

- console の手元の構成 (`deploy/local/rehearse`) は console の image を要る。手元にある image (`asterism-console:local`・`644efe0`・`ead120b`) はどれも 2026-10-12 の、席 (0.3.0、`c9bd672`) より前のもので、部屋の席の証明書の道 (条件 4) を通せない。
- 作業コピーからの image のビルド (`rehearse build`) は「asterism-console の作業コピーも読むだけ (ビルドもしない)」の取り決めに当たる。`rehearse prepare` も作業コピーの中に `.env` と `certs/` を書く。
- 条件 4 には席の入ったブラウザ版の束 (`PLAY_DIST`) も要る。

そこで条件 4・5 には手を付けていない。選択肢:

1. console の作業が終わったことを確かめたうえで、作業コピー (main) から `rehearse build` してよいと親が決める。そのあと GS1 と同じ手順 (prepare → build → bootstrap → up、asterism-games の deploy/vps を `ASTERISM_PROJECT=asterism-rehearsal` で、`GAMES=1`) で 4・5 を通す。console の .env の `ASTERISM_ROOM_APPS` に `Othello (table)=/app/game/othello/othello.app.rb` を足す必要がある (console のコードの変更は要らない)。
2. console の image (0.3.0 以降) を別に用意してもらい、`CONSOLE_IMAGE` / `CONSOLE_VERSION` で使う (ビルドしない)。
3. 6.4 の自前の mTLS の確かめで足りるとみなし、console での確かめは GS3 以降にまとめる。

## 9. 見立てと違った点・撤回した仮説

- 「並べたツールの呼び出しは同時に走る」と考えて、ブラウザ版と sim の手を 1 回ずつの入力の列にして並べて送ったが、順に走ったので 2 手目から食い違った。状態を読んで手番の側をクリックする台本 (CLI の `fmrb_web.rb` と `fmrb_input.rb`) に替えた。
- `bin/othello-tty` で `quit` のあとにも「パイプの終わりまで待つ」に入ってしまい抜けなかった (`quit` と入力の終わりを分けた)。
- 画面の写しは描いた直後だと古い絵のことがあり、Esc → Y の投了が効いていないように見えた (効いていた)。続けて投了のボタンの位置を押したので、終局の画面の同じ位置の「始める」が押されて 1 局始まった。ボタンの配置が状態で入れ替わるのは、検収で触るときに気を付けてほしい点。

## 10. GS3 (on_join / on_leave、探し直し、case/in) に向けて分かったこと

- 途切れは今も「呼び出しの間隔」(view を 3 秒ごと) で数えている。オセロの卓のアプリも CRuby のクライアントも、いることの印のためだけに view を呼んでいる。on_leave で居なくなったことを知れれば、この定期の view は版が新しいときだけにできる。
- 席の管理を上げるかは、`forget` / `keep_member?` を on_leave から呼ぶ形に変えるときに、麻雀とオセロを並べて決めるのがよい (4.3)。
- 卓のアプリの卓の探し方は 2 秒ごとの `Asterism.each("*/games/*")` と状態の購読の両方。on_join を合図にすれば `each` は減らせる。
- 卓のアプリの view の場合分け (`st` が wait / play / over、自分の番か) は `case/in` で書ける形 (今は if の連なり)。
- CRuby のクライアントは受け手のスレッドで待つ呼び出しをしない形にした (購読の block から Queue で起こす)。asterism の README の「Do not wait in a block」どおりで、GS3 の on_join の block でも同じ形がよい。

## 11. 残ったこと

- 条件 4・5 (8 章)。
- 卓のアプリはまだ実機 (Tab5・P4-Nano) で動かしていない (指示書の範囲外)。
- 卓のアプリは再起動したときの「自分の席あり」の印 (麻雀の `mine` の探り) を持たない。同じ node は join で同じ token と席に戻る。
- ブラウザ版の暗いテーマでは、黒の席の印 (黒い丸) が背景に沈んで見えにくい (`table_web_*.png`)。
- 後片付け: 平文の zenohd `gs2-zenohd`、mTLS の zenohd `gs2-zenohd-tls`、volume `gs2_rooms` と `asterism-games-gs2_state`、network `gs2_router`、作った image `asterism-games:08f3f92`、deploy/vps の `.env` と `credentials/games.tar`、家の LAN の卓のホスト、web_* の開発サーバとブラウザ、sim (音を戻してから sim_down) を止めて消した。`alpine:3` は rooms.json を volume に書くのに使った (前からあったか分からないので残した)。docker の prune はしていない。fmruby-core の build/ は x86-64 のまま (`file build/fmruby-core.elf` で確認)。wasm/build はこの branch で作り直したまま。
