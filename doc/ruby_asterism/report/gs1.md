# GS1 報告: asterism-games の枠組みを切り出し、麻雀を載せ直す

> 状態: 完了 (検収待ち) | 更新: 2026-10-17 | instruction_gs1.md の結果。ゲームに依らない部分を枠組み (`lib/asterism_games.rb` と `lib/asterism_games/*.rb`) に分け、麻雀をその上に載せ直した。ホストは Asterism の Net (`Asterism.connect { |net| }`) と `caller: :from` で書き直した。見た目・動き・呼び方・1 通の大きさは変えていない。プロフィールに卓の数を載せる件は asterism の API が受け付けないので入れていない (4 章)

## 1. コミット

| リポジトリ | ブランチ | コミット | 中身 |
|---|---|---|---|
| asterism-games | main | `c733ba2` games 0.6.0 | 枠組みと麻雀の分離、Net と `caller: :from`、枠組みの試験 (試しのゲーム tally)、README |
| fmruby-core | develop | この報告と同じコミット | この報告だけ (コードは変えていない) |

push・タグはしていない。VERSION は 0.6.0 に上げた (image の版と `GET /status` の `version` に出る。戻すなら 1 行)。

## 2. 途中で受けた決定 (2026-10-17 ユーザ、親から)

開発中なので、古い卓のアプリとの互換は要らない。今の fmruby-core develop の卓のアプリ (とそこから作ったブラウザ版) が新しいホストで動けば、手間のかかる古い呼び方・古い見え方の鍵は落としてよい。止まる条件「古い卓のアプリとの互換が保てない」は外れた。

GS1 では何も落としていない。今の作りでは、互換のための部分が枠組みの形を複雑にしていなかったため。落とせる候補は 6 章に挙げる (どれも卓のアプリを変えずに落とせるかは個別に確かめが要る)。

## 3. 枠組みの形

### 3.1 ファイルと依存の向き

```
 bin/asterism-games ──> asterism_games/mahjong ──> riichi_engine (fork)
        │                       │  Table < Game, Remote < Remote
        └──────> asterism_games <┘
                 (version, game, remote, store, config, host, rooms) ──> asterism
```

| ファイル | 枠組み / ゲーム | 中身 |
|---|---|---|
| `lib/asterism_games.rb` | 枠組み | 枠組みの入口。ゲームは読まない |
| `lib/asterism_games/game.rb` | 枠組み | ゲームの基底 `Game`: 登録 (`register`)、既定の卓 (`tables`)、部屋で卓を出すアプリ (`screens`)、設定の鍵 (`settings`)、会員と token (`join` / `mine` / `leave` / `node_of` / `touch` / `member_away?` / `expire_members`)、部屋の守り (席の ID・ホストの名前を信じる、旧ページの秘密、席の渡し直し)、見え方 (`public_view` / `spectator_view` / `state_view`)、`tick`、保存 (`changed` / `dirty?` / `unsaved?` / `save` / `to_saved` / `restore` / `resume_clock` / `SAVE_FORMAT`)、見張りの数 (`summary`) |
| `lib/asterism_games/remote.rb` | 枠組み | 出すオブジェクトの基底 `Remote`: `exposes` (メソッドと引数の数)、`from:` (呼び手)、`join` / `mine` / `leave`、`mark(token, from)` |
| `lib/asterism_games/host.rb` | 枠組み | Net での接続・expose・受け手、自分の輪 (`step`)、状態の発行、状態のファイル、1 通の大きさの記録 |
| `lib/asterism_games/rooms.rb` | 枠組み | 部屋の監督、常設の卓、`GET /status` (中身は前と同じ。コメントから麻雀の名前を外しただけ) |
| `lib/asterism_games/config.rb` | 枠組み | 設定。卓の鍵・既定の卓・部屋のアプリの既定を、登録されたゲームから取る |
| `lib/asterism_games/store.rb` | 枠組み | 変えていない |
| `lib/asterism_games/mahjong.rb` | ゲーム | 麻雀の入口。読むと `mahjong` が登録される |
| `lib/asterism_games/mahjong/table.rb` | ゲーム | `Table < Game`。席・局・ルール・持ち時間・見え方。会員と部屋の守りの部分を基底へ移した (289 行の差分、-176 行) |
| `lib/asterism_games/mahjong/remote.rb` | ゲーム | `Remote < AsterismGames::Remote` |
| `test/framework/tally.rb` | 試験 | 枠組みだけで書いた小さなゲーム (約 120 行) |

