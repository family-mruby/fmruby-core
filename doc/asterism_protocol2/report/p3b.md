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

## 11. P2c-2: 共有の置き場 `@shared` (asterism-console、2026-11-13)

決定 23〜28 と P2c の 9 章の引き継ぎに合わせた。asterism-console 5 コミット (809b971・dac89ed・a9e1b37・23dd4ea・4e2bc1f)。
asterism・asterism-games・VPS には触っていない。`env -u ASTERISM_CREDENTIALS` で流した。

### 11.1 図

- 共有の置き場は**アプリでなく置き場**として描く。種類 `a_shared` を 1 つ足し、ノードの置き場は `a_shared:<node>` (そのノードの箱の中、
  ラベル `@shared`)、網全体は `a_shared:@shared` (箱の外、ラベル `@shared (network)`)。置き場ごとに 1 つ (キーごとではない)。
- 宣言しているアプリから置き場へ破線 `shares` を引き、線のラベルは置き場の下の名前 (ROS 2 のトピックの線が名前を並べるのと同じ流儀)。
  置き場の詳細は Place・Keys (キーごとに送り手とその宣言・型) と、既存の「Watch its values」(置き場の `**`)。凡例は既存の並びに 1 行ずつ。
  新しい画面・操作は足していない。
- 材料は各アプリの `@pubs` の共有の項目 (`<node>/@shared/<name>`・`@shared/<name>`)。読み分けは `Bridge::Shared` (Puma でも使うので
  gem に頼らない素の Ruby)。アプリの詳細の Topics には共有の項目も今どおり出る (名前は完全な項目名)。
- **送り手が去ったら宣言を落とす** (途中の追加指示): `AppProfiles` が生存の印で今いるアプリを覚え (`due` のたび)、去ったアプリの宣言を消す
  (前から)。加えて、いないアプリの `@status`・`@pubs` の put、問い合わせの答え、問い合わせ直しを**受け付けない**ようにした
  (遅れて届いた put や、去る間際の答えで死んだ送り手の宣言が戻るのを防ぐ。印が見えてからの get で同じものが来る)。図は `a_app` がある
  アプリからしか線を引かないので、印が消えた時点で線も消える。試験: `app_profiles_test` (去ったら `topic` が nil・図に線なし・遅れた
  put と答えは捨てる・戻れば聞き直す)、live 試験 (子のプロセスで `room01` を閉じ、印が消えたら宣言・図の線・置き場の送り手から消える)。
- spectate をアプリと仮定していたものはコードには無かった (10.2 のとおり)。試験の `spectate` アプリ (`test_helpers/spectate_connection.rb`、
  live 試験の 2 本目) は消して、`room01` に置き換えた。

撮影 (見本モード、scratchpad に写した repo と新しい見本のデータベース、終わったら消した):
`/tmp/claude-1000/-home-kishima-fmrb-family-mruby/d865add5-d638-40f7-89da-40dce8f29b87/scratchpad/shots/` の
`p2c2_graph.png` (games の箱の中の `@shared` に games と room01 から線、pc/chat から網全体の置き場へ線)、`p2c2_place_details.png`
(ノードの置き場の詳細)、`p2c2_global_details.png` (網全体の置き場の詳細)。

### 11.2 購読

- `Bridge::Shared.companions`: `asterism/**` には `asterism/*/@shared/**` と `asterism/@shared/**`、`asterism/<node>/**` には
  `asterism/<node>/@shared/**` を**横に足して**購読する。記録 (Recording の key 式) と見張り (Watch) に入れた。それ以外の式 (置き場を
  名指す式、`asterism/*/*/log` など) は足さない。
- console の証明書のルータの規則に `<id>__shared_in` (declare_subscriber と put、置き場 2 つ) と `<id>__shared_out` (put) を足した。
  ingress の put は再生が置き場へ置き直すため (console は前から `asterism/**` を rw で持つので、その延長)。

### 11.3 記録 (MCAP) の共有のキー

| MCAP | 共有の置き場のキー |
|---|---|
| channel | **キー + 送り手**ごとに 1 つ (topic は同じキー) |
| channel の metadata | `asterism_from` = 送り手 `"<node>/<app>"` (attachment の 3 つ目)。`asterism_declaration` はその送り手の宣言 |
| schema | その送り手の `t` と、送り手の `?types` の定義 (同じ型の名前でも送り手ごとに別の schema になりうる) |
| message の sequence | その送り手の通し番号 (送り手ごとに続く) |
| message の publish_time | 送り手の時刻 (送り手の宣言に `ts`)、無ければルータの時刻 |
| 送り手が無い値 | キーだけのチャンネル (`asterism_from` なし、型なし) |

