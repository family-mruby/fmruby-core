# GS4 報告: 状態のキー @status、網を見る CLI、Asterism を MCP のツールにする橋渡し

> 状態: 完了 (検収待ち) | 更新: 2026-10-17 | instruction_gs4.md の結果。asterism に `asterism/<node>/<app>/@status` (アプリの状態) を足し (protocol.md 8 章、mruby と CRuby の両方)、網を見る CLI `asterism` (nodes / objects / tree / watch / ping (echo) / call) と MCP の橋渡し `asterism mcp` を置いた。ゲームホストは卓ごとの数を @status で出し (asterism-games 0.9.0)、console は図の Details に出す (console 0.3.4)。止まる条件には当たっていない

## 1. コミット

| リポジトリ | ブランチ | コミット | 中身 |
|---|---|---|---|
| asterism | main | `a3bf96e` | `mrblib/status.rb` (`Asterism.status` / `status_of` / `Status.check` / `Status.parse`)、`Asterism.apps`、CRuby の `net.status` / `status_of` / `on_status`、protocol.md 8 章、適合の見本 version 3、`exe/asterism` と `lib/asterism/cli*`、試験 (`test_status.rb`・`test_cli.rb`・host_test)、README「Status」「The command line」、CHANGELOG (Unreleased) |
| asterism-console | main | `72fa3a3` console 0.3.4 | `Bridge::Statuses`、図の Details の Status、cloud_config の `console__statuses_in/out`、docs/rooms.md (games-host に @status)、docs/g1_5.md (見るだけの役割 viewer)、試験 |
| asterism-games | main | `e11fe90` games 0.9.0 | `Host::TablesStatus`・`tables_status`・`tell_tables`、README「Status」、docs/install.md、試験 |
| fmruby-core | develop | この報告と同じコミット | この報告と画面 (`report/gs4/*.png`) |

push・タグ・rubygems への公開はしていない。asterism の `VERSION` は 0.7.1 のまま (CHANGELOG の Unreleased に書いた。版はユーザが公開のときに決める)。コミットの前に 3 つの差分から実在の IP・端末 ID・鍵・メールアドレス・`~/.openai_key` の中身を検査して 0 件だった (例の `192.0.2.x`・`fmruby-aaaaaa`・`wasm-1a2b3c`・docker の内部の `172.31.240.x` だけ)。

## 2. @status の決まり (protocol.md の差分の要点)

- 鍵 `asterism/<node>/<app>/@status`。**アプリごと** (ノードのプロフィールとは別)。状態を持つアプリだけが queryable (depth 4) を宣言して get に今の値で答え、**変わったときだけ** put する (同じ値をもう一度設定しても put しない。前に put した符号化を比べる)。接続したとき (すでに状態があれば) と、切れた接続が戻ったときにも put する。消す put は無い (アプリが去れば liveliness で分かる)。
- 中身: str の鍵 (空でない) の MessagePack の map。値はプロトコルの値なら何でもよく、map から **4 段まで** の入れ子 (`{"a" => [[[[1]]]]}` が最深)、符号化して **1024 バイトまで**。送る側が越えたら送る前に `ArgumentError`。読む側は map でない・1024 バイトを越えるなら「状態なし」(nil)、str でない鍵の項目を落とす。入れ子の深さは読む側では確かめない。上限は `@profile` のアイコン (8 KB) より小さくした。数と状態を載せるもので、20 卓分の数でも収まる (5 章)。
- `@status` は verbatim chunk: `asterism/**`・`asterism/<node>/**`・`asterism/<node>/<app>/**` に当たらない (アプリの queryable にも届かない)。`asterism/*/*/@status`・`asterism/<node>/*/@status` は当たる。ACL も別に書く (読む側: query と subscription を `asterism/*/*/@status`、書く側: put・queryable・reply を `asterism/<node>/*/@status`)。
- protocol 1 のままの追加 (古いノードは答えも put もしない。読む側には「状態なし」)。冒頭の「番号を変えない追加」の説明に「新しい鍵」を足した。
- 章立て: 8 章「Status」を足し、もとの 8〜11 章を 9〜12 章に送った (参照を直したのは asterism の中の 3 か所だけ。ほかのリポジトリからの章番号の参照は無かった)。
- 適合の見本 (samples.json version 3): 受ける側に「status」「parameters は無視」「`asterism/iut/conf/**` に当たらない」「`asterism/*/*/@status` に当たる」「状態の無いアプリは答えない」、出す側に `status_of` 4 件 (int の鍵を落とす・map でない・1024 バイト超・誰も答えない) と `set_status` 2 件 (変えたら put、同じならしない)、`status_puts` (接続時と変えたときの 2 回だけ)。

