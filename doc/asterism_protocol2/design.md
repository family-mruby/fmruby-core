# Asterism protocol 2: データの決まり (設計)

> 状態: 計画済 | 更新: 2026-11-13 | P0 の 8 点を決定 (2026-11-13 ユーザ)。protocol 1 で決まりの無かった「アプリが自分で流す値 (データ)」を決める。形式は Zenoh の encoding、通し番号は attachment、型はトピックの宣言に 1 回だけ、アプリの自己紹介は `asterism/<node>/<app>/@profile` の queryable 1 つ。互換性は考えない (2026-10-21 ユーザ: 使っているのは作者だけ、あるべき姿を考える)。実装の段は plan.md、初心者向けの説明は explainer.html

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

protocol 1 のまま。変えるのは次だけ:

- 答え (`{"value"}` / `{"error"}`) にも 2 章の決まりを当てはめるか (encoding を付けない = MessagePack) は、呼び出しの側では何も変わらないので、文言をそろえるだけ。
- protocol の番号は 2 にする (自己紹介の `protocol`)。protocol 1 との送り分けは作らない (全部を一斉に入れ替える。X1 と同じやり方)。
- **アプリの自己紹介 `asterism/<node>/<app>/@profile`**: アプリごとの queryable を 1 つにし、状態・トピックの宣言・独自の型の定義を
  問い合わせのパラメータで答え分ける (`?status`・`?pubs`・`?types`)。今の `@status` の queryable はこれに置き換わる (queryable の数は今と同じ)。
  名前は端末の自己紹介 `asterism/<node>/@profile` と同じ語にし、「アプリの自己紹介」と読ませる。変わったときの put は今どおり
  `@status`・`@pubs` の別のキーに出す (購読する側は、知りたいものだけを購読できる)。`@` で始まる区切りなので `asterism/**` に掛からず、
  ACL の規則は別に書く (今の `@status` と同じ)。

## 2. データの値 (値ごとに送るもの)

| 何を | どこに | 大きさ |
|---|---|---|
| 形式が MessagePack | **encoding を付けない**。「Asterism のデータで encoding が無ければ MessagePack」と決める | 0 バイト (Zenoh は既定の encoding を送らない) |
| 形式が MessagePack 以外 | Zenoh の決まった番号の encoding (`image/jpeg` 17、`image/png` 16、`text/plain` 4、`application/cdr` 7 など) | 1 バイト |
| 型 | **値には載せない**。トピックの宣言 (3 章) に 1 回 | 0 バイト |
| 通し番号 | attachment (下の形)。**全部のトピックに付ける** | 1〜5 バイト |
| 送り手の時刻 | トピックが選んだときだけ attachment に足す (4 章) | 0 (使うときは数バイト) |

- **MessagePack には Zenoh の決まった番号が無い** (Zenoh 1.10.1 の決まった番号は 0〜52、`zenoh/bytes` から動画まで)。`application/msgpack;...` を文字列で付けると値ごとに 20〜30 バイト太るので、付けない形にする。独自の番号 (Zenoh が使っていない範囲) を名乗る手もあるが、Zenoh の道具では「不明」に見えるので採らない。
- **生のバイト列で流せる**: `image/jpeg` を付ければ、JPEG を MessagePack の bin に包まずに流してよい (受け手は encoding で分かる)。
- **attachment の形**: MessagePack の配列 `[通し番号]` か `[通し番号, 送り手の時刻]`。送り手の時刻は 1970 年からの**ミリ秒**の整数。通し番号はトピックごとに 0 から 1 ずつ増える整数 (つなぎ直しで 0 に戻ってよい、受け手は戻りを「つなぎ直し」と読む)。MessagePack の正の整数は 127 まで 1 バイト、65535 まで 3 バイト。ROS 2 (rmw_zenoh) の attachment (通し番号・送った時刻・送り手の ID の固定 32 バイト程度) と同じ役割で、もっと短い。
- **キーも短くなる**: トピックを Zenoh の publisher として宣言すると、Zenoh はキーを番号に置き換えて送る (zenoh-pico 1.10.1 も publisher の宣言でキーを宣言する、`_z_declared_keyexpr_declare`)。今の素の put より値ごとのキーの分が軽くなる。

## 3. トピックの宣言 (1 回だけ送るもの)

- アプリがトピックを宣言する: Ruby の形 `pub = Asterism.publish("board1/strokes", type: "asterism-games/board.strokes")`、`pub.put(value)`。数値なら `Asterism.publish("imu", type: :vector3, unit: "m/s2")`。
- 値のキーは `asterism/<node>/<app>/<宣言の名前>` (今と同じ場所)。宣言の名前には `/` を許す (`board1/strokes`)。
- **宣言の一覧**は、アプリの自己紹介の queryable に `asterism/<node>/<app>/@profile?pubs` で問い合わせて得る。変わったら `asterism/<node>/<app>/@pubs` に put する。中身:

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

