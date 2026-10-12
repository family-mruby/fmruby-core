# G1b 報告: 接続を 1 本に (Asterism.session)、poll を軽く、自分の名前 (asterism 0.6.0)

> 状態: 完了 (検収待ち) | 更新: 2026-10-12 | instruction_g1b.md の実装と確かめの記録。asterism 0.6.0 に `Asterism.session` (CRuby は `net.session`) を足し、オセロ・実時間の測り・台車の頭脳・操縦機を接続 1 本に直した。G0 の通し稽古の構成で、1 回で使い切る合言葉の `wss://` から、オセロ (2 つのブラウザ版で対局)・実時間の測り・操縦機が入れた (2 本目で 401 になる件が消えた)。`Asterism.poll` はノード 5・オブジェクト 5 で、同じファームの上の比べで P4-Nano 14.3 ms → 0.26 ms、AtomS3R 21.7 ms → 0.50 ms。on_join / on_leave の振る舞いは 0.5.0 と同じ (試験)。台車の頭脳の ROS 2 のノードは、console の直し (同じ接続の ROS 2 のノードを端末の枠に入れる) で端末の枠の中に出た。名前はプロフィールの `name` に置き、端末は `/home/asterism_profile.toml`、オセロの待ち合わせの F2 で変えられる。相手の一覧と console の図に出て、端末の起動し直し・ブラウザ版の読み直しでも残った

## 1. 結果の要約

| リポジトリ | ブランチ | 中身 | push |
|---|---|---|---|
| asterism | feature/session-poll (main から) | `Asterism.session`、`net.session`、軽い `Asterism.poll`、0.6.0、試験 (CRuby と mruby)、`rake bench:poll`、README・CHANGELOG・docs/api_review.md | していない |
| asterism-console | feature/g1b (main から) | 同じ接続の ROS 2 のノードを端末の枠に入れる、図の名前を `名前 (ID)`、docs/g1b.md、Gemfile.lock (手元の asterism 0.6.0) | していない |
| fmruby-core | feature/asterism-g1b (develop から) | 4 つのアプリの直し、オセロの名前の欄、既定のプロフィールが名前を読む、ASTERISM_PIN、この報告・game.md・browser.md・plan.md | していない |

- asterism-zenoh・picoruby-asterism-zenoh は変えていない (0.4.0 のまま、gemspec も `~> 0.4.0` のまま)。
- タグ・gem の作成と公開はしていない。止まる条件には当たらなかった (使う人から見える名前は、指示書の
  `Asterism.session` と、それに対応する CRuby の `net.session` だけ。2 章)。
- 端末: P4-Nano と AtomS3R のアプリの区画をこのブランチのファームで焼き直した (8 章)。

## 2. Asterism.session

### 2.1 形

portable API (端末と CRuby):

```ruby
Asterism.connect(router, node: id, app: "game")
s = Asterism.session                         # Asterism::Zenoh::Session そのもの
s.put("asterism/#{id}/game/othello/state", bytes)
sub = s.subscribe("asterism/*/game/othello/state")
node = Asterism::ROS::Node.new(s, "fmruby_brain_x")   # Asterism と同じ Zenoh の ID
# 更新の 1 周: Asterism.poll (と node.poll)
```

- 返すのは包み (wrapper) ではなく、`Asterism.connect` が開いたセッションそのもの。`Asterism::ROS::Node.new`
  など、セッションを受け取るものはそのまま受け取れる。つながっている間は同じオブジェクトを返す。
- つながっていなければ `Disconnected` (0.5.0 の `session!` と同じ。`session!` は `@api private` のまま残した)。

CRuby API: `net.session` は同じセッションを `Zenoh::Connection` として返す。受信のスレッドは `Net` のもの
(`subscribe { }` のブロックは、呼び出しに答えるのと同じスレッドで、その合間に走る)。ROS 2 のノードは
`Asterism::ROS::Connection.new(net.session).node("name")`。`net.session.shared?` は true。

### 2.2 持ち主と閉じ方