API (README「Status」):

```ruby
Asterism.status(tables: 3, humans: 2)        # その欄だけ変える (nil で消す)、ほかは残す
Asterism.status({ "mahjong1" => {...} })     # Hash は状態の全体
Asterism.status                              # => 今の状態、無ければ nil
Asterism.status_of("games", "games")         # ほかのアプリの状態、無ければ nil
Asterism::Status.parse(key, payload)         # 購読した put を [node, app, status] に
net.on_status("*/*") { |node, app, st| }     # CRuby
Asterism.apps("*/*")                         # 生きているアプリ "<node>/<app>"
```

`Asterism.profile` と同じく、接続と close をまたいで残る (プロセス、ボードでは VM のもの)。mruby でも同じ (host_test の 40 項目の中で、2 つの mruby プロセス間の status_of・変化の put・同じ値で put しないこと、checks を確かめた)。

## 3. CLI と MCP の使い方

### 3.1 CLI (`exe/asterism`、README「The command line」)

```
asterism nodes   --router tcp/192.0.2.2:7447            # ノード、アプリ (状態つき)、プロフィール
asterism objects --router ... 'games/games/*'            # オブジェクトとメソッド (meta)
asterism tree    --credentials admin-viewer.tar          # ノード > アプリ (状態) > オブジェクト (メソッド)
asterism watch   --router ...  [--json] [--duration 60]  # ノード・アプリ・オブジェクトの出入り、状態とプロフィールの変化
asterism ping    --router ... games/games/othello1       # 往復時間 (meta の get。メソッドは動かない)
asterism ping    --router ... linux                      # ノードのプロフィールの往復
asterism ping    ... games/games/othello1 --call table   # 呼び出しの往復 (明示したときだけ)
asterism call    --router ... games/games/othello1 join '"Alice"'
asterism call    ... linux/demo/screen say hello --kwargs '{color: 3}'
```

- 名前: 往復時間を測るコマンドは `ping` にし、指示書の名前 `echo` を別名にした (中身は meta の get が既定。`echo` という名前だと中身を送り返すように読めるため)。
- 入口: `--router` (平文) か `--credentials <.tar>` (相互 TLS、ルータと名前は .tar から)。ほかに `--peer` (ルータなしで直接つなぐ。試験用)。ノード ID は証明書の名前、なければ `cli-<6 桁>`、アプリは `cli-<4 桁>`。
- `call` の引数: JSON → Ruby の literal (Ripper で読み、評価しない。nil/true/false/数/文字列/Symbol/配列/Hash だけ) → そのままの文字列、の順。
- 見えるものはルータが見せるものだけ。断られたものは一覧に出ない (「居ない」とは言わない)。`.tar` で `call` が役割で断られたときは「nobody answers」と、その注意書きを出す。

`watch` の出力 (家の LAN、本番の作業から):

```
18:08:30.115 - node games
18:08:30.115 - app games/games
18:08:42.990 + node games
18:08:42.990 + app games/games
18:08:42.990 * profile games  (cruby) "asterism-games"
18:08:42.994 ~ status games/games {"mahjong1":{...},"othello1":{"st":"wait","members":3,"humans":1,"cpus":0,"away":0}}
18:09:15.378 ~ status games/games {...,"othello1":{"st":"play","members":4,"humans":1,"cpus":1,"away":0}}
```

### 3.2 MCP (`asterism mcp`)

```
asterism mcp --router tcp/192.0.2.2:7447 --allow 'games/othello1.*'
claude mcp add asterism -- asterism mcp --credentials admin.tar --allow 'games/othello1.*'
```

