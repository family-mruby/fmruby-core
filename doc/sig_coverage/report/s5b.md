# S5b Asterism の残り・ROS 2 の橋・卓のゲームの部品を sig に書く

> 2026-11-25 | サブ (Sonnet) の報告。指示書は instruction_s5b.md。ブランチ `sig-s5b`。

## 1. 結果

指示書の 4 つのまとまりと、既存の直しを全部済ませた (残りのまとまりは無い)。
`rake sig:coverage` の差は `app_class -1, app_constant -1, app_method -5, app_name -16, impl_ruby -231` (基準との差。基準は別コミットで更新済み)。
sig は 104 → 125 クラス、1244 → 1470 メソッド。書き方の欠け (「3. doc gaps」) は 0 のまま。

**先に書く注意**: 型 db のシグネチャの領域が 48,927 → 58,256 バイトになった (旧い上限 65,535 の 89%)。指示書のとおり
今は 24 ビットなので上限ではないが、旧い上限で見ていた所があれば要確認。クラスの数は 126 (上限 255 まで余裕あり)。

| クラス | 書いた数 | ファイル |
|---|---|---|
| `Asterism` (モジュールの関数) | 6 (`connect`・`connections`・`poll_all`・`profile_defaults`・`default_icon_path`・`read_attachment`) + 定数 5 | `sig/fmrb_asterism.rbs` |
| `Asterism::Connection` | 29 (既存 13 は直しだけ) | 同上 |
| `Asterism::Topic` | 9 | 同上 |
| `Asterism::Subscription` | 4 | 同上 |
| `Asterism::SampleInfo` | 9 | 同上 |
| `Asterism::Future` | 4 | 同上 |
| `Asterism::Proxy` | 3 | 同上 |
| `Asterism::CurrentCall` | 1 | 同上 |
| `Asterism::RemoteError` | 1 (`.new`) | 同上 |
| `Asterism::Credentials` (+ `Credentials::Error`) | 12 | 同上 |
| `Asterism::Types` | 2 + 定数 7 | 同上 |
| `Asterism::Pubs` | 7 | 同上 |
| `Asterism::Sequences` | 5 | 同上 |
| `Asterism::Status` | 2 + 定数 2 | 同上 |
| `Asterism::Profile` | 2 | 同上 |
| `Asterism::SharedSession` | 1 | 同上 |
| `FmrbAsterism` (モジュールの関数) | 10 + 定数 4 | 同上 |
| `FmrbAsterism::Settings` | 2 (`credentials`・`zenoh_config`) | 同上 |
| `FmrbAsterism::Link` | 定数 2 のみ (メソッドの欠けは内側 7 だけ) | 同上 |
| `Asterism::ROS` (モジュールの関数) | 4 (`require_type`・`type_path`・`field_types`・`resolve`) + 定数 2、例外 2 (`TimeoutError`・`UnknownType`) | `sig/asterism_ros.rbs` (新) |
| `Asterism::ROS::Node` | 18 | 同上 |
| `Asterism::ROS::Publisher` | 7 | 同上 |
| `Asterism::ROS::Subscription` | 11 | 同上 |
| `Asterism::ROS::Service` | 7 | 同上 |
| `Asterism::ROS::Client` | 10 | 同上 |
| `Asterism::ROS::Call` | 6 | 同上 |
| `Asterism::ROS::Timer` | 4 | 同上 |
| `Asterism::ROS::Attachment` | 7 | 同上 |
| `Asterism::ROS::Message` | 6 | 同上 |
| `FmrbAsterism::TableClient` | 37 (うち書き込みの accessor 7) + 定数 7 | `sig/fmrb_table_client.rbs` (新) |

足した sig のファイル: `sig/asterism_ros.rbs`・`sig/fmrb_table_client.rbs`。領域分け (`tools/ti/gen_site.rb` の `AREAS`) に入れるのは親
(`asterism_ros` と `fmrb_table_client` は `fmrb_asterism` の隣、「ネットワーク」が自然)。

素直 (例だけ) とつまずく (型紙の順) の内訳:

