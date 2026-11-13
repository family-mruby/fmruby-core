# P2d report: レビューの直し (asterism、P2d-1)

> 状態: 済 (P2d-2 への引き継ぎあり) | 更新: 2026-11-13 | asterism に決定 29〜33 とレビュー 3 章の直しを入れた。接続ごとの回の識別子 (`info.run_id`・`net.run_id`) を全部の値の attachment に載せ、番号は接続ごと、アプリの leave で読み手が番号・宣言・型の記憶を捨てる。番号の記憶に上限、`type_of`・`declaration_of` は送り手ごとに覚える、ノードの置き場で別のノードの送り手は送り手なし、CRuby の接続は網の重複の確認をロックの外で。フィールドの型は名前だけ。`net.protocol_of` と、別の protocol のノードの警告 (断らない)。protocol.md・README・適合の見本 (版 7)・試験。asterism 4b96b57・35eafab。asterism-games・console・fmruby-core は触っていない (P2d-2)

## 1. 回の識別子 (決定 33)

- 接続を開くたびに 16 ビットの乱数を決め (`Asterism.new_run_id`)、その接続の全部の値の attachment に載せる。
  - アプリのトピック: `[seq, id]`、`ts` のとき `[seq, id, ms]`。
  - 共有の置き場: いつも 4 要素 `[seq, id, ms または nil, "<node>/<app>"]`。
- 名前: Ruby では `info.run_id` (受け取った値の送り手の回)、`net.run_id` (自分の接続の回)。決まりの文書では attachment の要素を
  `id`、言葉は「run id」。`info.run` は CRuby の `Connection#run` (受信のループ) と同じ語になるので避け、`run_id` にそろえた。
  CLI・MCP の JSON の項目も `run_id`。
- 読み手: `id` が前と違えば新しい回 (`restarted?` true、`lost` 0)。同じ (どちらも無いを含む) なら今までどおり距離で読む。
  `id` が整数でない値は「回の識別子なし」として距離だけで読む (時刻が整数でなければ時刻なしと同じ流儀。下の 9 章の確認してほしい点 1)。
- 乱数の出どころ: `Kernel#rand(65536)`。
  - CRuby: 標準の `rand`。
  - 端末 (Family mruby): `picoruby-rng` が gembox に入っていて (`lib/add/family_mruby.gembox`)、`Kernel#rand(max)` が
    `RNG.random_int % max`。ESP32 では `esp_random()` (ハードウェアの乱数)、POSIX (ネイティブ版・sim) とブラウザ版は
    `/dev/urandom` (emscripten は crypto の乱数に置き換える)。時計の無い端末が毎回同じ値で始まる心配は無い。
  - mruby の試験のビルド: `mruby-random` を host_test/build_config.rb に足した (端末の `rand` の代わり)。
  - `rand` の無い VM: `NoMethodError` を拾い、時計の ms と接続の object_id を混ぜた値にする (逃げ道。今の端末では通らない)。
- `Machine` には乱数が無い (delay_ms・board_millis だけを使っている)。

## 2. 番号は接続ごと・leave で捨てる (決定 29)

- 送り手: トピックの番号は接続のもの。同じ接続で同じ名前を `publish` し直すと前の番号を引き継ぐ (取り下げた後に宣言し直しても、
  接続の表 `@seq_of` から引き継ぐ)。**同じ宣言・同じ encoding で宣言し直すときは publisher を作り直さず使い続ける** (前の Topic は
  閉じる)。zenoh-pico で publisher を作り直すと最初の値を落としうるため。宣言が違えば今までどおり作り直す。
- 読み手の記憶を `Asterism::Sequences` に切り出した (レーン = キー、共有の置き場ではキー + 送り手 → `[id, seq, アプリ]`)。
  `Subscription`・CLI の `Values::Seq` (ロックで包む)・(P2d-2 で) console が同じものを使える。