- 見るだけのツールは常に出る: `asterism_nodes` (ノード・プロフィール・アプリと状態・オブジェクト名)、`asterism_objects` (メソッドと引数の数。許したメソッドには `callable_as` でツール名)、`asterism_status`。
- 呼び出しのツールは `--allow` に当たるメソッドだけ。名前は `<node>.<app>.<object>.<method>` (MCP の名前に使えない文字は `_`)。入力は meta の引数の数から: n 個なら `arg1`〜`argN` (必須、型は問わない)、-1 なら配列 `args`、ほかに任意の `kwargs` (object)。答えは `{"value": ...}`、失敗は isError で `{"error": {"kind", "class", "message"}}`。
- `--allow` の形: `[node/][app/]object.method`、各部分で `*` だけが wildcard (`?` は `ok?` のようなメソッド名のためそのまま)。部分が足りなければ後ろに合わせる (`games/othello1.*` は `*/games/othello1.*`)、`.method` が無ければ全メソッド。
- 網の変化に追う: 2 秒ごとに許した対象のオブジェクトを見直し、変わったらツールを作り直して `notifications/tools/list_changed` を送る。
- mcp gem は gemspec の依存にしていない (`asterism mcp` のときだけ require し、無ければ `gem install mcp` と言う)。CLI だけの人には入らない。手元の mcp gem は 1.4.0。

## 4. 受け入れ条件ごとの結果

### 4.1 試験 (条件 1)

| | 前 | 後 |
|---|---|---|
| asterism `rake test` (msgs・objects・api・cli・protocol) | 77 runs、protocol 194 passed | **86 runs / 1707 assertions、0 failures**、protocol (CRuby) **212 passed** |
| asterism `rake test:mruby` | | **40 / 15 / 24 checks、0 failed** (40 が status を含む) |
| asterism `rake test:protocol_mruby` | | **212 passed、0 failed** |
| asterism、ルータ越し (`ASTERISM_TEST_ROUTER=tcp/127.0.0.1:17487`) で test_status / test_cli / test:protocol | | 4 runs / 5 runs / 212 passed、全部合格 |
| asterism-console `bin/rails test` | | **445 runs / 4387 assertions、0 failures**。`bin/rubocop` 250 files、no offenses |
| asterism-games `MJ_GAMES=8 bundle exec rake test` | 169 runs (GS3) | **169 runs、0 failures、skip 1** (別プロセスの tally は 10 runs、+1 件) |
| games、ルータ越しの tally の網の試験 | | asterism 0.7.1 (手で put する道) と手元の asterism (`net.status` の道、`RUBYLIB` で) の両方で合格 (status の put と get の答えを確かめる 2 項目を足した) |

足した試験: asterism `test_status.rb` (checks・parse・欄の合わせ方・2 プロセス間の status_of / 変化の put / 同じ値で put しない / `**` に当たらない / 状態なし / 受け付けない答え / `on_status`・`net.status_of`)、`test_cli.rb` (literal、`--allow` の形、使い方の表示、別プロセスのノードに nodes・objects・tree・ping・echo・call (kwargs、呼び手、raised、not_exposed)・watch、mcp gem の客で MCP)。console `statuses_test.rb`・`viewer_role_test.rb`、cloud_config と games-host の試験に @status。games `test_the_at_status_has_counts_only` と部屋の試験 (部屋では出さない)。

### 4.2 家の LAN (条件 2)

構成: この Step の zenohd `gs4-zenohd` (tcp 17487・ws 17489、sim の zenohd `fmruby_zenohd:7447` へ `-e`、sim の網に同居)、ゲームホスト (手元の asterism、`net.status` の道。mahjong1・othello1)、sim (426x240) の「Othello (table)」と asterism_demo、ブラウザ版 (`?zenoh=ws/localhost:17489`) の「Othello (table)」、`bin/othello-tty`、MCP の客。CLI の watch を通しで動かした。

| コマンド | 結果 |
|---|---|
| `nodes` / `tree` / `objects` | games (プロフィール名・状態・2 卓のメソッド)、sim の `linux` (family_mruby/sim)、ブラウザ版の `wasm-…`、tty、CLI 自身 |
| `watch` | sim の卓のアプリ: `+ node linux`・`+ app linux/game`。卓に入る → `members` 1、S で座る → `humans` 1、S で立つ → 0。ブラウザ版の入る・座る・立つ、tty の入る、MCP の客の入る・始める (`st` play)・終局 (over)・抜ける、がそれぞれ status の変化として流れた (変化から put まで数 ms: 例 tty の join 18:08:17.315 の 12 ms 後に status)。ゲームホストを止めて起こす → `- node games` … `+ node games`・`+ app`・`+ object`・profile・起動直後の status |
| `ping` | テストの CRuby ノードの meta 2.7-5.0 ms (平均 3.9)。sim のプロフィール 9.2-51.0 ms (平均 41.9)、sim の `linux/demo/info` の meta 3.5-53.2 ms (平均 36.9。sim の更新の輪 50 ms しだい) |
| `call` | `linux/demo/info status` → `{"name"=>"linux", ...}`。ゲームホストの卓の `join`・`table` など |

