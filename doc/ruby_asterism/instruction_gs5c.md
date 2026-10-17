# GS5c 指示書: 麻雀の待ち合わせで、CPU・AI を席ごとに設定する

> 状態: 指示 | 更新: 2026-10-18 | 卓の麻雀のアプリの待ち合わせの画面で、CPU と AI (Claude・Jev) を下の段のボタンではなく、各席の行で設定できるようにする (2026-10-18 ユーザ決定、選択形式)

前提: report/gs5.md (`+Claude` / `+Jev` の追加、`ai_kinds`、`cpu(token, seat, on)` の on に "claude" / "jev")、卓の麻雀のアプリ (fmruby-core flash/app/game/mahjong/mahjong.app.rb の待ち合わせ、mahjong_xl.rb の 1280x720、mahjong_panels.rb)、asterism-games の app/asterism_games/mahjong/ (table.rb の `cpu`、remote.rb)。

言葉: 文書は常体。fmruby-core と asterism-games のコードのコメントは英語。

## 1. 決まっていること (2026-10-18 ユーザ、選択形式)

1. **各席の行に 1 つのボタンを置き、押すたびに 空き → CPU → Claude → Jev → 空き と巡回する**。ボタンには今の状態 (空き / CPU / Claude / Jev) を出す。鍵が無い AI (ホストの `ai_kinds` に無いもの) は巡回に出さない。操作できるのは卓の主だけ (ほかの人には状態の表示だけ)。
2. **下の段は「CPU で埋める」だけを残す** (`c` キーも)。`+Claude` / `+Jev` は消す。
3. **「座る」は、空き席と CPU・AI の席だけに出す** (CPU・AI の席に座ると入れ替わる、今の動き)。人間が座っている席には出さない。

## 2. 作るもの

1. 卓の麻雀のアプリ (640x360・426x240 の待ち合わせと、1280x720 の待ち合わせの両方): 1 章の 3 点。ボタンの文字は日本語の画面に合わせる (例:「空き」「CPU」「Claude」「Jev」)。キーでの操作も残す (例: 席を上下で選んで、決まったキーで巡回。今のキーの割り当てとぶつけない)。
2. ゲームホスト (要るときだけ): CPU が座っている席を Claude に、Claude を Jev に、Jev を空きに、と 1 回の呼び出しで替えられるか確かめ、できなければ `cpu` の呼び出しで替えられるようにする (今の呼び方は壊さない)。1 日の上限を超えて AI を断ったときは、ボタンが CPU のままで分かるように。
3. 試験 (ゲームホストを触ったなら)。

## 3. 受け入れ条件

1. asterism-games の `MJ_GAMES=8 bundle exec rake test` (触ったなら) が通る。
2. 家の LAN の卓 (偽物のつなぎの AI でよい、`ASTERISM_GAMES_AI_FAKE=1`) で、卓の主が各席のボタンで 空き→CPU→Claude→Jev→空き と巡回でき、ほかの人の画面では状態が出るだけ。`CPU で埋める` が効く。人間の席に「座る」が出ない。画面を写す (ブラウザ版 1280x720・640x360、sim 426x240)。
3. 鍵の無い AI を巡回に出さない (片方の鍵だけのホストで確認)。
4. sim の標準構成で、エディタを 1 回起動して 1 打鍵する。

## 4. 範囲・決まり

- **触ってよいもの**: fmruby-core はブランチ `feature/gs5c` (develop から) で、卓の麻雀のアプリと report。asterism-games (main) は 2 章の 2 で要るときだけ。
- **触らないもの**: オセロ・ボードのアプリ、zenohd、VPS、実機の端末、sdkconfig。
- コミットのみ。push・タグはしない。`git commit -a` は使わない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。fmruby-core のコミットの件名は `<領域>: <要約>`、本文は英文。鍵と実在の値の検査をしてからコミットする。
- fmruby-core のビルドは本体の checkout で。ブラウザ版は `source ~/emsdk/emsdk_env.sh` のあと `rake spinel:gen wasm:mruby wasm:mrb wasm:web` で作り直す。終わったら build/ は Linux の標準構成 (x86-64)、`file build/fmruby-core.elf` で確認する。
- 終わったら起こしたもの (zenohd、ゲームホスト、sim・web) を名前で止めて消す。家の LAN の zenohd は名前に `gs5c` を入れ、ポートは sim の 7447 とぶつけない。sim は音を出す前にミュート。
- **止まる条件**: 1 章に無い仕様の選択 (利用者から見た動き・既定値) が要る。止まったら report に、ユーザが選べる形の選択肢 (推奨を先頭に、各選択肢で何が変わるか一言) を書いて返す。
- **report** (`doc/ruby_asterism/report/gs5c.md`、常体): 画面の前と後、キーの割り当て、ゲームホストの変更の有無、受け入れ条件の結果と画面。