- 例だけ (読み取り・1 行で足りる): 約 95。`Connection#app`・`node_id`・`exposed`・`published`・`lost_reason`・`unsubscribe`・`off_*`、`Topic`・`Subscription`・`SampleInfo`・`Future` の読み取り、
  `Credentials` の読み取り (`path`・`kind`・`ca_pem` ほか)、`Asterism::ROS` の各読み取り (`name`・`domain`・`topic_key`・`gid` ほか)、`Timer`・`Call` の読み取り、`TableClient` の読み取りと accessor。
- つまずく (型紙の順。失敗・呼べる場面・待ち・既定値を書いた): 残り (約 130)。`Asterism.connect`・`Connection#poll`・`session`・`reconnecting?`・`profile`・`profile_of`・`protocol_of`・`define_type`・`pubs_of` ほか、
  `publishers_of`・`declaration_of`・`type_of`、`Credentials.load`、`Pubs.place`・`check`、`Sequences.see`、`Node` の作る・受ける・`call`・`every`・`service`、`Client#call`、`Message.from`、
  `TableClient` の `new`・`update`・`update_view`・`join`・`leave`・`forget`・`ask_view`・`proxy`、`FmrbAsterism.config`・`write_config`・`save_name`・`credentials_for`。

卓の部品の「アプリが持つ (呼ばれる側の) メソッド」(`table_state` ほか 13 個) は、**クラスの長文に必須 / 無くてよいを分けて並べ**、`TableClient.new` の長文から参照した。
型 (メソッドの宣言) にはしていない (実装にそのクラスのメソッドとして存在しないため。迷ったもの参照)。

## 2. 直した既存のシグネチャ (決定 22)

前 → 後。根拠は実装の `def` の行。要約 (1 行目) は変えていない。長文には省いたときの値と新しく出したキーワードを足した。

| メソッド | 前 | 後 | 根拠 |
|---|---|---|---|
| `Link.new` | `(FmrbApp owner, String app)` | `(FmrbApp owner, String app, ?tag: String?, ?retry_ms: Integer?, ?wifi_wait_ms: Integer, ?screen_wait_ms: Integer?, ?settings: FmrbAsterism::Settings?)` | link.rb:75 (`initialize`)、既定 60-61 |
| `Connection#expose` | `(String name, untyped obj, untyped methods)` | `(String name, untyped obj, methods: (Array[untyped] \| Hash[untyped, untyped]), ?caller: (Symbol \| String)?, ?doc: String?)` | asterism.rb:865 (`methods:` は必須キーワード) |
| `Connection#[]` | `(String path)` | `(String path, ?timeout: (Integer \| Float)?, ?timeout_ms: Integer?)` | asterism.rb:1306 (位置引数の ms は非推奨で書かない) |
| `Connection#each` | `(String pattern) -> Array[Proxy]` | `(?String pattern) -> Array[Proxy]` と `(?String pattern) { (Proxy) -> untyped } -> Integer` | asterism.rb:1283 (既定 `"**"`、ブロックは数を返す) |
| `Connection#apps` | `(String pattern)` | `(?String pattern)` | status.rb:169 (既定 `"*/*"`) |
| `Connection#publish` | `(String name)` | `((String \| Symbol) name, ?type: (Symbol \| Integer \| String)?, ?unit: String?, ?desc: String?, ?ts: bool, ?encoding: String?, ?shared: (Symbol \| String)?)` | topic.rb:768 (`check_topic_name` が Symbol も受ける: topic.rb 末の `check_topic_name`) |
| `Connection#subscribe` | `(String path)` | `(String path, ?depth: Integer?) { (untyped value, SampleInfo info) -> untyped }` | topic.rb:1180 (ブロックが無いと ArgumentError) |
| `Connection#status` | `(Hash status) -> Hash` | `(?Hash status, **untyped fields) -> Hash?` | status.rb:115 (引数なしは nil があり得る) |
| `Connection#status_of` | `(String node, String app)` | `(String node, String app, ?timeout: (Integer \| Float)?, ?timeout_ms: Integer?)` | status.rb:138 |
| `Connection#on_join`・`on_leave` | `() -> Connection` | `() { (String node) -> untyped } -> Connection` | asterism.rb:947・953 (ブロックが無いと ArgumentError) |
| `Proxy#asterism_call` | `(String name)` | `((String \| Symbol) name, *untyped args, **untyped kw)` | proxy.rb:41 |
| `AsyncProxy#asterism_call` | `(String name)` | 同上 | proxy.rb:124 |

