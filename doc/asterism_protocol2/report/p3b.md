# P3b report: asterism-console を protocol 2 に入れ替えた

> 状態: 済 (P4 での順序の注意あり) | 更新: 2026-11-13 | console が状態をアプリの自己紹介 `@profile` (1 回の get で `status;pubs;types`) から読み、`@status`・`@pubs` の put を追い、図の詳細にトピックと型を出すようにした。記録 (MCAP) は encoding・通し番号・送り手の時刻・宣言の型を design.md 5 章のとおりに書き、再生も合わせた。ルータの ACL は console の `@status` の規則の組を `@profile` の問い合わせと `@status`・`@pubs` の購読に置き換えた。asterism-console 4 コミット (ac0c6e7・5faa47d・44328cd・1f1916d)。**データベースの中のロール (games-host など) は管理者のデータなので変えていない。P4 で手で直す必要がある** (下の 8 章)

## 1. 変えた所 (asterism-console のみ)

| ファイル | 何を |
|---|---|
| `lib/bridge/app_profiles.rb` (新設、`statuses.rb` を置き換え) | `Bridge::AppProfiles`: アプリごとに status・pubs・types を持つ。現れたアプリを 1 回だけ問い合わせ (`due`)、答え (`answered`)、`@status` の put (`told_status`)、`@pubs` の put (`told_pubs`: 知らない独自の型を名指したら問い合わせ直し `ask_again`)、図への受け渡し (`to_graph`)、記録への受け渡し (`topic(key)`: 宣言と、その型が要る定義をフィールドからたどって全部)。読み方は asterism の `Status.check`・`Pubs.check`・`Types.check` |
| `lib/bridge/runner.rb` | `@status` への get を `asterism/<node>/<app>/@profile` への get 1 回 (引数 `status;pubs;types`) に。`asterism/*/*/@pubs` を購読。記録に宣言の引き先を渡す。再生の put に encoding と attachment を通す |
| `lib/bridge/graph.rb` | `statuses:` を `app_profiles:` に。アプリの data に `status` (今どおり)・`pubs` (トピックごとの行: 組み込みの型と encoding を名前で、単位・説明・送り手の時刻)・`types` |
| `app/javascript/controllers/console_controller.js` | アプリの詳細に Topics・Types の行、ノードの詳細にアプリごとの Topics の行 (今の Status の行と同じ出し方) |
| `lib/bridge/fixture.rb` | 見本のファイルが `app_profiles` を持てる (目視の確認に使った) |
| `lib/bridge/recorder.rb`・`lib/bag/channels.rb` | 3 章の対応で書く |
| `lib/bag/view.rb`・`lib/bag/decode.rb` | MessagePack 以外の encoding の Asterism のチャンネルも kind `key`。`image/jpeg`・`image/png` は絵として出す |
| `lib/bridge/player.rb` | Asterism のチャンネルは encoding (MessagePack 以外のとき) と、protocol 2 の記録なら attachment `[seq]` / `[seq, ms]` を付けて put。JPEG のトピックも再生する |
| `lib/relay/cloud_config.rb` | 4 章 |
| `docs/rooms.md`・`docs/g1_5.md` | 文書にあるロールの例 (games-host・viewer) を protocol 2 の行に |
| `Gemfile` (コメントのみ)・`README.md` | protocol 2 が要ること、記録のチャンネルの形 |
| 試験 | `test/lib/bridge/app_profiles_test.rb` (新設)、`app_profiles_live_test.rb` (新設、CRuby のアプリと peer でつなぐ)、recorder・player・bag・cloud_config・viewer_role・spectate_roles の試験を足し直し。`statuses_test.rb` は消した |

### Zenoh を直接使い続けるところ (指示書「理由を report に」)

console に `Asterism.publish` / `Asterism.subscribe` を使う所は無い。値を受けるのはすべて読み取り用の Zenoh の接続 (`Asterism::Zenoh.open`) のままにした。理由:

- 見張り (Watch)・グラフ (Plots)・ログ・頻度・記録は、**利用者が書いた任意のキー式** (ROS 2 のキー、`asterism/` の外も) を購読し、**受け取ったバイト列そのもの**を使う (記録はバイト単位で同じに残す、見張りは hex の下見)。`Asterism.subscribe` は `asterism/` を頭に付け、MessagePack を解いてから渡すので合わない。
- 記録は Sample の `encoding`・`attachment`・`timestamp` を読んで protocol 2 の決まりどおりに書く (attachment の読み方は asterism の `Subscription#unwrap` と同じ範囲の検査、`Bag::Channels.attachment`)。
- アプリの自己紹介の get も読み取り用の接続から 1 回で 3 つを聞く。`Asterism.status_of`・`pubs_of`・`types_of` を使うと get が 3 回になる。
- 再生は「記録した値を記録したキーに置き直す」もので、console 自身のトピックではない (`Asterism.publish` は `asterism/<console>/console/<name>` にしか出せない)。そこで素の put に記録の encoding と attachment を付ける。

## 2. 開発中の手元のつなぎ方と P4 での戻し方

- **何も足していない**。console の Gemfile は前から既定で隣の checkout を指している
  (`gem "asterism", path: ENV.fetch("ASTERISM_DIR", "../asterism")`、asterism-zenoh も同じ)。`ASTERISM_GEMS=rubygems` のときだけ rubygems.org の
  `~> 0.13` を取る (deploy/vps のイメージがこれ)。試験はこのまま手元の asterism (P2 の protocol 2、途中から P3a の 8c90094) で流した。
- **P4 で直すもの**: asterism の protocol 2 の版を出したら、Gemfile の `ASTERISM_GEMS == "rubygems"` の枝の `~> 0.13` をその版に上げ、
  `bundle install` で Gemfile.lock の asterism の版を合わせる。README の「The Asterism gems」の節 (rubygems から取るときの例 `~> 0.8` と、
  「protocol 2 の版はまだ出ていない」の文) を直す。今 `ASTERISM_GEMS=rubygems` でイメージを作ると protocol 1 の 0.13 が入り、
  `Asterism::Pubs` が無いので bridge の自己紹介の読み取りが落ちる (P4 の前に配置しないこと)。

## 3. 記録 (MCAP) の対応 (design.md 5 章)

| MCAP | console の書き方 |
|---|---|
| channel の topic | キー (今どおり) |
| channel の message_encoding | Sample の encoding。無い (`zenoh/bytes`) なら `msgpack`、あればそのまま (`image/jpeg`)。同じキーで encoding が変われば別のチャンネル |
| schema (name / encoding / data) | 組み込みの型: `asterism/<名前>` (`asterism/vector3`) / `""` / `""`。独自の型: 型の名前 / `asterism/types` / その型とフィールドが名指す独自の型の定義の JSON (`{名前 => {"f", "d"}}`、定義を聞けていなければ encoding・data は空)。型なし: `asterism/msgpack` (MessagePack) か `asterism/untyped` / `""` / `""` |
| channel の metadata | `asterism_key_expr` (今どおり)、`asterism_protocol` は `"2"`、宣言の JSON を `asterism_declaration` に (宣言を知っているときだけ) |
| message の sequence | attachment の通し番号 (無ければ 0) |
| message の publish_time | attachment の送り手の時刻 (ms → ns)、無ければルータのタイムスタンプ、それも無ければ log_time |
| message の log_time | bridge が受け取った時刻 (今どおり) |

- 宣言はチャンネルを作るとき (そのキーの最初の値) に引く。記録を始めた直後に現れたアプリなど、まだ自己紹介を聞けていないキーは型なしのチャンネルになる。
- 再生: Asterism のチャンネルは encoding (msgpack 以外) を付け、`asterism_protocol` が 2 以上なら attachment `[sequence]`、宣言に `ts` があれば
  `[sequence, publish_time の ms]`。protocol 1 の記録は今どおり何も付けない。受け手から見ると、記録の番号と送り手の時刻がそのまま届く
  (live 試験で確認)。
- timeline の Type の欄は schema の名前を出すので、何も変えずに `asterism/vector3` や独自の型の名前が出る。

## 4. ACL の変更 (`Relay::CloudConfig`)

