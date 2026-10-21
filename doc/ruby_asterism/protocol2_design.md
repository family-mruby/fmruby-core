# Asterism protocol 2: データの決まり (設計案)

> 状態: 構想 | 更新: 2026-10-21 | protocol 1 で決まりの無かった「アプリが自分で流す値 (データ)」を決める。形式は Zenoh の encoding、通し番号は attachment、型は流す口の宣言に 1 回だけ。値ごとに送るものは最小にする。互換性は考えない (2026-10-21 ユーザ: 使っているのは作者だけ、あるべき姿を考える)。V6 (記録、doc/asterism_recording/) と V7 (形に合わせた表示) はこの上に乗せる

## 目的

- アプリが流す値の**形式と型**を、受け手 (console の表示、記録、他のアプリ) が推し量らずに分かるようにする。
- 記録 (MCAP) に**通し番号と時刻**が素直に入り、取りこぼしが数えられるようにする。
- **何が流れているか**を網の上で見つけられるようにする (今は値が来るまで分からない)。
- 端末 (内蔵 RAM・flash・WiFi) の負担を増やさない: **値ごとに送るものは最小**にし、説明は 1 回だけ送る。

## 今の protocol 1 (前提)

asterism の `docs/protocol.md` が正。要点:

- ノード > アプリ > オブジェクト。宛先 `<node>/<app>/<object>`。アプリごとに Zenoh の接続 1 本。サーバは無い。
- 中身は MessagePack の値 1 つだけ。Zenoh の encoding・attachment・タイムスタンプは使わない (付けても受け手は無視)。
- 呼び出しは get で `asterism/<node>/<app>/<object>/call/<呼んだ相手>`。呼んだ相手をキーに置くのは、ルータの ACL がキーしか見られないため。答えは `{"value"}` か `{"error" => {kind, class, message}}`。
- メタ (`.../meta`)、自己紹介 (`asterism/<node>/@profile`)、状態 (`asterism/<node>/<app>/@status`、変わったら put)。
- **アプリ自身が流すキー** (卓の状態、お絵かきの `strokes`、ログ) は「`asterism/<node>/<app>/...` の下の MessagePack」とだけあり、形も見つけ方も決まっていない。protocol 2 はここを決める。

## 原則

1. **ACL で縛るものはキーに置く** (ノード・アプリ・呼んだ相手)。変えない。
2. **値の説明は Zenoh の仕組みに載せ、中身には値だけを入れる**: 形式は encoding、通し番号と送り手の時刻は attachment。
3. **値ごとに送るものと、1 回だけ宣言するものを分ける**: 型・単位・説明は宣言に 1 回。値ごとには、形式 (MessagePack 以外のときだけ) と通し番号だけ。
4. **短くできるものは短く**: 組み込みの型は番号、宣言の map のキーは 1〜2 文字 (ゲームの状態の `"st"` `"mb"` と同じ流儀)、説明は省略できる。

## 1. 呼び出し・メタ・自己紹介・状態

protocol 1 のまま。変えるのは次だけ (案):

- 答え (`{"value"}` / `{"error"}`) にも 2 章の決まりを当てはめるか (encoding を付けない = MessagePack) は、呼び出しの側では何も変わらないので、文言をそろえるだけ。
- protocol の番号は 2 にする (自己紹介の `protocol`)。protocol 1 との送り分けは作らない (全部を一斉に入れ替える。X1 と同じやり方)。

## 2. データの値 (値ごとに送るもの)

| 何を | どこに | 大きさ |
|---|---|---|
| 形式が MessagePack | **encoding を付けない**。「Asterism のデータで encoding が無ければ MessagePack」と決める | 0 バイト (Zenoh は既定の encoding を送らない) |
| 形式が MessagePack 以外 | Zenoh の決まった番号の encoding (`image/jpeg` 17、`image/png` 16、`text/plain` 4、`application/cdr` 7 など) | 1 バイト |
| 型 | **値には載せない**。流す口の宣言 (3 章) に 1 回 | 0 バイト |
| 通し番号 | attachment (下の形) | 1〜5 バイト |
| 送り手の時刻 | 流す口が選んだときだけ attachment に足す (4 章) | 0 (使うときは数バイト) |

