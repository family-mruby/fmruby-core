# R4b 報告: 麻雀を FmrbAsterism::TableClient に載せ替える

> 状態: 完了 | 更新: 2026-10-25 | instruction_r4b.md の記録。共通部品 `FmrbAsterism::TableClient` に麻雀の要る口を足し、ユーザが決めた 2 つの直し (join の待ちを捨てる、誤りの答えでホストがいないを消す) を入れ、麻雀の接続と卓の節を部品に置き換えた。ホストの試験、手元のゲームホストでの sim (Modern、麻雀は 640x360) とブラウザ版の確認、Tab5・StickS3 の DIRAM の比較まで済んでいる。実機は使っていない。止まる条件には当たらなかった

## 1. 結果の要約

- **部品** (`lib/add/picoruby-fmrb-asterism/mrblib/table_client.rb`、385 → 401 行): 麻雀のための口 2 つ (`table_ask_view`、`blank:`) と、2 つの直しを入れた (2・3 章)。
- **麻雀**: 本体 `mahjong.app.rb` 2,083 → 1,884 行 (-199)。ほかのファイルは参照の書き換えだけ (行数は同じ)。9 ファイルの .rb の合計は 6,873 → 6,674。牌・鳴き・点数・観戦・牌譜・音・640/XL の画面には触っていない。
- **試験**: `test/asterism/table_run.rb` に 16 項目を足した (88 → 104)。`rake test` は全部通る。`rake mahjong:overflow` は 0 failed / 55 warnings で HEAD と同じ。
- **確認**: 手元のゲームホストで、sim の Modern (麻雀 640x360、オセロ・ボード 426x240) とブラウザ版 (麻雀 640x360、オセロ 426x240)。麻雀は前の版と新しい版を同じファームで順に動かして比べた (5 章)。
- **内蔵 RAM**: Tab5・StickS3 とも DIRAM は前後で同じ。VM プールも同じ幅 (6 章)。
- **コミット**: 2 つ (9 章)。push はしていない。

## 2. 足した口

| 口 | 形 | 使うアプリ |
|---|---|---|
| `table_ask_view(px, token)` (アプリの任意のメソッド) | あれば部品は `view(token)` の代わりにこれを呼び、返った future を待つ。`px` は卓の async の proxy | 麻雀 (`px.view(token, @assist)`) |
| `TableClient.new(..., blank: {})` | 一覧に載ったが put がまだ来ていない卓に覚える値 (毎回 `dup` した写し)。既定は nil で前と同じ | 麻雀 (`@tc.tables[p]` を Hash として読む。title の `list_ready?` も) |

r4a.md 7 章の見通しのうち、残りは部品をそのまま使えた。

- `table_dropped`: 麻雀は `@tc.forget` (leave を送らない) と `@seat`・`@view`・`@probed`・`@mine` の片付け、`@host_away` を戻す (前の collect_view の error の枝と同じ)。
- `table_view_failed`: `@host_away = true`。ログは `mahjong: view: no answer` (前は例外の型名)。
- `mine()` の探り (`probe_tables`) はアプリに残した。`@tc.proxy(p).async.mine`。一覧からホストの卓が消えたときの `@mine`・`@probed` の片付けは `tables_changed(false)` で行う (前は node_left の中)。
- on_destroy は `@tc.close` だけ (ホストに席を残す、前と同じ)。
- 状態の put (`table_state`): `hv` (ホストの版)、自分の卓なら `@host_away` を戻す、札の前は put を view に。一覧には `{st, sk, mb}` を返す。
- `take_view` の `@want_v` の引き上げは消した (部品の `want_v > view_v` で同じ判定)。アシストの段を変えたときの「すぐ view を聞き直す」は `@tc.refresh_view` にした (前は `@view_at = 0` と `@want_v` の書き換え。待っていた view があれば捨ててすぐ聞く点だけ違うが、結果は同じく新しい段の view が来る)。
- 打牌・ロビー・名前の変更の答えのあとの view は `@tc.ask_view` (前の ask_view と同じく、待っている view があれば何もしない)。
- join の拒否の文 (「卓が満員: <卓>」) のために、送った先の卓を `@join_to` に覚える (部品は拒否のときに `table` を nil にしてから知らせるため)。