- アプリの生存の印の leave (`follow_liveliness` で `asterism/<node>/<app>` の delete) で `forget_app`: その接続の全部の購読から、
  そのアプリのレーン (共有の置き場ではそのアプリが送った値のレーン) を消し、`type_of` 用に覚えた宣言・型も捨てる。
  次の値は最初の値 (`lost` 0、`restarted?` false)。
- 上限 (レビュー 3 章の 1): 購読ごとに端末 64 レーン、それ以外 4096 (`Asterism.lanes_max`、`machine?` で分ける)。超えたら
  最も前に見たレーンを忘れる (値が来るたびに並びの最後へ移すので、最近使ったものが残る)。
- CLI の `object echo`・MCP の値の店は生存の印の購読を持たない (MCP は持っていて、`Store#left` で番号も忘れるようにした)。
  `object echo` は leave を見ない読み手で、回の識別子で新しい回を見分ける。

## 3. レビュー 3 章の直し

| 項 | 直し |
|---|---|
| 1 番号の記憶が減らない | 2 章 (leave で捨てる + 上限) |
| 2 `type_of`・`declaration_of` が毎回問い合わせる | 送り手ごとに `[宣言, 型]` を覚える (`@profile?pubs;types` を 1 回)。最初に呼んだときに `asterism/*/*/@pubs` の購読 (深さ 16) を立て、その送り手の put で捨てる。購読の輪があふれたら全部捨てる。leave でも捨てる。問い合わせの間に誰かの `@pubs` が来たら、その答えは覚えない (古いかもしれない)。上限は端末 8・それ以外 256 送り手。自分のアプリの分は覚えない (毎回手元から)。`define_type` だけで型を変えても `@pubs` が出ないので、それは次の `@pubs` の put か leave まで見えない (README と doc コメントに書いた) |
| 4 ノードの置き場の送り手 | `from` のノードがキーのノードと違えば送り手なし (キーのレーン、ノードはキーのもの)。`Asterism.read_attachment` に入れたので CLI も同じ |
| 5 接続のときのロック | `Connection#initialize` を「プログラムの重複を予約 (`Asterism.claim`) → `open_checked` (セッションを開いて網の確認、ロックなし) → `attach` (宣言・登録、ロックあり)」に分けた。CRuby の `Locked` から `initialize` を外して `attach` を入れた。予約で、同じ `node/app` を 2 つのスレッドが同時に開くのも断る |
| 7 文書 | protocol.md 13 章の「boards have no TLS」を直した。10.1・9.2 に schema は `e` に入らないと書いた |

## 4. 時刻・今の値・フィールドの型 (決定 30・31・32)

- 30: protocol.md 3.3 (絶対時刻は ms の整数、ROS 層の `stamp_ns` は対象外)、11.1 (log 型の `time` は ms の整数)、README の Values の
  節 (ns → ms)、適合の見本の注記。asterism の CLI・MCP・コードで log 型を作る・読む所は無かった (grep で 0 件)。
- 31: protocol.md に 9.6「The present value」を足した (今の値は露出したオブジェクトのメソッドで答え、読み手は最初と `lost > 0` か
  `restarted?` で呼び直す)。README の Topics にも。