- セッションは Asterism のもの。接続のときにセッションに `close` を差し替える小さなモジュールを `extend` し、
  Asterism 自身が閉じるとき (`Asterism.close`・接続が切れたとき) 以外の `close` は何もしない (1 回だけ警告)。
  セッションを渡された別のライブラリが閉じても、Asterism の接続は壊れない。
- `Asterism.close` は、セッションと、その上にアプリが作ったもの (購読・ROS 2 のノード) をまとめて閉じる。
  先に要らなくなったものは、アプリがそれぞれ閉じる (`sub.close`、`node.close`)。
- 切れたあとは、つなぎ直して `Asterism.session` を取り直し (別のオブジェクト)、購読や ROS 2 のノードを作り直す。
  4 つのアプリはそうしている (つなぎ直しは 3 秒ごと、0.5.0 のときと同じ)。
- CRuby の `net.session.close` も何もしない。`ROS::Connection.new(net.session)` の `close` は自分のノードを
  閉じるだけで、受信のスレッドもセッションも止めない (`Net` のもの)。
- 見回り: `Asterism.poll` がセッションを poll し、`node.poll` も poll する。受け取った物は購読・queryable・
  ノードごとの列に入るので、順番や回数に関係なく取り合いにならない。同じセッションの自分の `put` は、
  自分の購読には届かない (CRuby の試験で確かめた。0.5.0 の 2 本目のセッションでも同じ)。
- キー: `asterism/<ノード>/<アプリ>/**` は Asterism の queryable が答える (そこへの get は呼び出し)。put と購読は
  queryable や生存の印と交わらないので、`asterism/<ノード>/game/othello/state` のような put はそのまま使える。

### 2.3 試験

| 対象 | 中身 | 結果 |
|---|---|---|
| CRuby `test/test_session.rb` | 同じセッションで put・購読・ROS 2 のノード (zid が同じ、ROS 2 のノードの生存の印の zid も同じ)、その間も呼び出しに答える、アプリの `close` は何もしない (警告 1 回)、`Asterism.close` で閉じる、つなぎ直すと別のセッション、切れたら閉じて `Disconnected` | 通る |
| CRuby `test/test_api_session.rb` | `net.session` の購読のブロックが受信のスレッド (`asterism receiver`) で走る、`ROS::Connection.new(net.session)` のノードが `/odom` を受ける、呼び出しに答える、`ros.close` で受信が止まらない、`net.session.close` は何もしない、connect のブロックの終わりで閉じる | 通る |
| mruby `host_test/test_session.rb` + `peer.rb` (`rake test:mruby`) | 別のプロセスの相手と、同じセッションで put・購読・ROS 2 のノード、アプリの `close`、生存の監視、切れたとき | 24 項目 通る |

## 3. Asterism.poll を軽く

### 3.1 直したこと

- 来た物の列 (呼び出し・プロフィールの問い合わせ・生存の印) は、件数 (`pending`) が 0 なら取り出さない
  (0.5.0 は毎回 3 つの列から空の配列を作っていた)。
- ノードの一覧 (`nodes`) と on_join / on_leave の比べは、生存の印が届いたときだけやり直す (印を受けたら
  変わったと記す。つないだとき・切れたときも)。変化の無い周回では何もしない。
- やり直すときも `include?` の総当たりをやめ、Hash で引く。オブジェクトの印にはノードの名前を値として持ち、
  一覧のたびに `split` しない。ブロックを使わず `while` で回す。
- `Asterism.nodes` は今までどおり新しい配列を返す (中で持っている一覧の写し)。`Asterism.each` の重なりの
  除き方も Hash にした。

### 3.2 振る舞いが変わらないこと

`test/test_session.rb` の `test_join_and_leave_as_before`: 印だけのノード、オブジェクトだけのノード、同じノードの
2 つ目のオブジェクト、印が消えてもオブジェクトが残るノード (残る)、最後のオブジェクトが消えたノード (去る)、
印とオブジェクトがまとめて来たノード (1 回だけ join)、後から足したブロック、接続が切れたとき (知っていた全部が
1 回ずつ、知った順に leave)。on_join / on_leave の順・回数と `nodes` の並びを確かめる。**同じ試験を 0.5.0 の
コードに当てても通る** (0.5.0 の振る舞いを写した試験)。mruby の `host_test/test_session.rb` も同じ筋を確かめる。

