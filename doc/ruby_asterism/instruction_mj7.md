# MJ7 指示書: 牌譜を SQLite に入れ、成績を集計する

> 状態: 指示 | 更新: 2026-10-18 | ゲームホストが終局した牌譜を SQLite に保存し (対局中の卓の保存は JSON のまま)、人ごと・CPU / Claude / Jev ごとの成績を集計して公開メソッドで出す。卓の麻雀のアプリに成績の画面を足す

前提: report/mj6.md (牌譜の形、`records` / `record`、保存の場所、@status の last)、asterism-games の app/asterism_games/mahjong/records.rb・table.rb・remote.rb、枠組み (lib/asterism_games/ の store.rb・rooms.rb の部屋の保存と片付け)、deploy/vps (compose、update.sh、Dockerfile、console のバックアップがゲームホストの volume を取り込む形 docker-compose.games.yml)、卓の麻雀のアプリ (fmruby-core flash/app/game/mahjong/ の mahjong_replay.rb)。

言葉: 文書は常体、「確かめ」ではなく「確認」。asterism-games と fmruby-core のコードのコメントは英語。

## 1. 決まっていること (2026-10-18 ユーザ、選択形式)

1. **牌譜だけを SQLite に**: 対局中の卓の保存は今の JSON のまま。終局した牌譜を SQLite に 1 回書く。
2. **残す数は卓ごとに直近 1000 対局** (設定で変えられる)。
3. **部屋の牌譜は、部屋を閉じても集計のために残す**。卓のアプリの「直近の結果」には、その部屋が開いている間だけ、その部屋の対局を出す (部屋の外の人に部屋の牌譜を見せない)。
4. **集計は公開メソッドと、卓のアプリの成績の画面**。CLI・MCP からも読める。
5. **人は表示名でまとめる** (卓で入れた表示名が同じなら同じ人。名前を変えると別の人、同じ名前を名乗れることは許す)。CPU・Claude・Jev は種類ごとにまとめる。

## 2. 作るもの

1. **SQLite の保存** (asterism-games):
   - 置き場所はゲームホストの状態の volume (例 `state_dir/records.sqlite3`)。WAL の形式で、複数のプロセス (監督・部屋ごとのホスト・常設のホスト) が終局のときだけ書く。書くときの錠の待ち (busy timeout) を設ける。
   - 表の形は任せる (例: 対局の表 (id・日時・卓・部屋の印・ルール・種類 東風/半荘・終わり方)、席ごとの結果の表 (対局・席・表示名・種類 human/cpu/claude/jev・順位・点数・和了の回数・放銃の回数・リーチの回数・局数)、牌譜そのもの (JSON の文字列、今の `record` の形))。集計に要る数は終局のときに牌譜から数えて席ごとの表に入れる。
   - 卓ごとに直近 1000 を超えたら古いものから消す。
   - 今ある JSON の牌譜 (`<卓>.records.json`) は、最初に起きたときに SQLite へ取り込み、取り込んだら JSON の牌譜は使わない (消すか、取り込み済みの印を付ける。どちらにしたかを report に)。
   - `sqlite3` gem は image と開発の Gemfile に入れ、**枠組みの gem の依存には入れない**。sqlite3 が無い環境 (gem だけで入れた人) では今の JSON の保存 (直近 20) のまま動く。
2. **公開メソッド**:
   - `records` / `record` は今の形のまま、SQLite から読む。部屋の中のホストは、その部屋の対局だけを出す。常設・家の LAN の卓は、その卓の対局。
   - 新しく `stats` (引数は任せる。例: 卓・期間 (直近 N 対局か日数)・種類 東風/半荘)。答え: 人ごと・CPU/Claude/Jev ごとに、対局数・平均順位・順位ごとの回数 (1 位率など)・和了率・放銃率・リーチ率・平均の点数 (と順位点を数えていればその合計)。1 通の大きさ (4096 B) に収まらなければ頁に分ける。meta に args / doc。
   - 部屋の中から呼ばれた `stats` が何を集計するか (部屋の対局だけか、全体か) は、3 の「部屋の外の人に部屋の牌譜を見せない」と矛盾しない範囲で決め、report に書く (名前と数だけの集計は見せてよい、が既定の見立て。牌譜そのものは見せない)。
