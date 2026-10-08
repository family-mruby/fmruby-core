# Asterism C7 指示書: zenoh-c の機能を CRuby から使えるようにする

> 状態: 指示 | 更新: 2026-10-08 | asterism-zenoh の docs/feature_coverage.md で「未対応・一部」の機能のうち、意味のあるものをなるべく Ruby から使えるようにする。版は 0.3.0

ユーザの依頼 (2026-10-08): 「意味のありそうなものはなるべく使えるようにしたい」。

## 1. 対象 (asterism-zenoh の `docs/feature_coverage.md` の行)

### 1.1 入れるもの

| 機能 | Ruby での形の目安 |
|---|---|
| セッションの設定を渡す | `Session.open(loc, mode:, listen:, config: {"transport/link/tls/..." => ...})` (key → JSON5 の値) と `config_file:`。今の内部の設定 (接続の時間制限など) は既定のまま、使う人の指定が上書きする |
| 相手を自動で探す | `Asterism::Zenoh.scout(what: :peer\|:router, timeout:)` (見つかった相手の zid・種類・住所)。peer のときマルチキャストのスカウティングを入れる設定 (`scouting:` か config) |
| セッションの情報 | `peers` / `routers` が zid の配列を返す形 (今の peers の数は残す) |
| put の設定 | `put(key, payload, attachment:, encoding:, priority:, congestion_control:, express:, reliability:, timestamp:, allowed_destination:)` |
| delete | `session.delete(key, ...)` |
| 宣言した publisher | `pub = session.publisher(key, encoding:, priority:, ...)`、`pub.put`、`pub.delete`、`pub.matching?`、`pub.on_matching { \|bool\| }` (受け手がいるかの通知。ポーリングで取り出す) |
| subscribe の値の付加情報 | 受け取った値から kind (put / delete)、encoding、timestamp、priority などを取れる (今の `each_pending { \|k, v, a\| }` はそのまま。付加情報は別の取り出し方か、CRuby の層の Sample に足す) |
| 宣言した querier | `q = session.querier(key, target:, consolidation:, timeout:)`、`q.get(params, payload:, attachment:)`、matching |
| queryable の答え方 | `query.reply_err(payload, encoding:)`、`query.reply_del(key)`、答えの encoding / timestamp。get 側で、エラーの答えを区別して取れる |
| エンコーディング | put・publisher・答えで指定、受け取りで読める (文字列で扱う。例: "application/json") |
| 時刻 (HLC) | セッションの新しい時刻 `session.new_timestamp`、値の timestamp を読める (Time と id) |
| キーの演算 | `Asterism::Zenoh::KeyExpr` (`intersects?`、`includes?`、`join`、`concat`、正規化)。宣言したキー (`session.declare_keyexpr`) |
| 高度な pub/sub | `session.advanced_publisher(key, cache: N, ...)`、`session.advanced_subscriber(key, history:, recovery:, ...)`、送り手の検出。ROS 2 の transient local に使える形 |
| 接続・リンクの通知 | `session.on_transport { \|ev\| }` / `on_link { \|ev\| }` (相手のつながり・切れ)。ポーリングで取り出す |
| ログ | `Asterism::Zenoh.init_log(level)` (zenoh-c の RUST_LOG 相当) |

### 1.2 入れないもの (理由を表に書く)

- 共有メモリ (同じ機械の中だけで、Ruby の値の受け渡しには合いにくい)。
- 裏で動く宣言 (background)。オブジェクトの寿命で閉じる今の形で足りる。
- 符号化の道具 (ze_serialize)。MessagePack と CDR がある。
- publication cache / querying subscriber。高度な pub/sub の古い形。

上の判断を変えるべき事情が見つかったら、report に書く (止まらなくてよい)。

## 2. 作りの決まり

- **受け取りの形**: 受け取りはすべて今と同じ形 (FIFO に溜めて、ポーリングで取り出す)。zenoh-c のコールバックの中で Ruby に触らない。
  - 新しく通知を受けるもの (matching、接続・リンク、高度な subscribe の送り手の検出など) も同じ形にする。
- **今の API は変えない**: 0.2.0 の呼び方はそのまま動く。足すのはキーワード引数と新しいメソッド・クラス。
- **CRuby の層 (`asterism` の `lib/asterism/cruby/`)**: ブロックで受け取る形 (`on_matching { }`、`on_transport { }`、advanced subscriber の `{ }` など) を足す。
  - 受け取りのスレッド (Runner) から配る。`Sample` の値オブジェクトに、付加情報 (kind、encoding、timestamp、priority) を足す。
- **共有の層 (`mrblib/`)**: 触らない。機体 (zenoh-pico) に同じものを入れられるかは、表の列で示すだけ。
- **表**: `docs/feature_coverage.md` を、出来上がった状態に書き直す。入れなかったものの理由も書く。
- **README**: 各機能の例を README か `docs/` に書く。
- **版**: asterism-zenoh と asterism を 0.3.0 にする。asterism の依存は `~> 0.3.0`。

## 3. 試験

- **機能ごとの試験** (asterism-zenoh の `rake`): 2 つのセッションの間で確かめる。
  - 設定の受け渡し: 例として、待ち受けの住所や時間制限を config で渡す。TLS は自己署名の証明書で、2 つの CRuby の間の `tls/` 接続を試す (証明書は試験の中で作る)。
  - スカウティング: マルチキャストが使えない環境なら、試験は skip にして理由を出す。docker の中や手元で 1 回は確かめる。
  - そのほか、delete、publisher と matching、querier、reply_err / reply_del、エンコーディング、時刻、キーの演算、高度な pub/sub (後から来た購読者が履歴を受け取る)、接続・リンクの通知、ログ。
- **今の試験**: 全部通る (asterism の `test:msgs`、`test:objects`、`test:api`)。
- **機体との往復**: P4-Nano の asterism_demo を CRuby から呼ぶ、を 1 回。新しい機能で機体との互換が崩れていないこと。
  - 機体は put / get の古い形のままでつながること。
- **ROS 2 (任意、時間があれば)**: 高度な publisher で transient local の相手に後から値が届くか、rmw_zenoh の購読者で試す。
  - できなければ、何が要るかを report に書く。

## 4. 範囲・決まり

- **触ってよいもの**:
  - asterism-zenoh と asterism のリポジトリ。`main` にコミットする。push は親が行う。
  - fmruby-core の `doc/ruby_asterism/report/c7.md`。fmruby-core のブランチは develop から `feature/asterism-c7` を切る。
- **触らないもの**:
  - picoruby-asterism-zenoh と、`mrblib/`。
  - fmruby-core のビルドの仕組み、`.env`、sdkconfig。
- **公開**: rubygems.org への公開はしない。
- **機体**: P4-Nano。本当の IP は親が渡す。文書には本当の IP・機体の名前を書かない。
  - ミュート中。
  - 親の capture が向いている (`serial_start` を呼ばない)。
- **zenohd・ROS 2**: 重ね合わせのファイルで上げ、終わったら `down`。
- **止まる条件**:
  - C 拡張の作りを大きく変える必要があるとき。例: コールバックの中で Ruby を呼ばないと成り立たない。
  - 今の API を変える必要があるとき。
- **report**: Japanese plain style (常体)。書くことは次のとおり。
  - 入れたもの・入れなかったもの (表)。
  - API の一覧。
  - 試験の結果。
  - 機体側 (zenoh-pico) へ持っていける候補。