`FmrbAsterism.host`・`settings`・`Settings` の 4 つ・`Link` のほかのメソッド・`Topic#put`・`Subscription#close` ほかは実装と合っていた (変更なし)。

## 3. 根拠の表

パスは `fmruby-core/` から。Asterism の gem は `vendor/asterism/mrblib/`、FmrbAsterism は `lib/gems/picoruby-fmrb-asterism/mrblib/`。
`@api private` の印のあるメソッドは書いていない (数える道具が読んで除く)。

### sig/fmrb_asterism.rbs: Asterism の核

| 書いたもの | 実装 | 失敗・呼べる場面の根拠 |
|---|---|---|
| `Asterism.connect` | asterism.rb:296 | Disconnected / Error (同じ node/app): 296 直前のコメント。`connect_timeout:` は CRuby のみ (同コメント)。`check_timeout` の既定 1.5 秒: asterism.rb の `CHECK_TIMEOUT`、待ち: `open_checked` 656-675・`already_there?` 723-735 |
| `Asterism.connections`・`poll_all` | asterism.rb:305・311 | |
| `Asterism.profile_defaults`・`default_icon_path` | profile.rb:234・301 | ArgumentError: `Profile.fields!` (profile.rb 369 以降) |
| `Asterism.read_attachment` | topic.rb:525 | |
| 定数 `PROTOCOL` ほか | asterism.rb の `ROOT`・`PROTOCOL`・`DEFAULT_TIMEOUT(_MS)`・`CHECK_TIMEOUT` | |
| `Connection#app`・`node_id`・`run_id` | asterism.rb:626 | |
| `connected?`・`reconnecting?`・`lost_reason` | asterism.rb:738・755・767 | 閉じていたら lost にして false: 738-745 |
| `close` | asterism.rb:775 | 何度でも・閉じたら再びは開かない: コメント 771-774 |
| `poll` | asterism.rb:928 | 待ちの中からは呼ばない: on_join のコメント 940-945 |
| `session` | asterism.rb:820 | Disconnected: 821。close が無視される: `SharedSession` 508-515 |
| `exposed`・`unexpose` | asterism.rb:913・907 | |
| `published`・`unpublish` | topic.rb:858・836 | |
| `unsubscribe` | topic.rb:1200 | |
| `off_join` ほか 4 つ・`on_app_join`・`on_app_leave` | asterism.rb:962-997・980-986 | ブロック無しは ArgumentError: 981・987 |
| `profile`・`profile_of`・`protocol_of` | profile.rb:406・418・458 | ArgumentError: `Profile.fields!`。profile_of が Disconnected を再送出: 424-428 |
| `define_type` | topic.rb:931 | ArgumentError: `Types.definition!` (topic.rb 89-119)・`field_type!` |
| `pubs_of`・`types_of` | topic.rb:945・954 | nil / {} / Disconnected: `app_profile_part` (topic.rb 1130 付近) |
| `publishers_of` | topic.rb:968 | ArgumentError: 969-972。待ち: `wait_until` 985-997 |
| `declaration_of`・`type_of` | topic.rb:1020・1034 | 覚える: `sender_cached`・`sender_store` |
| `Topic` の読み取り | topic.rb:292 (`attr_reader`)・`declaration`・`seq`・`ts?`・`closed?` | `SEQ_MAX` 2147483647: topic.rb 285-287 |
| `Subscription#path`・`connection`・`dropped`・`malformed` | topic.rb:544・558-575 | |
| `SampleInfo` の読み取り・`restarted?`・`to_h` | topic.rb:404・425-436 | restarted と lost: コメント 391-402。`Sequences#see` 461-478 |
| `Future#path`・`method_name`・`took`・`raw_value` | future.rb:7・44・71 | TimeoutError / Disconnected / Error: 72-85 |
| `Proxy#asterism_connection`・`asterism_meta`・`asterism_refresh` | proxy.rb:27・73・78 | meta の失敗: asterism.rb:1348-1369 |
| `CurrentCall#verified` | asterism.rb:113 | 値 `:claimed`・`:local`: コメント 104-111 |
| `RemoteError.new` | asterism.rb:97 | |
| `Credentials` 全部 | credentials.rb:31-150 | `Error`: 24。1 MB まで: `MAX_SIZE`・`read_tar` 174-185。鍵が 2 つ・無い: 130-138 |
| `Types.number`・`check` | topic.rb:59・138 | |
| `Pubs.*` 7 つ | topic.rb:181-273 | |
| `Sequences.new`・`see`・`forget`・`size`・`clear` | topic.rb:453・461・482・494・498 | 既定 `lanes_max`: 506 |
| `Status.check`・`parse` | status.rb:31・45 | |
| `Profile.png_info`・`check` | profile.rb:50・117 | |
| `SharedSession#close` | asterism.rb:508-515 | 警告 1 回: `warn_once` |

