# GS4 指示書: 状態のキー @status、網を見る CLI、Asterism を MCP のツールにする橋渡し

> 状態: 指示 | 更新: 2026-10-17 | games_showcase.md の GS4。変わり続ける状態を出す `asterism/<node>/<app>/@status` を網の決まりに足し、asterism gem に網を見る CLI と MCP の橋渡しを置く。ゲームホストは卓の数を @status で出し、console はそれを読めるようにする

前提: games_showcase.md (2.4 網を見る CLI、2.5 MCP)、report/gs1.md 4.3 (プロフィールに卓の数を載せなかった理由)・gs2b.md (console の ACL に @profile の読みを足した形 `profile_rules`)・gs3.md (居る・居ないはノード単位、という申し送り)、asterism の README・docs/protocol.md (`@profile` の決まり: 7 章、verbatim chunk)・mrblib/profile.rb・lib/asterism/cruby、asterism-console の lib/relay/cloud_config.rb・lib/bridge/、asterism-games の host.rb (`summary` の鍵 `st` / `members` / `humans` / `cpus` / `away`)。

言葉: 文書は常体。asterism・asterism-console・asterism-games の文書とコードのコメントは英語。

## 1. 決まっていること (2026-10-17 ユーザ、選択形式)

1. **@status**: `asterism/<node>/<app>/@status` に、中身は自由な Hash を、**変わったときだけ put** する。問い合わせ (get) にも今の値で答える。網の決まり (docs/protocol.md) と ACL に 1 項目ずつ足す。プロフィール (自己紹介、`asterism/<node>/@profile`) とは分ける。
2. **MCP の範囲**: 既定は見るだけ (ノード・アプリ・オブジェクト・メソッドの一覧と @status)。呼べるメソッドは起動の引数で形を明示して許す (例: `--allow 'games/othello1.*'`)。許したものだけが MCP のツールになる。
3. **CLI の入口**: 平文のルータ (`--router tcp/...`) と console の `.tar` の両方。見える範囲は `.tar` の役割のまま。管理者用の「見るだけ」の役割の作り方を docs に書く。

ほか (前から決まっていること): 網を見る CLI は asterism gem に置く (asterism-games の外、2026-10-16)。`call` は明示したときだけ。Asterism の実装は Family mruby に依存しない。

## 2. 作るもの

1. **@status** (asterism、portable の層 = mruby と CRuby の両方):
   - API の例: `Asterism.status(tables: 3, humans: 2)` (アプリごと。前の値と同じなら put しない)、CRuby の Net でも同じ (`net.status(...)`)。読む側: `Asterism.status_of(node, app)` / 購読。名前と形は asterism の今の API (`Asterism.profile` / `profile_of`) の流儀に合わせて決めてよい。
   - 中身の形 (MessagePack か JSON か) と大きさの上限は、`@profile` と同じ考え方で決め、protocol.md に書く (鍵の表・depth・wildcard の当たり方。`@status` も verbatim chunk なので `**` に当たらないことを明記)。
   - 適合の試験 (CRuby と mruby の両方、asterism の `rake test`・`test:mruby`・`test:protocol_mruby`) に足す。
2. **網を見る CLI** (asterism gem の実行ファイル、例: `asterism`。名前は任せる): `nodes` (ノードとそのアプリ・プロフィールの名前)・`objects` (オブジェクトとメソッドの一覧、meta)・`tree` (ノード → アプリ → オブジェクトの木)・`watch` (現れる・消える・@status の変化を流す)・`echo` (呼び出しの往復時間を測る、名前と中身は任せる)・`call` (明示したときだけ、引数は JSON か Ruby の literal)。入口は `--router` か `--credentials <.tar>`。出力は人が読む形と `--json` の両方。
3. **MCP の橋渡し** (asterism gem、Ruby の `mcp` gem。gem の依存は任意の依存にするなど、CLI だけ使う人に重くならない形): stdio の MCP サーバ (例: `asterism mcp --router ... --allow 'games/othello1.*'`)。見るだけのツール (一覧・meta・@status) は常に出し、呼び出しのツールは `--allow` に当たるメソッドだけ (meta の引数の数を MCP のツールの入力に写す)。
4. **ゲームホストの @status** (asterism-games): 卓ごとの数 (`summary` と同じ鍵: `st` / `members` / `humans` / `cpus` / `away`) を、変わったときに @status で出す (卓の名前・ノード ID・鍵は載せない、gs1.md の決まりと同じ)。部屋の中でも出せるか (部屋の ACL) を確かめ、出せないなら部屋では出さない。
5. **console** (asterism-console):
   - ACL: console の主体に `asterism/*/*/@status` を読む規則を自動で足す (`profile_rules` と同じ形)。書く側は役割ごとに `asterism/${node}/*/@status` を書く (docs/rooms.md の役割の雛形に足す。games-host に足す)。
   - bridge: @status を読み、図の詳細の欄 (ノードかアプリを選んだときの Details) に出す。図の見え方そのもの (ノード・線) は変えない。
   - CHANGELOG・docs・VERSION (0.3.4)。
