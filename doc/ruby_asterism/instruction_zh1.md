# ZH1 指示書: console の bridge が zenohd を止める問い合わせをやめる (console 0.3.2)

> 状態: 指示 | 更新: 2026-10-17 | zenohd 1.10.1 が新しいセッションを受けなくなる件の対策 1。bridge の管理用の空間の読み方を、zenohd の錠の二重取りに当たらない形に変える。グラフの見え方は変えない

前提: asterism-console の lib/bridge/runner.rb (`read_admin`) と lib/bridge/graph.rb (`add_admin`・`link_tokens`・`parse_linkstate`)、その試験、docs/ の bridge とグラフの説明。zenohd の原因の調べは下の 1 章 (調べの報告の全文は親が持っている)。

言葉: 文書は常体。asterism-console の文書とコードのコメントは英語。

## 1. 原因 (2026-10-17 に再現・特定済み)

- zenohd 1.10.1 の zenoh/src/net/runtime/adminspace.rs で、`resources_data` (token・subscriber・publisher・queryable・querier の一覧が通る) と `linkstate_data` が、ルーティング表の読み錠 (`zread!(tables.tables)`) を持ったまま `query.reply(...).wait()` する。返答の経路の `route_send_response` が同じ読み錠をもう一度取りに行く。そのあいだに書き手 (`new_unicast`・`send_declare` など) が並ぶと、書き手を優先する Linux の RwLock の上で互いに待ったまま止まる。以後、新しいセッションは TLS の握手のあと open が時間切れになる。
- 引き金は bridge の `read_admin`: 2 秒ごとに `@/*/router/linkstate/**` と `@/*/router/token/**` を問い合わせている。bridge を止めると約 5,200 セッションの出入りでも止まらず、戻すと 4 分で止まった。ACL も TLS も無い zenohd でも同じ。
- `@/*/router` (`local_data`) は錠を外してから返すので当たらない。`route_successor` も主な道は錠を外してから返す (ただし `src/<zid>/dst/<zid>` の近道は錠の中で返すので使わない)。
- zenohd 自体の直しと上流への PR は別に進める (対策 2)。この Step は zenohd が直っていなくても止まらない bridge にする。

## 2. 作るもの

1. bridge が **`resources_data` と `linkstate_data` を通る問い合わせ (`@/*/router/linkstate/**`、`@/*/router/token/**`、`subscriber`・`publisher`・`queryable`・`querier` の一覧) と `route/successor` の近道を、一切しない** ようにする。`@/*/router` は続けてよい。
2. グラフの見え方を保つための代わり:
   - ルータどうしの線 (今は linkstate の dot から): 各ルータの `@/*/router` の `sessions` (相手がルータのもの) から作る。網の端のルータどうしの線が、今と同じに出ること。
   - ノードを運ぶセッションの線 (`carries`、今は token 表の `clients` / `peers` から): `@/*/router` の `sessions` の情報 (相手の zid、リンク、証明書の名前など、1.10.1 が出しているもの) と、bridge がすでに持っているもの (liveliness の token、自分のリンク、台帳、プロフィール) から、今と同じ線が引けるかを調べて作る。
   - **今と同じ線が引けない部分が残るなら、止まって返す** (グラフから何が消えるか、ほかの道 — asterism のプロフィールに zid を載せる等の網の決まりの変更 — を選択肢として書く)。
3. 試験: 今の bridge とグラフの試験を通し、代わりの作り方の試験を足す (`@/*/router` の JSON から、ルータどうしの線と carries の線が今と同じに出ること。今の linkstate・token 表を入れた試験の期待値と同じグラフになることを示す)。bridge が上の問い合わせをしないことを確かめる試験 (問い合わせたキーを記録して検査する、など) も足す。
4. 文書: CHANGELOG の 0.3.2 (zenohd が止まる件と、この直し。VPS の更新手順は `./update.sh 0.3.2` だけで済むこと)、docs の bridge の説明。VERSION を 0.3.2 に。

## 3. 受け入れ条件

1. asterism-console の試験全部と rubocop が通る。
2. 止まらないこと: 手元の構成 (deploy/local、zenohd は今の image の 1.10.1 のまま) で、bridge を動かしたまま、セッションの出入りを繰り返す負荷 (調べで使った形: 接続・宣言・切断を並べて繰り返す CRuby の客を数本) を **30 分** かけ、新しいセッションが開けるかを数秒ごとに試す見張りが一度も失敗しない。直す前の bridge では同じ負荷で止まることも短く確かめる (数分で止まる)。数字 (セッション数、時間、止まるまでの時間) を report に出す。
3. グラフの見え方: 手元の構成で、家のルータ (網の端のルータ) をつないだ形を含めて、直す前と後の console のグラフを写して比べる (ノード・線の数と種類が同じ)。
4. 移行が要らないこと (DB と設定の変更なし)、`./update.sh 0.3.2` だけで上がること。

## 4. 範囲・決まり

- **触ってよいもの**: asterism-console (main)。fmruby-core は文書 (この指示書の report) だけ (develop に直接)。
- **触らないもの**: zenohd の image と設定 (直しは別に進めている)、asterism・asterism-games・fmruby-core のコード、VPS、端末。
- コミットのみ。push・タグはしない。`git commit -a` は使わない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。
- 公開の文書・コミットに実在の IP・ドメイン・端末 ID・鍵を書かない。コミットの前に実在の値の検査をする。
- docker の container と compose の project は、別に動いている zenohd の直しの試し (名前に `zh-pr` を含む) とぶつからない名前・ポートにする。終わったら起こしたもの (deploy/local の一式、試しの image と volume、負荷の客) を名前で止めて消す。前からある image は残す。docker の prune はしない。
- **止まる条件**: 2 章の 2 で今と同じグラフが作れない。zenohd・asterism・網の決まりの変更が要る。1 章の見立てと違う事実が出た (例: 直した bridge でも止まる)。利用者から見た動き (グラフの見え方・画面・設定) が変わる選択が要る。
- **report** (`doc/ruby_asterism/report/zh1.md`、常体): 何をやめて何で代えたか (前と後のコードの抜粋)、`@/*/router` の sessions に 1.10.1 が出している欄 (実物の例、実在の値は伏せる)、受け入れ条件ごとの結果と数字、グラフの比較、見立てと違った点、残ったこと。