### sig/fmrb_asterism.rbs: FmrbAsterism

| 書いたもの | 実装 | 根拠 |
|---|---|---|
| `config`・`read_config`・`write_config` | fmrb-asterism.rb:74・105・123 | 昔のファイルの引っ越し: `config` 74-78・`migrate` 144-163。TOML でない: 105 のコメント。ファイル全体を置き換える: `config_text` 131-141 |
| `saved_name`・`save_name` | fmrb-asterism.rb:80・89 | ArgumentError: profile_defaults が先に走る 90-91 |
| `hostname`・`system_conf` | fmrb-asterism.rb:210・191 | |
| `tls?`・`credentials_path`・`credentials_for` | fmrb-asterism.rb:290・295・311 | Asterism::Error: 314 付近。2 つ以上でログ: 299-301 |
| 定数 `CONFIG_FILE`・`DIR`・`DEFAULT_ROUTER`・`SYSTEM_CONF` | fmrb-asterism.rb:52-62 | |
| `Settings#credentials`・`zenoh_config` | fmrb-asterism.rb:220・232 | |
| `Link` の定数 | link.rb:60-61 | |

### sig/asterism_ros.rbs

| 書いたもの | 実装 | 根拠 |
|---|---|---|
| `ROS.require_type`・`type_path`・`field_types`・`resolve` | ros.rb:218・203・278・88 | ArgumentError: `check_type_name`・UnknownType: 201・228-231 |
| `Node.new` | ros.rb:414 | 名前の ArgumentError: 417。liveliness を出す: 429-430 |
| `Node#poll` | ros.rb:515 | `session.poll` は zenoh.c:2129-2147 (steps の意味) |
| `publisher`・`subscription`・`service`・`client`・`subscribe`・`every`・`cancel_timer`・`call`・`close`・`closed?` と読み取り | ros.rb:433-470・482・491・500・585・410 | `service` のブロック無しは ArgumentError: 450。`every` の ArgumentError: 483-485。`subscribe` が @handled に入れる: 471-475 |
| `Publisher#publish`・`<<`・`close` と読み取り | ros.rb:629-650・616 | ClosedError: 630 |
| `Subscription` (ROS) | ros.rb:653-712 | `each_pending` の形: 669-687。`handler=`: 655 |
| `Service#handle_pending`・読み取り | ros.rb:776-793・758 | 例外の扱い: 776 のコメントと 782-791 |
| `Client#call`・`call_async`・読み取り | ros.rb:946・928・910 | 待つ間の pump: `wait_for` 975-980 |
| `Call` | ros.rb:830-902 | TimeoutError / ClosedError: 861-873 |
| `Timer` | ros.rb:713-749 | |
| `Attachment`・`Message` | ros.rb:155-196・341-408 | `from` の TypeError: 345-350 (`fields_of`) |

### sig/fmrb_table_client.rbs

| 書いたもの | 実装 | 根拠 |
|---|---|---|
| `TableClient.new` | table_client.rb:91 | 既定の `app: "game"`・`host_app`・`retry_ms: 3000`・wifi の既定: 128-132 |
| `update`・`update_view`・`close` | table_client.rb:142・154・160 | |
| `net`・`reconnecting?`・`joining?`・`host_gone?`・`conn`・`link`・`kind`・`tables`・`table_list`・`table`・`token`・`want_v`・`view_v` | table_client.rb:78・168-190 | |
| `proxy` | table_client.rb:192 | Disconnected: 194。道が無いと ArgumentError は `split_path` (asterism.rb:240) から |
| `join`・`leave`・`forget`・`ask_view`・`refresh_view` | table_client.rb:211・223・233・243・254 | nil / エラー文: 211-221。捨てる答え: 冒頭コメント 56-58 |
| accessor 7 つと定数 7 つ | table_client.rb:70-90 | |
| 持つメソッド (`table_state` ほか) | table_client.rb:28-54 (冒頭のコメント) と 128-137 (`respond_to?` で呼ぶ・呼ばない)、呼ぶ所: 302-338・393-440 | |