## 3. 2 つの直しと試験

### 3.1 join の答えを待つ間に一覧へ戻る (1-1)

- 直し: `forget` (と、それを呼ぶ `leave`) で join の future (`@join_f`) も捨てる。答えは読まれず、次の join がすぐ送れる。
- 3 アプリともアプリのコードは変えていない (オセロ・ボードの Esc は `leave_table` → `@tc.leave`)。
- 麻雀では、join の答えを待つ間は状態の画面 (「卓に入っています」) でキーもボタンも受けないので、この不具合は前から起きない。直しは部品の中なので麻雀にも入っているが、麻雀の画面からは届かない。
- 捨てた join の答えはホストでは成り立っている (その端末は卓の会員になっている)。同じ卓に入り直せば同じ札が返る。ほかの卓へ行った場合は、ホストが離席として扱う (前の版で答えを読んでから出た場合と同じ)。麻雀では一覧に「(自分の席あり)」が出た (5.2)。

### 3.2 ホストから誤りの答えが来たとき (1-2)

- 部品の collect_view で、答えが来たら (誤り・Hash でない値も含めて) 先に host gone を消す (ホストが去った後に送った view の答えなら。麻雀の前の条件と同じ)。そのうえで誤りなら `table_dropped` (無ければ leave)。前は誤りの答えでは host gone を消さないまま table_dropped を呼んでいた。
- 見え方: オセロ・ボードの `table_dropped` は前から「卓から外れた」「ボードから外れた」を出し、forget で host gone も消えていたので、画面で目に見える違いはほぼ無い。違うのは table_dropped が呼ばれた時点で `host_gone?` が false になっていること、Hash でない答えでも host gone が消えること (アプリが卓に留まる形のときに、view の間隔が 30 s のままにならない)。
- join の誤りの答え (満席・観戦の上限) は、3 アプリとも前から `table_refused` の文で「ホストが答えない」を置き換えている。sim・ブラウザ版で確認した (5 章)。

### 3.3 試験 (`test/asterism/table_run.rb`、104 項目)

足した場合:

- join を送って答えが来る前に leave: join の待ちが消え、札が無いので leave は送らない。すぐ次の join (別の卓) が出る。両方の答えが来ても、入るのは 2 つ目だけ (table_joined 1 回、table_refused 0 回)。forget でも同じ。
- ホストが去った後に送った view に誤りの答え: table_dropped の時点で host gone が消えている、table_host_back は呼ばない。去る前に送った view の誤りでは消えない (前と同じ条件)。
- Hash でない答えでも host gone が消える (必須だけのアプリ)。
- `table_ask_view`: 札を渡して呼ばれ、ゲームの呼び方 (`view(token, 2)`) が出て、答えが table_view に届く。
- `blank: {}`: 一覧の値が `{}` で、卓ごとに別の物。

前の部品 (HEAD) に対して流すと、足した場合のうち 10 項目が落ち、`blank:` のところで引数の誤りで止まることを確認した。

## 4. 麻雀の行数

| ファイル | 前 | 後 |
|---|---|---|
| mahjong.app.rb | 2,083 | 1,884 |
| mahjong_panels.rb・replay・stats・title | 参照の書き換えだけ | 同じ行数 |
| .rb 9 ファイルの合計 | 6,873 | 6,674 |

- 消えたもの: 接続 (network_ready?・connect・lost・reconnecting?・exchange)、proxy、find_tables・add_table、node_left・node_joined、状態の購読、join・collect_join・ask_view・collect_view・view_due?、定数 9 つ (APP・HOST_APP・RECONNECT_MS・VIEW_EVERY_MS・LIST_EVERY_MS・RESCAN_MS・RESCAN_STEP_MS・HOST_GONE_VIEW_MS・WIFI_WAIT_MS)、インスタンス変数 15 個ほど。CALL_TIMEOUT は牌譜の呼び出しが使うので残した。
- 見込み (150-200 行減) の上の端。

## 5. 確認

### 5.1 構成