- アプリの下のキーでは attachment の 3 つ目を読まない (決まりどおり)。
- 再生は `asterism_from` のある共有のキーのチャンネルを `[seq, ms または nil, 送り手]` で置き直す (live 試験で受け手の `info.from` と番号を確認)。
- 記録中の「チャンネルごとの数」は topic で合計するようにした (同じ topic のチャンネルが複数になるため。前は上書きで数が欠けた)。
  タイムラインは行と詳細に「from <送り手>」を出す。

### 11.4 ロール

| ロール | 足す・置き換える行 |
|---|---|
| `match` (既定、メンバー) | Read `asterism/@shared/**` (読むだけ。書くのは管理者がロールで許す) |
| `viewer` (Read `asterism/**` を持つもの) | Read `asterism/*/@shared/**`、`asterism/@shared/**` |
| `games-host` (rw `<p>/spectate/**` を持つもの) | rw `<p>/@shared/**`、`<p>/room$*`、`<p>/room$*/**` (ルームのアプリの印・queryable。`@profile`・`@status`・`@pubs` は前からの `*/@profile` などの行で足りる) |
| `spectator` (Read `<p>/spectate/**` を持つもの) | spectate の行 (Read `<p>/spectate`・`/spectate/**`・`/spectate/@profile`・`/spectate/@pubs`、Ask `/spectate/@profile`) を消して、Read `<p>/@shared/spectate/**`、`<p>/games/@profile`、`<p>/games/@pubs`、`<p>/room$*/@profile`、`<p>/room$*/@pubs`、Ask `<p>/games/@profile`、`<p>/room$*/@profile` |

- 新しい console の種 (`db/seeds.rb`) の `match` に `asterism/@shared/**` を入れた。文書の例 (docs/rooms.md・docs/g1_5.md) も同じ行に。
- 今の console: データの migration `db/migrate/20261113300000_relay_role_shared_places.rb` (`RelayRoleSharedPlaces`)。足すだけで、
  消すのは spectator の古い spectate の行だけ (古い形があるときだけ)。2 回目は何も変えない。`relay role <名前> (shared places): ...` を出す。
  `db/schema.rb` の版を合わせた。spectator の判定で、新しい行 `<p>/@shared/spectate/**` を古い形と取り違えないようにした (試験で見つけた)。
- **ロールの行に Zenoh の `$*` を許した** (`RelayRole` の検査。`${...}` と単独の `$` は今どおり断る)。`room$*` のため。ルータの ACL は
  そのまま渡す (zenohd 1.10.1 は chunk の中の `$*` を扱う。試験の評価器も前から扱う)。
- spectator の型の読み方: 送り手 (`games`・`room<slot>`) の `@profile` への Ask と、答えが届くための Read の `@profile`、宣言の `@pubs`。
  `type_of(info)` は送り手を名指す get なので、名前を絞った行で足りる。**網全体の `publishers_of` のような `asterism/*/*/@profile` への
  ワイルドカードの問い合わせは通らない** (spectator には要らない前提)。網全体の置き場は spectator に入れていない (決定 28)。
- 試験: `test/db/relay_role_shared_places_test.rb` (8 runs: 各ロール、前置きを引き継ぐ、該当しないロールは変えない、2 回目は変化なし、
  データベースの行に流して本物のルータの規則生成 `Relay::CloudConfig` で、host の `games`・`room01` の全動作 (`GameHostConnections`)、
  spectator の値と送り手の宣言と型、古い行が消えたこと、メンバーが網全体を読めて書けないこと、viewer が両方を読めること)、
  `spectate_roles_test` を新しい形に書き直し、`relay_role_app_profiles_test` は 2 つの migration を順に当てて host の全動作が通ること、
  `relay_role_seed_test` は種と「古い console に migration を順に当てたもの」が同じこと、`relay_role_templates_test` (`$*`)。

### 11.5 試験

| 何を | 結果 |
|---|---|
| `bin/rails test` (全部) | **647 runs、6876 assertions、0 failures、0 errors、0 skips** (前は 624 runs) |
| `app_profiles_live_test` (CRuby の子プロセスがルータを開き、`livenode/demo` と `livenode/room01` が同じ共有のキーに別の型の定義で put) | **6 runs、60 assertions**、3 回流して揺れなし |
| `bin/rubocop` | 319 files、no offenses |
| 目視 | 11.1 の 3 枚 |

`bin/signer` の `TypeError` の出力は前からのもの。Docker は撮影 (Playwright) だけに使い、`fmruby_*` には触っていない。

### 11.6 未決・確認してほしい点