### 3.3 測った数字

ノード 5 (自分を含む)・オブジェクト 5 (自分のを含む)・変化なし、1 回の `Asterism.poll`。

端末 (測りのアプリを 1 秒に 1 回、50 回続けて poll した時間を割る。30 回の中央値。ほかのノードとオブジェクトは
PC から生存の印だけを出した):

| 端末 | 0.5.0 のファーム (焼く前) | 0.6.0 のファーム + 0.5.0 の poll のコード | 0.6.0 |
|---|---|---|---|
| P4-Nano | 12.4 ms | 14.3 ms | **0.26 ms** (最大 0.62 ms) |
| AtomS3R | 20.7 ms | 21.7 ms | **0.50 ms** (最大 0.56 ms) |

- 真ん中の列は、同じファームの上で、測りのアプリが `Asterism` の poll まわりを 0.5.0 のコードに戻したもの
  (同じビルドどうしで比べるため)。目標の 1 ms 前後に届いた。
- 残りは主に zenoh-pico の見回り (`session.poll`) そのもの。変化のあった周回 (オブジェクトが 1 つ現れる) は
  一覧を作り直すが、Hash で引くので数の 2 乗にはならない。

PC (`rake bench:poll`、CRuby 3.2、1 回あたり):

| ノード | オブジェクト | 0.5.0 変化なし | 0.5.0 確保 | 0.6.0 変化なし | 0.6.0 確保 |
|---|---|---|---|---|---|
| 0 | 0 | 1.5 µs | 24 | 0.6 µs | 7 |
| 4 | 5 | 3.2 µs | 49 | 0.6 µs | 7 |
| 10 | 10 | 8.1 µs | 74 | 0.5 µs | 7 |
| 20 | 40 | 19.5 µs | 225 | 0.6 µs | 7 |

- CRuby の 7 個は、CRuby の層の錠 (`Asterism::LOCK`) の包みの分で、ノードの数によらない。
- mruby (PC の上の `rake test:mruby`): ノード 5・オブジェクト 5 で、変化なし 14-17 µs → 2.2 µs、オブジェクトが
  1 つ現れる周回 95-129 µs → 59-64 µs。

### 3.4 実時間の測り (P4-Nano ↔ AtomS3R、毎秒 25 回、60 秒)

| | 直す前 (G1 の rt_probe、0.5.0) | 直した後 (接続 1 本、0.6.0) |
|---|---|---|
| P4-Nano 送った / 更新の周回 / 1 周の平均 | 23.8 回/s / 50.2 回/s / 19.5 ms | 23.9 回/s / 55.3 回/s / 17.7 ms |
| P4-Nano 毎秒の Asterism.poll + 2 本目の poll | 67 + 21 ms (Asterism.poll は毎秒 10 回だけ) | 37 + 1 ms (毎周回) |
| AtomS3R 送った / 更新の周回 / 1 周の平均 | 22.5 回/s / 28.5 回/s / 34.7 ms | 22.5 回/s / 29.3 回/s / 33.8 ms |
| AtomS3R 毎秒の Asterism.poll + 2 本目の poll | 65 + 21 ms | 26 + 3 ms |
| 往復の中央値 / 99 % / 最大 | 79 / 206-442 / 675-766 ms | 75-77 / 173-229 / 343-423 ms |
| 落ちた・入れ替わった | 0・0 | 0・0 |

- 直した後の rt_probe は `Asterism.poll` を毎周回呼ぶ (0.5.0 では重いので毎秒 10 回に間引いていた)。それでも
  見回りにかかる時間の合計は半分以下になった。
- 送る回数は変わらない。P4-Nano はもともと 25 回に近く、AtomS3R の 1 周は受けて解く (毎秒 93 ms) と組んで送る
  (182 ms) と描く (4.5 ms/回) が大半で、見回りの分は小さかった。AtomS3R が 25 回に届かないのは見回りのせいでは
  ない。

## 4. アプリの直し