console の証明書の規則 (console 以外は誰も持たない):

| 規則 | messages | 向き | キー |
|---|---|---|---|
| `<id>__app_profiles_in` | query | ingress | `asterism/*/*/@profile` |
| `<id>__app_profiles_out` | reply | egress | `asterism/*/*/@profile` |
| `<id>__app_notices_in` | declare_subscriber | ingress | `asterism/*/*/@status`、`asterism/*/*/@pubs` |
| `<id>__app_notices_out` | put | egress | 同上 |

- 前の `<id>__statuses_in` / `_out` (`asterism/*/*/@status` に subscriber・query / put・reply) は無くした (`@status` への問い合わせは要らなくなった)。
- 今の `@status` の規則より狭くした (`@profile` には購読を、`@status`・`@pubs` には問い合わせを許さない。誰もそこに出さない・答えないため)。
- 文書のロールの例 (コードの外、データベースのロール): games-host の「Read and write」に `asterism/${node}/*/@profile`・`asterism/${node}/*/@pubs` を足し、
  viewer の read に `asterism/*/*/@profile`・`asterism/*/*/@pubs`、ask の `asterism/*/*/@status` を `asterism/*/*/@profile` に。
  **ask の行だけでは答えが届かない** (ロールの ask は query の ingress だけで、答えの egress は read の行から来る。ノードの `@profile` が read と ask の
  両方にあるのと同じ) ので、viewer の read にも `asterism/*/*/@profile` が要る。試験で確かめた。

## 5. 試験

| 何を | 結果 |
|---|---|
| 変更前の `bin/rails test` (手元の asterism、P2 の protocol 2) | 598 runs, 6405 assertions, 0 failures |
| 変更後の `bin/rails test` (`env -u ASTERISM_CREDENTIALS`) | **606 runs, 6523 assertions, 0 failures, 0 errors, 0 skips**。asterism が P3a の 8c90094 に進んだ後にもう一度流して同じ |
| `test/lib/bridge/app_profiles_live_test.rb` (CRuby のアプリを別プロセスで peer につなぐ、ルータなし) | 2 runs, 20 assertions。3 回流して揺れなし。get 1 回で status・pubs・types、`@status` の put、新しい独自の型の `@pubs` で問い合わせ直し、記録 (encoding・連番・送り手の時刻 ms・schema・JPEG のバイト列)、再生した値をアプリの `Asterism.subscribe` が記録の番号と時刻で受け取る |
| `bin/rubocop` | 309 files, no offenses |
| 目視 (Docker: `script/headless/run`、Playwright の container、見本モード) | アプリの詳細の Status・Topics・Types の行、ノードの詳細のアプリごとの Topics の行が出ることを確かめた。利用者のデータベースに触らないよう、リポジトリの写しを scratchpad に作って見本用のデータベースを新しく作り、終わったら消した |

- `bin/signer` の `TypeError` の出力は変更前から出ている (試験は通る)。触っていない。
- Docker は見本モードの画面撮影だけに使った。`fmruby_*` のコンテナには触っていない。VPS には触っていない。

## 6. 見立てと違った点

- 手元へのつなぎは要らなかった (2 章)。
- **変更前の試験は protocol 2 の asterism でもすべて通っていた**: bridge の試験は網を偽物にするので、protocol 1 の `@status` への問い合わせが
  答えられなくなったことは見えない。protocol 2 の相手と実際に話す live 試験を足した。
- viewer のロールは ask の行に `@profile` を足すだけでは答えが届かなかった (4 章)。
- 見本モード (Puma) では encoding が「encoding 17」と番号で出る。Puma は asterism の gem を読まない決まり (runner.rb の冒頭) なので、
  encoding の名前の表は gem から引けるとき (bridge の中) だけ使う。実際の図は bridge が作るので `image/jpeg` と出る。組み込みの型の名前 7 つは
  console に表を持った (`Bag::Channels::BUILTIN_TYPES`)。

## 7. 撤回した仮説