6. **管理者用の「見るだけ」の役割** の作り方を docs に書く (console の docs と asterism の README の CLI の節)。
7. **居る・居ないはノード単位** (gs3.md の申し送り): @status か CLI の `watch` でアプリ単位の現れる・消えるが見えるかを確かめ、GS5 以降でゲームホストがアプリ単位で見られるようにするための材料を report に書く (この Step で直すことは求めない)。

## 3. 受け入れ条件

1. asterism の試験全部 (CRuby・mruby・protocol の適合)、asterism-console の試験全部と rubocop、asterism-games の `MJ_GAMES=8 bundle exec rake test` が通る。
2. 家の LAN (平文の zenohd) で: CLI の `nodes` / `objects` / `tree` / `watch` / `echo` / `call` がゲームホスト・ブラウザ版・sim の端末・bin/othello-tty の網で動く。ゲームホストを起こす・卓に人が座る・立つで `watch` に @status の変化が流れる。
3. console の deploy/local で: 管理者用の見るだけの役割の `.tar` で CLI が網全体を見られ、`call` は役割で断られる。図の Details にゲームホストの @status が出る (常設の卓のホスト。部屋は 2 章 4 の結果どおり)。
4. MCP: MCP のクライアント (例: Claude Code に stdio サーバとして登録、または mcp gem の客) から、見るだけのツールで網の一覧と @status が読める。`--allow` で許したオセロの卓のメソッド (例: `games/othello1` の join / move など) を呼んで、CPU と 1 局を終局まで打てる。許していないメソッドはツールに出ない。
5. 1 通の大きさ: @status の 1 通の最大を出す。ゲームホストの RSS が GS3 と同じくらい。

## 4. 範囲・決まり

- **触ってよいもの**: asterism (main)、asterism-console (main)、asterism-games (main)。fmruby-core は文書 (この指示書の report) だけ (develop に直接)。
- **触らないもの**: fmruby-core のコードと ASTERISM_PIN (端末の portable の層の取り込みは別の Step)、asterism-zenoh・picoruby-asterism-zenoh (足りなければ止まる)、zenohd、VPS、実機の端末。rubygems への公開・タグはしない (ユーザがする)。
- コミットのみ。push・タグはしない。`git commit -a` は使わない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。実在の値と鍵の検査をしてからコミットする。
- 終わったら起こしたもの (deploy/local、zenohd、ゲームホスト、MCP サーバ、試しの image と volume、sim・web) を名前で止めて消す。前からある image は残す。docker の prune はしない。sim は音を出す前にミュート。
- **止まる条件**: asterism-zenoh / picoruby-asterism-zenoh の変更が要る。1 章に無い仕様の選択 (利用者から見た動き・既定値・網の決まりで 1 章を越えるもの) が要る。console の図の見え方を変える必要がある。止まったら report に、ユーザが選べる形の選択肢 (推奨を先頭に、各選択肢で何が変わるか一言) を書いて返す。
- **report** (`doc/ruby_asterism/report/gs4.md`、常体): @status の決まり (protocol.md の差分の要点)、CLI と MCP の使い方の例、受け入れ条件ごとの結果と数字、見立てと違った点、アプリ単位の居る・居ないの材料、GS5 (麻雀の MCP と AI の打ち手) に向けて分かったこと、残ったこと。
