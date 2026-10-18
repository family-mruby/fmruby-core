# CM1 指示書: console の「Relay」を「Members」に、台帳の行に表示名

> 状態: 指示 | 更新: 2026-10-18 | asterism-console の画面「Relay」(端末・人・ゲームホストの台帳、役割、証明書、ルータへの反映) の名前を「Members」に改め、台帳の行に管理者が付ける表示名の欄を足す

前提: asterism-console の app/ (relay の controllers・views・helpers、ナビゲーション)、config/routes.rb、台帳のモデル (RelayPeer など)、lib/bridge/graph.rb と app/javascript/controllers/console_controller.js の `nodeElement` (図のラベル: プロフィールの名前があれば「名前 (ID)」)、docs/ (install.md・rooms.md ほか「Admin > Relay」と書いている所)、CHANGELOG。

言葉: 文書は常体、「確かめ」ではなく「確認」。asterism-console の文書とコードのコメントは英語。

## 1. 決まっていること (2026-10-18 ユーザ、選択形式)

1. **画面の名前「Relay」を「Members」にする** (ナビゲーション・見出し・docs の「Admin > Relay」)。
2. **台帳の行に表示名の欄を足す**: 管理者が行ごとに「Tab5」「P4-Nano」のような名前を付けられる。台帳・部屋・図に「Tab5 (fmruby-aaaaaa)」の形で出す。ID (証明書の名前、ACL に使う) はそのまま。図のラベルは、台帳の表示名があればそれを、無ければ今どおり端末が名乗るプロフィールの名前を使う (どちらも ID を括弧で添える)。

## 2. 作るもの

1. 画面の名前を Members に。URL (例 `/relay/peers`) を変えるかは任せる (変えるなら古い URL から新しい URL へ転送し、ブックマークが壊れないように)。コードの中のクラス名・モジュール名 (Relay::...) は、ルータの中継の仕組みの名前として残してよい (画面の名前だけを変える)。
2. 台帳の行に表示名 (例 `display_name`、任意、長さの上限あり) を足す移行。登録と編集の画面で入れられる。一覧で表示名と ID を並べる。
3. 図のラベル: bridge が台帳の表示名をグラフのノードに渡し、`nodeElement` で「表示名 (ID)」に。台帳の表示名 → プロフィールの名前 → ID の順。部屋の中の席・ブラウザ版のラベルは今のまま。
4. 試験 (表示名の保存と表示、図のラベルの順、転送)。
5. 文書: docs の「Admin > Relay」を「Admin > Members」に、表示名の説明、CHANGELOG (0.3.5)、VERSION 0.3.5。更新の手順は `./update.sh 0.3.5` だけ (移行あり、と書く)。

## 3. 受け入れ条件

1. console の試験全部と rubocop が通る。
2. deploy/local で、Members の画面に表示名つきの行が並び、図に「表示名 (ID)」が出ることを写す。古い URL で開いても新しい画面に着く (URL を変えたなら)。
3. 移行で既存の行が壊れない (表示名は空のまま、今どおりの見え方)。

## 4. 範囲・決まり

- **触ってよいもの**: asterism-console (main)。fmruby-core は文書 (この指示書の report) だけ (develop に直接、ほかの作業がブランチを使っていたら develop を checkout しない: その場合は report のコミットを親に任せて、ファイルだけ書いて返す)。
- **触らないもの**: asterism・asterism-games・fmruby-core のコード、zenohd の設定の作り方 (ACL は変えない)、VPS。
- コミットのみ。push・タグはしない。`git commit -a` は使わない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。実在の値の検査をしてからコミットする (例に端末の実在の ID を書かない。`fmruby-aaaaaa` の形を使う)。
- 終わったら起こしたもの (deploy/local、試しの image と volume) を名前で止めて消す。前からある image は残す。docker の prune はしない。
- **止まる条件**: ACL・ルータの設定の変更が要る。1 章に無い仕様の選択が要る。止まったら report に、ユーザが選べる形の選択肢 (推奨を先頭に) を書いて返す。
- **report** (`doc/ruby_asterism/report/cm1.md`、常体): 変えた画面と URL、表示名の欄、図のラベルの順、受け入れ条件の結果と画面、残ったこと。
