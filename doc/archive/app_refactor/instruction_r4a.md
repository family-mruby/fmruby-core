# R4a 指示書: 卓のクライアントの共通部分を作り、オセロの卓とお絵かきボードを載せ替える

> 状態: 指示 | 更新: 2026-10-25 | plan.md の R4 の前半。オセロの卓 (`flash/app/game/othello/othello.app.rb`)・お絵かきボード (`flash/app/game/board/board.app.rb`)・麻雀 (`flash/app/game/mahjong/`) は、asterism-games のゲームホストに繋ぐ「卓のクライアント」の部分 (卓の一覧、入る・出る、席の札、様子の問い合わせ、状態の購読、ホストがいなくなったとき) をそれぞれ写して育てている。共通の部分を部品にし、まずオセロとボードを載せ替える。麻雀は R4b。置き場はファームの gem (2026-10-25 決定、R1・R2 と同じ)。振る舞いは変えない

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントは英語。

## 1. 読むもの

- 3 つのアプリの接続と卓の部分 (オセロ: `node_joined`・`subscribe_states`・`join`・`leave_table`・`ask_view`・`send_cmd`・`view_due?`・LIST_EVERY_MS・RESCAN_MS・VIEW_EVERY_MS、ボード・麻雀の同じ役)。3 つの違いを表にする (ゲームごとの部分と共通の部分の境目)。
- R1 の `FmrbAsterism::Link` (`lib/add/picoruby-fmrb-asterism/mrblib/link.rb`) と report/r1.md の「R4 への見通し」(connect の中身を link_up へ、lost の後始末を link_down へ、reconnecting? を Link の状態へ、exchange の誤りを `@link.lost` へ、RECONNECT_MS を retry_ms へ。オセロは esp32 以外で WiFi を待たない)。
- asterism-games のホスト側の卓の口 (`asterism-games/lib/asterism_games/` の game.rb・remote.rb・host.rb、othello/・board の remote)。端末側の部品はこの口と対になる。asterism-games は読むだけ。
- doc/ruby_asterism/report/gs2.md・gs13.md (卓のアプリの作りと試験のしかた)。

## 2. 作るもの

1. **卓のクライアントの部品** (例 `FmrbAsterism::TableClient`。名前と形は読んでから決めてよい。置き場は picoruby-fmrb-asterism の gem): Link の上に乗り、ゲームによらない部分を受け持つ。
   - 卓の一覧 (ホストを探す、on_join で探し直す期間と、ふだんの間隔)、卓に入る (席の札を受け取る)・出る、様子 (view) の問い合わせ (版が新しいとき・決まった間隔)、状態の購読、ホストがいなくなった・戻ったとき。
   - ゲームごとの部分 (打つ・描く・席に座る・CPU や AI の席など) はアプリに残し、部品はアプリのメソッドを名前で呼ぶ (R1 の link_up / link_down と同じく、ブロックを保持しない。picoruby・Spinel の罠を避ける)。
   - 期間の定数 (LIST_EVERY_MS など) は既定値を持ち、アプリが変えられる。
2. **載せ替え**: オセロの卓 → お絵かきボードの順。接続 (Link) と卓の部分を部品に置き換え、ゲームの本筋だけを残す。行数の前後。
3. **試験**: ホストの CRuby で、偽のホスト (卓の一覧・join・view・切断・戻り) を相手に部品の動きを試す (asterism:link_test と同じ流儀)。

## 3. 確認

- **手元のゲームホスト**で、オセロ・ボードの両方について、前後で同じに動くこと: 卓の一覧が出る、入る、座る・打つ (ボードは描く)、相手 (CPU・Jev・端末のクライアント `bin/othello-tty` など) とのやり取り、立つ、出る、ゲームホストを止めて戻したときの繋ぎ直しと卓の様子の戻り。ゲームホストの起動のしかたは gs2・gs13 の report と asterism-games の README (rake mahjong:up など)。**VPS には繋がない**。
- sim (Modern) とブラウザ版の両方。Retro で動くアプリ (オセロは Retro で全画面) は Retro の sim でも。
- `rake test` (新しい試験を含む)、標準構成の sim でエディタを 1 回起動して 1 打鍵。
- 内蔵 RAM: Tab5・Retro・StickS3 の DIRAM が増えない (gem を変えるので前後で比べる)。ファームの大きさの増分。
- 実機は使わない。

## 4. 範囲・決まり

- **触ってよいもの**: fmruby-core (develop) の picoruby-fmrb-asterism の gem、オセロの卓・ボード、試験、report。lib/ を触ったら `rake clean`、ターゲットの切り替えは `rake clean_all`。
- **触らないもの**: 麻雀 (R4b)、asterism-games・asterism の gem 本体 (読むだけ)、console、sdkconfig、実機、VPS、`~/asterism_keys/`。
- push・タグはしない。`git commit -a` は使わない。件名 `asterism:` `game:` など、本文英語、末尾 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。実在の値を書かない。doc (この指示書・report) はコミットしない (親がする)。
- 終わったら sim・web・手元のゲームホストを片付ける (前から動いていたものは残す)。
- **止まる条件**: asterism-games のホストの変更が要る、振る舞いを変えないと載せ替えられない、内蔵 RAM が増える、Spinel で動く構成があり部品が使えない。
- **report**: `doc/archive/app_refactor/report/r4a.md` (常体): 3 アプリの違いの表、部品の設計、載せ替えの前後の行数、確認の結果、RAM、麻雀 (R4b) をどう載せ替えるかの見通し、残件、コミット。