- 「状態・宣言・型は asterism の `status_of`・`pubs_of`・`types_of` で読めばよい」: get が 3 回になり、bridge の自己紹介の worker の待ちも 3 倍になる。
  引数を `status;pubs;types` にした get 1 回にした (P2 の「2 つ書けば 2 つ返す」の延長で、3 つでも答えることを live 試験で確認)。
- 「console の規則は今の `@status` の組 (購読+問い合わせ / put+答え) を 3 つのキーに広げればよい」: 誰も使わない許可が増えるので、
  `@profile` は問い合わせと答え、`@status`・`@pubs` は購読と put に分けた。

## 8. 残件・P4 での順序の注意

1. **console の更新とルータの設定は同時に**: console の更新の setup がルータの設定を書き直して再起動する (0.3.3・0.3.4 と同じ流れ)。
   古い console (protocol 1) は `@status` に問い合わせるが、新しい規則ではそれが通らない。逆に新しい console は protocol 1 のアプリの状態を読めない
   (`@profile` に誰も答えない。`@status` の put は届くので、put が来れば出る)。値 (データ) の流れには影響しない。
2. **データベースのロールを手で直す (管理者の作業、Apply が要る)**: games-host の「Read and write」に `asterism/${node}/*/@profile` と
   `asterism/${node}/*/@pubs` を足す。viewer (作ってあれば) の read に `asterism/*/*/@profile`・`asterism/*/*/@pubs`、ask の `asterism/*/*/@status` を
   `asterism/*/*/@profile` に。**ゲームホストを protocol 2 の asterism-games に上げるのと同時か先に**。遅れると、ホストの `@profile` の queryable・
   答えと `@pubs` の put がルータに捨てられ、図の詳細から状態が消える (卓の値は流れる)。CLI・MCP (P3a) を viewer の証明書で使うときも同じ。
   install.md の更新手順への書き込み (0.3.4 の「add ... to the role games-host and Apply」と同じ形) は版を上げる P4 で書く。
3. **止まって返す点 (仕様の選択)**: 例のロール `match` (データベースが空のときに setup が作る、ブラウザの端末が使う) とほかの端末向けのロールには
   `asterism/${node}/*/@profile`・`@status`・`@pubs` の行が無い。P3c で端末のアプリ (お絵かきなど) がトピックを宣言すると、雲のルータ越しでは
   宣言 (`@pubs`) と自己紹介の答えが捨てられる (値は `asterism/${node}/game/**` などで流れる)。例のロールの既定を変えるか、各自が足すかはユーザが決める。
4. **部屋 (room) の中のアプリ**: 部屋の規則は `room/<slot>/asterism/<node>` とその下・ノードの `@profile` だけを書かせ、アプリの `@profile`・`@status`・
   `@pubs` は入らない (前から「部屋のホストは状態を言わない」)。console は部屋の中の値を見ないので console には影響しないが、部屋の中のアプリどうしが
   `pubs_of` で宣言を読むなら規則が要る (今は誰もしていない)。
5. Watch (キーの見張り) と Plots は encoding・通し番号をまだ見ていない (JPEG のトピックを見張ると hex で出る)。P3b の範囲外として残した。
6. ルータのタイムスタンプを publish_time にする経路は、live 試験が peer の直結 (タイムスタンプなし) なので偽の Sample の試験だけで確かめた。
   ルータ越しの通しは P3c (手元のルータ) で見る。
7. 版・CHANGELOG は上げていない。push していない。

## 9. 追記: ロールの既定と、今の台帳の自動の直し (2026-11-13)

8 章の 3 (例のロールの既定) をユーザが「既定に足し、今の台帳も自動で直す」と決めた。asterism-console b705f7d・642ff61 で入れた。

- **新しい console**: setup が作る例のロール `match` の「Read and write」に `asterism/${node}/*/@profile`・`asterism/${node}/*/@status`・
  `asterism/${node}/*/@pubs` を最初から入れた (db/seeds.rb)。rw はすべての message を両方向に通すので、queryable・答え・put が入る。
  console のコードが作る既定のロールは `match` だけで、端末向けの既定のロールは無かった (games-host・viewer・spectator は文書の例を管理者が作る)。