### 4.3 console の deploy/local (条件 3)

console の作業コピー (0.3.4) を `rehearse prepare → build → bootstrap → up`。役割と行を console の container の `bin/rails runner` で作った: docs/rooms.md どおりの `games-host` (@status の行を足した形) と行 `games`、docs/g1_5.md の `viewer` と行 `admin-viewer`、`games` と行 `player-a`。`.tar` は `Relay::Issuer#issue_local` で作り「Save all (.tar)」と同じ中身にした。もう一度 bootstrap してルータの設定 (`@status` の規則 6 か所) を書き、zenohd を起こし直した。常設の卓のホストは asterism-games の作業コピーを `games.tar` で手元から起こした (bundle の asterism 0.7.1 = **手で put する道**)。

- **見るだけの役割の `.tar` で網全体**: `asterism tree --credentials admin-viewer.tar` で console・games (プロフィール名「permanent tables」・状態・2 卓のメソッド) が見えた (0.47 s)。`ping games/games/othello1` 2.7-3.5 ms。`watch` に player-a の tty の入る・座る・CPU と始める・終局 (CPU の勝ち 41-23)・抜けるが status の変化として流れた。
- **`call` は役割で断られる**: `asterism call games/games/othello1 table` → `no answer ... (nobody answers)` と注意書き、終了コード 1。MCP の客でも同じ (isError、`Asterism::TimeoutError`)。
- **図の Details にゲームホストの @status**: Playwright 1.56 (rehearsal の web の網、管理者の address、TOTP) で app `games` とノード `games` を選んだ: どちらにも「Status」が出た (`mahjong1 st="wait" members=0 …`、`othello1 st="over" … cpus=1`)。`report/gs4/console_details_node.png`・`console_details_app.png`。図のノード・線は前と同じ。
- **部屋**: 部屋では出さない (2 章 4 の結果)。部屋のホストの規則 (`room_host_rules` の own は `room/*/asterism/games$*`・`/**`・`/@profile`・`/**/@adv/**`) に `@status` が無く、席の規則も同じなので、部屋の中で put しても通らない。読む側の console も部屋は見張るだけ。console の試験で「部屋の鍵に put できない」を、games の試験で「namespace のあるホストは出さない」を確かめた。deploy/local で部屋は開いていない。

### 4.4 MCP (条件 4)

mcp gem 1.4.0 の客 (`MCP::Client::Stdio`) から `asterism mcp --router tcp/127.0.0.1:17487 --allow 'games/othello1.*'`:

- ツールは見るだけの 3 つ + `games.games.othello1.{join,mine,sit,stand,cpu,start,move,resign,view,table,leave}` の 11。**mahjong1 のツールは 0 個** (許していないメソッドは出ない)。
- `asterism_status` で 2 卓の数、`asterism_nodes` でノードの一覧が読めた。
- `join("mcp")` → token、`sit(token, 0)`・`cpu(token, 1, true)`・`start(token)` → "ok"、あとは `view` の `lg` の先頭の升目を `move(token, n, square)` で打ち続け、**30 手で終局** (25.9 s、ホストの記録 `game over (count, 41-23, seat 0 wins)`、色はくじで CPU が黒)、`leave` → true。
- 見るだけの役割の `.tar` で `--allow` を付けても、ツールは出るが呼び出しはルータが断る (6 章)。

### 4.5 数字 (条件 5)

| | GS3 | GS4 |
|---|---|---|
| @status の 1 通 (2 卓、鍵 28 B + 中身) | - | **123 B** (中身 95 B)。3 卓で数がいちばん大きいときの中身 142 B。1024 B の上限には 21 卓まで入る |
| 1 通の最大 (実戦、平文、ボット 3 + CPU の東風戦とオセロ 1 局を同時に) | 549 B (view reply) | **552 B** (view reply。局の中身で揺れる幅) |
| ゲームホストの RSS (VmHWM、同じ負荷) | 59.6 MB | **60.4 MB** (手元の asterism で。常設の卓のホスト (0.7.1、2 卓、オセロ 1 局) は最大 243 B) |