- zenohd: sim の `fmruby_zenohd` に `docker/asterism/docker-compose.zenoh-ws.yml` を重ね、ws 7449 を足した (sim・ブラウザ版・CRuby が同じルータ)。
- ゲームホスト: asterism-games の作業コピー (0.22.0、`ef9891a`) を `bundle exec bin/asterism-games` で。設定と状態は scratchpad (node `r4bgames`、mahjong1 (東風、cpu_ms 150)、mahjong2・othello2 (spectators 0)、othello1、board1)。AI は `ASTERISM_GAMES_AI_FAKE=1`、API の鍵は外した。asterism-games には何も書いていない。
- 別の参加者: `asterism-games play` (端末のクライアント) と、CRuby の小さな台本 (状態のファイルから sim の札を読んで `leave(token)` を呼び、ホストに sim を忘れさせる)。
- 誤りの答えと「ホストが答えない」の作り方: ホストのプロセスを SIGSTOP で止めると、網には残ったまま呼び出しに答えない (3 s で時間切れ)。SIGCONT で戻す。
- 前の版: HEAD の麻雀一式を `flash/app/tmp_r4b/mahjong_old/` に写し (require と APP_DIR だけ書き換え)、新しいファームで前後を順に動かした。終わってから消した。ESP32 のビルドのときは storage に入らないよう外に出した。

### 5.2 sim (Modern、麻雀は 640x360)

| 確認 | 前の版 | 新しい版 |
|---|---|---|
| 卓の一覧 (状態つき) | 出る | 同じ画面。前の版が残した席に「(自分の席あり)」(mine の探り) |
| 入る・座る・CPU で埋める・始める | 動く | Enter で席に戻って続きから |
| 打つ (東風の途中まで) | ロンの和了 (CPU)、満貫 | CPU のロン 2 回、自分のポン |
| アシストの段 (A) | - | 2・3 段で view が段つきで返る (テンパイの印、向聴の印)。1 に戻した |
| ホストを止めて戻す | 「ホストとのつながりが切れた」→ 戻ると続き | 同じ (`r4bgames is back`) |
| zenohd を止めて戻す | - | 「網から切れた: つなぎ直し中」→ 繋がり直して続き |
| 状態を消したホストに戻す | - | 一覧に戻る (`the host does not know us`)、印も消える、文は出ない (前のコードと同じ) |
| 観戦 | - | mahjong2 で CPU 4 席の観戦対局 (W)、観戦の画面、Esc で出る |
| 座る・立つ (1・S)・出る (Esc) | - | 合格 |
| 1-1 (ホストを止めて Enter、すぐ Esc) | Esc は状態の画面で効かない | 同じ。時間切れで「ホストが答えない: 待っています」、戻した後の Enter ですぐ入れた |
| 1-2 (「ホストが答えない」のあと満員の卓に Enter) | - | 「観戦の人数が上限」に置き換わる (端末のクライアントが座って始めた mahjong2) |
| Ctrl+Q で閉じる | - | `mahjong: closed` |

オセロ・ボード (426x240):

- オセロ 1-1: ホストを止めて Enter → Esc → 戻して Enter。すぐ入れた (ログの joined は 1 回だけ)。
- オセロ 1-2: 時間切れの「ホストが答えない」のあと、対局中の othello2 (spectators 0) に Enter → 「観戦の人数が上限」。
- ボード 1-1: オセロと同じ手順で、すぐ入れて絵を読んだ。
- ボード 1-2: ホストを止めて「ホストが答えない: 待っています」が出た間に、CRuby から sim の札で leave を送っておき、戻す → 次の view が誤りの答え → 「ボードから外れた」(ホストがいないの文は残らない)。
- エディタを開いて `a` を 1 打鍵: `Ln 1, Col 2 *`。

### 5.3 ブラウザ版

`rake wasm:mruby` → `wasm:mrb` → `wasm:web`。開発サーバは前から動いていたもの (8006) を使った。`?app=...&zenoh=ws/localhost:7449`。

- 麻雀 (640x360): 一覧、入る・座る・CPU・始める、東風の途中まで (CPU のロン 2 回)、牌のクリックで打牌、自分のチー (C)、ホストを止めると「ホストとのつながりが切れた」、戻すと続き。
- オセロ (426x240): 1-1 (止めて Enter → Esc → 戻して Enter で入れた)、1-2 (「ホストが答えない」のあと終局で満員の othello2 に Enter → 「観戦の人数が上限」)。
- 前の版の麻雀はブラウザ版では動かしていない (束は git の追跡分だけで、一時の写しは入らない)。前後の比較は sim で行った。ボードはブラウザ版では動かしていない。