- **今の console (更新)**: データの migration `db/migrate/20261113100000_relay_role_app_profiles.rb` (`RelayRoleAppProfiles`)。
  管理者の手直しと例を見分けられないので、すべてのロールに同じ規則を当て、**足すだけ** (消すのは ask の `@status` の行の置き換えだけ)。
  - 名前が `match`: rw に上の 3 行。
  - rw の `<prefix>/*/@status` (games-host): 隣に `<prefix>/*/@profile`・`<prefix>/*/@pubs`。
  - read の `<prefix>/*/@status` (viewer): 隣に `<prefix>/*/@pubs`。
  - ask の `<prefix>/*/@status`: `<prefix>/*/@profile` に置き換え、read にも `<prefix>/*/@profile` を足す (答えは read の行から届く、4 章)。
  - 変えたロールごとに `relay role <名前> (protocol 2): ...` を出す。もう一度流しても何も変わらない (試験で確認)。前からある RelayRoleAdvKeys と同じ形。
- **いつ効くか**: 更新の setup は db:prepare (migration) の後にルータの設定を台帳から書き、ルータを再起動する (`write_router_config(update: true)`)。
  よって**別の Apply は要らない** (再起動できなかったと setup が言ったときだけ Apply)。docs/install.md の更新の節に書いた。
- 試験: `test/db/relay_role_app_profiles_test.rb` (新設、6 runs: match・games-host・viewer・該当しないロールは変えない・2 回目は変化なし・
  データベースの行に流してログとルータの ACL で host の `@profile` の queryable・`@pubs` の put と viewer の問い合わせ・答えが通ること)。
  `relay_role_seed_test.rb` は「新しい console の種」と「古い console に migration を順に当てたもの」が同じになることを確かめる (protocol 2 の段を足した)。
  `bin/rails test` は **612 runs, 6558 assertions, 0 failures**、rubocop 違反 0。
- 文書: docs/install.md に「0.3.21 から次のリリースへ」の節 (全部の端末も protocol 2 に、migration が足す行、setup がルータを再起動、確認、戻し方)、
  docs/rooms.md と docs/g1_5.md に「前のロールは更新で行が足される」。版の番号は書いていない (P4 で決める)。
- 戻すとき: 足した行は残る (protocol 1 が使わないものを通すだけ)。ask の `asterism/*/*/@status` は手で戻して Apply (install.md に書いた)。
- 残る注意: 自前のロールで `@status` の行が無いもの (例と違う名前で、端末のアプリが状態やトピックを出すもの) は自動では足されない。
  install.md に手で足す行を書いた。

## 10. P2b-2: 接続のオブジェクトへの入れ替え (asterism-console、2026-11-13)

asterism の P2b-1 (2152661・81b3707、`Asterism.connect` が `Asterism::Connection` を返し、モジュールの関数は無い) に console を合わせた。
asterism-console 3 コミット (7c95f36・783b8b3・b0844f1)。asterism には触っていない (足りない API は無かった)。

### 10.1 入れ替えた所

| 場所 | 前 | 後 |
|---|---|---|
| `lib/bridge/runner.rb` の接続 | `Asterism.connect(...)` の後に `@net = Asterism.net` | `@net = Asterism.connect(...)` |
| 同 見張り | `Asterism.connected?`・`Asterism.lost_reason` | `@net.connected?`・`@net.lost_reason` |
| 同 ノードの自己紹介を聞く | `Asterism.profile_of` | `@net.profile_of` (worker は `@net` が無ければ `Disconnected`) |
| 同 console 自身の自己紹介 | 接続の前に `Asterism.profile(kind: :console, icon:)` | 接続の前に `Asterism.profile_defaults(kind: :console, icon:)` (接続の時の put が 1 回のまま) |
| 同 片付け | `Asterism.close` | `@net&.close` |
| `lib/bridge/objects.rb` (ページの meta・呼び出し) | `Asterism[path, timeout:]` | `Objects.new(net)` の `net[path, timeout:]`。`net` は接続か、接続を返す Proc (bridge はルータを失うと接続を作り直すので Proc を渡す) |
| `lib/bridge/terminal.rb` (Terminal の CLI) | `::Asterism.net` | `Terminal.new(net: -> { @net })` の接続を `CLI.new(net:)` に |
| `lib/bridge/transfers.rb` (ファイルの送り手) | `Transfer::Sender.new(to, ...)` | `Sender.new(to, ..., net: @net)` (P2b-1 で必須になった) |
| `deploy/vps/check_node.rb` | `Asterism.current_call` | 接続の `current_call` |