使われ方の根拠: `flash/app/game/{board,mahjong,othello}/*.app.rb` が呼ぶ `@tc.*` を grep し、使われているものはすべて書いた
(`table`・`table_list`・`token`・`proxy`・`link`・`net`・`tables`・`reconnecting?`・`ask_view`・`update_view`・`update`・`refresh_view`・`leave`・`joining?`・`join`・`close`・`host_gone?`・`want_v`・`view_min_ms=`・`forget`・`conn`)。

## 4. 載せない候補 (内側。親が ignore.yml に入れる)

| クラス | メソッド | 理由 |
|---|---|---|
| `Asterism` | `.const_missing` | 古い名前 `Asterism::Timeout` の警告つき別名 (asterism.rb:134) |
| `Asterism::Future` | `.new` | `Connection` が作る (`call_async`・`app_profile_async`)。アプリは作らない |
| `Asterism::Proxy` | `.new`・`methods` | `.new` は `Connection#[]`。`methods` は非推奨で `remote_methods` を返すだけ (proxy.rb:65-69) |
| `Asterism::AsyncProxy` | `.new` | `Proxy#async` が作る |
| `Asterism::CurrentCall` | `.new` | `dispatch` が作る |
| `FmrbAsterism::Settings` | `.new` | `FmrbAsterism.settings` が作る |
| `FmrbAsterism` | `.config_text`・`.first_line`・`.migrate`・`.read_file`・`.router_unset?`・`.strings_of`・`.warn_old_files` | `config`・`read_config`・`write_config`・`settings` の下回り (昔のファイルの引っ越しと読み書きの部品) |
| `FmrbAsterism::Link` | `close_session`・`connect`・`now_ms`・`poll`・`prepare`・`tell_down`・`wifi_ready?` | link.rb:151 の「inside」の節。`update` が呼ぶ |
| `FmrbAsterism::TableClient` | `add_table`・`collect_join`・`collect_view`・`find_tables`・`host_of`・`ours?`・`view_due?`・`link_up`・`link_down`・`node_joined`・`node_left`・`state_came`・`subscribe_states`・`watch_states` | `update`・`update_view`・Link の呼び先 (`link_up`・`link_down`) の下回り。アプリが呼ばず上書きもしない (table_client.rb:289 の「inside」の節ほか) |
| `Asterism::ROS` | `.const_missing`・`.fields_of`・`.gid_for`・`.load_type_file`・`.mangle`・`.now_ms`・`.now_ns`・`.pause`・`.strip_slashes`・`.type_constant`・`.type_of` | 古い名前の別名・キーの組み立て・時計・型の読み込みの下回り (`require_type` と `Node` が使う) |
| `Asterism::ROS::Publisher`・`Subscription`・`Service`・`Call` | `.new` | `Node`・`Client` が作る |
| `Asterism::ROS::Client` | `.new`・`decode_response` | `Node#client` が作る。`decode_response` は `Call` が呼ぶ |
| `Asterism::ROS::Timer` | `due?`・`fire` | `Node#poll` の下回り |
| `Asterism::ROS::Node` | `fire` | 同上 |
| `Asterism::ROS::Attachment` | `.le_int` | `decode` の下回り |
| `Asterism::ROS::Message` | `.plain` | `to_h` の下回り |
| 全体 | `inspect`・`to_s` の上書き | Object と同じ形 (道具が数えていれば) |

### 迷ったもの (書いていない。親が決める)

- **`TableClient` が呼ぶアプリ側のメソッド (`table_state` ほか 13 個) を型にするか**: 今は長文に並べただけ。型にするなら、実装に無いクラス
  (`FmrbAsterism::TableOwner` のような) を sig にだけ作る話になるので、「実在するメソッドだけ書く」の決まりに当たる。