ゲームが基底に渡す口 (麻雀が使っているもの): `member_reply` (join の答えに席と卓の主)、`join_refusal` (観戦の上限)、`renamed` (席の名前も変える)、`forget` (抜けた会員の席を CPU に、卓の主を渡す)、`keep_member?` (席にいる会員は消さない)。

### 3.2 依存していないことの示し方

`test/test_framework.rb`:

- 枠組みの 8 ファイルの require に `riichi` / `mahjong` が無く、本文に `Mahjong` / `RiichiEngine` が出ず、`require_relative` の先が枠組みのファイルだけであること。
- 別のプロセスで `require "asterism_games"` だけをしたとき、読まれる lib の下のファイルがちょうど枠組みの 8 つで、登録されたゲームが 0、riichi を含む読み込みが 0。
- 別のプロセスで `test/framework/test_tally.rb` (9 件) を動かす。麻雀も riichi_engine も読まれていないことをその中でも確かめ、tally の卓で: 出すメソッドと引数の数、ゲームからの設定、`from:` 付きの会員と token、保存と別のホストでの再開 (同じ token、形式の違う保存は読まない)、状態のファイル (数だけ)、部屋 (席の ID・ホストの名前・旧ページの秘密・知らない ID の断り) と常設の卓 (子のホストが書いた数が `GET /status` に出る、部屋を閉じると部屋の保存が消え、常設の保存は残る)、ルータ越し (呼び出し、`from:` を自分で渡す呼び出しの断り、状態の put、プロフィール)。

## 4. 書き方の変更 (前と後)

### 4.1 呼び手: `Asterism.current_call` から `from:` へ

前 (mahjong/remote.rb):

```ruby
def caller_node
  c = ::Asterism.current_call
  c ? c.from.to_s : ""
end

def join(name, secret = nil)
  @table.join(caller_node, name.to_s, @clock.call, secret)
end

def sit(token, seat)
  mark(token)
  @table.sit(token, seat.to_i, @clock.call)
end
```

後 (remote.rb と mahjong/remote.rb):

```ruby
class Remote                                   # the framework's
  exposes join: -1, mine: -1, leave: 1
  def join(name, secret = nil, from: nil)
    @game.join(from.to_s, name.to_s, now, secret)
  end
end

class Mahjong::Remote < AsterismGames::Remote
  METHODS = exposes(join: -1, mine: -1, sit: 2, stand: 1, cpu: 3, start: 1, rule: 3, view: -1, act: 4, table: 0,
                    leave: 1)
  def sit(token, seat, from: nil)
    mark(token, from)
    @game.sit(token, seat.to_i, now)
  end
end
```

ホストは `net.expose(name, remote, methods: Remote.exposes, caller: :from)`。引数の数の表は前と同じ値・同じ順 (console の meta の並びも同じ)。`from:` を自分で渡す呼び出しは Asterism が断る (`RemoteError`、`remote_kind` `arguments`。ルータ越しの試験で確認)。大きさの記録 (`SizeWatch`) も `current_call` を読まず、`from:` と卓の名前から鍵の長さを出す。

### 4.2 接続: Net と自分の輪

前 (host.rb):

```ruby
::Asterism.connect(locator, **opts)
::Asterism.profile(name: "asterism-games") unless @config["namespace"]
@games.each_key { |name| ::Asterism.expose(name, @remotes[name], methods: @methods[name]) }
@session = ::Asterism.session
# serve
until @stop
  unless ::Asterism.poll
    @log.call("games: connection lost: #{::Asterism.lost_reason}")
    return
  end
  step
  sleep LOOP_S
end
```

後:

```ruby
::Asterism.connect(locator, **connect_options) do |net|
  net.profile(name: "asterism-games") unless @config["namespace"]
  net.on_error { |e, where| @log.call("games: #{where}: #{e.class}: #{e.message}") }
  @games.each_key { |name| net.expose(name, @remotes[name], methods: @methods[name], caller: :from) }
  net.start                                    # the calls on the Net's receiving thread
  until @stop
    unless net.running?
      @log.call("games: connection lost: #{net.lost_reason}")
      break
    end
    ::Asterism::LOCK.synchronize { step }       # tick, save, put: the host's own loop
    sleep LOOP_S
  end
end
```

- 呼び出しは Net の受け手のスレッドで答える。ゲームの進行 (時間切れ・CPU・保存・状態の発行) は自分の輪 (10 ms ごと) に残した。
- 自分の輪は `Asterism::LOCK` を取って `step` する。受け手のスレッドは 1 回の tick ごとに同じ錠を持つので、ゲームの中で呼び出しと進行が重ならない (ゲームは錠を持たなくてよい)。ゲーム用に別の錠を作ると、輪の中の `Asterism.reconnecting?` (LOCK を取る) と受け手 (LOCK を持ったまま卓の錠を待つ) とで順序が逆になり詰まるので、gem の錠に揃えた。
- 状態の発行は `net.session.put`。
- つなぎ直し: ルータのクライアントは zenoh が自分でつなぎ直す間 `reconnecting?` で、発行を止める (前と同じ)。受け手のスレッドが終わったとき (接続を失った) は block を抜けて閉じ、3 秒後につなぎ直す (前と同じ)。deploy/local で zenohd を `docker restart` したとき、ホストは接続を失わず (ログに lost なし、状態は up・connected)、その後の呼び出しも通った。

### 4.3 プロフィール: 卓の数は載せていない (asterism の API が足りない)

`Asterism.profile` が受け付けるのは `kind` / `name` / `host` / `icon` だけで、ほかの鍵は `ArgumentError` (`unknown profile field ... (kind, name, host, icon)`、asterism 0.7.1 の mrblib/profile.rb。asterism の main c04faf0 も同じ)。README にも「アプリのプロフィール (`asterism/<node>/<app>/@profile`) は not yet」とある。指示書の止まる条件「asterism の API が足りない」にあたるので、プロフィールは前と同じ `name: asterism-games` (部屋の中では出さない) のまま、Net の `net.profile` に書き換えただけにした。`name` に数を詰める形は表示名の流用で、変わるたびに `@profile` の put が飛ぶので採らなかった。

GS2 以降に要るもの: asterism にアプリ単位のプロフィール (自由な数の欄) を足すか、数を出す別の決まり (例: `asterism/<node>/<app>/@status`) を決めるか。どちらも仕様の選択なのでユーザに返す。

## 5. 受け入れ条件ごとの結果

### 5.1 試験

| | 件数 | 結果 |
|---|---|---|
| `MJ_GAMES=8 bundle exec rake test` (前 6644cf5) | 141 | 0 failures、skip 1 (ネットワーク)、22 秒 |
| 同 (後 c733ba2) | 145 (+4: test_framework.rb) | 0 failures、skip 1、24 秒。その中の別プロセスで tally 9 件 (skip 1) |
| `ASTERISM_TEST_ROUTER=tcp/127.0.0.1:17447` で test_network.rb | 1 | 合格 (19 assertions。`from:` を渡す呼び出しと引数の数の断りを足した) |
| 同じルータで test/framework/test_tally.rb | 9 | 合格 (ルータ越しの 1 件を含む) |

既存の試験で変えたのは 1 か所: test_mahjong_room_identity.rb の `Asterism.stub(:current_call, ...)` を `from:` で渡す形に (中身の確かめは同じ)。

気づき: 試験の中で、親のプロセスが zenoh-c を使った後に fork したホストは connect の futex で止まる (スレッド 1 本、`futex_do_wait`)。asterism の README の「fork の前に開いた session は子で使えない」より強く、閉じた後でも起きる。ネットワークの試験はホストを最初の接続より前に 1 回だけ fork する形 (1 件にまとめる) にした。

### 5.2 呼び出しの互換 (今の卓のアプリ・今の呼び方)