3. **バックアップ**: console の毎日のバックアップがゲームホストの volume を取り込むとき、使用中の SQLite のファイルを写して不整合にならないようにする (例: 終局で書いたあとに `VACUUM INTO` で一貫した写しを作り、バックアップはその写しを取る。console の変更が要るなら docs だけで済むかを確認し、コードの変更が要るなら止まる)。戻し方 (リストアで SQLite が戻ること) も確認する。
4. **卓の麻雀のアプリ**: 「直近の結果」の画面から開ける「成績」の画面 (人ごと・種類ごとの表。期間の切り替えは 1 つあればよい)。振り返りと同じく、開いたときだけ読む別のファイルにする。1280x720・640x360 (426x240 は落ちない程度)。
5. **試験**: SQLite への保存と読み出し、1000 の上限、JSON からの取り込み、複数のプロセスが同時に終局しても欠けない、部屋を閉じても残り部屋の外には牌譜を出さない、`stats` の数が牌譜から数えた値と合う、sqlite3 が無いときに JSON のまま動く。
6. **文書**: README (SQLite、stats、バックアップ)、docs/install.md (更新の注意: 取り込みが走る)、CHANGELOG。VERSION は 0.14.0。

## 3. 受け入れ条件

1. `MJ_GAMES=8 bundle exec rake test` (足した試験も) と `rake test:gem` (sqlite3 なしの道) が通る。fmruby-core の `rake mahjong:overflow` ではみ出し 0。
2. 家の LAN の卓で、CPU と偽物の AI (`ASTERISM_GAMES_AI_FAKE=1`) を入れた東風戦を何回か (ボットや客の台本でよい) 打ち、成績の画面に人ごと・種類ごとの数が出る (写す)。CLI (`asterism call ... stats`) と MCP で同じ数が読める。
3. 部屋 (console の deploy/local) で 1 局打って部屋を閉じ、SQLite に残っていること、ほかの部屋や常設の卓の「直近の結果」に出ないこと。
4. バックアップとリストアで SQLite が戻ること (deploy/local)。
5. 0.13.0 の JSON の牌譜がある状態から 0.14.0 に上げると、取り込まれて「直近の結果」に同じ対局が出ること。
6. 数字: 1 対局の SQLite の増え方、1000 対局のときのファイルの大きさの見積もり、`stats` の答えの時間と大きさ、ゲームホストの RSS、卓のアプリの VM (成績の画面を開いたとき)。
7. sim の標準構成で、エディタを 1 回起動して 1 打鍵する。

## 4. 範囲・決まり

- **触ってよいもの**: asterism-games (main)、fmruby-core はブランチ `feature/mj7` (develop から) で卓の麻雀のアプリと report。asterism-console は docs だけ (コードの変更が要るなら止まる)。asterism は読むだけ。
- **触らないもの**: riichi_engine の Fork、zenohd、VPS、実機の端末、sdkconfig。
- コミットのみ。push・タグはしない。`git commit -a` は使わない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。fmruby-core のコミットの件名は `<領域>: <要約>`、本文は英文。鍵と実在の値の検査をしてからコミットする。
- fmruby-core のビルドは本体の checkout で。ブラウザ版は `source ~/emsdk/emsdk_env.sh` のあと `rake spinel:gen wasm:mruby wasm:mrb wasm:web` で作り直す。終わったら build/ は Linux の標準構成 (x86-64)、`file build/fmruby-core.elf` で確認する。
- 終わったら起こしたもの (zenohd、ゲームホスト、deploy/local、試しの image と volume、sim・web) を名前で止めて消す。家の LAN の zenohd は名前に `mj7` を入れ、ポートは sim の 7447 とぶつけない。sim は音を出す前にミュート。ゲームホストの状態の置き場所は作業用の場所に (`var/state` を汚さない)。API の鍵は使わない。
- **止まる条件**: console のコードの変更が要る。1 章に無い仕様の選択 (利用者から見た動き・既定値) が要る。止まったら report に、ユーザが選べる形の選択肢 (推奨を先頭に、各選択肢で何が変わるか一言) を書いて返す。
- **report** (`doc/ruby_asterism/report/mj7.md`、常体): 表の形、取り込み、バックアップの形、stats の形と数え方、部屋の中の stats の範囲、受け入れ条件ごとの結果と数字と画面、見立てと違った点、残ったこと。
