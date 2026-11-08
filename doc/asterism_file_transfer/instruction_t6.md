# T6 指示書: 受け相手を console で管理し、端末は手元の写しで判断する

> 状態: 実装中 | 更新: 2026-11-09 | 正は plan.md の「決定事項 (2026-11-09 ユーザ、T6)」

## 範囲

1. **console (asterism-console)**: Members の端末の行の Edit に「ファイルを受ける相手」
   (種類 file・checkpoint ごと。Members の端末から複数選ぶ、型 `fmruby-*` も可)。T4b の
   「console と送り受け」の 2 つの印はそのまま。選んだ内容から、ルータの許可の行
   (送り手の ask `asterism/<受け手>/transfer/inbox/call/<送り手>` と、答えを通す reply・egress、
   受け手の rw `asterism/<受け手>/transfer`・`/**`) を台帳の生成で作る (Apply)。
2. **配る道**: console (bridge) が受け手の端末の転送のアプリのメソッド (例: `policy_set`) を
   呼んで、その端末の受け相手の一覧を渡す。変わったとき・端末が現れたとき・定期に配る。
   受け手は、呼び手が console のノード (設定の `console`、既定 `console`) のときだけ受け入れる。
   **安全**: 呼び手のノード ID を確かめられるのは、端末のつなぎ方が証明書つき (mTLS) のときだけ。
   証明書なしの接続 (家のルータ・P2P) では console を名乗る相手を確かめられないので、
   その接続で来た配布は受け入れない (report に書く。これは決定の「自動で受け入れる」を
   証明書つきの接続に限る安全の線で、受け入れ方の仕様は変えない)。
3. **端末 (fmruby-core の転送のアプリ)**: 受けた一覧を `receive.toml` の分けた節
   (例: `[managed.file]`・`[managed.checkpoint]`、`managed_at`・`managed_by`) に書く。手で書いた節は
   触らない。判断は手書きと managed の和。console に届かないときも、最後に書いた managed の節で
   動く (期限なし)。ESP32 の flash を書きすぎない (中身が変わったときだけ書く)。
4. 端末の画面や Shell から、今の受け相手 (手書き・managed のどちらから来たか) を見られる
   (例: Shell の `asend -l` か `receive` のようなコマンド。名前は既存に合わせ report に)。

## 確認

- console: `bin/rails test` (選ぶ画面、ACL の生成、配る側、移行)。
- fmruby-core: `rake regress`、`rake regress:transfer:quick` (managed の節で受け・断る場面を足す)、
  `rake regress:checkpoint` (実機の BOARD= は除く)。
- 手元で zenohd (mTLS と ACL のある設定、T4b の report の回し方) と console とネイティブ版の
  端末 2 つをつなぎ、console で受け相手を選ぶ → 端末の `receive.toml` に managed の節が書かれる →
  端末どうしで `asend` が通る・選んでいない相手は断られる → console を止めても同じに動く、を示す。

## 触ってよい範囲

asterism-console、asterism gem (要れば。CHANGELOG の Unreleased)、fmruby-core の転送まわりと
Shell と文書。コミットまで。push・タグ・公開・VPS はしない。

## 止まる条件

- 決定と違う動きが要る。既存の権限・台帳の作りを大きく変える必要が出た。
- 安全の前提 (2 の線) を守ると作れない所が出た。

## report

`doc/asterism_file_transfer/report/t6.md`。