1. **games-host の行が asterism-games の文書 (作業中、未コミットのものを読んだ) と違う**: games 側は `asterism/${node}/**` と
   `asterism/${node}/@shared/**`、console は `asterism/${node}/room$*`・`asterism/${node}/room$*/**` と `asterism/${node}/@shared/**`
   (ノードの全部ではなくルームのアプリだけに絞った)。どちらでも host は動く。揃える側を親が決める。spectator の行は games 側の
   (Read `@pubs`、Ask `@profile`) に、答えが届くための Read `@profile` を足した形で、games 側にもこの 2 行が要る。
2. 観戦のアプリ名を `games` と `room$*` に決め打ちした (games の作業中の文書と同じ)。名前が変われば migration の `SENDERS` と文書を直す。
3. console の証明書に共有の置き場への put (ingress) を許した (再生のため)。網全体の置き場も含む。
4. メンバーのロールとして網全体の読みを足したのは `match` (名前で) と `asterism/**` を読むもの (viewer) だけ。players の `games` など
   ほかの例のロールには足していない。
5. CLI の `object get/echo`・MCP の `asterism_values` が共有の置き場の値を拾わない件 (P2c の 7 章) は asterism 側で、console は触っていない。
6. 版・CHANGELOG は上げていない。push していない。P4 の順序の注意 (8 章) に、この migration とゲームホストの更新を同時に、が加わる。

## 12. P2d-2: 回の識別子・欠けの数・leave の掃除・ms の時刻 (asterism-console、2026-11-13)

決定 29〜33 と p2d の 10 章の引き継ぎに合わせた。asterism-console 4 コミット (f1a96be・ceb1047・8c73f03・3f24b83)。
asterism・asterism-games・fmruby-core のソース・VPS には触っていない。`env -u ASTERISM_CREDENTIALS` で流した。

### 12.1 attachment の読み方

- `Bag::Channels.attachment(key, bytes)` を `Asterism.read_attachment` で読むようにした (→ `[seq, id, ms, from]`)。置き場はキーから
  `Bridge::Shared.key_place` で決め、ノードの置き場で別のノードの送り手は送り手なし (asterism の読み方のまま)。前の読み方は
  新しい形の `id` を ms と読み違えていた (引き継ぎのとおり)。
- 着手時点で既存の試験が 3 つ落ちていた (asterism 4b96b57 に上がったため): live 試験 2 つ (上の読み違えで送り手と時刻が取れない) と、
  `app_profiles_test` の番号のフィールドの型 (決定 32)。

### 12.2 記録 (MCAP) の回の識別子: 回ごとにチャンネルを分ける

| MCAP | 回の識別子 |
|---|---|
| channel | キー (共有の置き場はキー + 送り手) **+ 回**ごとに 1 つ。topic は同じキー |
| channel の metadata | `asterism_run` = 回の識別子 (10 進の文字列)。識別子の無い値 (protocol 2 の前の形、送り手が付けない値) は付けない |
| message の sequence | その回の通し番号 (回の中で 0 から 1 ずつ) |

- 選んだ理由: MCAP の message には sequence の 1 つしか番号の置き場が無く、回を message ごとに残す所が無い。チャンネルを 1 つに
  まとめたままだと、つなぎ直しで番号が戻り、後で解析する道具 (チャンネルごとに番号の飛びを見るもの) が「戻った・飛んだ」と読む。
  回ごとに分ければ**チャンネル 1 つ = 番号が 1 本の列**になり、番号の飛びがそのまま欠け、再起動は新しいチャンネルとして見える。
  Foxglove などは topic で束ねて見せるので、topic で読む道具からはチャンネルが分かれても同じに見える。共有の置き場で送り手ごとに
  分けたのと同じ流儀で、`asterism_from` の横に `asterism_run` が並ぶだけ。
- 代わりに考えた形: metadata のレコード (回が変わった log_time の表) を別に置く形は、チャンネル 1 つのまま番号が戻るので上の問題が
  残る。message の data に包む形は「payload は受け取ったまま」の決まりを崩すので採らない。
- 回が増えるとチャンネルが増える (再起動 1 回で 1 つ)。記録の上限 (1 時間・512 MB) の中では問題にならない見込み。
- タイムライン: 同じ topic (と送り手) に回が 2 つ以上あるときだけ、行と詳細に「run <id>」を出す (`Bag::View` の `run`、1 つなら nil)。
  新しい画面・操作は足していない。記録中の「チャンネルごとの数」は前から topic で合計しているので変わらない。

### 12.3 再生の回の識別子