| 卓 | 客 | 結果 |
|---|---|---|
| 家の LAN の卓 (zenohd 1.10.1、tcp/7447 と ws/7449、ホストは平文) | ブラウザ版 (fmruby-core develop の `rake wasm:web` 済みの束、22:23 のビルド。web_up、`?zenoh=ws/localhost:17449`) の卓のアプリ | 合格。一覧に `games/mahjong1`・`games/mahjong2`、Enter で入る (観戦)、席 1 に座る (卓の主)、ルールの半荘→東風、C で CPU 3 つ、始める、打牌 3 回 (応答 62-63 ms) |
| 常設の卓 (asterism-console deploy/local 41eafac、zenohd mTLS、asterism-games の image を compose で) | `bin/mahjong-bots --credentials player-a.tar --node player-a` と player-b (行 player-a/b、役割 games) + CPU 2 | 合格。東風戦を最後まで (170・183 手、前の image でも同じ形で合格) |
| 部屋の卓 (同じ構成、部屋 01、席 16、rooms.json の hosts に games) | 席の証明書の客: door で席を取り (POST /room/seat、`wasm-0105`)、door の WebSocket を通る CRuby (G2b の zenoh-c の TCP を WebSocket に包む橋) が卓のアプリと同じ形で呼ぶ: `join(name)` (秘密なし)、`mine`、`sit`、`rule`、`cpu`、`start`、`view(token)` / `view(token, 3)`、`act`、`leave` | 11 / 11 合格。打牌 3 回、view の答え 6-13 ms、手牌は自分の view だけ、`table` には無い |
| 部屋の卓 | `bin/mahjong-bots --credentials games.tar --namespace room/01` (3 人 + CPU) | 合格。東風戦を最後まで |

### 5.3 1 通の最大とメモリ

1 通の最大 (鍵と中身。決まった形で作る最悪の view は試験の数字):

| | 前 | 後 |
|---|---|---|
| 最悪の view (試験) | 2120 B | 2120 B |
| 最悪の観戦の view (試験) | 2170 B | 2170 B |
| 最悪のリーチの view / 受け入れの view (試験) | 1511 B / 2113 B | 1511 B / 2113 B |
| 平文 zenohd、ボット 3 + CPU 1 + 観戦 1 (assist 3) の東風戦、3 回 | 560 / 630 / 545 B | 515 / 522 / 530 B |
| deploy/local、常設の卓 (ボット 2 + CPU 2) | 498 B | 514 B |
| deploy/local、部屋の卓 | 528 B (ボット 3) | 685 B (ボット 3 と席の客。席の客が `view(token, 3)` を呼ぶ分、受け入れの `uk` が載る) |

対局は乱数で毎回違うので、実戦の数字は前後とも 500-700 B の幅に入っていることを見る (4096 B の上限から十分遠い)。形が同じ最悪の場合は前後で同じ。

メモリ (RSS):

| | 前 | 後 |
|---|---|---|
| 平文 zenohd のホスト 1 プロセス (卓 1、東風戦中の最大、3 回) | 58.5 / 58.3 / 58.4 MB | 60.0 / 59.8 / 59.6 MB |
| deploy/local の image: 監督 | 42.4 MB | 43.3 MB |
| 同: 部屋 01 のホスト (VmHWM) | 50.8 MB | 51.4 MB |
| 同: 常設の卓のホスト (VmHWM) | 52.3 MB | 51.2 MB |
| 同: `GET /status` の `rss_kb` (3 プロセスの和、対局中の最大) | 144.0 MB | 145.9 MB |
| 同: container (docker stats) | 71.8 MiB | 67.7 MiB |

手元 (MALLOC_ARENA_MAX の指定なし) では受け手のスレッドの分 1.5 MB ほど増えた。image (`MALLOC_ARENA_MAX=2`) では前後の差は ±1 MB の中で、container 全体はむしろ小さかった。

### 5.4 枠組みの独立

3.2 のとおり。`test_framework.rb` の 4 件と、別プロセスの tally 9 件が合格。

### 5.5 ボットと VPS の一式

