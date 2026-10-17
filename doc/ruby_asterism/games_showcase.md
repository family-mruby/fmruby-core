# asterism-games を Asterism の見本にする

> 状態: 進行中 | 更新: 2026-10-17 | GS1-GS5 完了 (report/gs1.md・gs2.md・gs2b.md・gs3.md・gs4.md・gs5.md。GS5 の本物の API での確かめは鍵待ち)、GS6 完了 (report/gs6.md)、次は GS5b (Jev を直接の API に) と GS7。麻雀だけのゲームホストを、Asterism でサーバ側のアプリを書く見本にする。共通の枠組み (gem) とゲーム (アプリと image) に分け、書き方を Asterism らしくし、麻雀以外の例・CRuby のクライアント・網を見る CLI・MCP・AI の打ち手を足す

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
| GS3 (完了 2026-10-17、report/gs3.md) | 居る・居ないを on_join / on_leave で、端末の探し直し、case/in | 途切れの判定が速くなる。試験。結果: asterism-games 0.8.0。途切れの判定は約 10-14 s から 6-17 ms (家の LAN・部屋・常設。常設は役割 games-host に `Read asterism/*/game` が要る、2026-10-17 ユーザ決定)。卓のアプリは一覧の取り直しをやめてホストの on_join で探し直し (待つ間の問い合わせ 0 回、戻るまで 5-17 ms)。呼び出しの間隔の仕組みは、印が見えないノードだけの備えに残した。席の管理は基底に上げない (共通は 30 行ほど)。残り: アプリの VM の case/in で配列の形が nil で落ちる (コンパイラの直し)、居る・居ないはノード単位 (GS4 へ) |
| GS4 (完了 2026-10-17、report/gs4.md) | 網を見る CLI と MCP の橋渡し (asterism gem) | CRuby だけで網の全体が見える。MCP のクライアントからオセロと麻雀の卓を呼べる。結果: 状態のキー `asterism/<node>/<app>/@status` (変わったときだけ put、get にも答える、protocol.md 8 章、上限 1024 B) を網の決まりに足した (2026-10-17 ユーザ)。asterism の CLI (nodes / objects / tree / watch / ping / call、`--router` と `--credentials`) と `asterism mcp` (既定は見るだけ、`--allow` で許したメソッドだけがツール)。console 0.3.4 は図の Details に @status、見るだけの役割 viewer。asterism-games 0.9.0 は卓ごとの数を @status で (部屋の中では出さない)。MCP の客から許したオセロの卓で CPU と終局まで打てた。asterism は 0.7.1 のまま (公開はユーザ)、console と games は 0.7.1 でも動く形 |
| GS5 (完了 2026-10-18、report/gs5.md。本物の API での確かめは鍵待ち) | 麻雀の MCP のツールと AI の打ち手 (Claude API、Jev) | AI が卓に座って半荘を打てる。CPU と比べた数字。結果: asterism の meta に `args` / `doc`、アプリ単位の出入り (`on_app_join` / `on_app_leave`、卓のアプリはアプリ名 `game`)。asterism-games 0.10.0: AI 向けの `describe` / `look` / `choose` / `points` (AI に渡すのはその席に見える範囲だけ)、VPS のゲームホストの中の AI の打ち手 (Claude `claude-sonnet-5-5`・Jev `typesafe/jev`、卓の主が空いた席に入れる)、費用の上限は 1 日の額 (既定 3.0 USD) と 1 ゲームあたりの呼び出し回数 (既定 400)、AI の席の持ち時間は打牌 30 秒・鳴き 15 秒で切れたらその手だけ CPU、Claude が答えを拒んだらその手だけ CPU (2026-10-17〜18 ユーザ)。オセロの AI の打ち手は無し (MCP の客からは打てる) |
| GS6 (完了 2026-10-18、report/gs6.md) | 共有のお絵かきボード (MCAP の記録と再生) | 複数の端末とブラウザで描け、記録を再生できる。結果: asterism-games 0.11.0 のゲーム `board` (線の列で配る、ボードごとに保存、全消しは主、各自は自分の最後の線を取り消し、2026-10-18 ユーザ)。枠組みに状態の外に配る `Game#tell` と保存の間引き。アプリ「Board / お絵かき」(fmruby-core)。記録と再生は console の今の V4 のまま (コードの変更なし)。ボード座標 640x360・16 色・4 段、上限 1000 本・40,000 点で古い線から捨てる。残り: 太い線の斜めの縞 (描く側の命令が要る)、白紙にせず再生すると番号が重なる、1280x720 では窓のアプリなので小さい (全画面の高解像度にする候補) |
| GS7 | 枠組みを gem に、image を ghcr に | gem と image の形が整い、ユーザが公開できる |

## 5. 受け入れ条件 (全体)

- asterism-games の README の冒頭で「Asterism の見本」であることと、枠組みとゲームの関係が分かる。
- どのゲームも、同じ枠組みと同じ書き方で書かれている。
- Family mruby なしで (CRuby だけで) 卓に入れる例がある。
- 部屋の守り (席の証明書、ゲームホスト自身の証明書) と、常設の卓・家の LAN の卓が、今どおり動く。

## 6. 未確定事項

- 枠組みの gem の名前と、いつ gem に切り出すか (GS7 まで同じリポジトリの中でよいか)。
- AI の打ち手の鍵 (Anthropic の API キー、Cloudflare の API トークン) の置き場所と、費用の上限。Jev の数字 (速さ・料金) は会社の発表なので、使う前に確かめる。