- 再生は `[seq, id]`・`[seq, id, ms]`・共有 `[seq, id, ms または nil, 送り手]` で置き直す。
- `id` は**再生が自分で決める** (送り手と記録された回の組ごとに新しい 16 ビットの乱数、再生ごとに新しく)。記録された `id` をそのまま
  使わない理由: 再生するのは console で、送り手の接続ではない (決定 33 の「接続を開くたびに決める」に合わせる)。送り手が今も同じ回で
  動いていると、記録の `id` を名乗った再生の値が送り手の今の列に混ざり、読み手が偽の欠けを数える。新しい `id` なら読み手は別の回と
  読み (`restarted?`)、欠けは数えない。記録の中の回の区切りは、組ごとに別の `id` にするので保たれる。
- 回の識別子の無い記録 (この版より前の protocol 2 の記録) も、送り手ごとに 1 つの `id` を付けて新しい形で置き直す。protocol 1 の記録は
  今どおり attachment なし。

### 12.4 記録の欠け (レビュー 3 章の 6)

- `Bridge::Recorder` が読み手と同じに番号を追う: `Asterism::Sequences` を**購読した key 式ごとに 1 つ**、レーンはキー (共有の置き場は
  キー + 送り手)。同じ回の飛びは `lost` に足し、回が変われば数えない (`restarted`)。
- key 式ごとにしたのは、重なる式 (`asterism/**` と `asterism/<node>/**` を両方選んだ記録) で同じ値が 2 回届くため。1 つの記憶で追うと
  2 回目を「同じ番号 = つなぎ直し」と読み、その番号の分を欠けと数えてしまう。購読ごとの `dropped` の和と同じ数え方になる。
- 記録の `lost` (進み具合と行、表示は今の「lost」の所のまま) = 購読の `dropped` の和 + 番号の欠け。進み具合には内訳の `gaps` と
  新しい回の数 `restarts` も入れた (画面には出していない)。

### 12.5 leave の掃除

- 宣言・型: P2c-2 で `AppProfiles#due` が去ったアプリのものを捨てる形にしてあった (11.1)。今回はそのまま。
- 番号の記憶: `Runner#sync_app_profiles` が前回見たアプリと今のアプリの差 (生存の印が消えたもの) を各記録の `Recorder#forget(app)` に
  渡し、`Sequences#forget` でそのアプリのレーンを捨てる。共有の置き場のレーンは送り手で捨てる。宣言・型と同じ場所 (同じ周期) で
  捨てるので、図・記録の schema・番号の記憶がそろって消える。
- console がほかに送り手ごとの番号の記憶を持つ所は無かった (見張り・グラフ・ログは attachment を読まない)。

### 12.6 時刻 (決定 30)・フィールドの型 (決定 32)

- `bridge/logs.rb`: log の `time` を ms の整数としてそのまま使う (`* 1000` をやめた)。正でない・数でないときは受け取った時刻。
  ほかに時刻を読む所 (`Bag::Decode` のタイムラインの log 行) は同じ関数を通る。console の中の時計 (`Time.now.to_f`) はプロトコルの値ではない。
  文書 (README・docs/v3.md) の log の `time` も ms に直した。
- console は型を作らず、`Asterism::Types.check` で読むだけだった。番号のフィールドを含む型は捨てられる (試験で確認)。直したのは試験の
  見本だけ (`"v", 1` → `"v", "number"`)。

### 12.7 試験

| 何を | 結果 |
|---|---|
| `bin/rails test` (全部) | **651 runs、6908 assertions、0 failures、0 errors、0 skips** (着手時は 647 runs で 3 failures) |
| `app_profiles_live_test` | **7 runs、65 assertions**、3 回流して揺れなし |
| `bin/rubocop` | 319 files、no offenses |

- 足した試験: recorder (回ごとのチャンネル、回の中の欠け、新しい回は欠けなし、key 式ごと、2^31 - 1 から 0 の折り返し、forget の後は
  最初の値、識別子の無い値は metadata なし、タイムラインの run)、runner (行の `lost` に欠けが入る、生存の印が消えたら番号を忘れる)、
  player (記録の回ごとに再生の `id`、同じ送り手の別のキーは同じ `id`)、bag (新しい形の読み、アプリのキーは 4 つ目を読まない、
  別のノードの送り手は無し)。
- live 試験を 1 つ足した: 子のプロセスのアプリ `livenode/quick` が記録中に閉じてすぐつなぎ直し (同じ node/app、新しい回)、
  3 個と 4 個を put。記録は `lost` 0・`gaps` 0・`restarts` 1、チャンネルは `asterism_run` 違いの 2 つ、番号はそれぞれ 0 から 1 ずつ。