| アプリ | 前 | 後 |
|---|---|---|
| othello | Asterism + 状態の put 用のセッション | `@zs = Asterism.session`。切れたときは閉じずに捨てる。AtomS3R の観戦の購読も同じセッション |
| rt_probe | Asterism + 流れのセッション (毎周回 poll)、Asterism.poll は毎秒 10 回 | 流れも `Asterism.session`、`Asterism.poll` を毎周回 (流れも一緒に見回る) |
| rover_brain | Asterism + ROS 2 用のセッション | ROS 2 のノードとログの put を `Asterism.session` に |
| rover_pilot | Asterism + ROS 2 用のセッション | ROS 2 のノード (カメラ・`/odom`) を `Asterism.session` に |

確かめ:

- sim の othello (G1b) と P4-Nano の othello (G1、焼く前) で対局した (手は互いに通る。呼び出しの形は変えていない)。
  PC で `asterism/*/game/othello/state` を購読すると、両方の状態が届いた。対局中に zenohd を起こし直すと、sim は
  3 秒でつなぎ直し、「相手が戻った」で続いた。アプリの `close` の警告は出ていない (アプリは閉じていない)。
- 台車の頭脳: sim と P4-Nano で、PC から `go(0.3)`・`turn(90)` が終わった (誤差は S4 と同じくらい: 0.294 m、
  88.7 度)。ブラウザ版の操縦機 (家の中の `ws://`) から P4-Nano の頭脳を見つけ、カメラ (4.4-5.0 Hz、遅れ 128-178 ms) と
  `t` の旋回 (88.2 度) が通った。
- AtomS3R の観戦: P4-Nano とブラウザ版の対局の盤が映った (`/fb`)。

## 5. console の図: ROS 2 のノードが端末の枠の中に

- S4 の見立て (「セッションを 1 本にすれば同じ zid になり、図で端末の枠に入る」) は半分だけ正しかった。1 本に
  すると同じ zid にはなったが、console の図は Asterism のノードと ROS 2 のノードを別々にルータへつなぐだけで、
  枠に入れる決まりが無かった。
- console を直した (`lib/bridge/connections.rb`): Asterism のノードを 1 つだけ運ぶセッションが ROS 2 のノードも運ぶとき、
  その ROS 2 のノードを Asterism のノードの子にする (data `in_node`)。ルータへの線は Asterism のノードの 1 本だけ。
  機械ごとのまとまり (`Bridge::Groups`) からは外す。トピックの矢印はそのまま。記録の巻き戻しも同じ導き方を使う。
- sim の頭脳と P4-Nano の頭脳で、図の端末の枠の中に `/fmruby_brain_<ID>` が入り、MuJoCo の台車 (まとまり) との
  間に `/cmd_vel`・`/odom` の矢印が出た。
- 試験を 1 つ足した (枠に入る、線が無い、まとまりに入らない、ROS 2 だけのプロセスは外のまま、導き直しても同じ)。
  console の試験 276 本 2291 項目 失敗 0、rubocop 指摘 0。

## 6. 自分の名前

### 6.1 保存と読み込み