### 5.4 試験

- `ruby test/asterism/table_run.rb`: 104 項目すべて通る。
- `rake mahjong:overflow`: 検査の道具 (`tools/mahjong_overflow/check.rb`) がアプリのインスタンス変数 (`@net`・`@table`・`@tables`…) を直接置いていたので、画面が読む口だけを持つ偽の table client を置く形に直した。0 failed / 55 warnings で、HEAD の麻雀と HEAD の道具の組と同じ。
- `rake test`: 全部通る (golden 103 passed ほか)。

## 6. RAM と VM プール

`python -m esp_idf_size build/fmruby-core.map`。前 = HEAD の table_client.rb、後 = 作業ツリー。どちらも `rake clean_all` → `FMRB_HW_TARGET=<機種> rake build:esp32`。

| 端末 | DIRAM 前 | DIRAM 後 | 像の大きさ 前 → 後 |
|---|---|---|---|
| Tab5 (P4) | 129,698 | 129,698 | 6,685,476 → 6,685,668 (+192) |
| StickS3 | 114,863 | 114,863 | 4,527,891 → 4,528,063 (+172) |

- DIRAM の値は R4a の「後」と同じ。

VM プール (麻雀は large_memory。30 秒ごとのログ、sim の Modern):

| | 前の版 | 新しい版 |
|---|---|---|
| 一覧〜対局中 | 876-893 KB (28-29%) | 859-890 KB (27-28%) |

- オセロ 502 KB (32%)、ボード 536-612 KB (34-39%) で、R4a の幅と同じ。

## 7. 振る舞いの小さな違い (載せ替えによる)

- 接続のログが Link の形になった (`mahjong: linux/game connected (...)`、`connect: ...`・`lost: ...` は warn)。接続が 1 更新遅れる (:connecting を挟む)。R4a のオセロ・ボードと同じ種類の違い。
- view が答えないときのログが `mahjong: view: no answer` (前は例外の型名)。
- アシストの段を変えたとき、待っている view があれば捨ててすぐ聞く (前は答えを待ってから聞き直していた)。
- join の拒否で、Hash に `error` も `token` も無い答えのときの文が空になる (前は Hash をそのまま文にした)。今のホストはこの形を返さない。

## 8. 残件

- 実機 (Tab5・StickS3) では動かしていない (指示どおり)。gem を変えたので、実機で確認するには焼き直しが要る。Tab5 では麻雀の入る・打つ・ホストを止めて戻す・Esc の 1-1、StickS3 では gem を持つ端末として起動の確認。Retro の ESP32 のビルドは Asterism を持たないので卓のアプリは動かない (前と同じ)。
- 捨てた join の答えでホストに残る会員 (3.1)。ほかの卓に行ったときは離席として扱われ、やがてホストが忘れる。気になるなら、捨てた答えが札なら leave を送る形にできる (振る舞いの変更になるので別に決める)。
- オセロで view の答えからホストの戻りを知ったときに「ホストとのつながりが切れた」の文が残る (R4a 4 章のまま。誤りでない答えのときの扱いは今回の決定の外)。
- R5 (画面の小物)。

## 9. コミット (fmruby-core develop、push していない)

- `35382529` asterism: TableClient drops a pending join on leave, any view answer clears host gone, and mahjong's hooks
- `dbfd5115` mahjong: the table app on FmrbAsterism::TableClient

## 10. 後片付け

- 手元のゲームホスト・端末のクライアント (TERM)、自分が起こしたブラウザ (web_down)、sim (音を戻して sim_down。zenohd も一緒に下りた) を止めた。開発サーバ (8006) は前から動いていたので残した。
- `flash/app/tmp_r4b/` (前の版の写し) は消した。
- build/ は最後に Linux (TAB5、x86-64) で作り直した。TAB5 の esp32 ビルドは置き換わっているので、焼く前に `rake clean_all && rake build:esp32` が要る。ブラウザ版の束は新しいもの。
