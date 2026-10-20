# GS14 指示書: プレーヤーに見えない「観戦用の様子」(手牌入り)

> 状態: 指示 | 更新: 2026-10-20 | ゲームホストが、プレーヤーには読めないキーに手牌入りの卓の様子を流し、管理者・観戦者 (と見るだけの MCP) が読めるようにする。常設の卓と Room の卓の両方

前提:
- asterism-games main (0.17.1、f699f98、rubygems 公開済み)。枠組み `lib/asterism_games/{game,host,remote,rooms}.rb`、麻雀の卓 `app/asterism_games/mahjong/table.rb` (今の観戦: 観戦者として入った人に 4 人の手牌を返す view、ローカルルールの観戦の遅れ。既定 0、N 分)、オセロ・お絵かきボード。卓の state は `asterism/<host>/<app>/<table>/state` に put している。
- asterism-console main (0.3.10)。ACL の作り `lib/relay/cloud_config.rb` (Room の枠 `room/<slot>/**` は席が全部読める、外へは届かない。Room のホストの規則 `room/*/asterism/<host>$*`)。役割の手順書 `docs/rooms.md` (ゲームホストの役割 `games-host`、プレーヤーの役割 `games` は `Read asterism/*/games/**`)、`docs/g1_5.md` (viewer)。
- asterism 0.10.0 (未公開、main 2a039b8) の MCP `asterism_values` と CLI `object get/echo` で、流れる値を読める。

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントと asterism-* の文書は英語。

## 1. 決まっていること (2026-10-20 ユーザ、選択形式)

1. **置き場所は Room の外**。Room の席は `room/<slot>/**` を全部読めるので、Room の中には置かない。プレーヤーの役割 (`games`: `asterism/*/games/**`) にも当たらないキーにする (例: ホストの node の下の別の app `spectate`、`asterism/<host>/spectate/<卓>/state`)。常設の卓と Room の卓の両方。
2. **載せる範囲は 4 人の手牌とツモ牌まで** (今の観戦画面と同じ範囲)。山の並び・裏ドラなど、まだ誰も知らないものは載せない。
3. **遅れは卓の観戦の遅れの設定に従う** (今のローカルルール。既定 0)。
4. **すべてのゲームに共通の仕組み**として枠組みに口を作り、麻雀がそれを使う (オセロ・お絵かきは隠すものが無いので、使わないか、使っても今の state と同じ)。
5. **役割は console の手順書に書き方を載せる** (console のコードは変えない)。ai-viewer (`read asterism/**`) はそのままで読める。

## 2. 決めごと (親が決めた形。変えるなら止まって返す)

- **枠組み**: `AsterismGames::Game` に「観戦用の様子」の口 (例: `spectate_view`、既定は nil = 流さない) を足し、ホストが卓の state と同じ契機で、決まったキーに put する。遅れがあるときは、その秒数だけ遅らせて流す (遅れの間の分をためて順に出す、または遅れた時点の様子を出す。どちらかを決めて report に。局の区切りで漏れが起きないこと)。
- **キー**: 常設の卓とRoom の卓が区別でき、Room の枠をまたいでも重ならない名前 (例: 常設 `asterism/games/spectate/mahjong1/state`、Room `asterism/games/spectate/room01-<卓>/state` など。Asterism の名前の決まり (1 chunk、使える文字) に従う)。Room の卓の分も、**Room の namespace の付かない接続**から put する (Room の中に漏らさない)。ホストがその接続・書く許可を持っているかを確認し、足りなければ `games-host` の役割に足す行を console の手順書に書く。
- **中身**: 4 人の手牌・ツモ牌・鳴き・河・点数・局など、今の観戦の view と同じ範囲。token・秘密・山・裏ドラ・未公開のドラ表示牌は入れない (試験で中身の鍵を検査する)。AI 席の考え中などの状態は入れてよい。
- **プレーヤーに見えないこと**: ACL で確認する。手元の ACL つきのルータ (asterism-console の deploy/local を image から作る、UP8 と同じ要領) で、console の画面か rake/CLI で役割と行を作り、
  - プレーヤーの証明書 (`games` の役割) では、観戦用のキーが**届かない**。
  - 観戦の役割 (新しく手順書に書く `spectator`: `read asterism/*/spectate/**` など) と viewer では**届く**。
  - Room の席の規則 (cloud_config が作る) が観戦用のキーに届かないことを、生成された ACL の中身で確認する (Room の door まで動かさなくてよい。動かせるならなおよい)。
- **今の観戦はそのまま**: 卓のメソッドで観戦者に手牌を返す今の観戦は変えない。
- 依存は増やさない。asterism の gem は 0.9.0 (公開済み) で動くこと (0.10.0 の機能に頼らない。確認には 0.10.0 の作業コピーの CLI/MCP を使ってよい)。

## 3. 作るもの

1. asterism-games: 枠組みの口、ホストの put と遅れ、麻雀の観戦用の様子、README (観戦用の様子の節: キー、中身、遅れ、役割の例、MCP での見方)、CHANGELOG **0.18.0**、VERSION、docs/install.md (更新の節: `.env` の変更の有無)。
2. asterism-games の試験: 口の既定 (流さない)、麻雀の中身 (手牌あり、山・裏ドラ・token なし)、遅れ (0 と N)、キーの名前 (常設と Room)、Room の分が namespace の外に出ること。
3. asterism-console: 手順書 (`docs/rooms.md` か新しい節) に `spectator` の役割の書き方、`games-host` に要る行 (要るなら)、MCP/CLI での見方。コードは変えない。CHANGELOG の Unreleased に文書の追記として。
4. 確認: 上の ACL の確認と、常設の卓で CPU 4 人の半荘 (か東風) を流して、観戦用の様子を viewer 相当の証明書の CLI (`object echo` / `-o json`) で読めること。写しを report に。

## 4. 受け入れ条件

1. `MJ_GAMES=4 bundle exec rake test` (asterism-games) と asterism-console の `bin/rails test` が通る (console はコードを変えないなら、ACL の試験に観戦のキーの問いを足した場合だけ)。
2. ACL の確認が通る (プレーヤーには届かない、観戦の役割と viewer には届く)。
3. 中身に山・裏ドラ・token が無い。

## 5. 範囲・決まり

- **触ってよいもの**: asterism-games (main)、asterism-console (main、docs と、要るなら ACL の試験)。fmruby-core は report だけ (ファイルとして書くだけ、git の操作はしない)。
- **触らないもの**: asterism・asterism-zenoh・riichi_engine・fmruby-core のコード、VPS、GitHub の設定、Tab5。ユーザの `~/asterism_keys/` の証明書を使わない (VPS につながない)。
- 外の AI の API を呼ばない。push・タグ・gem push はしない。`git commit -a` は使わない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。実在の値の検査をしてからコミットする。
- 起こしたもの (deploy/local、zenohd、ホスト、試しの image と volume) は名前で止めて消す。前からある image は残す。docker の prune はしない。pid で止める (`pkill -f` は使わない)。
- **止まる条件**: 1・2 章に無い仕様の選択が要る (例: ホストが Room の外に書く許可を持たず、console のコードの変更が要る)。止まったら report に、ユーザが選べる形の選択肢 (推奨を先頭に) を書いて返す。
- **report** (`doc/ruby_asterism/report/gs14.md`、常体): キーと中身、遅れの扱い、ACL の確認の写し、役割の書き方、試験の結果、VPS に出すとき要ること (console の画面での役割の作り方、`.env` の変更の有無)。