- **受け口 (`Transfer::Endpoint`) は読み取り用の Zenoh の接続のまま**にした。2 本目の Asterism の接続 (`console/transfer`) で
  `expose` しても同じことができるが、セッションとルータの席が 1 つずつ増え、console の証明書の ACL は今のままで足りるので変える理由が無い。
  docs/files.md に一言書いた。
- 記録・再生・見張りは前から読み取り用の Zenoh の接続 (1 章の「Zenoh を直接使い続けるところ」) で、モジュールの関数を使っていなかった。変更なし。
- console に MCP は無い (CLI の mcp は Terminal では「プログラムとしてだけ動く」と断る、前から)。
- 文書は今の API の箇所だけ直した (README の Terminal の節、docs/terminal.md、docs/files.md、コードのコメント)。古い段の記録
  (docs/g1b.md・v2_5.md・x1.md など) の `Asterism.session` などは当時の記録として触っていない。

### 10.2 観戦 (`spectate`) がアプリとして図に出ること

- 図はアプリを生存の印 `asterism/<node>/<app>` から作る (`Graph#add_asterism`)。`spectate` が 2 本目の接続になれば自分の印を持つので、
  **図のコードは変えずに** `games` ノードの中のアプリとして出る。ノードを運ぶセッションは 2 つになり、`Connections` はノードごとに
  複数のセッションを前から扱える (ROS 2 と同じ)。アプリの自己紹介も `asterism_apps` が印から拾うので、`spectate` の status・pubs・types を
  ほかのアプリと同じに聞く。
- live 試験 (`app_profiles_live_test`) を、子のプロセスが自分でルータ (zenoh-c の router モード) を開き、同じノードの 2 本
  (`livenode/demo` と `livenode/spectate`) をクライアントでつなぐ形にした (前は peer の直結。同じプロセスの 2 本を外から見るにはルータが要る)。
  足した試験: 印が 2 つ、`spectate` の `@profile` から status・宣言・型 (型は接続ごとで `demo` の型は混ざらない)、記録が引く宣言と型、
  図で `a_app:livenode/spectate` が `a_node:livenode` の子で Topics の行を持つこと。ノードの自己紹介を bridge の接続で聞く試験も足した。

### 10.3 台帳の自動の直し (9 章) と `spectate`

**`spectate` のための行は足していない。9 章の migration で足りている**ことを試験で確かめた。

- 2 本目の接続がルータに対してすること (`test/test_helpers/spectate_connection.rb` に一覧): アプリの印、呼び出しの queryable
  (`asterism/<host>/spectate/**`)、生存の印の見張り、重複の確認 (liveliness の get)、ノードの `@profile` の queryable と put と答え、
  アプリの `@profile` の queryable と答え、`@status`・`@pubs` の put、卓の値の put。
- 文書の games-host (protocol 2 の行あり) はこれを全部通す。**protocol 1 の games-host** (`*/@status` と `spectate`・`spectate/**` の行) は、
  migration の前はアプリの `@profile` と `@pubs` だけが通らず、migration の後は全部通る (`*/@profile`・`*/@pubs` は `*` が `spectate` にも
  当たるため)。console と viewer はその `@profile` に問い合わせ、答えと `@pubs` を受け取れる。
- 試験: `test/lib/relay/spectate_roles_test.rb` に 1 本、`test/db/relay_role_app_profiles_test.rb` に 1 本 (どちらも上の一覧で)。
  migration は変えていないので、足すだけ・2 回目は何も変えない、は 9 章の試験のまま。
- docs/rooms.md の「Watching the tables from outside」に、protocol 2 では `spectate` が自分のアプリになること、games-host は今の行で足りること、
  spectator のロールは値だけを読むことを書いた。

### 10.4 試験

