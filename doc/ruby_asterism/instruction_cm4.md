# CM4 指示書: 招待リンクごとの案内文

> 状態: 指示 | 更新: 2026-10-18 | 招待リンクのページ (部屋を作るだけのページ) に、管理者が招待リンクごとに書いた案内文 (例: 日本語の遊び方の手順) を出す

前提: asterism-console の招待リンク (CM2、0.3.5: RoomInvite、Admin > Rooms の招待の作成・一覧・詳細・Revoke、`/invite` のページ `app/views/invite_pages/show.html.erb`、layouts/invite)、docs/rooms.md「Invite links」、CHANGELOG の 0.3.6 (CM3 で足した、未タグ)。

言葉: 文書は常体、「確かめ」ではなく「確認」。asterism-console の文書とコードのコメントは英語。

## 1. 決まっていること (2026-10-18 ユーザ、選択形式)

- **招待リンクごとに案内文**。管理者が招待リンクを作るときに管理画面で案内文を入れ、その招待のページに出す。相手に合わせて文を変えられ、VPS の作業は要らない。新しい招待を作るときは、直前に作った招待の案内文を初期値にする。
- console は OSS で各自が導入する。特定の運用の文 (麻雀、ブラウザの銘柄など) はコードに焼き込まない。

ユーザの文 (受け入れ確認に使う。コードや文書の例には入れない): `C:\Users\Public\asterism_deploy\invite_help_ja.md` (WSL からは `/mnt/c/Users/Public/asterism_deploy/invite_help_ja.md`)。見出し 1 行、説明 1 段落、番号つきの手順 7 行の素の文。

## 2. 作るもの

1. **移行**: RoomInvite に案内文の欄 (例 `message`、text、任意、長さの上限あり (例 4000 文字))。既存の招待は空のまま、今どおりの見え方。
2. **管理画面**: 招待の作成の画面に案内文の textarea (直前に作った招待の案内文を初期値に)。招待の詳細の画面で案内文を直せる (Revoke 済みでも直せてよいが、任せる)。一覧には出さなくてよい。
3. **招待のページ**: 案内文があれば、見出し (「Make a room」) の下・フォームの上に出す。HTML は解釈しない (エスケープ)。改行は保つ。番号つきの行・空行で区切った段落がそのまま読める程度の整形 (例 `simple_format` 相当) でよい。リンクの自動化はしない。日本語が崩れない (layouts/invite の文字の大きさ・行間)。
4. **試験**: 保存・長さの上限・エスケープ (`<script>` が文字として出る)・改行・初期値 (直前の招待の文)・既存の招待 (空) のページが今どおり。
5. **文書**: docs/rooms.md「Invite links」に案内文の説明。CHANGELOG の **0.3.6 に足す** (CM3 と一緒に出す。未タグ)。install.md の「From 0.3.5 to 0.3.6」に移行があることを足す (update.sh だけ)。

## 3. 受け入れ条件

1. console の試験全部と rubocop が通る。
2. deploy/local で、管理者が招待を作るときにユーザの文 (1 章のファイルの中身) を案内文に入れ、ログインしていないブラウザ (Playwright) で招待のページを開いて、文が崩れずに出ることを画面に写す。2 つ目の招待を作る画面で、案内文の初期値が 1 つ目の文になっていることを確認する。
3. 案内文の無い招待のページが今どおり。

## 4. 範囲・決まり

- **触ってよいもの**: asterism-console (main)。fmruby-core は report と画面だけ (ファイルとして書くだけ。fmruby-core の git 操作はしない。親がコミットする)。
- **触らないもの**: asterism・asterism-games・fmruby-core のコード、VPS、GitHub の設定、zenohd の設定。
- コミットのみ (asterism-console)。push・タグはしない。`git commit -a` は使わない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。実在の値の検査 (ドメイン・アドレス・端末 ID・鍵) をしてからコミットする。
- 終わったら起こしたもの (deploy/local、試しの image と volume、Playwright の台本) を名前で止めて消す。前からある image は残す。docker の prune はしない。親が Tab5 (シリアル、WiFi) を使うので触らない。
- **止まる条件**: 1 章に無い仕様の選択が要る (例: 案内文を Markdown として描く、言語ごとに複数の文を持つ)。止まったら report に、ユーザが選べる形の選択肢 (推奨を先頭に) を書いて返す。
- **report** (`doc/ruby_asterism/report/cm4.md`、常体): 欄・画面・整形の形、受け入れ条件の結果と画面、残ったこと。