- **MessagePack には Zenoh の決まった番号が無い** (Zenoh 1.10.1 の決まった番号は 0〜52、`zenoh/bytes` から動画まで)。`application/msgpack;...` を文字列で付けると値ごとに 20〜30 バイト太るので、付けない形にする。独自の番号 (Zenoh が使っていない範囲) を名乗る手もあるが、Zenoh の道具では「不明」に見えるので採らない。
- **生のバイト列で流せる**: `image/jpeg` を付ければ、JPEG を MessagePack の bin に包まずに流してよい (受け手は encoding で分かる)。
- **attachment の形** (案): MessagePack の配列 `[通し番号]` か `[通し番号, 送り手の時刻]`。通し番号は流す口ごとに 0 から 1 ずつ増える整数 (つなぎ直しで 0 に戻ってよい、受け手は戻りを「つなぎ直し」と読む)。MessagePack の正の整数は 127 まで 1 バイト、65535 まで 3 バイト。ROS 2 (rmw_zenoh) の attachment (通し番号・送った時刻・送り手の ID の固定 32 バイト程度) と同じ役割で、もっと短い。
- **キーも短くなる**: 流す口を Zenoh の publisher として宣言すると、Zenoh はキーを番号に置き換えて送る (zenoh-pico 1.10.1 も publisher の宣言でキーを宣言する、`_z_declared_keyexpr_declare`)。今の素の put より値ごとのキーの分が軽くなる。

## 3. 流す口の宣言 (1 回だけ送るもの)

- アプリが流す口を宣言する: Ruby の形 (案) `pub = Asterism.publish("board1/strokes", type: "asterism-games/board.strokes")`、`pub.put(value)`。数値なら `Asterism.publish("imu", type: :vector3, unit: "m/s2")`。
- 値のキーは `asterism/<node>/<app>/<宣言の名前>` (今と同じ場所)。
- **宣言の一覧**は、アプリごとの `asterism/<node>/<app>/@pubs` (`@status` と同じ作り: queryable で答え、変わったら put、`@` で始まる区切りなので `asterism/**` に掛からず ACL の規則は別)。中身 (案):

```
{"board1/strokes" => {"t" => "asterism-games/board.strokes"},
 "imu"            => {"t" => 2, "u" => "m/s2", "d" => "加速度"},
 "camera"         => {"t" => 4, "e" => 17}}
```

| キー | 意味 | 省略 |
|---|---|---|
| `t` | 型。組み込みは番号、アプリ独自は名前空間つきの文字列 | 省略すると「型なし」(今と同じく推し量る) |
| `e` | encoding の番号 (MessagePack 以外のとき) | 省略 = MessagePack |
| `u` | 単位 | 省略可 |
| `d` | 説明 (人と道具が読む) | 省略可 |
| `ts` | 送り手の時刻を attachment に付けるか (4 章) | 省略 = 付けない |

- **生存の印は流す口ごとには立てない** (端末の資源を使うため)。アプリの生存の印 (今ある) と `@pubs` の put で足りる。宣言を見る人は `asterism/*/*/@pubs` を購読する。
- **組み込みの型** (番号、案): 1 number、2 vector3 (`[x, y, z]`)、3 pose (位置と向き)、4 image (encoding で JPEG/PNG)、5 state (文字列か整数の場面・入/切)、6 log (`docs/v3.md` のログの形)、7 text。番号と形の対応は protocol 2 の文書に表で持つ。足すときは番号を足すだけ。
- **アプリ独自の型の定義**: 名前 (`asterism-games/board.strokes`) で呼ぶ。定義 (フィールドとその型、説明) を網で引けるようにするか、文書だけにするかは未確定 (下)。ROS 2 の `.msg` にあたる。

## 4. 時刻

3 つの時刻を使い分ける。

| 時刻 | どこから | 意味 | 使い道 |
|---|---|---|---|
| 送り手の時刻 | attachment (宣言の `ts` を立てた口だけ) | 値が測られた時刻 | グラフの横軸 (センサが測ってから送るまでに間がある場合) |
| ルータのタイムスタンプ | zenohd が付ける (1.10.1 の既定の設定は router: true、client: false。手元の Zenoh 1.10.1 の設定ファイルで確認) | ルータを通った時刻 (HLC) | 複数のノードの値の順番 |
| 受け取った時刻 | 受け手 | 記録の時刻 | MCAP の log_time |