- **生存の印はトピックごとには立てない** (端末の資源を使うため)。アプリの生存の印 (今ある) と `@pubs` の put で足りる。宣言を見る人は `asterism/*/*/@pubs` を購読する。
- **組み込みの型** (番号): 1 number、2 vector3 (`[x, y, z]`)、3 pose (位置と向き)、4 image (encoding で JPEG/PNG)、5 state (文字列か整数の場面・入/切)、6 log (`docs/v3.md` のログの形)、7 text。形は**配列**の流儀 (vector3 は `[x, y, z]`、pose も同じ流儀)。番号と形の対応は protocol 2 の文書に表で持つ。足すときは番号を足すだけ。
- **アプリ独自の型の定義**: 名前 (`asterism-games/board.strokes`) で呼ぶ。定義 (フィールドとその型、説明) は**網で引ける**: `asterism/<node>/<app>/@profile?types`。ROS 2 の `.msg` にあたる。

## 4. 時刻

3 つの時刻を使い分ける。

| 時刻 | どこから | 意味 | 使い道 |
|---|---|---|---|
| 送り手の時刻 | attachment (宣言の `ts` を立てたトピックだけ) | 値が測られた時刻 | グラフの横軸 (センサが測ってから送るまでに間がある場合) |
| ルータのタイムスタンプ | zenohd が付ける (1.10.1 の既定の設定は router: true、client: false。手元の Zenoh 1.10.1 の設定ファイルで確認) | ルータを通った時刻 (HLC) | 複数のノードの値の順番 |
| 受け取った時刻 | 受け手 | 記録の時刻 | MCAP の log_time |

端末の時計は合っていないことがある。送り手の時刻は「同じトピックの中での間隔」としては正しく、ルータの時刻は「全体の順番」として正しい。普段は送り手の時刻を付けず、ルータの時刻で足りるかを先に確かめる (ルータを通らない peer の直結では付かない)。

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
| encoding を付ける・読む | 部品にある | **zenoh-pico にはある** (`z_put_options_t` の encoding、`z_sample_encoding`、決まった番号の定数。`Z_FEATURE_ENCODING_VALUES` は 1)。**部品の Ruby の API は無い**ので足す |
| attachment | 部品にある | 部品にある (ROS 2 のために作った) |
| publisher の宣言 | 部品にある (`Publisher`) | zenoh-pico にある (`z_declare_publisher`)。**部品の Ruby の API は無い**ので足す |
| タイムスタンプを読む | 部品にある | 要確認 |

呼び出しの答えには通し番号も時刻も付けない。ROS 2 の側 (`Asterism::ROS`) は rmw_zenoh の決まりのまま変えない。

## 決定 (2026-11-13 ユーザ)

2026-11-12 に P0 の 8 点へ仮の答えを出し、2026-11-13 にユーザがそのまま決定とした (「この内容で、設計を決定として」)。
上の本文はこの答えで書いてある。実装の段は plan.md。

| # | 問い | 答え |
|---|---|---|
| 1 | 宣言の一覧に答える queryable | アプリの自己紹介 `@profile` の 1 つにまとめ、`?status`・`?pubs`・`?types` で答え分ける (名前は 2026-11-13) |
| 2 | 組み込みの型の形 | 配列 (vector3 は `[x, y, z]`) |
| 3 | アプリ独自の型の定義 | 網で引ける (`@profile?types`) |
| 4 | 通し番号 | 全部のトピックに付ける |
| 5 | 送り手の時刻の単位 | ミリ秒 (1970 年からの整数、宣言で `ts` を立てたトピックだけ) |
| 6 | 呼び出しの答えの通し番号・時刻 | 付けない |
| 7 | ROS 2 の側 | rmw_zenoh の決まりのまま |
| 8 | 宣言の名前の `/` | 許す |

### 細部の決定 (2026-11-13 ユーザ、P2 の前に)

| # | 問い | 答え |
|---|---|---|
| 9 | `@profile` に引数なしで問い合わせたときの答え | 状態と宣言: `{"status" => {...}, "pubs" => {...}}`。`?status`・`?pubs`・`?types` を付ければ、その 1 つだけを同じ形 (`{"types" => {...}}` など) で返す |
| 10 | 独自の型の定義 (`?types`) の形 | 型の名前ごとに `{"f" => [[名前, 型], ...], "d" => 説明}`。フィールドの型は組み込みの番号、基本の型名 (`int`・`float`・`str`・`bin`・`bool`)、ほかの独自の型の名前のどれかで、末尾の `[]` で配列。`d` は省略可 |
| 11 | 受け手の Ruby の API | 値の包み 1 つをブロックに渡す: `Asterism.subscribe(path) { \|s\| s.value; s.seq }`。包みは値 (MessagePack は解いた後)・通し番号・送り手の時刻・encoding・キーなどを持つ |
| 12 | 通し番号の折り返し | 2^31 未満で 0 に戻す (どの端末の整数でも扱える)。受け手は「上限近くから 0」を折り返し、それ以外の戻りをつなぎ直しと読む |
| 13 | 受け手の包みの encoding (encoding が無い値) | `"msgpack"` (記録の MCAP の message_encoding と同じ語)。部品 (Zenoh の層) は `"zenoh/bytes"` を返すままで、Asterism の包みが読み替える |

退けた案 (1 の queryable): 既存のアプリの queryable (`**`) で答える案は予約名が要り、`@pubs` 専用の queryable を足す案は
queryable が 1 つ増えるので採らなかった。
