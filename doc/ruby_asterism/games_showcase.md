# asterism-games を Asterism の見本にする

> 状態: 進行中 | 更新: 2026-10-17 | GS1・GS2 完了 (report/gs1.md・gs2.md)、次は GS3。麻雀だけのゲームホストを、Asterism でサーバ側のアプリを書く見本にする。共通の枠組み (gem) とゲーム (アプリと image) に分け、書き方を Asterism らしくし、麻雀以外の例・CRuby のクライアント・網を見る CLI・MCP・AI の打ち手を足す

前提: report/mj1.md〜mj5.md・mj4.md (部屋と常設の卓)・g2b.md (部屋の席)、mje*.md (エンジン)、asterism-games の README、asterism の README (portable API・CRuby API・Who called・Profiles)。

## 1. 目的

- asterism-games を **Asterism の見本** (ショウケース) にする。麻雀だけだと、どこまでがゲームに依らない仕組みかが見えないので、性格の違う例を足す。
- 書き方を **Asterism らしく** する。どのゲームも同じ書き方になり、そのまま読める手本になること。
- 画面のアプリは Family mruby (fmruby-core) に置く (Family mruby も見せる)。あわせて **Family mruby なしで試せる例** (CRuby) を置く。
- 原則: Asterism の実装 (asterism / asterism-zenoh / picoruby-asterism-zenoh) は Family mruby に依存しない (2026-10-16 に確かめた: コードの依存は無く、名前とコメントだけ)。asterism-games も依存しない。

## 2. 方針

### 2.1 配り方
- **枠組みは gem** (仮の名前 `asterism-games`): 自分のゲームのホストを書きたい人向け。ゲームはルールと出すメソッドだけを書く。gem の公開はユーザが行う。
- **ゲームと配備はアプリ (今のリポジトリ) と docker の image**: 麻雀・オセロ・お絵かきをそのまま動かす人、console の隣に置く人向け。image は console と同じく ghcr に公開する (今は手元で作って運んでいる)。
- 麻雀は riichi_engine の Fork (git の取得元) に頼るので、枠組みの gem には入れずアプリ側に置く。

### 2.2 枠組みが持つもの (今の asterism-games から切り出す)
- 部屋の監督 (部屋ごとのプロセス、片付け、席の ID を信じる)、常設の卓、保存と再開、状態の発行、見張り (`GET /status`)、VPS 用の一式。
- ゲームの基底: 出すメソッド、呼び手、居る・居ない、状態の見え方 (公開・席ごと・観戦)、時間 (手番の持ち時間)。

### 2.3 Asterism らしい書き方 (土台に入れる)
- 呼び手は `expose(..., caller: :from)` で引数 `from:` として受ける (今は `Asterism.current_call` を各メソッドで読んでいる)。
- CRuby は `Asterism.connect(...) do |net| ... end` の Net の書き方。受け手のスレッドと `on_error` を gem に任せ、ゲームの進行 (時間切れ・CPU) だけを自分の輪に残す。
- 居る・居ないは `on_join` / `on_leave` で知る (今は呼び出しの間隔で「途切れ」を数えている)。
- 端末は `on_join` を合図に卓を探し直す (今は数秒ごと)。端末の見え方の場合分けは `case/in`。
- 卓の数は GS2 では網に載せない。GS4 で読む側 (網を見る CLI の watch・console) と一緒に、状態のキー `asterism/<ID>/<app>/@status` の決まりを作る (2026-10-17 ユーザ。プロフィールは現れたときに 1 回読む自己紹介で、変わり続ける数とは合わない。gs1.md 4.3)。
- 部屋の卓は、部屋が開くアプリからゲームを引いて、そのゲームの卓を出す。部屋の設定は「開くアプリ」1 つのまま (2026-10-17 ユーザ)。
- 互換のための部分 (`view` の 1 引数・`join`/`mine` の秘密と `rooms.secret_ids`・`ru`/`rx`/`ry` と `RULE_TURNS`) は GS2 の最初にまとめて落とす (2026-10-17 ユーザ)。
- 手牌のような席だけの情報は、今どおり「呼び出しの答え」で渡す (購読で流さない。部屋の中は席ごとに ACL を分けても、購読される鍵は守れないため)。

## 3. 作るもの