| 何を | 結果 |
|---|---|
| 変更前 (asterism が P2b-1 の後) の `bin/rails test` | 612 runs、5 failures・7 errors (objects・terminal_live・app_profiles_live・transfers。どれもモジュールの関数と `Sender` の `net:`) |
| 変更後の `bin/rails test` (`env -u ASTERISM_CREDENTIALS`) | **618 runs、6593 assertions、0 failures、0 errors、0 skips** |
| `app_profiles_live_test` | **4 runs、38 assertions**、3 回流して揺れなし |
| `bin/rubocop` | 312 files、no offenses |
| console 自身の自己紹介 (`own_profile` → 接続) | 手で一度確かめた (kind console、アイコンの hash が付く)。試験のデータベースに残ったアイコンの行は消した |

- `bin/signer` の `TypeError` の出力は前からのもの (5 章)。Docker・VPS には触っていない。

### 10.5 見立てと違った点・残る点

1. 図にも台帳にも `spectate` のための変更は要らなかった (10.2・10.3)。指示書の「要るなら足す」はどちらも「要らない」。
2. ~~止まって返す点~~ → 10.6 で入れた (ユーザが「例の文書と自動の直しの両方に足す」と決めた)。
3. 自前の games-host で `*/@status` の行が無いもの (例と違う形) は、9 章と同じく自動では直らない。`spectate` も同じ。
4. bridge の接続は 1 本のまま。ファイルの受け口を 2 本目の接続にする (10.1) かは、ルータの席が増えるので今は採らない。
5. 版・CHANGELOG は上げていない。push していない。P4 の順序の注意 (8 章) は変わらない: console・ゲームホスト・asterism の protocol 2 の版を
   同時に入れる。

### 10.6 追記: spectator が `spectate` の自己紹介とトピックを読む (2026-11-13)

10.5 の 2 をユーザが「例のロールと更新の自動の直しの両方に足す」と決めた。asterism-console a54cf73。

- **例のロール** (docs/rooms.md の spectator): Read に `asterism/*/spectate/@profile`・`asterism/*/spectate/@pubs`、Ask に
  `asterism/*/spectate/@profile` を足した。答えと `@pubs` の put は Read の行から届く (4 章と同じ)。spectator の証明書の
  `asterism topic list` に卓の状態のトピックが出る。
- **更新の自動の直し**: 新しいデータの migration `db/migrate/20261113200000_relay_role_spectate_profiles.rb` (`RelayRoleSpectateProfiles`)。
  9 章の migration は変えずに別にした (手元のデータベースで流し終えたものは二度流れないため)。`db/schema.rb` の版も合わせた。
  - 規則は 1 つ: Read の行 `<base>/**` で base が `spectate` のアプリで終わるもの (`SPECTATE_READ = %r{\A(.+/spectate)/\*\*\z}`) に、
    Read `<base>/@profile`・`<base>/@pubs` と Ask `<base>/@profile` を足す。**行の前置きはそのロール自身の行から取る**
    (`asterism/games/spectate/**` なら `asterism/games/spectate/@profile`)。部屋の観戦の名前が変わったときに直すのはこの正規表現 1 つ。
  - 足すだけ・2 回目は何も変えない・変えたロールごとに `relay role <名前> (spectate's profile): ...` を出す (9 章と同じ形)。
    players・viewer (`asterism/**` は当たらない)・games-host (spectate の行は rw にある) は変わらない。
- docs/install.md の protocol 2 への更新の節に 2 本目の migration と足す行を書いた。
- 試験: `test/db/relay_role_spectate_profiles_test.rb` (新設、5 runs: spectator の例・前置きを引き継ぐ・該当しないロールは変えない・
  2 回目は変化なし・データベースの行に流してルータの ACL で topic list と同じ操作 (`@profile` の問い合わせと答え、`@pubs` の購読と put) が
  前は全部断られ、後は全部通り、`games` アプリの `@profile`・呼び出し・書き込みは断られたまま)。`spectate_roles_test` の例のロールを新しい行にし、
  1 本足した。**`bin/rails test` は 624 runs、6633 assertions、0 failures、0 errors**、rubocop 314 files 違反 0。
