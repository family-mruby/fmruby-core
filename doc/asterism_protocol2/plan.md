# Asterism protocol 2 の実装

> 状態: 進行中 | 更新: 2026-11-13 | design.md の決まり (2026-11-13 決定) を部品・Asterism 本体・使う側の順に入れ、最後に一斉に入れ替えて公開する。次は P1 (部品)

## 目的

- design.md で決めた protocol 2 (アプリが流す値の形式・型・通し番号・時刻、アプリの自己紹介 `@profile`) を、
  CRuby・端末 (picoruby)・ブラウザ版のすべてで動かす。
- 受け手 (console・記録・他のアプリ) が、値が来る前に「だれが何を流しているか」と型を知れるようにする。
- V6 (記録、doc/asterism_recording/) と V7 (形に合わせた表示) を乗せる土台にする (V6・V7 そのものはこの企画に含めない)。

## 方針

1. **決まりの正は design.md**。実装中に決まりの穴が見つかったら、直し方を選択肢にしてユーザに聞く (実装する側で決めない)。
2. **下から順に**: 部品 (Zenoh の Ruby の API) → Asterism 本体 (CRuby と端末で同じ `mrblib/`) → 使う側。
3. **互換は考えない**: protocol 1 との送り分けは作らず、P3 で全部を一度に入れ替える (design.md 1 章)。
4. **リポジトリをまたぐ開発は手元の checkout で**: fmruby-core は `ASTERISM_DIR`・`PICORUBY_ASTERISM_ZENOH_DIR` で手元の
   checkout を指してビルドする。各リポジトリへのコミットはしてよいが、**push・gem の公開・VPS への配置は P4 でユーザの許可を得てから**。
5. **値ごとに送るものは最小**、**内蔵 RAM は増やさない** (publisher の宣言・queryable の置き換えで増えるなら止まって返す)。

## 関係するリポジトリ (family-mruby の直下の checkout)

| リポジトリ | この企画で触るもの |
|---|---|
| picoruby-asterism-zenoh | encoding を付ける・読む、publisher の宣言、(あれば) タイムスタンプを読む Ruby の API |
| asterism-zenoh | CRuby の部品。encoding・publisher・タイムスタンプは既にある。足りない分だけ |
| asterism | `Asterism.publish`、アプリの `@profile`、`@pubs` の put、通し番号と時刻の attachment、受け手の API、`docs/protocol.md` の protocol 2、適合の試験 |
| asterism-games | 卓の状態・観戦・お絵かきを `Asterism.publish` に |
| asterism-console | `@profile`・`@pubs` を読む、図に流しているものを出す、記録に encoding・通し番号・時刻 |
| fmruby-core | ピンの更新、アプリ (`session.put` を直に使っているもの)、ブラウザ版、MCP |

## スコープ (段)

| 段 | 内容 | 状態 |
|---|---|---|
| P1 | 部品: picoruby-asterism-zenoh に encoding (put の指定と、受け取った値の encoding の読み出し) と publisher の宣言 (宣言・put・取り下げ) の Ruby の API を足す。zenoh-pico でタイムスタンプが読めるかを確かめ、読めるなら足す。asterism-zenoh は足りない分だけ。CRuby と端末で同じ書き方になるよう Ruby の形をそろえる。publisher 1 つ・queryable 1 つあたりの RAM を測る | 未着手 |
| P2 | Asterism 本体: `Asterism.publish(name, type:, unit:, desc:, ts:, encoding:)` と `pub.put`、通し番号 (全トピック) と送り手の時刻 (ミリ秒、`ts` のときだけ) の attachment、アプリの `@profile` queryable (`?status`・`?pubs`・`?types`) で今の `@status` の queryable を置き換え、`@pubs` の put、独自の型の定義の登録、受け手の API (値と一緒に encoding・通し番号・時刻を受け取る、宣言の一覧を引く)、自己紹介の `protocol => 2`。`docs/protocol.md` に protocol 2 を書き (英語)、適合の試験を作り直す | 未着手 |
| P3 | 入れ替え: asterism-games、asterism-console、fmruby-core のアプリ・ブラウザ版・MCP、CLI (`asterism`)。ネイティブ版・sim・ブラウザ版・console の間で、宣言が見え、値が型つきで届き、通し番号の欠けが数えられることを通しで確かめる | 未着手 |
| P4 | 公開: 各リポジトリの push、gem (asterism-zenoh・asterism・asterism-games) の版上げと公開、console の版上げと VPS 更新、fmruby-core のピン更新、ブラウザ版の配置。**どれもユーザの許可語を得てから** | 未着手 |

## 受け入れ条件

- CRuby のアプリと端末 (ネイティブ版・sim・ブラウザ版) のアプリが、`Asterism.publish` で宣言したトピックを流し、
  相手が `@profile?pubs` で宣言を引け、値を encoding・通し番号 (と `ts` のときの時刻) つきで受け取れる。
- MessagePack の値には encoding が付かない (値ごとの増分は attachment の通し番号の 1〜5 バイトだけ)。`image/jpeg` は bin に包まずに流せる。
- アプリごとの queryable の数が今と同じ (`**` と `@profile`)。`@profile` が引数 (`?status`・`?pubs`・`?types`) で答え分ける。
- 自己紹介の `protocol` が 2。asterism の適合の試験が CRuby と picoruby の両方で通る。
- fmruby-core の `rake regress` が通り、`rake regress:size` で内蔵 RAM の増分が 0。
- console の図に、アプリの流しているトピックと型が出る。記録 (MCAP) に encoding・通し番号・時刻が入る。

## 未確定事項

P2 で案を作り、ユーザに選択肢で聞く。

- `@profile` に引数なしで問い合わせたときの答え (状態と宣言をまとめて返すか、状態だけか)。
- 独自の型の定義 (`?types` の答え) の形: フィールドの並びと型の書き方 (ROS 2 の `.msg` のような名前と型の組の配列か)。
- 受け手の Ruby の API の形 (ブロックに値と一緒に通し番号・時刻を渡すか、値の包みを渡すか)。
- 通し番号の上限と折り返し (MessagePack の正の整数で 32 ビットを超えたとき)。

## 関連

- design.md: 決まりの正。explainer.html: 初心者向けの説明 (2026-11-13 の決定を反映した版)。
- doc/ruby_asterism/plan.md: Asterism 全体の計画。V6 は doc/asterism_recording/。
- asterism の `docs/protocol.md`: protocol 1 の正。P2 で protocol 2 に書き換える。