端末の時計は合っていないことがある。送り手の時刻は「同じ口の中での間隔」としては正しく、ルータの時刻は「全体の順番」として正しい。普段は送り手の時刻を付けず、ルータの時刻で足りるかを先に確かめる (ルータを通らない peer の直結では付かない)。

## 5. 記録 (MCAP) との対応

| MCAP | protocol 2 |
|---|---|
| channel の topic | キー |
| channel の message_encoding | encoding (無ければ `msgpack`) |
| schema | 宣言の `t` (と、独自の型の定義) |
| message の sequence | attachment の通し番号 (取りこぼし・つなぎ直しが分かる) |
| message の publish_time | 送り手の時刻、無ければルータのタイムスタンプ |
| message の log_time | console が受け取った時刻 (今と同じ) |

V7 の「形に合わせた表示」は `t` で描き方を選ぶ (2 は軌跡、4 はコマ送り、5 は色の帯、独自の型はその描き手)。

## 6. 部品の対応 (2026-10-21 に確認)

| | CRuby (asterism-zenoh、zenoh-c 1.10.1) | 端末・ブラウザ版 (picoruby-asterism-zenoh、zenoh-pico 1.10.1) |
|---|---|---|
| encoding を付ける・読む | 部品にある | **zenoh-pico にはある** (`z_put_options_t` の encoding、`z_sample_encoding`、決まった番号の定数。`Z_FEATURE_ENCODING_VALUES` は 1)。**部品の Ruby の口は無い**ので足す |
| attachment | 部品にある | 部品にある (ROS 2 のために作った) |
| publisher の宣言 | 要確認 (部品の口) | zenoh-pico にある (`z_declare_publisher`)。部品の口は要確認 |
| タイムスタンプを読む | 部品にある | 要確認 |

## 7. 段 (案)

| 段 | 中身 |
|---|---|
| P0 | この文書の未確定事項を決める。asterism の `docs/protocol.md` に protocol 2 の章を書く (英語) |
| P1 | 部品: picoruby-asterism-zenoh に encoding と publisher の口を足す。asterism-zenoh も足りなければ足す。端末で内蔵 RAM の増分を測る (0 であること。増えるなら止まって返す) |
| P2 | asterism (CRuby と端末で同じ `mrblib/`): `Asterism.publish`・`@pubs`・通し番号・自己紹介の `protocol => 2`。適合の試験を protocol 2 に作り直す |
| P3 | 一斉に入れ替える: asterism-games (卓の状態、観戦、お絵かき)、Family mruby のアプリ、console (`@pubs` を読む、図に「流しているもの」を出す、記録に encoding・通し番号・時刻を入れる)、ブラウザ版、MCP |
| P4 | V7 の表示と V6 の記録をこの上に作る |

## 未確定事項

1. `@pubs` を `@status` と同じ queryable に相乗りさせるか (端末の queryable を増やさないため)。別にするなら増分を測ってから。
2. 組み込みの型の番号と形 (上の 1〜7 は案)。vector3 を配列にするか map にするか (配列の方が短い)。
3. アプリ独自の型の定義を網で引けるようにするか (例 `asterism/<node>/<app>/@types`)、文書と道具 (console の描き手) だけで持つか。
4. 通し番号を全部の口に付けるか、宣言で選ぶか (1〜5 バイトなので全部に付ける案が素直)。
5. 送り手の時刻の単位 (1970 年からのナノ秒の整数は 9 バイト。ミリ秒なら 6 バイト程度)。ルータの時刻で足りるかを先に測る。
6. 呼び出しの答えにも通し番号や時刻を付けるか (今は要らないと見ている)。
7. ROS 2 の側 (`Asterism::ROS`) は rmw_zenoh の決まりのまま (変えない) でよいか。
8. 宣言の名前 (`board1/strokes`) に `/` を許すか (今のキーは許している)。