- **`TableClient#host_of`・`ours?`・`view_due?`**: public で、アプリも呼べる。同梱のアプリは呼ばないので載せない候補に置いた。必要なら短い例だけで足せる。
- **`Asterism::ROS` の `now_ms`・`pause`・`now_ns`**: アプリが時計の代わりに呼べるが、`Machine` と `Time` の方が普通。
- **`Asterism::Zenoh` (C のバインディング)**: `Node.new` の session、`Connection#session` の戻り、`SampleInfo#timestamp` の型として出てくるが、sig には無い (`untyped` にした)。
  C の `Asterism::Zenoh::Session` を書くかは別の話 (数える道具の C の表には出てこなかった)。
- **`Asterism::CDR::DecodeError`**: `Message.decode` の失敗の名前を長文に書いたが、クラスは ignore.yml の `Asterism::CDR` の中。
- **`Connection#publish` の `type:` の型**: 組み込みの型 (Symbol・Integer) と自分の型 (String) の 3 種。選択肢のリテラル型にはしていない (`Types.number` が Symbol を取る)。

## 5. アプリが使っているのに実装に見つからなかったもの

この範囲では無い。`FmrbAsterism::TableClient`・`FmrbAsterism.hostname`・`read_config`・`system_conf`・`CONFIG_FILE`・`Asterism.profile_defaults` は全部実装があり、sig に書いた。
数える道具の「アプリが使う未定義」で Asterism 関係はすべて消えた (残りは `BMI270`・`MbedTLS`・`Net::HTTP`・`FmrbGfx#draw_text_mixed`・`FmrbApp._get_last_error` ほか、この一括の範囲外)。

## 6. 数える道具の前後と型 db の大きさ

- `rake sig:coverage` 前 → 後: sig 104 クラス・1244 メソッド → 125 クラス・1470 メソッド。アプリが使う未定義: クラス 11 → 10 (15 → 12 回)、定数 6 → 5、受け手のわかるメソッド 13 → 8 (24 → 16 回)、型の決まらない名前 60 → 44 (186 → 91 回)。
  実装にあって sig に無い: Ruby 293 → 62 (34 → 22 クラス)、C 15 のまま。基準との差 `app_class -1, app_constant -1, app_method -5, app_name -16, impl_ruby -231`。基準は更新済み。
  書き方の欠け 0 のまま (要約 111 バイト超 0・`<<en>>` 無し 0・長文なし 0・例なし 0・強調が行をまたぐ 0)。
- 型 db (tidbgen を最初の sig と今の sig で別々に走らせた。`sig/` のみ、試験用の重ね合わせなし):

| 領域 | 前 | 後 | 差 |
|---|---|---|---|
| 名前 | 13,045 | 15,359 | +2,314 |
| シグネチャ | 48,927 | 58,256 | +9,329 |
| 要約 (文書) | 92,742 | 111,805 | +19,063 |
| メソッドの数 | 4,319 | 5,156 | +837 |
| 引数の数 | 3,675 | 4,354 | +679 |
| クラスの数 | 105 | 126 | +21 |
| 定数の数 | 249 | 278 | +29 |
| 型の和 (union) の数 | 29 | 44 | +15 |

(前の値は S5a の報告とずれる: S5a の db は試験用の重ね合わせあり、こちらはなし。)

## 7. 確認の結果

- `rake ti:test`: 通った (期待値の直しは不要)。
- `rake regress`: 通った (host 34 suites 全部 pass、描画の記録 310 runs pass)。
- `rake sig:coverage`: 上のとおり。ビルドと sim は流していない。`ruby tools/boundary/gen.rb` は流していない (境界の印を付けたクラスは足していない)。

## 8. 親が sim で確かめるとよい項目

シェルの irb か、エディタの行 (Ctrl+E で指摘されるか)。相手の端末が要るものは「要」と付けた。