- 32: `Types.field_type!` は整数を `ArgumentError` (「field type 2: a field's type is a name ("vector3"), not a number」)。
  Symbol は名前として通す。読み手の `Types.check` は `[str, 空でない str]` だけのフィールドの定義を残す (番号の混じった定義は丸ごと落ちる)。
  protocol.md 11.2 の表・例 (`["at", "vector3"]`、フィールド名を 1〜2 文字に `p`・`c`)・作法の一文、README、適合の見本と node.rb、
  host_test/data_node.rb の `["kind", 5]` を `"state"` に。

## 5. 相手の protocol (レビュー 5 章の 1、実装だけ)

- `net.protocol_of(node, timeout:)`: そのノードの自己紹介の `protocol` (無ければ 0)、答えが無ければ nil。覚えた値があればそれを返し、
  無ければ自己紹介を 1 回聞く (アイコンなし)。自分のノードは `Asterism::PROTOCOL`。覚えた値はノードの leave で捨てる。
- 接続は `asterism/*/@profile` の put を購読して (深さ 8)、各ノードの protocol を覚え、2 以外なら 1 回だけ警告する
  (「node X speaks Asterism protocol 1, this program 2: its calls may work, its statuses, topics and data will not」)。**断らない**。
- この購読は CRuby では既定で立て、端末 (`machine?`) では立てない (購読 1 つ分の資源)。`Asterism.watch_protocols = true` で端末でも立つ。
  接続より後に来たノードは put で分かる。前からいたノードは put が来ないので、`protocol_of` で聞くか、相手の側が自分の put を見て警告する。
- 退けた案: ノードの join のたびに自己紹介に get を出す。適合の driver の `@profile` の queryable が問い合わせを順番に数える作りで、
  余計な get で見本がずれる。網の上でも N×N の問い合わせになる。

## 6. 1 値あたりに送るバイト数 (実測)

p2 の report 5 章と同じ測り方 (受け手は CRuby の peer の listen、間に TCP の中継を挟んで送り手 → 受け手のバイト数、500 個を 2 ms おきの平均、
Zenoh の枠を含む)。スクリプトは scratchpad (コミットしていない)。回の識別子は乱数なので、回ごとに 1〜3 バイトの幅がある (256 以上なら 3 バイト、
確率 99.6%)。

| 送り手 | 値 | 今 (B/値) | P2 の測定 |
|---|---|---|---|
| CRuby | `[0.5, -1, 3]`、`[seq, id]` | 34.2 | 33.2 (`[seq]`) |
| CRuby | 同じ、`ts: true` (`[seq, id, ms]`) | 43.2 | 43.2 |
| CRuby | 整数 1 つ | 23.2 | 20.2 |
| CRuby | `[0.5, -1, 3]`、ノードの置き場 (`[seq, id, nil, "snd/m"]`) | 53.2 | (なし) |
| mruby (試験の MessagePack、長い形) | `[0.5, -1, 3]` | 43.7 | 34.9 |
| mruby (同) | `ts: true` | 52.7 | 44.0 |
| mruby (同) | 整数 1 つ | 30.7 | (なし) |
| mruby (同) | ノードの置き場 | 50.7 | (なし) |

- attachment は「配列の頭 1 + seq (128 未満 1、255 まで 2、65535 まで 3、それより上 5) + id (256 以上で 3)」、`ts` で +9、送り手は文字列 + 1。
  典型 (seq < 128) で `[seq]` の 2 バイトから `[seq, id]` の 5 バイトへ、**値ごとに約 3 バイト増えた**。
- CRuby の `ts` ありが P2 と同じなのは、平均を取った seq の範囲と id の大きさの組み合わせによるぶれ (1〜2 バイト) の内。
- mruby の試験のビルドは host_test/msgpack.rb が整数を int64 の 9 バイトで書くので +9 になる。端末の msgpack-c は最短の形なので、
  **端末の `topic.put` は約 28 B/値** (P2 の見積もり約 25 B + id の 3 B。実機では測っていない)。

## 7. 試験

すべて `env -u ASTERISM_CREDENTIALS`。

| 何を | 結果 |
|---|---|
| `rake` (CRuby、全部) | 156 runs・2870 assertions・0 failures、msgs 2775 checks、適合 `cruby: 244 passed, 0 failed` (+1 informational、前からの「method name as bin」) |
| うち test_topic | 8 runs・160 (前 7・142): 回の識別子が変われば新しい回 (前へ 18 跳んでも欠け 0)、識別子なしは距離だけ、`forget`、`Sequences` の上限と LRU、2 プロセス間で全部の値が同じ id・`info.run_id`、宣言し直しで番号が続く (前は 0 から) |
| うち test_shared | 5 runs・105 (前 3・74): 4 要素の attachment、別のノードの送り手は送り手なし、古い 3 要素は送り手なし、leave で送り手のレーンだけ消える。網で: `type_of` が覚える → `@pubs` の put で捨てる → leave で番号も覚えた型も消える → つなぎ直した送り手の値は最初の値。`protocol_of` と警告 1 回 (protocol 1 のノード、protocol の無いノードは 0)、断らない |
| うち test_connections | 3 runs・70: 網の重複の確認の間にロックを持っていない、その間に別のスレッドがロックを取れる、同じ `node/app` を断る |
| うち test_cli | 30 runs・658: `Values.read`、run_id の項目、`Seq` の回と `forget`、別のノードの送り手 |
| `rake test:protocol_mruby` | `mruby: 244 passed, 0 failed` |
| `rake test:mruby` | 72 + 15 + 25 checks、0 failed (test_profile に番号が数字のフィールドを断る・読み手が落とす、回の識別子、leave、上限、`new_run_id` の範囲を足した) |
| `rake test:cross` | cross_mruby 1 run・35 assertions (mruby の値は全部同じ id、mruby が CRuby の `run_id` を読む)、host_test/test_connections 36 checks (leave で game のレーンと覚えた型が消える、覚えた型を 2 回目は手元から) |
| `rake test:reconnect` (docker) | 前からの `reconnection ok` (CRuby・mruby) に、新しい `restart_check` (CRuby・mruby) を足した: ルータ (zenohd 1.10.1) に送り手 `pub/rc` をつなぎ 3 値送って **SIGKILL**、すぐに同じ名前で起動し直して番号を 20 から送る (最初の 20 個を読み手が受け取らなかった形)。leave を見ない読み手 (CLI の `object echo -o json`) は 20 を `restarted: true`・`lost` なしで読む (識別子が無ければ 17 欠けと読む)。Asterism の購読の読み手は leave を先に見て最初の値として読み、欠け 0。両方とも `restart ok` |
| `rake test:credentials` (docker) | `credentials: ok` |
| zenohd 1.10.1 (docker) をルータに | test_shared 5/105、test_topic 8/160、test_connections 3/70、`test:protocol` 244 passed |

- 適合の見本は版 7: data_puts の attachment に id (`{"any": "u16"}` の新しい照合、driver が全部同じ id かも見る)、2 つ目の imu の前に
  同じ宣言で publish し直す (番号 1 のまま)、feed は全部 `[seq, id]` / 4 要素に、id が変わる値 (5 で欠け 0)、id の無い値 (距離だけ)、
  別のノードの送り手、報告の行の末尾に run id。types の答えに番号のフィールドの定義 (落ちる) と空の型 (落ちる)。
- picoruby-asterism-zenoh の不具合は見つからず、触っていない (test:cross の終わりの `Dropping peer due to processing error` は
  P2c からの既知のもの)。

## 8. コミット (asterism、main、push していない)

- 4b96b57 feat(protocol): run ids, numbers per connection, and readers that forget what left
- 35eafab docs(protocol): run ids, the reader's forgetting, ms times and field types by name

版・CHANGELOG は上げていない。

## 9. 止まらずに決めたこと (確認してほしい点)

1. **`id` が整数でない値**は「識別子なし」として距離だけで読む (識別子のある値と無い値の間は「違う識別子」= 新しい回)。design.md は
   attachment の形の誤りの扱いを `seq` と時刻についてだけ書いていたので、時刻と同じ流儀にした。読み手は範囲 (0〜65535) を確かめない。
2. 名前は `run_id` (`info.run_id`・`net.run_id`、JSON も)。1 章。
3. 番号の記憶の上限 64 / 4096、覚える送り手の上限 8 / 256 (端末 / それ以外)。
4. 同じ宣言での publish し直しは publisher を使い続ける (2 章)。
5. protocol の購読は CRuby だけ既定で立てる。`Asterism.watch_protocols=` を足した (5 章)。
6. `define_type` だけで型を変えたときは、覚えた型が次の `@pubs` の put か leave まで古い (3 章の 2)。`define_type` で `@pubs` を
   put し直す案もあるが、`@pubs` の意味 (宣言の変化) が変わるので入れていない。
7. leave の掃除の後の値は「最初の値」(`restarted?` false)。design.md の書き方どおり。CLI のように leave を見ない読み手では同じ場面が
   `restarted?` true になる。

## 10. P2d-2 への引き継ぎ

### asterism-games

- 決定 32: 独自の型は全部名前で書かれていた (`othello/table.rb` の `TYPES` など、整数のフィールドは grep で 0 件)。直しは要らない見込み。
  `define_type` は整数を `ArgumentError` にするので、試験を流せば残りが分かる。
- 決定 31: 卓の状態・観戦の読み手 (実況 `commentate`、ブラウザ版の観戦) が `info.lost > 0` か `info.restarted?` のときに、ホストの
  オブジェクトの呼び出しで今の状態を取り直す (今は lib に `info.lost`・`restarted?` を見る所が無い)。観戦の置き場は送り手ごとに
  数えるので、送り手 (`info.from`) ごとに取り直す。
- 決定 30: games の log 型の値は無かった。`record_book.rb` の `rec["time"] = now.to_i` (秒) は記録簿の保存の形でプロトコルの値ではないが、
  網に出すなら ms にそろえるか名前で単位を言う (`time_s` など)。`rooms.rb` の `clock: -> { Time.now.to_f }` は中の時計。
- 値の attachment の形が変わった (ブラウザ版の観戦は Asterism の購読で読むので、asterism を上げれば読める)。games の中で attachment を
  直に読んでいる所は無かった。

### asterism-console

- **leave の掃除の再利用**: `Asterism::Sequences` (`see(lane, run_id, seq, app)`・`forget(app)`・上限) と
  `Asterism.read_attachment(bytes, place, key_node)` (→ `[seq, run_id, ms, from]`) をそのまま使える。`lib/bag/channels.rb` の
  `attachment` は `[seq, ms, sender]` を `a[1]`・`a[2]` から読んでいて、**新しい形では ms を run id と読み違える**。`read_attachment` に
  置き換える (共有の置き場は 4 つ目が送り手、ノードの置き場で別のノードの送り手は送り手なし)。`bridge/recorder.rb:99` もこれを使う。
- 記録の欠け (レビュー 3 章の 6): `bridge/runner.rb:1000` の `rec.lost` は購読の `dropped` の合計だけ。通し番号の欠け (`info.lost` の和、
  キー + 送り手ごと) を足し、回の識別子で新しい回を数える。MCAP の message の sequence は送り手ごとの番号のまま、回が変われば番号が
  戻るので、channel の metadata か attachment に run id を残すかは console で決める。
- `@pubs`・宣言・型の記憶: console の図や記録が持っている送り手ごとの宣言・型は、アプリの生存の印の leave で捨てる (決まりは
  protocol.md 8.5 に 1 か所)。
- log の時刻 (決定 30): `bridge/logs.rb:121` は `time` を秒の数として `* 1000` している。ms の整数をそのまま使うように直す
  (今の Family mruby の log の送り手が秒で送っているなら、そちらも P3c で ms に)。
- 相手の protocol: console は CRuby なので、asterism を上げれば protocol の違うノードの警告が出る。図に出すなら `net.protocol_of(node)`。

### fmruby-core (P3c)

- mrblib (Ruby) だけの変更で、内蔵 RAM の静的な増分は無い見込み (端末のビルドは通していない)。購読ごとの番号の記憶は最大 64 レーン、
  `type_of` を使うアプリだけ `@pubs` の購読が 1 つ増える。protocol の購読は端末では立てない。
- 値ごとに attachment が約 3 バイト増える (6 章)。