- 既存の live 試験の直し: 共有の置き場の再生先を `asterism/console/@shared/...` から送り手のノードの置き場
  `asterism/livenode/@shared/replayed/t1` に移した。asterism がノードの置き場で別のノードの送り手を「送り手なし」と読むようになった
  (レビュー 3 章の 4) ため、前の形では受け手に `from` が来ない。

### 12.8 未決・確認してほしい点

1. **再生の `id` を記録の `id` にしない**こと (12.3)。記録のとおりの `id` を流したい用途 (受け手の回の扱いの再現試験など) があれば
   選べるようにする必要があるが、今は足していない。
2. live 試験は「新しい回の最初に受けた番号が前の回の最後より大きい」場合 (番号だけでは前進と読み違える場合) を確定的には作れない
   (Zenoh が publisher の最初の値を落とすかどうかに任せることになる)。その場合は recorder の単体試験 (`Sequences` を通る) で見ている。
3. 回が分かれたチャンネルの数の上限は置いていない (12.2)。
4. 記録の重なる key 式で同じ値が 2 回書かれるのは前からのまま (欠けの数え方だけを購読ごとにした)。
5. 版・CHANGELOG は上げていない。push していない。

### 12.9 追加: 観戦者が `spectate` オブジェクトを呼ぶ (2026-11-13)

ユーザの決定: games は観戦の値を宣言するアプリ (`<node>/games`・`<node>/room<slot>`) に公開のオブジェクト `spectate` を足し、
`state(table)` が最後の (遅らせた) 観戦の値を返す (決定 31 の取り直し)。観戦者のロールはそのオブジェクト**だけ**を呼べる。
asterism-console 2 コミット (bd40b84・6fedfe8)。

- 呼び出しのキーは protocol.md 4.1 の `asterism/<node>/<app>/<object>/call/<caller>` (呼んだ相手は自分のノード名)。console のロールの
  書き方 (`asterism/*/<app>/*/call/${node}`、docs/g1_5.md) に合わせ、Read と Ask に `asterism/*/games/spectate/call/${node}` と
  `asterism/*/room$*/spectate/call/${node}` を足す (答えは Read の行で届く。`@profile` と同じ形)。メソッド名は payload の中なので、
  ルータでは `state` に絞れない (オブジェクトまで)。
- データの migration `db/migrate/20261113400000_relay_role_spectate_calls.rb` (`RelayRoleSpectateCalls`): Read に
  `<prefix>/@shared/spectate/**` を持つロールに、その前置きで上の 2 行を Read と Ask に足す。足すだけで、2 回目は何も変えない。
  `relay role <名前> (spectate calls): ...` を出す。`db/schema.rb` の版を合わせた。種 (`db/seeds.rb`) には spectator が無いので変えていない。
- 文書: docs/rooms.md の spectator の例と説明 (meta は読まない、ほかのオブジェクトは呼ばない)、docs/install.md の更新の migration (4 つ目)。
- **console の呼べるメソッドの許可表 (`CallPermission`) は関係しない**: あれは console の画面 (グラフのページ) が console の
  接続で呼ぶものの表で、観戦者の証明書の呼び出しはルータの ACL だけが見る。行は足していない。console の画面から `spectate.state` を
  呼びたいなら、管理者が表に行を足す (今は要らない)。
- 試験 (`Relay::CloudConfig` を通す):
  - `test/db/relay_role_spectate_calls_test.rb` (5 runs): 足す行、前置きを引き継ぐ、該当しないロールは変えない、2 回目は変化なし、
    データベースの行に流して、観戦者が games と room01 の `spectate` を自分の名前で呼べて答えが届く、別の名前・ワイルドカードの呼び手・
    ほかのノードのアプリは不可、卓のオブジェクト (`mahjong1`)・`lobby`・`spectate/meta` は不可、queryable は立てられない、ホストが
    受けて答えられる。
  - `spectate_roles_test` に観戦者の呼び出しの試験を 1 つ足し、ロールの例を新しい行に。座席の証明書は呼べない。`GameHostConnections` に
    ホストの `spectate` の呼び出しを受けて答える 4 動作を足した。
  - `bin/rails test` (全部) **657 runs、6973 assertions、0 failures、0 errors、0 skips**、`bin/rubocop` 321 files、no offenses。
- 気づいたこと: プレイヤーのロール `games` (`asterism/*/games/*/call/${node}`) は前から games アプリの全部のオブジェクトを呼べるので、
  `games/spectate` も呼べる (room の `spectate` は呼べない)。ゲーム中のプレイヤーが遅らせた観戦の値を読めるだけで、卓の値より
  知ることは増えない見込みだが、気になるなら games 側の公開の範囲と合わせて決める。