1. `FmrbAsterism.tls?("tls/x:7447")` → true
2. `FmrbAsterism.credentials_for("tcp/x:7447")` → nil
3. `FmrbAsterism.credentials_for("tls/x:7447")` → Asterism::Error (.tar 無し)
4. `Asterism::Types.number(:vector3)` → 2
5. `Asterism::Pubs.place("@shared/lobby")` → [:global, nil, "lobby"]
6. `Asterism::Pubs.encoding_id("image/jpeg")` → 17
7. `Asterism::ROS.resolve("a", "/ns")` → "/ns/a"
8. `Asterism::ROS.require_type("std_msgs/msg/Foo")` → ROS::UnknownType
9. `Asterism::Profile.png_info("x")` → nil
10. エディタ: `@link.conn.status(lines: 3)` → 指摘なし
11. エディタ: `@link.conn.expose("b", self)` → 指摘あり (methods: が無い)
12. エディタ: `@link.conn.each { |p| p }` → 指摘なし
13. エディタ: `FmrbAsterism::Link.new(self, "x", wifi_wait_ms: 0)` → 指摘なし
14. エディタ: `@tc = FmrbAsterism::TableClient.new(self, "othello")` → 指摘なし
15. 要: sim に talk_board を起こし、`@link.conn.expose("board", self, methods: [:post])` が通ること (つなぐ所まで)

## 9. 読んでいて見つけた実装の不具合の疑い (直していない)・確かめていないこと

- **`Asterism::ROS::Subscription#handler=` だけでは Node#poll に拾われない** (ros.rb:655・471-475): `@handled` に入るのは `Node#subscribe` が作ったものだけ。`node.subscription` で作ったものにあとから
  `handler=` で付けても呼ばれない。仕様だとしても紛らわしいので、長文に「node.subscribe で作ったものだけ」と書いた。
- **`Connection#each` のブロックの戻り** (asterism.rb:1283 のコメント): 「1.0 で接続を返すようになる」予定がコメントにある。今はブロックの個数 (Integer) を返すので、そう書いた。1.0 で変わる。
- **`TableClient#leave` の戻り**: `forget` の最後の式 (`@refetch = false`、つまり false) がそのまま返る。`void` と書いた。
- **`TableClient#join` はつながっていないと `proxy` が Disconnected を出し**、`rescue ::Asterism::Error` で文字列を返す (table_client.rb:211-221)。つながる前に呼んでも例外にはならず文が返る。
- 確かめていない: (1) `Asterism::ROS` が sim・実機・ブラウザ版の build に入るか (`family_mruby_*.rb` は `picoruby-asterism` を入れており、`ros.rb` は同じ mrblib なので入るはずだが、型のファイルは `/usr/share/asterism/msgs` が storage に入っている前提)。
  (2) ROS の相手 (rmw_zenoh の ROS 2 側) との通しは見ていない。ルータの共有と rmw_zenoh の版はコメント (ros.rb:25-30) の読み。
  (3) `Asterism.connect` の `mode:` に String を渡せるか (C の `Session.open` が Symbol だけか): `Symbol?` と書いたが C は読んでいない。
  (4) `Pubs.entry` ほかの純関数の戻りの例 (`#=>` の値) は実装を読んで書いたもので、mruby では動かしていない (親が上の 4〜9 で確認)。
  (5) `Credentials` の `warn:` は端末では何もしない (`check_mode` は空: credentials.rb:168) と読んだ。CRuby 側の `lib/asterism/cruby/credentials.rb` は読んでいない。
- 迷った所: `Connection#profile` の戻りは String キーの Hash。`FmrbAsterism.save_name` の戻りも同じ Hash (`Asterism.profile_defaults` の答え)。型は `Hash` のまま (キーと値の型は書いていない)。

## 10. コミット

ブランチ `sig-s5b`、`git log 385b20a6..sig-s5b` で見える。

- `04768dcd` fix(sig): Asterism signatures show the optional arguments and keywords as the code takes them
- `4320e5e9` feat(sig): the rest of the Asterism gem (connection, topics, credentials, profile, status, types)
- `0f15b0d2` feat(sig): the ROS 2 bridge (Asterism::ROS node, publisher, subscription, service, client, message)
- `cd094700` feat(sig): FmrbAsterism settings functions, Settings#credentials and the Link defaults
- `608174f3` feat(sig): FmrbAsterism::TableClient, the device side of a game host's table
- `9ee0d5cf` fix(sig): keep code examples of the Asterism signatures within 39 columns and state when lost is 0
- `55bf7290` fix(sig): TableClient#close does not leave the table
- `ff4596d4` test(sig_coverage): baseline after the Asterism, ROS 2 bridge and table client signatures
- `docs(sig_coverage)`: この報告 (と `doc/README.md` の索引)