| | 中身 | 置き場所 |
|---|---|---|
| 枠組み | 2.2 と 2.3 | asterism-games (gem に切り出せる形) |
| 麻雀 | 枠組みの上に載せ直す。見た目と動きは変えない | asterism-games (アプリ) |
| オセロ (ホスト版) | いちばん小さな見本 (数百行)。今の端末どうしのオセロを、ゲームホストを置く形で。今の端末どうしのオセロは「オセロ (直接)」として残し、名前で区別する (2026-10-17 ユーザ) | asterism-games、画面は fmruby-core |
| 共有のお絵かきボード | 勝ち負けの無い、状態を共有して配る例。履歴 (advanced pub/sub) と console の MCAP の記録・再生 | asterism-games、画面は fmruby-core |
| CRuby のターミナルのクライアント | アスキーアートでオセロを観戦 (座標を打ち込んで打てる) | asterism-games |
| 網を見る CLI | `asterism nodes` / `objects` / `watch` / `echo` / `call` / `tree`。管理者の `.tar` (見るだけの役割) で、既定は見るだけ、`call` は明示したときだけ | asterism gem |
| Asterism → MCP の橋渡し | オブジェクトのメソッドの一覧 (meta) を読んで MCP のツールにする。Ruby の `mcp` gem | asterism gem |
| 麻雀の MCP のツール | 卓の見え方を AI が読める形にする説明、アシストの情報、点数の問い合わせ | asterism-games |
| AI の打ち手 | 打ち手のプログラムに「判断のつなぎ」を 1 つ決め、Claude API と Jev (TypeSafe AI、Cloudflare Workers AI 経由も) のつなぎを書く。卓に Claude・Jev・CPU を並べて比べる | asterism-games |

## 4. Step

| Step | 内容 | 受け入れの目安 |
|---|---|---|
| GS1 | 枠組みの切り出しと、麻雀を載せ直す。2.3 の書き方のうち小さいもの (caller: :from、Net の書き方、プロフィール) | 試験 (今の 141 件) が全部通る。部屋・常設・家の LAN の卓が今どおり。1 通の大きさとメモリが変わらない |
| GS2 (完了 2026-10-17、report/gs2.md) | オセロ (ホスト版) とその画面、CRuby のアスキーアートのクライアント | ブラウザ版・端末・CRuby が同じオセロの卓で打てる、観戦できる。結果: asterism-games 0.7.0、オセロの卓 (592 行、枠組みへの追加なし)、卓のアプリ「Othello (table)」、`bin/othello-tty`、今のオセロは「Othello (direct)」。部屋の卓は部屋が開くアプリのゲームから選ぶ。互換は view の 1 引数・秘密の道・RULE_TURNS を落とし、ru/rx/ry は今のアプリが読むので残した。遊び方の細部: 色は始めるときにくじ、パスはホストが自動で打つ、終局後も盤が残る、観戦の上限の既定 14。席の管理を基底に上げるかは GS3 で判断 |
| GS3 | 居る・居ないを on_join / on_leave で、端末の探し直し、case/in | 途切れの判定が速くなる。試験 |
| GS4 | 網を見る CLI と MCP の橋渡し (asterism gem) | CRuby だけで網の全体が見える。MCP のクライアントからオセロと麻雀の卓を呼べる |
| GS5 | 麻雀の MCP のツールと AI の打ち手 (Claude API、Jev) | AI が卓に座って半荘を打てる。CPU と比べた数字 |
| GS6 | 共有のお絵かきボード (MCAP の記録と再生) | 複数の端末とブラウザで描け、記録を再生できる |
| GS7 | 枠組みを gem に、image を ghcr に | gem と image の形が整い、ユーザが公開できる |

## 5. 受け入れ条件 (全体)

- asterism-games の README の冒頭で「Asterism の見本」であることと、枠組みとゲームの関係が分かる。
- どのゲームも、同じ枠組みと同じ書き方で書かれている。
- Family mruby なしで (CRuby だけで) 卓に入れる例がある。
- 部屋の守り (席の証明書、ゲームホスト自身の証明書) と、常設の卓・家の LAN の卓が、今どおり動く。

## 6. 未確定事項

- 枠組みの gem の名前と、いつ gem に切り出すか (GS7 まで同じリポジトリの中でよいか)。
- AI の打ち手の鍵 (Anthropic の API キー、Cloudflare の API トークン) の置き場所と、費用の上限。Jev の数字 (速さ・料金) は会社の発表なので、使う前に確かめる。
