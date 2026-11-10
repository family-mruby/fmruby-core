# T8c 指示書: 公開の後の使い勝手 (開き直し・ページの文字・サインイン)

> 状態: 実装中 | 更新: 2026-11-10 | 正は plan.md の「決定事項 (2026-11-10 ユーザ、T8 の公開の後)」

公開 (console 0.3.18) の後、ユーザが VPS で試して困った 3 点を直す。

## 範囲

1. **開き直したページが確認済みの名前を引き継ぐ** (asterism-console)
   - 今: 前のページの名前が図に残っている間、または 30 秒以内に pass を出していると、console は
     `wasm-<handle>_2` を渡し、そのページは共有の接続になる (`Relay::BrowserNode`、door)。
   - 変える: 「Launch Web Studio」などで新しく開いたページに、いつも `wasm-<handle>` を渡す。door は、その
     名前で証明書の接続を持つ前のページがあれば切り、新しいページを証明書でつなぐ。前のページは共有の接続で
     つなぎ直されるか閉じる。前のページには「別のページがこの名前を引き継いだ」と分かる知らせを出す
     (door の Text の知らせに理由を足す。ページの Network の行に出す)。
   - 部屋の席・pass の 1 回きり・ほかの利用者の名前の扱いは変えない。
2. **ブラウザ版のページの下の欄** (fmruby-core wasm/web: index.html の CSS・main.js)
   - Display・Your files・Network などの欄の文字を一回り大きく (本文 14px 前後)、注記 (`.note`・`.meta` など)
     の色を背景との差 4.5:1 以上に。テレビの形の画面の見た目は変えない。
   - Network の行: 確認済み (files can go) は緑系、共有の接続 (no files) は黄系などで目立たせる。pass つきで
     開いたのにまだ WebSocket が無いときは「not connected yet (an Asterism app connects it, e.g. Receiving)」
     のように出す。
   - 端末の中の Network アプリ (ブラウザ版で「The browser handles the network here」と出すもの) にも、
     ページが確認済みかどうか (`/tmp/.asterism_door`) を 1 行出す。
3. **TOTP のブラウザを 30 日覚える** (asterism-console)
   - 番号の画面に「このブラウザを 30 日覚える」の選択 (既定は外れ)。選ぶと、署名つきの cookie (httponly・secure・
     same_site lax) と、サーバ側の記録 (利用者・作った日時・user agent。cookie の値はハッシュで持つ) を作る。
   - 次のサインインでは、パスワードが合い、その cookie が有効なら番号を聞かない。パスワードは今どおり。
   - アカウントの画面に覚えたブラウザの一覧と「消す」「全部消す」。パスワードや TOTP を変えたら全部消える。
   - 管理者による TOTP の強制など、既存の決まりは変えない。

## 確認

- console: `bin/rails test` (引き継ぎ: 2 つ目のページが名前を取り前のページが切られる、ほかの利用者の名前は
  取れない、部屋の席は今どおり。TOTP: 覚えたブラウザは番号なし、期限切れ・消した・パスワード変更後は聞く)。
  `bin/rubocop`。手元の rehearsal (T8 の report の回し方) で、同じ利用者で 2 回開き、2 つ目が確認済みになり
  1 つ目の Network の行に引き継がれたと出ることを示す。
- fmruby-core: `rake regress`、`rake regress:screens` のブラウザ版 (基準が変わるなら report に書き、基準の
  更新は別コミット)、`rake regress:transfer:quick WEB=1`。ページの下の欄を撮り、前後を scratchpad に。

## 触ってよい範囲

asterism-console、fmruby-core の wasm/web と端末の Network アプリと文書。コミットまで。push・タグ・公開・
VPS・ブラウザ版の配置はしない。実在のドメイン・IP・ホスト名を書かない。

## 止まる条件

決定と違う動きが要る、部屋・席・pass の動きを変える必要が出た、TOTP の安全の前提 (cookie を盗まれた
ときの範囲) で決めることが出た。

## report

`doc/asterism_file_transfer/report/t8c.md`。