## 5. 見立てと違った点・判断したこと

- **ワイルドカードの get は ACL の網で断られる**。最初の CLI は `asterism/*/*/@status`・`asterism/*/@profile`・`asterism/*/*/*/meta` を 1 回ずつ get していた。家の LAN では動いたが、console の網では状態とプロフィールが出なかった。zenohd は query を届け先ごとにその規則で確かめ、ゲームホストの規則 (`asterism/games/*/@status`) に `asterism/*/*/@status` は含まれないので届かない。ノードごと・アプリごとの get を一度に出す形に直した (meta はアプリごとの `asterism/<node>/<app>/*/meta`、足りないものをオブジェクトごと)。一覧 1 回で時間切れ 1 回分まで。console の bridge は最初からアプリごとに聞く形にしてある。watch の購読はワイルドカードのまま (put は購読ごとに届けられ、規則に合うものが通る)。
- **asterism-games は asterism 0.7.1 に固定** (rubygems の版。image もそれで作る)。0.7.1 には `Asterism.status` が無いので、ホストは `net.respond_to?(:status)` で分け、無ければ同じことを session の上で手でやる (`Host::TablesStatus`: 鍵の queryable と、数が変わったときの put。約 20 行)。新しい asterism を公開して Gemfile を上げたら手の道は消せる。この形のため、手の道は 0.7.1 では「接続が戻ったときの put し直し」をしない (次の変化か 1 秒ごとの見直しで出る。ホストが接続し直すときは作り直すので出る)。
- **console の bridge も asterism 0.7.1 の image で動く必要がある** (VPS の image は rubygems の asterism)。`Asterism::Status` に頼らず、読み方 (map・str の鍵・1024 B) を `Bridge::Statuses` に写した。
- 「卓の名前・ノード ID・鍵は載せない」: 状態の鍵には **卓のオブジェクト名** (`mahjong1`) を使った。オブジェクト名は liveliness の鍵ですでに誰にでも見えていて、`GET /status` と同じ形。載せないのは会員の名前・ノード ID・token。卓の名前も伏せる (配列にする) という読み方なら、GS5 の前に直せる。
- **console 自身のプロフィールはほかのノードから読めない** (前から)。console の規則は profile の query を console から出すことと、答えと put を console に届けることだけで、ほかのノードの query を console に届けない。CLI の tree で console のノードに名前が出ないのはこのため。この Step では触っていない。
- watch の始めに、直前に終わった別の CLI のノードが `- node` として出ることがある (接続した直後の liveliness の出入りで、実際に起きたこと)。
- MCP は役割を知らないので、見るだけの役割で `--allow` を付けると呼べないツールが並ぶ。呼ぶとルータが断り、isError で返る。
- console の rehearsal の image は `deploy/vps/env.example` の `CONSOLE_VERSION` で名前が付く (0.3.3 のままだった)。0.3.4 に上げた (`docker-compose.yml` の既定も)。
- games の tally の網の試験を bundler なしで動かすと、子のノード (`RbConfig.ruby -e 'require "asterism"'`) が asterism を読めずに失敗する (gem が手元に入っていないため)。`RUBYLIB` で渡せば通る。試験そのものは直していない。
- `pkill -f` が自分のシェル (同じ文字列を含むコマンド) を殺すので、片付けは pid で行った。

## 6. アプリ単位の居る・居ないの材料 (GS3 の申し送り)