- `bin/mahjong-bots`: 平文・常設 (自分の証明書)・部屋 (ホストの証明書と namespace) の 3 つの形で、東風戦を最後まで (5.2)。bots は `Table.room_node_id` を基底から受け継いで今どおり使える。
- `deploy/vps/`: `docker compose -p asterism-games-rehearsal build` で image (`asterism-games:c733ba2`) を作り、`ASTERISM_PROJECT=asterism-rehearsal` で起こした。8 秒で healthy (healthcheck の `bin/asterism-games status`)。部屋 01 と常設の卓 2 つが上がり、止めて起こし直すと 3 つの卓が保存から戻った (`resumed`)。`status` は `asterism-games 0.6.0 ... RSS 146.0 MB` と卓ごとの数を出した。`update.sh` は VPS 用 (git の取得と project `asterism-games`) なので手元では動かしていない。

## 6. GS2 (オセロ) に向けて分かったこと

- 新しいゲームが書くもの: `Remote` の子 (出すメソッドと `from:`)、`Game` の子 (`register` / `remote` / `tables` / `screens` / `settings` / `DEFAULTS`、`tick`、見え方、`to_saved` / `restore` / `resume_clock`、`summary`)。tally は約 120 行で部屋・常設・保存・見張りまで動いた。オセロのホスト版もこの形で数百行に収まる見込み。
- 席の管理 (座る・立つ・卓の主・CPU) は麻雀の Table に残した。オセロも 2 席と卓の主を持つので、GS2 で 2 つ目が出たところで基底 (例: `Seats`) に上げるかを決めるのがよい (今は 1 つしか使い手がいない)。
- 部屋の設定は「アプリの一覧 (`rooms.apps`)」と「卓の一覧 (`rooms.tables`)」が 1 組だけ。オセロのアプリを開く部屋にオセロの卓を出すには、アプリごとに卓を選ぶ形 (例: `screens` からゲームを引き、そのゲームの `tables` の最初を出す) が要る。今は既定の apps が全ゲームの screens、既定の tables が最初に登録したゲームの最初の卓なので、2 つ目のゲームを足すと部屋の卓が合わない。GS2 の最初に決める。
- `summary` の鍵 (`st` / `members` / `humans` / `cpus` / `away`) は `bin/asterism-games status` と console の rooms の画面が読む。新しいゲームもこの鍵を出す。
- ゲームの錠: 呼び出しと進行は `Asterism::LOCK` で直列なので、ゲームの中に錠は要らない。ゲームが自分のスレッドを持つときだけ考える。

落とせる互換の候補 (2 章の決定による。GS1 では落としていない):

| 候補 | 中身 | 卓のアプリへの影響 |
|---|---|---|
| `view` の引数 1 つの形 | assist なしの呼び出しを 1 とみなす | 今のアプリは assist が既定のときは 1 つで呼ぶ (mahjong.app.rb 546 行)。落とすならアプリを 2 引数に揃える (1 行) |
| `join` / `mine` の秘密と `rooms.secret_ids` | 席より前のブラウザ版のページ | 今のアプリは秘密を付けない (g2b で外した)。ホスト側の分岐と試験を消せる |
| `ru` / `rx` / `ry` の分け方と `RULE_TURNS` | 項目の数が少ない古いアプリ向け | 今のアプリが読む鍵を確かめてから |

## 7. 残ったこと

- 4.3 のプロフィールの数 (asterism の API か決まりが要る)。
- 6 章の部屋の卓の選び方 (GS2 の最初)。
- 席の証明書のブラウザ版のページでの部屋の卓 (Playwright) は今回はしていない。door と席の証明書の道は CRuby の客で通した (呼び方はアプリと同じ)。
- 後片付け: deploy/local (`VOLUMES=1 rehearse down`、`.env` と `certs/` も消した)、asterism-games の rehearsal の project (`down -v`、`.env` と `credentials/games.tar` を消した)、手元の zenohd 2 つ (`gs1-zenohd`・`gs1-zenohd-ws`)、作った image (`asterism-games:c733ba2`・`asterism-console-rehearsal:0.3.0`)、家の LAN の卓のホスト、web_* の開発サーバとブラウザを止めて消した。もともとあった image (`asterism-games:6644cf5` ほか) は残した。docker の prune はしていない。fmruby-core は develop のまま、build/ は触っていない。
