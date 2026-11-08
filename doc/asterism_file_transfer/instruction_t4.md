# T4 指示書: console (CRuby) からの送受と記録

> 状態: 実装中 | 更新: 2026-11-08 | 正は plan.md の「決定事項 (2026-11-08 ユーザ、T4 の前)」と、それより前の決定

## 範囲

1. **CRuby の受け手**: asterism gem の `Asterism::Transfer` の受け手を CRuby でも使える形に
   (端末の転送のアプリと同じ決まり: 片・再送・照合・名前・同じ名前・空き・相手 1 つ)。
   置き場所は使う側 (console) が渡す。
2. **console (`asterism-console`、Rails) の 3 つ**:
   - **ブラウザから端末へ送る**: ページで PC のファイルを選び (ブラウザから console へ
     アップロード)、ノードを選び、上書き・SD を選んで送る。進み具合と取りやめがページに出る。
     送り手は bridge (ノード `console`、`bin/bridge` が唯一のネットワークの口)。
   - **端末から console へ受ける**: bridge が `<console>/transfer/inbox` (端末と同じ受け口) を
     出し、受けたファイルを console の置き場 (`storage/` の下、gitignore、上限を設定で) に置く。
     ページで一覧と落とす (ダウンロード) ができる。受ける相手は console の設定で許す。
   - **記録**: 送った・受けたの記録 (だれ・いつ・ファイル名・大きさ・SHA-256・相手・結果) を
     DB に残し、ページで見られる。今の console の記録の作り (録画などの記録、監査の
     記録があればそれ) に合わせる。
3. **権限**: Call permissions と同じ表に「このノードへファイルを送ってよい」を足す。管理者が
   許したノードへだけ送れる。受けるのは console の設定で許した相手だけ。
4. 端末の側は今のまま (`receive.toml` の `[file]` に console のノード ID を書けば受ける)。
   README に手順を書く。

## 確認

- console の試験 (`bin/rails test` など、リポジトリの試験の道具) が通る。
- 手元 (WSL) で zenohd・console (`bin/dev`)・ネイティブ版の端末をつなぎ、
  (a) ページから端末へ 1 MB 以上を送って `/home/inbox/` に置かれる (上書き・名前を変える・
  取りやめも)、(b) 端末の `asend console <file>` (か `FmrbTransfer`) で console に届きページから
  落とせる、(c) 権限の無いノードへはページから送れない、(d) 記録がページに出る、を示す。
  画面はヘッドレスの Chromium (既存の道具があればそれ) で撮る。
- fmruby-core を変えたら `rake regress` と `rake regress:transfer:quick`。gem を変えたら
  gem の試験。

## 触ってよい範囲

`asterism-console` (コミットまで)、asterism gem の作業用 checkout (コミットまで)、
fmruby-core の転送まわりと文書 (`doc/asterism_file_transfer/` の report)。
**VPS への配置はしない** (親が検収の後に行う)。公開リポジトリへの push もしない。
console の DB の migration は足してよい (既存のデータを消さない形で)。

## 止まる条件

- plan.md の決定と違う動きが要る。仕様が変わる直し方を選ぶ必要が出た。
- console の既存の権限・記録の作りを大きく変える必要が出た。
- 実機・sudo・push・配置が要る所まで来た。

## report

`doc/asterism_file_transfer/report/t4.md` (作り、ページの使い方、権限と記録、試験の結果、
VPS へ配置するときに要る設定 (環境変数・置き場の上限・migration)、残件)。