- **watch でアプリ単位の出入りは見える**。sim でノード `linux` の「Othello (table)」(app `game`) を開いたまま asterism_demo (app `demo`) を起こして止めると、`+ app linux/demo`・`+ object …` のあと `- object …`・`- app linux/demo` が流れ、ノード `linux` は抜けなかった (18:06:38 → 18:06:53)。liveliness のアプリの印 (`asterism/<node>/<app>`) は protocol 1 で全アプリにあり、読む側が数えれば分かる。
- asterism に `Asterism.apps` を足した (手元の表 `@apps` を出すだけ)。アプリの出入りを知らせる `on_app_join` / `on_app_leave` (仮) は足していない (指示書の範囲外、API の選択)。`tell_nodes` と同じ比べ方で `@apps` の差を取れば 20 行ほど。
- **ゲームホストがアプリ単位で見るには、会員がどのアプリかを知る必要がある**。呼び出しの鍵の最後は呼び手のノード ID だけで、アプリ名は無い (protocol.md 4.1)。選択肢: (a) 卓のアプリはアプリ名 `game` という決まりにして、会員を `<from>/game` の印で見る (今の端末とブラウザ版はそう。常設の卓の役割も `Read asterism/*/game` で、この印だけが届く)。(b) 呼び出しの鍵か request の map に呼び手のアプリを足す (protocol の追加。map の欄なら番号は変えない)。(c) `join` の引数でアプリ名を申告させる (主張にすぎない)。(a) が最小で、ACL ともすでに合っている。
- 読む側が見えない印を「居ない」と言わないこと (GS3 9 章) は、CLI では「断られたものは一覧に出さない」とした。watch は見えた変化だけを流す。

## 7. GS5 (麻雀の MCP と AI の打ち手) に向けて分かったこと

- **meta には引数の名前も意味も無い**。MCP のツールの入力は `arg1`〜`argN` で、AI はオセロの `move(token, n, square)` の `n` が「view の n」だと知る手段が無い。今回は客の台本が知っていた。protocol.md 5 章は meta の項目に欄を足してよい (読む側は知らない欄を無視) ので、`"args" => ["token", "n", "square"]`・`"doc" => "..."` のような欄を足し、`expose` で書けるようにするのが素直 (protocol の追加。GS5 の前にユーザの判断が要る)。麻雀は `act(token, n, kind, tile)` など引数が多く、なおさら要る。
- 卓の view は短い鍵 (`st`・`tn`・`lg`・`dl`…) で、AI には説明が要る (games_showcase.md の「麻雀の MCP のツール」の「卓の見え方を AI が読める形にする説明」)。MCP の側でツールの説明に README の表を載せるか、ゲーム側に AI 向けのメソッド (例: `describe`) を足すか。
- MCP の呼び出しはすべて MCP サーバのノード ID で出る (`caller: :from`)。1 つの MCP サーバは 1 人の打ち手で、席も 1 つ。複数の AI を並べるなら MCP サーバ (ノード ID) を分ける。
- 手番を待つのに今は view を 0.2 秒ごとに呼んでいる。@status (卓の `st`・数) や卓の状態の put を AI に知らせる道 (MCP の resources の subscribe か、通知) があると呼び出しが減る。
- `--allow` は `games/mahjong1.*` で足りる。持ち時間 (麻雀の turn 5 s + bank 20 s) は MCP の往復と AI の思考時間に対して短い。AI の卓では時間を延ばす設定が要る。
- case/in で答えの形を分けるときの VM の制約 (GS3 9 章) は CRuby の MCP には関係ない。

## 8. 残ったこと

- 5 章の「卓の名前を状態に載せる」読み方の確認。
- 7 章の meta の欄の追加 (引数名・説明) と、アプリの出入りの API (6 章) はユーザの判断。
- asterism の公開 (`Asterism.status` と CLI) と、そのあと asterism-games・console の Gemfile を上げて手の道を消すこと。fmruby-core の ASTERISM_PIN (端末の portable の層の取り込み) は別の Step。
- 部屋を開いた deploy/local での確かめはしていない (規則と試験で確かめた)。
- 後片付け: deploy/local を `VOLUMES=1 rehearse down` で volume ごと消し、`deploy/local/.env` と `certs/` を消した。作った image `asterism-console-rehearsal:0.3.3` (0.3.4 の中身。env.example の版の名前で付いた) を消した。zenohd `gs4-zenohd`、家の LAN のゲームホスト・常設の卓のホスト・CLI の watch 2 つ・tty・ボット・MCP サーバ、web_* の開発サーバとブラウザを止めて消した。sim は音を戻してから sim_down。scratchpad の `.tar`・管理者のパスワード・TOTP の秘密・Playwright の台本を消した。前からある image (`asterism-console-rehearsal:0.3.2`、Playwright、`eclipse/zenoh:1.10.1` ほか) は残した。docker の prune はしていない。fmruby-core の build/ は触っていない。fmruby-graphics-audio の作業コピーの前からの変更 (`flash/etc/display_conf_linux.txt`) は触っていない。
