# R4b 指示書: 麻雀を FmrbAsterism::TableClient に載せ替える

> 状態: 指示 | 更新: 2026-10-25 | plan.md の R4 の後半。R4a で作った `FmrbAsterism::TableClient` (`lib/add/picoruby-fmrb-asterism/mrblib/table_client.rb`) に麻雀 (`flash/app/game/mahjong/`) の接続と卓の部分を載せ替える。あわせて、ユーザが決めた 2 つの直しを共通部品に入れる (2026-10-25)。**実機は使わない** (ユーザ「しばらく実機使わないように」)

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントは英語。

## 1. 決まっていること (2026-10-25 ユーザ、選択形式)

1. **join の答えを待つ間に Esc で一覧に戻ると、繋ぎ直すまで次の join が送れない不具合**を、共通部品の中で直す (オセロ・ボード・麻雀のすべてが直る)。待っていた答えを捨て、すぐ次の join を送れるようにする。
2. **ホストから誤りの答えが来たとき**は、麻雀の今の扱いに 3 つのアプリをそろえる: 返事が来た以上ホストはいるので「ホストがいない」(host gone / host away) を消す。オセロ・ボードの振る舞いはこの点だけ変わる。

## 2. 作るもの

1. 共通部品に、麻雀に要る口を足す (report/r4a.md 7 章の見通し: `view(token, assist)` の呼び方の口 (例 `table_ask_view`)、一覧の値が `{}` で始まる点、`table_dropped` で leave を送らない forget と `@mine` の片付け、`table_view_failed` で host away、`mine()` の探りは `@tc.proxy` を使ってアプリに残す、on_destroy は close だけ)。
2. 1-1・1-2 の直しを共通部品に入れ、host の試験 (`test/asterism/table_run.rb`) に場合を足す。
3. 麻雀の接続と卓の節 (約 330 行) を部品に置き換える。麻雀の本筋 (牌・鳴き・点数・観戦・牌譜・音・640/XL の画面) は変えない。行数の前後。
4. オセロ・ボードが 1-1・1-2 の直しの後も前と同じに動くこと (1-2 の変わる点を除く)。

## 3. 確認

- **手元のゲームホスト** (asterism-games の作業コピー、VPS には繋がない。AI は fake でよい) で、麻雀について前の版と並べて比べる (R4a と同じく前の版を一時的に置いてよい、後で消す): 卓の一覧 (各卓の mine の探り)、入る、座る、CPU を入れて半荘か東風の途中まで打つ (鳴き・リーチ・和了のどれかを含む)、観戦、立つ、出る、ホストを止めて戻す (続きから)、状態を消したホストに戻す (卓から外れた)、zenohd を止めて戻す。
- 1-1: 3 アプリで、join を送ってすぐ Esc → もう一度入れる。1-2: ホストに誤りの答えを返させる場面 (例: 満席・存在しない卓) で「ホストがいない」が出ない/消える。
- sim の Modern (麻雀は 640x360 の高解像度の全画面) とブラウザ版。Retro は麻雀を外しているので不要 (オセロの Retro は 1-1・1-2 の確認だけ)。
- `rake test`、標準構成の sim でエディタを 1 回起動して 1 打鍵、`rake mahjong:overflow`。
- 内蔵 RAM: Tab5・StickS3 の DIRAM が増えない (gem を変えるので前後で比べる)。VM プールの使用量 (麻雀は large_memory) を前後で並べる。

## 4. 範囲・決まり

- **触ってよいもの**: fmruby-core (develop) の table_client.rb (と link.rb は要る範囲)、麻雀、オセロ・ボード (1-1・1-2 に関わる範囲だけ)、試験、report。lib/ を触ったら `rake clean`、ターゲットの切り替えは `rake clean_all`。
- **触らないもの**: 実機 (Tab5・StickS3・Retro・シリアル)、asterism-games・asterism の gem 本体 (読む・手元で動かすだけ)、console、sdkconfig、VPS、`~/asterism_keys/`、麻雀の手描きの牌の絵 (作り直さない)。
- push・タグはしない (親がする)。`git commit -a` は使わない。件名 `asterism:` `mahjong:` `game:`、本文英語、末尾 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。実在の値を書かない。doc (この指示書・report) はコミットしない。
- 終わったら sim・web・手元のゲームホストを片付ける (前から動いていたものは残す)。
- **止まる条件**: asterism-games のホストの変更が要る、決まった 2 点以外で振る舞いを変えないと載せ替えられない、内蔵 RAM が増える。
- **report**: `doc/app_refactor/report/r4b.md` (常体): 足した口、2 つの直しと試験、麻雀の行数の前後、確認の結果、RAM と VM プール、残件 (実機での確認が要るものを含む)、コミット。