- 保存先: `/home/asterism_profile.toml` (1 行 `name = "..."`。`\` と `"` は逃がす)。ブラウザ版は `/home` がブラウザの
  保存領域 (IndexedDB) にあるので、同じ道のまま残る。
- `picoruby-fmrb-asterism` (fmruby-core の既定のプロフィールの gem) が、アプリの VM が始まるときにファイルを読み、
  `name` があれば `Asterism.profile(name:)` に入れる。全アプリに効く。ファイルが無ければ読まない (`File.exist?` で
  先に見る)。プロフィールが受けない名前 (64 バイトを超える、手で書いた) は入れない。
- 書くのは `FmrbAsterism.save_name(name)`: ファイルに書き、すぐ今の VM のプロフィールにも入れる (つながっていれば
  `asterism/<ID>/@profile` に知らせが出て、console が描き直す)。空なら名前を消す。純 Ruby、VM のプールで動くので
  内蔵 RAM は使わない。
- ほかのアプリがすでに動いているときは、その VM のプロフィールは古いまま (起動し直すと読む)。同じ端末の複数の
  アプリが違う名前を答えうるのは V2.5 の 8 章と同じ。

### 6.2 オセロの画面

- 待ち合わせの 1 行目の右に「名前: <名前>」(未設定ならノードの ID を薄い色で)。F2 か「名前 (F2)」のボタンで
  入力の欄が下に出る。Enter か「保存」で保存、Esc か「やめる」でやめる、Backspace で 1 文字消す。
- 日本語: かな入力は合成した文字を UTF-8 の 1 バイトずつのキーとして送ってくるので、エディタと同じように
  バイトを集めて 1 文字にする。64 バイトまで (超える文字は入らない)。
- 表示の幅: 名前の入る所 (相手の一覧、誘い、対局中の相手の名前) を、全角 2 桁・半角 1 桁で数えて切るようにした
  (G1 は文字数で切っていたので、日本語の名前は右へはみ出しえた)。入力の欄は末尾が見えるよう前を落とす。

### 6.3 確かめ

| 試し | 結果 |
|---|---|
| sim で F2、かな入力で「きしま」、英字に戻して「P2」、Backspace、「3」、Enter | 「名前を「きしまP3」にした」。P4-Nano (G1 の othello) の一覧に「きしまP3 (linux)」 |
| console の図 | `きしまP3 (linux)`。sim で名前を「Sim2」に変えると、bridge が知らせを受け、図が `Sim2 (linux)` に変わった |
| sim のアプリを閉じて開き直す | PC から `profile_of` で名前が返り、`info` の名前も同じ |
| ブラウザ版 (家の中) で F2、かなで「たろう」と英字 | 保存され、読み直しても「名前: たろうw」。P4-Nano の一覧に「たろうw (wasm-...)」 |
| P4-Nano で F2、「nano」 | ブラウザ版の一覧に「nano (fmruby-...)」。**P4-Nano を起動し直して** othello を開くと「名前: nano」 |
| 通し稽古のブラウザ版 A で名前を変える | B の一覧と誘いに「alice」 |

- 注入で大文字を打つと小文字になった (ブラウザ版の web_input と Playwright の `type`。sim の注入の「P2」は大文字の
  まま)。注入の道具の側の話と見て、追っていない。

## 7. 通し稽古 (G0 の手元の構成、1 回で使い切る合言葉)

構成: asterism-console の `deploy/local/rehearse` (prepare・build・bootstrap・up、ブラウザ版の束は `rake wasm:dist` の
dist)。ブラウザは docker の Playwright (Chromium) を web の網に置き、ブラウザごとに別の文脈 (保存領域) で開いた。
合言葉は console の `BrowserPass.issue!` (画面の「Make a pass」と同じ処理) でブラウザ 1 つに 1 つ。

| アプリ | 結果 |
|---|---|
| othello (ブラウザ版 2 つ) | どちらも `wss://router.localtest:7443/?pass=***` で 1 本だけ開き、相手を見つけ、誘い・受け・3 手指せた。G1 では 2 本目が 401 でつながらなかった |
| rt_probe (2 つ) | 往復の中央値 30-31 ms・最大 33 ms、毎秒 25 回、落ちた 0 |
| rover_brain と rover_pilot (2 つ) | どちらも 1 本でつながった。ただし操縦機は頭脳を見つけられない: G0 の ACL がブラウザ版に許すのは `asterism/<ID>/game/**` だけで、`brain/rover` と ROS 2 のキー (`@ros2_lv/**` など) は通らない (つながりの話ではなく、キーの範囲の話)。家の中 (`ws://`、ACL なし) では 4 章のとおり動いた |

- 入口 (door) の記録は接続ごとに 1 行 (`#1`-`#6`、ブラウザ 6 つで 6 本)。nginx の記録に 401 は 0 件。
- 構成は volume ごと消し、`deploy/local/.env` と自己署名の証明書も消した。

## 8. 端末と内蔵 RAM

### 8.1 端末

- P4-Nano: このブランチのファーム (NARYAv4、asterism 0.6.0) をアプリの区画だけ焼いた (ポートを指定、build image の
  esptool)。`/app/test/` に othello・rt_probe・rover_brain・rover_pilot (G1b の版) と測りのアプリ `g1b_pollcost` を置いた。
  `/home/asterism_profile.toml` は確かめで「nano」になっている (ユーザが othello で変えられる)。動いているアプリは無い。
- AtomS3R: 同じくアプリの区画だけ焼いた (ATOMS3R、`scratchpad/h1/flash_atoms3r.sh`。シリアルの記録を一度止めて焼き、
  開き直した)。`/app/test/` に othello・rt_probe (G1b) と `g1b_pollcost`。動いているアプリは無い (元の Small Screen Test は
  止まったまま)。
- どちらもミュートのまま (焼いたのはアプリの区画だけで、設定には触れていない)。

### 8.2 内蔵 RAM

変えたのは純 Ruby (gem の mrblib と、アプリ) だけで、C は変えていない。

| ビルド | D/IRAM (esp_idf_size) | 前 (V2.5) |
|---|---|---|
| AtomS3R | 111,279 B | 111,279 B |
| P4-Nano (NARYAv4) | 125,612 B | - |
| Tab5 | 130,186 B | 130,186 B |
| Retro (NARYAv3) | 126,259 B | 126,259 B |

- 実行時: AtomS3R の測りのアプリが出した内蔵 RAM の空きは、焼く前 117,444 B、焼いた後 117,620 B。rt_probe の間は
  焼く前 117,296 B、焼いた後 117,620-117,628 B。P4-Nano は焼く前 172,908-182,988 B、焼いた後 166,708-184,392 B
  (rt_probe の 1 周の中の山の位置で揺れる)。増えていない。

## 9. 試験とビルド

| 対象 | 結果 |
|---|---|
| asterism `rake test` (msgs・objects・api) | 通る。凍結リテラル (`RUBYOPT=--enable-frozen-string-literal`) でも通る |
| asterism `rake test:mruby` | 通る (プロフィール 22 項目、session 24 項目) |
| asterism-console `bin/rails test`、`bin/rubocop` | 276 本 2291 項目 失敗 0、指摘 0 |
| fmruby-core `rake test` | 通る (exit 0) |
| sim の標準構成 | デスクトップが出て、エディタを開いて `a` が入る (`Ln 1, Col 2 *`) |
| ブラウザ版 (`rake wasm:mruby`・`wasm:web`・`wasm:dist`) | 通る。dist の束に G1b のアプリが入っている |
| AtomS3R・P4-Nano・Tab5・Retro のビルド | 通る (8.2 節) |

## 10. 見立てと違った点・残すこと

- **console の枠**: S4 の「1 本にすれば枠に入る」は console の直しが要った (5 章)。指示書の範囲 (「要る直しがあれば
  feature/g1b」) で直した。
- **通し稽古の ACL と台車**: ブラウザ版の ACL は `game/**` だけなので、通し稽古では操縦機が頭脳を見つけられない。
  台車を VPS 越しに操縦するなら、ACL に `brain/**` と ROS 2 のキーを足すか、G1.5 の「役割」で書く。
- **自分の put は自分に届かない**: Asterism.session で put と購読を同じセッションにしても、今までと同じく自分の put は
  自分の購読に来ない (rt_probe は自分の番号を相手のパケットで知るので影響なし)。
- **大文字の注入**: 6.3 節。
- **ほかのアプリの名前**: 名前を変えたとき、すでに動いているほかのアプリの VM は起動し直すまで古い名前 (6.1 節)。
- **公開の前に親がすること**: asterism の feature/session-poll を main へ入れて CI (mruby の job を含む) を見る。
  ASTERISM_PIN を main のコミットにし、`branch: main` に戻し、G1b の注釈を消す。console の feature/g1b を main へ入れ、
  Gemfile の rubygems の行 (`~> 0.5.0`) を 0.6.0 の公開のあとで `~> 0.6.0` に上げる (公開前に上げると deploy/vps の
  image が作れない)。gem の作成と公開 (asterism 0.6.0 だけ。asterism-zenoh は 0.4.0 のまま) はユーザ。
- 測りのアプリ `g1b_pollcost` は端末に置いたまま (リポジトリには入れていない。スクラッチ)。
